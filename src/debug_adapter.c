#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
    #define _WINSOCKAPI_    // stops windows.h including winsock.h
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #define sleep_ms(x) Sleep(x)
#else
    #include <unistd.h>
    #define sleep_ms(x) usleep((x) * 1000)
#endif

#include "debug_adapter.h"
#include "vm.h"
#include "compiler.h"

// --- State ---
static char current_source_path[4096];
static int breakpoints[128];
static int bp_count = 0;

// --- Logging Helper ---
// Prints to stderr so it shows up in VS Code without breaking the JSON protocol
void log_debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[Mylo Adapter] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    fflush(stderr); // Ensure it sends immediately
}

const char* get_function_name(VM* vm, int ip) {
    const char* best_name = "main";
    int best_addr = -1;
    for(int i=0; i<vm->function_count; i++) {
        if (vm->functions[i].addr <= ip && vm->functions[i].addr > best_addr) {
            best_name = vm->functions[i].name;
            best_addr = vm->functions[i].addr;
        }
    }
    return best_name;
}

// --- JSON Helpers ---
void escape_json_string(const char* input, char* output) {
    while (*input) {
        if (*input == '"') { *output++ = '\\'; *output++ = '"'; }
        else if (*input == '\\') { *output++ = '\\'; *output++ = '\\'; }
        else if (*input == '\n') { *output++ = '\\'; *output++ = 'n'; }
        else if (*input == '\r') { *output++ = '\\'; *output++ = 'r'; }
        else if (*input == '\t') { *output++ = '\\'; *output++ = 't'; }
        else { *output++ = *input; }
        input++;
    }
    *output = '\0';
}

void send_json(const char* content) {
    int len = strlen(content);
    log_debug("<< Sending: %s", content); // Log outgoing
    fprintf(stdout, "Content-Length: %d\r\n\r\n%s", len, content);
    fflush(stdout);
}

void send_event(const char* event, const char* body_json) {
    char buf[4096];
    snprintf(buf, 4096, "{\"jsonrpc\": \"2.0\", \"type\": \"event\", \"event\": \"%s\", \"body\": %s}", event, body_json);
    send_json(buf);
}

void dap_print_callback(const char* str) {
    char escaped[4096];
    escape_json_string(str, escaped);
    char buf[4096 + 128];
    snprintf(buf, sizeof(buf), "{\"category\": \"stdout\", \"output\": \"%s\"}", escaped);
    send_event("output", buf);
}


void send_response(int seq, const char* command, const char* body_json) {
    char buf[8192];
    snprintf(buf, 8192, "{\"jsonrpc\": \"2.0\", \"type\": \"response\", \"request_seq\": %d, \"command\": \"%s\", \"success\": true, \"body\": %s}", seq, command, body_json);
    send_json(buf);
}

// --- Execution Loop ---
void run_debug_session(VM* vm, bool step_mode) {
    log_debug("Starting execution loop (Step Mode: %d)", step_mode);

    while (vm->ip < vm->code_size) {
        int prev_line = (vm->lines) ? vm->lines[vm->ip] : 0;

        int op = vm_step(vm, false);
        if (op == -1) {
            log_debug("VM Terminated naturally");
            send_event("terminated", "{}");
            exit(0);
        }

        int current_line = (vm->lines) ? vm->lines[vm->ip] : 0;

        // Check if we hit a breakpoint or finished a step
        if (current_line != prev_line || step_mode) {

            // Check Breakpoints
            for(int i=0; i<bp_count; i++) {
                if (breakpoints[i] == current_line) {
                    log_debug("Hit Breakpoint at line %d", current_line);
                    send_event("stopped", "{\"reason\": \"breakpoint\", \"threadId\": 1}");
                    return;
                }
            }

            if (step_mode) {
                log_debug("Step complete at line %d", current_line);
                send_event("stopped", "{\"reason\": \"step\", \"threadId\": 1}");
                return;
            }
        }
    }

    log_debug("VM execution finished");
    send_event("terminated", "{}");
    exit(0);
}

// --- Main Adapter Loop ---
static VM* vm;
static const char* source;

void dap_error_callback(const char* str) {
    char escaped[4096];
    escape_json_string(str, escaped);
    char buf[4096 + 128];
    snprintf(buf, sizeof(buf), "{\"category\": \"stderr\", \"output\": \"%s\\n\"}", escaped);
    send_event("output", buf);
}

void start_debug_adapter(VM* vm_in, const char* filename, const char* source_content) {
    vm = vm_in;
    source = source_content;

    // Use binary mode for stdin/stdout to prevent \r\n issues on Windows
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    // Unbuffer stderr so logs appear instantly
    setvbuf(stderr, NULL, _IONBF, 0);

    log_debug("Adapter Started. Waiting for handshake...");

    MyloConfig.print_callback = dap_print_callback;
    MyloConfig.error_callback = dap_error_callback;

    if (filename) strcpy(current_source_path, filename);
    else strcpy(current_source_path, "unknown.mylo");

    char header[1024];

    while (1) {
        // 1. Read Header
        if (!fgets(header, sizeof(header), stdin)) {
            log_debug("Stdin closed or error.");
            break;
        }

        if (strncmp(header, "Content-Length: ", 16) == 0) {
            int len = atoi(header + 16);

            // Consume separator lines
            while(fgets(header, sizeof(header), stdin) && strcmp(header, "\r\n") != 0 && strcmp(header, "\n") != 0);

            // 2. Read Body
            char* body = malloc(len + 1);
            if (!body) { log_debug("OOM reading body"); break; }

            int read_len = fread(body, 1, len, stdin);
            body[read_len] = 0;

            log_debug(">> Received (%d bytes): %s", read_len, body);

            // 3. Parse Command
            int seq = 0;
            char command[64] = {0};

            char* seq_ptr = strstr(body, "\"seq\"");
            if (seq_ptr) seq = atoi(seq_ptr + 6);

            char* cmd_ptr = strstr(body, "\"command\"");
            if (cmd_ptr) {
                char* start = strchr(cmd_ptr, ':');
                if (start) {
                    start = strchr(start, '"');
                    if (start) {
                        start++;
                        char* end = strchr(start, '"');
                        if (end) strncpy(command, start, end - start);
                    }
                }
            }

            // 4. Handle Commands
            if (strcmp(command, "initialize") == 0) {
                send_response(seq, command, "{\"supportsConfigurationDoneRequest\": true}");
                send_event("initialized", "{}");
            }
            else if (strcmp(command, "launch") == 0) {
                send_response(seq, command, "{}");
                log_debug("Launch request received. Waiting for configuration.");
            }
            else if (strcmp(command, "setBreakpoints") == 0) {
                bp_count = 0;
                char* lines_ptr = strstr(body, "\"lines\"");
                if (lines_ptr) {
                    char* p = strchr(lines_ptr, '[');
                    if (p) {
                        p++;
                        while (*p && *p != ']') {
                            if (isdigit(*p)) {
                                if(bp_count < 128) breakpoints[bp_count++] = atoi(p);
                                while(isdigit(*p)) p++;
                            } else p++;
                        }
                    }
                }
                log_debug("Set %d breakpoints", bp_count);
                send_response(seq, command, "{\"breakpoints\": []}");
            }
            else if (strcmp(command, "configurationDone") == 0) {
                send_response(seq, command, "{}");

                int start_line = (vm->lines && vm->code_size > 0) ? vm->lines[vm->ip] : 0;
                bool hit_start = false;
                for(int i=0; i<bp_count; i++) if (breakpoints[i] == start_line) hit_start = true;

                if (hit_start) {
                    log_debug("Stopped at entry breakpoint (Line %d)", start_line);
                    send_event("stopped", "{\"reason\": \"breakpoint\", \"threadId\": 1}");
                } else {
                    log_debug("Starting run...");
                    send_event("stopped", "{\"reason\": \"entry\", \"threadId\": 1}");
                    // To auto-run on start, uncomment line below and remove send_event above
                    // run_debug_session(vm, false);
                }
            }
            else if (strcmp(command, "threads") == 0) {
                send_response(seq, command, "{\"threads\": [{\"id\": 1, \"name\": \"Main Thread\"}]}");
            }
            else if (strcmp(command, "stackTrace") == 0) {
                char stack[4096];
                int line = (vm->lines && vm->ip > 0) ? vm->lines[vm->ip] : 1;
                const char* fn = get_function_name(vm, vm->ip);

                char escaped_path[4096];
                escape_json_string(current_source_path, escaped_path);

                snprintf(stack, 4096, "{\"stackFrames\": [{\"id\": 1, \"name\": \"%s\", \"source\": {\"name\": \"%s\", \"path\": \"%s\"}, \"line\": %d, \"column\": 1}], \"totalFrames\": 1}",
                    fn, "source.mylo", escaped_path, line);
                send_response(seq, command, stack);
            }
            else if (strcmp(command, "scopes") == 0) {
                send_response(seq, command, "{\"scopes\": [{\"name\": \"Locals\", \"variablesReference\": 1, \"expensive\": false}, {\"name\": \"Globals\", \"variablesReference\": 2, \"expensive\": false}]}");
            }
            else if (strcmp(command, "variables") == 0) {
                int ref = 0;
                char* ref_ptr = strstr(body, "\"variablesReference\"");
                if (ref_ptr) ref = atoi(ref_ptr + 21);

                char json[8192] = "[";
                bool first = true;

                if (ref == 1) { // Locals
                    if (vm->local_symbols) {
                        for(int i=0; i<vm->local_symbol_count; i++) {
                             VMLocalInfo* sym = &vm->local_symbols[i];
                             if (vm->ip >= sym->start_ip && vm->ip <= sym->end_ip) {
                                 int stack_idx = vm->fp + sym->stack_offset;
                                 if (stack_idx <= vm->sp) {
                                     if (!first) strcat(json, ",");
                                     char item[512];
                                     double val = vm->stack[stack_idx];
                                     int type = vm->stack_types[stack_idx];

                                     if (type == T_NUM) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", sym->name, val);
                                     else if (type == T_STR) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"...\\\"\", \"variablesReference\": 0}", sym->name);
                                     else snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", sym->name);

                                     strcat(json, item);
                                     first = false;
                                 }
                             }
                        }
                    }
                }
                else if (ref == 2) { // Globals
                    if (vm->global_symbols) {
                         for(int i=0; i<vm->global_symbol_count; i++) {
                             if (!first) strcat(json, ",");
                             int addr = vm->global_symbols[i].addr;
                             double val = vm->globals[addr];
                             int type = vm->global_types[addr];
                             char item[512];

                             if (type == T_NUM) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", vm->global_symbols[i].name, val);
                             else if (type == T_STR) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"...\\\"\", \"variablesReference\": 0}", vm->global_symbols[i].name);
                             else snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", vm->global_symbols[i].name);

                             strcat(json, item);
                             first = false;
                         }
                    }
                }
                strcat(json, "]");
                char full[9000];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            }
            else if (strcmp(command, "next") == 0 || strcmp(command, "stepIn") == 0) {
                send_response(seq, command, "{}");
                run_debug_session(vm, true);
            }
            else if (strcmp(command, "continue") == 0) {
                send_response(seq, command, "{}");
                run_debug_session(vm, false);
            }
            else if (strcmp(command, "disconnect") == 0) {
                send_response(seq, command, "{}");
                exit(0);
            }
            else {
                log_debug("Unknown command: %s", command);
                send_response(seq, command, "{}");
            }

            free(body);
        }
    }
}
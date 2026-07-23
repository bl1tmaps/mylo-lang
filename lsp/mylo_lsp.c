#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <setjmp.h>
#include "vm.h"
#include "compiler.h"
#include "mylolib.h"
#include "utils.h"

// Safely converts internal 'Foo_bar' back to 'Foo::bar' for the editor
void demangle_name(const char* mangled, char* out_label, bool is_enum) {
    strcpy(out_label, mangled);
    char* sep = strrchr(out_label, '_'); // Find the last underscore
    
    // We replace the underscore if it's an enum, OR if the prefix starts 
    // with a capital letter (standard Mylo convention for Modules).
    // This protects normal snake_case variables like 'pixel_color'.
    if (sep && (is_enum || (out_label[0] >= 'A' && out_label[0] <= 'Z'))) {
        // Shift the right side of the string over by 1 to make room for '::'
        memmove(sep + 2, sep + 1, strlen(sep + 1) + 1);
        sep[0] = ':';
        sep[1] = ':';
    }
}

// Helper to send JSON-RPC responses formatted for LSP
void lsp_send_response(int id, const char* result_json) {
    char body[65536];
    snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, result_json);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    fflush(stdout); // Crucial: Neovim requires flushing to receive the message
}

void handle_completion(int id, const char* body) {
    char items[65536] = "[";
    bool first = true;

    // 1. Standard Library Functions
    // std_library is defined in mylolib.h / mylolib.c
    for (int i = 0; std_library[i].name != NULL; i++) {
        if (!first) strcat(items, ",");
        char item[256];
        // kind: 3 represents a Function/Method in LSP
        snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":3,\"sortText\":\"1\"}", std_library[i].name);
        strcat(items, item);
        first = false;
    }

    // 2. User Defined Symbols
    // Extract the file URI to read the user's current code
    char* uri_start = (char *)strstr(body, "\"uri\":\"file://");
    if (uri_start) {
        uri_start += 14; 
        char* uri_end = strchr(uri_start, '"');
        if (uri_end) {
            char filepath[1024] = {0};
            strncpy(filepath, uri_start, uri_end - uri_start);

            // Read the file and parse it to populate the compiler's symbol tables
            char* source = read_file(filepath); 
            if (source) {
                VM vm;
                vm_init(&vm); 
                compiler_reset(); 
                
                jmp_buf lsp_env;
                MyloConfig.repl_jmp_buf = &lsp_env; 
                // Note: If parse() calls exit() on a syntax error (e.g., while the user is typing),
                // the LSP will die. Neovim will automatically restart it, but for a production 
                // server, you should adapt parse() to return an error code instead.
                if (setjmp(lsp_env) == 0) {
                    parse(&vm, source); 
                }

                // --- User Functions ---
                for (int i = 0; i < func_count; i++) {
                    if (!first) strcat(items, ",");
                    char item[512], display_name[256];
                    demangle_name(funcs[i].name, display_name, false);
                    
                    snprintf(item, sizeof(item), 
                        "{\"label\":\"%s\",\"insertText\":\"%s\",\"kind\":3,\"sortText\":\"1\"}", 
                        display_name, display_name);
                    strcat(items, item);
                    first = false;
                }

                // --- User Global Variables ---
                for (int i = 0; i < global_count; i++) {
                    if (!first) strcat(items, ",");
                    char item[512], display_name[256];
                    demangle_name(globals[i].name, display_name, false);
                    
                    snprintf(item, sizeof(item), 
                        "{\"label\":\"%s\",\"insertText\":\"%s\",\"kind\":6,\"sortText\":\"2\"}", 
                        display_name, display_name);
                    strcat(items, item);
                    first = false;
                }
                // User Local Variables
                for (int i = 0; i < local_count; i++) {
                    if (!first) strcat(items, ",");
                    char item[256];
                    snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":6,\"sortText\":\"2\"}", locals[i].name);
                    strcat(items, item);
                    first = false;
                }

                for (int i = 0; i < enum_entry_count; i++) {
                    if (!first) strcat(items, ",");
                    char item[512], display_name[256];
                    // Pass true because Enums are ALWAYS namespaced
                    demangle_name(enum_entries[i].name, display_name, true); 
                    
                    snprintf(item, sizeof(item), 
                        "{\"label\":\"%s\",\"insertText\":\"%s\",\"kind\":20,\"sortText\":\"3\"}", 
                        display_name, display_name);
                    strcat(items, item);
                    first = false;
                }
                // 4. Keywords & Types
                const char* keywords[] = {
                    "fn", "cfn", "var", "if", "for", "ret", "print", "in", "struct", "else",
                    "mod", "import", "forever", "break", "continue", "enum", "true", "false",
                    "embed", "self", "monitor", "debugger", "elif",
                    // Types
                    "any", "num", "str", "f64", "f32", "i32", "i16", "i64", "byte", "bool",
                    NULL
                };

                for (int i = 0; keywords[i] != NULL; i++) {
                    if (!first) strcat(items, ",");
                    char item[256];
                    // kind: 14 is Keyword
                    snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":14,\"sortText\":\"4\"}", keywords[i]);
                    strcat(items, item);
                    first = false;
                }

                vm_cleanup(&vm);
                free(source);
            }
        }
    }

    strcat(items, "]");
    lsp_send_response(id, items);
}

int main() {
    char line[1024];
    int content_length = 0;

    // Listen for incoming LSP commands via stdin
    while (fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "Content-Length: ", 16) == 0) {
            content_length = atoi(line + 16);
        }
        
        // An empty \r\n marks the end of headers and the start of the JSON body
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) {
            if (content_length > 0) {
                char* body = malloc(content_length + 1);
                fread(body, 1, content_length, stdin);
                body[content_length] = '\0';

                // Extract Request ID
                int id = -1;
                char* id_ptr = strstr(body, "\"id\":");
                if (id_ptr) id = atoi(id_ptr + 5);

                // Handle 'initialize'
                if (strstr(body, "\"method\":\"initialize\"")) {
                    const char* init_res = "{"
                        "\"capabilities\":{"
                        "\"textDocumentSync\": 1,"
                            "\"completionProvider\":{\"resolveProvider\":false}"
                        "}"
                    "}";
                    lsp_send_response(id, init_res);
                } 
                // Handle 'completion'
                else if (strstr(body, "\"method\":\"textDocument/completion\"")) {
                    handle_completion(id, body);
                }

                free(body);
                content_length = 0;
            }
        }
    }
    return 0;
}

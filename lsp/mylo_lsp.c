#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <setjmp.h>
#include <ctype.h>
#include "vm.h"
#include "compiler.h"
#include "mylolib.h"
#include "utils.h"

#define MAX_DOCS 64
#define MAX_PATH_LEN 1024
#define MAX_ERR_LEN 1024

// ----------------------------------------------------------------------------
// In-Memory Document Store (Synchronized with editor buffers)
// ----------------------------------------------------------------------------
typedef struct {
    char uri[MAX_PATH_LEN];
    char* text;
} Document;

static Document s_docs[MAX_DOCS];
static int s_doc_count = 0;

static void doc_set(const char* uri, const char* text) {
    for (int i = 0; i < s_doc_count; i++) {
        if (strcmp(s_docs[i].uri, uri) == 0) {
            free(s_docs[i].text);
            s_docs[i].text = strdup(text);
            return;
        }
    }
    if (s_doc_count < MAX_DOCS) {
        strncpy(s_docs[s_doc_count].uri, uri, MAX_PATH_LEN - 1);
        s_docs[s_doc_count].text = strdup(text);
        s_doc_count++;
    }
}

static const char* doc_get(const char* uri) {
    for (int i = 0; i < s_doc_count; i++) {
        if (strcmp(s_docs[i].uri, uri) == 0) {
            return s_docs[i].text;
        }
    }
    return NULL;
}

static void doc_remove(const char* uri) {
    for (int i = 0; i < s_doc_count; i++) {
        if (strcmp(s_docs[i].uri, uri) == 0) {
            free(s_docs[i].text);
            s_docs[i] = s_docs[s_doc_count - 1];
            s_doc_count--;
            return;
        }
    }
}

// ----------------------------------------------------------------------------
// Diagnostic Error Capture
// ----------------------------------------------------------------------------
static char s_last_error_msg[MAX_ERR_LEN] = {0};
static int s_last_error_line = -1;
static bool s_has_error = false;

static void lsp_error_callback(const char* msg) {
    s_has_error = true;
    s_last_error_line = -1;
    s_last_error_msg[0] = '\0';

    if (!msg) return;

    // Pattern: "[Line %d] Error: %s"
    const char* p = strstr(msg, "[Line ");
    if (p) {
        p += 6;
        s_last_error_line = atoi(p);
        const char* err_start = strstr(p, "] Error: ");
        if (err_start) {
            err_start += 9;
            strncpy(s_last_error_msg, err_start, sizeof(s_last_error_msg) - 1);
        } else {
            strncpy(s_last_error_msg, msg, sizeof(s_last_error_msg) - 1);
        }
    } else {
        strncpy(s_last_error_msg, msg, sizeof(s_last_error_msg) - 1);
    }
}

// ----------------------------------------------------------------------------
// JSON-RPC Helpers
// ----------------------------------------------------------------------------
static void lsp_send_message(const char* body) {
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    fflush(stdout);
}

static void lsp_send_response(int id, const char* result_json) {
    size_t cap = strlen(result_json) + 128;
    char* body = malloc(cap);
    if (!body) return;
    snprintf(body, cap, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, result_json);
    lsp_send_message(body);
    free(body);
}

static void lsp_send_notification(const char* method, const char* params_json) {
    size_t cap = strlen(params_json) + 256;
    char* body = malloc(cap);
    if (!body) return;
    snprintf(body, cap, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}", method, params_json);
    lsp_send_message(body);
    free(body);
}

static void json_escape(const char* src, char* dest, size_t dest_size) {
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 2 < dest_size; si++) {
        switch (src[si]) {
            case '\n': if (di + 2 < dest_size) { dest[di++] = '\\'; dest[di++] = 'n'; } break;
            case '\r': if (di + 2 < dest_size) { dest[di++] = '\\'; dest[di++] = 'r'; } break;
            case '\t': if (di + 2 < dest_size) { dest[di++] = '\\'; dest[di++] = 't'; } break;
            case '"':  if (di + 2 < dest_size) { dest[di++] = '\\'; dest[di++] = '"'; } break;
            case '\\': if (di + 2 < dest_size) { dest[di++] = '\\'; dest[di++] = '\\'; } break;
            default: dest[di++] = src[si]; break;
        }
    }
    dest[di] = '\0';
}

static char* json_unescape(const char* src, size_t len) {
    char* dest = malloc(len + 1);
    if (!dest) return NULL;
    size_t di = 0;
    for (size_t si = 0; si < len; si++) {
        if (src[si] == '\\' && si + 1 < len) {
            si++;
            switch (src[si]) {
                case 'n': dest[di++] = '\n'; break;
                case 'r': dest[di++] = '\r'; break;
                case 't': dest[di++] = '\t'; break;
                case '"': dest[di++] = '"'; break;
                case '\\': dest[di++] = '\\'; break;
                case '/': dest[di++] = '/'; break;
                default: dest[di++] = src[si]; break;
            }
        } else {
            dest[di++] = src[si];
        }
    }
    dest[di] = '\0';
    return dest;
}

// Robust JSON key finder that allows arbitrary whitespace around colons
static const char* json_find_value(const char* json, const char* key) {
    if (!json || !key) return NULL;
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t nlen = strlen(needle);
    const char* p = json;
    while ((p = strstr(p, needle)) != NULL) {
        p += nlen;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == ':') {
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            return p;
        }
    }
    return NULL;
}

static bool json_extract_string(const char* json, const char* key, char* out, size_t max_out) {
    const char* val = json_find_value(json, key);
    if (!val || *val != '"') return false;
    val++; // Skip leading quote

    const char* p = val;
    while (*p) {
        if (*p == '\\' && *(p + 1)) {
            p += 2;
            continue;
        }
        if (*p == '"') break;
        p++;
    }

    size_t len = (size_t)(p - val);
    char* unescaped = json_unescape(val, len);
    if (!unescaped) return false;
    strncpy(out, unescaped, max_out - 1);
    out[max_out - 1] = '\0';
    free(unescaped);
    return true;
}

static char* json_extract_alloc_string(const char* json, const char* key) {
    const char* val = json_find_value(json, key);
    if (!val || *val != '"') return NULL;
    val++; // Skip leading quote

    const char* p = val;
    while (*p) {
        if (*p == '\\' && *(p + 1)) {
            p += 2;
            continue;
        }
        if (*p == '"') break;
        p++;
    }

    size_t len = (size_t)(p - val);
    return json_unescape(val, len);
}

static int json_extract_int(const char* json, const char* key, int default_val) {
    const char* val = json_find_value(json, key);
    if (!val) return default_val;
    return atoi(val);
}

// Convert "file:///path/to/file.mylo" to system filesystem path
static void uri_to_path(const char* uri, char* out, size_t max_out) {
    if (strncmp(uri, "file://", 7) == 0) {
        const char* p = uri + 7;
#ifdef _WIN32
        if (*p == '/' && isalpha(p[1]) && p[2] == ':') p++;
#endif
        strncpy(out, p, max_out - 1);
        out[max_out - 1] = '\0';
    } else {
        strncpy(out, uri, max_out - 1);
        out[max_out - 1] = '\0';
    }
}

// Safely converts internal 'Foo_bar' back to 'Foo::bar'
void demangle_name(const char* mangled, char* out_label, bool is_enum) {
    strcpy(out_label, mangled);
    char* sep = strrchr(out_label, '_');
    if (sep && (is_enum || (out_label[0] >= 'A' && out_label[0] <= 'Z'))) {
        memmove(sep + 2, sep + 1, strlen(sep + 1) + 1);
        sep[0] = ':';
        sep[1] = ':';
    }
}

// ----------------------------------------------------------------------------
// Diagnostics Publisher
// ----------------------------------------------------------------------------
static void publish_diagnostics(const char* uri, const char* source) {
    if (!source || !uri) return;

    VM vm;
    vm_init(&vm);
    compiler_reset();

    s_has_error = false;
    s_last_error_line = -1;
    s_last_error_msg[0] = '\0';

    MyloConfig.debug_mode = true;
    MyloConfig.error_callback = lsp_error_callback;

    jmp_buf lsp_env;
    MyloConfig.repl_jmp_buf = &lsp_env;

    char* source_copy = strdup(source);
    if (setjmp(lsp_env) == 0) {
        parse(&vm, source_copy);
    }
    free(source_copy);
    vm_cleanup(&vm);

    char params[2048];
    if (s_has_error && s_last_error_line >= 1) {
        int lsp_line = s_last_error_line - 1; // LSP lines are 0-indexed
        char esc_msg[512];
        json_escape(s_last_error_msg[0] ? s_last_error_msg : "Syntax error", esc_msg, sizeof(esc_msg));

        snprintf(params, sizeof(params),
            "{\"uri\":\"%s\",\"diagnostics\":[{"
                "\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":1000}},"
                "\"severity\":1,"
                "\"source\":\"mylo\","
                "\"message\":\"%s\""
            "}]}",
            uri, lsp_line, lsp_line, esc_msg);
    } else {
        snprintf(params, sizeof(params), "{\"uri\":\"%s\",\"diagnostics\":[]}", uri);
    }

    lsp_send_notification("textDocument/publishDiagnostics", params);
}

// ----------------------------------------------------------------------------
// Completion Handler
// ----------------------------------------------------------------------------
static void handle_completion(int id, const char* body) {
    char uri[MAX_PATH_LEN] = {0};
    json_extract_string(body, "uri", uri, sizeof(uri));

    const char* source = doc_get(uri);
    char* disk_source = NULL;
    if (!source && uri[0]) {
        char fpath[MAX_PATH_LEN];
        uri_to_path(uri, fpath, sizeof(fpath));
        disk_source = read_file(fpath);
        source = disk_source;
    }

    size_t cap = 131072;
    char* items = malloc(cap);
    strcpy(items, "[");
    bool first = true;

    // 1. Standard Library Functions
    for (int i = 0; std_library[i].name != NULL; i++) {
        if (!first) strcat(items, ",");
        char item[512];
        snprintf(item, sizeof(item),
            "{\"label\":\"%s\",\"kind\":3,\"detail\":\"fn %s(...) -> %s\",\"sortText\":\"1_%s\"}",
            std_library[i].name, std_library[i].name, std_library[i].ret_type ? std_library[i].ret_type : "void", std_library[i].name);
        if (strlen(items) + strlen(item) + 32 < cap) {
            strcat(items, item);
            first = false;
        }
    }

    // 2. User Symbols & Declarations
    if (source) {
        VM vm;
        vm_init(&vm);
        compiler_reset();

        MyloConfig.debug_mode = true;
        MyloConfig.error_callback = lsp_error_callback;

        jmp_buf lsp_env;
        MyloConfig.repl_jmp_buf = &lsp_env;

        char* copy = strdup(source);
        if (setjmp(lsp_env) == 0) {
            parse(&vm, copy);
        }
        free(copy);

        // User Functions
        for (int i = 0; i < func_count; i++) {
            if (!first) strcat(items, ",");
            char item[512], display_name[256];
            demangle_name(funcs[i].name, display_name, false);
            snprintf(item, sizeof(item),
                "{\"label\":\"%s\",\"insertText\":\"%s\",\"kind\":3,\"detail\":\"fn %s\",\"sortText\":\"2_%s\"}",
                display_name, display_name, display_name, display_name);
            if (strlen(items) + strlen(item) + 32 < cap) {
                strcat(items, item);
                first = false;
            }
        }

        // Native CFN Functions
        for (int i = 0; i < ffi_count; i++) {
            if (ffi_blocks[i].is_cfn && ffi_blocks[i].c_symbol_name[0]) {
                if (!first) strcat(items, ",");
                char item[512];
                snprintf(item, sizeof(item),
                    "{\"label\":\"%s\",\"insertText\":\"%s\",\"kind\":3,\"detail\":\"cfn %s -> %s\",\"sortText\":\"2_%s\"}",
                    ffi_blocks[i].c_symbol_name, ffi_blocks[i].c_symbol_name, ffi_blocks[i].c_symbol_name,
                    ffi_blocks[i].return_type[0] ? ffi_blocks[i].return_type : "void", ffi_blocks[i].c_symbol_name);
                if (strlen(items) + strlen(item) + 32 < cap) {
                    strcat(items, item);
                    first = false;
                }
            }
        }

        // Globals
        for (int i = 0; i < global_count; i++) {
            if (!first) strcat(items, ",");
            char item[512], display_name[256];
            demangle_name(globals[i].name, display_name, false);
            snprintf(item, sizeof(item),
                "{\"label\":\"%s\",\"insertText\":\"%s\",\"kind\":6,\"detail\":\"global %s\",\"sortText\":\"3_%s\"}",
                display_name, display_name, display_name, display_name);
            if (strlen(items) + strlen(item) + 32 < cap) {
                strcat(items, item);
                first = false;
            }
        }

        // Locals
        for (int i = 0; i < local_count; i++) {
            if (!first) strcat(items, ",");
            char item[256];
            snprintf(item, sizeof(item),
                "{\"label\":\"%s\",\"kind\":6,\"detail\":\"local %s\",\"sortText\":\"3_%s\"}",
                locals[i].name, locals[i].name, locals[i].name);
            if (strlen(items) + strlen(item) + 32 < cap) {
                strcat(items, item);
                first = false;
            }
        }

        // Enums
        for (int i = 0; i < enum_entry_count; i++) {
            if (!first) strcat(items, ",");
            char item[512], display_name[256];
            demangle_name(enum_entries[i].name, display_name, true);
            snprintf(item, sizeof(item),
                "{\"label\":\"%s\",\"insertText\":\"%s\",\"kind\":20,\"detail\":\"enum entry\",\"sortText\":\"4_%s\"}",
                display_name, display_name, display_name);
            if (strlen(items) + strlen(item) + 32 < cap) {
                strcat(items, item);
                first = false;
            }
        }

        vm_cleanup(&vm);
    }

    if (disk_source) free(disk_source);

    // 3. Keywords
    const char* keywords[] = {
        "fn", "cfn", "if", "else", "elif", "for", "in", "forever", "break",
        "continue", "ret", "print", "struct", "mod", "import", "native",
        "enum", "true", "false", "embed", "self", "monitor", "debugger",
        "region", "clear", NULL
    };
    for (int i = 0; keywords[i] != NULL; i++) {
        if (!first) strcat(items, ",");
        char item[256];
        snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":14,\"detail\":\"keyword\",\"sortText\":\"5_%s\"}", keywords[i], keywords[i]);
        if (strlen(items) + strlen(item) + 32 < cap) {
            strcat(items, item);
            first = false;
        }
    }

    // 4. Strict Types
    const char* types[] = {
        "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64",
        "f16", "f32", "f64", "bool", "str", "bytes", "any", NULL
    };
    for (int i = 0; types[i] != NULL; i++) {
        if (!first) strcat(items, ",");
        char item[256];
        snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":25,\"detail\":\"type\",\"sortText\":\"6_%s\"}", types[i], types[i]);
        if (strlen(items) + strlen(item) + 32 < cap) {
            strcat(items, item);
            first = false;
        }
    }

    strcat(items, "]");
    lsp_send_response(id, items);
    free(items);
}

// ----------------------------------------------------------------------------
// Word Extraction Helper for Hover / Definition
// ----------------------------------------------------------------------------
static bool get_word_at_pos(const char* text, int target_line, int target_char, char* out_word, size_t max_out) {
    if (!text) return false;
    int cur_line = 0;
    const char* line_start = text;
    const char* p = text;

    while (*p && cur_line < target_line) {
        if (*p == '\n') {
            cur_line++;
            line_start = p + 1;
        }
        p++;
    }
    if (cur_line != target_line) return false;

    const char* line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;

    int line_len = (int)(line_end - line_start);
    if (target_char < 0 || target_char > line_len) return false;

    const char* cursor = line_start + target_char;
    if (cursor > line_end) cursor = line_end;

    // Move back to start of identifier (supporting '::' for namespacing)
    const char* start = cursor;
    while (start > line_start) {
        char prev = *(start - 1);
        if (isalnum(prev) || prev == '_') {
            start--;
        } else if (prev == ':' && start - 2 >= line_start && *(start - 2) == ':') {
            start -= 2;
        } else {
            break;
        }
    }

    // Move forward to end of identifier
    const char* end = cursor;
    while (end < line_end) {
        char c = *end;
        if (isalnum(c) || c == '_') {
            end++;
        } else if (c == ':' && end + 1 < line_end && *(end + 1) == ':') {
            end += 2;
        } else {
            break;
        }
    }

    if (end <= start) return false;
    size_t len = (size_t)(end - start);
    if (len >= max_out) len = max_out - 1;
    strncpy(out_word, start, len);
    out_word[len] = '\0';
    return true;
}

// ----------------------------------------------------------------------------
// Hover Handler
// ----------------------------------------------------------------------------
static void handle_hover(int id, const char* body) {
    char uri[MAX_PATH_LEN] = {0};
    json_extract_string(body, "uri", uri, sizeof(uri));
    int line = json_extract_int(body, "line", 0);
    int character = json_extract_int(body, "character", 0);

    const char* source = doc_get(uri);
    char* disk_source = NULL;
    if (!source && uri[0]) {
        char fpath[MAX_PATH_LEN];
        uri_to_path(uri, fpath, sizeof(fpath));
        disk_source = read_file(fpath);
        source = disk_source;
    }

    char word[256] = {0};
    if (!source || !get_word_at_pos(source, line, character, word, sizeof(word))) {
        if (disk_source) free(disk_source);
        lsp_send_response(id, "null");
        return;
    }

    char hover_text[1024] = {0};

    // 1. Check StdLib
    for (int i = 0; std_library[i].name != NULL; i++) {
        if (strcmp(std_library[i].name, word) == 0) {
            char args_desc[256] = {0};
            for (int a = 0; a < std_library[i].arg_count; a++) {
                if (a > 0) strcat(args_desc, ", ");
                strcat(args_desc, std_library[i].arg_types[a] ? std_library[i].arg_types[a] : "any");
            }
            snprintf(hover_text, sizeof(hover_text),
                "```mylo\\nfn %s(%s) -> %s\\n```\\n*Mylo Standard Library Function*",
                std_library[i].name, args_desc, std_library[i].ret_type ? std_library[i].ret_type : "void");
            break;
        }
    }

    // 2. Check Native C functions or User symbols
    if (!hover_text[0]) {
        VM vm;
        vm_init(&vm);
        compiler_reset();
        MyloConfig.debug_mode = true;
        MyloConfig.error_callback = lsp_error_callback;
        jmp_buf lsp_env;
        MyloConfig.repl_jmp_buf = &lsp_env;

        char* copy = strdup(source);
        if (setjmp(lsp_env) == 0) {
            parse(&vm, copy);
        }
        free(copy);

        // Check user functions
        for (int i = 0; i < func_count; i++) {
            char display_name[256];
            demangle_name(funcs[i].name, display_name, false);
            if (strcmp(display_name, word) == 0 || strcmp(funcs[i].name, word) == 0) {
                int def_line = (funcs[i].addr >= 0 && funcs[i].addr < vm.code_size) ? vm.lines[funcs[i].addr] : 0;
                snprintf(hover_text, sizeof(hover_text),
                    "```mylo\\nfn %s(...)\\n```\\n*Defined on line %d*", display_name, def_line);
                break;
            }
        }

        // Check CFN
        if (!hover_text[0]) {
            for (int i = 0; i < ffi_count; i++) {
                if (ffi_blocks[i].is_cfn && strcmp(ffi_blocks[i].c_symbol_name, word) == 0) {
                    snprintf(hover_text, sizeof(hover_text),
                        "```mylo\\ncfn %s(...) -> %s\\n```\\n*Native C Function Binding*",
                        ffi_blocks[i].c_symbol_name, ffi_blocks[i].return_type[0] ? ffi_blocks[i].return_type : "void");
                    break;
                }
            }
        }

        // Check Globals
        if (!hover_text[0]) {
            for (int i = 0; i < global_count; i++) {
                char display_name[256];
                demangle_name(globals[i].name, display_name, false);
                if (strcmp(display_name, word) == 0 || strcmp(globals[i].name, word) == 0) {
                    snprintf(hover_text, sizeof(hover_text),
                        "```mylo\\nglobal %s\\n```", display_name);
                    break;
                }
            }
        }

        // Check Keywords & Types
        if (!hover_text[0]) {
            const char* types[] = { "u8","u16","u32","u64","i8","i16","i32","i64","f16","f32","f64","bool","str","bytes","any", NULL };
            for (int i = 0; types[i]; i++) {
                if (strcmp(types[i], word) == 0) {
                    snprintf(hover_text, sizeof(hover_text), "```mylo\\ntype %s\\n```\\n*Built-in primitive type*", word);
                    break;
                }
            }
        }

        vm_cleanup(&vm);
    }

    if (disk_source) free(disk_source);

    if (hover_text[0]) {
        char resp[2048];
        snprintf(resp, sizeof(resp), "{\"contents\":{\"kind\":\"markdown\",\"value\":\"%s\"}}", hover_text);
        lsp_send_response(id, resp);
    } else {
        lsp_send_response(id, "null");
    }
}

// ----------------------------------------------------------------------------
// Definition (Go-to-definition) Handler
// ----------------------------------------------------------------------------
static void handle_definition(int id, const char* body) {
    char uri[MAX_PATH_LEN] = {0};
    json_extract_string(body, "uri", uri, sizeof(uri));
    int line = json_extract_int(body, "line", 0);
    int character = json_extract_int(body, "character", 0);

    const char* source = doc_get(uri);
    char* disk_source = NULL;
    if (!source && uri[0]) {
        char fpath[MAX_PATH_LEN];
        uri_to_path(uri, fpath, sizeof(fpath));
        disk_source = read_file(fpath);
        source = disk_source;
    }

    char word[256] = {0};
    if (!source || !get_word_at_pos(source, line, character, word, sizeof(word))) {
        if (disk_source) free(disk_source);
        lsp_send_response(id, "null");
        return;
    }

    VM vm;
    vm_init(&vm);
    compiler_reset();
    MyloConfig.debug_mode = true;
    MyloConfig.error_callback = lsp_error_callback;
    jmp_buf lsp_env;
    MyloConfig.repl_jmp_buf = &lsp_env;

    char* copy = strdup(source);
    if (setjmp(lsp_env) == 0) {
        parse(&vm, copy);
    }
    free(copy);

    int target_line = -1;

    for (int i = 0; i < func_count; i++) {
        char display_name[256];
        demangle_name(funcs[i].name, display_name, false);
        if (strcmp(display_name, word) == 0 || strcmp(funcs[i].name, word) == 0) {
            if (funcs[i].addr >= 0 && funcs[i].addr < vm.code_size) {
                target_line = vm.lines[funcs[i].addr] - 1; // 0-indexed
                if (target_line < 0) target_line = 0;
            }
            break;
        }
    }

    vm_cleanup(&vm);
    if (disk_source) free(disk_source);

    if (target_line >= 0) {
        char resp[512];
        snprintf(resp, sizeof(resp),
            "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":0}}}",
            uri, target_line, target_line);
        lsp_send_response(id, resp);
    } else {
        lsp_send_response(id, "null");
    }
}

// ----------------------------------------------------------------------------
// Document Symbols Handler (Outline / Breadcrumbs)
// ----------------------------------------------------------------------------
static void handle_document_symbols(int id, const char* body) {
    char uri[MAX_PATH_LEN] = {0};
    json_extract_string(body, "uri", uri, sizeof(uri));

    const char* source = doc_get(uri);
    char* disk_source = NULL;
    if (!source && uri[0]) {
        char fpath[MAX_PATH_LEN];
        uri_to_path(uri, fpath, sizeof(fpath));
        disk_source = read_file(fpath);
        source = disk_source;
    }

    if (!source) {
        lsp_send_response(id, "[]");
        return;
    }

    VM vm;
    vm_init(&vm);
    compiler_reset();
    MyloConfig.debug_mode = true;
    MyloConfig.error_callback = lsp_error_callback;
    jmp_buf lsp_env;
    MyloConfig.repl_jmp_buf = &lsp_env;

    char* copy = strdup(source);
    if (setjmp(lsp_env) == 0) {
        parse(&vm, copy);
    }
    free(copy);

    size_t cap = 65536;
    char* symbols = malloc(cap);
    strcpy(symbols, "[");
    bool first = true;

    // Functions (Kind 12 = Function)
    for (int i = 0; i < func_count; i++) {
        char display_name[256];
        demangle_name(funcs[i].name, display_name, false);
        int l = (funcs[i].addr >= 0 && funcs[i].addr < vm.code_size)
            ? (vm.lines[funcs[i].addr] - 1) : 0;
        if (l < 0) l = 0;

        char sym[512];
        if (!first) strcat(symbols, ",");
        snprintf(sym, sizeof(sym),
            "{\"name\":\"%s\",\"kind\":12,\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":100}},"
            "\"selectionRange\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":%zu}}}",
            display_name, l, l, l, l, strlen(display_name));
        if (strlen(symbols) + strlen(sym) + 32 < cap) {
            strcat(symbols, sym);
            first = false;
        }
    }

    // Globals (Kind 13 = Variable)
    for (int i = 0; i < global_count; i++) {
        char display_name[256];
        demangle_name(globals[i].name, display_name, false);
        char sym[512];
        if (!first) strcat(symbols, ",");
        snprintf(sym, sizeof(sym),
            "{\"name\":\"%s\",\"kind\":13,\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":100}},"
            "\"selectionRange\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":%zu}}}",
            display_name, strlen(display_name));
        if (strlen(symbols) + strlen(sym) + 32 < cap) {
            strcat(symbols, sym);
            first = false;
        }
    }

    strcat(symbols, "]");
    lsp_send_response(id, symbols);

    free(symbols);
    vm_cleanup(&vm);
    if (disk_source) free(disk_source);
}

// ----------------------------------------------------------------------------
// LSP Server Main Loop
// ----------------------------------------------------------------------------
int main() {
    char line[1024];
    int content_length = 0;

    // Listen for incoming LSP messages via stdin
    while (fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "Content-Length:", 15) == 0) {
            const char* p = line + 15;
            while (*p == ' ' || *p == '\t') p++;
            content_length = atoi(p);
        }

        // An empty \r\n or \n marks the end of headers and the start of the JSON body
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) {
            if (content_length > 0) {
                char* body = malloc(content_length + 1);
                if (!body) continue;
                
                size_t total_read = 0;
                while (total_read < (size_t)content_length) {
                    size_t r = fread(body + total_read, 1, content_length - total_read, stdin);
                    if (r == 0) break;
                    total_read += r;
                }
                body[total_read] = '\0';

                // Extract Request ID (if present)
                int id = json_extract_int(body, "id", -1);

                // --- 1. Lifecycle ---
                if (strstr(body, "\"method\":\"initialize\"") || strstr(body, "\"method\": \"initialize\"")) {
                    const char* init_res = "{"
                        "\"capabilities\":{"
                            "\"textDocumentSync\": 1,"
                            "\"completionProvider\":{\"resolveProvider\":false,\"triggerCharacters\":[\":\",\".\"]},"
                            "\"hoverProvider\":true,"
                            "\"definitionProvider\":true,"
                            "\"documentSymbolProvider\":true"
                        "}"
                    "}";
                    lsp_send_response(id, init_res);
                }
                else if (strstr(body, "\"method\":\"initialized\"") || strstr(body, "\"method\": \"initialized\"")) {
                    // Client confirmation; no response needed
                }
                else if (strstr(body, "\"method\":\"shutdown\"") || strstr(body, "\"method\": \"shutdown\"")) {
                    lsp_send_response(id, "null");
                }
                else if (strstr(body, "\"method\":\"exit\"") || strstr(body, "\"method\": \"exit\"")) {
                    free(body);
                    return 0;
                }

                // --- 2. Document Synchronization ---
                else if (strstr(body, "\"method\":\"textDocument/didOpen\"") || strstr(body, "\"method\": \"textDocument/didOpen\"")) {
                    char uri[MAX_PATH_LEN] = {0};
                    json_extract_string(body, "uri", uri, sizeof(uri));
                    char* doc_text = json_extract_alloc_string(body, "text");
                    if (doc_text && uri[0]) {
                        doc_set(uri, doc_text);
                        publish_diagnostics(uri, doc_text);
                        free(doc_text);
                    }
                }
                else if (strstr(body, "\"method\":\"textDocument/didChange\"") || strstr(body, "\"method\": \"textDocument/didChange\"")) {
                    char uri[MAX_PATH_LEN] = {0};
                    json_extract_string(body, "uri", uri, sizeof(uri));
                    char* doc_text = json_extract_alloc_string(body, "text");
                    if (doc_text && uri[0]) {
                        doc_set(uri, doc_text);
                        publish_diagnostics(uri, doc_text);
                        free(doc_text);
                    }
                }
                else if (strstr(body, "\"method\":\"textDocument/didSave\"") || strstr(body, "\"method\": \"textDocument/didSave\"")) {
                    char uri[MAX_PATH_LEN] = {0};
                    json_extract_string(body, "uri", uri, sizeof(uri));
                    const char* doc_text = doc_get(uri);
                    if (doc_text && uri[0]) {
                        publish_diagnostics(uri, doc_text);
                    }
                }
                else if (strstr(body, "\"method\":\"textDocument/didClose\"") || strstr(body, "\"method\": \"textDocument/didClose\"")) {
                    char uri[MAX_PATH_LEN] = {0};
                    json_extract_string(body, "uri", uri, sizeof(uri));
                    if (uri[0]) doc_remove(uri);
                }

                // --- 3. Language Features ---
                else if (strstr(body, "\"method\":\"textDocument/completion\"") || strstr(body, "\"method\": \"textDocument/completion\"")) {
                    handle_completion(id, body);
                }
                else if (strstr(body, "\"method\":\"textDocument/hover\"") || strstr(body, "\"method\": \"textDocument/hover\"")) {
                    handle_hover(id, body);
                }
                else if (strstr(body, "\"method\":\"textDocument/definition\"") || strstr(body, "\"method\": \"textDocument/definition\"")) {
                    handle_definition(id, body);
                }
                else if (strstr(body, "\"method\":\"textDocument/documentSymbol\"") || strstr(body, "\"method\": \"textDocument/documentSymbol\"")) {
                    handle_document_symbols(id, body);
                }

                free(body);
                content_length = 0;
            }
        }
    }
    return 0;
}

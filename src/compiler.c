#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include "mylolib.h"
#include "vm.h"
#include "utils.h"
#include "defines.h"
#include "compiler.h"
#include <setjmp.h>

// --- Tokenizer & Structures ---

typedef enum {
    TK_FN, TK_CFN, TK_VAR, TK_IF, TK_FOR, TK_RET, TK_PRINT, TK_IN, TK_STRUCT, TK_ELSE,
    TK_ELIF,
    TK_MOD, TK_IMPORT, TK_FOREVER,
    TK_ID, TK_NUM, TK_STR, TK_RANGE,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_PLUS, TK_MINUS,
    TK_LT, TK_GT, TK_LE, TK_GE, TK_EQ, TK_NEQ,
    TK_EQ_ASSIGN, TK_QUESTION,
    TK_EOF, TK_FSTR, TK_BSTR,
    TK_COLON, TK_COMMA, TK_DOT,
    TK_SCOPE, // ::
    TK_ARROW, // ->
    TK_BREAK, TK_CONTINUE,
    TK_ENUM,
    TK_MODULE_PATH,
    TK_TRUE, TK_FALSE,
    TK_MUL, TK_DIV, TK_MOD_OP,
    TK_EMBED,
    TK_TYPE_DEF,
    TK_REGION,
    TK_CLEAR,
    TK_MONITOR,
    TK_DEBUGGER,
    TK_OR,
    TK_AND
} MyloTokenType;

// Token Names for pretty printing
const char *TOKEN_NAMES[] = {
    "fn", "cfn", "var", "if", "for", "ret", "print", "in", "struct", "else", "elif",
    "mod", "import", "forever",
    "Identifier", "Number", "String", "Range (...)",
    "(", ")", "{", "}", "[", "]",
    "+", "-", "<", ">", "<=", ">=", "==", "!=",
    "=", "?",
    "End of File", "Format String", "Byte String",
    ":", ",", ".", "::", "->",
    "break", "continue", "enum", "module_path",
    "true", "false", "*", "/", "%",
    "embed", "Type Definition", "region", "clear", "monitor",
    "debugger","||","&&",
};

// Mylo Exit Function
extern void mylo_exit(int code);

void match(MyloTokenType t);

const char *get_token_name(MyloTokenType t) {
    if (t < 0 || t > TK_DEBUGGER) return "Unknown";
    return TOKEN_NAMES[t];
}

typedef struct {
    MyloTokenType type;
    char text[MAX_IDENTIFIER];
    double val_float;
    int line;
    char *start;
    int length;
} Token;

typedef struct {
    char name[MAX_IDENTIFIER];
    char type[MAX_IDENTIFIER];
} FFIArg;

typedef struct {
    int id;
    char func_name[MAX_IDENTIFIER];
    char code_body[MAX_C_BLOCK_SIZE];
    FFIArg args[MAX_FFI_ARGS];
    int arg_count;
    char return_type[MAX_IDENTIFIER];
} FFIBlock;

// --- [INSERT] TypeInfo Definition ---
typedef struct {
    int id;
    bool is_array;
} TypeInfo;
// ------------------------------------

FFIBlock ffi_blocks[MAX_NATIVES];
int ffi_count = 0;
int bound_ffi_count = 0;

static char *src;
static char *current_file_start = NULL;
static Token curr;
static Token prev_token;
static bool inside_function = false;
static char current_namespace[MAX_IDENTIFIER] = "";
static int line = 1;
// NOTE: Compiler still uses a global VM instance for bytecode generation
// In a full multi-instance compilation scenario, this would be passed around.
// For now, we update it to be a struct instance, not a pointer to global scope.
static VM* compiling_vm = NULL;

char search_paths[MAX_SEARCH_PATHS][MAX_STRING_LENGTH];
int search_path_count = 0;

char c_headers[MAX_C_HEADERS][MAX_STRING_LENGTH];
int c_header_count = 0;

typedef struct {
    int break_patches[MAX_JUMPS_PER_LOOP];
    int break_count;
    int continue_patches[MAX_JUMPS_PER_LOOP];
    int continue_count;
    int scope_depth; // <-- NEW: Scope depth at loop body entry
    int local_count; // <-- NEW: Locals at loop body entry
} LoopControl;

LoopControl loop_stack[MAX_LOOP_NESTING];
int loop_depth = 0;
int current_scope_depth = 0; // <-- NEW: Compiler-wide scope tracker

Symbol globals[MAX_GLOBALS];
int global_count = 0;

LocalSymbol locals[MAX_GLOBALS];
int local_count = 0;

DebugSym debug_symbols[MAX_DEBUG_SYMBOLS];
int debug_symbol_count = 0;

FuncDebugInfo funcs[MAX_GLOBALS];
int func_count = 0;

struct StructDef {
    char name[MAX_IDENTIFIER];
    char fields[MAX_FIELDS][MAX_IDENTIFIER];
    int field_types[MAX_FIELDS]; // Added to store type IDs
    int field_count;
    char methods[MAX_FIELDS][MAX_IDENTIFIER];
    int method_count;
} struct_defs[MAX_STRUCTS];

int struct_count = 0;

EnumEntry enum_entries[MAX_ENUM_MEMBERS];
int enum_entry_count = 0;

void parse_internal(char *source, bool is_import);
void parse_struct_literal(int struct_idx);
void parse_map_literal();
void expression();
void statement();
void range_expr();
void parse_function_body(char* registered_name, char* mangled_name, int implicit_self_type);

void print_line_slice(char *start, char *end) {
    while (start < end) { fputc(*start, stderr); start++; }
    fputc('\n', stderr);
}

void print_error_context() {
    if (!current_file_start || !curr.start) return;

    char *line_start = curr.start;
    while (line_start > current_file_start && *(line_start - 1) != '\n') {
        line_start--;
    }

    char *line_end = curr.start;
    while (*line_end && *line_end != '\n') {
        line_end++;
    }

    if (line_start > current_file_start) {
        char *prev_line_end = line_start - 1;
        if (prev_line_end > current_file_start && *prev_line_end == '\r') prev_line_end--;

        char *prev_line_start = prev_line_end;
        while (prev_line_start > current_file_start && *(prev_line_start - 1) != '\n') {
            prev_line_start--;
        }

        if (prev_line_end >= prev_line_start) {
            fsetTerminalColor(stderr, MyloFgWhite, MyloBgColorDefault);
            fprintf(stderr, "    ");
            print_line_slice(prev_line_start, prev_line_end);
        }
    }

    fsetTerminalColor(stderr, MyloFgBlue, MyloBgColorDefault);
    fprintf(stderr, "    ");
    print_line_slice(line_start, line_end);

    fprintf(stderr, "    ");
    int offset = (int) (curr.start - line_start);

    for (int i = 0; i < offset; i++) {
        char c = line_start[i];
        if (c == '\t') fputc('\t', stderr);
        else fputc(' ', stderr);
    }

    fsetTerminalColor(stderr, MyloFgRed, MyloBgColorDefault);
    int len = curr.length;
    if (len <= 0) len = 1;
    for (int i = 0; i < len; i++) fputc('^', stderr);
    fprintf(stderr, "\n");

    if (*line_end) {
        char *next_line_start = line_end + 1;
        if (*next_line_start) {
            char *next_line_end = next_line_start;
            while (*next_line_end && *next_line_end != '\n') next_line_end++;

            fsetTerminalColor(stderr, MyloFgWhite, MyloBgColorDefault);
            fprintf(stderr, "    ");
            print_line_slice(next_line_start, next_line_end);
        }
    }

    fresetTerminal(stderr);
}

void error(const char *fmt, ...) {
    fprintf(stderr, "\n");
    print_error_context();
    fprintf(stderr, "\n");

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int offset = snprintf(buffer, 1024, "[Line %d] Error: ", curr.line > 0 ? curr.line : line);
    vsnprintf(buffer + offset, 1024 - offset, fmt, args);
    va_end(args);

    if (MyloConfig.debug_mode && MyloConfig.error_callback) {
        MyloConfig.error_callback(buffer);
        if (MyloConfig.repl_jmp_buf) longjmp(*(jmp_buf *) MyloConfig.repl_jmp_buf, 1);
        mylo_exit(1);
    }

    fsetTerminalColor(stderr, MyloFgRed, MyloBgColorDefault);
    fprintf(stderr, "%s\n", buffer);
    fresetTerminal(stderr);

    if (MyloConfig.repl_jmp_buf) {
        longjmp(*(jmp_buf *) MyloConfig.repl_jmp_buf, 1);
    }
    mylo_exit(1);
}

void emit(int op) {
    if (compiling_vm->code_size >= MAX_CODE) {
        fprintf(stderr, "Error: Code overflow\n");
        mylo_exit(1);
    }
    compiling_vm->bytecode[compiling_vm->code_size] = op;
    compiling_vm->lines[compiling_vm->code_size] = prev_token.line > 0 ? prev_token.line : line;
    compiling_vm->code_size++;
}

int find_local(char *name) {
    for (int i = 0; i < local_count; i++) if (strcmp(locals[i].name, name) == 0) return i;
    return -1;
}

int find_global(char *name) {
    for (int i = 0; i < global_count; i++) if (strcmp(globals[i].name, name) == 0) return i;
    return -1;
}

int find_func(char *name) {
    for (int i = 0; i < func_count; i++) if (strcmp(funcs[i].name, name) == 0) return funcs[i].addr;
    return -1;
}

int find_struct(char *name) {
    for (int i = 0; i < struct_count; i++) if (strcmp(struct_defs[i].name, name) == 0) return i;
    return -1;
}

int find_field(int struct_idx, char *field) {
    for (int i = 0; i < struct_defs[struct_idx].field_count; i++)
        if (strcmp(struct_defs[struct_idx].fields[i], field) == 0) return i;
    return -1;
}

int find_method(int struct_idx, char *method) {
    for (int i = 0; i < struct_defs[struct_idx].method_count; i++)
        if (strcmp(struct_defs[struct_idx].methods[i], method) == 0) return i;
    return -1;
}

int find_enum_val(char *name) {
    for (int i = 0; i < enum_entry_count; i++) {
        if (strcmp(enum_entries[i].name, name) == 0) return enum_entries[i].value;
    }
    return -1;
}

int find_stdlib_func(char *name) {
    int i = 0;
    while (std_library[i].name != NULL) {
        if (strcmp(std_library[i].name, name) == 0) return i;
        i++;
    }
    return -1;
}

int find_cfn(char *name) {
    for (int i = 0; i < ffi_count; i++) {
        if (strlen(ffi_blocks[i].func_name) > 0 && strcmp(ffi_blocks[i].func_name, name) == 0) return i;
    }
    return -1;
}

void get_mangled_name(char *out, char *raw_name) {
    if (strlen(current_namespace) > 0) {
        sprintf(out, "%s_%s", current_namespace, raw_name);
    } else {
        strcpy(out, raw_name);
    }
}

static void parse_cfn_decl() {
    match(TK_CFN);
    char name[MAX_IDENTIFIER];
    strcpy(name, curr.text);
    match(TK_ID);

    char mangled_name[MAX_IDENTIFIER * 2];
    get_mangled_name(mangled_name, name);

    int ffi_idx = ffi_count++;
    ffi_blocks[ffi_idx].id = ffi_idx;
    ffi_blocks[ffi_idx].arg_count = 0;
    strcpy(ffi_blocks[ffi_idx].func_name, mangled_name);
    strcpy(ffi_blocks[ffi_idx].return_type, "void");
    match(TK_LPAREN);
    while (curr.type != TK_RPAREN && curr.type != TK_EOF) {
        if (ffi_blocks[ffi_idx].arg_count >= MAX_FFI_ARGS) error("Too many FFI args");
        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].name, curr.text);
        match(TK_ID);
        match(TK_COLON);
        if (curr.type == TK_TYPE_DEF) {
            strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
            match(TK_TYPE_DEF);
        } else {
            strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
            match(TK_ID);
        }
        if (curr.type == TK_LBRACKET) {
            match(TK_LBRACKET); match(TK_RBRACKET);
            strcat(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "[]");
        }
        ffi_blocks[ffi_idx].arg_count++;
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RPAREN);

    if (curr.type == TK_ARROW) {
        match(TK_ARROW);
        if (curr.type == TK_TYPE_DEF) {
            strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
            match(TK_TYPE_DEF);
        } else {
            strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
            match(TK_ID);
        }
        if (curr.type == TK_LBRACKET) {
            match(TK_LBRACKET); match(TK_RBRACKET);
            strcat(ffi_blocks[ffi_idx].return_type, "[]");
        }
    }

    // Auto-generate the C call logic
    char call_str[MAX_C_BLOCK_SIZE] = "";
    if (strcmp(ffi_blocks[ffi_idx].return_type, "void") != 0) strcat(call_str, "return ");
    strcat(call_str, name);
    strcat(call_str, "(");
    for (int i = 0; i < ffi_blocks[ffi_idx].arg_count; i++) {
        strcat(call_str, ffi_blocks[ffi_idx].args[i].name);
        if (i < ffi_blocks[ffi_idx].arg_count - 1) strcat(call_str, ", ");
    }
    strcat(call_str, ");");
    strcpy(ffi_blocks[ffi_idx].code_body, call_str);
}

// Pass in the current depths right before entering the loop body
void push_loop(int scope_depth_at_body, int local_count_at_body) {
    if (loop_depth >= MAX_LOOP_NESTING) error("Loop nesting too deep");
    loop_stack[loop_depth].break_count = 0;
    loop_stack[loop_depth].continue_count = 0;
    loop_stack[loop_depth].scope_depth = scope_depth_at_body;
    loop_stack[loop_depth].local_count = local_count_at_body;
    loop_depth++;
}

void pop_loop(int continue_addr, int break_addr) {
    loop_depth--;
    LoopControl *loop = &loop_stack[loop_depth];
    for (int i = 0; i < loop->continue_count; i++) {
        compiling_vm->bytecode[loop->continue_patches[i]] = continue_addr;
    }
    for (int i = 0; i < loop->break_count; i++) {
        compiling_vm->bytecode[loop->break_patches[i]] = break_addr;
    }
}

void emit_break() {
    if (loop_depth == 0) error("'break' outside of loop");
    LoopControl *loop = &loop_stack[loop_depth - 1];

    // Unwind nested memory scopes
    int scopes_to_pop = current_scope_depth - loop->scope_depth;
    for (int i = 0; i < scopes_to_pop; i++) emit(OP_SCOPE_EXIT);

    // Unwind nested local variables
    if (inside_function) {
        int locals_to_pop = local_count - loop->local_count;
        for (int i = 0; i < locals_to_pop; i++) emit(OP_POP);
    }

    if (loop->break_count >= MAX_JUMPS_PER_LOOP) error("Too many 'break' statements");
    emit(OP_JMP);
    loop->break_patches[loop->break_count++] = compiling_vm->code_size;
    emit(0);
}

void emit_continue() {
    if (loop_depth == 0) error("'continue' outside of loop");
    LoopControl *loop = &loop_stack[loop_depth - 1];

    int scopes_to_pop = current_scope_depth - loop->scope_depth;
    for (int i = 0; i < scopes_to_pop; i++) emit(OP_SCOPE_EXIT);

    if (inside_function) {
        int locals_to_pop = local_count - loop->local_count;
        for (int i = 0; i < locals_to_pop; i++) emit(OP_POP);
    }

    if (loop->continue_count >= MAX_JUMPS_PER_LOOP) error("Too many 'continue' statements");
    emit(OP_JMP);
    loop->continue_patches[loop->continue_count++] = compiling_vm->code_size;
    emit(0);
}

void next_token() {
    prev_token = curr;
    while (1) {
        unsigned char c = (unsigned char) *src;
        if (isspace(c)) {
            if (c == '\n') line++;
            src++;
            continue;
        }
        if (*src == '/' && *(src + 1) == '/') {
            while (*src != '\n' && *src != '\0') src++;
            continue;
        }
        break;
    }

    curr.line = line;
    curr.start = src;

    if (*src == 'b' && *(src + 1) == '"') {
        src += 2;
        int idx = 0;
        while (*src != '"' && *src != 0) {
            if (*src == '\n') line++;
            if (*src == '\\' && *(src + 1) == 'x' && isxdigit((unsigned char)*(src+2)) && isxdigit(
                    (unsigned char)*(src+3))) {
                char c1 = *(src + 2);
                char c2 = *(src + 3);
                int v1 = (isdigit(c1) ? c1 - '0' : tolower(c1) - 'a' + 10);
                int v2 = (isdigit(c2) ? c2 - '0' : tolower(c2) - 'a' + 10);
                if (idx < MAX_STRING_LENGTH - 1) curr.text[idx++] = (char) ((v1 << 4) | v2);
                src += 4;
            } else {
                if (idx < MAX_STRING_LENGTH - 1) curr.text[idx++] = *src;
                src++;
            }
        }
        curr.text[idx] = '\0';
        if (*src == '"') src++;
        curr.type = TK_BSTR;
        curr.length = (int) (src - curr.start);
        return;
    }
    if (*src == 'f' && *(src + 1) == '"') {
        src += 2;
        char *start = src;
        int brace_depth = 0;
        while (*src) {
            if (*src == '"' && brace_depth == 0) break;
            if (*src == '\n') line++;
            if (*src == '{') brace_depth++;
            else if (*src == '}') { if (brace_depth > 0) brace_depth--; }
            src++;
        }
        int len = (int) (src - start);
        if (len > MAX_STRING_LENGTH - 1) len = MAX_STRING_LENGTH - 1;
        strncpy(curr.text, start, len);
        curr.text[len] = '\0';
        if (*src == '"') src++;
        curr.type = TK_FSTR;
        curr.length = (int) (src - curr.start);
        return;
    }

    if (*src == 0) {
        curr.type = TK_EOF;
        curr.length = 0;
        return;
    }

    if (isdigit((unsigned char)*src) || (*src == '.' && isdigit((unsigned char)*(src+1)))) {
        curr.type = TK_NUM;
        char *end;
        curr.val_float = strtod(src, &end);
        if (end > src && *(end - 1) == '.' && *end == '.') end--;
        int len = (int) (end - src);
        if (len > MAX_IDENTIFIER - 1) len = MAX_IDENTIFIER - 1;
        strncpy(curr.text, src, len);
        curr.text[len] = '\0';
        src = end;
        curr.length = (int) (src - curr.start);
        return;
    }

    if (isalpha((unsigned char)*src)) {
        char *start = src;
        while (isalnum((unsigned char)*src) || *src == '_') src++;
        int len = (int) (src - start);
        if (len > MAX_STRING_LENGTH - 1) len = MAX_STRING_LENGTH - 1;
        strncpy(curr.text, start, len);
        curr.text[len] = '\0';
        if (strcmp(curr.text, "fn") == 0) curr.type = TK_FN;
        else if (strcmp(curr.text, "struct") == 0) curr.type = TK_STRUCT;
        else if (strcmp(curr.text, "cfn") == 0) curr.type = TK_CFN;
        else if (strcmp(curr.text, "var") == 0) curr.type = TK_VAR;
        else if (strcmp(curr.text, "if") == 0) curr.type = TK_IF;
        else if (strcmp(curr.text, "else") == 0) curr.type = TK_ELSE;
        else if (strcmp(curr.text, "elif") == 0) curr.type = TK_ELIF;
        else if (strcmp(curr.text, "for") == 0) curr.type = TK_FOR;
        else if (strcmp(curr.text, "in") == 0) curr.type = TK_IN;
        else if (strcmp(curr.text, "ret") == 0) curr.type = TK_RET;
        else if (strcmp(curr.text, "print") == 0) curr.type = TK_PRINT;
        else if (strcmp(curr.text, "mod") == 0) curr.type = TK_MOD;
        else if (strcmp(curr.text, "import") == 0) curr.type = TK_IMPORT;
        else if (strcmp(curr.text, "break") == 0) curr.type = TK_BREAK;
        else if (strcmp(curr.text, "continue") == 0) curr.type = TK_CONTINUE;
        else if (strcmp(curr.text, "enum") == 0) curr.type = TK_ENUM;
        else if (strcmp(curr.text, "module_path") == 0) curr.type = TK_MODULE_PATH;
        else if (strcmp(curr.text, "true") == 0) curr.type = TK_TRUE;
        else if (strcmp(curr.text, "false") == 0) curr.type = TK_FALSE;
        else if (strcmp(curr.text, "forever") == 0) curr.type = TK_FOREVER;
        else if (strcmp(curr.text, "embed") == 0) curr.type = TK_EMBED;
        else if (strcmp(curr.text, "region") == 0) curr.type = TK_REGION;
        else if (strcmp(curr.text, "clear") == 0) curr.type = TK_CLEAR;
        else if (strcmp(curr.text, "monitor") == 0) curr.type = TK_MONITOR;
        else if (strcmp(curr.text, "debugger") == 0) curr.type = TK_DEBUGGER;
        else if (strcmp(curr.text, "any") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "num") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "str") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "f64") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "f32") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "i32") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "i16") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "i64") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "byte") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "bool") == 0) curr.type = TK_TYPE_DEF;
        else curr.type = TK_ID;
        curr.length = (int) (src - curr.start);
        return;
    }
    if (*src == '"') {
        src++;
        char *start = src;
        while (*src != '"' && *src != 0) {
            if (*src == '\n') line++;
            src++;
        }
        int len = (int) (src - start);
        if (len > MAX_STRING_LENGTH - 1) len = MAX_STRING_LENGTH - 1;
        strncpy(curr.text, start, len);
        curr.text[len] = '\0';
        if (*src == '"') src++;
        curr.type = TK_STR;
        curr.length = (int) (src - curr.start);
        return;
    }
    if (strncmp(src, "...", 3) == 0) {
        src += 3;
        curr.type = TK_RANGE;
        curr.length = 3;
        return;
    }
    if (strncmp(src, "::", 2) == 0) {
        src += 2;
        curr.type = TK_SCOPE;
        curr.length = 2;
        return;
    }
    if (strncmp(src, "->", 2) == 0) {
        src += 2;
        curr.type = TK_ARROW;
        curr.length = 2;
        return;
    }

    switch (*src++) {
        case '(': curr.type = TK_LPAREN; break;
        case ')': curr.type = TK_RPAREN; break;
        case '{': curr.type = TK_LBRACE; break;
        case '}': curr.type = TK_RBRACE; break;
        case '[': curr.type = TK_LBRACKET; break;
        case ']': curr.type = TK_RBRACKET; break;
        case '+': curr.type = TK_PLUS; break;
        case '-': curr.type = TK_MINUS; break;
        case '*': curr.type = TK_MUL; break;
        case '/': curr.type = TK_DIV; break;
        case '%': curr.type = TK_MOD_OP; break;
        case ':': curr.type = TK_COLON; break;
        case ',': curr.type = TK_COMMA; break;
        case '.': curr.type = TK_DOT; break;
        case '?': curr.type = TK_QUESTION; break;
        case '<': if (*src == '=') { src++; curr.type = TK_LE; } else curr.type = TK_LT; break;
        case '>': if (*src == '=') { src++; curr.type = TK_GE; } else curr.type = TK_GT; break;
        case '!': if (*src == '=') { src++; curr.type = TK_NEQ; } else error("Unexpected char '!'"); break;
        case '=': if (*src == '=') { src++; curr.type = TK_EQ; } else curr.type = TK_EQ_ASSIGN; break;
        case '|': if (*src == '|') { src++; curr.type = TK_OR; } else error("Unexpected char '|'"); break;
        case '&': if (*src == '&') { src++; curr.type = TK_AND; } else { error("Unexpected char '&' (Did you mean &&?)");} break;
        default: error("Unknown char '%c'", *(src - 1));
    }
    curr.length = (int) (src - curr.start);
}

int get_type_id_from_token(const char *txt) {
    if (strcmp(txt, "any") == 0) return TYPE_ANY;
    if (strcmp(txt, "num") == 0) return TYPE_NUM;
    if (strcmp(txt, "str") == 0) return TYPE_STR;
    if (strcmp(txt, "byte") == 0) return TYPE_BYTES;
    if (strcmp(txt, "i32") == 0) return TYPE_I32_ARRAY;
    if (strcmp(txt, "f32") == 0) return TYPE_F32_ARRAY;
    if (strcmp(txt, "i16") == 0) return TYPE_I16_ARRAY;
    if (strcmp(txt, "i64") == 0) return TYPE_I64_ARRAY;
    if (strcmp(txt, "bool") == 0) return TYPE_BOOL_ARRAY;
    return TYPE_ARRAY;
}

void match(MyloTokenType t) {
    if (curr.type == t) next_token();
    else error("Expected '%s', got '%s'", get_token_name(t), get_token_name(curr.type));
}

bool parse_namespaced_id(char *out_name) {
    strcpy(out_name, curr.text);

    if (curr.type == TK_TYPE_DEF) {
        match(TK_TYPE_DEF);
        return false;
    }

    match(TK_ID);
    if (curr.type == TK_SCOPE) {
        match(TK_SCOPE);
        char sub[64];
        strcpy(sub, curr.text);
        match(TK_ID);
        char combined[128];
        sprintf(combined, "%s_%s", out_name, sub);
        strcpy(out_name, combined);
        return true;
    }
    return false;
}

void compiler_reset() {
    global_count = 0;
    local_count = 0;
    func_count = 0;
    struct_count = 0;
    enum_entry_count = 0;
    ffi_count = 0;
    bound_ffi_count = 0;
    debug_symbol_count = 0;
    loop_depth = 0;
    current_scope_depth = 0;
    current_namespace[0] = '\0';
    search_path_count = 0;
    c_header_count = 0;
    line = 1;
    inside_function = false;
}

TypeInfo parse_type_spec() {
    TypeInfo info = {TYPE_ANY, false};
    if (curr.type == TK_TYPE_DEF) {
        info.id = get_type_id_from_token(curr.text);
        match(TK_TYPE_DEF);
    } else {
        char tn[MAX_IDENTIFIER];
        parse_namespaced_id(tn);
        int explicit_struct = find_struct(tn);
        if (explicit_struct == -1) {
             char m[MAX_IDENTIFIER * 2];
             get_mangled_name(m, tn);
             explicit_struct = find_struct(m);
        }
        if(explicit_struct != -1) info.id = explicit_struct;
    }

    if (curr.type == TK_LBRACKET) {
        match(TK_LBRACKET);
        match(TK_RBRACKET);
        info.is_array = true;
    }
    return info;
}


static void c_gen_headers(FILE *fp) {
    fprintf(fp, "// Generated by Mylo Compiler\n");
    fprintf(fp, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n");

    fprintf(fp, "\n// --- USER IMPORTS ---\n");
    for (int i = 0; i < c_header_count; i++) {
        if (c_headers[i][0] == '<') fprintf(fp, "#include %s\n", c_headers[i]);
        else fprintf(fp, "#include \"%s\"\n", c_headers[i]);
    }
    fprintf(fp, "#include \"vm.h\"\n#include \"mylolib.h\"\n\n");
}

static void c_gen_structs(FILE *fp) {
    fprintf(fp, "// --- GENERATED C STRUCTS ---\n");
    for (int i = 0; i < struct_count; i++) {
        fprintf(fp, "typedef struct { ");
        for (int j = 0; j < struct_defs[i].field_count; j++) {
            // FIX: VM storage is ALWAYS double (either a number or a pointer/ref)
            // We ignore type_id here for storage layout, as C needs to match the VM's 8-byte alignment.
            fprintf(fp, "double %s; ", struct_defs[i].fields[j]);
        }
        fprintf(fp, "} c_%s;\n", struct_defs[i].name);
    }
    fprintf(fp, "\n");
}

static void c_gen_type_name(FILE* fp, const char* type, bool is_return) {
    if (strcmp(type, "num") == 0) fprintf(fp, "double");
    else if (strcmp(type, "void") == 0) fprintf(fp, "void");
    else if (strcmp(type, "string") == 0 || strcmp(type, "str") == 0) fprintf(fp, "char*");
    else if (strcmp(type, "i32") == 0) fprintf(fp, "int");
    else if (strcmp(type, "i64") == 0) fprintf(fp, "long long");
    else if (strcmp(type, "f32") == 0) fprintf(fp, "float");
    else if (strcmp(type, "i16") == 0) fprintf(fp, "short");
    else if (strcmp(type, "bool") == 0 || strcmp(type, "byte") == 0) fprintf(fp, "unsigned char");

    else if (strstr(type, "[]")) {
        if (is_return) {
            fprintf(fp, "MyloReturn");
        } else {
            if (strstr(type, "byte") || strstr(type, "bool")) fprintf(fp, "unsigned char*");
            else if (strstr(type, "i32")) fprintf(fp, "int*");
            else if (strstr(type, "f32")) fprintf(fp, "float*");
            else fprintf(fp, "void*");
        }
    }
    else if (strlen(type) > 0) fprintf(fp, "c_%s", type);
    else fprintf(fp, "MyloReturn");
}

static void c_gen_ffi_wrappers(FILE *fp) {
    for (int i = 0; i < ffi_count; i++) {
        c_gen_type_name(fp, ffi_blocks[i].return_type, true);
        fprintf(fp, " __mylo_user_%d(", i);
        // Added VM* argument to generated C functions for access
        fprintf(fp, "VM* vm");
        if (ffi_blocks[i].arg_count > 0) fprintf(fp, ", ");

        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            c_gen_type_name(fp, type, false);
             if (strlen(type) > 0 && strcmp(type, "num") != 0 && !strstr(type, "[]") &&
                 strcmp(type, "i32")!=0 && strcmp(type, "i64")!=0 && strcmp(type, "f32")!=0 &&
                 strcmp(type, "i16")!=0 && strcmp(type, "bool")!=0 && strcmp(type, "byte")!=0 &&
                 strcmp(type, "str")!=0 && strcmp(type, "string")!=0 && strcmp(type, "bytes")!=0) {
                 fprintf(fp, "*");
             }
            fprintf(fp, " %s", ffi_blocks[i].args[a].name);
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ") {\n%s\n}\n\n", ffi_blocks[i].code_body);
    }

    fprintf(fp, "// --- NATIVE WRAPPERS ---\n");
    for (int i = 0; i < ffi_count; i++) {
        fprintf(fp, "void __wrapper_%d(VM* vm) {\n", i);
        for (int a = ffi_blocks[i].arg_count - 1; a >= 0; a--) {
            fprintf(fp, "    double _raw_%s = vm_pop(vm);\n", ffi_blocks[i].args[a].name);
        }

        char *ret_type = ffi_blocks[i].return_type;
        if (strcmp(ret_type, "void") != 0) {
            c_gen_type_name(fp, ret_type, false);
            fprintf(fp, " res = ");
        }
        fprintf(fp, "__mylo_user_%d(vm", i);
        if (ffi_blocks[i].arg_count > 0) fprintf(fp, ", ");

        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            char *name = ffi_blocks[i].args[a].name;
            if (strcmp(type, "str") == 0 || strcmp(type, "string") == 0) fprintf(
                fp, "vm->string_pool[(int)_raw_%s]", name);
            else if (strcmp(type, "num") == 0) fprintf(fp, "_raw_%s", name);
            else if (strcmp(type, "i32") == 0) fprintf(fp, "(int)_raw_%s", name);
            else if (strcmp(type, "i64") == 0) fprintf(fp, "(long long)_raw_%s", name);
            else if (strcmp(type, "f32") == 0) fprintf(fp, "(float)_raw_%s", name);
            else if (strcmp(type, "i16") == 0) fprintf(fp, "(short)_raw_%s", name);
            else if (strcmp(type, "bool") == 0 || strcmp(type, "byte") == 0) fprintf(
                fp, "(unsigned char)_raw_%s", name);
            else if (strcmp(type, "bytes") == 0) fprintf(
                fp, "(unsigned char*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_ARRAY)", name);
            else if (strcmp(type, "byte[]") == 0 || strcmp(type, "bool[]") == 0) fprintf(fp, "(unsigned char*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_ARRAY)", name);
            else if (strcmp(type, "i32[]") == 0) fprintf(fp, "(int*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_ARRAY)", name);
            else if (strcmp(type, "i64[]") == 0) fprintf(fp, "(long long*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_ARRAY)", name);
            else if (strcmp(type, "f32[]") == 0) fprintf(fp, "(float*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_ARRAY)", name);
            else if (strcmp(type, "i16[]") == 0) fprintf(fp, "(short*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_ARRAY)", name);
            else if (strstr(type, "[]")) fprintf(fp, "(void*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_ARRAY)", name);
            else fprintf(fp, "(c_%s*)(vm_resolve_ptr(vm, _raw_%s) + HEAP_HEADER_STRUCT)", type, name);
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ");\n");

        if (strcmp(ret_type, "num") == 0) fprintf(fp, "    vm_push(vm, res, T_NUM);\n");
        else if (strcmp(ret_type, "void") == 0) fprintf(fp, "    vm_push(vm, 0.0, T_NUM);\n");
        else if (strcmp(ret_type, "string") == 0 || strcmp(ret_type, "str") == 0) fprintf(
            fp, "    int id = make_string(vm, res);\n    vm_push(vm, (double)id, T_STR);\n");
        else if (strcmp(ret_type, "i32") == 0 || strcmp(ret_type, "i64") == 0 || strcmp(ret_type, "f32") == 0 ||
                 strcmp(ret_type, "i16") == 0 || strcmp(ret_type, "byte") == 0 || strcmp(ret_type, "bool") == 0)
            fprintf(fp, "    vm_push(vm, (double)res, T_NUM);\n");
        else if (strlen(ret_type) > 0) {

            if (strstr(ret_type, "[]")) {
                fprintf(fp, "    vm_push(vm, res.value, res.type);\n");
            }
            else {
                int st_idx = -1;
                for (int s = 0; s < struct_count; s++) if (strcmp(struct_defs[s].name, ret_type) == 0) st_idx = s;
                if (st_idx != -1) {
                    fprintf(fp, "    double ptr = heap_alloc(vm, %d + HEAP_HEADER_STRUCT);\n", struct_defs[st_idx].field_count);
                    fprintf(fp, "    double* base = vm_resolve_ptr(vm, ptr);\n");
                    fprintf(fp, "    base[HEAP_OFFSET_TYPE] = %d.0;\n", st_idx);
                    for (int f = 0; f < struct_defs[st_idx].field_count; f++) fprintf(
                        fp, "    base[HEAP_HEADER_STRUCT + %d] = res.%s;\n", f, struct_defs[st_idx].fields[f]);
                    fprintf(fp, "    vm_push(vm, ptr, T_OBJ);\n");
                } else {
                    fprintf(fp, "    vm_push(vm, 0.0, T_OBJ);\n");
                }
            }

        } else fprintf(fp, "    vm_push(vm, res.value, res.type);\n");
        fprintf(fp, "}\n");
    }
}

void factor() {
    if (curr.type == TK_NUM) {
        int idx = make_const(compiling_vm, curr.val_float);
        emit(OP_PSH_NUM); emit(idx);
        match(TK_NUM);
    } else if (curr.type == TK_STR) {
        int id = make_string(compiling_vm, curr.text);
        emit(OP_PSH_STR); emit(id);
        match(TK_STR);
    } else if (curr.type == TK_TRUE) {
        emit(OP_PSH_NUM); emit(make_const(compiling_vm, 1.0));
        match(TK_TRUE);
    } else if (curr.type == TK_FALSE) {
        emit(OP_PSH_NUM); emit(make_const(compiling_vm, 0.0));
        match(TK_FALSE);
    } else if (curr.type == TK_MINUS) {
        match(TK_MINUS);
        if (curr.type == TK_NUM) {
            int idx = make_const(compiling_vm, -curr.val_float);
            emit(OP_PSH_NUM); emit(idx);
            match(TK_NUM);
        } else {
            emit(OP_PSH_NUM); emit(make_const(compiling_vm, 0.0));
            factor();
            emit(OP_SUB);
        }
    } else if (curr.type == TK_LBRACKET) {
        match(TK_LBRACKET);
        int count = 0;
        if (curr.type != TK_RBRACKET) {
            expression(); count++;
            while (curr.type == TK_COMMA) { match(TK_COMMA); expression(); count++; }
        }
        match(TK_RBRACKET);
        emit(OP_ARR); emit(count);
    } else if (curr.type == TK_LBRACE) {
        char *safe_src = src; Token safe_curr = curr; int safe_line = line;
        match(TK_LBRACE);
        bool is_map = (curr.type == TK_STR || curr.type == TK_RBRACE);
        src = safe_src; curr = safe_curr; line = safe_line;
        if (is_map) parse_map_literal();
        else {
            match(TK_LBRACE);
            char first_field[MAX_IDENTIFIER]; strcpy(first_field, curr.text);
            src = safe_src; curr = safe_curr; line = safe_line;
            int st_idx = -1;
            for (int s = 0; s < struct_count; s++) {
                if (find_field(s, first_field) != -1) { st_idx = s; break; }
            }
            if (st_idx != -1) parse_struct_literal(st_idx);
            else error("Could not infer struct type from field '%s'", first_field);
        }
    } else if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) {
        match(TK_ID);
        int ffi_idx = ffi_count++;
        ffi_blocks[ffi_idx].id = ffi_idx;
        ffi_blocks[ffi_idx].func_name[0] = '\0';
        ffi_blocks[ffi_idx].arg_count = 0;
        ffi_blocks[ffi_idx].return_type[0] = '\0';

        if (curr.type == TK_LPAREN) {
            match(TK_LPAREN);
            while (curr.type != TK_RPAREN) {
                if (ffi_blocks[ffi_idx].arg_count >= MAX_FFI_ARGS) mylo_exit(1);
                strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].name, curr.text);
                match(TK_ID);
                if (curr.type == TK_COLON) {
                    match(TK_COLON);
                    if (curr.type == TK_TYPE_DEF) {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_TYPE_DEF);
                    } else {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_ID);
                    }
                    if (curr.type == TK_LBRACKET) {
                        match(TK_LBRACKET); match(TK_RBRACKET);
                        strcat(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "[]");
                    }
                } else strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "num");
                match(TK_EQ_ASSIGN);
                expression();
                ffi_blocks[ffi_idx].arg_count++;
                if (curr.type == TK_COMMA) match(TK_COMMA);
            }
            match(TK_RPAREN);
        }

        match(TK_ARROW);
        if (curr.type == TK_TYPE_DEF) {
            strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
            match(TK_TYPE_DEF);
        } else {
            strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
            match(TK_ID);
        }

        if (curr.type == TK_LBRACKET) {
            match(TK_LBRACKET);
            match(TK_RBRACKET);
            strcat(ffi_blocks[ffi_idx].return_type, "[]");
        }

        if (curr.type != TK_LBRACE) mylo_exit(1);
        char *start = src + 1;
        int braces = 1;
        char *end = start;
        while (*end && braces > 0) {
            if (*end == '{') braces++;
            if (*end == '}') braces--;
            if (*end == '\n') line++;
            if (braces > 0) end++;
        }
        int len = (int) (end - start);
        if (len >= MAX_C_BLOCK_SIZE) len = MAX_C_BLOCK_SIZE - 1;
        strncpy(ffi_blocks[ffi_idx].code_body, start, len);
        ffi_blocks[ffi_idx].code_body[len] = '\0';
        src = end + 1;
        next_token();
        emit(OP_NATIVE);
        int std_count = 0;
        while (std_library[std_count].name != NULL) std_count++;
        emit(std_count + ffi_idx);
    }     
    
    else if (curr.type == TK_FSTR) {
        int empty_id = make_string(compiling_vm, "");
        emit(OP_PSH_STR); emit(empty_id);
        
        // --- THE FIX: Isolate the string so recursive tokenization doesn't corrupt it ---
        char safe_fstr[MAX_STRING_LENGTH];
        strncpy(safe_fstr, curr.text, MAX_STRING_LENGTH);
        safe_fstr[MAX_STRING_LENGTH - 1] = '\0';
        
        char *raw = safe_fstr; 
        char *ptr = raw; 
        char *start = ptr;
        // --------------------------------------------------------------------------------
        
        while (*ptr) {
            if (*ptr == '{') {
                if (ptr > start) {
                    char chunk[MAX_STRING_LENGTH]; int len = (int) (ptr - start);
                    if (len >= MAX_STRING_LENGTH) len = MAX_STRING_LENGTH - 1;
                    strncpy(chunk, start, len); chunk[len] = '\0';
                    int id = make_string(compiling_vm, chunk); emit(OP_PSH_STR); emit(id); emit(OP_CAT);
                }
                ptr++; char *expr_start = ptr;
                while (*ptr && *ptr != '}') ptr++;
                if (*ptr == '}') {
                    char expr_code[256]; int len = (int) (ptr - expr_start);
                    if (len >= 256) len = 255;
                    strncpy(expr_code, expr_start, len); expr_code[len] = '\0';
                    char *old_src = src; Token old_token = curr; int old_line = line;
                    
                    src = expr_code; next_token(); expression();
                    
                    src = old_src; curr = old_token; line = old_line;
                    emit(OP_CAT);
                    ptr++; start = ptr;
                }
            } else ptr++;
        }
        if (ptr > start) {
            char chunk[MAX_STRING_LENGTH]; int len = (int) (ptr - start);
            if (len >= MAX_STRING_LENGTH) len = MAX_STRING_LENGTH - 1;
            strncpy(chunk, start, len); chunk[len] = '\0';
            int id = make_string(compiling_vm, chunk); emit(OP_PSH_STR); emit(id); emit(OP_CAT);
        }
        match(TK_FSTR);

    } else if (curr.type == TK_BSTR) {
        int id = make_string(compiling_vm, curr.text);
        emit(OP_PSH_STR); emit(id); emit(OP_MK_BYTES);
        match(TK_BSTR);
    } else if (curr.type == TK_ID) {
        Token start_token = curr;
        char name[MAX_IDENTIFIER];
        parse_namespaced_id(name);

        int enum_val = find_enum_val(name);
        if (enum_val != -1) {
            char type_name[MAX_IDENTIFIER];
            char short_name[MAX_IDENTIFIER];

            // Extract the Type name and Member name (e.g., "Foo" and "bar" from "Foo_bar")
            char* underscore_pos = strchr(name, '_');
            if (underscore_pos) {
                int type_len = underscore_pos - name;
                strncpy(type_name, name, type_len);
                type_name[type_len] = '\0';
                strcpy(short_name, underscore_pos + 1);
            } else {
                // Fallback just in case
                strcpy(type_name, "unknown");
                strcpy(short_name, name);
            }

            // Get String IDs for both
            int type_str_id = make_string(compiling_vm, type_name);
            int member_str_id = make_string(compiling_vm, short_name);

            // Pack the Type ID, Member ID, and Integer value into a single 64-bit unsigned long long (using 48 bits)
            unsigned long long packed = ((unsigned long long)type_str_id << 32) |
                                        ((unsigned long long)member_str_id << 16) |
                                        (enum_val & 0xFFFF);

            int idx = make_const(compiling_vm, (double)packed);
            emit(OP_PSH_ENUM);
            emit(idx);
            return;
        }
        if (curr.type == TK_LPAREN) {
            match(TK_LPAREN);
            int arg_count = 0;
            if (curr.type != TK_RPAREN) {
                expression(); arg_count++;
                while (curr.type == TK_COMMA) { match(TK_COMMA); expression(); arg_count++; }
            }
            match(TK_RPAREN);
            int faddr = find_func(name);
            if (faddr != -1) { emit(OP_CALL); emit(faddr); emit(arg_count); return; }
            int std_idx = find_stdlib_func(name);
            if (std_idx != -1) {
                if (std_library[std_idx].arg_count != arg_count) error("StdLib function '%s' expects %d args", name, std_library[std_idx].arg_count);
                emit(OP_NATIVE); emit(std_idx); return;
            }

            int cfn_idx = find_cfn(name);
            if (cfn_idx != -1) {
                if (ffi_blocks[cfn_idx].arg_count != arg_count) error("CFN function '%s' expects %d args", name, ffi_blocks[cfn_idx].arg_count);
                int std_count = 0;
                while (std_library[std_count].name != NULL) std_count++;
                emit(OP_NATIVE); emit(std_count + cfn_idx); return;
            }

            char m[MAX_IDENTIFIER * 2]; get_mangled_name(m, name);
            faddr = find_func(m);
            if (faddr != -1) { emit(OP_CALL); emit(faddr); emit(arg_count); return; }
            curr = start_token; error("Undefined function '%s'", name);
        } else {
            int loc = find_local(name); int type_id = -1; bool is_array = false;
            if (loc != -1) {
                emit(OP_LVAR); emit(locals[loc].offset); type_id = locals[loc].type_id; is_array = locals[loc].is_array;
            } else {
                int glob = find_global(name);
                if (glob == -1) { char m[MAX_IDENTIFIER * 2]; get_mangled_name(m, name); glob = find_global(m); }
                if (glob == -1) error("Undefined var '%s'", name);
                emit(OP_GET); emit(globals[glob].addr); type_id = globals[glob].type_id; is_array = globals[glob].is_array;
            }
            while (curr.type == TK_DOT || curr.type == TK_LBRACKET) {
                if (curr.type == TK_DOT) {
                    match(TK_DOT); char f[MAX_IDENTIFIER]; strcpy(f, curr.text); match(TK_ID);

                    if (type_id < 0) error("Accessing member '%s' of untyped/primitive var.", f);

                    int offset = find_field(type_id, f);
                    if (offset == -1) {
                        int method_idx = find_method(type_id, f);
                        if (method_idx != -1) {
                            if (curr.type != TK_LPAREN) error("Method '%s' must be called", f);
                            match(TK_LPAREN);
                            int arg_count = 1; // 'self' is implicitly pushed by the previous emit(OP_LVAR) or emit(OP_GET)
                            if (curr.type != TK_RPAREN) {
                                expression(); arg_count++;
                                while (curr.type == TK_COMMA) { match(TK_COMMA); expression(); arg_count++; }
                            }
                            match(TK_RPAREN);
                            char full_name[MAX_IDENTIFIER * 2];
                            sprintf(full_name, "%s_%s", struct_defs[type_id].name, f);
                            int faddr = find_func(full_name);
                            if (faddr == -1) error("Undefined method '%s'", full_name);
                            emit(OP_CALL); emit(faddr); emit(arg_count);
                            type_id = -1; // Methods don't have statically typed return values right now
                            continue;
                        }
                        error("Struct '%s' has no field or method '%s'", struct_defs[type_id].name, f);
                    }
                    int field_type = struct_defs[type_id].field_types[offset];
                    emit(OP_HGET); emit(offset); emit(type_id);
                    type_id = field_type; // Propagate the type of the field to the next iteration
                } else if (curr.type == TK_LBRACKET) {
                    match(TK_LBRACKET); expression();
                    if (curr.type == TK_COLON) { match(TK_COLON); expression(); match(TK_RBRACKET); emit(OP_SLICE); }
                    else { match(TK_RBRACKET); emit(OP_AGET); if (is_array) is_array = false; else type_id = -1; }
                }
            }
        }
    } else if (curr.type == TK_LPAREN) {
        match(TK_LPAREN); expression(); match(TK_RPAREN);
    } else error("Unexpected token '%s' in expression", curr.text);
}


void term() {
    factor();
    while (curr.type == TK_MUL || curr.type == TK_DIV || curr.type == TK_MOD_OP) {
        MyloTokenType op = curr.type;
        next_token();
        factor();
        switch (op) {
            case TK_MUL: emit(OP_MUL);
                break;
            case TK_DIV: emit(OP_DIV);
                break;
            case TK_MOD_OP: emit(OP_MOD);
                break;
            default: break;
        }
    }
}

void additive_expr() {
    term(); // Parses multiplication/division first
    while (curr.type == TK_PLUS || curr.type == TK_MINUS) {
        MyloTokenType op = curr.type;
        next_token();
        term();
        switch (op) {
            case TK_PLUS:
                // The VM's exec_math_op already checks if the
                // operands are T_STR and performs concatenation.
                emit(OP_ADD);
                break;
            case TK_MINUS:
                emit(OP_SUB);
                break;
            default: break;
        }
    }
}

void relation_expr() {
    range_expr();
    while (curr.type >= TK_LT && curr.type <= TK_NEQ) {
        MyloTokenType op = curr.type;
        next_token();
        range_expr();
        switch (op) {
            case TK_LT: emit(OP_LT);
                break;
            case TK_GT: emit(OP_GT);
                break;
            case TK_LE: emit(OP_LE);
                break;
            case TK_GE: emit(OP_GE);
                break;
            case TK_EQ: emit(OP_EQ);
                break;
            case TK_NEQ: emit(OP_NEQ);
                break;
            default: break;
        }
    }
}

void range_expr() {
    additive_expr();
    if (curr.type == TK_RANGE) {
        match(TK_RANGE);
        additive_expr();
        emit(OP_RANGE);
    }
}
// 1. Create the AND expression parser
void logic_and_expr() {
    relation_expr();
    while (curr.type == TK_AND) {
        match(TK_AND);
        relation_expr();
        emit(OP_AND);
    }
}

// 2. Update logic_or_expr to point to logic_and_expr instead of relation_expr
void logic_or_expr() {
    logic_and_expr(); //
    while (curr.type == TK_OR) {
        match(TK_OR);
        logic_and_expr();
        emit(OP_OR);
    }
}

void expression() {
    logic_or_expr();
    if (curr.type == TK_QUESTION) {
        match(TK_QUESTION);
        emit(OP_JZ);
        int p1 = compiling_vm->code_size;
        emit(0);
        expression();
        emit(OP_JMP);
        int p2 = compiling_vm->code_size;
        emit(0);
        compiling_vm->bytecode[p1] = compiling_vm->code_size;
        match(TK_ELSE);
        expression();
        compiling_vm->bytecode[p2] = compiling_vm->code_size;
    }
}

#define EMIT_SET(is_loc, addr) if(is_loc) { emit(OP_SVAR); emit(addr); } else { emit(OP_SET); emit(addr); }
#define EMIT_GET(is_loc, addr) if(is_loc) { emit(OP_LVAR); emit(addr); } else { emit(OP_GET); emit(addr); }

int alloc_var(bool is_loc, char *name, int type_id, bool is_array) {
    if (is_loc) {
        if (name) strcpy(locals[local_count].name, name);
        locals[local_count].offset = local_count;
        locals[local_count].type_id = type_id;
        locals[local_count].is_array = is_array;
        if (debug_symbol_count < MAX_DEBUG_SYMBOLS) {
            if (name) strcpy(debug_symbols[debug_symbol_count].name, name);
            debug_symbols[debug_symbol_count].stack_offset = local_count;
            debug_symbols[debug_symbol_count].start_ip = compiling_vm->code_size;
            debug_symbols[debug_symbol_count].end_ip = -1;
            debug_symbol_count++;
        }
        return local_count++;
    }

    char m[MAX_IDENTIFIER * 2];
    get_mangled_name(m, name);

    for (int i = 0; i < global_count; i++) {
        if (strcmp(globals[i].name, m) == 0) {
            globals[i].type_id = type_id;
            globals[i].is_array = is_array;
            return i;
        }
    }

    if (name) strcpy(globals[global_count].name, m);
    globals[global_count].addr = global_count;
    globals[global_count].type_id = type_id;
    globals[global_count].is_array = is_array;
    return global_count++;
}

int get_var_addr(char *n, bool is_local, int explicit_type) {
    if (is_local) {
        int loc = find_local(n);
        if (loc != -1) return loc;
        emit(OP_PSH_NUM);
        emit(make_const(compiling_vm, 0.0));
        return alloc_var(true, n, explicit_type, false);
    } else {
        char m[MAX_IDENTIFIER * 2];
        get_mangled_name(m, n);
        int glob = find_global(m);
        if (glob != -1) return glob;
        return alloc_var(false, n, explicit_type, false);
    }
}

static void parse_region() {
    match(TK_REGION);
    char name[MAX_IDENTIFIER];
    strcpy(name, curr.text);
    match(TK_ID);
    emit(OP_NEW_ARENA);
    int var_idx = alloc_var(inside_function, name, TYPE_ANY, false);
    if (inside_function) {
        emit(OP_SVAR); emit(locals[var_idx].offset);
    } else {
        emit(OP_SET); emit(globals[var_idx].addr);
    }
}

static void parse_print() {
    match(TK_PRINT);
    match(TK_LPAREN);
    expression();

    match(TK_RPAREN);
    emit(OP_PRN);
}

static void parse_c_block_stmt() {
    match(TK_ID);
    int ffi_idx = ffi_count++;
    ffi_blocks[ffi_idx].id = ffi_idx; 
    ffi_blocks[ffi_idx].func_name[0] = '\0';
    ffi_blocks[ffi_idx].arg_count = 0;
    strcpy(ffi_blocks[ffi_idx].return_type, "void");

    if (curr.type == TK_LPAREN) {
        match(TK_LPAREN);
        while (curr.type != TK_RPAREN) {
            if (ffi_blocks[ffi_idx].arg_count >= MAX_FFI_ARGS) mylo_exit(1);
            strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].name, curr.text);
            match(TK_ID);
            if (curr.type == TK_COLON) {
                match(TK_COLON);
                if (curr.type == TK_TYPE_DEF) {
                    strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                    match(TK_TYPE_DEF);
                } else {
                    strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                    match(TK_ID);
                }
                if (curr.type == TK_LBRACKET) {
                     match(TK_LBRACKET); match(TK_RBRACKET);
                     strcat(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "[]");
                }
            } else strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "num");
            match(TK_EQ_ASSIGN);
            expression();
            ffi_blocks[ffi_idx].arg_count++;
            if (curr.type == TK_COMMA) match(TK_COMMA);
        }
        match(TK_RPAREN);
    }

    if (curr.type == TK_ARROW) {
        match(TK_ARROW);
        if (curr.type == TK_TYPE_DEF) {
            strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
            match(TK_TYPE_DEF);
        } else {
            strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
            match(TK_ID);
        }

        if (curr.type == TK_LBRACKET) {
            match(TK_LBRACKET);
            match(TK_RBRACKET);
            strcat(ffi_blocks[ffi_idx].return_type, "[]");
        }
    }

    if (curr.type != TK_LBRACE) error("Expected '{'");

    char *start = src + 1;
    int braces = 1;
    char *end = start;
    while (*end && braces > 0) {
        if (*end == '{') braces++;
        if (*end == '}') braces--;
        if (*end == '\n') line++;
        if (braces > 0) end++;
    }
    int len = (int) (end - start);
    if (len >= MAX_C_BLOCK_SIZE) len = MAX_C_BLOCK_SIZE - 1;
    strncpy(ffi_blocks[ffi_idx].code_body, start, len);
    ffi_blocks[ffi_idx].code_body[len] = '\0';
    src = end + 1;
    next_token();
    emit(OP_NATIVE);
    int std_count = 0;
    while (std_library[std_count].name != NULL) std_count++;
    emit(std_count + ffi_idx);
    emit(OP_POP);
}
static void parse_import() {
    match(TK_IMPORT);

    if (curr.type == TK_ID && strcmp(curr.text, "native") == 0) {
        match(TK_ID);
        if (curr.type != TK_STR) error("Expected filename");
        char filename[MAX_STRING_LENGTH];
        strcpy(filename, curr.text);
        match(TK_STR);
        int std_count = 0;
        while (std_library[std_count].name != NULL) std_count++;
        int start_ffi_index = ffi_count;
        char *c = read_file(filename);
        if (!c) {
            for (int i = 0; i < search_path_count; i++) {
                char tmp[MAX_STRING_LENGTH];
                sprintf(tmp, "%s/%s", search_paths[i], filename);
                c = read_file(tmp);
                if (c) break;
            }
        }
        if (!c) error("Cannot find native import '%s'", filename);
        parse_internal(c, true);
        if (!MyloConfig.build_mode) {
            int added_natives = ffi_count - start_ffi_index;
            char lib_name[MAX_STRING_LENGTH];
            get_lib_name(lib_name, filename);
            if (!MyloConfig.debug_mode) fprintf(stderr, "Mylo: Loading Native Module '%s'...\n", lib_name);
            void *lib = load_library(lib_name);
            if (!lib) error("Could not load native binary '%s'", lib_name);
            typedef void (*BindFunc)(VM *, int, MyloAPI *);
            BindFunc binder = (BindFunc) get_symbol(lib, "mylo_bind_lib");
            if (!binder) error("Native module '%s' invalid", lib_name);
            MyloAPI api;
            api.push = vm_push;
            api.pop = vm_pop;
            api.make_string = make_string;
            api.heap_alloc = heap_alloc;
            api.resolve_ptr = vm_resolve_ptr;
            api.store_copy = vm_store_copy;
            api.store_ptr = vm_store_ptr;
            api.get_ref = vm_get_ref;
            api.free_ref = vm_free_ref;
            api.natives_array = compiling_vm->natives;
            api.string_pool = compiling_vm->string_pool;
            binder(compiling_vm, std_count + start_ffi_index, &api);
            bound_ffi_count += added_natives;

            if (compiling_vm->dependency_count < MAX_DEPENDENCIES) {
                Dependency* dep = &compiling_vm->dependencies[compiling_vm->dependency_count++];
                // Save the calculated library name (lib_name was derived earlier in this function)
                strcpy(dep->name, lib_name);
                dep->start_index = std_count + start_ffi_index;
            }
        }
    }
    else if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) {
        match(TK_ID);
        if (curr.type == TK_STR) {
            if (c_header_count < MAX_C_HEADERS) strcpy(c_headers[c_header_count++], curr.text);
            match(TK_STR);
            return;
        }
        int ffi_idx = ffi_count++;
        ffi_blocks[ffi_idx].id = ffi_idx;
        ffi_blocks[ffi_idx].func_name[0] = '\0';
        ffi_blocks[ffi_idx].arg_count = 0;
        strcpy(ffi_blocks[ffi_idx].return_type, "void");
        if (curr.type == TK_LPAREN) {
            match(TK_LPAREN);
            while (curr.type != TK_RPAREN) {
                if (ffi_blocks[ffi_idx].arg_count >= MAX_FFI_ARGS) mylo_exit(1);
                strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].name, curr.text);
                match(TK_ID);
                if (curr.type == TK_COLON) {
                    match(TK_COLON);
                    if (curr.type == TK_TYPE_DEF) {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_TYPE_DEF);
                    } else {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_ID);
                    }
                    if (curr.type == TK_LBRACKET) {
                         match(TK_LBRACKET); match(TK_RBRACKET);
                         strcat(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "[]");
                    }
                } else strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "num");
                match(TK_EQ_ASSIGN);
                expression();
                ffi_blocks[ffi_idx].arg_count++;
                if (curr.type == TK_COMMA) match(TK_COMMA);
            }
            match(TK_RPAREN);
        }
        if (curr.type == TK_ARROW) {
            match(TK_ARROW);
            if (curr.type == TK_TYPE_DEF) {
                 strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                 match(TK_TYPE_DEF);
            } else {
                 strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                 match(TK_ID);
            }
        }
        if (curr.type != TK_LBRACE) error("Expected '{'");
        char *start = src + 1;
        int braces = 1;
        char *end = start;
        while (*end && braces > 0) {
            if (*end == '{') braces++;
            if (*end == '}') braces--;
            if (*end == '\n') line++;
            if (braces > 0) end++;
        }
        int len = (int) (end - start);
        if (len >= MAX_C_BLOCK_SIZE) len = MAX_C_BLOCK_SIZE - 1;
        strncpy(ffi_blocks[ffi_idx].code_body, start, len);
        ffi_blocks[ffi_idx].code_body[len] = '\0';
        src = end + 1;
        next_token();
        emit(OP_NATIVE);
        int std_count = 0;
        while (std_library[std_count].name != NULL) std_count++;
        emit(std_count + ffi_idx);
        emit(OP_POP);
    }
    else {
        char f[MAX_STRING_LENGTH];
        strcpy(f, curr.text);
        match(TK_STR);
        char *c = read_file(f);
        if (!c) {
            for (int i = 0; i < search_path_count; i++) {
                char tmp[MAX_STRING_LENGTH];
                sprintf(tmp, "%s/%s", search_paths[i], f);
                c = read_file(tmp);
                if (c) break;
            }
        }
        if (c) parse_internal(c, true);
        else error("Cannot find import '%s'", f);
    }
}

static void parse_var_decl() {
    match(TK_VAR);
    char name[MAX_IDENTIFIER];
    strcpy(name, curr.text);
    match(TK_ID);
    bool specific_region = false;
    char region_var_name[MAX_IDENTIFIER];

    if (curr.type == TK_SCOPE) {
        match(TK_SCOPE);
        strcpy(region_var_name, name);
        specific_region = true;
        char member_name[MAX_IDENTIFIER];
        strcpy(member_name, curr.text);
        match(TK_ID);
        sprintf(name, "%s_%s", region_var_name, member_name);

        int reg_loc = find_local(region_var_name);
        if (reg_loc != -1) {
            emit(OP_LVAR);
            emit(locals[reg_loc].offset);
        } else {
            int reg_glob = find_global(region_var_name);
            if (reg_glob != -1) {
                emit(OP_GET);
                emit(globals[reg_glob].addr);
            } else error("Undefined region");
        }
        emit(OP_SET_CTX);
    }

    TypeInfo type_info = {TYPE_ANY, false};
    if (curr.type == TK_COLON) {
        match(TK_COLON);
        type_info = parse_type_spec();
    }
    match(TK_EQ_ASSIGN);

    bool handled = false;

    if (type_info.is_array && type_info.id != TYPE_ANY && type_info.id < 0 && curr.type == TK_LBRACKET) {
        match(TK_LBRACKET);
        int count = 0;
        if (curr.type != TK_RBRACKET) {
            do {
                expression();
                count++;
                if (curr.type == TK_COMMA) match(TK_COMMA);
            } while (curr.type != TK_RBRACKET && curr.type != TK_EOF);
        }
        match(TK_RBRACKET);
        emit(OP_MAKE_ARR);
        emit(count);
        emit(type_info.id);
        handled = true;
    }
    else if (type_info.is_array && type_info.id >= 0 && curr.type == TK_LBRACKET) {
        match(TK_LBRACKET);
        int count = 0;
        if (curr.type != TK_RBRACKET) {
            do {
                if (curr.type == TK_LBRACE) parse_struct_literal(type_info.id);
                else expression();
                count++;
                if (curr.type == TK_COMMA) match(TK_COMMA);
            } while (curr.type != TK_RBRACKET && curr.type != TK_EOF);
        }
        match(TK_RBRACKET);
        emit(OP_ARR);
        emit(count);
        handled = true;
    }
    else if (type_info.id >= 0 && !type_info.is_array && curr.type == TK_LBRACE) {
        parse_struct_literal(type_info.id);
        handled = true;
    }
    else if (type_info.id == TYPE_ANY && curr.type == TK_LBRACE) {
        char *safe_src = src;
        Token safe_curr = curr;
        int safe_line = line;
        match(TK_LBRACE);
        bool is_map = (curr.type == TK_STR || curr.type == TK_RBRACE);
        src = safe_src;
        curr = safe_curr;
        line = safe_line;
        if (is_map) parse_map_literal();
        else error("Struct literal requires type");
        handled = true;
    }

    if (!handled) {
        expression();
    }

    if (type_info.id != TYPE_ANY && !type_info.is_array) {
        emit(OP_CAST);
        emit(type_info.id);
    }

    int var_idx = alloc_var(inside_function, name, type_info.id, type_info.is_array);
    if (!inside_function) {
        emit(OP_SET);
        emit(globals[var_idx].addr);
    }

    if (specific_region) {
        emit(OP_PSH_NUM);
        emit(make_const(compiling_vm, 0.0));
        emit(OP_SET_CTX);
    }
}

static void parse_id_statement(Token start_token, char *name) {
    if (curr.type == TK_EQ_ASSIGN) {
        match(TK_EQ_ASSIGN);
        expression();
        int loc = find_local(name);
        if (loc != -1) {
            if (locals[loc].type_id != TYPE_ANY && !locals[loc].is_array) {
                emit(OP_CAST);
                emit(locals[loc].type_id);
            }
            emit(OP_SVAR);
            emit(locals[loc].offset);
        } else {
            int glob = -1;
            char m[MAX_IDENTIFIER * 2];
            get_mangled_name(m, name);
            if ((glob = find_global(m)) != -1) {
            } else if ((glob = find_global(name)) != -1) {
            }
            if (glob == -1) error("Undefined var '%s'", name);
            if (globals[glob].type_id != TYPE_ANY && !globals[glob].is_array) {
                emit(OP_CAST);
                emit(globals[glob].type_id);
            }
            emit(OP_SET);
            emit(globals[glob].addr);
        }
    } else if (curr.type == TK_LPAREN) {
        match(TK_LPAREN);
        int arg_count = 0;
        if (curr.type != TK_RPAREN) {
            expression();
            arg_count++;
            while (curr.type == TK_COMMA) {
                match(TK_COMMA);
                expression();
                arg_count++;
            }
        }
        match(TK_RPAREN);
        int faddr = find_func(name);
        if (faddr != -1) {
            emit(OP_CALL);
            emit(faddr);
            emit(arg_count);
            emit(OP_POP);
            return;
        }
        int std_idx = find_stdlib_func(name);
        if (std_idx != -1) {
            if (std_library[std_idx].arg_count != arg_count) error("StdLib function '%s' expects %d args", name,
                                                                   std_library[std_idx].arg_count);
            emit(OP_NATIVE);
            emit(std_idx);
            emit(OP_POP);
            return;
        }

        int cfn_idx = find_cfn(name);
        if (cfn_idx != -1) {
            if (ffi_blocks[cfn_idx].arg_count != arg_count) error("CFN function '%s' expects %d args", name, ffi_blocks[cfn_idx].arg_count);
            int std_count = 0; 
            while (std_library[std_count].name != NULL) std_count++;
            emit(OP_NATIVE); emit(std_count + cfn_idx); emit(OP_POP); return;
        }

        char m[MAX_IDENTIFIER * 2];
        get_mangled_name(m, name);
        faddr = find_func(m);
        if (faddr != -1) {
            emit(OP_CALL);
            emit(faddr);
            emit(arg_count);
            emit(OP_POP);
            return;
        }
        curr = start_token;
        error("Undefined function '%s'", name);
    } else if (curr.type == TK_DOT || curr.type == TK_LBRACKET) {
        int loc = find_local(name);
        int type_id = -1;
        bool is_array = false;
        if (loc != -1) {
            emit(OP_LVAR);
            emit(locals[loc].offset);
            type_id = locals[loc].type_id;
            is_array = locals[loc].is_array;
        } else {
            int glob = find_global(name);
            if (glob == -1) {
                char m[MAX_IDENTIFIER * 2];
                get_mangled_name(m, name);
                glob = find_global(m);
            }
            if (glob == -1) error("Undefined var '%s'", name);
            emit(OP_GET);
            emit(globals[glob].addr);
            type_id = globals[glob].type_id;
            is_array = globals[glob].is_array;
        }
        while (curr.type == TK_DOT || curr.type == TK_LBRACKET) {
            if (curr.type == TK_DOT) {
                match(TK_DOT);
                char f[MAX_IDENTIFIER];
                strcpy(f, curr.text);
                match(TK_ID);

                if (type_id < 0) error("Accessing member '%s' of untyped/primitive var.", f);

                int offset = find_field(type_id, f);
                if (offset == -1) error("Struct '%s' has no field '%s'", struct_defs[type_id].name, f);

                int field_type = struct_defs[type_id].field_types[offset];
                if (curr.type == TK_EQ_ASSIGN) {
                    match(TK_EQ_ASSIGN);
                    expression();

                    // Strong Typing: If the leaf field has a specific type, cast the expression result
                    if (field_type != TYPE_ANY && field_type != TYPE_NUM) {
                        emit(OP_CAST);
                        emit(field_type);
                    }

                    emit(OP_HSET);
                    emit(offset);
                    emit(type_id);
                    emit(OP_POP);
                    break;
                } else {
                    emit(OP_HGET);
                    emit(offset);
                    emit(type_id);
                    type_id = field_type;
                }
            } else if (curr.type == TK_LBRACKET) {
                match(TK_LBRACKET);
                expression();
                if (curr.type == TK_COLON) {
                    match(TK_COLON);
                    expression();
                    match(TK_RBRACKET);
                    if (curr.type == TK_EQ_ASSIGN) {
                        match(TK_EQ_ASSIGN);
                        expression();
                        emit(OP_SLICE_SET);
                        emit(OP_POP);
                        break;
                    }
                    emit(OP_SLICE);
                } else {
                    match(TK_RBRACKET);
                    if (curr.type == TK_EQ_ASSIGN) {
                        match(TK_EQ_ASSIGN);
                        expression();
                        emit(OP_ASET);
                        emit(OP_POP);
                        break;
                    } else {
                        emit(OP_AGET);
                        if (is_array) is_array = false;
                        else type_id = -1;
                    }
                }
            }
        }
    }
}

static void parse_if() {
    match(TK_IF);
    expression();
    emit(OP_JZ);
    int p1 = compiling_vm->code_size;
    emit(0);
    match(TK_LBRACE);

    // --- Track locals for the IF body ---
    int saved_local_count_if = local_count;
    bool is_local_scope = inside_function;

    emit(OP_SCOPE_ENTER);
    current_scope_depth++;

    while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();

    if (is_local_scope) {
        int vars_to_pop = local_count - saved_local_count_if;
        for(int k = 0; k < vars_to_pop; k++) emit(OP_POP);
        local_count = saved_local_count_if;
    }

    emit(OP_SCOPE_EXIT);
    current_scope_depth--;
    match(TK_RBRACE);

    // Array to track the end-jump for every successful branch
    int exit_jumps[256];
    int exit_jump_count = 0;

    // --- PARSE ELIF BLOCKS ---
    while (curr.type == TK_ELIF) {
        // Jump to the end if the previous branch succeeded
        emit(OP_JMP);
        exit_jumps[exit_jump_count++] = compiling_vm->code_size;
        emit(0);

        // Patch the previous OP_JZ to jump to THIS condition
        compiling_vm->bytecode[p1] = compiling_vm->code_size;

        match(TK_ELIF);
        expression();
        emit(OP_JZ);
        p1 = compiling_vm->code_size; // Track new OP_JZ
        emit(0);
        match(TK_LBRACE);

        int saved_local_count_elif = local_count;
        emit(OP_SCOPE_ENTER);
        current_scope_depth++;

        while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();

        if (is_local_scope) {
            int vars_to_pop = local_count - saved_local_count_elif;
            for(int k = 0; k < vars_to_pop; k++) emit(OP_POP);
            local_count = saved_local_count_elif;
        }

        emit(OP_SCOPE_EXIT);
        current_scope_depth--;
        match(TK_RBRACE);
    }

    // --- PARSE ELSE BLOCK ---
    if (curr.type == TK_ELSE) {
        emit(OP_JMP);
        exit_jumps[exit_jump_count++] = compiling_vm->code_size;
        emit(0);

        // Patch the last OP_JZ to jump to THIS else block
        compiling_vm->bytecode[p1] = compiling_vm->code_size;

        match(TK_ELSE);
        match(TK_LBRACE);

        int saved_local_count_else = local_count;
        emit(OP_SCOPE_ENTER);
        current_scope_depth++;

        while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();

        if (is_local_scope) {
            int vars_to_pop = local_count - saved_local_count_else;
            for(int k = 0; k < vars_to_pop; k++) emit(OP_POP);
            local_count = saved_local_count_else;
        }

        emit(OP_SCOPE_EXIT);
        current_scope_depth--;

        match(TK_RBRACE);
    } else {
        // If there's no 'else', the last condition's failure jumps here
        compiling_vm->bytecode[p1] = compiling_vm->code_size;
    }

    // Patch all successful branch exit jumps to point to the very end
    for (int i = 0; i < exit_jump_count; i++) {
        compiling_vm->bytecode[exit_jumps[i]] = compiling_vm->code_size;
    }
}

static void parse_embed() {
    match(TK_EMBED);
    match(TK_LPAREN);
    char name[MAX_IDENTIFIER];
    strcpy(name, curr.text);
    match(TK_ID);
    match(TK_COMMA);
    if (curr.type != TK_STR) error("embed expects filename");
    char filename[MAX_STRING_LENGTH];
    strcpy(filename, curr.text);
    match(TK_STR);
    match(TK_RPAREN);
    FILE *f = fopen(filename, "rb");
    if (!f) {
        for (int i = 0; i < search_path_count; i++) {
            char tmp[MAX_STRING_LENGTH];
            sprintf(tmp, "%s/%s", search_paths[i], filename);
            f = fopen(tmp, "rb");
            if (f) break;
        }
    }
    if (!f) error("embed: Could not open file '%s'", filename);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc(fsize);
    fread(data, 1, fsize, f);
    fclose(f);
    emit(OP_EMBED);
    emit((int) fsize);
    for (long i = 0; i < fsize; i++) emit((int) data[i]);
    free(data);
    int var_idx = alloc_var(inside_function, name, TYPE_BYTES, false);
    if (inside_function) {
        emit(OP_SVAR);
        emit(locals[var_idx].offset);
    } else {
        emit(OP_SET);
        emit(globals[var_idx].addr);
    }
}

void for_statement() {
    match(TK_FOR);
    match(TK_LPAREN);
    char name1[MAX_IDENTIFIER] = {0};
    char name2[MAX_IDENTIFIER] = {0};
    bool is_iter = false;
    bool is_pair = false;
    int explicit_type = -1;

    emit(OP_SCOPE_ENTER); current_scope_depth++; // Outer Loop Scope

    if (curr.type == TK_VAR) {
        match(TK_VAR);
        strcpy(name1, curr.text);
        match(TK_ID);
        if (curr.type == TK_COLON) { match(TK_COLON); TypeInfo ti = parse_type_spec(); explicit_type = ti.id; }
        is_iter = true;
    } else if (curr.type == TK_ID) {
        char *safe_src = src; Token safe_curr = curr; int safe_line = line;
        strcpy(name1, curr.text);
        match(TK_ID);
        if (curr.type == TK_COMMA) { match(TK_COMMA); strcpy(name2, curr.text); match(TK_ID); is_pair = true; }
        if (!is_pair && curr.type == TK_COLON) { match(TK_COLON); TypeInfo ti = parse_type_spec(); explicit_type = ti.id; }
        if (curr.type == TK_IN) is_iter = true;
        else { src = safe_src; curr = safe_curr; line = safe_line; is_iter = false; }
    }

    int saved_local_count = local_count;
    bool is_local_scope = inside_function;

    if (is_iter) {
        int var1_addr = get_var_addr(name1, is_local_scope, explicit_type);
        int var2_addr = (is_pair) ? get_var_addr(name2, is_local_scope, explicit_type) : -1;

        match(TK_IN); expression();
        char arr_name[64]; char idx_name[64];
        sprintf(arr_name, "_arr_%d", loop_depth); sprintf(idx_name, "_idx_%d", loop_depth);

        int a = alloc_var(is_local_scope, arr_name, TYPE_ANY, true);
        if (!is_local_scope) { emit(OP_SET); emit(a); }

        int i = alloc_var(is_local_scope, idx_name, TYPE_NUM, false);   emit(OP_PSH_NUM); emit(make_const(compiling_vm, 0.0));
        if (!is_local_scope) { emit(OP_SET); emit(i); }

        int loop = compiling_vm->code_size;
        EMIT_GET(is_local_scope, i); EMIT_GET(is_local_scope, a); emit(OP_ALEN); emit(OP_LT); emit(OP_JZ);
        int exit = compiling_vm->code_size; emit(0);

        if (is_pair) {
            EMIT_GET(is_local_scope, a); EMIT_GET(is_local_scope, i); emit(OP_IT_KEY); EMIT_SET(is_local_scope, var1_addr);
            EMIT_GET(is_local_scope, a); EMIT_GET(is_local_scope, i); emit(OP_IT_VAL); EMIT_SET(is_local_scope, var2_addr);
        } else {
            EMIT_GET(is_local_scope, a); EMIT_GET(is_local_scope, i); emit(OP_IT_DEF); EMIT_SET(is_local_scope, var1_addr);
        }

        match(TK_RPAREN); match(TK_LBRACE);

        int body_saved_local_count = local_count;
        bool body_is_local_scope = inside_function;

        // Capture State right before entering the body!
        push_loop(current_scope_depth, local_count);

        emit(OP_SCOPE_ENTER); current_scope_depth++; // Inner Body Scope

        while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();

        if (body_is_local_scope) {
            int vars_to_pop = local_count - body_saved_local_count;
            for(int k=0; k<vars_to_pop; k++) emit(OP_POP);
            local_count = body_saved_local_count;
        }

        emit(OP_SCOPE_EXIT); current_scope_depth--; // Inner Body Scope

        int brace_line = curr.line;
        match(TK_RBRACE);

        int continue_dest = compiling_vm->code_size;
        EMIT_GET(is_local_scope, i); emit(OP_PSH_NUM); emit(make_const(compiling_vm, 1.0)); emit(OP_ADD); EMIT_SET(is_local_scope, i);
        emit(OP_JMP); emit(loop);

        compiling_vm->lines[compiling_vm->code_size - 1] = brace_line;
        compiling_vm->bytecode[exit] = compiling_vm->code_size;

        int break_dest = compiling_vm->code_size; // Break jumps HERE!

        // Clean up the iterator variables
        if (is_local_scope) {
            int vars_to_pop = local_count - saved_local_count;
            for(int k=0; k<vars_to_pop; k++) emit(OP_POP);
            local_count = saved_local_count;
        }

        emit(OP_SCOPE_EXIT); current_scope_depth--; // Outer Scope
        pop_loop(continue_dest, break_dest);
    } else {
        int loop = compiling_vm->code_size;

        // push_loop needs to happen before body!
        push_loop(current_scope_depth, local_count);

        emit(OP_SCOPE_ENTER); current_scope_depth++; // Inner Scope starts BEFORE condition!

        expression(); match(TK_RPAREN); emit(OP_JZ);
        int exit = compiling_vm->code_size; emit(0); match(TK_LBRACE);

        int body_saved_local_count = local_count;
        bool body_is_local_scope = inside_function;

        while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();

        if (body_is_local_scope) {
            int vars_to_pop = local_count - body_saved_local_count;
            for(int k=0; k<vars_to_pop; k++) emit(OP_POP);
            local_count = body_saved_local_count;
        }

        int brace_line = curr.line; match(TK_RBRACE);

        emit(OP_SCOPE_EXIT); current_scope_depth--; // Close inner scope normally

        int continue_dest = compiling_vm->code_size; // 'continue' jumps here bypassing static exit
        emit(OP_JMP); emit(loop);                    // Jump to next iteration
        compiling_vm->lines[compiling_vm->code_size - 1] = brace_line;

        // If condition failed, we land here.
        compiling_vm->bytecode[exit] = compiling_vm->code_size;
        emit(OP_SCOPE_EXIT); current_scope_depth--; // Close inner scope because we broke out of condition

        int break_dest = compiling_vm->code_size;

        if (is_local_scope) {
            int vars_to_pop = local_count - saved_local_count;
            for(int k=0; k<vars_to_pop; k++) emit(OP_POP);
            local_count = saved_local_count;
        }

        emit(OP_SCOPE_EXIT); current_scope_depth--; // Outer Scope
        pop_loop(continue_dest, break_dest);
    }
}

void struct_decl() {
    match(TK_STRUCT);
    char name[MAX_IDENTIFIER];
    parse_namespaced_id(name);
    char m[MAX_IDENTIFIER * 2];
    get_mangled_name(m, name);
    match(TK_LBRACE);
    int idx = struct_count++;
    strcpy(struct_defs[idx].name, m);
    struct_defs[idx].field_count = 0;
    while (curr.type == TK_VAR || curr.type == TK_FN) {
        if (curr.type == TK_VAR) {
            match(TK_VAR);
            strcpy(struct_defs[idx].fields[struct_defs[idx].field_count], curr.text);

            // Default to NUM (double) if no type is provided, standard for Mylo structs
            struct_defs[idx].field_types[struct_defs[idx].field_count] = TYPE_NUM;

            match(TK_ID);

            // Check for Type Annotation : i16
            if (curr.type == TK_COLON) {
                match(TK_COLON);
                TypeInfo ti = parse_type_spec();
                struct_defs[idx].field_types[struct_defs[idx].field_count] = ti.id;
            }

            struct_defs[idx].field_count++;
        } else if (curr.type == TK_FN) {
            match(TK_FN);
            char method_name[MAX_IDENTIFIER];
            strcpy(method_name, curr.text);
            match(TK_ID);
            
            // Add method to struct
            strcpy(struct_defs[idx].methods[struct_defs[idx].method_count], method_name);
            struct_defs[idx].method_count++;
            
            // The method's global mangled name is structName_methodName
            char full_name[MAX_IDENTIFIER * 2];
            sprintf(full_name, "%s_%s", m, method_name); // `m` is the mangled struct name
            
            int saved_scope = current_scope_depth;
            current_scope_depth = 0;
            
            parse_function_body(full_name, full_name, idx);
            
            current_scope_depth = saved_scope;
        }
        
        // Optional trailing comma
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);
}

// Note: Pass VM* vm into this function, or use your compiler's
// internal global VM pointer if you have one.
void enum_decl(VM* vm) {
    match(TK_ENUM);
    char enum_name[MAX_IDENTIFIER];
    strcpy(enum_name, curr.text);
    match(TK_ID);
    match(TK_LBRACE);

    int val = 0;
    int member_count = 0;

    // 1. Register the Enum Type Name string for the 48-bit packing
    int type_str_id = make_string(vm, enum_name);

    while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
        // Grab the member name before we advance the token!
        char member_name[MAX_IDENTIFIER];
        strcpy(member_name, curr.text);

        char entry_name[MAX_IDENTIFIER * 2];
        sprintf(entry_name, "%s_%s", enum_name, member_name);

        if (enum_entry_count >= MAX_ENUM_MEMBERS) error("Too many enum members");
        strcpy(enum_entries[enum_entry_count].name, entry_name);
        enum_entries[enum_entry_count].value = val;
        enum_entry_count++;

        // --- NEW: Generate Runtime Array Elements ---
        int member_str_id = make_string(vm, member_name);

        // 48-bit Pack: [Type ID (16)] [Member ID (16)] [Value (16)]
        unsigned long long packed = ((unsigned long long)type_str_id << 32) |
                                    ((unsigned long long)member_str_id << 16) |
                                    (val & 0xFFFF);

        int const_idx = make_const(vm, (double)packed);

        // Emit instructions to push this enum value to the stack
        emit(OP_PSH_ENUM);
        emit(const_idx);

        val++;
        member_count++;

        match(TK_ID);
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);

    // --- NEW: Build Array and Assign to Global ---
    // This turns the enum declaration into a hidden array allocation!
    emit(OP_ARR);
    emit(member_count);

    // Find or create global variable matching the Enum's name
    int global_idx = -1;
    for (int i = 0; i < global_count; i++) {
        if (strcmp(globals[i].name, enum_name) == 0) {
            global_idx = i;
            break;
        }
    }
    if (global_idx == -1) {
        global_idx = global_count++;
        strcpy(globals[global_idx].name, enum_name);
        globals[global_idx].type_id = 0; // Or TYPE_ANY if defined
        globals[global_idx].addr = global_idx;
    }

    emit(OP_SET);
    emit(global_idx);
}

void parse_struct_literal(int struct_idx) {
    match(TK_LBRACE);
    emit(OP_ALLOC);
    emit(struct_defs[struct_idx].field_count);
    emit(struct_idx);
    while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
        char field_name[MAX_IDENTIFIER];
        strcpy(field_name, curr.text);
        match(TK_ID);

        int offset = find_field(struct_idx, field_name);

        if (curr.type == TK_COLON) match(TK_COLON);
        else match(TK_EQ_ASSIGN);

        expression(); // Pushes the value (e.g., 88.2)

        // --- NEW CODE START ---
        // Look up the definition of the field we are setting
        int field_type = struct_defs[struct_idx].field_types[offset];

        // If the field has a specific type (not 'any' and not generic 'num'), enforce it
        if (field_type != TYPE_ANY && field_type != TYPE_NUM) {
            emit(OP_CAST);
            emit(field_type);
        }
        // --- NEW CODE END ---

        emit(OP_HSET);
        emit(offset);
        emit(struct_idx);
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);
}

void parse_map_literal() {
    match(TK_LBRACE);
    emit(OP_MAP);
    while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
        emit(OP_DUP);
        if (curr.type != TK_STR) error("Map keys must be strings");
        int id = make_string(compiling_vm, curr.text);
        emit(OP_PSH_STR);
        emit(id);
        match(TK_STR);
        match(TK_EQ_ASSIGN);
        expression();
        emit(OP_ASET);
        emit(OP_POP);
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);
}

void statement() {
    if (curr.type == TK_REGION) {
        parse_region();
    } else if (curr.type == TK_CLEAR) {
        match(TK_CLEAR);
        match(TK_LPAREN);
        expression();
        match(TK_RPAREN);
        emit(OP_DEL_ARENA);
    }
    else if (curr.type == TK_CFN) {   
        parse_cfn_decl();               
    }
    else if (curr.type == TK_MONITOR) {
        match(TK_MONITOR);
        match(TK_LPAREN);
        match(TK_RPAREN);
        emit(OP_MONITOR);
    }
    else if (curr.type == TK_DEBUGGER) {
        match(TK_DEBUGGER);
        emit(OP_DEBUGGER);
    }
    else if (curr.type == TK_PRINT) {
        parse_print();
    } else if (curr.type == TK_IMPORT) {
        parse_import();
    } else if (curr.type == TK_MOD) {
        match(TK_MOD);
        char m[MAX_IDENTIFIER];
        strcpy(m, curr.text);
        match(TK_ID);
        match(TK_LBRACE);
        char old[MAX_IDENTIFIER];
        strcpy(old, current_namespace);
        if (strlen(current_namespace) > 0) sprintf(current_namespace, "%s_%s", old, m);
        else strcpy(current_namespace, m);
        while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
            if (curr.type == TK_FN) {
                void function();
                function();
            } else statement();
        }
        match(TK_RBRACE);
        strcpy(current_namespace, old);
    } else if (curr.type == TK_BREAK) {
        match(TK_BREAK);
        emit_break();
    } else if (curr.type == TK_CONTINUE) {
        match(TK_CONTINUE);
        emit_continue();
    } else if (curr.type == TK_ENUM) {
        enum_decl(compiling_vm);
    } else if (curr.type == TK_MODULE_PATH) {
        match(TK_MODULE_PATH);
        match(TK_LPAREN);
        char path[MAX_STRING_LENGTH];
        strcpy(path, curr.text);
        match(TK_STR);
        match(TK_RPAREN);
        if (search_path_count < MAX_SEARCH_PATHS) strcpy(search_paths[search_path_count++], path);
    } else if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) {
        parse_c_block_stmt();
    } else if (curr.type == TK_VAR) {
        parse_var_decl();
    } else if (curr.type == TK_FOR) {
        for_statement();
    } else if (curr.type == TK_ID) {
        Token start_token = curr;
        char name[MAX_IDENTIFIER];
        parse_namespaced_id(name);
        parse_id_statement(start_token, name);
    } else if (curr.type == TK_STRUCT) {
        struct_decl();
    } else if (curr.type == TK_IF) {
        parse_if();
    } else if (curr.type == TK_FOREVER) {
        match(TK_FOREVER);
        match(TK_LBRACE);
        int saved_local_count = local_count;
        bool is_local_scope = inside_function;

        int loop_start = compiling_vm->code_size;

        push_loop(current_scope_depth, local_count);

        emit(OP_SCOPE_ENTER); current_scope_depth++; // Body Scope

        while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();

        if (is_local_scope) {
            int vars_to_pop = local_count - saved_local_count;
            for(int k=0; k<vars_to_pop; k++) emit(OP_POP);
            local_count = saved_local_count;
        }

        emit(OP_SCOPE_EXIT); current_scope_depth--; // Body Scope

        int brace_line = curr.line;
        match(TK_RBRACE);

        int continue_dest = loop_start; // Continue loops straight back to start
        emit(OP_JMP);
        emit(loop_start);
        compiling_vm->lines[compiling_vm->code_size - 1] = brace_line;
        compiling_vm->lines[compiling_vm->code_size - 2] = brace_line;

        int break_dest = compiling_vm->code_size; // Break falls after the loop

        pop_loop(continue_dest, break_dest);
    } else if (curr.type == TK_EMBED) {
        parse_embed();
    } else if (curr.type == TK_RET) {
        match(TK_RET);
        if (curr.type == TK_RBRACE) {
            emit(OP_PSH_NUM);
            emit(make_const(compiling_vm, 0.0));
        } else expression();
        emit(OP_RET);
    } else if (curr.type != TK_EOF) next_token();
}

void parse_function_body(char* registered_name, char* mangled_name, int implicit_self_type) {
    emit(OP_JMP);
    int p = compiling_vm->code_size;
    emit(0);
    strcpy(funcs[func_count].name, mangled_name);
    funcs[func_count++].addr = compiling_vm->code_size;
    vm_register_function(compiling_vm, registered_name, compiling_vm->code_size);

    int start_debug_idx = debug_symbol_count;
    bool ps = inside_function;
    int pl = local_count;
    inside_function = true;
    local_count = 0;
    match(TK_LPAREN);

    struct { int offset; int type; bool is_arr; } typed_args[MAX_FFI_ARGS];
    int typed_arg_count = 0;

    while (curr.type != TK_RPAREN) {
        char arg_name[MAX_IDENTIFIER];
        strcpy(arg_name, curr.text);
        match(TK_ID);
        TypeInfo ti = {TYPE_ANY, false};
        if (curr.type == TK_COLON) {
            match(TK_COLON);
            ti = parse_type_spec();
        } else if (implicit_self_type != -1 && strcmp(arg_name, "self") == 0) {
            ti.id = implicit_self_type;
        }
        int loc = alloc_var(true, arg_name, ti.id, ti.is_array);

        if (ti.id != TYPE_ANY && typed_arg_count < MAX_FFI_ARGS) {
            typed_args[typed_arg_count].offset = locals[loc].offset;
            typed_args[typed_arg_count].type = ti.id;
            typed_args[typed_arg_count].is_arr = ti.is_array;
            typed_arg_count++;
        }

        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RPAREN);
    match(TK_LBRACE);

    // [NEW] Function Scope
    emit(OP_SCOPE_ENTER); current_scope_depth++; // <-- Track the function scope

    for(int i=0; i<typed_arg_count; i++) {
        if (!typed_args[i].is_arr) {
            emit(OP_LVAR); emit(typed_args[i].offset);
            emit(OP_CHECK_TYPE); emit(typed_args[i].type);
            emit(OP_SVAR); emit(typed_args[i].offset);
        }
    }

    while (curr.type != TK_RBRACE) statement();
    match(TK_RBRACE);
    emit(OP_PSH_NUM);
    int z = make_const(compiling_vm, 0.0);
    emit(z);
    emit(OP_RET);
    compiling_vm->bytecode[p] = compiling_vm->code_size;
    int func_end_ip = compiling_vm->code_size;
    for (int i = start_debug_idx; i < debug_symbol_count; i++) {
        if (debug_symbols[i].end_ip == -1) debug_symbols[i].end_ip = func_end_ip;
    }
    inside_function = ps;
    local_count = pl;
}

void function() {
    match(TK_FN);
    int saved_scope_depth = current_scope_depth; // <-- Save outer scope
    current_scope_depth = 0;                     // <-- Reset for new function
    if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) error("'C' is reserved");
    char name[MAX_IDENTIFIER];
    strcpy(name, curr.text);
    match(TK_ID);
    
    char m[MAX_IDENTIFIER * 2];
    get_mangled_name(m, name);
    
    parse_function_body(name, m, -1);
    
    current_scope_depth = saved_scope_depth; // <-- Restore outer scope
}


void parse_internal(char *source, bool is_import) {
    char *os = src;
    Token oc = curr;
    int saved_line = line;
    char *ofs = current_file_start;

    if (is_import) line = 1;
    int start_debug_idx = debug_symbol_count;

    current_file_start = source;
    src = source;
    next_token();

    while (curr.type != TK_EOF) {
        if (curr.type == TK_FN) function();
        else statement();
    }
    if (!is_import) emit(OP_HLT);

    int end_ip = compiling_vm->code_size;
    for (int i = start_debug_idx; i < debug_symbol_count; i++) {
        if (debug_symbols[i].end_ip == -1) debug_symbols[i].end_ip = end_ip;
    }
    if (!is_import) {
        if (compiling_vm->global_symbols) free(compiling_vm->global_symbols);
        compiling_vm->global_symbols = malloc(sizeof(VMSymbol) * global_count);
        compiling_vm->global_symbol_count = global_count;
        for (int i = 0; i < global_count; i++) {
            strcpy(compiling_vm->global_symbols[i].name, globals[i].name);
            compiling_vm->global_symbols[i].addr = globals[i].addr;
        }

        if (compiling_vm->local_symbols) free(compiling_vm->local_symbols);
        compiling_vm->local_symbols = malloc(sizeof(VMLocalInfo) * debug_symbol_count);
        compiling_vm->local_symbol_count = debug_symbol_count;
        for (int i = 0; i < debug_symbol_count; i++) {
            strcpy(compiling_vm->local_symbols[i].name, debug_symbols[i].name);
            compiling_vm->local_symbols[i].stack_offset = debug_symbols[i].stack_offset;
            compiling_vm->local_symbols[i].start_ip = debug_symbols[i].start_ip;
            compiling_vm->local_symbols[i].end_ip = debug_symbols[i].end_ip;
        }
    }
    src = os;
    curr = oc;
    line = saved_line;
    current_file_start = ofs;
}

void parse(VM* vm, char *source) {
    compiling_vm = vm;
    parse_internal(source, false);
}

void generate_binding_c_source(VM* vm, const char *output_filename) {
    compiling_vm = vm;
    FILE *fp = fopen(output_filename, "w");
    if (!fp) {
        printf("Failed to open output file\n");
        mylo_exit(1);
    }

    fprintf(fp, "#define MYLO_BINDING_MODE\n");
    c_gen_headers(fp);

    fprintf(fp, "// --- HOST API BRIDGE ---\n");
    fprintf(fp, "void (*host_vm_push)(VM*, double, int);\n");
    fprintf(fp, "double (*host_vm_pop)(VM*);\n");
    fprintf(fp, "int (*host_make_string)(VM*, const char*);\n");
    fprintf(fp, "double (*host_heap_alloc)(VM*, int);\n");
    fprintf(fp, "double* (*host_vm_resolve_ptr)(VM*, double);\n");
    fprintf(fp, "double (*host_vm_store_copy)(VM*, void*, size_t, const char*);\n");
    fprintf(fp, "double (*host_vm_store_ptr)(VM*, void*, const char*);\n");
    fprintf(fp, "void* (*host_vm_get_ref)(VM*, int, const char*);\n");
    fprintf(fp, "void (*host_vm_free_ref)(VM*, int);\n");
    fprintf(fp, "NativeFunc* host_natives_array;\n");

    fprintf(fp, "#define vm_push (*host_vm_push)\n");
    fprintf(fp, "#define vm_pop (*host_vm_pop)\n");
    fprintf(fp, "#define make_string (*host_make_string)\n");
    fprintf(fp, "#define heap_alloc (*host_heap_alloc)\n");
    fprintf(fp, "#define vm_resolve_ptr (*host_vm_resolve_ptr)\n");
    fprintf(fp, "#define vm_store_copy (*host_vm_store_copy)\n");
    fprintf(fp, "#define vm_store_ptr (*host_vm_store_ptr)\n");
    fprintf(fp, "#define vm_get_ref (*host_vm_get_ref)\n");
    fprintf(fp, "#define vm_free_ref (*host_vm_free_ref)\n");
    fprintf(fp, "#define natives host_natives_array\n");

    c_gen_structs(fp);
    c_gen_ffi_wrappers(fp);

    fprintf(fp, "\n#undef make_string\n");
    fprintf(fp, "#undef heap_alloc\n");

    fprintf(
        fp,
        "\n#ifdef _WIN32\n__declspec(dllexport) void mylo_bind_lib(VM* vm, int start_index, MyloAPI* api) {\n#else\nvoid mylo_bind_lib(VM* vm, int start_index, MyloAPI* api) {\n#endif\n");
    fprintf(
        fp,
        "    host_vm_push = api->push;\n    host_vm_pop = api->pop;\n    host_make_string = api->make_string;\n    host_heap_alloc = api->heap_alloc;\n    host_vm_resolve_ptr = api->resolve_ptr;\n");
    fprintf(
        fp,
        "    host_vm_store_copy = api->store_copy;\n    host_vm_store_ptr = api->store_ptr;\n    host_vm_get_ref = api->get_ref;\n    host_vm_free_ref = api->free_ref;\n");
    fprintf(fp, "    host_natives_array = api->natives_array;\n");
    for (int i = 0; i < ffi_count; i++) fprintf(fp, "    host_natives_array[start_index + %d] = __wrapper_%d;\n", i, i);
    fprintf(fp, "}\n");

    fclose(fp);

    printf("\n");
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgCyan, MyloBgColorDefault);
    printf("Binding generation complete. Wrapper C source code generated to: ");
    setTerminalColor(MyloFgMagenta, MyloBgColorDefault);
    printf(" %s\n", output_filename);
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n\n");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("  To build %s into an shared object you need:\n\n", output_filename);
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo VM header (vm.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo Standard Library header (mylolib.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("A 'C' Compiler (GCC, MSVC, Clang, Zig etc.), referred to as ('cc' below.)\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Shared Object (.so) file to have the same name as your .mylo wrapper `libname.so`\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("Position independent code, so the .so can be loaded at runtime by the interpreter \n\n");
    setTerminalColor(MyloFgCyan, MyloBgColorDefault);
    printf("  ! ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("To build into a single executable instead, use --build on your main mylo file \n");
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("  Example Command:\n\n");
    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("     cc -shared -fPIC %s -Isrc/ -o libname.so\n", output_filename);
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
}

void compile_to_c_source(VM* vm, const char *output_filename) {
    compiling_vm = vm;
    FILE *fp = fopen(output_filename, "w");
    if (!fp) {
        printf("Failed to open output file\n");
        mylo_exit(1);
    }

    c_gen_headers(fp);
    c_gen_structs(fp);
    c_gen_ffi_wrappers(fp);

    fprintf(fp, "int bytecode[] = {\n");
    for (int i = 0; i < vm->code_size; i++) {
        fprintf(fp, "%d,", vm->bytecode[i]);
        if ((i + 1) % 16 == 0) fprintf(fp, "\n");
    }
    fprintf(fp, "};\n\n");

    fprintf(fp, "double constants[] = {\n");
    for (int i = 0; i < vm->const_count; i++) {
        fprintf(fp, "%f,", vm->constants[i]);
        if ((i + 1) % 8 == 0) fprintf(fp, "\n");
    }
    fprintf(fp, "};\n\n");

    fprintf(fp, "char string_pool[%d][%d] = {\n", MAX_STRINGS, MAX_STRING_LENGTH);
    for (int i = 0; i < vm->str_count; i++) {
        fprintf(fp, "  \"");
        for (int j = 0; j < MAX_STRING_LENGTH; j++) {
            char c = vm->string_pool[i][j];
            if (c == '\0') break;
            if (c == '"') fprintf(fp, "\\\"");
            else if (c == '\\') fprintf(fp, "\\\\");
            else if (c == '\n') fprintf(fp, "\\n");
            else if (c == '\r') fprintf(fp, "\\r");
            else if (c >= 32 && c <= 126) fprintf(fp, "%c", c);
            else fprintf(fp, "\\x%02x", (unsigned char) c);
        }
        fprintf(fp, "\",\n");
    }
    fprintf(fp, "};\n\n");

    int sym_count = vm->global_symbol_count > 0 ? vm->global_symbol_count : 1;
    fprintf(fp, "VMSymbol global_symbols[%d] = {\n", sym_count);
    for (int i = 0; i < vm->global_symbol_count; i++) {
        fprintf(fp, "  {\"%s\", %d},\n", vm->global_symbols[i].name, vm->global_symbols[i].addr);
    }
    fprintf(fp, "};\n\n");

    int vm_func_count = vm->function_count > 0 ? vm->function_count : 1;
    fprintf(fp, "VMFunction vm_functions[%d] = {\n", vm_func_count);
    for (int i = 0; i < vm->function_count; i++) {
        fprintf(fp, "  {\"%s\", %d},\n", vm->functions[i].name, vm->functions[i].addr);
    }
    fprintf(fp, "};\n\n");

    // Main Entry Point
    fprintf(fp, "int main(int argc, char** argv) {\n");
    // Instantiate VM on stack or heap
    fprintf(fp, "    VM vm;\n");
    fprintf(fp, "    vm_init(&vm);\n\n");

    fprintf(fp, "    // Load Embedded Code\n");
    fprintf(fp, "    vm.code_size = %d;\n", vm->code_size);
    fprintf(fp, "    memcpy(vm.bytecode, bytecode, sizeof(bytecode));\n\n");

    fprintf(fp, "    vm.const_count = %d;\n", vm->const_count);
    fprintf(fp, "    memcpy(vm.constants, constants, sizeof(constants));\n\n");

    fprintf(fp, "    vm.str_count = %d;\n", vm->str_count);
    fprintf(fp, "    memcpy(vm.string_pool, string_pool, sizeof(string_pool));\n\n");
    fprintf(fp, "    vm.global_symbol_count = %d;\n", vm->global_symbol_count);
    fprintf(fp, "    vm.global_symbols = malloc(sizeof(VMSymbol) * %d);\n", sym_count);
    fprintf(fp, "    memcpy(vm.global_symbols, global_symbols, sizeof(VMSymbol) * vm.global_symbol_count);\n\n");
    fprintf(fp, "    vm.function_count = %d;\n", vm->function_count);
    fprintf(fp, "    memcpy(vm.functions, vm_functions, sizeof(VMFunction) * vm.function_count);\n\n");
    fprintf(fp, "    // Register Standard Library\n");
    fprintf(fp, "    int i = 0;\n");
    fprintf(fp, "    while (std_library[i].name != NULL) {\n");
    // NOTE: Use vm.natives here since we are inside main() using the 'vm' instance
    fprintf(fp, "        vm.natives[i] = std_library[i].func;\n");
    fprintf(fp, "        i++;\n");
    fprintf(fp, "    }\n");

    fprintf(fp, "    // Register FFI Wrappers\n");
    for (int i = 0; i < ffi_count; i++) fprintf(fp, "    vm.natives[i + %d] = __wrapper_%d;\n", bound_ffi_count + i, i);

    fprintf(fp, "\n    // Run\n");
    fprintf(fp, "    run_vm(&vm, false);\n");
    fprintf(fp, "    vm_cleanup(&vm);\n");
    fprintf(fp, "    return 0;\n");
    fprintf(fp, "}\n");

    fclose(fp);

    printf("\n");
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgCyan, MyloBgColorDefault);
    printf("Build complete. File: ");
    setTerminalColor(MyloFgMagenta, MyloBgColorDefault);
    printf(" %s\n", output_filename);
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n\n");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);

    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("  To build %s into an executable you need:\n\n", output_filename);
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo VM implementation code (vm.c and vm.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo Standard Library (mylolib.c and mylolib.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("A 'C' Compiler (GCC, MSVC, Clang, Zig etc.), referred to as ('cc' below.)\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("Link the C Standard Math Library and any other libraries you are using (-lm below)\n\n");


    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("  Example Command:\n\n");
    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("     cc %s src/vm.c src/mylolib.c -Isrc/ -o mylo_executable -lm\n\n", output_filename);
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
}

void compile_repl(VM* vm, char *source, int *out_start_ip) {
    compiling_vm = vm;
    current_file_start = source;
    line = 1;

    src = source;
    next_token();
    *out_start_ip = vm->code_size;

    if (curr.type == TK_EOF) return;

    if (curr.type == TK_FN) function();
    else if (curr.type == TK_STRUCT) struct_decl();
    else if (curr.type == TK_VAR || curr.type == TK_IF || curr.type == TK_FOR ||
             curr.type == TK_FOREVER || curr.type == TK_PRINT || curr.type == TK_IMPORT ||
             curr.type == TK_RET || curr.type == TK_BREAK || curr.type == TK_CONTINUE ||
             curr.type == TK_ENUM || curr.type == TK_REGION || curr.type == TK_CLEAR ||
             curr.type == TK_MONITOR || curr.type == TK_CFN) {
        statement();
    } else if (curr.type == TK_ID) {
        int saved_code_size = vm->code_size;
        int saved_line = line;

        statement();

        if (curr.type != TK_EOF) {
            vm->code_size = saved_code_size;
            src = source;
            line = saved_line;
            next_token();
            expression();
        }
    } else {
        expression();
    }
    emit(OP_HLT);

    if (vm->global_symbols) free(vm->global_symbols);
    vm->global_symbols = malloc(sizeof(VMSymbol) * global_count);
    vm->global_symbol_count = global_count;
    for (int i = 0; i < global_count; i++) {
        strcpy(vm->global_symbols[i].name, globals[i].name);
        vm->global_symbols[i].addr = globals[i].addr;
    }
}

// Copies the host executable and appends the current program's bytecode
void create_standalone_executable(VM* vm, const char* output_filename, const char* host_exe_path) {
    FILE* host = fopen(host_exe_path, "rb");
    if (!host) {
        printf("Error: Could not open host executable '%s'\n", host_exe_path);
        exit(1);
    }

    FILE* out = fopen(output_filename, "wb");
    if (!out) {
        printf("Error: Could not create output file '%s'\n", output_filename);
        fclose(host);
        exit(1);
    }

    // 1. Copy the Host Executable (The VM itself)
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), host)) > 0) {
        fwrite(buffer, 1, n, out);
    }

    // 2. Append Bytecode
    fwrite(vm->bytecode, sizeof(int), vm->code_size, out);

    // 3. Append Constants
    fwrite(vm->constants, sizeof(double), vm->const_count, out);

    // 4. Append String Pool
    // We flatten the 2D array for simple writing
    fwrite(vm->string_pool, MAX_STRING_LENGTH, vm->str_count, out);

    fwrite(vm->dependencies, sizeof(Dependency), vm->dependency_count, out);
    fwrite(vm->global_symbols, sizeof(VMSymbol), vm->global_symbol_count, out);
    fwrite(vm->functions, sizeof(VMFunction), vm->function_count, out);
    // 5. Append Footer (The Metadata)
    StandaloneFooter footer;
    footer.dependency_size = vm->dependency_count * sizeof(Dependency);
    strcpy(footer.magic, MYLO_MAGIC);
    footer.bytecode_size = vm->code_size * sizeof(int);
    footer.const_size = vm->const_count * sizeof(double);
    footer.string_size = vm->str_count * MAX_STRING_LENGTH;
    footer.symbol_size = vm->global_symbol_count * sizeof(VMSymbol);
    footer.function_size = vm->function_count * sizeof(VMFunction);
    fwrite(&footer, sizeof(StandaloneFooter), 1, out);

    // Cleanup
    fclose(host);
    fclose(out);

    // Make executable (Unix-like systems only, ignored on Windows)
#ifndef _WIN32
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "chmod +x %s", output_filename);
    system(cmd);
#endif

    printf("Successfully created standalone executable: %s\n", output_filename);
}

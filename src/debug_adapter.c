// Debug Adapter Protocol (DAP) server for Mylo, started with `mylo --dap script.mylo`.
//
// Messages arrive on stdin and replies go to stdout, each framed by a Content-Length header.
// Anything else the program writes to stdout is moved to stderr, so it can't corrupt the
// protocol; print() output is sent as DAP output events instead.
//
// Besides the standard requests (breakpoints, stepping, stack, scopes, variables, evaluate,
// pause), it answers a few Mylo-specific ones that Mylo IDE uses for its memory viewer:
//   myloVmState     limits, usage and arenas
//   myloStack       the value stack, slot by slot
//   myloArena       raw slots of one memory arena
//   myloStrings     the interned string pool
//   myloDisassemble bytecode around an address
// and sends a myloCompileError event when the program doesn't compile.

#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #define _WINSOCKAPI_    // stops windows.h including winsock.h
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #define dap_read _read
    #define dap_write _write
    #define dap_dup _dup
    #define dap_dup2 _dup2
#else
    #include <poll.h>
    #include <pthread.h>
    #include <strings.h>
    #include <unistd.h>
    #define dap_read read
    #define dap_write write
    #define dap_dup dup
    #define dap_dup2 dup2
#endif

#include "debug_adapter.h"
#include "vm.h"
#include "compiler.h"
#include "utils.h"

// ---------------------------------------------------------------------------
// String builder
// ---------------------------------------------------------------------------

typedef struct {
    char* s;
    size_t len;
    size_t cap;
} Sb;

static void sb_reserve(Sb* b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    char* grown = (char*)realloc(b->s, cap);
    if (!grown) { fprintf(stderr, "[Mylo Adapter] out of memory\n"); exit(1); }
    b->s = grown;
    b->cap = cap;
}

static void sb_putn(Sb* b, const char* s, size_t n) {
    sb_reserve(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

static void sb_puts(Sb* b, const char* s) { sb_putn(b, s, strlen(s)); }

static void sb_printf(Sb* b, const char* fmt, ...) {
    char local_buf[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(local_buf, sizeof(local_buf), fmt, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof(local_buf)) { sb_putn(b, local_buf, (size_t)n); return; }
    sb_reserve(b, (size_t)n);
    va_start(args, fmt);
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, args);
    va_end(args);
    b->len += (size_t)n;
}

// Appends s as a JSON string, quotes included. At most max_bytes of s are written (0 = all).
static void sb_json_str_n(Sb* b, const char* s, size_t max_bytes) {
    sb_putn(b, "\"", 1);
    size_t written = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if (max_bytes && written >= max_bytes) { sb_puts(b, "..."); break; }
        unsigned char c = *p;
        switch (c) {
            case '"': sb_puts(b, "\\\""); break;
            case '\\': sb_puts(b, "\\\\"); break;
            case '\n': sb_puts(b, "\\n"); break;
            case '\r': sb_puts(b, "\\r"); break;
            case '\t': sb_puts(b, "\\t"); break;
            default:
                if (c < 0x20) sb_printf(b, "\\u%04x", c);
                else sb_putn(b, (const char*)p, 1);
        }
        written++;
    }
    sb_putn(b, "\"", 1);
}

static void sb_json_str(Sb* b, const char* s) { sb_json_str_n(b, s, 0); }

static void sb_json_num(Sb* b, double v) {
    if (isnan(v) || isinf(v)) { sb_puts(b, "null"); return; }
    if (v == floor(v) && fabs(v) < 9e15) sb_printf(b, "%lld", (long long)v);
    else sb_printf(b, "%.17g", v);
}

// ---------------------------------------------------------------------------
// Minimal JSON reader
// ---------------------------------------------------------------------------

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal {
    JType type;
    double num;
    bool boolean;
    char* str;            // J_STR value
    char* key;            // name of this member inside an object
    struct JVal* child;   // first element or member
    struct JVal* next;    // next sibling
} JVal;

static void j_free(JVal* v) {
    while (v) {
        JVal* next = v->next;
        j_free(v->child);
        free(v->str);
        free(v->key);
        free(v);
        v = next;
    }
}

static void j_skip_ws(const char** p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

static void j_put_utf8(Sb* b, unsigned cp) {
    char out[4];
    if (cp < 0x80) { out[0] = (char)cp; sb_putn(b, out, 1); }
    else if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); sb_putn(b, out, 2); }
    else {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); sb_putn(b, out, 3);
    }
}

static char* j_parse_string(const char** p) {
    if (**p != '"') return NULL;
    (*p)++;
    Sb b = {0};
    sb_reserve(&b, 16);
    while (**p && **p != '"') {
        char c = *(*p)++;
        if (c != '\\') { sb_putn(&b, &c, 1); continue; }
        char e = *(*p)++;
        switch (e) {
            case 'n': sb_puts(&b, "\n"); break;
            case 't': sb_puts(&b, "\t"); break;
            case 'r': sb_puts(&b, "\r"); break;
            case 'b': sb_puts(&b, "\b"); break;
            case 'f': sb_puts(&b, "\f"); break;
            case 'u': {
                unsigned cp = 0;
                for (int i = 0; i < 4 && isxdigit((unsigned char)**p); i++) {
                    char h = *(*p)++;
                    cp = cp * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10));
                }
                j_put_utf8(&b, cp);
                break;
            }
            case '\0': (*p)--; break;
            default: sb_putn(&b, &e, 1); break;
        }
    }
    if (**p == '"') (*p)++;
    return b.s;
}

static JVal* j_new(JType type) {
    JVal* v = (JVal*)calloc(1, sizeof(JVal));
    if (v) v->type = type;
    return v;
}

static JVal* j_parse_value(const char** p, int depth) {
    j_skip_ws(p);
    if (depth > 64) return NULL;
    char c = **p;
    if (c == '{' || c == '[') {
        bool is_obj = c == '{';
        JVal* v = j_new(is_obj ? J_OBJ : J_ARR);
        if (!v) return NULL;
        (*p)++;
        JVal** tail = &v->child;
        for (;;) {
            j_skip_ws(p);
            if (**p == (is_obj ? '}' : ']')) { (*p)++; break; }
            if (**p == '\0') break;
            char* key = NULL;
            if (is_obj) {
                key = j_parse_string(p);
                j_skip_ws(p);
                if (**p == ':') (*p)++;
            }
            JVal* item = j_parse_value(p, depth + 1);
            if (!item) { free(key); break; }
            item->key = key;
            *tail = item;
            tail = &item->next;
            j_skip_ws(p);
            if (**p == ',') (*p)++;
        }
        return v;
    }
    if (c == '"') {
        JVal* v = j_new(J_STR);
        if (v) v->str = j_parse_string(p);
        return v;
    }
    if (strncmp(*p, "true", 4) == 0) { JVal* v = j_new(J_BOOL); if (v) v->boolean = true; *p += 4; return v; }
    if (strncmp(*p, "false", 5) == 0) { JVal* v = j_new(J_BOOL); *p += 5; return v; }
    if (strncmp(*p, "null", 4) == 0) { *p += 4; return j_new(J_NULL); }
    char* end = NULL;
    double d = strtod(*p, &end);
    if (end && end != *p) {
        JVal* v = j_new(J_NUM);
        if (v) v->num = d;
        *p = end;
        return v;
    }
    return NULL;
}

static JVal* j_get(JVal* obj, const char* key) {
    if (!obj || obj->type != J_OBJ) return NULL;
    for (JVal* m = obj->child; m; m = m->next) {
        if (m->key && strcmp(m->key, key) == 0) return m;
    }
    return NULL;
}

static double j_num(JVal* v, double fallback) { return (v && v->type == J_NUM) ? v->num : fallback; }
static bool j_bool(JVal* v, bool fallback) { return (v && v->type == J_BOOL) ? v->boolean : fallback; }
static const char* j_str(JVal* v, const char* fallback) { return (v && v->type == J_STR && v->str) ? v->str : fallback; }

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

static int proto_in = 0;
static int proto_out = 1;
static char* in_buf = NULL;
static size_t in_len = 0;
static size_t in_cap = 0;

#ifdef _WIN32
static CRITICAL_SECTION send_lock;
#define LOCK_SEND() EnterCriticalSection(&send_lock)
#define UNLOCK_SEND() LeaveCriticalSection(&send_lock)
#else
static pthread_mutex_t send_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_SEND() pthread_mutex_lock(&send_lock)
#define UNLOCK_SEND() pthread_mutex_unlock(&send_lock)
#endif

static void log_debug(const char* fmt, ...) {
    if (!getenv("MYLO_DAP_LOG")) return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[Mylo Adapter] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void write_all(const char* data, size_t len) {
    while (len > 0) {
        int n = dap_write(proto_out, data, (unsigned)(len > 65536 ? 65536 : len));
        if (n <= 0) exit(0);   // The client has gone, so there's nobody left to debug for.
        data += n;
        len -= (size_t)n;
    }
}

static void send_json(const char* body) {
    char header[64];
    int header_len = snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", (int)strlen(body));
    LOCK_SEND();
    write_all(header, (size_t)header_len);
    write_all(body, strlen(body));
    UNLOCK_SEND();
    log_debug("<< %s", body);
}

static int out_seq = 1;

static void send_event(const char* event, const char* body_json) {
    Sb b = {0};
    sb_printf(&b, "{\"seq\":%d,\"type\":\"event\",\"event\":\"%s\",\"body\":%s}", out_seq++, event, body_json ? body_json : "{}");
    send_json(b.s);
    free(b.s);
}

static void send_response(int seq, const char* command, bool success, const char* body_json, const char* message) {
    Sb b = {0};
    sb_printf(&b, "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,\"command\":", out_seq++, seq);
    sb_json_str(&b, command);
    sb_printf(&b, ",\"success\":%s", success ? "true" : "false");
    if (message) { sb_puts(&b, ",\"message\":"); sb_json_str(&b, message); }
    sb_printf(&b, ",\"body\":%s}", body_json ? body_json : "{}");
    send_json(b.s);
    free(b.s);
}

// True when more input can be read without waiting.
static bool input_waiting(void) {
#ifdef _WIN32
    HANDLE h = (HANDLE)_get_osfhandle(proto_in);
    DWORD available = 0;
    if (h != INVALID_HANDLE_VALUE && PeekNamedPipe(h, NULL, 0, NULL, &available, NULL)) return available > 0;
    return false;
#else
    struct pollfd pfd = { proto_in, POLLIN, 0 };
    return poll(&pfd, 1, 0) > 0;
#endif
}

static bool input_pending(void) { return in_len > 0 || input_waiting(); }

// Reads more bytes into in_buf. Returns false when the input has closed.
static bool read_more(void) {
    if (in_len + 4096 + 1 > in_cap) {
        size_t cap = in_cap ? in_cap * 2 : 8192;
        while (cap < in_len + 4096 + 1) cap *= 2;
        char* grown = (char*)realloc(in_buf, cap);
        if (!grown) return false;
        in_buf = grown;
        in_cap = cap;
    }
    int n = dap_read(proto_in, in_buf + in_len, 4096);
    if (n <= 0) return false;
    in_len += (size_t)n;
    in_buf[in_len] = '\0';
    return true;
}

// Returns the next complete message body (caller frees). Returns NULL when the input has
// closed (setting *closed), or, if block is false, as soon as no complete message is waiting.
static char* next_message(bool block, bool* closed) {
    *closed = false;
    for (;;) {
        if (in_len > 0) {
            in_buf[in_len] = '\0';
            char* header_end = strstr(in_buf, "\r\n\r\n");
            if (header_end) {
                int content_length = -1;
                for (char* line = in_buf; line < header_end; ) {
                    char* eol = strstr(line, "\r\n");
                    if (!eol) break;
#ifdef _WIN32
                    if (_strnicmp(line, "Content-Length:", 15) == 0) content_length = atoi(line + 15);
#else
                    if (strncasecmp(line, "Content-Length:", 15) == 0) content_length = atoi(line + 15);
#endif
                    line = eol + 2;
                }
                size_t header_size = (size_t)(header_end - in_buf) + 4;
                if (content_length < 0) {
                    // A malformed header: drop it and look for the next one.
                    memmove(in_buf, in_buf + header_size, in_len - header_size);
                    in_len -= header_size;
                    continue;
                }
                if (in_len >= header_size + (size_t)content_length) {
                    char* body = (char*)malloc((size_t)content_length + 1);
                    if (!body) return NULL;
                    memcpy(body, in_buf + header_size, (size_t)content_length);
                    body[content_length] = '\0';
                    size_t used = header_size + (size_t)content_length;
                    memmove(in_buf, in_buf + used, in_len - used);
                    in_len -= used;
                    return body;
                }
            }
        }
        if (!block && !input_waiting()) return NULL;
        if (!read_more()) { *closed = true; return NULL; }
    }
}

// ---------------------------------------------------------------------------
// Program output
// ---------------------------------------------------------------------------

// print() writes in small pieces, so output is gathered into lines before it's sent.
static Sb pending_output = {0};

static void flush_output(void) {
    LOCK_SEND();
    if (pending_output.len == 0) { UNLOCK_SEND(); return; }
    Sb body = {0};
    sb_puts(&body, "{\"category\":\"stdout\",\"output\":");
    sb_json_str(&body, pending_output.s);
    sb_puts(&body, "}");
    pending_output.len = 0;
    pending_output.s[0] = '\0';
    UNLOCK_SEND();
    send_event("output", body.s);
    free(body.s);
}

static void dap_print_callback(const char* str) {
    LOCK_SEND();
    sb_puts(&pending_output, str);
    bool flush = strchr(str, '\n') != NULL || pending_output.len > 4096;
    UNLOCK_SEND();
    if (flush) flush_output();
}

static void send_stderr(const char* text) {
    flush_output();
    Sb body = {0};
    sb_puts(&body, "{\"category\":\"stderr\",\"output\":");
    sb_json_str(&body, text);
    sb_puts(&body, "}");
    send_event("output", body.s);
    free(body.s);
}

// ---------------------------------------------------------------------------
// Debugger state
// ---------------------------------------------------------------------------

typedef enum { ST_STARTING, ST_RUNNING, ST_STOPPED, ST_FINISHED } AdapterState;
typedef enum { RUN_CONTINUE, RUN_STEP_IN, RUN_STEP_OVER, RUN_STEP_OUT } RunMode;

typedef struct {
    int file;
    int line;
} Breakpoint;

#define MAX_DAP_BREAKPOINTS 1024
#define MAX_FRAMES 512
#define MAX_HANDLES 4096
#define MAX_CHILDREN 1000

static VM* vm = NULL;
static char program_path[4096];
static char* program_source = NULL;
static AdapterState state = ST_STARTING;
static RunMode run_mode = RUN_CONTINUE;
static int step_depth = 0;
static int step_line = 0;   // where the current step started
static int step_file = 0;
static bool pause_requested = false;
static bool stop_on_entry = true;
static bool no_debug = false;
static bool program_failed = false;   // stopped on an error; resuming ends the program

static Breakpoint breakpoints[MAX_DAP_BREAKPOINTS];
static int breakpoint_count = 0;

static char last_error[2048];
static jmp_buf error_env;

typedef struct { double value; int type; } Handle;
static Handle handles[MAX_HANDLES];
static int handle_count = 0;

// Variable references: 1 = globals, 2 + frame index = that frame's locals, 10000 + n = handle n.
#define REF_GLOBALS 1
#define REF_FRAME_BASE 2
#define REF_HANDLE_BASE 10000

static void dap_error_callback(const char* str) {
    snprintf(last_error, sizeof(last_error), "%s", str ? str : "error");
}

static int line_at(int ip) { return (vm->lines && ip >= 0 && ip < vm->code_size) ? vm->lines[ip] : 0; }
static int file_at(int ip) { return (vm->file_ids && ip >= 0 && ip < vm->code_size) ? vm->file_ids[ip] : 0; }

// True if ip begins a run of code for its line, rather than continuing one. Coming back from
// a call lands part-way through the caller's line, which mustn't count as reaching it again.
static bool starts_line(int ip) {
    return ip == 0 || line_at(ip - 1) != line_at(ip) || file_at(ip - 1) != file_at(ip);
}

// Compares paths loosely: separators and, on Windows, case don't matter.
static bool same_path(const char* a, const char* b) {
    char ca[4096], cb[4096];
    if (!realpath(a, ca)) snprintf(ca, sizeof(ca), "%s", a);
    if (!realpath(b, cb)) snprintf(cb, sizeof(cb), "%s", b);
    const char* x = ca;
    const char* y = cb;
    while (*x && *y) {
        char cx = *x == '\\' ? '/' : *x;
        char cy = *y == '\\' ? '/' : *y;
#ifdef _WIN32
        cx = (char)tolower((unsigned char)cx);
        cy = (char)tolower((unsigned char)cy);
#endif
        if (cx != cy) return false;
        x++;
        y++;
    }
    return *x == *y;
}

static int find_file_id(const char* path) {
    for (int i = 0; i < vm->source_file_count; i++) {
        if (same_path(vm->source_files[i], path)) return i;
    }
    return -1;
}

static bool has_breakpoint(int file, int line) {
    for (int i = 0; i < breakpoint_count; i++) {
        if (breakpoints[i].line == line && breakpoints[i].file == file) return true;
    }
    return false;
}

// The first line at or after `line` in `file` that has code, or -1 if there's none.
static int nearest_code_line(int file, int line) {
    int best = -1;
    for (int ip = 0; ip < vm->code_size; ip++) {
        if (file_at(ip) != file) continue;
        int l = line_at(ip);
        if (l >= line && (best < 0 || l < best)) {
            best = l;
            if (best == line) break;
        }
    }
    return best;
}

typedef struct {
    int ip;        // the instruction this frame runs next, or the call it's waiting on
    int fp;
    int func_addr; // the function's entry address, -1 for the main program
} Frame;

static int collect_frames(Frame* out, int max) {
    int ip = vm->ip;
    int fp = vm->fp;
    int n = 0;
    while (n < max) {
        Frame f = { ip, fp, -1 };
        if (fp < 2 || fp > vm->sp + 1 || fp >= vm->stack_capacity) {
            out[n++] = f;
            break;
        }
        int ret_ip = (int)vm->stack[fp - 2];
        int caller_fp = (int)vm->stack[fp - 1];
        // The return address follows a CALL target argc instruction, so the target is two back.
        if (ret_ip >= 2 && ret_ip <= vm->code_size) f.func_addr = vm->bytecode[ret_ip - 2];
        out[n++] = f;
        if (caller_fp >= fp || ret_ip < 1) break;   // a damaged chain; don't loop
        ip = ret_ip - 1;
        fp = caller_fp;
    }
    return n;
}

// The entry address of the function running now, or -1 in the main program.
static int current_function(void) {
    int fp = vm->fp;
    if (fp < 2 || fp >= vm->stack_capacity) return -1;
    int ret_ip = (int)vm->stack[fp - 2];
    return (ret_ip >= 2 && ret_ip <= vm->code_size) ? vm->bytecode[ret_ip - 2] : -1;
}

static int frame_depth(void) {
    int depth = 1;
    int fp = vm->fp;
    while (fp >= 2 && fp < vm->stack_capacity && depth < 1000000) {
        int caller = (int)vm->stack[fp - 1];
        if (caller >= fp) break;
        fp = caller;
        depth++;
    }
    return depth;
}

static const char* function_name(int addr) {
    if (addr < 0) return "main";
    for (int i = 0; i < vm->function_count; i++) {
        if (vm->functions[i].addr == addr) return vm->functions[i].name;
    }
    static char unknown[32];
    snprintf(unknown, sizeof(unknown), "fn@%d", addr);
    return unknown;
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

static const char* type_name(int t) {
    switch (t) {
        case T_NUM: return "num";
        case T_STR: return "str";
        case T_OBJ: return "obj";
        case T_ENUM: return "enum";
        default: return "?";
    }
}

static const char* packed_type_name(int obj_type) {
    switch (obj_type) {
        case TYPE_U16: return "u16";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_I8: return "i8";
        case TYPE_I16: return "i16";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_F16: return "f16";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: case TYPE_BOOL_ARRAY: return "bool";
        default: return "packed";
    }
}

static bool is_packed_array(int obj_type) {
    return obj_type < 0 && obj_type != TYPE_ARRAY && obj_type != TYPE_MAP && obj_type != TYPE_STRUCT &&
           obj_type != TYPE_ENUM && obj_type != TYPE_BYTES && obj_type != TYPE_U8;
}

static double packed_element(int obj_type, const char* data, int i) {
    switch (obj_type) {
        case TYPE_BOOL: case TYPE_BOOL_ARRAY: return ((const unsigned char*)data)[i];
        case TYPE_I8: return ((const signed char*)data)[i];
        case TYPE_U16: return ((const unsigned short*)data)[i];
        case TYPE_I16: return ((const short*)data)[i];
        case TYPE_F16: return ((const short*)data)[i];
        case TYPE_U32: return ((const unsigned int*)data)[i];
        case TYPE_I32: return ((const int*)data)[i];
        case TYPE_F32: return ((const float*)data)[i];
        case TYPE_U64: return (double)((const unsigned long long*)data)[i];
        case TYPE_I64: return (double)((const long long*)data)[i];
        default: return ((const double*)data)[i];
    }
}

static void format_number(Sb* b, double v) {
    if (v == floor(v) && fabs(v) < 1e15) sb_printf(b, "%lld", (long long)v);
    else sb_printf(b, "%.15g", v);
}

static double* live_object(double val, int** types_out) {
    if (val == 0.0) return NULL;
    int arena = UNPACK_ARENA(val);
    int gen = UNPACK_GEN(val);
    if (arena < 0 || arena >= MAX_ARENAS || !vm->arenas[arena].active || vm->arenas[arena].generation != gen) return NULL;
    double* base = vm_resolve_ptr(vm, val);
    int* types = vm_resolve_type(vm, val);
    if (!base || !types) return NULL;
    if (types_out) *types_out = types;
    return base;
}

// How many children an object shows when expanded, or 0 if it isn't expandable.
static int child_count(double val, int type) {
    if (type != T_OBJ) return 0;
    double* base = live_object(val, NULL);
    if (!base) return 0;
    int obj_type = (int)base[HEAP_OFFSET_TYPE];
    if (obj_type == TYPE_ARRAY || is_packed_array(obj_type)) return (int)base[HEAP_OFFSET_LEN];
    if (obj_type == TYPE_MAP) return (int)base[HEAP_OFFSET_COUNT];
    if (obj_type == TYPE_BYTES || obj_type == TYPE_U8) return 0;
    if (obj_type >= 0) return (int)base[1];   // struct: field count
    return 0;
}

static void format_value(Sb* b, double val, int type, int depth);

static void format_object(Sb* b, double val, int depth) {
    if (val == 0.0) { sb_puts(b, "null"); return; }
    int* types = NULL;
    double* base = live_object(val, &types);
    if (!base) { sb_puts(b, "<freed>"); return; }
    int obj_type = (int)base[HEAP_OFFSET_TYPE];
    int preview = depth == 0 ? 8 : 0;

    if (obj_type == TYPE_ARRAY) {
        int len = (int)base[HEAP_OFFSET_LEN];
        sb_printf(b, "array[%d]", len);
        if (preview) {
            sb_puts(b, " [");
            for (int i = 0; i < len && i < preview; i++) {
                if (i) sb_puts(b, ", ");
                format_value(b, base[HEAP_HEADER_ARRAY + i], types[HEAP_HEADER_ARRAY + i], depth + 1);
            }
            sb_puts(b, len > preview ? ", ...]" : "]");
        }
    } else if (obj_type == TYPE_MAP) {
        sb_printf(b, "map{%d}", (int)base[HEAP_OFFSET_COUNT]);
    } else if (obj_type == TYPE_BYTES || obj_type == TYPE_U8) {
        int len = (int)base[HEAP_OFFSET_LEN];
        const unsigned char* bytes = (const unsigned char*)&base[HEAP_HEADER_ARRAY];
        sb_printf(b, "bytes[%d]", len);
        if (preview) {
            sb_puts(b, " b\"");
            for (int i = 0; i < len && i < 32; i++) {
                if (bytes[i] >= 32 && bytes[i] < 127 && bytes[i] != '"' && bytes[i] != '\\') sb_printf(b, "%c", bytes[i]);
                else sb_printf(b, "\\x%02X", bytes[i]);
            }
            sb_puts(b, len > 32 ? "...\"" : "\"");
        }
    } else if (is_packed_array(obj_type)) {
        int len = (int)base[HEAP_OFFSET_LEN];
        sb_printf(b, "%s[%d]", packed_type_name(obj_type), len);
        if (preview) {
            const char* data = (const char*)&base[HEAP_HEADER_ARRAY];
            sb_puts(b, " [");
            for (int i = 0; i < len && i < preview; i++) {
                if (i) sb_puts(b, ", ");
                format_number(b, packed_element(obj_type, data, i));
            }
            sb_puts(b, len > preview ? ", ...]" : "]");
        }
    } else if (obj_type >= 0) {
        const char* sname = compiler_struct_name(obj_type);
        int fields = (int)base[1];
        sb_puts(b, sname ? sname : "struct");
        if (preview) {
            sb_puts(b, " {");
            for (int i = 0; i < fields && i < preview; i++) {
                if (i) sb_puts(b, ", ");
                const char* fname = compiler_struct_field(obj_type, i);
                if (fname) sb_printf(b, "%s: ", fname);
                format_value(b, base[2 + i], types[2 + i], depth + 1);
            }
            sb_puts(b, fields > preview ? ", ...}" : "}");
        }
    } else {
        sb_printf(b, "object(type %d)", obj_type);
    }
}

static void format_value(Sb* b, double val, int type, int depth) {
    if (type == T_STR) {
        // Quoted for display, with control characters shown as escapes.
        sb_json_str_n(b, vm_string(vm, (int)val), 200);
        return;
    }
    if (type == T_ENUM) {
        unsigned long long packed = (unsigned long long)val;
        sb_puts(b, vm_string(vm, (int)((packed >> 16) & 0xFFFF)));
        return;
    }
    if (type == T_OBJ) { format_object(b, val, depth); return; }
    format_number(b, val);
}

static int make_handle(double val, int type) {
    if (child_count(val, type) <= 0) return 0;
    if (handle_count >= MAX_HANDLES) return 0;
    handles[handle_count].value = val;
    handles[handle_count].type = type;
    return REF_HANDLE_BASE + handle_count++;
}

// Appends one DAP variable object.
static void add_variable(Sb* b, bool* first, const char* name, double val, int type) {
    if (!*first) sb_puts(b, ",");
    *first = false;
    Sb text = {0};
    format_value(&text, val, type, 0);
    sb_puts(b, "{\"name\":");
    sb_json_str(b, name);
    sb_puts(b, ",\"value\":");
    sb_json_str(b, text.s ? text.s : "");
    sb_puts(b, ",\"type\":");
    sb_json_str(b, type_name(type));
    int ref = make_handle(val, type);
    sb_printf(b, ",\"variablesReference\":%d", ref);
    if (ref) sb_printf(b, ",\"indexedVariables\":%d", child_count(val, type));
    sb_puts(b, "}");
    free(text.s);
}

static void list_children(Sb* b, double val, int start, int count) {
    bool first = true;
    int* types = NULL;
    double* base = live_object(val, &types);
    if (!base) return;
    int obj_type = (int)base[HEAP_OFFSET_TYPE];
    int total = child_count(val, T_OBJ);
    if (start < 0) start = 0;
    int end = (count > 0) ? start + count : total;
    if (end > total) end = total;
    if (end - start > MAX_CHILDREN) end = start + MAX_CHILDREN;
    char name[256];

    for (int i = start; i < end; i++) {
        if (obj_type == TYPE_ARRAY) {
            snprintf(name, sizeof(name), "[%d]", i);
            add_variable(b, &first, name, base[HEAP_HEADER_ARRAY + i], types[HEAP_HEADER_ARRAY + i]);
        } else if (is_packed_array(obj_type)) {
            snprintf(name, sizeof(name), "[%d]", i);
            add_variable(b, &first, name, packed_element(obj_type, (const char*)&base[HEAP_HEADER_ARRAY], i), T_NUM);
        } else if (obj_type == TYPE_MAP) {
            double* data = vm_resolve_ptr(vm, base[HEAP_OFFSET_DATA]);
            int* dtypes = vm_resolve_type(vm, base[HEAP_OFFSET_DATA]);
            if (!data || !dtypes) return;
            Sb key = {0};
            format_value(&key, data[i * 2], dtypes[i * 2], 1);
            add_variable(b, &first, key.s ? key.s : "?", data[i * 2 + 1], dtypes[i * 2 + 1]);
            free(key.s);
        } else if (obj_type >= 0) {
            const char* fname = compiler_struct_field(obj_type, i);
            if (fname) snprintf(name, sizeof(name), "%s", fname);
            else snprintf(name, sizeof(name), "field %d", i);
            add_variable(b, &first, name, base[2 + i], types[2 + i]);
        }
    }
}

// The highest stack slot belonging to a frame: the next frame's call starts above it.
static int frame_top(const Frame* frames, int index) {
    return (index == 0) ? vm->sp : frames[index - 1].fp - 3;
}

static void list_locals(Sb* b, const Frame* frames, int frame_count, int index) {
    bool first = true;
    if (index < 0 || index >= frame_count || !vm->local_symbols) return;
    const Frame* f = &frames[index];
    int top = frame_top(frames, index);
    for (int i = 0; i < vm->local_symbol_count; i++) {
        VMLocalInfo* sym = &vm->local_symbols[i];
        if (f->ip < sym->start_ip || f->ip > sym->end_ip) continue;
        int slot = f->fp + sym->stack_offset;
        if (slot < 0 || slot > top || slot >= vm->stack_capacity) continue;
        add_variable(b, &first, sym->name, vm->stack[slot], vm->stack_types[slot]);
    }
}

static void list_globals(Sb* b) {
    bool first = true;
    if (!vm->global_symbols) return;
    // Functions are compiled ahead of the main program, so "declared yet" is measured by
    // where the main program has got to, which is the outermost frame.
    static Frame frames[MAX_FRAMES];
    int frame_count = collect_frames(frames, MAX_FRAMES);
    int main_ip = frame_count > 0 ? frames[frame_count - 1].ip : vm->ip;
    for (int i = 0; i < vm->global_symbol_count; i++) {
        VMSymbol* sym = &vm->global_symbols[i];
        if (sym->addr < 0 || sym->addr >= MAX_GLOBALS) continue;
        if (sym->start_ip > main_ip) continue;   // not declared yet
        add_variable(b, &first, sym->name, vm->globals[sym->addr], vm->global_types[sym->addr]);
    }
}

// Finds a variable by name, the innermost local first. Returns false if there's none.
static bool lookup_variable(const char* name, const Frame* frames, int frame_count, int index, double* val, int* type) {
    if (index >= 0 && index < frame_count && vm->local_symbols) {
        const Frame* f = &frames[index];
        int top = frame_top(frames, index);
        int best = -1;
        for (int i = 0; i < vm->local_symbol_count; i++) {
            VMLocalInfo* sym = &vm->local_symbols[i];
            if (strcmp(sym->name, name) != 0 || f->ip < sym->start_ip || f->ip > sym->end_ip) continue;
            int slot = f->fp + sym->stack_offset;
            if (slot < 0 || slot > top || slot >= vm->stack_capacity) continue;
            if (best < 0 || sym->start_ip >= vm->local_symbols[best].start_ip) best = i;
        }
        if (best >= 0) {
            int slot = f->fp + vm->local_symbols[best].stack_offset;
            *val = vm->stack[slot];
            *type = vm->stack_types[slot];
            return true;
        }
    }
    if (vm->global_symbols) {
        for (int i = 0; i < vm->global_symbol_count; i++) {
            VMSymbol* sym = &vm->global_symbols[i];
            if (strcmp(sym->name, name) == 0 && sym->addr >= 0 && sym->addr < MAX_GLOBALS) {
                *val = vm->globals[sym->addr];
                *type = vm->global_types[sym->addr];
                return true;
            }
        }
    }
    return false;
}

// Evaluates `name`, `name.field`, `name[index]` and chains of them.
static bool evaluate_path(const char* expr, const Frame* frames, int frame_count, int index, double* val, int* type,
                          char* err, size_t err_len) {
    const char* p = expr;
    while (isspace((unsigned char)*p)) p++;
    char name[MAX_IDENTIFIER];
    int n = 0;
    while ((isalnum((unsigned char)*p) || *p == '_' || *p == ':') && n < MAX_IDENTIFIER - 1) name[n++] = *p++;
    name[n] = '\0';
    if (n == 0) { snprintf(err, err_len, "can't evaluate '%s'", expr); return false; }
    // Namespaced names are stored with an underscore: Mod::x is Mod_x.
    char mangled[MAX_IDENTIFIER];
    snprintf(mangled, sizeof(mangled), "%s", name);
    char* sep = strstr(mangled, "::");
    if (sep) { *sep = '_'; memmove(sep + 1, sep + 2, strlen(sep + 2) + 1); }
    if (!lookup_variable(mangled, frames, frame_count, index, val, type)) {
        snprintf(err, err_len, "no variable named '%s' here", name);
        return false;
    }

    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*type != T_OBJ) { snprintf(err, err_len, "'%s' isn't an object", expr); return false; }
        int* types = NULL;
        double* base = live_object(*val, &types);
        if (!base) { snprintf(err, err_len, "the object has been freed"); return false; }
        int obj_type = (int)base[HEAP_OFFSET_TYPE];

        if (*p == '.') {
            p++;
            char field[MAX_IDENTIFIER];
            int fn = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && fn < MAX_IDENTIFIER - 1) field[fn++] = *p++;
            field[fn] = '\0';
            if (obj_type < 0) { snprintf(err, err_len, "'%s' isn't a struct", expr); return false; }
            int found = -1;
            for (int i = 0; i < (int)base[1]; i++) {
                const char* fname = compiler_struct_field(obj_type, i);
                if (fname && strcmp(fname, field) == 0) { found = i; break; }
            }
            if (found < 0) { snprintf(err, err_len, "no field named '%s'", field); return false; }
            *val = base[2 + found];
            *type = types[2 + found];
        } else if (*p == '[') {
            p++;
            char* after = NULL;
            long idx = strtol(p, &after, 10);
            p = after;
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
            int len = (int)base[HEAP_OFFSET_LEN];
            if (obj_type == TYPE_ARRAY) {
                if (idx < 0 || idx >= len) { snprintf(err, err_len, "index %ld is out of range", idx); return false; }
                *val = base[HEAP_HEADER_ARRAY + idx];
                *type = types[HEAP_HEADER_ARRAY + idx];
            } else if (is_packed_array(obj_type)) {
                if (idx < 0 || idx >= len) { snprintf(err, err_len, "index %ld is out of range", idx); return false; }
                *val = packed_element(obj_type, (const char*)&base[HEAP_HEADER_ARRAY], (int)idx);
                *type = T_NUM;
            } else {
                snprintf(err, err_len, "'%s' can't be indexed", expr);
                return false;
            }
        } else {
            snprintf(err, err_len, "can't evaluate '%s'", expr);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Disassembly
// ---------------------------------------------------------------------------

// Writes one instruction and returns the address of the next.
static int disassemble_one(int ip, Sb* b) {
    int op = vm->bytecode[ip];
    if (op < 0 || op > OP_CAST_ARR) { sb_printf(b, "UNKNOWN %d", op); return ip + 1; }
    sb_puts(b, OP_NAMES[op]);
    int i = ip + 1;
    switch (op) {
        case OP_JMP: case OP_JZ: case OP_JNZ: sb_printf(b, " -> %d", vm->bytecode[i++]); break;
        case OP_SET: case OP_GET: sb_printf(b, " G[%d]", vm->bytecode[i++]); break;
        case OP_LVAR: case OP_SVAR: sb_printf(b, " FP[%d]", vm->bytecode[i++]); break;
        case OP_CALL: {
            int target = vm->bytecode[i++];
            int args = vm->bytecode[i++];
            sb_printf(b, " %s (%d args)", function_name(target), args);
            break;
        }
        case OP_ALLOC: case OP_HSET: case OP_HGET: {
            int a = vm->bytecode[i++];
            int c = vm->bytecode[i++];
            sb_printf(b, " %d, %d", a, c);
            break;
        }
        case OP_NATIVE: sb_printf(b, " #%d", vm->bytecode[i++]); break;
        case OP_ARR: sb_printf(b, " %d", vm->bytecode[i++]); break;
        case OP_CAST: case OP_CHECK_TYPE: case OP_CAST_ARR: sb_printf(b, " type %d", vm->bytecode[i++]); break;
        case OP_MAKE_ARR: {
            int count = vm->bytecode[i++];
            int type_id = vm->bytecode[i++];
            sb_printf(b, " %d, type %d", count, type_id);
            break;
        }
        case OP_EMBED: { int len = vm->bytecode[i++]; sb_printf(b, " (%d words)", len); i += len; break; }
        case OP_PSH_NUM: {
            int idx = vm->bytecode[i++];
            sb_puts(b, " ");
            format_number(b, (idx >= 0 && idx < vm->const_count) ? vm->constants[idx] : 0);
            break;
        }
        case OP_PSH_STR: { int idx = vm->bytecode[i++]; sb_puts(b, " "); format_value(b, idx, T_STR, 1); break; }
        case OP_PSH_ENUM: sb_printf(b, " [%d]", vm->bytecode[i++]); break;
        default: break;
    }
    return i;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

static void send_stopped(const char* reason, const char* description, const char* text) {
    flush_output();
    handle_count = 0;   // variable references only last until the program moves on
    state = ST_STOPPED;
    Sb b = {0};
    sb_puts(&b, "{\"reason\":");
    sb_json_str(&b, reason);
    sb_puts(&b, ",\"threadId\":1,\"allThreadsStopped\":true");
    if (description) { sb_puts(&b, ",\"description\":"); sb_json_str(&b, description); }
    if (text) { sb_puts(&b, ",\"text\":"); sb_json_str(&b, text); }
    sb_puts(&b, "}");
    send_event("stopped", b.s);
    free(b.s);
}

static void finish(int code) {
    flush_output();
    state = ST_FINISHED;
    char body[64];
    snprintf(body, sizeof(body), "{\"exitCode\":%d}", code);
    send_event("exited", body);
    send_event("terminated", "{}");
}

static void handle_message(char* body);

// Runs until a stop, the end of the program, or an error.
static void run_program(void) {
    state = ST_RUNNING;
    MyloConfig.repl_jmp_buf = &error_env;
    if (setjmp(error_env) != 0) {
        MyloConfig.repl_jmp_buf = NULL;
        // A runtime error, or the program asked to exit.
        if (strstr(last_error, "Mylo called `Exit()`")) { finish(1); return; }
        program_failed = true;
        // The error happened in the instruction just run.
        int at = vm->ip > 0 ? vm->ip - 1 : 0;
        const char* file = vm_source_file_at(vm, at);
        Sb text = {0};
        sb_printf(&text, "%s:%d: runtime error: %s\n", *file ? file : program_path, line_at(at), last_error);
        send_stderr(text.s);
        free(text.s);
        if (no_debug) { finish(1); return; }
        send_stopped("exception", "Runtime error", last_error);
        return;
    }

    int prev_line = line_at(vm->ip);
    int prev_file = file_at(vm->ip);
    bool first = true;
    int counter = 0;

    for (;;) {
        if (vm->ip >= vm->code_size) break;
        int line = line_at(vm->ip);
        int file = file_at(vm->ip);
        bool new_line = line != prev_line || file != prev_file;

        if (!first && new_line && line > 0 && !no_debug) {
            bool fresh = starts_line(vm->ip);
            if (fresh && has_breakpoint(file, line)) { MyloConfig.repl_jmp_buf = NULL; send_stopped("breakpoint", NULL, NULL); return; }
            int depth = (run_mode == RUN_CONTINUE) ? 0 : frame_depth();
            // Stepping into a call lands on the function's header, which only checks the
            // arguments, so carry on to its first statement.
            bool on_header = false;
            if (run_mode == RUN_STEP_IN && depth > step_depth) {
                int entry = current_function();
                on_header = entry >= 0 && line_at(entry) == line && file_at(entry) == file;
            }
            // Returning from a call to the line the step began on isn't a new line.
            bool back_where_started = depth == step_depth && line == step_line && file == step_file && !fresh;
            if ((run_mode == RUN_STEP_IN && !on_header && !back_where_started) ||
                (run_mode == RUN_STEP_OVER && depth <= step_depth && !back_where_started) ||
                (run_mode == RUN_STEP_OUT && depth < step_depth)) {
                MyloConfig.repl_jmp_buf = NULL;
                send_stopped("step", NULL, NULL);
                return;
            }
        }
        if (vm->bytecode[vm->ip] == OP_DEBUGGER && !no_debug) {
            vm->ip++;
            MyloConfig.repl_jmp_buf = NULL;
            send_stopped("breakpoint", "debugger statement", NULL);
            return;
        }
        prev_line = line;
        prev_file = file;
        first = false;

        if (vm_step(vm, false) == -1) break;

        // Look for requests now and then, so pause and new breakpoints work while running.
        if (++counter >= 2048) {
            counter = 0;
            if (input_pending()) {
                bool closed = false;
                char* msg;
                while ((msg = next_message(false, &closed)) != NULL) {
                    handle_message(msg);
                    free(msg);
                }
                if (closed) exit(0);
                if (pause_requested) {
                    pause_requested = false;
                    MyloConfig.repl_jmp_buf = NULL;
                    send_stopped("pause", NULL, NULL);
                    return;
                }
                if (state != ST_RUNNING) { MyloConfig.repl_jmp_buf = NULL; return; }
            }
        }
    }
    MyloConfig.repl_jmp_buf = NULL;
    finish(0);
}

static void resume(RunMode mode) {
    if (program_failed) { finish(1); return; }
    run_mode = mode;
    step_depth = frame_depth();
    step_line = line_at(vm->ip);
    step_file = file_at(vm->ip);
    run_program();
}

// Compiles the program. Returns false, having reported the error, if it doesn't compile.
static bool compile_program(void) {
    last_error[0] = '\0';
    MyloConfig.repl_jmp_buf = &error_env;
    if (setjmp(error_env) != 0) {
        MyloConfig.repl_jmp_buf = NULL;
        int line = 0;
        const char* at = strstr(last_error, "[Line ");
        if (at) line = atoi(at + 6);
        const char* message = last_error;
        const char* colon = strstr(last_error, "] Error: ");
        if (colon) message = colon + 9;
        const char* file = compiler_current_file();
        if (!file || !*file) file = program_path;

        Sb text = {0};
        sb_printf(&text, "%s:%d: error: %s\n", file, line, message);
        send_stderr(text.s);
        free(text.s);

        Sb body = {0};
        sb_printf(&body, "{\"line\":%d,\"message\":", line);
        sb_json_str(&body, message);
        sb_puts(&body, ",\"file\":");
        sb_json_str(&body, file);
        sb_puts(&body, "}");
        send_event("myloCompileError", body.s);
        free(body.s);
        return false;
    }
    compiler_set_main_file(vm, program_path);
    parse(vm, program_source);
    MyloConfig.repl_jmp_buf = NULL;
    return true;
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

static void handle_set_breakpoints(int seq, JVal* args) {
    JVal* source = j_get(args, "source");
    const char* path = j_str(j_get(source, "path"), program_path);
    int file = find_file_id(path);

    // Keep other files' breakpoints, replace this file's.
    int kept = 0;
    for (int i = 0; i < breakpoint_count; i++) {
        if (breakpoints[i].file != file) breakpoints[kept++] = breakpoints[i];
    }
    breakpoint_count = kept;

    static int requested[MAX_DAP_BREAKPOINTS];
    int requested_count = 0;
    JVal* list = j_get(args, "breakpoints");
    if (list && list->type == J_ARR) {
        for (JVal* bp = list->child; bp && requested_count < MAX_DAP_BREAKPOINTS; bp = bp->next)
            requested[requested_count++] = (int)j_num(j_get(bp, "line"), 0);
    } else {
        JVal* lines = j_get(args, "lines");   // the older form
        if (lines && lines->type == J_ARR) {
            for (JVal* l = lines->child; l && requested_count < MAX_DAP_BREAKPOINTS; l = l->next)
                requested[requested_count++] = (int)j_num(l, 0);
        }
    }

    Sb body = {0};
    sb_puts(&body, "{\"breakpoints\":[");
    for (int i = 0; i < requested_count; i++) {
        int actual = (file >= 0) ? nearest_code_line(file, requested[i]) : -1;
        bool verified = actual > 0;
        if (verified && breakpoint_count < MAX_DAP_BREAKPOINTS) {
            breakpoints[breakpoint_count].file = file;
            breakpoints[breakpoint_count].line = actual;
            breakpoint_count++;
        }
        if (i) sb_puts(&body, ",");
        sb_printf(&body, "{\"id\":%d,\"verified\":%s,\"line\":%d", i + 1, verified ? "true" : "false",
                  verified ? actual : requested[i]);
        if (!verified) sb_puts(&body, file < 0 ? ",\"message\":\"This file isn't part of the program\""
                                               : ",\"message\":\"No code on or after this line\"");
        sb_puts(&body, "}");
    }
    sb_puts(&body, "]}");
    send_response(seq, "setBreakpoints", true, body.s, NULL);
    free(body.s);
}

static void handle_stack_trace(int seq, JVal* args) {
    static Frame frames[MAX_FRAMES];
    int count = collect_frames(frames, MAX_FRAMES);
    int start = (int)j_num(j_get(args, "startFrame"), 0);
    int levels = (int)j_num(j_get(args, "levels"), 0);
    int end = (levels > 0 && start + levels < count) ? start + levels : count;

    Sb body = {0};
    sb_puts(&body, "{\"stackFrames\":[");
    for (int i = start; i < end; i++) {
        if (i > start) sb_puts(&body, ",");
        const char* file = vm_source_file_at(vm, frames[i].ip);
        if (!*file) file = program_path;
        const char* base = file;
        for (const char* p = file; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
        sb_printf(&body, "{\"id\":%d,\"name\":", i + 1);
        sb_json_str(&body, function_name(frames[i].func_addr));
        sb_puts(&body, ",\"source\":{\"name\":");
        sb_json_str(&body, base);
        sb_puts(&body, ",\"path\":");
        sb_json_str(&body, file);
        sb_printf(&body, "},\"line\":%d,\"column\":1,\"instructionPointerReference\":\"%d\"}", line_at(frames[i].ip), frames[i].ip);
    }
    sb_printf(&body, "],\"totalFrames\":%d}", count);
    send_response(seq, "stackTrace", true, body.s, NULL);
    free(body.s);
}

static void handle_scopes(int seq, JVal* args) {
    int frame = (int)j_num(j_get(args, "frameId"), 1) - 1;
    if (frame < 0) frame = 0;
    Sb body = {0};
    sb_printf(&body, "{\"scopes\":[{\"name\":\"Locals\",\"presentationHint\":\"locals\",\"variablesReference\":%d,\"expensive\":false},"
                     "{\"name\":\"Globals\",\"variablesReference\":%d,\"expensive\":false}]}", REF_FRAME_BASE + frame, REF_GLOBALS);
    send_response(seq, "scopes", true, body.s, NULL);
    free(body.s);
}

static void handle_variables(int seq, JVal* args) {
    int ref = (int)j_num(j_get(args, "variablesReference"), 0);
    int start = (int)j_num(j_get(args, "start"), 0);
    int count = (int)j_num(j_get(args, "count"), 0);
    Sb body = {0};
    sb_puts(&body, "{\"variables\":[");
    if (ref == REF_GLOBALS) {
        list_globals(&body);
    } else if (ref >= REF_HANDLE_BASE && ref - REF_HANDLE_BASE < handle_count) {
        list_children(&body, handles[ref - REF_HANDLE_BASE].value, start, count);
    } else if (ref >= REF_FRAME_BASE && ref < REF_HANDLE_BASE) {
        static Frame frames[MAX_FRAMES];
        int frame_count = collect_frames(frames, MAX_FRAMES);
        list_locals(&body, frames, frame_count, ref - REF_FRAME_BASE);
    }
    sb_puts(&body, "]}");
    send_response(seq, "variables", true, body.s, NULL);
    free(body.s);
}

static void handle_evaluate(int seq, JVal* args) {
    const char* expr = j_str(j_get(args, "expression"), "");
    int frame = (int)j_num(j_get(args, "frameId"), 1) - 1;
    static Frame frames[MAX_FRAMES];
    int frame_count = collect_frames(frames, MAX_FRAMES);
    if (frame < 0 || frame >= frame_count) frame = 0;

    double val = 0;
    int type = T_NUM;
    char err[256];
    if (!evaluate_path(expr, frames, frame_count, frame, &val, &type, err, sizeof(err))) {
        send_response(seq, "evaluate", false, "{}", err);
        return;
    }
    Sb text = {0};
    format_value(&text, val, type, 0);
    Sb body = {0};
    sb_puts(&body, "{\"result\":");
    sb_json_str(&body, text.s ? text.s : "");
    sb_puts(&body, ",\"type\":");
    sb_json_str(&body, type_name(type));
    sb_printf(&body, ",\"variablesReference\":%d}", make_handle(val, type));
    send_response(seq, "evaluate", true, body.s, NULL);
    free(text.s);
    free(body.s);
}

static void handle_vm_state(int seq) {
    Sb b = {0};
    sb_printf(&b, "{\"ip\":%d,\"sp\":%d,\"fp\":%d,\"line\":%d,\"file\":", vm->ip, vm->sp, vm->fp, line_at(vm->ip));
    sb_json_str(&b, vm_source_file_at(vm, vm->ip));
    sb_printf(&b, ",\"depth\":%d,\"codeSize\":%d,\"maxCode\":%d,\"stackCapacity\":%d,\"strCount\":%d,\"maxStrings\":%d,"
                  "\"maxStringLength\":%d,\"constCount\":%d,\"maxConstants\":%d,\"heapInitial\":%d,\"heapMax\":%d,"
                  "\"currentArena\":%d,\"functionCount\":%d,\"globalCount\":%d,\"arenas\":[",
              frame_depth(), vm->code_size, vm->max_code, vm->stack_capacity, vm->str_count, vm->max_strings,
              vm->max_string_length, vm->const_count, vm->max_constants, vm->heap_initial, vm->heap_max,
              vm->current_arena, vm->function_count, vm->global_symbol_count);
    bool first = true;
    for (int i = 0; i < MAX_ARENAS; i++) {
        MemoryArena* a = &vm->arenas[i];
        if (!a->memory && !a->active) continue;
        if (!first) sb_puts(&b, ",");
        first = false;
        sb_printf(&b, "{\"id\":%d,\"active\":%s,\"head\":%d,\"capacity\":%d,\"generation\":%d}",
                  i, a->active ? "true" : "false", a->head, a->capacity, a->generation);
    }
    sb_puts(&b, "]}");
    send_response(seq, "myloVmState", true, b.s, NULL);
    free(b.s);
}

static void handle_stack(int seq, JVal* args) {
    int total = vm->sp + 1;
    int start = (int)j_num(j_get(args, "start"), 0);
    int count = (int)j_num(j_get(args, "count"), 256);
    if (start < 0) start += total;   // a negative start counts back from the top
    if (start < 0) start = 0;
    if (count > 4096) count = 4096;
    static Frame frames[MAX_FRAMES];
    int frame_count = collect_frames(frames, MAX_FRAMES);

    Sb b = {0};
    sb_printf(&b, "{\"total\":%d,\"capacity\":%d,\"frames\":[", total, vm->stack_capacity);
    for (int i = 0; i < frame_count; i++) {
        if (i) sb_puts(&b, ",");
        sb_printf(&b, "{\"fp\":%d,\"name\":", frames[i].fp);
        sb_json_str(&b, function_name(frames[i].func_addr));
        sb_puts(&b, "}");
    }
    sb_puts(&b, "],\"slots\":[");
    for (int i = start; i < total && i < start + count; i++) {
        if (i > start) sb_puts(&b, ",");
        Sb text = {0};
        format_value(&text, vm->stack[i], vm->stack_types[i], 1);
        sb_printf(&b, "{\"index\":%d,\"type\":\"%s\",\"raw\":", i, type_name(vm->stack_types[i]));
        sb_json_num(&b, vm->stack[i]);
        sb_puts(&b, ",\"value\":");
        sb_json_str(&b, text.s ? text.s : "");
        sb_puts(&b, "}");
        free(text.s);
    }
    sb_puts(&b, "]}");
    send_response(seq, "myloStack", true, b.s, NULL);
    free(b.s);
}

static void handle_arena(int seq, JVal* args) {
    int id = (int)j_num(j_get(args, "arena"), 0);
    int start = (int)j_num(j_get(args, "start"), 0);
    int count = (int)j_num(j_get(args, "count"), 256);
    if (id < 0 || id >= MAX_ARENAS || !vm->arenas[id].memory) {
        send_response(seq, "myloArena", false, "{}", "no such arena");
        return;
    }
    MemoryArena* a = &vm->arenas[id];
    if (start < 0) start = 0;
    if (count > 4096) count = 4096;
    Sb b = {0};
    sb_printf(&b, "{\"arena\":%d,\"head\":%d,\"capacity\":%d,\"generation\":%d,\"active\":%s,\"slots\":[",
              id, a->head, a->capacity, a->generation, a->active ? "true" : "false");
    for (int i = start; i < a->capacity && i < start + count; i++) {
        if (i > start) sb_puts(&b, ",");
        sb_printf(&b, "{\"offset\":%d,\"type\":%d,\"raw\":", i, a->types[i]);
        sb_json_num(&b, a->memory[i]);
        unsigned long long bits;
        memcpy(&bits, &a->memory[i], sizeof(bits));
        sb_printf(&b, ",\"bits\":\"%016llx\"}", bits);
    }
    sb_puts(&b, "]}");
    send_response(seq, "myloArena", true, b.s, NULL);
    free(b.s);
}

static void handle_strings(int seq, JVal* args) {
    int start = (int)j_num(j_get(args, "start"), 0);
    int count = (int)j_num(j_get(args, "count"), 256);
    if (start < 0) start = 0;
    if (count > 4096) count = 4096;
    Sb b = {0};
    sb_printf(&b, "{\"total\":%d,\"max\":%d,\"strings\":[", vm->str_count, vm->max_strings);
    for (int i = start; i < vm->str_count && i < start + count; i++) {
        if (i > start) sb_puts(&b, ",");
        sb_printf(&b, "{\"id\":%d,\"length\":%d,\"text\":", i, (int)strlen(vm_string(vm, i)));
        sb_json_str_n(&b, vm_string(vm, i), 400);
        sb_puts(&b, "}");
    }
    sb_puts(&b, "]}");
    send_response(seq, "myloStrings", true, b.s, NULL);
    free(b.s);
}

static void handle_disassemble(int seq, JVal* args) {
    int around = (int)j_num(j_get(args, "around"), vm->ip);
    int count = (int)j_num(j_get(args, "count"), 64);
    if (count < 1) count = 1;
    if (count > 2048) count = 2048;
    // Instructions vary in length, so walk from the start, remembering where recent ones began.
    int back = count / 3;
    int ring_size = back + 1;
    int* ring = (int*)calloc((size_t)ring_size, sizeof(int));
    int seen = 0;
    int ip = 0;
    Sb scratch = {0};
    while (ip < vm->code_size && ip < around) {
        ring[seen % ring_size] = ip;
        seen++;
        scratch.len = 0;
        ip = disassemble_one(ip, &scratch);
    }
    int first = ip;
    if (seen > 0 && back > 0) {
        int take = back < seen ? back : seen;
        first = ring[(seen - take) % ring_size];
    }
    free(ring);
    free(scratch.s);

    Sb b = {0};
    sb_printf(&b, "{\"ip\":%d,\"instructions\":[", vm->ip);
    ip = first;
    for (int n = 0; n < count && ip < vm->code_size; n++) {
        Sb text = {0};
        int next = disassemble_one(ip, &text);
        if (n) sb_puts(&b, ",");
        sb_printf(&b, "{\"address\":%d,\"line\":%d,\"text\":", ip, line_at(ip));
        sb_json_str(&b, text.s ? text.s : "");
        sb_puts(&b, "}");
        free(text.s);
        ip = next;
    }
    sb_puts(&b, "]}");
    send_response(seq, "myloDisassemble", true, b.s, NULL);
    free(b.s);
}

static bool is_resume(const char* command) {
    return strcmp(command, "continue") == 0 || strcmp(command, "next") == 0 ||
           strcmp(command, "stepIn") == 0 || strcmp(command, "stepOut") == 0;
}

static void handle_message(char* body) {
    log_debug(">> %s", body);
    const char* p = body;
    JVal* msg = j_parse_value(&p, 0);
    if (!msg) return;
    int seq = (int)j_num(j_get(msg, "seq"), 0);
    const char* command = j_str(j_get(msg, "command"), "");
    JVal* args = j_get(msg, "arguments");
    bool stopped = state == ST_STOPPED;

    if (strcmp(command, "initialize") == 0) {
        send_response(seq, command, true,
            "{\"supportsConfigurationDoneRequest\":true,\"supportsTerminateRequest\":true,"
            "\"supportsEvaluateForHovers\":true,\"supportsDelayedStackTraceLoading\":true}", NULL);
    } else if (strcmp(command, "launch") == 0) {
        stop_on_entry = j_bool(j_get(args, "stopOnEntry"), true);
        no_debug = j_bool(j_get(args, "noDebug"), false);
        send_response(seq, command, true, "{}", NULL);
        if (compile_program()) send_event("initialized", "{}");
        else finish(1);
    } else if (strcmp(command, "setBreakpoints") == 0) {
        handle_set_breakpoints(seq, args);
    } else if (strcmp(command, "setExceptionBreakpoints") == 0 || strcmp(command, "setFunctionBreakpoints") == 0) {
        send_response(seq, command, true, "{\"breakpoints\":[]}", NULL);
    } else if (strcmp(command, "configurationDone") == 0) {
        send_response(seq, command, true, "{}", NULL);
        if (state == ST_FINISHED) {
            // The program didn't compile, and that has been reported.
        } else if (stop_on_entry && !no_debug) {
            // Skip the jumps over function bodies, so the program stops on its first statement.
            while (vm->ip < vm->code_size && line_at(vm->ip) == 0 && vm->bytecode[vm->ip] == OP_JMP) vm_step(vm, false);
            send_stopped("entry", NULL, NULL);
        } else if (!no_debug && has_breakpoint(file_at(vm->ip), line_at(vm->ip))) {
            send_stopped("breakpoint", NULL, NULL);
        } else {
            resume(RUN_CONTINUE);
        }
    } else if (strcmp(command, "threads") == 0) {
        send_response(seq, command, true, "{\"threads\":[{\"id\":1,\"name\":\"main\"}]}", NULL);
    } else if (strcmp(command, "pause") == 0) {
        send_response(seq, command, true, "{}", NULL);
        if (state == ST_RUNNING) pause_requested = true;
    } else if (strcmp(command, "disconnect") == 0 || strcmp(command, "terminate") == 0) {
        flush_output();
        send_response(seq, command, true, "{}", NULL);
        if (strcmp(command, "terminate") == 0 && state != ST_FINISHED) finish(0);
        exit(0);
    } else if (is_resume(command)) {
        if (!stopped) {
            send_response(seq, command, false, "{}", state == ST_FINISHED ? "the program has ended" : "the program is running");
        } else {
            send_response(seq, command, true, strcmp(command, "continue") == 0 ? "{\"allThreadsContinued\":true}" : "{}", NULL);
            if (strcmp(command, "continue") == 0) resume(RUN_CONTINUE);
            else if (strcmp(command, "next") == 0) resume(RUN_STEP_OVER);
            else if (strcmp(command, "stepIn") == 0) resume(RUN_STEP_IN);
            else resume(RUN_STEP_OUT);
        }
    } else if (!stopped) {
        // Inspecting state only makes sense while stopped.
        send_response(seq, command, false, "{}", state == ST_FINISHED ? "the program has ended" : "the program is running");
    } else if (strcmp(command, "stackTrace") == 0) {
        handle_stack_trace(seq, args);
    } else if (strcmp(command, "scopes") == 0) {
        handle_scopes(seq, args);
    } else if (strcmp(command, "variables") == 0) {
        handle_variables(seq, args);
    } else if (strcmp(command, "evaluate") == 0) {
        handle_evaluate(seq, args);
    } else if (strcmp(command, "myloVmState") == 0) {
        handle_vm_state(seq);
    } else if (strcmp(command, "myloStack") == 0) {
        handle_stack(seq, args);
    } else if (strcmp(command, "myloArena") == 0) {
        handle_arena(seq, args);
    } else if (strcmp(command, "myloStrings") == 0) {
        handle_strings(seq, args);
    } else if (strcmp(command, "myloDisassemble") == 0) {
        handle_disassemble(seq, args);
    } else {
        send_response(seq, command, false, "{}", "unsupported request");
    }
    j_free(msg);
}

void start_debug_adapter(VM* vm_in, const char* filename, const char* source_content) {
    vm = vm_in;
    snprintf(program_path, sizeof(program_path), "%s", filename ? filename : "main.mylo");
    char resolved[4096];
    if (realpath(program_path, resolved)) snprintf(program_path, sizeof(program_path), "%s", resolved);
    size_t len = strlen(source_content ? source_content : "");
    program_source = (char*)malloc(len + 1);
    if (!program_source) return;
    memcpy(program_source, source_content ? source_content : "", len + 1);

#ifdef _WIN32
    InitializeCriticalSection(&send_lock);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    // Keep the real stdout for the protocol, and send everything else written there to stderr.
    fflush(stdout);
    proto_in = 0;
    proto_out = dap_dup(1);
    dap_dup2(2, 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    MyloConfig.debug_mode = true;
    MyloConfig.print_callback = dap_print_callback;
    MyloConfig.error_callback = dap_error_callback;

    for (;;) {
        bool closed = false;
        char* body = next_message(true, &closed);
        if (!body) break;
        handle_message(body);
        free(body);
    }
    flush_output();
}

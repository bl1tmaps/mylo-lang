#define _CRT_SECURE_NO_WARNINGS
#include "vm_options.h"
#include "defines.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

MyloVMOptions MyloVMOpts = {
    STACK_SIZE, MAX_STRINGS, MAX_STRING_LENGTH, MAX_CODE, MAX_CONSTANTS,
    DEFAULT_ARENA_SLOTS, 0, false, 0, {{0}}
};

#define OPT(field) offsetof(MyloVMOptions, field)

static const MyloOptionInfo option_table[] = {
    { "stack_size", MYLO_OPT_INT, OPT(stack_size), STACK_SIZE, 64, 1 << 26, "slots",
      "Value slots on the VM stack.",
      "Each local variable, argument and temporary uses a slot, and each call uses two more. "
      "Raise it if deep recursion stops with a stack overflow." },
    { "max_strings", MYLO_OPT_INT, OPT(max_strings), MAX_STRINGS, 256, 1 << 24, "strings",
      "Distinct strings a program can create.",
      "Every distinct string, including each result of joining strings, takes an entry and is never "
      "freed. Raise it if a long-running program stops with a string pool overflow." },
    { "max_string_length", MYLO_OPT_INT, OPT(max_string_length), MAX_STRING_LENGTH, 64, 1 << 24, "bytes",
      "Longest string, in bytes.",
      "Strings built at runtime are cut short at this length. Literals in source code are limited "
      "separately by the compiler." },
    { "max_code", MYLO_OPT_INT, OPT(max_code), MAX_CODE, 4096, 1 << 28, "words",
      "Bytecode words a program can compile to.",
      "Raise it only for very large programs. Each word also stores its source line and file, "
      "so it costs 12 bytes." },
    { "max_constants", MYLO_OPT_INT, OPT(max_constants), MAX_CONSTANTS, 64, 1 << 24, "constants",
      "Distinct numeric constants in a program.",
      "Each distinct number literal in the source takes one entry." },
    { "heap_initial", MYLO_OPT_INT, OPT(heap_initial), DEFAULT_ARENA_SLOTS, 1024, 1 << 28, "slots",
      "Slots each memory arena starts with.",
      "Arenas hold arrays, structs, maps and bytes, and grow as needed. A larger start avoids "
      "growing later; each slot costs 12 bytes." },
    { "heap_max", MYLO_OPT_INT, OPT(heap_max), 0, 0, 1 << 28, "slots",
      "Slots one arena may grow to (0 = no limit).",
      "Stops a runaway program with an out of memory error instead of using all of the computer's "
      "memory. 0 allows growth up to the VM's address limit of 268,435,456 slots." },
    { "unbuffered_output", MYLO_OPT_BOOL, OPT(unbuffered_output), 0, 0, 1, "",
      "Write output straight away instead of buffering it.",
      "Useful when another program, such as an IDE, shows the output as it arrives." },
    { "module_path", MYLO_OPT_PATHS, OPT(module_paths), 0, 0, MYLO_VM_MAX_CONF_PATHS, "",
      "Extra folders to search for imports.",
      "Searched after the script's own folder. Separate several with ';', or repeat the key. "
      "Relative paths are relative to the config file." },
};

#undef OPT

const MyloOptionInfo* mylo_vm_option_table(int* out_count) {
    if (out_count) *out_count = (int)(sizeof(option_table) / sizeof(option_table[0]));
    return option_table;
}

const MyloOptionInfo* mylo_vm_option_find(const char* key) {
    int count = 0;
    const MyloOptionInfo* table = mylo_vm_option_table(&count);
    for (int i = 0; i < count; i++) {
        if (strcmp(table[i].key, key) == 0) return &table[i];
    }
    return NULL;
}

void mylo_vm_options_defaults(MyloVMOptions* opts) {
    memset(opts, 0, sizeof(*opts));
    int count = 0;
    const MyloOptionInfo* table = mylo_vm_option_table(&count);
    for (int i = 0; i < count; i++) {
        char* field = (char*)opts + table[i].offset;
        if (table[i].kind == MYLO_OPT_INT) *(int*)field = (int)table[i].default_value;
        else if (table[i].kind == MYLO_OPT_BOOL) *(bool*)field = table[i].default_value != 0;
    }
}

static void set_error(char* err, size_t err_len, const char* fmt, const char* a, const char* b) {
    if (err && err_len > 0) snprintf(err, err_len, fmt, a, b);
}

static char* trim(char* s) {
    while (isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_size(const char* text, long long* out) {
    errno = 0;
    char* end = NULL;
    long long value = strtoll(text, &end, 10);
    if (end == text || errno != 0) return false;
    while (*end == '_' || *end == ',') {
        // Allow 1_000_000 and 1,000,000 by parsing the rest as more digits.
        long long more = 0;
        char* next = NULL;
        more = strtoll(end + 1, &next, 10);
        if (next == end + 1) return false;
        int digits = (int)(next - (end + 1));
        for (int i = 0; i < digits; i++) value *= 10;
        value += more;
        end = next;
    }
    if (*end == 'k' || *end == 'K') { value *= 1024; end++; }
    else if (*end == 'm' || *end == 'M') { value *= 1024 * 1024; end++; }
    else if (*end == 'g' || *end == 'G') { value *= 1024LL * 1024 * 1024; end++; }
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') return false;
    *out = value;
    return true;
}

static bool parse_bool(const char* text, bool* out) {
    const char* yes[] = { "1", "true", "yes", "on" };
    const char* no[] = { "0", "false", "no", "off" };
    for (int i = 0; i < 4; i++) {
#ifdef _WIN32
        if (_stricmp(text, yes[i]) == 0) { *out = true; return true; }
        if (_stricmp(text, no[i]) == 0) { *out = false; return true; }
#else
        if (strcasecmp(text, yes[i]) == 0) { *out = true; return true; }
        if (strcasecmp(text, no[i]) == 0) { *out = false; return true; }
#endif
    }
    return false;
}

static bool add_paths(MyloVMOptions* opts, const char* value, const char* base_dir, char* err, size_t err_len) {
    char buffer[MYLO_VM_PATH_LENGTH * 4];
    snprintf(buffer, sizeof(buffer), "%s", value);
    char* cursor = buffer;
    while (cursor && *cursor) {
        char* sep = strchr(cursor, ';');
        if (sep) *sep = '\0';
        char* path = trim(cursor);
        if (*path) {
            if (opts->module_path_count >= MYLO_VM_MAX_CONF_PATHS) {
                set_error(err, err_len, "too many module_path entries (the limit is %s)%s", "16", "");
                return false;
            }
            char* dest = opts->module_paths[opts->module_path_count++];
            bool absolute = path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
            if (!absolute && base_dir && *base_dir) snprintf(dest, MYLO_VM_PATH_LENGTH, "%s/%s", base_dir, path);
            else snprintf(dest, MYLO_VM_PATH_LENGTH, "%s", path);
        }
        cursor = sep ? sep + 1 : NULL;
    }
    return true;
}

static bool set_option(MyloVMOptions* opts, const char* key, const char* value, const char* base_dir,
                       char* err, size_t err_len) {
    const MyloOptionInfo* info = mylo_vm_option_find(key);
    if (!info) {
        set_error(err, err_len, "unknown setting '%s'%s", key, "");
        return false;
    }
    char* field = (char*)opts + info->offset;

    if (info->kind == MYLO_OPT_INT) {
        long long number = 0;
        if (!parse_size(value, &number)) {
            set_error(err, err_len, "'%s' needs a whole number, not '%s'", key, value);
            return false;
        }
        if (number < info->min_value || number > info->max_value) {
            char range[96];
            snprintf(range, sizeof(range), "%lld to %lld", info->min_value, info->max_value);
            set_error(err, err_len, "'%s' must be from %s", key, range);
            return false;
        }
        *(int*)field = (int)number;
        return true;
    }
    if (info->kind == MYLO_OPT_BOOL) {
        bool flag = false;
        if (!parse_bool(value, &flag)) {
            set_error(err, err_len, "'%s' needs true or false, not '%s'", key, value);
            return false;
        }
        *(bool*)field = flag;
        return true;
    }
    return add_paths(opts, value, base_dir, err, err_len);
}

bool mylo_vm_options_set(MyloVMOptions* opts, const char* key, const char* value, char* err, size_t err_len) {
    return set_option(opts, key, value, NULL, err, err_len);
}

bool mylo_vm_options_set_pair(MyloVMOptions* opts, const char* pair, char* err, size_t err_len) {
    char buffer[MYLO_VM_PATH_LENGTH * 4];
    snprintf(buffer, sizeof(buffer), "%s", pair);
    char* eq = strchr(buffer, '=');
    if (!eq) {
        set_error(err, err_len, "expected key=value, not '%s'%s", pair, "");
        return false;
    }
    *eq = '\0';
    return set_option(opts, trim(buffer), trim(eq + 1), NULL, err, err_len);
}

bool mylo_vm_options_load(MyloVMOptions* opts, const char* path, char* err, size_t err_len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        set_error(err, err_len, "cannot open %s%s", path, "");
        return false;
    }

    // module_path entries are relative to the config file.
    char base_dir[MYLO_VM_PATH_LENGTH];
    snprintf(base_dir, sizeof(base_dir), "%s", path);
    char* slash = strrchr(base_dir, '/');
    char* backslash = strrchr(base_dir, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (slash) *slash = '\0';
    else strcpy(base_dir, ".");

    // A file names its paths in full, so they replace any from a previous config.
    bool cleared_paths = false;

    char line[MYLO_VM_PATH_LENGTH * 4];
    int line_no = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char* text = trim(line);
        if (*text == '\0') continue;

        char* eq = strchr(text, '=');
        if (!eq) {
            char where[64];
            snprintf(where, sizeof(where), "%d", line_no);
            set_error(err, err_len, "line %s: expected key = value%s", where, "");
            ok = false;
            break;
        }
        *eq = '\0';
        char* key = trim(text);
        char* value = trim(eq + 1);

        if (strcmp(key, "module_path") == 0 && !cleared_paths) {
            opts->module_path_count = 0;
            cleared_paths = true;
        }

        char detail[256];
        if (!set_option(opts, key, value, base_dir, detail, sizeof(detail))) {
            char where[64];
            snprintf(where, sizeof(where), "line %d", line_no);
            set_error(err, err_len, "%s: %s", where, detail);
            ok = false;
            break;
        }
    }
    fclose(f);
    return ok;
}

long long mylo_vm_options_get_int(const MyloVMOptions* opts, const MyloOptionInfo* info) {
    const char* field = (const char*)opts + info->offset;
    if (info->kind == MYLO_OPT_INT) return *(const int*)field;
    if (info->kind == MYLO_OPT_BOOL) return *(const bool*)field ? 1 : 0;
    return opts->module_path_count;
}

void mylo_vm_options_format(const MyloVMOptions* opts, const MyloOptionInfo* info, char* out, size_t out_len) {
    if (out_len == 0) return;
    out[0] = '\0';
    if (info->kind == MYLO_OPT_INT) {
        int value = (int)mylo_vm_options_get_int(opts, info);
        if (value != 0 && value % (1024 * 1024) == 0) snprintf(out, out_len, "%dm", value / (1024 * 1024));
        else if (value != 0 && value % 1024 == 0) snprintf(out, out_len, "%dk", value / 1024);
        else snprintf(out, out_len, "%d", value);
    } else if (info->kind == MYLO_OPT_BOOL) {
        snprintf(out, out_len, "%s", mylo_vm_options_get_int(opts, info) ? "true" : "false");
    } else {
        size_t used = 0;
        for (int i = 0; i < opts->module_path_count && used + 1 < out_len; i++) {
            int n = snprintf(out + used, out_len - used, "%s%s", i ? ";" : "", opts->module_paths[i]);
            if (n < 0) break;
            used += (size_t)n;
        }
    }
}

bool mylo_vm_options_save(const MyloVMOptions* opts, const char* path, char* err, size_t err_len) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        set_error(err, err_len, "cannot write %s%s", path, "");
        return false;
    }

    MyloVMOptions defaults;
    mylo_vm_options_defaults(&defaults);

    fprintf(f, "# Mylo VM settings. Sizes accept k and m suffixes (x1024, x1024*1024).\n");
    fprintf(f, "# A setting left out uses its default. Run `mylo --help-config` for details.\n\n");

    int count = 0;
    const MyloOptionInfo* table = mylo_vm_option_table(&count);
    for (int i = 0; i < count; i++) {
        const MyloOptionInfo* info = &table[i];
        char value[MYLO_VM_PATH_LENGTH * 4];
        char def[64];
        mylo_vm_options_format(opts, info, value, sizeof(value));
        mylo_vm_options_format(&defaults, info, def, sizeof(def));

        fprintf(f, "# %s", info->summary);
        if (info->kind != MYLO_OPT_PATHS) fprintf(f, " Default: %s", def);
        fprintf(f, "\n");

        if (info->kind == MYLO_OPT_PATHS) {
            if (opts->module_path_count == 0) fprintf(f, "# module_path = modules\n");
            for (int p = 0; p < opts->module_path_count; p++) fprintf(f, "module_path = %s\n", opts->module_paths[p]);
        } else {
            fprintf(f, "%s = %s\n", info->key, value);
        }
        fprintf(f, "\n");
    }

    bool ok = ferror(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) set_error(err, err_len, "failed writing %s%s", path, "");
    return ok;
}

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool mylo_vm_options_find(char* out, size_t out_len, const char* script_dir, const char* exe_dir) {
    const char* env = getenv("MYLO_VM_CONF");
    if (env && *env && file_exists(env)) {
        snprintf(out, out_len, "%s", env);
        return true;
    }
    const char* dirs[2] = { script_dir, exe_dir };
    for (int i = 0; i < 2; i++) {
        if (!dirs[i] || !*dirs[i]) continue;
        char candidate[MYLO_VM_PATH_LENGTH * 2];
        snprintf(candidate, sizeof(candidate), "%s/%s", dirs[i], MYLO_VM_CONF_NAME);
        if (file_exists(candidate)) {
            snprintf(out, out_len, "%s", candidate);
            return true;
        }
    }
    return false;
}

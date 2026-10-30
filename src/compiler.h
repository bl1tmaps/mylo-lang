#ifndef MYLO_COMPILER_H
#define MYLO_COMPILER_H

#include "defines.h"
#include <stdbool.h>
#include "vm.h" // Needed for VM* definition

// Symbol Table Entry
typedef struct {
    char name[MAX_IDENTIFIER];
    int addr;
    int type_id;
    bool is_array;
    int scope_depth;
} Symbol;

// Local Variable Entry
typedef struct {
    char name[MAX_IDENTIFIER];
    int offset;
    int type_id;
    bool is_array;
    int scope_depth;
} LocalSymbol;

typedef struct {
    char name[MAX_IDENTIFIER];
    int stack_offset; // Offset relative to FP
    int start_ip;     // Variable becomes valid here
    int end_ip;       // Variable dies here
} DebugSym;

typedef struct {
    char name[MAX_IDENTIFIER];
    int addr;
} FuncDebugInfo;

extern FuncDebugInfo funcs[MAX_GLOBALS];
extern int func_count;

typedef struct {
    char name[MAX_IDENTIFIER];
    int value;
} EnumEntry;

extern EnumEntry enum_entries[MAX_ENUM_MEMBERS];
extern int enum_entry_count;

#define MAX_DEBUG_SYMBOLS 4096
extern DebugSym debug_symbols[MAX_DEBUG_SYMBOLS];
extern int debug_symbol_count;

// Expose the tables to the Debugger
extern Symbol globals[MAX_GLOBALS];
extern int global_count;

extern LocalSymbol locals[MAX_GLOBALS];
extern int local_count;

// Compiler entry point
void parse(VM* vm, char *source);
void compile_to_c_source(VM* vm, const char *output_filename);
void generate_binding_c_source(VM* vm, const char *output_filename);
void compile_repl(VM* vm, char* source, int* out_start_ip);
void compiler_reset();
void create_standalone_executable(VM* vm, const char* output_filename, const char* host_exe_path);
#endif

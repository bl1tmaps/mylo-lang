#ifndef MYLO_VM_H
#define MYLO_VM_H

#include <stdbool.h>
#include "defines.h"
#include "stddef.h"

// --- Types ---
#define T_NUM  0
#define T_STR  1
#define T_OBJ  2
#define T_ENUM 3

// Return type for C-Blocks
typedef struct {
    double value;
    int type;
} MyloReturn;

// Helper Macros for C-Blocks
#define MYLO_RET_NUM(v) (MyloReturn){ .value = (double)(v), .type = T_NUM }
#define MYLO_RET_OBJ(v) (MyloReturn){ .value = (double)(v), .type = T_OBJ }

typedef struct {
    bool print_to_memory;
    bool debug_mode;
    bool build_mode;
    void (*print_callback)(const char *);
    void (*error_callback)(const char*);
    void* repl_jmp_buf;
} MyloConfigType;

extern MyloConfigType MyloConfig;

typedef enum {
    OP_PSH_NUM, OP_PSH_STR,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_LT, OP_EQ, OP_GT, OP_GE, OP_LE, OP_NEQ,
    OP_SET, OP_GET, OP_LVAR, OP_SVAR,
    OP_JMP, OP_JZ, OP_JNZ,
    OP_CALL, OP_RET,
    OP_PRN, OP_HLT, OP_CAT,
    OP_ALLOC, OP_HSET, OP_HGET, OP_DUP,
    OP_POP,
    OP_NATIVE,
    OP_ARR, OP_AGET, OP_ALEN, OP_SLICE,
    OP_MAP, OP_ASET,
    OP_MK_BYTES,
    OP_SLICE_SET,
    OP_IT_KEY,
    OP_IT_VAL,
    OP_IT_DEF,
    OP_EMBED,
    OP_MAKE_ARR,
    OP_NEW_ARENA,
    OP_DEL_ARENA,
    OP_SET_CTX,
    OP_MONITOR,
    OP_CAST,
    OP_CHECK_TYPE,
    OP_OR,
    OP_AND,
    OP_RANGE,
    OP_SCOPE_ENTER,
    OP_SCOPE_EXIT,
    OP_DEBUGGER,
    OP_PSH_ENUM
} OpCode;

extern const char *OP_NAMES[];

typedef struct {
    char name[64];
    int addr;
} VMFunction;

// --- GC Heap Struct ---
typedef struct {
    double** memory; // Array of double arrays (the data)
    int** types;     // Array of int arrays (the type info for data)
    int* sizes;      // Size of each allocation
    char* marked;    // Mark bits for GC
    int capacity;    // Max number of objects
    int count;       // Current allocated objects
    int next_free;   // Hint for next free index
} GCHeap;

// --- Debug Structures ---
typedef struct {
    char name[MAX_IDENTIFIER];
    int addr;
} VMSymbol;

typedef struct {
    char name[MAX_IDENTIFIER];
    int stack_offset;
    int start_ip;
    int end_ip;
} VMLocalInfo;

typedef struct {
    int arena_id;
    int head;     // The offset in the arena where memory started for this scope
    int fp;       // Frame pointer (safety check)
    int sp_at_entry;
} VMScope;


// Forward declaration
struct VM;
typedef void (*NativeFunc)(struct VM *);

typedef struct {
    char name[64];   // Name of the library (e.g., "raylib_binding.so")
    int start_index; // The VM Native ID index where this library starts
} Dependency;

typedef struct VM {
    double* stack;
    double* globals;
    double* constants;
    GCHeap heap;
    int* bytecode;
    int* lines;
    int* stack_types;
    int* global_types;
    char (*string_pool)[MAX_STRING_LENGTH];
    int code_size;
    int sp;
    int fp;
    int ip;
    int str_count;
    int const_count;
    char output_char_buffer[OUTPUT_BUFFER_SIZE];
    int output_mem_pos;
    VMFunction functions[MAX_VM_FUNCTIONS];
    int function_count;
    VMSymbol* global_symbols;
    int global_symbol_count;
    VMLocalInfo* local_symbols;
    int local_symbol_count;
    VMScope scope_stack[MAX_LOOP_NESTING]; // Reuse loop constant or define MAX_SCOPES 64
    int scope_sp;
    // --- Debugger Fields ---
    char* source_code;
    bool cli_debug_mode;
    int last_debug_line;
    // --- Native Interface ---
    NativeFunc natives[MAX_NATIVES];
    Dependency dependencies[MAX_DEPENDENCIES];
    int dependency_count;
} VM;

typedef struct {
    void (*push)(VM*, double, int);
    double (*pop)(VM*);
    int (*make_string)(VM*, const char*);
    double (*heap_alloc)(VM*, int);
    double* (*resolve_ptr)(VM*, double);
    double (*store_copy)(VM*, void*, size_t, const char*);
    double (*store_ptr)(VM*, void*, const char*);
    void* (*get_ref)(VM*, int, const char*);
    void (*free_ref)(VM*, int);
    NativeFunc* natives_array;
    char (*string_pool)[MAX_STRING_LENGTH];
} MyloAPI;

void vm_init(VM* vm);
void vm_cleanup(VM* vm);
void vm_push(VM* vm, double val, int type);
double vm_pop(VM* vm);
int make_string(VM* vm, const char *s);
int make_const(VM* vm, double val);
double heap_alloc(VM* vm, int size);
void run_vm_from(VM* vm, int start_ip, bool debug_trace);
void run_vm(VM* vm, bool debug_trace);
int vm_step(VM* vm, bool debug_trace);
double* vm_resolve_ptr(VM* vm, double ptr_val);
double* vm_resolve_ptr_safe(VM* vm, double ptr_val);
int* vm_resolve_type(VM* vm, double ptr_val);
double vm_store_copy(VM* vm, void* data, size_t size, const char* type_name);
double vm_store_ptr(VM* vm, void* ptr, const char* type_name);
void* vm_get_ref(VM* vm, int id, const char* expected_type_name);
void vm_free_ref(VM* vm, int id);
int vm_find_function(VM* vm, const char* name);
void vm_register_function(VM* vm, const char* name, int addr);
void print_recursive(VM* vm, double val, int type, int depth, int max_elem);
double vm_evacuate_object(VM* vm, double ptr_val, int target_head);
void enter_debugger(VM* vm);
void print_raw(VM* vm, const char* str);

// Expose GC allocator
void gc_collect(VM* vm);
// Pointer storage and retrieval
#define MYLO_STORE(val, type_name) vm_store_copy(vm, &(val), sizeof(val), type_name)
#define MYLO_RETRIEVE(id, c_type, type_name) (c_type*)vm_get_ref(vm, (int)(id), type_name)
#define MYLO_REGISTER(ptr, type_name) vm_store_ptr(vm, (void*)(ptr), type_name)

#define MYLO_STORE_TO_VM(vm, val, type_name) vm_store_copy(vm, &(val), sizeof(val), type_name)
#define MYLO_RETRIEVE_FROM_VM(vm, id, c_type, type_name) (c_type*)vm_get_ref(vm, (int)(id), type_name)
#define MYLO_REGISTER_IN_VM(vm, ptr, type_name) vm_store_ptr(vm, (void*)(ptr), type_name)

bool load_self_contained(VM* vm, const char* exe_path);

// DLL Loading functions
// Loads a shared library (.dll / .so) at the given path
// Returns an opaque handle, or NULL on failure
void* load_library(const char* path);

// Retrieves a function pointer from the library
void* get_symbol(void* lib_handle, const char* symbol_name);

// Helper to construct platform specific names (foo -> foo.dll or libfoo.so)
void get_lib_name(char* out, const char* base_name);

// errors
void mylo_runtime_error(VM* vm, const char* fmt, ...);
#endif

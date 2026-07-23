#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <ctype.h>
#include "vm.h"
#include <setjmp.h>
#include <stdarg.h>

#include "mylolib.h"
// Loader
#ifdef _WIN32
    #define _WINSOCKAPI_    // stops windows.h including winsock.h
    #include <windows.h>
    #define EXT ".dll"
#else
    #include <dlfcn.h>
    #define EXT ".so"
#endif

// --- Global Instance ---
// VM vm; // REMOVED GLOBAL VM
MyloConfigType MyloConfig = {false, false, false, NULL, NULL, NULL};

// --- Opcodes String Representation ---
const char *OP_NAMES[] = {
    "PSH_NUM", "PSH_STR",
    "ADD", "SUB", "MUL", "DIV", "MOD",
    "LT", "EQ", "GT", "GE", "LE", "NEQ",
    "SET", "GET", "LVAR", "SVAR",
    "JMP", "JZ", "JNZ",
    "CALL", "RET",
    "PRN", "HLT", "CAT",
    "ALLOC", "HSET", "HGET", "DUP",
    "POP",
    "NATIVE",
    "ARR", "AGET", "ALEN", "SLICE",
    "MAP", "ASET",
    "MK_BYTES",
    "SLICE_SET",
    "IT_KEY", "IT_VAL", "IT_DEF",
    "EMBED",
    "MAKE_ARR",
    "NEW_REG", "DEL_REG", "SET_CTX",
    "MONITOR",
    "CAST",
    "CHECK_TYPE",
    "OR",
    "AND",
    "RANGE","SCOPE_ENTER", "SCOPE_EXIT",
    "DEBUGGER",
    "PSH_ENUM"
};


void mylo_exit(int code) {
    if (MyloConfig.error_callback) {
        MyloConfig.error_callback("Mylo called `Exit()`");
    }
    exit(1);
}

void mylo_runtime_error(VM* vm, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);

    printf("[Line %d] Runtime Error: ", vm->lines[vm->ip - 1]);
    vsnprintf(buffer, 1024, fmt, args);
    printf("%s\n", buffer); // Print to console

    va_end(args);
    if (MyloConfig.debug_mode && MyloConfig.error_callback) {
        MyloConfig.error_callback(buffer);
        mylo_exit(1);
    }

    if (MyloConfig.repl_jmp_buf) {
        longjmp(*(jmp_buf*)MyloConfig.repl_jmp_buf, 1);
    } else {
        mylo_exit(1);
    }
}


#define RUNTIME_ERROR(fmt, ...) mylo_runtime_error(vm, fmt, ##__VA_ARGS__)
#define CHECK_STACK(count) if (vm->sp < (count) - 1) RUNTIME_ERROR("Stack Underflow")
#define CHECK_OBJ(depth) if (vm->stack_types[vm->sp - (depth)] != T_OBJ) RUNTIME_ERROR("Expected Object/Array")

// --- Helpers ---

int get_type_size(int heap_type) {
    switch(heap_type) {
        case TYPE_BYTES:
        case TYPE_BOOL_ARRAY: return 1;
        case TYPE_I16_ARRAY:
        case TYPE_F16_ARRAY:  return 2;
        case TYPE_I32_ARRAY:
        case TYPE_F32_ARRAY:  return 4;
        case TYPE_I64_ARRAY:
        default: return 8;
    }
}

// Simple Hash (djb2)
unsigned long vm_hash_str(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

// --- Reference Management ---

// [REPLACEMENT] Recursive Deep Evacuation
double vm_evacuate_object(VM* vm, double ptr_val, int target_head) {
    return ptr_val; // Evacuation not needed with GC
}
#define MAX_REFS 1024
typedef struct {
    void* ptr;
    unsigned long type_hash;
    bool is_copy;
} RefEntry;

static RefEntry ref_store[MAX_REFS];
static int ref_next_id = 0;

double vm_store_copy(VM* vm, void* data, size_t size, const char* type_name) {
    if (ref_next_id >= MAX_REFS) {
        printf("Runtime Error: Reference limit reached\n");
        return -1.0;
    }
    void* copy = malloc(size);
    if (!copy) return -1.0;
    memcpy(copy, data, size);

    int id = ref_next_id++;
    ref_store[id].ptr = copy;
    ref_store[id].type_hash = vm_hash_str(type_name);
    ref_store[id].is_copy = true;
    return (double)id;
}

double vm_store_ptr(VM* vm, void* ptr, const char* type_name) {
    if (ref_next_id >= MAX_REFS) return -1.0;
    int id = ref_next_id++;
    ref_store[id].ptr = ptr;
    ref_store[id].type_hash = vm_hash_str(type_name);
    ref_store[id].is_copy = false;
    return (double)id;
}

void* vm_get_ref(VM* vm, int id, const char* expected_type_name) {
    if (id < 0 || id >= ref_next_id) return NULL;
    unsigned long expected_hash = vm_hash_str(expected_type_name);
    if (ref_store[id].type_hash != expected_hash) return NULL;
    return ref_store[id].ptr;
}

void vm_free_ref(VM* vm, int id) {
    if (id >= 0 && id < ref_next_id) {
        if (ref_store[id].is_copy && ref_store[id].ptr) free(ref_store[id].ptr);
        ref_store[id].ptr = NULL;
        ref_store[id].type_hash = 0;
    }
}

double* vm_resolve_ptr_safe(VM* vm, double ptr_val) {
    int offset = UNPACK_OFFSET(ptr_val);
    if (offset < 0 || offset >= vm->heap.capacity) return NULL;
    if (vm->heap.memory[offset] == NULL) return NULL;
    return vm->heap.memory[offset];
}

// --- GC Management ---

static void mark_object(VM* vm, double ptr_val) {
    int offset = UNPACK_OFFSET(ptr_val);
    if (offset < 0 || offset >= vm->heap.capacity) return;
    if (vm->heap.memory[offset] == NULL || vm->heap.marked[offset]) return;
    
    vm->heap.marked[offset] = 1;
    
    // Trace children if it's an array or map
    double* base = vm->heap.memory[offset];
    int obj_type = (int)base[HEAP_OFFSET_TYPE];
    
    if (obj_type == TYPE_ARRAY) {
        int len = (int)base[HEAP_OFFSET_LEN];
        for (int i=0; i<len; i++) {
            if (vm->heap.types[offset][HEAP_HEADER_ARRAY + i] == T_OBJ) {
                mark_object(vm, base[HEAP_HEADER_ARRAY + i]);
            }
        }
    } else if (obj_type == TYPE_MAP) {
        int cap = (int)base[HEAP_OFFSET_CAP];
        int header_offset = HEAP_HEADER_MAP;
        for (int i = 0; i < cap; i++) {
            int key_type = vm->heap.types[offset][header_offset + (i * 2)];
            int val_type = vm->heap.types[offset][header_offset + (i * 2) + 1];
            double key = base[header_offset + (i * 2)];
            double val = base[header_offset + (i * 2) + 1];
            
            if (key_type == T_OBJ && key != 0.0) mark_object(vm, key);
            if (val_type == T_OBJ && val != 0.0) mark_object(vm, val);
        }
    } else if (obj_type >= 0) {
        // It's a struct! struct_id is obj_type, size is at base[1]
        int len = (int)base[1];
        for (int i = 0; i < len; i++) {
            if (vm->heap.types[offset][2 + i] == T_OBJ) {
                mark_object(vm, base[2 + i]);
            }
        }
    }
}

void gc_mark(VM* vm) {
    // 1. Mark Stack
    for (int i = 0; i <= vm->sp; i++) {
        if (vm->stack_types[i] == T_OBJ) mark_object(vm, vm->stack[i]);
    }
    // 2. Mark Globals
    if (vm->global_symbol_count > 0) {
        for (int i = 0; i < MAX_GLOBALS; i++) {
            if (vm->global_types[i] == T_OBJ) mark_object(vm, vm->globals[i]);
        }
    }
}

void gc_sweep(VM* vm) {
    for (int i = 0; i < vm->heap.capacity; i++) {
        if (vm->heap.memory[i] != NULL) {
            if (!vm->heap.marked[i]) {
                free(vm->heap.memory[i]);
                free(vm->heap.types[i]);
                vm->heap.memory[i] = NULL;
                vm->heap.types[i] = NULL;
                vm->heap.sizes[i] = 0;
                vm->heap.count--;
            }
        }
        vm->heap.marked[i] = 0; // Clear mark bits for next GC
    }
}

void gc_collect(VM* vm) {
    gc_mark(vm);
    gc_sweep(vm);
}

void vm_cleanup(VM* vm) {
    if (vm->bytecode) { free(vm->bytecode); vm->bytecode = NULL; }
    if (vm->lines) { free(vm->lines); vm->lines = NULL; }
    if (vm->stack) { free(vm->stack); vm->stack = NULL; }
    if (vm->stack_types) { free(vm->stack_types); vm->stack_types = NULL; }
    if (vm->globals) { free(vm->globals); vm->globals = NULL; }
    if (vm->global_types) { free(vm->global_types); vm->global_types = NULL; }
    if (vm->constants) { free(vm->constants); vm->constants = NULL; }

    for (int i = 0; i < vm->heap.capacity; i++) {
        if (vm->heap.memory[i]) {
            free(vm->heap.memory[i]);
            free(vm->heap.types[i]);
        }
    }
    if (vm->heap.memory) free(vm->heap.memory);
    if (vm->heap.types) free(vm->heap.types);
    if (vm->heap.sizes) free(vm->heap.sizes);
    if (vm->heap.marked) free(vm->heap.marked);
    if (vm->string_pool) { free(vm->string_pool); vm->string_pool = NULL; }

    for (int i = 0; i < ref_next_id; i++) {
        if (ref_store[i].is_copy && ref_store[i].ptr) free(ref_store[i].ptr);
        ref_store[i].ptr = NULL;
    }
    ref_next_id = 0;

    vm->sp = -1; vm->ip = 0; vm->str_count = 0;
    if (vm->global_symbols) { free(vm->global_symbols); vm->global_symbols = NULL; }
    if (vm->local_symbols) { free(vm->local_symbols); vm->local_symbols = NULL; }
}
void register_stdlib(VM* vm) {
    int i = 0;
    while (std_library[i].name != NULL) {
        vm->natives[i] = std_library[i].func;
        i++;
    }
}
void vm_init(VM* vm) {
    vm->scope_sp = 0;
    // OPTIMIZATION: Soft Reset if VM is already initialized
    // Instead of freeing everything and calloc-ing (zeroing) the whole heap again,
    // we reuse the pointers and only zero-out the memory that was *used* in the last run.
    if (vm->bytecode) {
        // 1. Reset Registers & Counters
        vm->sp = -1;
        vm->ip = 0;
        vm->fp = 0;
        vm->code_size = 0;
        vm->str_count = 0;
        vm->const_count = 0;
        vm->function_count = 0;
        vm->output_mem_pos = 0;
        vm->output_char_buffer[0] = '\0';
        vm->global_symbol_count = 0;
        vm->local_symbol_count = 0;
        vm->cli_debug_mode = false;
        vm->last_debug_line = -1;
        vm->dependency_count = 0; // [NEW] Reset dependencies

        // 2. Clear Small Buffers
        memset(vm->natives, 0, sizeof(vm->natives));
        memset(vm->globals, 0, MAX_GLOBALS * sizeof(double));
        memset(vm->global_types, 0, MAX_GLOBALS * sizeof(int));
        // Note: Stack doesn't need explicit clearing as sp=-1 protects it,
        // but if paranoid: memset(vm->stack, 0, STACK_SIZE * sizeof(double));

        // 3. Clean up References
        for (int i = 0; i < ref_next_id; i++) {
            if (ref_store[i].is_copy && ref_store[i].ptr) free(ref_store[i].ptr);
            ref_store[i].ptr = NULL;
        }
        ref_next_id = 0;

        // 4. Reset GC Heap
        for (int i = 0; i < vm->heap.capacity; i++) {
            if (vm->heap.memory[i]) {
                free(vm->heap.memory[i]);
                free(vm->heap.types[i]);
                vm->heap.memory[i] = NULL;
                vm->heap.types[i] = NULL;
                vm->heap.sizes[i] = 0;
            }
        }
        memset(vm->heap.marked, 0, vm->heap.capacity);
        vm->heap.count = 0;
        vm->heap.next_free = 0;

        // 6. Clear Debug Symbols (Compiler re-allocates these)
        if (vm->global_symbols) { free(vm->global_symbols); vm->global_symbols = NULL; }
        if (vm->local_symbols) { free(vm->local_symbols); vm->local_symbols = NULL; }

        register_stdlib(vm);
        return; // Done! Skip the heavy allocations below.
    }

    // --- Cold Start (First Run) ---

    vm->bytecode = (int*)malloc(MAX_CODE * sizeof(int));
    vm->lines    = (int*)malloc(MAX_CODE * sizeof(int));

    vm->stack       = (double*)calloc(STACK_SIZE, sizeof(double));
    vm->stack_types = (int*)calloc(STACK_SIZE, sizeof(int));
    vm->globals      = (double*)calloc(MAX_GLOBALS, sizeof(double));
    vm->global_types = (int*)calloc(MAX_GLOBALS, sizeof(int));

    vm->constants = (double*)malloc(MAX_CONSTANTS * sizeof(double));
    vm->string_pool = malloc(MAX_STRINGS * MAX_STRING_LENGTH);
    vm->heap.capacity = 1024; // Start small, grow dynamically
    vm->heap.memory = (double**)calloc(vm->heap.capacity, sizeof(double*));
    vm->heap.types = (int**)calloc(vm->heap.capacity, sizeof(int*));
    vm->heap.sizes = (int*)calloc(vm->heap.capacity, sizeof(int));
    vm->heap.marked = (char*)calloc(vm->heap.capacity, sizeof(char));
    vm->heap.count = 0;
    vm->heap.next_free = 0;

    if (!vm->bytecode || !vm->stack) {
        fprintf(stderr, "Critical Error: Failed to allocate VM memory\n");
        mylo_exit(1);

    }

    vm->sp = -1;
    vm->ip = 0;
    vm->fp = 0;
    vm->code_size = 0;
    vm->str_count = 0;
    vm->const_count = 0;
    vm->function_count = 0;
    vm->output_mem_pos = 0;
    vm->output_char_buffer[0] = '\0';
    vm->global_symbols = NULL;
    vm->global_symbol_count = 0;
    vm->local_symbols = NULL;
    vm->local_symbol_count = 0;
    vm->source_code = NULL;
    vm->cli_debug_mode = false;
    vm->last_debug_line = -1;
    vm->dependency_count = 0; // [NEW] Initialize dependency count
    memset(vm->natives, 0, sizeof(vm->natives));
    register_stdlib(vm);
}

double* vm_resolve_ptr(VM* vm, double ptr_val) {
    int offset = UNPACK_OFFSET(ptr_val);
    if (offset < 0 || offset >= vm->heap.capacity) {
        RUNTIME_ERROR("Access violation: Invalid GC Pointer %d", offset);
        return NULL;
    }
    if (vm->heap.memory[offset] == NULL) {
        RUNTIME_ERROR("Access violation: GC Object %d is freed", offset);
        return NULL;
    }
    return vm->heap.memory[offset];
}

int* vm_resolve_type(VM* vm, double ptr_val) {
    int offset = UNPACK_OFFSET(ptr_val);
    if (offset < 0 || offset >= vm->heap.capacity) return NULL;
    if (vm->heap.memory[offset] == NULL) return NULL;
    return vm->heap.types[offset];
}

double heap_alloc(VM* vm, int size) {
    if (vm->heap.count >= vm->heap.capacity * 0.75) { // Trigger GC at 75% capacity
        gc_collect(vm);
        if (vm->heap.count >= vm->heap.capacity * 0.75) {
            // Still full after GC? Grow the heap!
            int new_cap = vm->heap.capacity * 2;
            vm->heap.memory = (double**)realloc(vm->heap.memory, new_cap * sizeof(double*));
            vm->heap.types = (int**)realloc(vm->heap.types, new_cap * sizeof(int*));
            vm->heap.sizes = (int*)realloc(vm->heap.sizes, new_cap * sizeof(int));
            vm->heap.marked = (char*)realloc(vm->heap.marked, new_cap * sizeof(char));
            
            // Zero-out the new memory
            for (int i = vm->heap.capacity; i < new_cap; i++) {
                vm->heap.memory[i] = NULL;
                vm->heap.types[i] = NULL;
                vm->heap.sizes[i] = 0;
                vm->heap.marked[i] = 0;
            }
            vm->heap.capacity = new_cap;
        }
    }
    
    // Find a free slot
    int offset = -1;
    for (int i = vm->heap.next_free; i < vm->heap.capacity; i++) {
        if (vm->heap.memory[i] == NULL) {
            offset = i;
            break;
        }
    }
    if (offset == -1) {
        for (int i = 0; i < vm->heap.next_free; i++) {
            if (vm->heap.memory[i] == NULL) {
                offset = i;
                break;
            }
        }
    }
    
    if (offset == -1) {
        printf("Error: GC Heap Overflow (Fragmentation)!\n");
        mylo_exit(1);
    }
    
    vm->heap.next_free = (offset + 1) % vm->heap.capacity;
    vm->heap.memory[offset] = (double*)calloc(size, sizeof(double));
    vm->heap.types[offset] = (int*)calloc(size, sizeof(int));
    vm->heap.sizes[offset] = size;
    vm->heap.count++;
    
    return PACK_PTR(offset);
}

void vm_push(VM* vm, double val, int type) {
    if (vm->sp >= STACK_SIZE - 1) { printf("Error: Stack Overflow\n"); mylo_exit(1);}
    vm->sp++;
    vm->stack[vm->sp] = val;
    vm->stack_types[vm->sp] = type;
}

double vm_pop(VM* vm) {
    CHECK_STACK(1);
    return vm->stack[vm->sp--];
}

int make_string(VM* vm, const char *s) {
    for (int j = 0; j < vm->str_count; j++) {
        if (strcmp(vm->string_pool[j], s) == 0) return j;
    }
    if (vm->str_count >= MAX_STRINGS) { printf("Error: String Pool Overflow\n"); mylo_exit(1);}
    strncpy(vm->string_pool[vm->str_count], s, MAX_STRING_LENGTH - 1);
    return vm->str_count++;
}

int make_const(VM* vm, double val) {
    for (int i = 0; i < vm->const_count; i++) {
        if (vm->constants[i] == val) return i;
    }
    vm->constants[vm->const_count] = val;
    return vm->const_count++;
}

void vm_register_function(VM* vm, const char* name, int addr) {
    if (vm->function_count >= MAX_VM_FUNCTIONS) { printf("VM Limit: Too many functions.\n"); mylo_exit(1); }
    VMFunction* f = &vm->functions[vm->function_count++];
    strncpy(f->name, name, 63);
    f->name[63] = '\0';
    f->addr = addr;
}

int vm_find_function(VM* vm, const char* name) {
    for (int i = 0; i < vm->function_count; i++) {
        if (strcmp(vm->functions[i].name, name) == 0) return vm->functions[i].addr;
    }
    return -1;
}

// --- Printing ---

void print_raw(VM* vm, const char* str) {
    if (MyloConfig.debug_mode && MyloConfig.print_callback) {
        MyloConfig.print_callback(str);
    } else if (MyloConfig.print_to_memory) {
        vm->output_mem_pos += sprintf(vm->output_char_buffer + vm->output_mem_pos, "%s", str);
    } else {
        printf("%s", str);
    }
}

void print_recursive(VM* vm, double val, int type, int depth, int max_elem) {
    if (depth > 10) { print_raw(vm, "..."); return; }
    char buf[64];

    if (type == T_NUM) {
        if (val == (int)val) sprintf(buf, "%d", (int)val);
        else sprintf(buf, "%g", val);
        print_raw(vm, buf);
    } else if (type == T_STR) {
        if (depth > 0) print_raw(vm, "\"");
        print_raw(vm, vm->string_pool[(int)val]);
        if (depth > 0) print_raw(vm, "\"");
    } 
    else if (type == T_ENUM) {
        unsigned long long packed = (unsigned long long)val;
        int str_id = (packed >> 16) & 0xFFFF;
        if (depth > 0) print_raw(vm, "\"");
        print_raw(vm, vm->string_pool[str_id]);
        if (depth > 0) print_raw(vm, "\"");
    }
    else if (type == T_OBJ) {
        int offset = UNPACK_OFFSET(val);

        if (offset < 0 || offset >= vm->heap.capacity || vm->heap.memory[offset] == NULL) {
            print_raw(vm, "[Freed/Invalid Object]");
            return;
        }

        double* base = vm_resolve_ptr_safe(vm, val);
        if (!base) {
            if (val == 0.0) print_raw(vm, "null");
            else print_raw(vm, "[Invalid Ref]");
            return;
        }
        int* types = vm_resolve_type(vm, val);
        int obj_type = (int)base[HEAP_OFFSET_TYPE];

        if (obj_type == TYPE_ARRAY) {
            print_raw(vm, "[");
            int len = (int)base[HEAP_OFFSET_LEN];
            int limit = (max_elem != -1 && len > max_elem) ? max_elem : len;

            for (int i = 0; i < limit; i++) {
                if (i > 0) print_raw(vm, ", ");
                print_recursive(vm, base[HEAP_HEADER_ARRAY + i], types[HEAP_HEADER_ARRAY + i], depth + 1, max_elem);
            }
            if (len > limit) print_raw(vm, ", ...");
            print_raw(vm, "]");
        } else if (obj_type == TYPE_MAP) {
            print_raw(vm, "{");
            int count = (int)base[HEAP_OFFSET_COUNT];
            int limit = (max_elem != -1 && count > max_elem) ? max_elem : count;

            double* data = vm_resolve_ptr(vm, base[HEAP_OFFSET_DATA]);
            int* data_types = vm_resolve_type(vm, base[HEAP_OFFSET_DATA]);

            if(data) {
                for (int i = 0; i < limit; i++) {
                    if (i > 0) print_raw(vm, ", ");
                    // Let print_recursive handle the key dynamically
                    print_recursive(vm, data[i * 2], data_types[i * 2], 0, max_elem);
                    print_raw(vm, ": ");
                    print_recursive(vm, data[i * 2 + 1], data_types[i * 2 + 1], depth + 1, max_elem);
                }
                if (count > limit) print_raw(vm, ", ...");
            }
            print_raw(vm, "}");
        } else if (obj_type == TYPE_BYTES) {
            int len = (int)base[HEAP_OFFSET_LEN];
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            print_raw(vm, "b\"");
            int limit = (max_elem != -1 && len > max_elem) ? max_elem : len;
            for (int i = 0; i < limit; i++) {
                if (b[i] >= 32 && b[i] <= 126) sprintf(buf, "%c", b[i]);
                else sprintf(buf, "\\x%02X", b[i]);
                print_raw(vm, buf);
            }
            if (len > limit) print_raw(vm, "...");
            print_raw(vm, "\"");
        } else if (obj_type <= TYPE_I16_ARRAY && obj_type >= TYPE_BOOL_ARRAY) {
            print_raw(vm, "[");
            int len = (int)base[HEAP_OFFSET_LEN];
            char* data = (char*)&base[HEAP_HEADER_ARRAY];
            int limit = (max_elem != -1 && len > max_elem) ? max_elem : len;
            for (int i = 0; i < limit; i++) {
                if (i > 0) print_raw(vm, ", ");
                if (obj_type == TYPE_I32_ARRAY) sprintf(buf, "%d", ((int*)data)[i]);
                else if (obj_type == TYPE_F32_ARRAY) sprintf(buf, "%g", ((float*)data)[i]);
                else if (obj_type == TYPE_I16_ARRAY) sprintf(buf, "%d", ((short*)data)[i]);
                else if (obj_type == TYPE_I64_ARRAY) sprintf(buf, "%lld", ((long long*)data)[i]);
                else if (obj_type == TYPE_BOOL_ARRAY) { print_raw(vm, ((unsigned char*)data)[i] ? "true" : "false"); continue; }
                print_raw(vm, buf);
            }
            if (len > limit) print_raw(vm, ", ...");
            print_raw(vm, "]");
        } else {
            sprintf(buf, "[Struct Ref:%d]", UNPACK_OFFSET(val));
            print_raw(vm, buf);
        }
    }
}

// --- SLEEK TUI Debugger Implementation ---

// UI Colors (Background Blocks)
#define UI_BG_HEADER    "\033[48;5;236m" // Dark Slate for headers
#define UI_BG_CONTENT   "\033[48;5;234m" // Very Dark Gray for content
#define UI_BG_SELECTED  "\033[48;5;24m"  // Muted Blue for selected line
#define UI_BG_BP        "\033[48;5;88m"  // Dark Red for breakpoint line

// Text Colors
#define UI_FG_WHITE     "\033[38;5;255m"
#define UI_FG_GRAY      "\033[38;5;246m"
#define UI_FG_ACCENT    "\033[38;5;117m" // Light Cyan
#define UI_FG_RED       "\033[38;5;196m"
#define UI_RST          "\033[0m"
#define UI_BOLD         "\033[1m"
#define EXTEND          "\033[K"         // Extend bg color to end of line

#define LINES_CONTEXT 12

static int dbg_breakpoints[64];
static int dbg_bp_count = 0;

void dbg_print_source_window(VM* vm, int current_line, int context_lines) {
    if (!vm->source_code) {
        printf("  [No source code available]\n");
        return;
    }

    // Header
    printf(UI_BG_HEADER UI_BOLD UI_FG_WHITE "  SOURCE CODE  " EXTEND UI_RST "\n");

    int line = 1;
    char* ptr = vm->source_code;
    char* start = ptr;

    while (*ptr) {
        if (*ptr == '\n') {
            if (line >= current_line - context_lines && line <= current_line + context_lines) {
                int len = (int)(ptr - start);
                bool is_bp = false;
                for(int i=0; i<dbg_bp_count; i++) if(dbg_breakpoints[i] == line) is_bp = true;

                // Determine line background and marker
                const char* bg = UI_BG_CONTENT;
                const char* marker = "  ";
                const char* num_col = UI_FG_GRAY;

                if (line == current_line) {
                    bg = UI_BG_SELECTED;
                    marker = is_bp ? UI_FG_RED "● " : UI_FG_ACCENT "➤ ";
                    num_col = UI_FG_WHITE;
                } else if (is_bp) {
                    marker = UI_FG_RED "● ";
                }

                printf("%s%s%s%4d │ %.*s" EXTEND UI_RST "\n", bg, marker, num_col, line, len, start);
            }
            start = ptr + 1;
            line++;
        }
        ptr++;
    }
}

void dbg_print_state_window(VM* vm) {
    printf(UI_BG_HEADER UI_BOLD UI_FG_WHITE "  VM STATE  " EXTEND UI_RST "\n");
    printf(UI_BG_CONTENT UI_FG_ACCENT "  FP: %-4d  SP: %-4d" EXTEND UI_RST "\n", vm->fp, vm->sp);

    // Globals
    printf(UI_BG_CONTENT UI_FG_GRAY "  -- Globals --" EXTEND UI_RST "\n");
    for (int i = 0; i < vm->global_symbol_count; i++) {
        if (i > 5) {
            printf(UI_BG_CONTENT UI_FG_GRAY "    ... (%d more)" EXTEND UI_RST "\n", vm->global_symbol_count - 5);
            break;
        }
        int addr = vm->global_symbols[i].addr;
        printf(UI_BG_CONTENT UI_FG_WHITE "    %-12s: " UI_FG_ACCENT, vm->global_symbols[i].name);
        print_recursive(vm, vm->globals[addr], vm->global_types[addr], 0, 1);
        printf(EXTEND UI_RST "\n");
    }

    // Locals
    printf(UI_BG_CONTENT UI_FG_GRAY "  -- Locals --" EXTEND UI_RST "\n");
    bool found = false;
    if (vm->local_symbols) {
        for (int i = 0; i < vm->local_symbol_count; i++) {
            VMLocalInfo* sym = &vm->local_symbols[i];
            if ((vm->ip - 1) >= sym->start_ip && (sym->end_ip == -1 || (vm->ip - 1) <= sym->end_ip)) {
                int stack_idx = vm->fp + sym->stack_offset;
                if (stack_idx <= vm->sp) {
                    printf(UI_BG_CONTENT UI_FG_WHITE "    %-12s: " UI_FG_ACCENT, sym->name);
                    print_recursive(vm, vm->stack[stack_idx], vm->stack_types[stack_idx], 0, 1);
                    printf(EXTEND UI_RST "\n");
                    found = true;
                }
            }
        }
    }
    if (!found) printf(UI_BG_CONTENT UI_FG_GRAY "    (None)" EXTEND UI_RST "\n");
}

// Helpers for interactive commands
double* find_debug_var(VM* vm, char* name, int* out_type) {
    // 1. Search Locals
    if (vm->local_symbols) {
        for (int i = 0; i < vm->local_symbol_count; i++) {
            VMLocalInfo* sym = &vm->local_symbols[i];
            // Check if variable is active in current scope
            if (strcmp(sym->name, name) == 0 && (vm->ip - 1) >= sym->start_ip && (sym->end_ip == -1 || (vm->ip - 1) <= sym->end_ip)) {
                int stack_idx = vm->fp + sym->stack_offset;
                if (stack_idx <= vm->sp) {
                    if (out_type) *out_type = vm->stack_types[stack_idx];
                    return &vm->stack[stack_idx];
                }
            }
        }
    }
    // 2. Search Globals
    for (int i = 0; i < vm->global_symbol_count; i++) {
        if (strcmp(vm->global_symbols[i].name, name) == 0) {
            int addr = vm->global_symbols[i].addr;
            if (out_type) *out_type = vm->global_types[addr];
            return &vm->globals[addr];
        }
    }
    return NULL;
}

void enter_debugger(VM* vm) {
    // Clear screen (ANSI)
    printf("\033[2J\033[H");

    int current_line = vm->lines[vm->ip > 0 ? vm->ip - 1 : 0];

    dbg_print_source_window(vm, current_line, LINES_CONTEXT);
    dbg_print_state_window(vm);

    printf(UI_BG_HEADER UI_BOLD UI_FG_WHITE "  DEBUG CONSOLE  " EXTEND UI_RST "\n");

    while (1) {
        printf(UI_BG_CONTENT UI_FG_ACCENT "mylo-db > " UI_RST);
        char buf[128];
        if (!fgets(buf, 128, stdin)) return;

        // Trim newline
        buf[strcspn(buf, "\n")] = 0;

        if (buf[0] == 'n' && buf[1] == '\0') { // Next Line
            int start_line = current_line;
            while (vm->ip < vm->code_size) {
                 int op = vm_step(vm, false);
                 if (op == -1) exit(0);
                 if (op == OP_DEBUGGER) break;
                 int new_line = vm->lines[vm->ip];
                 if (new_line != start_line && new_line != 0) break;
            }
            enter_debugger(vm);
            return;
        }
        else if (buf[0] == 's' && buf[1] == '\0') { // Step
            int op = vm_step(vm, true);
            if (op == -1) exit(0);
            enter_debugger(vm);
            return;
        }
        else if (buf[0] == 'c' && buf[1] == '\0') { // Continue
            return;
        }
        else if (buf[0] == 'b' && buf[1] == ' ') { // Breakpoint
            int line = atoi(buf + 2);
            if (line > 0) {
                dbg_breakpoints[dbg_bp_count++] = line;
                printf("Breakpoint set at line %d\n", line);
            }
        }
        else if (buf[0] == 'p' && buf[1] == ' ') { // Print variable
            char* var_name = buf + 2;
            while(*var_name == ' ') var_name++;
            int type = 0;
            double* ptr = find_debug_var(vm, var_name, &type);
            if (ptr) {
                print_recursive(vm, *ptr, type, 0, -1);
                printf("\n");
            } else {
                printf("Variable '%s' not found.\n", var_name);
            }
        }
        else if (strncmp(buf, "set ", 4) == 0) { // Set variable
            char* ptr = buf + 4;
            while(*ptr == ' ') ptr++;
            char* var_name = ptr;
            char* val_str = strchr(ptr, ' ');
            if (val_str) {
                *val_str = '\0';
                val_str++;
                while(*val_str == ' ') val_str++; // Skip spaces

                int type = 0;
                double* val_ptr = find_debug_var(vm, var_name, &type);
                if (val_ptr) {
                    if (val_str[0] == '"') {
                        // String literal
                        val_str++;
                        char* end = strrchr(val_str, '"');
                        if (end) *end = '\0';
                        *val_ptr = (double)make_string(vm, val_str);
                        printf("Updated %s to \"%s\"\n", var_name, val_str);
                    } else {
                        // Number
                        double v = strtod(val_str, NULL);
                        *val_ptr = v;
                        printf("Updated %s to %g\n", var_name, v);
                    }
                } else {
                    printf("Variable '%s' not found.\n", var_name);
                }
            } else {
                printf("Usage: set <var> <value>\n");
            }
        }
        else if (buf[0] == 'q') {
            exit(0);
        }
        else {
            printf("Commands:\n");
            printf("  n          Next line\n");
            printf("  s          Step instruction\n");
            printf("  c          Continue\n");
            printf("  b <line>   Set breakpoint\n");
            printf("  p <var>    Print variable\n");
            printf("  set <x> v  Set variable x to v\n");
            printf("  q          Quit\n");
        }
    }
}

// ... Logic Implementation ... (Math, etc unchanged) ...
static void broadcast_math(VM* vm, int op, double obj_val, double scalar_val, int scalar_type, bool obj_is_lhs) {
    double* base = vm_resolve_ptr(vm, obj_val);
    int* types = vm_resolve_type(vm, obj_val);
    int hType = (int)base[0];
    int len = (int)base[1];

    double newPtr = heap_alloc(vm, len + HEAP_HEADER_ARRAY);
    double* newBase = vm_resolve_ptr(vm, newPtr);
    int* newTypes = vm_resolve_type(vm, newPtr);

    newBase[0] = TYPE_ARRAY;
    newBase[1] = (double)len;

    for(int i=0; i<len; i++) {
        double el = 0;
        int elType = T_NUM;
        if(hType == TYPE_BYTES) {
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            el = (double)b[i];
        } else if (hType == TYPE_ARRAY) {
            el = base[HEAP_HEADER_ARRAY+i];
            elType = types[HEAP_HEADER_ARRAY+i];
        } else if (hType == TYPE_I32_ARRAY) el = (double)((int*)&base[HEAP_HEADER_ARRAY])[i];
        else if (hType == TYPE_F32_ARRAY) el = (double)((float*)&base[HEAP_HEADER_ARRAY])[i];

        if (scalar_type == T_NUM && elType == T_NUM) {
             double lhs = obj_is_lhs ? el : scalar_val;
             double rhs = obj_is_lhs ? scalar_val : el;
             double res = 0;
             if (op == OP_ADD) res = lhs + rhs;
             else if (op == OP_SUB) res = lhs - rhs;
             else if (op == OP_MUL) res = lhs * rhs;
             else if (op == OP_DIV) res = lhs / rhs;
             else if (op == OP_MOD) res = fmod(lhs, rhs);
             newBase[HEAP_HEADER_ARRAY+i] = res;
             newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
        }
        // --- NEW STRING BROADCAST SUPPORT ---
        else if (op == OP_ADD && scalar_type == T_STR && elType == T_STR) {
             double lhs = obj_is_lhs ? el : scalar_val;
             double rhs = obj_is_lhs ? scalar_val : el;
             char buf[MAX_STRING_LENGTH * 2];
             snprintf(buf, sizeof(buf), "%s%s", vm->string_pool[(int)lhs], vm->string_pool[(int)rhs]);
             int id = make_string(vm, buf);
             newBase[HEAP_HEADER_ARRAY+i] = (double)id;
             newTypes[HEAP_HEADER_ARRAY+i] = T_STR;
        }
        // --- NEW BYTES BROADCAST SUPPORT ---
        else if (op == OP_ADD && scalar_type == T_OBJ && elType == T_OBJ) {
             double* o1 = vm_resolve_ptr_safe(vm, obj_is_lhs ? el : scalar_val);
             double* o2 = vm_resolve_ptr_safe(vm, obj_is_lhs ? scalar_val : el);
             if (o1 && o2 && (int)o1[0] == TYPE_BYTES && (int)o2[0] == TYPE_BYTES) {
                 int l1 = (int)o1[1];
                 int l2 = (int)o2[1];
                 double new_bytes = heap_alloc(vm, ((l1 + l2) + 7) / 8 + 2);
                 double* nb = vm_resolve_ptr(vm, new_bytes);
                 nb[0] = TYPE_BYTES;
                 nb[1] = (double)(l1 + l2);
                 unsigned char* dst = (unsigned char*)&nb[2];
                 memcpy(dst, (unsigned char*)&o1[2], l1);
                 memcpy(dst + l1, (unsigned char*)&o2[2], l2);

                 newBase[HEAP_HEADER_ARRAY+i] = new_bytes;
                 newTypes[HEAP_HEADER_ARRAY+i] = T_OBJ;
             } else {
                 newBase[HEAP_HEADER_ARRAY+i] = 0; newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
             }
        }
        else {
             newBase[HEAP_HEADER_ARRAY+i] = 0;
             newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
        }
    }
    vm_push(vm, newPtr, T_OBJ);
}

static void exec_math_op(VM* vm, int op) {
    CHECK_STACK(2);
    double b = vm_pop(vm); int tb = vm->stack_types[vm->sp + 1];
    double a = vm->stack[vm->sp]; int ta = vm->stack_types[vm->sp];
    
    bool is_num_a = (ta == T_NUM || ta == T_ENUM);
    bool is_num_b = (tb == T_NUM || tb == T_ENUM);

    if (is_num_a && is_num_b) {
        double val_a = (ta == T_ENUM) ? (double)((unsigned long long)a & 0xFFFF) : a; // <-- Change here
        double val_b = (tb == T_ENUM) ? (double)((unsigned long long)b & 0xFFFF) : b; // <-- Change here
        double res = 0;
        if (op == OP_ADD) res = val_a + val_b;
        else if (op == OP_SUB) res = val_a - val_b;
        else if (op == OP_MUL) res = val_a * val_b;
        else if (op == OP_DIV) res = val_a / val_b;
        else if (op == OP_MOD) res = fmod(val_a, val_b);
        vm->stack[vm->sp] = res;
        vm->stack_types[vm->sp] = T_NUM; // Returns standard number
        return;
    }

    if (ta == T_NUM && tb == T_NUM) {
        double res = 0;
        if (op == OP_ADD) res = a + b;
        else if (op == OP_SUB) res = a - b;
        else if (op == OP_MUL) res = a * b;
        else if (op == OP_DIV) res = a / b;
        else if (op == OP_MOD) res = fmod(a, b);
        vm->stack[vm->sp] = res;
        vm->stack_types[vm->sp] = T_NUM;
        return;
    }
    if (op == OP_ADD && ta == T_STR && tb == T_STR) {
        char buf[MAX_STRING_LENGTH * 2];
        snprintf(buf, sizeof(buf), "%s%s", vm->string_pool[(int)a], vm->string_pool[(int)b]);
        int id = make_string(vm, buf);
        vm->stack[vm->sp] = (double)id;
        vm->stack_types[vm->sp] = T_STR;
        return;
    }

    // --- ARRAY & BYTES & TYPED ARRAY CONCATENATION ---
    if (op == OP_ADD && ta == T_OBJ && tb == T_OBJ) {
        double* pa = vm_resolve_ptr_safe(vm, a);
        double* pb = vm_resolve_ptr_safe(vm, b);
        if (pa && pb) {
            int typeA = (int)pa[HEAP_OFFSET_TYPE];
            int typeB = (int)pb[HEAP_OFFSET_TYPE];
            int lenA = (int)pa[HEAP_OFFSET_LEN];
            int lenB = (int)pb[HEAP_OFFSET_LEN];

            // 1. Generic Arrays
            if (typeA == TYPE_ARRAY && typeB == TYPE_ARRAY) {
                int* typesA = vm_resolve_type(vm, a);
                int* typesB = vm_resolve_type(vm, b);

                double ptr = heap_alloc(vm, lenA + lenB + HEAP_HEADER_ARRAY);
                double* base = vm_resolve_ptr(vm, ptr);
                int* types = vm_resolve_type(vm, ptr);

                base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
                base[HEAP_OFFSET_LEN] = (double)(lenA + lenB);

                for(int i=0; i<lenA; i++) {
                    base[HEAP_HEADER_ARRAY + i] = pa[HEAP_HEADER_ARRAY + i];
                    types[HEAP_HEADER_ARRAY + i] = typesA[HEAP_HEADER_ARRAY + i];
                }
                for(int i=0; i<lenB; i++) {
                    base[HEAP_HEADER_ARRAY + lenA + i] = pb[HEAP_HEADER_ARRAY + i];
                    types[HEAP_HEADER_ARRAY + lenA + i] = typesB[HEAP_HEADER_ARRAY + i];
                }
                vm->stack[vm->sp] = ptr; // overwrite 'a'
                vm->stack_types[vm->sp] = T_OBJ;
                return;
            }
            // 2. Byte Arrays
            else if (typeA == TYPE_BYTES && typeB == TYPE_BYTES) {
                 // TYPE_BYTES layout: [TYPE, LEN, byte0, byte1...] (Offsets: 0, 1, 2...)
                 double ptr = heap_alloc(vm, ((lenA + lenB) + 7) / 8 + 2);
                 double* base = vm_resolve_ptr(vm, ptr);
                 base[0] = TYPE_BYTES;
                 base[1] = (double)(lenA + lenB);

                 unsigned char* dst = (unsigned char*)&base[2];
                 unsigned char* srcA = (unsigned char*)&pa[2];
                 unsigned char* srcB = (unsigned char*)&pb[2];

                 memcpy(dst, srcA, lenA);
                 memcpy(dst + lenA, srcB, lenB);

                 vm->stack[vm->sp] = ptr;
                 vm->stack_types[vm->sp] = T_OBJ;
                 return;
            }
            // 3. Typed Arrays
            else if (typeA == typeB && typeA <= TYPE_I16_ARRAY && typeA >= TYPE_BOOL_ARRAY) {
                int elem_size = get_type_size(typeA);
                int total_len = lenA + lenB;
                // Calculate doubles needed: header(2) + data
                // Data size in bytes: total_len * elem_size
                // Doubles for data: (bytes + 7) / 8
                int doubles_needed = 2 + (total_len * elem_size + 7) / 8;

                double ptr = heap_alloc(vm, doubles_needed);
                double* base = vm_resolve_ptr(vm, ptr);

                base[0] = (double)typeA;
                base[1] = (double)total_len;

                char* dst = (char*)&base[2];
                char* srcA = (char*)&pa[2];
                char* srcB = (char*)&pb[2];

                int bytesA = lenA * elem_size;
                int bytesB = lenB * elem_size;

                memcpy(dst, srcA, bytesA);
                memcpy(dst + bytesA, srcB, bytesB);

                vm->stack[vm->sp] = ptr;
                vm->stack_types[vm->sp] = T_OBJ;
                return;
            }
        }
    }
    // ---------------------------------------

    if (ta == T_OBJ || tb == T_OBJ) {
        double arrVal = (ta == T_OBJ) ? a : b;
        double scalVal = (ta == T_OBJ) ? b : a;
        int scalType = (ta == T_OBJ) ? tb : ta;
        bool objIsLhs = (ta == T_OBJ);
        vm_pop(vm);
        broadcast_math(vm, op, arrVal, scalVal, scalType, objIsLhs);
        return;
    }
    
    if (op == OP_ADD && (ta == T_STR || tb == T_STR)) {
        char s1[MAX_STRING_LENGTH], s2[MAX_STRING_LENGTH];

        if (ta == T_STR) strncpy(s1, vm->string_pool[(int)a], MAX_STRING_LENGTH-1);
        else if (ta == T_ENUM) strncpy(s1, vm->string_pool[((unsigned int)a >> 16) & 0xFFFF], MAX_STRING_LENGTH-1);
        else snprintf(s1, MAX_STRING_LENGTH, "%g", a);
        s1[MAX_STRING_LENGTH-1] = '\0';

        if (tb == T_STR) strncpy(s2, vm->string_pool[(int)b], MAX_STRING_LENGTH-1);
        else if (tb == T_ENUM) strncpy(s2, vm->string_pool[((unsigned int)b >> 16) & 0xFFFF], MAX_STRING_LENGTH-1);
        else snprintf(s2, MAX_STRING_LENGTH, "%g", b);
        s2[MAX_STRING_LENGTH-1] = '\0';

        char res[MAX_STRING_LENGTH * 2];
        snprintf(res, sizeof(res), "%s%s", s1, s2);
        int id = make_string(vm, res);
        vm->stack[vm->sp] = (double)id;
        vm->stack_types[vm->sp] = T_STR;
        return;
    }
    RUNTIME_ERROR("Invalid types for math operation");
}

static void exec_compare_op(VM* vm, int op) {
    CHECK_STACK(2);
    double b = vm_pop(vm); int tb = vm->stack_types[vm->sp + 1];
    double a = vm->stack[vm->sp]; int ta = vm->stack_types[vm->sp];
    
    // Extract integer if comparing enums
    double val_a = (ta == T_ENUM) ? (double)((unsigned long long)a & 0xFFFF) : a;
    double val_b = (tb == T_ENUM) ? (double)((unsigned long long)b & 0xFFFF) : b;

    double res = 0;
    switch(op) {
        case OP_LT: res = (val_a < val_b); break;
        case OP_GT: res = (val_a > val_b); break;
        case OP_LE: res = (val_a <= val_b); break;
        case OP_GE: res = (val_a >= val_b); break;
        case OP_EQ: res = (val_a == val_b); break;
        case OP_NEQ: res = (val_a != val_b); break;
    }
    vm->stack[vm->sp] = res;
    vm->stack_types[vm->sp] = T_NUM;
}

static void exec_var_op(VM* vm, int op) {
    int arg = vm->bytecode[vm->ip++];
    
    if (op == OP_SET) {
        CHECK_STACK(1);
        vm->globals[arg] = vm->stack[vm->sp];
        vm->global_types[arg] = vm->stack_types[vm->sp];
        vm->sp--;
        // NO GLOBAL PROTECTION: Top-level scripts must cleanly sweep loop memory!
        
    } else if (op == OP_GET) {
        vm_push(vm, vm->globals[arg], vm->global_types[arg]);
    } else if (op == OP_LVAR) {
        int fp = (int)vm->fp;
        vm_push(vm, vm->stack[fp + arg], vm->stack_types[fp + arg]);
    } else if (op == OP_SVAR) {
        CHECK_STACK(1);
        int fp = (int)vm->fp;
        double val = vm->stack[vm->sp];
        int type = vm->stack_types[vm->sp];
        int target_idx = fp + arg;
        
        vm->stack[target_idx] = val;
        vm->stack_types[target_idx] = type;
        vm->sp--;
        
        // Smart Local Protection
        if (type == T_OBJ) {
            int obj_offset = UNPACK_OFFSET(val);
            // GC handles memory, no explicit protection needed
        }
    }
}

static void exec_flow_op(VM* vm, int op) {
    if (op == OP_JMP) {
        vm->ip = vm->bytecode[vm->ip];
    } else if (op == OP_JZ) {
        int target = vm->bytecode[vm->ip++];
        if (vm_pop(vm) == 0.0) vm->ip = target;
    } else if (op == OP_JNZ) {
        int target = vm->bytecode[vm->ip++];
        if (vm_pop(vm) != 0.0) vm->ip = target;
    } else if (op == OP_CALL) {
        int target = vm->bytecode[vm->ip++];
        int argc = vm->bytecode[vm->ip++];
        CHECK_STACK(argc);
        int args_start = vm->sp - argc + 1;
        for(int i=0; i<argc; i++) {
            vm->stack[vm->sp + 2 - i] = vm->stack[vm->sp - i];
            vm->stack_types[vm->sp + 2 - i] = vm->stack_types[vm->sp - i];
        }
        vm->stack[args_start] = (double)vm->ip;
        vm->stack[args_start+1] = (double)vm->fp;
        vm->stack_types[args_start] = T_NUM;
        vm->stack_types[args_start+1] = T_NUM;
        vm->sp += 2;
        vm->fp = args_start + 2;
        vm->ip = target;
    } else if (op == OP_RET) {
        CHECK_STACK(1);
        double rv = vm->stack[vm->sp];
        int rt = vm->stack_types[vm->sp];

        while (vm->scope_sp > 0) {
            VMScope* scope = &vm->scope_stack[vm->scope_sp - 1];
            if (scope->fp != vm->fp) break;
            vm->scope_sp--; // Pop the scope
        }

        // Standard Return Logic
        vm->sp = vm->fp - 3;
        int fp = (int)vm->fp;
        vm->fp = (int)vm->stack[fp-1];
        vm->ip = (int)vm->stack[fp-2];
        vm_push(vm, rv, rt);
    }
}


static void exec_alloc_op(VM* vm, int op) {
    if (op == OP_ALLOC) {
        int size = vm->bytecode[vm->ip++];
        int struct_id = vm->bytecode[vm->ip++];
        // Alloc size + 2 to store (ID, SIZE, Fields...)
        double ptr = heap_alloc(vm, size + 2);
        double* base = vm_resolve_ptr(vm, ptr);
        base[0] = (double)struct_id;
        base[1] = (double)size; // Store size!
        vm_push(vm, ptr, T_OBJ);
    } else if (op == OP_HSET) {
        int off = vm->bytecode[vm->ip++];
        int expected_id = vm->bytecode[vm->ip++];
        CHECK_STACK(2);
        double v = vm_pop(vm); int t = vm->stack_types[vm->sp+1];
        double p = vm->stack[vm->sp];
        double* base = vm_resolve_ptr(vm, p);
        int* types = vm_resolve_type(vm, p);
        if((int)base[0] != expected_id) RUNTIME_ERROR("HSET Type mismatch");
        base[2+off] = v; // Offset by 2
        types[2+off] = t;
    } else if (op == OP_HGET) {
        int off = vm->bytecode[vm->ip++];
        int expected_id = vm->bytecode[vm->ip++];
        CHECK_STACK(1);
        double p = vm->stack[vm->sp];
        double* base = vm_resolve_ptr(vm, p);
        int* types = vm_resolve_type(vm, p);
        if((int)base[0] != expected_id) RUNTIME_ERROR("HGET Type mismatch");
        vm->stack[vm->sp] = base[2+off]; // Offset by 2
        vm->stack_types[vm->sp] = types[2+off];
    }
}


static void exec_array_op(VM* vm, int op) {
    if (op == OP_ARR) {
        int count = vm->bytecode[vm->ip++];
        double ptr = heap_alloc(vm, count+2);
        double* base = vm_resolve_ptr(vm, ptr);
        int* types = vm_resolve_type(vm, ptr);
        base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        base[HEAP_OFFSET_LEN] = (double)count;
        for(int i=count; i>0; i--) {
            base[HEAP_HEADER_ARRAY+i-1] = vm->stack[vm->sp];
            types[HEAP_HEADER_ARRAY+i-1] = vm->stack_types[vm->sp];
            vm->sp--;
        }
        vm_push(vm, ptr, T_OBJ);
    } else if (op == OP_AGET) {
        CHECK_STACK(2);
        double key = vm_pop(vm); int kt = vm->stack_types[vm->sp+1];
        double ptr = vm_pop(vm);

        double* base = vm_resolve_ptr(vm, ptr);
        int* types = vm_resolve_type(vm, ptr);
        int type = (int)base[HEAP_OFFSET_TYPE];

        if (type <= TYPE_ARRAY && type != TYPE_MAP) {
            int idx = (int)key;
            int len = (int)base[HEAP_OFFSET_LEN];
            if (idx < 0) idx += len;
            if (idx < 0 || idx >= len) RUNTIME_ERROR("Index OOB");

            if (type == TYPE_ARRAY) {
                vm_push(vm, base[HEAP_HEADER_ARRAY+idx], types[HEAP_HEADER_ARRAY+idx]);
            } else {
                char* data = (char*)&base[HEAP_HEADER_ARRAY];
                double res = 0;
                switch(type) {
                    case TYPE_BYTES:      res = (double)((unsigned char*)data)[idx]; break;
                    case TYPE_BOOL_ARRAY: res = (double)((unsigned char*)data)[idx]; break;
                    case TYPE_I16_ARRAY:  res = (double)((short*)data)[idx]; break;
                    case TYPE_I32_ARRAY:  res = (double)((int*)data)[idx]; break;
                    case TYPE_F32_ARRAY:  res = (double)((float*)data)[idx]; break;
                    case TYPE_I64_ARRAY:  res = (double)((long long*)data)[idx]; break;
                }
                vm_push(vm, res, T_NUM);
            }
        } else if (type == TYPE_MAP) {
            int count = (int)base[HEAP_OFFSET_COUNT];
            double* data = vm_resolve_ptr(vm, base[HEAP_OFFSET_DATA]);
            int* data_types = vm_resolve_type(vm, base[HEAP_OFFSET_DATA]);
            bool found = false;
            for(int i=0; i<count; i++) {
                // Ensure the types match
                if(data[i*2] == key && data_types[i*2] == kt) {
                    vm_push(vm, data[i*2 + 1], data_types[i*2 + 1]);
                    found = true; break;
                }
            }
            if (!found) {
                int empty = make_string(vm, "");
                vm_push(vm, (double)empty, T_STR);
            }
        }
    } else if (op == OP_ALEN) {
        CHECK_STACK(1);
        double ptr = vm_pop(vm);
        double* base = vm_resolve_ptr(vm, ptr);
        int type = (int)base[HEAP_OFFSET_TYPE];
        if (type == TYPE_MAP) vm_push(vm, base[HEAP_OFFSET_COUNT], T_NUM);
        else vm_push(vm, base[HEAP_OFFSET_LEN], T_NUM);
    } else if (op == OP_ASET) {
        CHECK_STACK(3);
        double val = vm_pop(vm); int vt = vm->stack_types[vm->sp+1];
        double key = vm_pop(vm); int kt = vm->stack_types[vm->sp+1];
        double ptr = vm->stack[vm->sp];
        double* base = vm_resolve_ptr(vm, ptr);
        int* types = vm_resolve_type(vm, ptr);
        int type = (int)base[0];

        vm_pop(vm);

        if (type == TYPE_ARRAY) {
            int idx = (int)key;
            base[2+idx] = val; types[2+idx] = vt;
        } else if (type == TYPE_BYTES) {
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            b[(int)key] = (unsigned char)val;
        } else if (type == TYPE_MAP) {
            int cap = (int)base[1];
            int count = (int)base[2];
            double data_ptr = base[3];
            double* data = vm_resolve_ptr(vm, data_ptr);
            int* data_types = vm_resolve_type(vm, data_ptr);
            bool found = false;
            for(int i=0; i<count; i++) {
                // Add `kt` type check so numbers and strings don't collide
                if (data[i*2] == key && data_types[i*2] == kt) {
                    data[i*2+1] = val; data_types[i*2+1] = vt;
                    found = true; break;
                }
            }
            if(!found) {
                if (count >= cap) {
                    int new_cap = cap * 2;
                    double new_data_ptr = heap_alloc(vm, new_cap*2);
                    double* new_data = vm_resolve_ptr(vm, new_data_ptr);
                    int* new_types = vm_resolve_type(vm, new_data_ptr);
                    for(int i=0; i<count*2; i++) {
                         new_data[i] = data[i]; new_types[i] = data_types[i];
                    }
                    base[1] = (double)new_cap;
                    base[3] = new_data_ptr;
                    data = new_data; data_types = new_types;

                }
                data[count*2] = key;
                data_types[count*2] = kt; // Use the actual type instead of T_STR
                data[count*2+1] = val;
                data_types[count*2+1] = vt;
                base[2] = (double)(count+1);
            }
        }
        else {
             int idx = (int)key;
             char* data = (char*)&base[HEAP_HEADER_ARRAY];
             switch(type) {
                case TYPE_I32_ARRAY: ((int*)data)[idx] = (int)val; break;
                case TYPE_F32_ARRAY: ((float*)data)[idx] = (float)val; break;
                case TYPE_I16_ARRAY: ((short*)data)[idx] = (short)val; break;
                case TYPE_I64_ARRAY: ((long long*)data)[idx] = (long long)val; break;
                case TYPE_BOOL_ARRAY: ((unsigned char*)data)[idx] = (unsigned char)val; break;
             }
        }
        vm_push(vm, val, vt);
    } else if (op == OP_MAP) {
        int cap = MAP_INITIAL_CAP;
        double map = heap_alloc(vm, 4);
        double data = heap_alloc(vm, cap*2);
        double* base = vm_resolve_ptr(vm, map);
        base[0] = TYPE_MAP;
        base[1] = (double)cap;
        base[2] = 0;
        base[3] = data;
        vm_push(vm, map, T_OBJ);
    } else if (op == OP_MAKE_ARR) {
        int count = vm->bytecode[vm->ip++];
        int type_id = vm->bytecode[vm->ip++];
        int elem_size = get_type_size(type_id);
        int doubles_needed = (count * elem_size + 7) / 8;

        double ptr = heap_alloc(vm, doubles_needed + HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(vm, ptr);
        base[0] = (double)type_id;
        base[1] = (double)count;
        char* data_ptr = (char*)&base[HEAP_HEADER_ARRAY];

        for(int i = count - 1; i >= 0; i--) {
            double val = vm->stack[vm->sp--];
            switch(type_id) {
                case TYPE_BYTES:
                case TYPE_BOOL_ARRAY: data_ptr[i] = (unsigned char)val; break;
                case TYPE_I16_ARRAY: ((short*)data_ptr)[i] = (short)val; break;
                case TYPE_I32_ARRAY: ((int*)data_ptr)[i] = (int)val; break;
                case TYPE_F32_ARRAY: ((float*)data_ptr)[i] = (float)val; break;
                case TYPE_I64_ARRAY: ((long long*)data_ptr)[i] = (long long)val; break;
            }
        }
        vm_push(vm, ptr, T_OBJ);
    }
}

static void exec_slice_op(VM* vm, int op) {
    if (op == OP_SLICE) {
        CHECK_STACK(3);
        double e = vm_pop(vm);
        double s = vm_pop(vm);
        double ptr = vm_pop(vm);
        double* base = vm_resolve_ptr(vm, ptr);
        int* types = vm_resolve_type(vm, ptr);
        int type = (int)base[HEAP_OFFSET_TYPE];
        int len = (int)base[HEAP_OFFSET_LEN];

        int start = (int)s; int end = (int)e;
        if (start < 0) start += len;
        if (end < 0) end += len;
        if (start < 0) start = 0;
        if (end >= len) end = len - 1;
        int newlen = (end >= start) ? (end - start + 1) : 0;
        if (start >= len) newlen = 0;

        if (type == TYPE_BYTES) {
            double newptr = heap_alloc(vm, (newlen + 7) / 8 + 2);
            double* newbase = vm_resolve_ptr(vm, newptr);
            newbase[0] = TYPE_BYTES; newbase[1] = (double)newlen;
            char* src = (char*)&base[HEAP_HEADER_ARRAY];
            char* dst = (char*)&newbase[HEAP_HEADER_ARRAY];
            if (newlen > 0) for (int i = 0; i < newlen; i++) dst[i] = src[start + i];
            vm_push(vm, newptr, T_OBJ);
        } else {
            double newptr = heap_alloc(vm, newlen + 2);
            double* newbase = vm_resolve_ptr(vm, newptr);
            int* newtypes = vm_resolve_type(vm, newptr);
            newbase[0] = TYPE_ARRAY; newbase[1] = (double)newlen;
            for (int i = 0; i < newlen; i++) {
                newbase[2 + i] = base[2 + start + i];
                newtypes[2 + i] = types[2 + start + i];
            }
            vm_push(vm, newptr, T_OBJ);
        }
    } else if (op == OP_SLICE_SET) {
        CHECK_STACK(4);
        double val = vm_pop(vm);
        double e = vm_pop(vm);
        double s = vm_pop(vm);
        double ptr = vm->stack[vm->sp]; vm_pop(vm);
        double* base = vm_resolve_ptr(vm, ptr);
        int* types = vm_resolve_type(vm, ptr);
        int type = (int)base[0];
        int len = (int)base[1];

        int start = (int)s; int end = (int)e;
        if (start < 0) start += len; if (end < 0) end += len;
        if (start < 0) start = 0; if (end >= len) end = len - 1;
        int slice_len = (end >= start) ? (end - start + 1) : 0;

        if (type == TYPE_BYTES) {
            double* vbase = vm_resolve_ptr(vm, val);
            if ((int)vbase[0] == TYPE_BYTES) {
                unsigned char* dst = (unsigned char*)&base[HEAP_HEADER_ARRAY];
                unsigned char* src = (unsigned char*)&vbase[HEAP_HEADER_ARRAY];
                int vlen = (int)vbase[1];
                int copy_len = (slice_len < vlen) ? slice_len : vlen;
                for(int i=0; i<copy_len; i++) dst[start+i] = src[i];
            }
        } else if (type == TYPE_ARRAY) {
            double* vbase = vm_resolve_ptr(vm, val);
            int* vtypes = vm_resolve_type(vm, val);
            if ((int)vbase[0] == TYPE_ARRAY) {
                int vlen = (int)vbase[1];
                int copy_len = (slice_len < vlen) ? slice_len : vlen;
                for(int i=0; i<copy_len; i++) {
                    base[2+start+i] = vbase[2+i];
                    types[2+start+i] = vtypes[2+i];
                }
            }
        }
        vm_push(vm, val, T_OBJ);
    }
}

static void exec_monitor(VM* vm) {
    //printf("\n================ [ VM MONITOR ] ================\n");
    printf(UI_BG_HEADER UI_BOLD UI_FG_WHITE "  Memory Monitor" EXTEND UI_RST "\n");

    printf(UI_BG_CONTENT UI_BOLD UI_FG_GRAY "  GC Heap" EXTEND UI_RST "\n");
    printf("  Objects: %d / %d\n", vm->heap.count, vm->heap.capacity);

    //printf("\n--- Globals ---\n");
    printf(UI_BG_CONTENT UI_BOLD UI_FG_GRAY "\n   Globals" EXTEND UI_RST "\n");
    if (vm->global_symbols) {
        for (int i = 0; i < vm->global_symbol_count; i++) {
            int addr = vm->global_symbols[i].addr;
            printf("  %s: ", vm->global_symbols[i].name);
            print_recursive(vm, vm->globals[addr], vm->global_types[addr], 0, MYLO_MONITOR_DEPTH);
            printf("\n");
        }
    }

    //printf("\n--- Locals (Frame FP: %d) ---\n", vm->fp);
    printf(UI_BG_CONTENT UI_BOLD UI_FG_GRAY "\n   Locals (Frame #%d)" EXTEND UI_RST "\n", vm->fp);

    bool found_any = false;
    if (vm->local_symbols) {
        for (int i = 0; i < vm->local_symbol_count; i++) {
            VMLocalInfo* sym = &vm->local_symbols[i];
            if ((vm->ip - 1) >= sym->start_ip && (sym->end_ip == -1 || (vm->ip - 1) <= sym->end_ip)) {
                int stack_idx = vm->fp + sym->stack_offset;
                if (stack_idx <= vm->sp) {
                    printf("  %s: ", sym->name);
                    print_recursive(vm, vm->stack[stack_idx], vm->stack_types[stack_idx], 0, MYLO_MONITOR_DEPTH);
                    printf("\n");
                    found_any = true;
                }
            }
        }
    }
    if (!found_any) printf("  (None)\n");
   //printf("================================================\n");
}

static void exec_cast_op(VM* vm, int target_type, bool is_check_only) {
    if (target_type == TYPE_ANY) return;

    double val = vm->stack[vm->sp];
    int current_type = vm->stack_types[vm->sp];
    
    if (current_type == T_ENUM) {
        if (target_type == TYPE_STR) {
            if (!is_check_only) {
                int str_id = ((unsigned long long)val >> 16) & 0xFFFF;
                vm->stack[vm->sp] = (double)str_id;
                vm->stack_types[vm->sp] = T_STR;
            }
            return;
        }
        // Extract raw number for integer casts (i32, byte, etc.)
        val = (double)((unsigned long long)val & 0xFFFF);
        if (!is_check_only) {
            vm->stack[vm->sp] = val;
            vm->stack_types[vm->sp] = T_NUM;
        }
        current_type = T_NUM;
    }

    if (target_type == TYPE_NUM || target_type == TYPE_I32_ARRAY || target_type == TYPE_I64_ARRAY ||
        target_type == TYPE_I16_ARRAY || target_type == TYPE_F32_ARRAY || target_type == TYPE_BOOL_ARRAY ||
        target_type == TYPE_BYTES) { // Added TYPE_BYTES check

        if (current_type != T_NUM) RUNTIME_ERROR("Type Mismatch: Expected Number");

        if (!is_check_only) {
            // Strong Casting: Truncate floats to integers for integer types
            if (target_type == TYPE_I32_ARRAY ||
                target_type == TYPE_I64_ARRAY ||
                target_type == TYPE_I16_ARRAY ||
                target_type == TYPE_BOOL_ARRAY ||
                target_type == TYPE_BYTES) {

                vm->stack[vm->sp] = floor(val);
                }
            // f32 and num (f64) do not need truncation, they stay as floating point
        }
        }
    else if (target_type == TYPE_STR) {
        if (current_type != T_STR) RUNTIME_ERROR("Type Mismatch: Expected String");
    }
    else if (target_type >= 0) { // Struct ID
        if (current_type != T_OBJ) RUNTIME_ERROR("Type Mismatch: Expected Object");
        double* base = vm_resolve_ptr(vm, val);
        if (!base || (int)base[0] != target_type) RUNTIME_ERROR("Type Mismatch: Expected Struct ID %d", target_type);
    }
}

// --- VM Execution Loop ---

int vm_step(VM* vm, bool debug_trace) {
    // 1. TUI Breakpoint Check
    if (vm->cli_debug_mode) {
         int curr = vm->lines[vm->ip];
         bool hit = false;
         for(int i=0; i<dbg_bp_count; i++) {
             // Stop if on a breakpoint AND we haven't already stopped on this line
             if (dbg_breakpoints[i] == curr && curr != vm->last_debug_line) hit = true;
         }

         if (hit) {
             vm->last_debug_line = curr;
             enter_debugger(vm);
         }

         // If we moved to a new line, reset the last_stop
         if (curr != vm->last_debug_line) vm->last_debug_line = -1;
    }

    if (vm->ip >= vm->code_size) return -1;

    if (debug_trace) {
        int op = vm->bytecode[vm->ip];
        if (op >= 0 && op <= OP_DEBUGGER) {
            printf("[TRACE] IP:%04d Line:%d SP:%2d OP:%s\n", vm->ip, vm->lines[vm->ip], vm->sp, OP_NAMES[op]);
        }
    }

    int op = vm->bytecode[vm->ip++];
    switch (op) {
        // Stack & Constants
        case OP_PSH_NUM: { int idx = vm->bytecode[vm->ip++]; vm_push(vm, vm->constants[idx], T_NUM); break; }
        case OP_PSH_STR: { int idx = vm->bytecode[vm->ip++]; vm_push(vm, (double)idx, T_STR); break; }
        case OP_DUP: CHECK_STACK(1); vm_push(vm, vm->stack[vm->sp], vm->stack_types[vm->sp]); break;
        case OP_POP: vm_pop(vm); break;

        // Math & Logic
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD: exec_math_op(vm, op); break;
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
        case OP_EQ:
        case OP_NEQ: exec_compare_op(vm, op); break;

        // Variables
        case OP_SET:
        case OP_GET:
        case OP_LVAR:
        case OP_SVAR: exec_var_op(vm, op); break;

        // Flow Control
        case OP_JMP:
        case OP_JZ:
        case OP_JNZ:
        case OP_CALL:
        case OP_RET: exec_flow_op(vm, op); break;
        case OP_HLT: return -1;

        // Memory & Objects
        case OP_ALLOC:
        case OP_HSET:
        case OP_HGET: exec_alloc_op(vm, op); break;

        // Arrays & Maps
        case OP_ARR:
        case OP_AGET:
        case OP_ALEN:
        case OP_ASET:
        case OP_MAP:
        case OP_MAKE_ARR: exec_array_op(vm, op); break;

        // Slices
        case OP_SLICE:
        case OP_SLICE_SET: exec_slice_op(vm, op); break;

        // Misc
        case OP_PSH_ENUM: { int idx = vm->bytecode[vm->ip++]; vm_push(vm, vm->constants[idx], T_ENUM); break; }
        case OP_CAT: {
            CHECK_STACK(2);
            double b = vm_pop(vm); int bt = vm->stack_types[vm->sp + 1];
            double a = vm->stack[vm->sp]; int at = vm->stack_types[vm->sp];
            char s1[MAX_STRING_LENGTH], s2[MAX_STRING_LENGTH];

            if (at == T_STR) strncpy(s1, vm->string_pool[(int)a], MAX_STRING_LENGTH-1);
            else if (at == T_ENUM) strncpy(s1, vm->string_pool[((unsigned long long)a >> 16) & 0xFFFF], MAX_STRING_LENGTH-1);
            else snprintf(s1, MAX_STRING_LENGTH, "%g", a);
            s1[MAX_STRING_LENGTH-1] = '\0';

            if (bt == T_STR) strncpy(s2, vm->string_pool[(int)b], MAX_STRING_LENGTH-1);
            else if (bt == T_ENUM) strncpy(s2, vm->string_pool[((unsigned long long)b >> 16) & 0xFFFF], MAX_STRING_LENGTH-1);
            else snprintf(s2, MAX_STRING_LENGTH, "%g", b);
            s2[MAX_STRING_LENGTH-1] = '\0';

            char res[MAX_STRING_LENGTH * 2];
            snprintf(res, sizeof(res), "%s%s", s1, s2);
            int id = make_string(vm, res);
            vm->stack[vm->sp] = (double)id;
            vm->stack_types[vm->sp] = T_STR;
            break;
        }
        case OP_PRN: {
            CHECK_STACK(1);
            double v = vm->stack[vm->sp]; int t = vm->stack_types[vm->sp]; vm->sp--;
            print_recursive(vm, v, t, 0, -1);
            print_raw(vm, "\n");
            break;
        }
        case OP_NATIVE: {
            int id = vm->bytecode[vm->ip++];
            if(vm->natives[id]) vm->natives[id](vm);
            else RUNTIME_ERROR("Unknown Native ID %d", id);
            break;
        }
        case OP_MK_BYTES: {
            CHECK_STACK(1);
            double v = vm_pop(vm);
            const char* s = vm->string_pool[(int)v];
            int len = strlen(s);
            double ptr = heap_alloc(vm, (len+7)/8 + 2);
            double* base = vm_resolve_ptr(vm, ptr);
            base[0] = TYPE_BYTES; base[1] = (double)len;
            char* b = (char*)&base[2];
            memcpy(b, s, len);
            vm_push(vm, ptr, T_OBJ);
            break;
        }
        case OP_EMBED: {
            int len = vm->bytecode[vm->ip++];
            double ptr = heap_alloc(vm, (len+7)/8 + 2);
            double* base = vm_resolve_ptr(vm, ptr);
            base[0] = TYPE_BYTES; base[1] = (double)len;
            unsigned char* dst = (unsigned char*)&base[2];
            for(int i=0; i<len; i++) dst[i] = (unsigned char)vm->bytecode[vm->ip++];
            vm_push(vm, ptr, T_OBJ);
            break;
        }
        case OP_SCOPE_ENTER: {
            break;
        }
        case OP_SCOPE_EXIT: {
            break;
        }
        // Iterators
        case OP_IT_KEY:
        case OP_IT_VAL:
        case OP_IT_DEF: {
            CHECK_STACK(2);
            double idx_val = vm_pop(vm);
            double obj_val = vm_pop(vm);
            int idx = (int)idx_val;

            double* base = vm_resolve_ptr(vm, obj_val);
            int* types = vm_resolve_type(vm, obj_val);
            int type = (int)base[0];

            if (type == TYPE_ARRAY || type == TYPE_BYTES) {
                int len = (int)base[1];
                if (idx < 0 || idx >= len) RUNTIME_ERROR("Iterator OOB");
                if (op == OP_IT_KEY) vm_push(vm, (double)idx, T_NUM);
                else {
                    if (type == TYPE_BYTES) vm_push(vm, (double)((unsigned char*)&base[2])[idx], T_NUM);
                    else vm_push(vm, base[2+idx], types[2+idx]);
                }
            } else if (type == TYPE_MAP) {
                int count = (int)base[HEAP_OFFSET_COUNT];
                if (idx < 0 || idx >= count) RUNTIME_ERROR("Iterator OOB");
                double* data = vm_resolve_ptr(vm, base[HEAP_OFFSET_DATA]);
                int* data_types = vm_resolve_type(vm, base[HEAP_OFFSET_DATA]);
                if (op == OP_IT_VAL) vm_push(vm, data[idx*2+1], data_types[idx*2+1]);
                else vm_push(vm, data[idx*2], data_types[idx*2]);
            }
            break;
        }

        // GC replaces Arenas
        case OP_NEW_ARENA: {
            vm_push(vm, 0.0, T_NUM); // Return a dummy region ID
            break;
        }
        case OP_DEL_ARENA: {
            vm_pop(vm); // Pop the region ID
            gc_collect(vm);
            break;
        }
        case OP_SET_CTX: {
            vm_pop(vm); // Pop Region ID
            break;
        }
        case OP_MONITOR: exec_monitor(vm); break;

        case OP_CAST: {
            int target = vm->bytecode[vm->ip++];
            exec_cast_op(vm, target, false);
            break;
        }
        case OP_CHECK_TYPE: {
            int target = vm->bytecode[vm->ip++];
            exec_cast_op(vm, target, true);
            break;
        }
        case OP_OR: {
            CHECK_STACK(2);
            double b = vm_pop(vm);
            double a = vm->stack[vm->sp]; // Peek 'a' to overwrite it
            // Logic: result is 1.0 if either a or b is non-zero, else 0.0
            vm->stack[vm->sp] = (a != 0.0 || b != 0.0) ? 1.0 : 0.0;
            vm->stack_types[vm->sp] = T_NUM;
            break;
        }
        case OP_AND: {
            CHECK_STACK(2);
            double b = vm_pop(vm);
            double a = vm->stack[vm->sp]; // Peek 'a' to overwrite it
            // Logic: result is 1.0 if both a and b are non-zero, else 0.0
            vm->stack[vm->sp] = (a != 0.0 && b != 0.0) ? 1.0 : 0.0;
            vm->stack_types[vm->sp] = T_NUM;
            break;
        }
        case OP_RANGE: {
            CHECK_STACK(2);
            double stop = vm_pop(vm);
            double start = vm_pop(vm); // Pop but we need to push array back, so just pop both.

            // Auto-direction: 0...5 (step 1), 5...0 (step -1)
            double step = (stop >= start) ? 1.0 : -1.0;
            int count = (int)(fabs(stop - start)) + 1;
            if (count < 0) count = 0; // Safety

            double ptr = heap_alloc(vm, count + HEAP_HEADER_ARRAY);
            double* base = vm_resolve_ptr(vm, ptr);
            int* types = vm_resolve_type(vm, ptr);

            base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
            base[HEAP_OFFSET_LEN] = (double)count;

            double val = start;
            for(int i=0; i<count; i++) {
                base[HEAP_HEADER_ARRAY + i] = val;
                types[HEAP_HEADER_ARRAY + i] = T_NUM;
                val += step;
            }

            vm_push(vm, ptr, T_OBJ);
            break;
        }
        // 2. TUI Manual Breakpoint
        case OP_DEBUGGER:
            if (vm->cli_debug_mode) {
                // Manually trigger entry
                vm->last_debug_line = vm->lines[vm->ip];
                enter_debugger(vm);
            }
            break;
    }
    return op;
}

void run_vm_from(VM* vm, int start_ip, bool debug_trace) {
    vm->ip = start_ip;
    while (vm->ip < vm->code_size) {
        if (vm_step(vm, debug_trace) == -1) break;
    }
}

void run_vm(VM* vm, bool debug_trace) {
    run_vm_from(vm, 0, debug_trace);
}

// =========================================================
// [NEW] Standalone Loader Implementation
// =========================================================

typedef void (*BindFunc)(VM *, int, MyloAPI *);

bool load_self_contained(VM* vm, const char* exe_path) {
    FILE* f = fopen(exe_path, "rb");
    if (!f) return false;

    // Check Footer
    fseek(f, -((long)sizeof(StandaloneFooter)), SEEK_END);
    StandaloneFooter footer;
    if (fread(&footer, sizeof(StandaloneFooter), 1, f) != 1) { fclose(f); return false; }
    if (strcmp(footer.magic, MYLO_MAGIC) != 0) { fclose(f); return false; }

    long total_payload_size = footer.bytecode_size + footer.const_size + footer.string_size + footer.dependency_size + footer.symbol_size + footer.function_size;
    fseek(f, -(total_payload_size + (long)sizeof(StandaloneFooter)), SEEK_END);

    // 1. Read Bytecode
    vm->code_size = footer.bytecode_size / sizeof(int);
    fread(vm->bytecode, sizeof(int), vm->code_size, f);

    // 2. Read Constants
    vm->const_count = footer.const_size / sizeof(double);
    fread(vm->constants, sizeof(double), vm->const_count, f);

    // 3. Read Strings
    vm->str_count = footer.string_size / MAX_STRING_LENGTH;
    fread(vm->string_pool, MAX_STRING_LENGTH, vm->str_count, f);

    // 4. Read Dependencies (Store in temporary memory so we can read symbols next)
    int dep_count = footer.dependency_size / sizeof(Dependency);
    Dependency* deps = NULL;
    if (dep_count > 0) {
        deps = malloc(footer.dependency_size);
        fread(deps, sizeof(Dependency), dep_count, f);
    }

    // 5. Read Symbols sequentially!
    int symbol_count = footer.symbol_size / sizeof(VMSymbol);
    vm->global_symbol_count = symbol_count;
    vm->global_symbols = malloc(footer.symbol_size);
    fread(vm->global_symbols, sizeof(VMSymbol), symbol_count, f);

    // --- [NEW] 5b. Read Functions sequentially! ---
    int func_count = footer.function_size / sizeof(VMFunction);
    vm->function_count = func_count;
    fread(vm->functions, sizeof(VMFunction), func_count, f);

    // 6. Bind Dependencies
    if (dep_count > 0) {
        // Prepare API for binding
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
        api.natives_array = vm->natives;
        api.string_pool = vm->string_pool;

        for (int i = 0; i < dep_count; i++) {
            printf("Loading ... %s\n", deps[i].name);
            void* lib = load_library(deps[i].name);
            
            if (!lib) {
                char *pathvar;
                pathvar = getenv("MYLO_LIBS");
                printf("MYLO_LIBS=%s",pathvar);
                if (pathvar && (strlen(deps[i].name) > 3)) {
                    char pathbind[1024] = "";
                    strcat(pathbind,pathvar);
                    strcat(pathbind,deps[i].name + 2);
                    printf("Attempting to load from MYLO_LIBS (%s)\n", pathvar); 
                    lib = load_library(pathbind);
                }
            }

            if (!lib) {
                printf("Runtime Error: Could not load dependency '%s'\n", deps[i].name);
                exit(1);
            }

            BindFunc binder = (BindFunc)get_symbol(lib, "mylo_bind_lib");
            if (!binder) {
                printf("Runtime Error: Library '%s' is not a valid Mylo module.\n", deps[i].name);
                exit(1);
            }

            binder(vm, deps[i].start_index, &api);
        }
        free(deps);
    }

    fclose(f);
    return true;
}

void get_lib_name(char* out, const char* base_name) {
    char temp[1024];
    strcpy(temp, base_name);
    char* dot = strrchr(temp, '.');
    if (dot) *dot = '\0';

#ifdef _WIN32
    sprintf(out, "%s.dll", temp);
#else
    sprintf(out, "./%s.so", temp);
#endif
}

void* load_library(const char* path) {
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h)
        fprintf(stderr, "Failed to load library %s (Error: %lu)\n", path, GetLastError());
    return (void*)h;
#else
    // RTLD_GLOBAL allows the plugin to see symbols if we exported them,
    // but we are using the struct method now, so standard load is fine.
    void* h = dlopen(path, RTLD_NOW);
    if (!h)
        fprintf(stderr, "Failed to load library %s (%s)\n", path, dlerror());
    return h;
#endif
}

void* get_symbol(void* handle, const char* symbol) {
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)handle, symbol);
#else
    return dlsym(handle, symbol);
#endif
}


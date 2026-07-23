#ifndef MYLO_DEFINES_H
#define MYLO_DEFINES_H

// VM Memory Limits
#define STACK_SIZE 2048
#define MAX_CODE 5368709
#define MAX_HEAP 100000000
#define MAX_GLOBALS 2048
#define MAX_CONSTANTS 1024
#define MAX_ARENAS 64         // 6 bits

// String Limits
#define MAX_STRINGS 10000
#define MAX_STRING_LENGTH 1024
#define MAX_C_HEADERS 32

// Compilation Limits
#define MAX_IDENTIFIER 256
#define MAX_STRUCTS 64
#define MAX_FIELDS 16
#define MAX_NATIVES 64
#define MAX_FFI_ARGS 16
#define MAX_C_BLOCK_SIZE 1024
#define MAX_LOOP_NESTING 32
#define MAX_JUMPS_PER_LOOP 64
#define MAX_ENUM_MEMBERS 1024
#define MAX_SEARCH_PATHS 16

// Output
#define OUTPUT_BUFFER_SIZE 128000

// Types
#define TYPE_ANY -99
#define TYPE_NUM -98
#define TYPE_STR -97
#define TYPE_VOID -96

#define TYPE_ARRAY -1
#define TYPE_BYTES -2
#define TYPE_MAP -3

#define TYPE_I16_ARRAY  -10
#define TYPE_I32_ARRAY  -11
#define TYPE_I64_ARRAY  -12
#define TYPE_F16_ARRAY  -13
#define TYPE_F32_ARRAY  -14

#define TYPE_BOOL_ARRAY -20

// Heap Layout Constants
#define HEAP_OFFSET_TYPE 0
#define HEAP_OFFSET_LEN 1
#define HEAP_OFFSET_CAP 1
#define HEAP_OFFSET_COUNT 2
#define HEAP_OFFSET_DATA 3

#define HEAP_HEADER_STRUCT 2
#define HEAP_HEADER_ARRAY 2
#define HEAP_HEADER_MAP 4
#define MYLO_MONITOR_DEPTH 4

#define MAP_INITIAL_CAP 16
#define MAX_VM_FUNCTIONS 1024
#define MAX_BUS_ENTRIES 2048
#define MAX_WORKERS 128

// FFI
#define MAX_STD_ARGS 12

// --- POINTER PACKING (Safe GC) ---
// Since we use a GC heap now, the pointer is just an index offset.
// Max index is 2^53 (double mantissa) which is plenty.

#define PACK_PTR(offset) ((double)((unsigned long long)(offset)))
#define UNPACK_OFFSET(ptr) ((int)((unsigned long long)(ptr)))


// For bundles

#define MYLO_MAGIC "MYLO_EXE"
#define MAX_DEPENDENCIES 128

typedef struct {
    long bytecode_size;
    long const_size;     // Size of constants block
    long string_size;    // Size of string pool
    long dependency_size;
    long symbol_size;
    long function_size; // func table size
    char magic[9];       // "MYLO_EXE\0"
} StandaloneFooter;



#endif

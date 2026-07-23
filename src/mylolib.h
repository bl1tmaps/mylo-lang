#ifndef MYLO_LIB_H
#define MYLO_LIB_H

#include "vm.h"

typedef struct {
  const char *name;
  NativeFunc func;
  const char *ret_type;
  int arg_count;
  const char *arg_types[MAX_STD_ARGS];
} StdLibDef;

// Implementation functions
// --- Standard Library Implementation Functions ---
void std_sqrt(VM *vm);
void std_sin(VM *vm);
void std_cos(VM *vm);
void std_tan(VM *vm);
void std_floor(VM *vm);
void std_ceil(VM *vm);
void std_mix(VM *vm);
void std_min(VM *vm);
void std_max(VM *vm);
void std_dist(VM *vm);

void std_type(VM *vm);
void std_len(VM *vm);
void std_contains(VM *vm);
void std_to_string(VM *vm);
void std_to_num(VM *vm);
void std_read_lines(VM *vm);
void std_write_file(VM *vm);
void std_read_bytes(VM *vm);
void std_write_bytes(VM *vm);
void std_list(VM *vm);
void std_remove(VM *vm);
void std_add(VM *vm);
void std_split(VM *vm);
void std_where(VM *vm);
void std_range(VM *vm);
void std_for_list(VM *vm);
void std_list_min(VM *vm);
void std_list_max(VM *vm);
void std_filter(VM *vm);
void std_param_filter(VM *vm);

// Rand
void std_seed(VM *vm);
void std_rand(VM *vm);
void std_rand_normal(VM *vm);
void std_noise(VM *vm);

// Threads
void std_create_worker(VM *vm);
void std_dock_worker(VM *vm);
void std_check_worker(VM *vm);

// Bus Functions
void std_bus_set(VM *vm);
void std_bus_get(VM *vm);

// Terminal
void std_cget(VM *vm);
void std_kbhit(VM *vm);
void std_get_keys(VM *vm);

// Memory Alloc
// Region functions removed due to GC implementation.

// Call via name
void std_call(VM *vm);

// Registry Declaration (Extern)
// We do NOT define it here, only say it exists.
extern const StdLibDef std_library[];

#endif

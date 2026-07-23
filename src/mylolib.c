#define _CRT_SECURE_NO_WARNINGS
#include "mylolib.h"
#include "defines.h"
#include "os_job.h"
#include "vm.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define _WINSOCKAPI_ // stops windows.h including winsock.h
#include <conio.h>
#include <process.h>
#include <windows.h>
#define STRDUP _strdup
#else
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>
#define STRDUP strdup

#endif

// HTTP Display Includes
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
// #include <winsock2.h> // This was causing a redefinition failure on MSVC
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// --- Worker Structure ---
typedef struct {
  bool active;
  bool complete;
  bool error;
  char error_msg[256];
#ifdef _WIN32
  HANDLE thread;
  unsigned threadID;
#else
  pthread_t thread;
#endif
  VM vm;             // The Worker's Isolated VM
  int region_id;     // The Region ID this worker owns
  char entry_func[64];
} MyloWorker;

static MyloWorker workers[MAX_WORKERS];
static bool workers_initialized = false;

// A simple global key-value store protected by a mutex
typedef struct {
  char *key;
  int type;
  double num_val;
  char *str_val;
} BusEntry;

static BusEntry global_bus[MAX_BUS_ENTRIES];
static int bus_count = 0;
static bool bus_initialized = false;

#ifdef _WIN32
static CRITICAL_SECTION bus_lock;
#define LOCK_BUS EnterCriticalSection(&bus_lock)
#define UNLOCK_BUS LeaveCriticalSection(&bus_lock)
#define INIT_BUS_LOCK InitializeCriticalSection(&bus_lock)
#else
static pthread_mutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_BUS pthread_mutex_lock(&bus_lock)
#define UNLOCK_BUS pthread_mutex_unlock(&bus_lock)
#define INIT_BUS_LOCK // Static init is enough for pthreads
#endif

static const char *get_str(VM *vm, double val) {
  int id = (int)val;
  if (id < 0 || id >= vm->str_count)
    return "";
  return vm->string_pool[id];
}

void trim_newline(char *str) {
  int len = strlen(str);
  if (len > 0 && str[len - 1] == '\n')
    str[len - 1] = '\0';
  if (len > 1 && str[len - 2] == '\r')
    str[len - 2] = '\0';
}

// Region functions were removed due to GC implementation.
void std_copy(VM *vm) {
  double val = vm->stack[vm->sp];
  int type = vm->stack_types[vm->sp];
  vm->sp--; // Pop argument
  vm_push(vm, val, type);
}
// Helper to init the lock lazily
static void init_bus() {
  if (!bus_initialized) {
#ifdef _WIN32
    INIT_BUS_LOCK;
#endif
    memset(global_bus, 0, sizeof(global_bus));
    bus_initialized = true;
  }
}

// Workers - Threading
static void init_workers() {
  if (!workers_initialized) {
    memset(workers, 0, sizeof(workers));
    workers_initialized = true;
  }
}

// --- Thread Worker Function ---
#ifdef _WIN32
unsigned __stdcall mylo_worker_entry(void *arg) {
#else
void *mylo_worker_entry(void *arg) {
#endif
  MyloWorker *worker = (MyloWorker *)arg;
  VM *vm = &worker->vm;

  // 1. Locate Entry Point
  int func_addr = vm_find_function(vm, worker->entry_func);
  if (func_addr == -1) {
    worker->error = true;
    // For now we just silently error or we could set error_msg if it existed
  } else {
    // 2. Setup Dummy Stack Frame for OP_RET
    vm_push(vm, (double)vm->code_size, T_NUM); // Return IP
    vm_push(vm, 0.0, T_NUM);                   // Old FP
    vm->fp = vm->sp + 1;                       // Set FP for OP_RET

    // 3. Execute
    run_vm_from(vm, func_addr, false);
  }

  // 3. Cleanup
  vm_cleanup(vm);

  worker->complete = true;

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

// std_create_worker("function_name") -> worker_id
void std_create_worker(VM *vm) {
  init_workers();

  double func_id = vm_pop(vm);
  const char *func_name = get_str(vm, func_id);

  // 1. Find Slot
  int slot = -1;
  for (int i = 0; i < MAX_WORKERS; i++) {
    if (!workers[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    vm_push(vm, -1.0, T_NUM); // No free workers
    return;
  }

  MyloWorker *w = &workers[slot];
  w->active = true;
  w->complete = false;
  w->error = false;
  w->region_id = 0; // Unused now
  strncpy(w->entry_func, func_name, 63);

  // 2. Initialize Worker VM
  VM *child = &w->vm;
  vm_init(child);

  // Copy Code
  child->code_size = vm->code_size;
  memcpy(child->bytecode, vm->bytecode, vm->code_size * sizeof(int));
  memcpy(child->lines, vm->lines, vm->code_size * sizeof(int));

  // Copy Constants
  child->const_count = vm->const_count;
  memcpy(child->constants, vm->constants, vm->const_count * sizeof(double));

  // Copy Strings
  child->str_count = vm->str_count;
  memcpy(child->string_pool, vm->string_pool,
         vm->str_count * MAX_STRING_LENGTH);

  // Copy Function Table
  child->function_count = vm->function_count;
  memcpy(child->functions, vm->functions,
         vm->function_count * sizeof(VMFunction));

  // Copy Globals (Primitive values only, object pointers are wiped)
  child->global_symbol_count = vm->global_symbol_count;
  for (int i=0; i<MAX_GLOBALS; i++) {
      if (vm->global_types[i] == T_OBJ) {
          child->globals[i] = 0.0;
          child->global_types[i] = T_NUM;
      } else {
          child->globals[i] = vm->globals[i];
          child->global_types[i] = vm->global_types[i];
      }
  }

  // Register Native Functions (Standard Library)
  int li = 0;
  while (std_library[li].name != NULL) {
    child->natives[li] = std_library[li].func;
    li++;
  }

  // 3. Spawn Thread
#ifdef _WIN32
  w->thread =
      (HANDLE)_beginthreadex(NULL, 0, &mylo_worker_entry, w, 0, &w->threadID);
#else
  pthread_create(&w->thread, NULL, &mylo_worker_entry, w);
#endif

  vm_push(vm, (double)slot, T_NUM);
}

// std_dock_worker(worker_id) -> void
// Blocks until worker is done.
void std_dock_worker(VM *vm) {
  double id_val = vm_pop(vm);
  int slot = (int)id_val;

  if (slot < 0 || slot >= MAX_WORKERS || !workers[slot].active) {
    printf("Runtime Error: Invalid or inactive worker ID %d\n", slot);
    exit(1);
  }

  MyloWorker *w = &workers[slot];

  // 1. Join Thread (Block)
#ifdef _WIN32
  WaitForSingleObject(w->thread, INFINITE);
  CloseHandle(w->thread);
#else
  pthread_join(w->thread, NULL);
#endif

  if (w->error) {
    printf("Worker Error: %s\n", w->error_msg);
  }

  w->active = false;

  vm_push(vm, 0.0, T_NUM);
}

// std_check_worker(worker_id) -> status code
// 0: Running, 1: Complete, -1: Error/Invalid
void std_check_worker(VM *vm) {
  double id_val = vm_pop(vm);
  int slot = (int)id_val;

  if (slot < 0 || slot >= MAX_WORKERS || !workers[slot].active) {
    vm_push(vm, -1.0, T_NUM);
    return;
  }

  if (workers[slot].complete) {
    vm_push(vm, 1.0, T_NUM);
  } else {
    vm_push(vm, 0.0, T_NUM);
  }
}

// --- Previous Standard Lib Functions (unchanged) ---

void std_sqrt(VM *vm) { vm_push(vm, sqrt(vm_pop(vm)), T_NUM); }
void std_sin(VM *vm) { vm_push(vm, sin(vm_pop(vm)), T_NUM); }
void std_cos(VM *vm) { vm_push(vm, cos(vm_pop(vm)), T_NUM); }
void std_tan(VM *vm) { vm_push(vm, tan(vm_pop(vm)), T_NUM); }
void std_floor(VM *vm) { vm_push(vm, floor(vm_pop(vm)), T_NUM); }
void std_ceil(VM *vm) { vm_push(vm, ceil(vm_pop(vm)), T_NUM); }

void std_type(VM *vm) {
  double val = vm_pop(vm);
  int type = vm->stack_types[vm->sp + 1];

  int str_id = -1;
  if (type == T_NUM) {
    str_id = make_string(vm, "num");
  } else if (type == T_STR) {
    str_id = make_string(vm, "str");
  } else if (type == T_ENUM) {
    // Shift right 32 bits to grab the Type Name string ID!
    unsigned long long packed = (unsigned long long)val;
    int type_str_id = (packed >> 32) & 0xFFFF;
    str_id = type_str_id;
  } else if (type == T_OBJ) {
    double *base = vm_resolve_ptr_safe(vm, val);
    if (base) {
      int obj_type = (int)base[0];
      if (obj_type == TYPE_ARRAY)
        str_id = make_string(vm, "list");
      else if (obj_type == TYPE_MAP)
        str_id = make_string(vm, "map");
      else if (obj_type == TYPE_BYTES)
        str_id = make_string(vm, "bytes");
      else if (obj_type == TYPE_I32_ARRAY)
        str_id = make_string(vm, "i32[]");
      else if (obj_type == TYPE_F32_ARRAY)
        str_id = make_string(vm, "f32[]");
      else if (obj_type == TYPE_I16_ARRAY)
        str_id = make_string(vm, "i16[]");
      else if (obj_type == TYPE_I64_ARRAY)
        str_id = make_string(vm, "i64[]");
      else if (obj_type == TYPE_BOOL_ARRAY)
        str_id = make_string(vm, "bool[]");
      else
        str_id = make_string(vm, "struct");
    } else {
      str_id = make_string(vm, "null");
    }
  }
  vm_push(vm, (double)str_id, T_STR);
}

void std_len(VM *vm) {
  double val = vm_pop(vm);

  if (vm->stack_types[vm->sp + 1] == T_OBJ) {
    double *base = vm_resolve_ptr(vm, val);
    if (!base) {
      vm_push(vm, 0, T_NUM);
      return;
    }

    int type = (int)base[HEAP_OFFSET_TYPE];

    if (type == TYPE_ARRAY || type == TYPE_BYTES ||
        (type <= TYPE_I16_ARRAY && type >= TYPE_BOOL_ARRAY)) {
      vm_push(vm, base[HEAP_OFFSET_LEN], T_NUM);
    } else if (type == TYPE_MAP) {
      vm_push(vm, base[HEAP_OFFSET_COUNT], T_NUM);
    } else {
      printf("Runtime Error: len() expects array, string, map, or bytes.\n");
      exit(1);
    }
  } else if (vm->stack_types[vm->sp + 1] == T_STR) {
    const char *s = get_str(vm, val);
    vm_push(vm, (double)strlen(s), T_NUM);
  } else {
    printf("Runtime Error: len() expects array or string.\n");
    exit(1);
  }
}
void std_contains(VM *vm) {
  double needle_val = vm_pop(vm);
  int needle_type = vm->stack_types[vm->sp + 1];
  double haystack_val = vm_pop(vm);
  int haystack_type = vm->stack_types[vm->sp + 1];

  if (haystack_type == T_STR) {
    if (needle_type != T_STR) {
      printf("Runtime Error: contains() string needle\n");
      exit(1);
    }
    if (strstr(get_str(vm, haystack_val), get_str(vm, needle_val)) != NULL)
      vm_push(vm, 1.0, T_NUM);
    else
      vm_push(vm, 0.0, T_NUM);
    return;
  }

  if (haystack_type == T_OBJ) {
    double *base = vm_resolve_ptr(vm, haystack_val);
    int *types = vm_resolve_type(vm, haystack_val);
    if (!base) {
      vm_push(vm, 0.0, T_NUM);
      return;
    }

    int objType = (int)base[HEAP_OFFSET_TYPE];

    if (objType == TYPE_ARRAY) {
      int len = (int)base[HEAP_OFFSET_LEN];
      int found = 0;
      for (int i = 0; i < len; i++) {
        if (types[HEAP_HEADER_ARRAY + i] == needle_type &&
            base[HEAP_HEADER_ARRAY + i] == needle_val) {
          found = 1;
          break;
        }
      }
      vm_push(vm, found ? 1.0 : 0.0, T_NUM);
      return;
    } else if (objType == TYPE_MAP) {
      if (needle_type != T_STR) {
        printf("Runtime Error: contains() map check requires string key\n");
        exit(1);
      }
      int count = (int)base[HEAP_OFFSET_COUNT];
      double dataPtrVal = base[HEAP_OFFSET_DATA];
      double *data = vm_resolve_ptr(vm, dataPtrVal);

      int found = 0;
      if (data) {
        for (int i = 0; i < count; i++) {
          if (data[i * 2] == needle_val) {
            found = 1;
            break;
          }
        }
      }
      vm_push(vm, found ? 1.0 : 0.0, T_NUM);
      return;
    }
  }
  printf("Runtime Error: contains() type\n");
  exit(1);
}

void std_to_string(VM *vm) {
  double val = vm_pop(vm);
  int type = vm->stack_types[vm->sp + 1];

  if (type == T_NUM) {
    char buf[64];
    if (val == (int)val)
      snprintf(buf, 64, "%d", (int)val);
    else
      snprintf(buf, 64, "%g", val);

    int str_id = make_string(vm, buf);
    vm_push(vm, (double)str_id, T_STR);
  } else if (type == T_STR) {
    vm_push(vm, val, T_STR);
  } else if (type == T_OBJ) {
    char buf[64];
    snprintf(buf, 64, "[Object]");
    int str_id = make_string(vm, buf);
    vm_push(vm, (double)str_id, T_STR);
  } else {
    int str_id = make_string(vm, "");
    vm_push(vm, (double)str_id, T_STR);
  }
}

void std_to_num(VM *vm) {
  double val = vm_pop(vm);
  int type = vm->stack_types[vm->sp + 1];

  if (type == T_NUM) {
    vm_push(vm, val, T_NUM);
  } else if (type == T_STR) {
    const char *s = get_str(vm, val);
    char *end;
    double d = strtod(s, &end);
    vm_push(vm, d, T_NUM);
  } else {
    vm_push(vm, 0.0, T_NUM);
  }
}

void std_read_lines(VM *vm) {
  double path_id = vm_pop(vm);
  const char *path = get_str(vm, path_id);

  FILE *f = fopen(path, "r");
  if (!f) {
    double addr = heap_alloc(vm, HEAP_HEADER_ARRAY);
    double *base = vm_resolve_ptr(vm, addr);
    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = 0;
    vm_push(vm, addr, T_OBJ);
    return;
  }

  int lines = 0;
  int ch;
  int last_char = '\n';

  while ((ch = fgetc(f)) != EOF) {
    if (ch == '\n')
      lines++;
    last_char = ch;
  }
  if (last_char != '\n' && ftell(f) > 0)
    lines++;

  double arr_addr = heap_alloc(vm, lines + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, arr_addr);
  int *types = vm_resolve_type(vm, arr_addr);

  base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  base[HEAP_OFFSET_LEN] = (double)lines;

  rewind(f);
  char buffer[4096];
  int i = 0;
  while (i < lines && fgets(buffer, sizeof(buffer), f)) {
    trim_newline(buffer);
    int str_id = make_string(vm, buffer);
    base[HEAP_HEADER_ARRAY + i] = (double)str_id;
    types[HEAP_HEADER_ARRAY + i] = T_STR;
    i++;
  }
  fclose(f);

  vm_push(vm, arr_addr, T_OBJ);
}

void std_write_file(VM *vm) {
  double mode_id = vm_pop(vm);
  double content_id = vm_pop(vm);
  double path_id = vm_pop(vm);

  const char *mode = get_str(vm, mode_id);
  const char *content = get_str(vm, content_id);
  const char *path = get_str(vm, path_id);

  if (strcmp(mode, "w") != 0 && strcmp(mode, "a") != 0) {
    printf("Runtime Error: write_file mode must be 'w' or 'a'\n");
    exit(1);
  }

  FILE *f = fopen(path, mode);
  if (!f) {
    vm_push(vm, 0.0, T_NUM);
    return;
  }

  fprintf(f, "%s", content);
  fclose(f);
  vm_push(vm, 1.0, T_NUM);
}

void std_read_bytes(VM *vm) {
  double stride_val = vm_pop(vm);
  double path_id = vm_pop(vm);
  const char *path = get_str(vm, path_id);
  int stride = (int)stride_val;

  if (stride != 1 && stride != 4) {
    printf("Runtime Error: read_bytes stride must be 1 (byte) or 4 (int)\n");
    exit(1);
  }

  FILE *f = fopen(path, "rb");
  if (!f) {
    double addr = heap_alloc(vm, HEAP_HEADER_ARRAY);
    double *base = vm_resolve_ptr(vm, addr);
    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = 0;
    vm_push(vm, addr, T_OBJ);
    return;
  }

  fseek(f, 0, SEEK_END);
  long file_len = ftell(f);
  fseek(f, 0, SEEK_SET);

  unsigned char *buf = (unsigned char *)malloc(file_len);
  fread(buf, 1, file_len, f);
  fclose(f);

  int element_count = file_len;
  int doubles_needed = (element_count + 7) / 8;
  double addr = heap_alloc(vm, doubles_needed + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, addr);

  base[HEAP_OFFSET_TYPE] = TYPE_BYTES;
  base[HEAP_OFFSET_LEN] = (double)element_count;

  char *heap_bytes = (char *)&base[HEAP_HEADER_ARRAY];
  memcpy(heap_bytes, buf, element_count);

  free(buf);
  vm_push(vm, addr, T_OBJ);
}

void std_write_bytes(VM *vm) {
  double arr_ref = vm_pop(vm);
  double path_id = vm_pop(vm);

  double *base = vm_resolve_ptr(vm, arr_ref);
  if (!base || (int)base[HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
    printf("Runtime Error: write_bytes expects array\n");
    exit(1);
  }

  const char *path = get_str(vm, path_id);
  int len = (int)base[HEAP_OFFSET_LEN];

  FILE *f = fopen(path, "wb");
  if (!f) {
    vm_push(vm, 0.0, T_NUM);
    return;
  }

  for (int i = 0; i < len; i++) {
    unsigned char b = (unsigned char)base[HEAP_HEADER_ARRAY + i];
    fputc(b, f);
  }

  fclose(f);
  vm_push(vm, 1.0, T_NUM);
}

void std_list(VM *vm) {
  double size_val = vm_pop(vm);
  int size = (int)size_val;

  if (size < 0) {
    printf("Runtime Error: list() size must be positive\n");
    exit(1);
  }

  double ptr = heap_alloc(vm, size + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, ptr);
  int *types = vm_resolve_type(vm, ptr);

  base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  base[HEAP_OFFSET_LEN] = (double)size;

  for (int i = 0; i < size; i++) {
    base[HEAP_HEADER_ARRAY + i] = 0.0;
    types[HEAP_HEADER_ARRAY + i] = T_NUM;
  }

  vm_push(vm, ptr, T_OBJ);
}

void std_remove(VM *vm) {
  double key_val = vm_pop(vm);
  int key_type = vm->stack_types[vm->sp + 1];
  double obj_val = vm_pop(vm);

  if (vm->stack_types[vm->sp + 1] != T_OBJ) {
    printf("Runtime Error: remove() expects an object\n");
    exit(1);
  }

  double *base = vm_resolve_ptr(vm, obj_val);
  int *types = vm_resolve_type(vm, obj_val);
  int type = (int)base[HEAP_OFFSET_TYPE];

  if (type == TYPE_ARRAY) {
    int index = (int)key_val;
    int len = (int)base[HEAP_OFFSET_LEN];

    if (index < 0)
      index += len;

    if (index >= 0 && index < len) {
      for (int i = index; i < len - 1; i++) {
        base[HEAP_HEADER_ARRAY + i] = base[HEAP_HEADER_ARRAY + i + 1];
        types[HEAP_HEADER_ARRAY + i] = types[HEAP_HEADER_ARRAY + i + 1];
      }
      base[HEAP_OFFSET_LEN] = (double)(len - 1);
    }
  } else if (type == TYPE_MAP) {
    int count = (int)base[HEAP_OFFSET_COUNT];
    double data_ptr_val = base[HEAP_OFFSET_DATA];
    double *data = vm_resolve_ptr(vm, data_ptr_val);
    int *data_types = vm_resolve_type(vm, data_ptr_val);

    if (data) {
      for (int i = 0; i < count; i++) {
        double k = data[i * 2];
        int kt = data_types[i * 2];

        if (k == key_val && kt == key_type) {
          int pairs_to_move = count - 1 - i;
          if (pairs_to_move > 0) {
            for (int j = 0; j < pairs_to_move * 2; j++) {
              data[i * 2 + j] = data[(i + 1) * 2 + j];
              data_types[i * 2 + j] = data_types[(i + 1) * 2 + j];
            }
          }
          base[HEAP_OFFSET_COUNT] = (double)(count - 1);
          break;
        }
      }
    }
  }
  vm_push(vm, obj_val, T_OBJ);
}

void std_add(VM *vm) {
  double val = vm_pop(vm);
  int val_type = vm->stack_types[vm->sp + 1];

  double idx_val = vm_pop(vm);
  int idx = (int)idx_val;

  double arr_val = vm_pop(vm);

  double *base = vm_resolve_ptr(vm, arr_val);
  int *types = vm_resolve_type(vm, arr_val);

  if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
    printf("Runtime Error: add() expects an array\n");
    exit(1);
  }

  int len = (int)base[HEAP_OFFSET_LEN];

  if (idx < 0)
    idx += len;
  if (idx < 0)
    idx = 0;
  if (idx > len)
    idx = len;

  double new_ptr = heap_alloc(vm, len + 1 + HEAP_HEADER_ARRAY);
  double *new_base = vm_resolve_ptr(vm, new_ptr);
  int *new_types = vm_resolve_type(vm, new_ptr);

  new_base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  new_base[HEAP_OFFSET_LEN] = (double)(len + 1);

  int src_base_idx = HEAP_HEADER_ARRAY;
  int dst_base_idx = HEAP_HEADER_ARRAY;

  for (int i = 0; i < idx; i++) {
    new_base[dst_base_idx + i] = base[src_base_idx + i];
    new_types[dst_base_idx + i] = types[src_base_idx + i];
  }

  new_base[dst_base_idx + idx] = val;
  new_types[dst_base_idx + idx] = val_type;

  for (int i = idx; i < len; i++) {
    new_base[dst_base_idx + i + 1] = base[src_base_idx + i];
    new_types[dst_base_idx + i + 1] = types[src_base_idx + i];
  }

  vm_push(vm, new_ptr, T_OBJ);
}

void std_split(VM *vm) {
  double del_val = vm_pop(vm);
  double str_val = vm_pop(vm);

  const char *str = get_str(vm, str_val);
  const char *del = get_str(vm, del_val);
  int del_len = strlen(del);

  if (del_len == 0) {
    int len = strlen(str);
    double ptr = heap_alloc(vm, len + HEAP_HEADER_ARRAY);
    double *base = vm_resolve_ptr(vm, ptr);
    int *types = vm_resolve_type(vm, ptr);

    base[0] = TYPE_ARRAY;
    base[1] = (double)len;

    for (int i = 0; i < len; i++) {
      char tmp[2] = {str[i], '\0'};
      int id = make_string(vm, tmp);
      base[2 + i] = (double)id;
      types[2 + i] = T_STR;
    }
    vm_push(vm, ptr, T_OBJ);
    return;
  }

  int count = 1;
  const char *p = str;
  while ((p = strstr(p, del)) != NULL) {
    count++;
    p += del_len;
  }

  double ptr = heap_alloc(vm, count + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, ptr);
  int *types = vm_resolve_type(vm, ptr);

  base[0] = TYPE_ARRAY;
  base[1] = (double)count;

  int idx = 0;
  p = str;
  const char *next;
  while ((next = strstr(p, del)) != NULL) {
    int token_len = next - p;
    char *buf = malloc(token_len + 1);
    strncpy(buf, p, token_len);
    buf[token_len] = '\0';

    int id = make_string(vm, buf);
    free(buf);

    base[2 + idx] = (double)id;
    types[2 + idx] = T_STR;
    idx++;
    p = next + del_len;
  }

  int id = make_string(vm, p);
  base[2 + idx] = (double)id;
  types[2 + idx] = T_STR;

  vm_push(vm, ptr, T_OBJ);
}

void std_where(VM *vm) {
  double item_val = vm_pop(vm);
  int item_type = vm->stack_types[vm->sp + 1];
  double col_val = vm_pop(vm);
  int col_type = vm->stack_types[vm->sp + 1];

  if (col_type == T_STR) {
    if (item_type != T_STR) {
      vm_push(vm, -1.0, T_NUM);
      return;
    }
    const char *haystack = get_str(vm, col_val);
    const char *needle = get_str(vm, item_val);
    const char *found = strstr(haystack, needle);
    if (found)
      vm_push(vm, (double)(found - haystack), T_NUM);
    else
      vm_push(vm, -1.0, T_NUM);
  } else if (col_type == T_OBJ) {
    double *base = vm_resolve_ptr(vm, col_val);
    int *types = vm_resolve_type(vm, col_val);
    if (!base) {
      vm_push(vm, -1.0, T_NUM);
      return;
    }

    int type = (int)base[0];

    if (type == TYPE_ARRAY) {
      int len = (int)base[1];
      for (int i = 0; i < len; i++) {
        double el = base[2 + i];
        int et = types[2 + i];
        if (el == item_val && et == item_type) {
          vm_push(vm, (double)i, T_NUM);
          return;
        }
      }
    } else if (type == TYPE_BYTES) {
      int len = (int)base[1];
      unsigned char *b = (unsigned char *)&base[HEAP_HEADER_ARRAY];
      for (int i = 0; i < len; i++) {
        if ((double)b[i] == item_val) {
          vm_push(vm, (double)i, T_NUM);
          return;
        }
      }
    }
    vm_push(vm, -1.0, T_NUM);
  } else {
    vm_push(vm, -1.0, T_NUM);
  }
}

void std_range(VM *vm) {
  double stop_val = vm_pop(vm);
  double step_val = vm_pop(vm);
  double start_val = vm_pop(vm);

  if (step_val == 0) {
    printf("Runtime Error: range() step cannot be 0\n");
    exit(1);
  }

  double abs_step = fabs(step_val);
  if (abs_step == 0)
    abs_step = 1.0;

  double diff = fabs(stop_val - start_val);
  int count = (int)((diff / abs_step) + 1.00000001);
  if (count < 0)
    count = 0;

  double ptr = heap_alloc(vm, count + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, ptr);
  int *types = vm_resolve_type(vm, ptr);

  base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  base[HEAP_OFFSET_LEN] = (double)count;

  double current = start_val;
  bool ascending = (start_val <= stop_val);

  for (int i = 0; i < count; i++) {
    base[HEAP_HEADER_ARRAY + i] = current;
    types[HEAP_HEADER_ARRAY + i] = T_NUM;
    if (ascending)
      current += abs_step;
    else
      current -= abs_step;
  }

  vm_push(vm, ptr, T_OBJ);
}

void std_for_list(VM *vm) {
  double list_ref = vm_pop(vm);
  double func_val = vm_pop(vm);

  double *base = vm_resolve_ptr(vm, list_ref);
  int *types = vm_resolve_type(vm, list_ref);

  if (vm->stack_types[vm->sp + 2] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
    printf("Runtime Error: for_list expects an array.\n");
    exit(1);
  }
  if (vm->stack_types[vm->sp + 1] != T_STR) {
    printf("Runtime Error: for_list expects a function name.\n");
    exit(1);
  }

  const char *func_name = get_str(vm, func_val);
  int len = (int)base[HEAP_OFFSET_LEN];

  NativeFunc native_target = NULL;
  for (int i = 0; std_library[i].name != NULL; i++) {
    if (strcmp(std_library[i].name, func_name) == 0) {
      native_target = std_library[i].func;
      break;
    }
  }

  int user_func_addr = -1;
  if (!native_target) {
    user_func_addr = vm_find_function(vm, func_name);
  }

  if (!native_target && user_func_addr == -1) {
    printf("Runtime Error: for_list could not find function '%s'\n", func_name);
    exit(1);
  }

  int saved_ip = vm->ip;

  double res_ptr = heap_alloc(vm, len + HEAP_HEADER_ARRAY);
  double *res_base = vm_resolve_ptr(vm, res_ptr);
  int *res_types = vm_resolve_type(vm, res_ptr);

  res_base[0] = TYPE_ARRAY;
  res_base[1] = (double)len;

  for (int i = 0; i < len; i++) {
    double val = base[HEAP_HEADER_ARRAY + i];
    int type = types[HEAP_HEADER_ARRAY + i];

    if (native_target) {
      vm_push(vm, val, type);
      native_target(vm);
    } else {
      vm_push(vm, (double)vm->code_size, T_NUM);
      vm_push(vm, (double)vm->fp, T_NUM);
      vm_push(vm, val, type);
      vm->fp = vm->sp;

      run_vm_from(vm, user_func_addr, false);
    }

    double res = vm_pop(vm);
    int res_type = vm->stack_types[vm->sp + 1];

    res_base[HEAP_HEADER_ARRAY + i] = res;
    res_types[HEAP_HEADER_ARRAY + i] = res_type;
  }

  vm->ip = saved_ip;
  vm_push(vm, res_ptr, T_OBJ);
}

void std_seed(VM *vm) {
  double val = vm_pop(vm);
  srand((unsigned int)val);
  vm_push(vm, 0.0, T_NUM);
}

void std_rand(VM *vm) {
  double r = (double)rand() / (double)RAND_MAX;
  vm_push(vm, r, T_NUM);
}

void std_rand_normal(VM *vm) {
  double u1 = (double)rand() / (double)RAND_MAX;
  double u2 = (double)rand() / (double)RAND_MAX;
  if (u1 < 1e-9)
    u1 = 1e-9;
  double z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
  vm_push(vm, z0, T_NUM);
}

void std_mix(VM *vm) {
  double a = vm_pop(vm);
  double y = vm_pop(vm);
  double x = vm_pop(vm);
  double result = x + (y - x) * a;
  vm_push(vm, result, T_NUM);
}

void std_min(VM *vm) {
  double b = vm_pop(vm);
  double a = vm_pop(vm);
  if (a < b)
    vm_push(vm, a, T_NUM);
  else
    vm_push(vm, b, T_NUM);
}

void std_max(VM *vm) {
  double b = vm_pop(vm);
  double a = vm_pop(vm);
  if (a > b)
    vm_push(vm, a, T_NUM);
  else
    vm_push(vm, b, T_NUM);
}

void std_dist(VM *vm) {
  double y2 = vm_pop(vm);
  double x2 = vm_pop(vm);
  double y1 = vm_pop(vm);
  double x1 = vm_pop(vm);
  double dx = x2 - x1;
  double dy = y2 - y1;
  double dist = sqrt(dx * dx + dy * dy);
  vm_push(vm, dist, T_NUM);
}

void std_list_min(VM *vm) {
  double list_ref = vm_pop(vm);
  double *base = vm_resolve_ptr(vm, list_ref);

  if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
    printf("Runtime Error: list_min() expects an array.\n");
    exit(1);
  }

  int len = (int)base[HEAP_OFFSET_LEN];
  if (len == 0) {
    printf("Runtime Error: list_min() called on empty array.\n");
    exit(1);
  }

  double min_val = base[HEAP_HEADER_ARRAY + 0];
  for (int i = 1; i < len; i++) {
    double val = base[HEAP_HEADER_ARRAY + i];
    if (val < min_val)
      min_val = val;
  }
  vm_push(vm, min_val, T_NUM);
}

void std_list_max(VM *vm) {
  double list_ref = vm_pop(vm);
  double *base = vm_resolve_ptr(vm, list_ref);

  if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
    printf("Runtime Error: list_max() expects an array.\n");
    exit(1);
  }

  int len = (int)base[HEAP_OFFSET_LEN];
  if (len == 0) {
    printf("Runtime Error: list_max() called on empty array.\n");
    exit(1);
  }

  double max_val = base[HEAP_HEADER_ARRAY + 0];
  for (int i = 1; i < len; i++) {
    double val = base[HEAP_HEADER_ARRAY + i];
    if (val > max_val)
      max_val = val;
  }
  vm_push(vm, max_val, T_NUM);
}

// --- Perlin Noise Internals ---
static int perlin_p[512] = {
    151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140,
    36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120,
    234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33,
    88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71,
    134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133,
    230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161,
    1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130,
    116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250,
    124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227,
    47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44,
    154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98,
    108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34,
    242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14,
    239, 107, 49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121,
    50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243,
    141, 128, 195, 78, 66, 215, 61, 156, 180,
    // Repeat
    151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140,
    36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120,
    234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33,
    88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71,
    134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133,
    230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161,
    1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130,
    116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250,
    124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227,
    47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44,
    154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98,
    108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34,
    242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14,
    239, 107, 49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121,
    50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243,
    141, 128, 195, 78, 66, 215, 61, 156, 180};

static double perlin_fade(double t) {
  return t * t * t * (t * (t * 6 - 15) + 10);
}
static double perlin_lerp(double t, double a, double b) {
  return a + t * (b - a);
}
static double perlin_grad(int hash, double x, double y, double z) {
  int h = hash & 15;
  double u = h < 8 ? x : y;
  double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

void std_noise(VM *vm) {
  double z = vm_pop(vm);
  double y = vm_pop(vm);
  double x = vm_pop(vm);

  int X = (int)floor(x) & 255;
  int Y = (int)floor(y) & 255;
  int Z = (int)floor(z) & 255;

  x -= floor(x);
  y -= floor(y);
  z -= floor(z);

  double u = perlin_fade(x);
  double v = perlin_fade(y);
  double w = perlin_fade(z);

  int A = perlin_p[X] + Y, AA = perlin_p[A] + Z, AB = perlin_p[A + 1] + Z;
  int B = perlin_p[X + 1] + Y, BA = perlin_p[B] + Z, BB = perlin_p[B + 1] + Z;

  double res = perlin_lerp(
      w,
      perlin_lerp(v,
                  perlin_lerp(u, perlin_grad(perlin_p[AA], x, y, z),
                              perlin_grad(perlin_p[BA], x - 1, y, z)),
                  perlin_lerp(u, perlin_grad(perlin_p[AB], x, y - 1, z),
                              perlin_grad(perlin_p[BB], x - 1, y - 1, z))),
      perlin_lerp(
          v,
          perlin_lerp(u, perlin_grad(perlin_p[AA + 1], x, y, z - 1),
                      perlin_grad(perlin_p[BA + 1], x - 1, y, z - 1)),
          perlin_lerp(u, perlin_grad(perlin_p[AB + 1], x, y - 1, z - 1),
                      perlin_grad(perlin_p[BB + 1], x - 1, y - 1, z - 1))));

  vm_push(vm, res, T_NUM);
}

// Helper to check if string ends with suffix
static int ends_with(const char *str, const char *suffix) {
  if (!str || !suffix)
    return 0;
  size_t lenstr = strlen(str);
  size_t lensuffix = strlen(suffix);
  if (lensuffix > lenstr)
    return 0;
  return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

void std_list_dir(VM *vm) {
  // Mylo pops arguments in reverse order of how they are passed
  double filter_id = vm_pop(vm);
  const char *filter = get_str(vm, filter_id);

  double path_id = vm_pop(vm);
  const char *path = get_str(vm, path_id);

  int count = 0;
  int capacity = 16;
  char **filenames = malloc(sizeof(char *) * capacity);

#ifdef _WIN32
  char search_path[MAX_PATH];
  snprintf(search_path, MAX_PATH, "%s\\*", path);

  WIN32_FIND_DATA fFD;
  HANDLE hFind = FindFirstFile(search_path, &fFD);

  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (strcmp(fFD.cFileName, ".") == 0 || strcmp(fFD.cFileName, "..") == 0)
        continue;

      // Apply filter logic
      if (strlen(filter) > 0 && !ends_with(fFD.cFileName, filter))
        continue;

      if (count >= capacity) {
        capacity *= 2;
        filenames = realloc(filenames, sizeof(char *) * capacity);
      }
      filenames[count++] = _strdup(fFD.cFileName);
    } while (FindNextFile(hFind, &fFD));
    FindClose(hFind);
  }
#else
  DIR *d = opendir(path);
  if (d) {
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
      if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
        continue;

      // Apply filter logic
      if (strlen(filter) > 0 && !ends_with(dir->d_name, filter))
        continue;

      if (count >= capacity) {
        capacity *= 2;
        filenames = realloc(filenames, sizeof(char *) * capacity);
      }
      filenames[count++] = strdup(dir->d_name);
    }
    closedir(d);
  }
#endif

  // Push the results to the Mylo Heap
  double arr_addr = heap_alloc(vm, count + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, arr_addr);
  int *types = vm_resolve_type(vm, arr_addr);

  base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  base[HEAP_OFFSET_LEN] = (double)count;

  for (int i = 0; i < count; i++) {
    int id = make_string(vm, filenames[i]);
    base[HEAP_HEADER_ARRAY + i] = (double)id;
    types[HEAP_HEADER_ARRAY + i] = T_STR;
    free(filenames[i]);
  }
  free(filenames);

  vm_push(vm, arr_addr, T_OBJ);
}

// Helper to read a whole file into a string
static char *read_whole_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return strdup("");
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(sz + 1);
  if (buf) {
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
  }
  fclose(f);
  return buf ? buf : strdup("");
}

// Platform agnostic execution logic
void internal_exec_command(const char *cmd, char **out_str, char **err_str) {
  char stderr_path[256];
  char actual_cmd[4096];

  // 1. Generate unique temp file for stderr
  // Using a random ID to avoid collisions
  int rnd = rand();
#ifdef _WIN32
  sprintf(stderr_path, "%s\\mylo_err_%d.tmp", getenv("TEMP"), rnd);
  // cmd 2> temp_file
  snprintf(actual_cmd, sizeof(actual_cmd), "%s 2> \"%s\"", cmd, stderr_path);
#else
  sprintf(stderr_path, "/tmp/mylo_err_%d.tmp", rnd);
  snprintf(actual_cmd, sizeof(actual_cmd), "%s 2> %s", cmd, stderr_path);
#endif

  // 2. Run with popen to capture stdout directly
  FILE *fp = popen(actual_cmd, "r");

  // 3. Read stdout
  size_t out_cap = 1024;
  size_t out_len = 0;
  char *out_buf = malloc(out_cap);
  out_buf[0] = '\0';

  if (fp) {
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
      size_t chunk_len = strlen(buffer);
      if (out_len + chunk_len >= out_cap) {
        out_cap *= 2;
        out_buf = realloc(out_buf, out_cap);
      }
      strcpy(out_buf + out_len, buffer);
      out_len += chunk_len;
    }
    pclose(fp);
  }
  *out_str = out_buf;

  // 4. Read stderr from temp file
  *err_str = read_whole_file(stderr_path);

  // 5. Cleanup
  remove(stderr_path);
}

// Thread Worker Entry Point
#ifdef _WIN32
unsigned __stdcall job_worker(void *arg) {
#else
void *job_worker(void *arg) {
#endif
  Job *job = (Job *)arg;

  char *o = NULL;
  char *e = NULL;
  internal_exec_command(job->cmd, &o, &e);

  LOCK_JOBS;
  job->out_res = o;
  job->err_res = e;
  job->status = 1; // Done
  UNLOCK_JOBS;

  free(job->cmd); // Free the command string copy
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

// --- 1. System (Blocking) ---
void std_system(VM *vm) {
  double cmd_id = vm_pop(vm);
  const char *cmd = get_str(vm, cmd_id);

  char *out_str = NULL;
  char *err_str = NULL;

  internal_exec_command(cmd, &out_str, &err_str);

  // Create Mylo Array [stdout, stderr]
  double arr_ptr = heap_alloc(vm, 2 + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, arr_ptr);
  int *types = vm_resolve_type(vm, arr_ptr);

  base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  base[HEAP_OFFSET_LEN] = 2.0;

  int id_out = make_string(vm, out_str ? out_str : "");
  int id_err = make_string(vm, err_str ? err_str : "");

  base[HEAP_HEADER_ARRAY + 0] = (double)id_out;
  types[HEAP_HEADER_ARRAY + 0] = T_STR;

  base[HEAP_HEADER_ARRAY + 1] = (double)id_err;
  types[HEAP_HEADER_ARRAY + 1] = T_STR;

  if (out_str)
    free(out_str);
  if (err_str)
    free(err_str);

  vm_push(vm, arr_ptr, T_OBJ);
}

// --- 2. System Thread (Non-Blocking) ---
void std_system_thread(VM *vm) {
  double name_id = vm_pop(vm);
  double cmd_id = vm_pop(vm);
  const char *name = get_str(vm, name_id);
  const char *cmd = get_str(vm, cmd_id);

  LOCK_JOBS;
  int slot = -1;
  for (int i = 0; i < MAX_JOBS; i++) {
    if (!job_registry[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    UNLOCK_JOBS;
    vm_push(vm, 0.0, T_NUM); // Failed (No slots)
    return;
  }

  // Setup Job
  strncpy(job_registry[slot].name, name, 63);
  job_registry[slot].cmd = strdup(cmd);
  job_registry[slot].out_res = NULL;
  job_registry[slot].err_res = NULL;
  job_registry[slot].status = 0; // Running
  job_registry[slot].active = 1;

  // Spawn Thread
#ifdef _WIN32
  unsigned threadID;
  job_registry[slot].thread = (HANDLE)_beginthreadex(
      NULL, 0, &job_worker, &job_registry[slot], 0, &threadID);
#else
  pthread_create(&job_registry[slot].thread, NULL, &job_worker,
                 &job_registry[slot]);
  pthread_detach(job_registry[slot].thread); // Detach so we don't need to join
#endif

  UNLOCK_JOBS;
  vm_push(vm, 1.0, T_NUM); // Success
}

// --- 3. Get Job (Poll Status) ---
void std_get_job(VM *vm) {
  double name_id = vm_pop(vm);
  const char *name = get_str(vm, name_id);

  LOCK_JOBS;
  int slot = -1;
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_registry[i].active && strcmp(job_registry[i].name, name) == 0) {
      slot = i;
      break;
    }
  }

  // Case 1: Job not found
  if (slot == -1) {
    UNLOCK_JOBS;
    vm_push(vm, -1.0, T_NUM);
    return;
  }

  // Case 2: Still Running
  if (job_registry[slot].status == 0) {
    UNLOCK_JOBS;
    vm_push(vm, 1.0, T_NUM);
    return;
  }

  // Case 3: Done - Return [out, err] and clean up
  char *o = job_registry[slot].out_res;
  char *e = job_registry[slot].err_res;

  // Make Mylo Array
  // NOTE: We must unlock before Allocating to prevent heap locks (if heap used
  // mutexes later) But here we need to copy strings to VM first. For safety, we
  // will grab the raw C strings, clear the job, unlock, THEN alloc VM memory.

  char *safe_o = o ? strdup(o) : strdup("");
  char *safe_e = e ? strdup(e) : strdup("");

  // Cleanup registry slot
  if (job_registry[slot].out_res)
    free(job_registry[slot].out_res);
  if (job_registry[slot].err_res)
    free(job_registry[slot].err_res);
  job_registry[slot].active = 0; // Free the slot

  UNLOCK_JOBS;

  // Now safe to alloc on VM Heap
  double arr_ptr = heap_alloc(vm, 2 + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, arr_ptr);
  int *types = vm_resolve_type(vm, arr_ptr);

  base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  base[HEAP_OFFSET_LEN] = 2.0;

  int id_out = make_string(vm, safe_o);
  int id_err = make_string(vm, safe_e);

  base[HEAP_HEADER_ARRAY + 0] = (double)id_out;
  types[HEAP_HEADER_ARRAY + 0] = T_STR;

  base[HEAP_HEADER_ARRAY + 1] = (double)id_err;
  types[HEAP_HEADER_ARRAY + 1] = T_STR;

  free(safe_o);
  free(safe_e);

  vm_push(vm, arr_ptr, T_OBJ);
}

// Bus commands
// bus_set(key, value)
void std_bus_set(VM *vm) {
  init_bus();

  // 1. Pop arguments
  double val = vm_pop(vm);
  int val_type = vm->stack_types[vm->sp + 1];

  double key_val = vm_pop(vm);
  // Ensure key is a string
  if (vm->stack_types[vm->sp + 1] != T_STR) {
    printf("Bus Error: Key must be a string.\n");
    vm_push(vm, 0.0, T_NUM);
    return;
  }
  const char *key = get_str(vm, key_val);

  LOCK_BUS;

  // 2. Find existing entry
  int idx = -1;
  for (int i = 0; i < bus_count; i++) {
    if (global_bus[i].key && strcmp(global_bus[i].key, key) == 0) {
      idx = i;
      break;
    }
  }

  // 3. Create new entry if needed
  if (idx == -1) {
    if (bus_count >= MAX_BUS_ENTRIES) {
      UNLOCK_BUS;
      // Bus full
      vm_push(vm, 0.0, T_NUM);
      return;
    }
    idx = bus_count++;
    global_bus[idx].key = strdup(key);
  } else {
    // Free old string value if overwriting
    if (global_bus[idx].type == T_STR && global_bus[idx].str_val) {
      free(global_bus[idx].str_val);
      global_bus[idx].str_val = NULL;
    }
  }

  // 4. Store Value
  // NOTE: We only support Primitives and Strings.
  // Objects cannot be safely shared across VMs without deep serialization.
  if (val_type == T_OBJ) {
    // Fallback for objects: store as string representation
    global_bus[idx].type = T_OBJ;
    global_bus[idx].num_val = val;
  } else if (val_type == T_STR) {
    global_bus[idx].type = T_STR;
    // Must copy string because the source VM might GC the original
    global_bus[idx].str_val = strdup(get_str(vm, val));
  } else {
    global_bus[idx].type = T_NUM;
    global_bus[idx].num_val = val;
  }

  UNLOCK_BUS;
  vm_push(vm, 1.0, T_NUM); // Success
}

// bus_get(key) -> value
void std_bus_get(VM *vm) {
  init_bus();

  double key_val = vm_pop(vm);
  const char *key = get_str(vm, key_val);

  LOCK_BUS;

  for (int i = 0; i < bus_count; i++) {
    if (global_bus[i].key && strcmp(global_bus[i].key, key) == 0) {
      int type = global_bus[i].type;

      if (type == T_NUM) {
        vm_push(vm, global_bus[i].num_val, T_NUM);
      } else if (type == T_STR) {
        // Create a new string in the CALLING VM's heap
        int id = make_string(vm, global_bus[i].str_val);
        vm_push(vm, (double)id, T_STR);
      } else if (type == T_OBJ) {
        vm_push(vm, global_bus[i].num_val, T_OBJ);
      } else {
        vm_push(vm, 0.0, T_NUM);
      }

      UNLOCK_BUS;
      return;
    }
  }

  UNLOCK_BUS;
  // Not found return 0 (or null equivalent)
  vm_push(vm, 0.0, T_NUM);
}

// Terminal control
#ifndef _WIN32
// Helper for non-blocking check on POSIX systems
int mylo_linux_kbhit() {
  struct termios oldt, newt;
  int ch;
  int oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if (ch != EOF) {
    ungetc(ch, stdin);
    return 1;
  }
  return 0;
}

// Helper for unbuffered char read on POSIX systems
int mylo_linux_getch() {
  struct termios oldt, newt;
  int ch;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  return ch;
}
#endif

// --- [INSERT] Implementation Functions ---

// std_cget(blocking) -> string_char
void std_cget(VM *vm) {
  double mode_val = vm_pop(vm);
  int blocking = (int)mode_val; // 1 = Blocking, 0 = Non-Blocking
  int ch = -1;

#ifdef _WIN32
  if (blocking) {
    ch = _getch();
  } else {
    if (_kbhit())
      ch = _getch();
  }
#else
  if (blocking) {
    ch = mylo_linux_getch();
  } else {
    if (mylo_linux_kbhit())
      ch = mylo_linux_getch();
  }
#endif

  if (ch != -1) {
    char buf[2] = {(char)ch, '\0'};
    int id = make_string(vm, buf);
    vm_push(vm, (double)id, T_STR);
  } else {
    int id = make_string(vm, "");
    vm_push(vm, (double)id, T_STR);
  }
}

// std_kbhit() -> num (1 or 0)
void std_kbhit(VM *vm) {
  int hit = 0;
#ifdef _WIN32
  hit = _kbhit();
#else
  hit = mylo_linux_kbhit();
#endif
  vm_push(vm, hit ? 1.0 : 0.0, T_NUM);
}

// Helper to decode special keys (Platform Agnostic Logic Wrapper)
// Returns a heap-allocated string for the key name, or NULL
char *decode_key_event(int c, bool *is_special) {
  *is_special = false;

  // 1. Control Codes (ASCII 1-31, excluding formatters)
  if (c > 0 && c <= 26) {
    // Special mapping for common controls
    if (c == 8)
      return STRDUP("[BACKSPACE]");
    if (c == 9)
      return STRDUP("[TAB]");
    if (c == 13)
      return STRDUP("[ENTER]");
    if (c == 27)
      return STRDUP("[ESC]");

    // Standard Ctrl+A to Ctrl+Z
    char buf[16];
    snprintf(buf, 16, "[CTRL+%c]", 'A' + c - 1);
    *is_special = true;
    return STRDUP(buf);
  }

  // 2. Printable Characters
  if (c >= 32 && c <= 126) {
    char buf[2] = {(char)c, '\0'};
    return STRDUP(buf);
  }

  return NULL; // Unknown/Binary
}

// Helper: Windows Special Key Mapper
#ifdef _WIN32
char *win_map_extended(int c) {
  switch (c) {
  case 72:
    return _strdup("[UP]");
  case 80:
    return _strdup("[DOWN]");
  case 75:
    return _strdup("[LEFT]");
  case 77:
    return _strdup("[RIGHT]");
  case 71:
    return _strdup("[HOME]");
  case 79:
    return _strdup("[END]");
  case 82:
    return _strdup("[INS]");
  case 83:
    return _strdup("[DEL]");
  case 59:
    return _strdup("[F1]");
  case 60:
    return _strdup("[F2]");
  case 61:
    return _strdup("[F3]");
  case 62:
    return _strdup("[F4]");
  case 63:
    return _strdup("[F5]");
  case 64:
    return _strdup("[F6]");
  case 65:
    return _strdup("[F7]");
  case 66:
    return _strdup("[F8]");
  case 67:
    return _strdup("[F9]");
  case 68:
    return _strdup("[F10]");
  default:
    return NULL;
  }
}
#endif

// std_get_keys() -> arr
// Returns a list of all key events currently in the buffer
void std_get_keys(VM *vm) {
  char *collected_keys[128]; // Max buffer per frame
  int count = 0;

#ifdef _WIN32
  while (_kbhit() && count < 128) {
    int ch = _getch();
    if (ch == 0 || ch == 0xE0) {
      // Extended key sequence (read next byte)
      int ext = _getch();
      char *spec = win_map_extended(ext);
      if (spec)
        collected_keys[count++] = spec;
    } else {
      bool special = false;
      char *s = decode_key_event(ch, &special);
      if (s)
        collected_keys[count++] = s;
    }
  }
#else
  // Linux/POSIX Sequence parsing
  while (mylo_linux_kbhit() && count < 128) {
    int ch = mylo_linux_getch();

    if (ch == 27) { // ESC sequence start?
      // Peek next chars non-blocking to see if it's a sequence
      if (mylo_linux_kbhit()) {
        int next = mylo_linux_getch();
        if (next == '[') {
          int dir = mylo_linux_getch(); // A, B, C, D...
          switch (dir) {
          case 'A':
            collected_keys[count++] = strdup("[UP]");
            break;
          case 'B':
            collected_keys[count++] = strdup("[DOWN]");
            break;
          case 'C':
            collected_keys[count++] = strdup("[RIGHT]");
            break;
          case 'D':
            collected_keys[count++] = strdup("[LEFT]");
            break;
          case 'H':
            collected_keys[count++] = strdup("[HOME]");
            break;
          case 'F':
            collected_keys[count++] = strdup("[END]");
            break;
          default:
            collected_keys[count++] = strdup("[UNKNOWN_SEQ]");
            break;
          }
        } else {
          // Alt+Key pattern (ESC followed by char)
          if (next >= 32 && next <= 126) {
            char buf[16];
            snprintf(buf, 16, "[ALT+%c]", (char)next);
            collected_keys[count++] = strdup(buf);
          } else {
            collected_keys[count++] = strdup("[ESC]");
            // We consumed 'next' but couldn't parse it well, ignore or map raw?
          }
        }
      } else {
        collected_keys[count++] = strdup("[ESC]");
      }
    } else {
      bool special = false;
      char *s = decode_key_event(ch, &special);
      if (s)
        collected_keys[count++] = s;
    }
  }
#endif

  // Construct Mylo Array
  double ptr = heap_alloc(vm, count + HEAP_HEADER_ARRAY);
  double *base = vm_resolve_ptr(vm, ptr);
  int *types = vm_resolve_type(vm, ptr);

  base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  base[HEAP_OFFSET_LEN] = (double)count;

  for (int i = 0; i < count; i++) {
    int id = make_string(vm, collected_keys[i]);
    base[HEAP_HEADER_ARRAY + i] = (double)id;
    types[HEAP_HEADER_ARRAY + i] = T_STR;
    free(collected_keys[i]); // Free the C-string temp copy
  }

  vm_push(vm, ptr, T_OBJ);
}

// Minimal HTML Screen with SSE Listener
const char *HTML_SCREEN =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><title>Mylo Canvas</title></head>"
    "<body style='margin:0;background:#000;overflow:hidden;'>"
    "<canvas id='v'></canvas><script>"
    "const cvs=document.getElementById('v'), ctx=cvs.getContext('2d');"
    "const res=()=>{cvs.width=window.innerWidth; "
    "cvs.height=window.innerHeight;};"
    "window.onresize=res; res();"

    // Shader Cache to prevent re-compiling every frame
    "const shaders = new Map();"
    "const getShader = (w, h, code) => {"
    "  if(shaders.has(code)) return shaders.get(code);"
    "  const sc = document.createElement('canvas'); sc.width=w; sc.height=h;"
    "  const gl = sc.getContext('webgl');"
    "  const vs = 'attribute vec2 p; void main(){gl_Position=vec4(p,0,1);}';"
    "  const createS = (t, s) => { const sh=gl.createShader(t); "
    "gl.shaderSource(sh,s); gl.compileShader(sh); return sh; };"
    "  const prog = gl.createProgram();"
    "  gl.attachShader(prog, createS(gl.VERTEX_SHADER, vs));"
    "  gl.attachShader(prog, createS(gl.FRAGMENT_SHADER, code));"
    "  gl.linkProgram(prog); gl.useProgram(prog);"
    "  const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);"
    "  gl.bufferData(gl.ARRAY_BUFFER, new "
    "Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]), gl.STATIC_DRAW);"
    "  const pL = gl.getAttribLocation(prog, 'p'); "
    "gl.enableVertexAttribArray(pL);"
    "  gl.vertexAttribPointer(pL, 2, gl.FLOAT, false, 0, 0);"
    "  const resL = gl.getUniformLocation(prog, 'u_resolution');"
    "  const timeL = gl.getUniformLocation(prog, 'u_time');"
    "  const render = (t) => {"
    "    gl.uniform2f(resL, w, h); gl.uniform1f(timeL, t/1000);"
    "    gl.drawArrays(gl.TRIANGLES, 0, 6); return sc;"
    "  };"
    "  shaders.set(code, render); return render;"
    "};"

    "const es=new EventSource('/stream');"
    "es.onmessage=(e)=>{"
    "  const [cmd, ...a]=e.data.split('|');"
    "  ctx.fillStyle = ctx.strokeStyle = a[a.length-1];"
    "  if(cmd==='CLR') ctx.clearRect(0,0,cvs.width,cvs.height);"
    "  else if(cmd==='R') "
    "ctx.fillRect(parseFloat(a[0]),parseFloat(a[1]),parseFloat(a[2]),"
    "parseFloat(a[3]));"
    "  else if(cmd==='C') {"
    "    ctx.beginPath(); "
    "ctx.arc(parseFloat(a[0]),parseFloat(a[1]),parseFloat(a[2]),0,Math.PI*2); "
    "ctx.fill();"
    "  }"
    "  else if(cmd==='L') {"
    "    ctx.lineWidth=parseFloat(a[4]); ctx.beginPath();"
    "    ctx.moveTo(parseFloat(a[0]),parseFloat(a[1])); "
    "ctx.lineTo(parseFloat(a[2]),parseFloat(a[3])); ctx.stroke();"
    "  }"
    "  else if(cmd==='TRI') {"
    "    ctx.beginPath(); ctx.moveTo(parseFloat(a[0]),parseFloat(a[1]));"
    "    ctx.lineTo(parseFloat(a[2]),parseFloat(a[3])); "
    "ctx.lineTo(parseFloat(a[4]),parseFloat(a[5]));"
    "    ctx.closePath(); ctx.fill();"
    "  }"
    "  else if(cmd==='SHD') {"
    "    const draw = getShader(parseFloat(a[2]), parseFloat(a[3]), a[4]);"
    "    ctx.drawImage(draw(performance.now()), parseFloat(a[0]), "
    "parseFloat(a[1]));"
    "  }"
    "  else if(cmd==='T') {"
    "    ctx.font = a[2]+'px monospace'; ctx.fillText(a[3], parseFloat(a[0]), "
    "parseFloat(a[1]));"
    "  }"
    "};"
    "es.onerror=(e)=>console.error('SSE Error:', e);"
    "</script></body></html>";

static int server_fd = -1;
static int client_fd = -1;

void std_canvas_init(VM *vm) {
  int port = 8080;
  struct sockaddr_in addr;
  int opt = 1;

  // ONLY create the server socket if it doesn't exist yet
  if (server_fd == -1) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      printf("Bind failed: %d\n", errno);
      vm_push(vm, 0, T_NUM);
      return;
    }
    listen(server_fd, 3);
    printf("Mylo Display active: http://localhost:%d\n", port);
  }

  printf("Waiting for browser connection...\n");

  while (1) {
    int fd = accept(server_fd, NULL, NULL);
    char buf[1024] = {0};
    recv(fd, buf, 1024, 0);

    if (strstr(buf, "GET /stream")) {
      const char *sse_hdr = "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/event-stream\r\n"
                            "Cache-Control: no-cache\r\n"
                            "Connection: keep-alive\r\n\r\n";
      send(fd, sse_hdr, strlen(sse_hdr), 0);
      client_fd = fd;
      printf("Visual Stream Connected!\n");
      break;
    } else {
      send(fd, HTML_SCREEN, strlen(HTML_SCREEN), 0);
#ifdef _WIN32
      closesocket(fd);
#else
      close(fd);
#endif
    }
  }
  vm_push(vm, 1.0, T_NUM); // Return true to indicate successful connection
}
// --- Helper: Minimal Base64 for Image Transmission ---
char *base64_encode(const unsigned char *data, size_t input_length) {
  const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t output_length = 4 * ((input_length + 2) / 3);
  char *encoded_data = malloc(output_length + 1);
  for (size_t i = 0, j = 0; i < input_length;) {
    uint32_t octet_a = i < input_length ? data[i++] : 0;
    uint32_t octet_b = i < input_length ? data[i++] : 0;
    uint32_t octet_c = i < input_length ? data[i++] : 0;
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
    encoded_data[j++] = table[(triple >> 18) & 0x3F];
    encoded_data[j++] = table[(triple >> 12) & 0x3F];
    encoded_data[j++] = table[(triple >> 6) & 0x3F];
    encoded_data[j++] = table[triple & 0x3F];
  }
  encoded_data[output_length] = '\0';
  return encoded_data;
}

// --- SSE Event Dispatcher ---
void send_event_sse(const char *cmd, const char *data) {
  if (client_fd == -1)
    return;

  char buf[4096];
  int len = snprintf(buf, sizeof(buf), "data: %s|%s\n\n", cmd, data);

#ifdef _WIN32
  int sent = send(client_fd, buf, len, 0);
#else
  // MSG_NOSIGNAL prevents the OS from killing Mylo when the browser closes
  int sent = send(client_fd, buf, len, MSG_NOSIGNAL);
#endif

  if (sent < 0) {
#ifdef _WIN32
    closesocket(client_fd);
#else
    close(client_fd);
#endif
    client_fd = -1;
  }
}

void std_web_status(VM *vm) {
  double wait_val = vm_pop(vm);
  int wait_for_reconnect = (int)wait_val;

  // If lost and wait is requested, re-trigger the init logic
  if (client_fd == -1 && wait_for_reconnect == 1) {
    printf("Connection lost. Re-initializing listener...\n");
    // Reuse your existing std_canvas_init logic
    std_canvas_init(vm);
    return; // std_canvas_init already pushes a return value
  }

  // Return 1 if connected, 0 otherwise
  vm_push(vm, client_fd != -1 ? 1.0 : 0.0, T_NUM);
}

// --- Mylo Native Functions ---
void std_canvas_text(VM *vm) {
  // Signature: text(str, x, y, size)
  double size = vm_pop(vm);
  double y = vm_pop(vm);
  double x = vm_pop(vm);
  char *text = (char *)get_str(vm, vm_pop(vm));

  char payload[512];
  snprintf(payload, 512, "%f|%f|%f|%s", x, y, size, text);
  send_event_sse("T", payload);
  vm_push(vm, 0, T_NUM);
}

void std_canvas_clear(VM *vm) {
  send_event_sse("CLR", "");
  vm_push(vm, 0, T_NUM);
}

// --- Helper to push return value ---
void web_ret(VM *vm) { vm_push(vm, 0, T_NUM); }

void std_web_rect(VM *vm) {
  char *color = (char *)get_str(vm, vm_pop(vm));
  double h = vm_pop(vm);
  double w = vm_pop(vm);
  double y = vm_pop(vm);
  double x = vm_pop(vm);

  char payload[256];
  snprintf(payload, 256, "%f|%f|%f|%f|%s", x, y, w, h, color);
  send_event_sse("R", payload);
  web_ret(vm);
}

void std_web_circle(VM *vm) {
  char *color = (char *)get_str(vm, vm_pop(vm));
  double r = vm_pop(vm);
  double y = vm_pop(vm);
  double x = vm_pop(vm);

  char payload[256];
  snprintf(payload, 256, "%f|%f|%f|%s", x, y, r, color);
  send_event_sse("C", payload);
  web_ret(vm);
}

void std_web_line(VM *vm) {
  char *color = (char *)get_str(vm, vm_pop(vm));
  double thickness = vm_pop(vm);
  double y2 = vm_pop(vm);
  double x2 = vm_pop(vm);
  double y1 = vm_pop(vm);
  double x1 = vm_pop(vm);

  char payload[256];
  snprintf(payload, 256, "%f|%f|%f|%f|%f|%s", x1, y1, x2, y2, thickness, color);
  send_event_sse("L", payload);
  web_ret(vm);
}

void std_web_triangle(VM *vm) {
  char *color = (char *)get_str(vm, vm_pop(vm)); // Arg 7
  double y3 = vm_pop(vm);                        // Arg 6
  double x3 = vm_pop(vm);                        // Arg 5
  double y2 = vm_pop(vm);                        // Arg 4
  double x2 = vm_pop(vm);                        // Arg 3
  double y1 = vm_pop(vm);                        // Arg 2
  double x1 = vm_pop(vm);                        // Arg 1

  char payload[512];
  snprintf(payload, sizeof(payload), "%f|%f|%f|%f|%f|%f|%s", x1, y1, x2, y2, x3,
           y3, color);
  send_event_sse("TRI", payload);
  web_ret(vm);
}
#define RUNTIME_ERROR(fmt, ...) mylo_runtime_error(vm, fmt, ##__VA_ARGS__)

void std_call(VM *vm) {
  // 1. Pop arguments: call("func_name", [args])
  double arr_val = vm_pop(vm);
  int arr_type = vm->stack_types[vm->sp + 1];

  double name_val = vm_pop(vm);
  int name_type = vm->stack_types[vm->sp + 1];

  if (name_type != T_STR || arr_type != T_OBJ) {
    RUNTIME_ERROR("call() expects (str, array) as arguments.");
  }

  const char *func_name = vm->string_pool[(int)name_val];

  double *base = vm_resolve_ptr_safe(vm, arr_val);
  int *types = vm_resolve_type(vm, arr_val);

  // Verify it is a standard array
  if (!base || (int)base[HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
    RUNTIME_ERROR("call() expects a valid array for arguments.");
  }

  int argc = (int)base[HEAP_OFFSET_LEN];

  // --- 1. Keyword "print" Intercept ---
  if (strcmp(func_name, "print") == 0) {
    for (int i = 0; i < argc; i++) {
      if (i > 0)
        print_raw(vm, " ");
      print_recursive(vm, base[HEAP_HEADER_ARRAY + i],
                      types[HEAP_HEADER_ARRAY + i], 0, -1);
    }
    print_raw(vm, "\n");
    vm_push(vm, 0.0, T_NUM); // print returns nothing, default to 0
    return;
  }

  // --- 2. Check Standard Library Functions ---
  int std_idx = -1;
  for (int i = 0; std_library[i].name != NULL; i++) {
    if (strcmp(std_library[i].name, func_name) == 0) {
      std_idx = i;
      break;
    }
  }

  if (std_idx != -1) {
    // Push arguments back to stack for the native function
    for (int i = 0; i < argc; i++) {
      vm_push(vm, base[HEAP_HEADER_ARRAY + i], types[HEAP_HEADER_ARRAY + i]);
    }
    if (vm->natives[std_idx])
      vm->natives[std_idx](vm);
    return;
  }

  // --- 3. Check Mylo Bytecode Functions ---
  int target_ip = vm_find_function(vm, func_name);
  if (target_ip != -1) {
    // Push arguments to stack
    for (int i = 0; i < argc; i++) {
      vm_push(vm, base[HEAP_HEADER_ARRAY + i], types[HEAP_HEADER_ARRAY + i]);
    }

    // Mimic OP_CALL stack framing
    int args_start = vm->sp - argc + 1;

    // Shift arguments up by 2 to make room for IP and FP pointers
    for (int i = 0; i < argc; i++) {
      vm->stack[vm->sp + 2 - i] = vm->stack[vm->sp - i];
      vm->stack_types[vm->sp + 2 - i] = vm->stack_types[vm->sp - i];
    }

    // Insert old IP and FP below the arguments
    vm->stack[args_start] = (double)vm->ip;
    vm->stack[args_start + 1] = (double)vm->fp;
    vm->stack_types[args_start] = T_NUM;
    vm->stack_types[args_start + 1] = T_NUM;

    // Advance pointers and jump!
    vm->sp += 2;
    vm->fp = args_start + 2;
    vm->ip = target_ip;
    return;
  }

  // Format the error string safely
  char err_msg[256];
  snprintf(err_msg, sizeof(err_msg), "call(): Function '%s' not found",
           func_name);
  RUNTIME_ERROR(err_msg);
}

void std_filter(VM *vm) {
  // Arguments are pushed left-to-right, so the string is popped first.
  // Example: filter(L, "filt") -> Pop 1: "filt", Pop 2: L
  double func_val = vm_pop(vm);
  int func_type = vm->stack_types[vm->sp + 1];

  double list_ref = vm_pop(vm);
  int list_type = vm->stack_types[vm->sp + 1];

  if (list_type != T_OBJ) {
    printf("Runtime Error: filter() expects an array as the first argument.\n");
    exit(1);
  }
  if (func_type != T_STR) {
    printf("Runtime Error: filter() expects a string (function name) as the "
           "second argument.\n");
    exit(1);
  }

  double *base = vm_resolve_ptr(vm, list_ref);
  int *types = vm_resolve_type(vm, list_ref);

  if (!base || (int)base[HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
    printf("Runtime Error: filter() expects a valid array.\n");
    exit(1);
  }

  const char *func_name = get_str(vm, func_val);
  int len = (int)base[HEAP_OFFSET_LEN];

  // Find the target function (Native stdlib or User VM Function)
  NativeFunc native_target = NULL;
  for (int i = 0; std_library[i].name != NULL; i++) {
    if (strcmp(std_library[i].name, func_name) == 0) {
      native_target = std_library[i].func;
      break;
    }
  }

  int user_func_addr = -1;
  if (!native_target) {
    user_func_addr = vm_find_function(vm, func_name);
  }

  if (!native_target && user_func_addr == -1) {
    printf("Runtime Error: filter() could not find function '%s'\n", func_name);
    exit(1);
  }

  int saved_ip = vm->ip;

  // Pre-allocate the maximum possible size for the new array on the heap
  double res_ptr = heap_alloc(vm, len + HEAP_HEADER_ARRAY);
  double *res_base = vm_resolve_ptr(vm, res_ptr);
  int *res_types = vm_resolve_type(vm, res_ptr);

  res_base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;

  int kept_count = 0;

  for (int i = 0; i < len; i++) {
    double val = base[HEAP_HEADER_ARRAY + i];
    int type = types[HEAP_HEADER_ARRAY + i];

    if (native_target) {
      vm_push(vm, val, type);
      native_target(vm);
    } else {
      // Setup dummy stack frame for the VM function call
      vm_push(vm, (double)vm->code_size, T_NUM); // Return IP
      vm_push(vm, (double)vm->fp, T_NUM);        // Return FP
      vm_push(vm, val, type);                    // Argument
      vm->fp = vm->sp;

      run_vm_from(vm, user_func_addr, false);
    }

    double res = vm_pop(vm);
    int res_type = vm->stack_types[vm->sp + 1];

    // Filter logic: keep if the function evaluates to a truthy (non-zero)
    // number
    bool keep = false;
    if (res_type == T_NUM && res != 0.0) {
      keep = true;
    }

    if (keep) {
      res_base[HEAP_HEADER_ARRAY + kept_count] = val;
      res_types[HEAP_HEADER_ARRAY + kept_count] = type;
      kept_count++;
    }
  }

  // Set the actual length to how many elements passed the filter
  res_base[HEAP_OFFSET_LEN] = (double)kept_count;

  vm->ip = saved_ip;
  vm_push(vm, res_ptr, T_OBJ);
}

void std_param_filter(VM *vm) {
  // Pop in reverse order: param, func_name, list
  double param_val = vm_pop(vm);
  int param_type = vm->stack_types[vm->sp + 1];

  double func_val = vm_pop(vm);
  int func_type = vm->stack_types[vm->sp + 1];

  double list_ref = vm_pop(vm);
  int list_type = vm->stack_types[vm->sp + 1];

  if (list_type != T_OBJ) {
    printf("Runtime Error: param_filter() expects an array as the first "
           "argument.\n");
    exit(1);
  }
  if (func_type != T_STR) {
    printf("Runtime Error: param_filter() expects a string as the second "
           "argument.\n");
    exit(1);
  }

  double *base = vm_resolve_ptr(vm, list_ref);
  int *types = vm_resolve_type(vm, list_ref);

  if (!base || (int)base[HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
    printf("Runtime Error: param_filter() expects a valid array.\n");
    exit(1);
  }

  const char *func_name = get_str(vm, func_val);
  int len = (int)base[HEAP_OFFSET_LEN];

  NativeFunc native_target = NULL;
  for (int i = 0; std_library[i].name != NULL; i++) {
    if (strcmp(std_library[i].name, func_name) == 0) {
      native_target = std_library[i].func;
      break;
    }
  }

  int user_func_addr = -1;
  if (!native_target) {
    user_func_addr = vm_find_function(vm, func_name);
  }

  if (!native_target && user_func_addr == -1) {
    printf("Runtime Error: param_filter() could not find function '%s'\n",
           func_name);
    exit(1);
  }

  int saved_ip = vm->ip;

  double res_ptr = heap_alloc(vm, len + HEAP_HEADER_ARRAY);
  double *res_base = vm_resolve_ptr(vm, res_ptr);
  int *res_types = vm_resolve_type(vm, res_ptr);

  res_base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
  int kept_count = 0;

  for (int i = 0; i < len; i++) {
    double val = base[HEAP_HEADER_ARRAY + i];
    int type = types[HEAP_HEADER_ARRAY + i];

    if (native_target) {
      vm_push(vm, val, type);
      vm_push(vm, param_val, param_type);
      native_target(vm);
    } else {
      // Setup stack frame for a 2-argument Mylo function
      vm_push(vm, (double)vm->code_size, T_NUM); // Return IP
      vm_push(vm, (double)vm->fp, T_NUM);        // Return FP

      vm_push(vm, val, type); // Arg 1 (The element)
      int new_fp = vm->sp;    // FP points to the first argument

      vm_push(vm, param_val, param_type); // Arg 2 (The parameter)
      vm->fp = new_fp;

      run_vm_from(vm, user_func_addr, false);
    }

    double res = vm_pop(vm);
    int res_type = vm->stack_types[vm->sp + 1];

    if (res_type == T_NUM && res != 0.0) {
      res_base[HEAP_HEADER_ARRAY + kept_count] = val;
      res_types[HEAP_HEADER_ARRAY + kept_count] = type;
      kept_count++;
    }
  }

  res_base[HEAP_OFFSET_LEN] = (double)kept_count;
  vm->ip = saved_ip;
  vm_push(vm, res_ptr, T_OBJ);
}

const StdLibDef std_library[] = {
    {"type", std_type, "str", 1, {"any"}},
    {"len", std_len, "num", 1, {"any"}},
    {"contains", std_contains, "num", 2, {"any", "any"}},
    {"to_string", std_to_string, "str", 1, {"any"}},
    {"to_num", std_to_num, "num", 1, {"any"}},
    {"sqrt", std_sqrt, "num", 1, {"num"}},
    {"sin", std_sin, "num", 1, {"num"}},
    {"cos", std_cos, "num", 1, {"num"}},
    {"tan", std_tan, "num", 1, {"num"}},
    {"floor", std_floor, "num", 1, {"num"}},
    {"ceil", std_ceil, "num", 1, {"num"}},
    {"read_lines", std_read_lines, "arr", 1, {"str"}},
    {"write_file", std_write_file, "num", 3, {"str", "str", "str"}},
    {"read_bytes", std_read_bytes, "arr", 2, {"str", "num"}},
    {"write_bytes", std_write_bytes, "num", 2, {"str", "arr"}},
    {"list", std_list, "arr", 1, {"num"}},
    {"remove", std_remove, "void", 2, {"any", "any"}},
    {"add", std_add, "arr", 3, {"arr", "num", "any"}},
    {"split", std_split, "arr", 2, {"str", "str"}},
    {"where", std_where, "num", 2, {"any", "any"}},
    {"range", std_range, "arr", 3, {"num", "num", "num"}},
    {"for_list", std_for_list, "arr", 2, {"str", "arr"}},
    {"seed", std_seed, "void", 1, {"num"}},
    {"rand", std_rand, "num", 0, {NULL}},
    {"rand_normal", std_rand_normal, "num", 0, {NULL}},
    {"mix", std_mix, "num", 3, {"num", "num", "num"}},
    {"min", std_min, "num", 2, {"num", "num"}},
    {"max", std_max, "num", 2, {"num", "num"}},
    {"distance", std_dist, "num", 4, {"num", "num", "num", "num"}},
    {"min_list", std_list_min, "num", 1, {"arr"}},
    {"max_list", std_list_max, "num", 1, {"arr"}},
    {"noise", std_noise, "num", 3, {"num", "num", "num"}},
    {"list_dir", std_list_dir, "arr", 2, {"str", "str"}},
    {"system", std_system, "arr", 1, {"str"}},
    {"system_thread", std_system_thread, "num", 2, {"str", "str"}},
    {"get_job", std_get_job, "any", 1, {"str"}},
    {"create_worker", std_create_worker, "num", 1, {"str"}},
    {"dock_worker", std_dock_worker, "void", 1, {"num"}},
    {"check_worker", std_check_worker, "num", 1, {"num"}},
    {"bus_set", std_bus_set, "num", 2, {"str", "any"}},
    {"bus_get", std_bus_get, "any", 1, {"str"}},
    {"cget", std_cget, "str", 1, {"num"}},
    {"kbhit", std_kbhit, "num", 0, {NULL}},
    {"get_keys", std_get_keys, "str", 0, {NULL}},
    {"copy", std_copy, "any", 1, {"any"}},
    {"web_status", std_web_status, "any", 1, {"num"}}, // 1 arg: wait (bool)
    {"web_init", std_canvas_init, "void", 0, {NULL}},
    {"web_text", std_canvas_text, "void", 4, {"str", "num", "num", "num"}},
    {"web_clr", std_canvas_clear, "void", 0, {NULL}},
    {"web_rect", std_web_rect, "any", 5, {"num", "num", "num", "num", "str"}},
    {"web_circle", std_web_circle, "any", 4, {"num", "num", "num", "str"}},
    {"web_line",
     std_web_line,
     "any",
     6,
     {"num", "num", "num", "num", "num", "str"}},
    {"web_triangle",
     std_web_triangle,
     "any",
     7,
     {"num", "num", "num", "num", "num", "num", "str"}},
    {"call", std_call, "any", 2, {"str", "any"}},
    {"filter", std_filter, "arr", 2, {"arr", "str"}},
    {"param_filter", std_param_filter, "arr", 3, {"arr", "str", "any"}},
    {NULL, NULL, NULL, 0, {NULL}}};

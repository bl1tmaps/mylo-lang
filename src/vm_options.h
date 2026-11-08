#ifndef MYLO_VM_OPTIONS_H
#define MYLO_VM_OPTIONS_H

// Runtime limits for the Mylo VM, set from a mylovm.conf file or the command line.
//
// A mylovm.conf holds one `key = value` per line; `#` starts a comment. Sizes accept a
// k or m suffix (x1024, x1024*1024), as in `stack_size = 64k`. See docs/mylovm_conf.md.
//
// The mylo command looks for a config in this order, using the first it finds:
//   1. --config <file>
//   2. the MYLO_VM_CONF environment variable
//   3. mylovm.conf beside the script being run
//   4. mylovm.conf beside the mylo executable
// Then each -X key=value on the command line overrides a single setting.

#include <stdbool.h>
#include <stddef.h>

#define MYLO_VM_CONF_NAME "mylovm.conf"
#define MYLO_VM_MAX_CONF_PATHS 16
#define MYLO_VM_PATH_LENGTH 1024

typedef struct {
    int stack_size;          // Value slots on the VM stack (locals, arguments, temporaries).
    int max_strings;         // Distinct strings the program can create; strings are never freed.
    int max_string_length;   // Longest string, in bytes. Longer strings are cut short.
    int max_code;            // Bytecode words a program can compile to.
    int max_constants;       // Distinct numeric constants in a program.
    int heap_initial;        // Slots each memory arena starts with.
    int heap_max;            // Slots one arena may grow to. 0 means up to the address limit.
    bool unbuffered_output;  // Write program output straight away instead of buffering it.
    int module_path_count;
    char module_paths[MYLO_VM_MAX_CONF_PATHS][MYLO_VM_PATH_LENGTH];  // Extra import search paths.
} MyloVMOptions;

// The options new VMs are created with.
extern MyloVMOptions MyloVMOpts;

typedef enum {
    MYLO_OPT_INT,
    MYLO_OPT_BOOL,
    MYLO_OPT_PATHS
} MyloOptionKind;

// Describes one setting, so tools can list, validate and edit them without knowing each one.
typedef struct {
    const char* key;
    MyloOptionKind kind;
    size_t offset;           // Offset of the field in MyloVMOptions.
    long long default_value;
    long long min_value;
    long long max_value;
    const char* unit;        // "slots", "bytes", ... for display.
    const char* summary;     // One line, used as the comment in a written mylovm.conf.
    const char* description; // Longer help text.
} MyloOptionInfo;

const MyloOptionInfo* mylo_vm_option_table(int* out_count);
const MyloOptionInfo* mylo_vm_option_find(const char* key);

void mylo_vm_options_defaults(MyloVMOptions* opts);

// Each returns true on success, or false with a message in err.
bool mylo_vm_options_set(MyloVMOptions* opts, const char* key, const char* value, char* err, size_t err_len);
// Applies "key=value", as given to -X.
bool mylo_vm_options_set_pair(MyloVMOptions* opts, const char* pair, char* err, size_t err_len);
// Applies every setting in a file. Settings the file leaves out keep their current values.
bool mylo_vm_options_load(MyloVMOptions* opts, const char* path, char* err, size_t err_len);
bool mylo_vm_options_save(const MyloVMOptions* opts, const char* path, char* err, size_t err_len);

// Formats one setting's current value as it would appear in a config file.
void mylo_vm_options_format(const MyloVMOptions* opts, const MyloOptionInfo* info, char* out, size_t out_len);
long long mylo_vm_options_get_int(const MyloVMOptions* opts, const MyloOptionInfo* info);

// Finds the config to use, following steps 2-4 above. Either directory may be NULL.
// Returns true and writes the path to out if one exists.
bool mylo_vm_options_find(char* out, size_t out_len, const char* script_dir, const char* exe_dir);

#endif

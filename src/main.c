#include "version.h"
#define VERSION_INFO MYLO_VERSION_FULL
#define MAX_INPUT 4096

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "vm.h"
#include "utils.h"
#include "debug_adapter.h"
#include "compiler.h"
#include "vm_options.h"
// Defined in compiler.c
// Note: Signatures updated to take VM*
void parse(VM* vm, char* source);
void compile_to_c_source(VM* vm, const char* output_filename);
void generate_binding_c_source(VM* vm, const char* output_filename);
void compiler_reset();
void disassemble(VM* vm);
extern void enter_debugger(VM* vm);

// extern the counters from compiler.c
extern int ffi_count;
extern int bound_ffi_count;

void disassemble(VM* vm) {
    printf("\n--- Disassembly ---\n");
    int i = 0;
    while (i < vm->code_size) {
        int op = vm->bytecode[i];

        // Updated range check to include OP_CAST_ARR
        if (op < 0 || op > OP_CAST_ARR) {
            printf("%04d UNKNOWN %d\n", i, op);
            i++;
            continue;
        }

        printf("%04d %-10s ", i, OP_NAMES[op]);
        i++;

        switch (op) {
            case OP_JMP:
            case OP_JZ:
            case OP_JNZ: {
                int addr = vm->bytecode[i++];
                printf("-> %04d", addr);
                break;
            }
            case OP_SET:
            case OP_GET: {
                int idx = vm->bytecode[i++];
                printf("G[%d]", idx);
                break;
            }
            case OP_LVAR:
            case OP_SVAR: {
                int off = vm->bytecode[i++];
                printf("FP[%d]", off);
                break;
            }
            case OP_CALL: {
                int target = vm->bytecode[i++];
                int args = vm->bytecode[i++];
                printf("-> %04d (%d args)", target, args);
                break;
            }
            case OP_ALLOC: {
                int size = vm->bytecode[i++];
                int type_id = vm->bytecode[i++]; // Consumes 2nd arg
                printf("(fields: %d, type_id: %d)", size, type_id);
                break;
            }
            case OP_HSET:
            case OP_HGET: {
                int off = vm->bytecode[i++];
                int type_id = vm->bytecode[i++]; // Consumes 2nd arg
                printf("(offset: %d, type_id: %d)", off, type_id);
                break;
            }
            case OP_NATIVE: {
                int id = vm->bytecode[i++];
                printf("Native[%d]", id);
                break;
            }
            case OP_ARR: {
                int count = vm->bytecode[i++];
                printf("(count: %d)", count);
                break;
            }
            case OP_EMBED: {
                int len = vm->bytecode[i++];
                printf("(len: %d) <embedded data>", len);
                // Important: Skip the raw data bytes so we don't interpret them as opcodes
                i += len;
                break;
            }
            case OP_PSH_STR: {
                int idx = vm->bytecode[i++];
                printf("[%d] (\"%s\")", idx, vm->string_pool[idx]);
                break;
            }
            case OP_PSH_ENUM: {
                int idx = vm->bytecode[i++];
                unsigned int packed = (unsigned int)vm->constants[idx];
                printf("[%d] (Enum %d -> \"%s\")", idx, packed & 0xFFFF, vm->string_pool[(packed >> 16) & 0xFFFF]);
                break;
            }
            case OP_CAST_ARR:
            case OP_CAST:
            case OP_CHECK_TYPE: {
                int target = vm->bytecode[i++];
                printf("(target_type: %d)", target);
                break;
            }
            case OP_MAKE_ARR: {
                int count = vm->bytecode[i++];
                int type_id = vm->bytecode[i++];
                printf("(count: %d, type_id: %d)", count, type_id);
                break;
            }
            default: break;
        }
        printf("\n");
    }
    printf("-------------------\n\n");
}

void print_greeting() {
    setTerminalColor(MyloFgMagenta, MyloBgColorDefault);
    printf("Mylo REPL v%s\n", VERSION_INFO);
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("Type 'exit' to quit.\n");
}

void start_repl() {
    char buffer[MAX_INPUT];
    char line_buf[MAX_INPUT];

    // Setup
    print_greeting();

    // Instantiate VM
    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm_init(&vm);
    compiler_reset();

    // 1. Setup Error Recovery Environment
    jmp_buf repl_env;
    MyloConfig.repl_jmp_buf = &repl_env;

    // State for multiline
    buffer[0] = '\0';
    int open_braces = 0;

    while (true) {

        // 2. ERROR LANDING PAD
        // setjmp returns 0 normally. It returns 1 if an error occurred and we jumped back.
        if (setjmp(repl_env) != 0) {
            // We just crashed! Reset state to survive.
            vm.sp = -1;       // Clear Stack
            open_braces = 0;  // Reset multiline parsing
            buffer[0] = '\0'; // Clear input buffer

            // Note: We do NOT call vm_init() again because we want to keep
            // functions/variables defined in previous successful lines.
        }

        // Change prompt based on depth
        if (open_braces > 0) {
            setTerminalColor(MyloFgBlue, MyloBgColorDefault);
            printf("... ");
            setTerminalColor(MyloFgDefault, MyloBgColorDefault);
        }
        else {
            setTerminalColor(MyloFgYellow, MyloBgColorDefault);
            printf("> ");
            setTerminalColor(MyloFgDefault, MyloBgColorDefault);
        }

        if (!fgets(line_buf, sizeof(line_buf), stdin)) break;

        // Handle Exit
        if (strncmp(line_buf, "exit", 4) == 0) break;

        // 1. Analyze Braces to detect Multiline
        for (int i = 0; line_buf[i]; i++) {
            if (line_buf[i] == '{') open_braces++;
            if (line_buf[i] == '}') open_braces--;
        }

        // 2. Append line to main buffer
        strcat(buffer, line_buf);

        // 3. If braces are balanced, Compile & Run
        if (open_braces <= 0) {
            open_braces = 0; // Reset safety

            // Skip empty lines
            if (strlen(buffer) == 0 || buffer[0] == '\n') {
                buffer[0] = '\0';
                continue;
            }

            int start_ip = 0;
            compile_repl(&vm, buffer, &start_ip);

            int stack_start = vm.sp;
            run_vm_from(&vm, start_ip, false);

            if (vm.sp > stack_start) {
                double val = vm.stack[vm.sp];
                int type = vm.stack_types[vm.sp];

                // Simple printer
                setTerminalColor(MyloFgCyan, MyloBgColorDefault);
                if (type == T_NUM) {
                    printf("%g\n", val);
                }
                else if (type == T_STR) {
                    printf("\"%s\"\n", vm.string_pool[(int)val]);
                }
                else if (type == T_OBJ) {
                    printf("[Object]\n");
                }
                setTerminalColor(MyloFgDefault, MyloBgColorDefault);
                vm.sp = stack_start;
            }

            // Clear buffer for next command
            buffer[0] = '\0';
        }
        // Else: Loop again to get next line (buffer is preserved)
    }

    vm_cleanup(&vm);
}

// Start - Help formatting
#define SET_COLOUR(fg, bg) setTerminalColor(fg, bg)
#define FG_CYAN MyloFgCyan
#define FG_GRAY MyloFgWhite
#define FG_DEFAULT MyloFgDefault
#define FG_YELLOW MyloFgYellow
#define BG_DEFAULT MyloBgColorDefault
#define FG_GREEN MyloFgGreen
#define FG_PURPLE MyloFgMagenta

// Macro for consistent formatting
#define PRINT_ARG(flag, desc) \
SET_COLOUR(FG_GREEN, BG_DEFAULT); \
printf("  %-15s", flag); \
SET_COLOUR(FG_DEFAULT, BG_DEFAULT); \
printf(" %s\n", desc);

void print_help() {
    printf("\n");

    // Header
    SET_COLOUR(FG_CYAN, BG_DEFAULT);
    printf("Mylo Language v%s\n", VERSION_INFO);

    SET_COLOUR(FG_GRAY, BG_DEFAULT);
    printf("A lightweight, embeddable language.\n\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);

    // Usage
    SET_COLOUR(FG_YELLOW, BG_DEFAULT);
    printf("USAGE:\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("mylo [flags] [file]\n\n");

    // Flags
    SET_COLOUR(FG_YELLOW, BG_DEFAULT);
    printf("FLAGS:\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);

    PRINT_ARG("--repl",       "Start the interactive Read-Eval-Print Loop.");
    PRINT_ARG("--version",    "Display the current version information.");
    PRINT_ARG("--help",       "Show this help message.");
    PRINT_ARG("--dap",        "Enable DAP debugging interface (see VSCode Extension).");
    PRINT_ARG("--db",         "Debug mode, load the code and jump into an interactive debugger.");
    PRINT_ARG("--trace",      "Run as normal but print every VM state change.");
    PRINT_ARG("--build",      "Build and generate a .c bootstrapping source file.");
    PRINT_ARG("--bind",       "Generate a .c source file binding for later interpreted or compiled dynamic linking.");
    PRINT_ARG("--bundle",     "Compile Mylo application and output mylo_exe (bundle bytecode and VM interpreter).");
    PRINT_ARG("--dump",       "Dump the generated bytecode instructions.");
    PRINT_ARG("--config FILE", "Read VM settings from FILE instead of looking for mylovm.conf.");
    PRINT_ARG("-X key=value", "Override one VM setting, e.g. -X stack_size=64k. Can be repeated.");
    PRINT_ARG("--print-config", "Show the VM settings that would be used, and where they came from.");
    PRINT_ARG("--write-config", "Write the current VM settings to mylovm.conf (or the file given next).");
    PRINT_ARG("--help-config", "Describe every VM setting.");
    PRINT_ARG("--unbuffered", "Write output immediately (same as -X unbuffered_output=true).");
    // Examples
    printf("\n");
    SET_COLOUR(FG_YELLOW, BG_DEFAULT);
    printf("EXAMPLES:\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);

    SET_COLOUR(FG_GRAY, BG_DEFAULT);
    printf("  Run a script:\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("    ./mylo script.mylo\n\n");

    SET_COLOUR(FG_GRAY, BG_DEFAULT);
    printf("  Start REPL with debug mode:\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("    ./mylo --repl\n\n");

    SET_COLOUR(FG_GRAY, BG_DEFAULT);
    printf("  Build a native application (with or without inline C Code):\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("    ./mylo --build my_file.mylo                               "); SET_COLOUR(FG_GRAY, BG_DEFAULT); printf("<-- Produced out.c \n "); SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("    cc out.c src/vm.c src/mylolib.c -o mylo_app -Isrc -lm    "); SET_COLOUR(FG_GRAY, BG_DEFAULT); printf("<-- Uses GNU / C Compiler to build native Executable \n "); SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("   ./mylo_app                                                "); SET_COLOUR(FG_GRAY, BG_DEFAULT); printf("<-- Runs like any other executable, with bundled VM. \n\n "); SET_COLOUR(FG_DEFAULT, BG_DEFAULT);


    SET_COLOUR(FG_GRAY, BG_DEFAULT);
    printf(" Dynamically or Statically Use C Code:\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("    ./mylo --bind test_lib.mylo                               "); SET_COLOUR(FG_GRAY, BG_DEFAULT); printf("   <-- Contains C blocks, this outputs test_lib.mylo_bind.c \n "); SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("   cc -shared -fPIC test_lib.mylo_bind.c -Isrc -o test_lib.so "); SET_COLOUR(FG_GRAY, BG_DEFAULT); printf("  <-- Produced a shared object which can be dynamically imported \n "); SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    SET_COLOUR(FG_GRAY, BG_DEFAULT);
    printf(" Then inside your mylo applications:\n");

    SET_COLOUR(FG_PURPLE, BG_DEFAULT);
    printf("  ```\n");
    SET_COLOUR(FG_YELLOW, BG_DEFAULT); printf("    import ");
    SET_COLOUR(FG_PURPLE, BG_DEFAULT); printf("native ");
    SET_COLOUR(FG_GREEN, BG_DEFAULT); printf("test_lib.mylo     ");
    SET_COLOUR(FG_GRAY, BG_DEFAULT); printf("   <-- Mylo will look for test_lib.so and load compiled C functions \n "); SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    printf("   print(my_cool_c_func())");  SET_COLOUR(FG_GRAY, BG_DEFAULT); printf("            <-- C functions from shared objects can be run in the interpreter, or compiled. \n "); SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
    SET_COLOUR(FG_PURPLE, BG_DEFAULT);
    printf(" ```\n\n");
    // Reset at end just in case
    SET_COLOUR(FG_GRAY, BG_DEFAULT);
    printf(" More examples, documentation and guides at "); SET_COLOUR(FG_CYAN, BG_DEFAULT); printf("https://github.com/blitmaps/mylo-lang/tree/main/docs\n");
    SET_COLOUR(FG_DEFAULT, BG_DEFAULT);

}
#undef PRINT_ARG

static void print_config_help(void) {
    int count = 0;
    const MyloOptionInfo* table = mylo_vm_option_table(&count);
    MyloVMOptions defaults;
    mylo_vm_options_defaults(&defaults);

    printf("VM settings, for mylovm.conf or -X key=value\n\n");
    printf("mylo uses the first config it finds: --config FILE, then the MYLO_VM_CONF environment\n");
    printf("variable, then %s beside the script, then %s beside mylo itself.\n\n", MYLO_VM_CONF_NAME, MYLO_VM_CONF_NAME);
    for (int i = 0; i < count; i++) {
        char def[64];
        mylo_vm_options_format(&defaults, &table[i], def, sizeof(def));
        SET_COLOUR(FG_GREEN, BG_DEFAULT);
        printf("  %s", table[i].key);
        SET_COLOUR(FG_DEFAULT, BG_DEFAULT);
        if (table[i].kind != MYLO_OPT_PATHS) printf("  (default %s%s%s)", def, *table[i].unit ? " " : "", table[i].unit);
        printf("\n    %s\n    %s\n\n", table[i].summary, table[i].description);
    }
}

static void print_config(const MyloVMOptions* opts, const char* source) {
    int count = 0;
    const MyloOptionInfo* table = mylo_vm_option_table(&count);
    printf("# Settings from: %s\n", source && *source ? source : "built-in defaults");
    for (int i = 0; i < count; i++) {
        char value[MYLO_VM_PATH_LENGTH * 4];
        mylo_vm_options_format(opts, &table[i], value, sizeof(value));
        printf("%s = %s\n", table[i].key, value);
    }
}


int main(int argc, char** argv) {
    get_executable_dir(mylo_exe_dir, sizeof(mylo_exe_dir), argv[0]);

    // A bundled program takes its settings from beside the executable.
    char config_path[MYLO_VM_PATH_LENGTH * 2] = "";
    char config_error[512];
    if (mylo_vm_options_find(config_path, sizeof(config_path), NULL, mylo_exe_dir) &&
        !mylo_vm_options_load(&MyloVMOpts, config_path, config_error, sizeof(config_error))) {
        fprintf(stderr, "Warning: ignoring %s: %s\n", config_path, config_error);
        mylo_vm_options_defaults(&MyloVMOpts);
    }

    // Instantiate Main VM
    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm_init(&vm);
    compiler_reset();

    // 1. Check if WE are the program
    if (load_self_contained(&vm, argv[0])) {
        if (MyloVMOpts.unbuffered_output) setvbuf(stdout, NULL, _IONBF, 0);
        run_vm(&vm, false);
        vm_cleanup(&vm);
        return 0;
    }
    // The settings may change below, so the VM is created again once they're known.
    vm_cleanup(&vm);

    if (argc < 2) {
        print_help();
        return 1;
    }

    bool build_mode = false;
    bool bind_mode = false;
    bool dump = false;
    bool trace = false;
    bool debug_mode = false;
    bool version = false;
    bool repl_mode = false;
    bool cli_debug_mode = false; // Capture flag locally
    bool bundle_mode = false;
    bool print_config_mode = false;
    bool write_config_mode = false;
    const char* write_config_path = MYLO_VM_CONF_NAME;
    const char* explicit_config = NULL;
    const char* overrides[64];
    int override_count = 0;

    char* fn = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: --config needs a file name\n"); return 2; }
            explicit_config = argv[++i];
        }
        else if (strcmp(argv[i], "-X") == 0 || strncmp(argv[i], "-X", 2) == 0) {
            // Accepts both "-X key=value" and "-Xkey=value".
            const char* pair = argv[i][2] ? argv[i] + 2 : (i + 1 < argc ? argv[++i] : NULL);
            if (!pair) { fprintf(stderr, "Error: -X needs key=value\n"); return 2; }
            if (override_count < 64) overrides[override_count++] = pair;
        }
        else if (strcmp(argv[i], "--unbuffered") == 0) {
            if (override_count < 64) overrides[override_count++] = "unbuffered_output=true";
        }
        else if (strcmp(argv[i], "--print-config") == 0) print_config_mode = true;
        else if (strcmp(argv[i], "--write-config") == 0) {
            write_config_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') write_config_path = argv[++i];
        }
        else if (strcmp(argv[i], "--help-config") == 0) {
            print_config_help();
            return 0;
        }
        else if (strcmp(argv[i], "--build") == 0) build_mode = true;
        else if (strcmp(argv[i], "--bind") == 0) bind_mode = true;
        else if (strcmp(argv[i], "--bundle") == 0) bundle_mode = true;
        else if (strcmp(argv[i], "--run") == 0) build_mode = false;
        else if (strcmp(argv[i], "--dump") == 0) dump = true;
        else if (strcmp(argv[i], "--trace") == 0) trace = true;
        else if (strcmp(argv[i], "--dap") == 0) { debug_mode = true; MyloConfig.debug_mode = true; }
        else if (strcmp(argv[i], "--db") == 0) cli_debug_mode = true;
        else if (strcmp(argv[i], "--version") == 0) version = true;
        else if (strcmp(argv[i], "--repl") == 0) repl_mode = true;
        else if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
        else fn = argv[i];
    }

    if (version) {
        printf("Mylo version %s\n", VERSION_INFO);
        return 0;
    }

    // --- VM settings: the first config found, then each -X override in order ---
    char script_dir[1024] = "";
    if (fn) get_executable_dir(script_dir, sizeof(script_dir), fn);
    mylo_vm_options_defaults(&MyloVMOpts);
    config_path[0] = '\0';
    if (explicit_config) {
        snprintf(config_path, sizeof(config_path), "%s", explicit_config);
    } else {
        mylo_vm_options_find(config_path, sizeof(config_path), fn ? script_dir : NULL, mylo_exe_dir);
    }
    if (config_path[0] && !mylo_vm_options_load(&MyloVMOpts, config_path, config_error, sizeof(config_error))) {
        fprintf(stderr, "Error in %s: %s\n", config_path, config_error);
        return 2;
    }
    for (int i = 0; i < override_count; i++) {
        if (!mylo_vm_options_set_pair(&MyloVMOpts, overrides[i], config_error, sizeof(config_error))) {
            fprintf(stderr, "Error in -X %s: %s\n", overrides[i], config_error);
            return 2;
        }
    }
    if (MyloVMOpts.unbuffered_output) {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

    if (print_config_mode) {
        print_config(&MyloVMOpts, config_path);
        if (override_count > 0) printf("# with %d -X override(s)\n", override_count);
        return 0;
    }
    if (write_config_mode) {
        if (!mylo_vm_options_save(&MyloVMOpts, write_config_path, config_error, sizeof(config_error))) {
            fprintf(stderr, "Error: %s\n", config_error);
            return 2;
        }
        printf("Wrote %s\n", write_config_path);
        return 0;
    }

    memset(&vm, 0, sizeof(vm));
    vm_init(&vm);
    compiler_reset();

    if (repl_mode) {
        get_executable_dir(mylo_exe_dir, sizeof(mylo_exe_dir), argv[0]);        // start_repl manages its own VM cycle usually, but since we have one, we could pass it or just cleanup and call start_repl
        start_repl();
        return 0;
    }

    MyloConfig.build_mode = build_mode;
    if(!fn) {
        printf("No input file provided.\n");
        return 1;
    }

    char* content = read_file(fn);
    if (!content) {
        fprintf(stderr, "Error: Cannot read file %s\n", fn);
        return 1;
    }

    get_executable_dir(current_file_dir, sizeof(current_file_dir), fn);
    // Apply TUI Debugger settings to VM instance
    vm.source_code = content;
    vm.cli_debug_mode = cli_debug_mode;
    compiler_set_main_file(&vm, fn);

    // The debug adapter compiles the program itself, so it can report errors to the debugger.
    if (debug_mode) {
        start_debug_adapter(&vm, fn, content);
        free(content);
        vm_cleanup(&vm);
        return 0;
    }

    // Parse source into the VM
    parse(&vm, content);

    if (bind_mode) {
        char out_name[1024];
        snprintf(out_name, 1024, "%s_bind.c", fn);
        generate_binding_c_source(&vm, out_name);
        free(content);
        vm_cleanup(&vm);
        return 0;
    }

    if (build_mode) {
        compile_to_c_source(&vm, "out.c");
        free(content);
        vm_cleanup(&vm);
        return 0;
    }

    // New Hook for the build command:
    if (bundle_mode) {
        create_standalone_executable(&vm, "mylo_exe", argv[0]);
        return 0;
    }

    // --- INTERPRETER SAFETY CHECK ---
    if ((ffi_count - bound_ffi_count) > 0) {
        for (int i = 0; i < ffi_count; i++) {
            if (ffi_blocks[i].is_cfn && !ffi_blocks[i].is_bound) {
                setTerminalColor(MyloFgRed, MyloBgColorDefault);
                printf("error: native function %s was not found, either build mylo with these libraries, or provide shared libraries\n", ffi_blocks[i].c_symbol_name);
            }
        }
        setTerminalColor(MyloFgMagenta, MyloBgColorDefault);
        printf("Error: This program contains Native C blocks and no shared objects are found, so it cannot be interpreted.\n");
        setTerminalColor(MyloFgCyan, MyloBgColorDefault);
        printf("Please compile it to bytecode using: mylo --build %s\n", fn);
        setTerminalColor(MyloFgDefault, MyloBgColorDefault);
        free(content);
        vm_cleanup(&vm);
        return 1;
    }

    if (dump) disassemble(&vm);

    // Start debugger immediately if flag is set
    if (vm.cli_debug_mode) {
        enter_debugger(&vm);
    }

    run_vm(&vm, trace);

    free(content);
    vm_cleanup(&vm);
    return 0;
}

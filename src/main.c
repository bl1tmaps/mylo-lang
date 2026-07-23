#define VERSION_INFO "0.3.6"
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

        // Updated range check to include OP_EMBED
        if (op < 0 || op > OP_DEBUGGER) {
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


int main(int argc, char** argv) {
    // Instantiate Main VM
    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm_init(&vm);
    compiler_reset();

    // 1. Check if WE are the program
    if (load_self_contained(&vm, argv[0])) {
        run_vm(&vm, false);
        vm_cleanup(&vm);
        return 0;
    }
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

    char* fn = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--build") == 0) build_mode = true;
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

    if (repl_mode) {
        // start_repl manages its own VM cycle usually, but since we have one, we could pass it or just cleanup and call start_repl
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


    // Apply TUI Debugger settings to VM instance
    vm.source_code = content;
    vm.cli_debug_mode = cli_debug_mode;

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

    // 1. If Debugging, Hand off control immediately (Do NOT parse yet)
    // The Debug Adapter manages its own parsing/execution loop via stdin/out commands
    if (debug_mode) {
        start_debug_adapter(&vm, fn, content);
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

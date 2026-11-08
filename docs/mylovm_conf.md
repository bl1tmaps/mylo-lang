# VM settings: mylovm.conf

The Mylo VM's limits, such as the stack size and how many strings a program can create, are set
when the VM starts. Most programs never need to change them. When one does, put the new values in a
`mylovm.conf` file, much like the options file for a Java VM.

```ini
# mylovm.conf beside my_game.mylo
stack_size = 64k        # deep recursion
max_strings = 100k      # a long-running program that builds many strings
heap_max = 64m          # stop runaway memory use at 64M slots per arena
module_path = ../shared # look here for imports too
```

Mylo IDE edits these files in its **VM Options** panel, so you don't need to remember the names.

## Where mylo looks

`mylo` uses the first config it finds:

1. `--config FILE` on the command line
2. the file named by the `MYLO_VM_CONF` environment variable
3. `mylovm.conf` beside the script being run
4. `mylovm.conf` beside the `mylo` executable, for settings shared by every script

Then each `-X key=value` on the command line overrides one setting:

```bash
mylo -X stack_size=64k -X max_strings=50k my_program.mylo
```

A program built with `--bundle` reads `mylovm.conf` beside its own executable.

To see what a script will run with, and where it came from:

```bash
mylo --print-config my_program.mylo
```

To start a config from the current settings, with a comment on each one:

```bash
mylo --write-config            # writes ./mylovm.conf
mylo --write-config other.conf
```

`mylo --help-config` describes every setting.

## Syntax

- One `key = value` per line. `#` starts a comment, and blank lines are ignored.
- Sizes accept a `k` or `m` suffix, meaning ×1024 and ×1024×1024: `64k` is 65,536.
- True/false settings accept `true`, `false`, `yes`, `no`, `on`, `off`, `1` or `0`.
- A setting left out keeps its default.
- An unknown key or an out-of-range value is an error, so a typo doesn't go unnoticed. mylo
  stops with the line number rather than run with settings you didn't intend.

## Settings

| Key | Default | Range | What it limits |
|---|---|---|---|
| `stack_size` | 2k | 64 to 64m | Value slots on the VM stack. Each local variable, argument and temporary takes one, and each call takes two more. Raise it if deep recursion stops with a stack overflow. |
| `max_strings` | 10000 | 256 to 16m | Distinct strings a program can create. Every distinct string, including each result of joining strings, takes an entry and is never freed. |
| `max_string_length` | 1k | 64 to 16m | The longest string, in bytes. Strings built at runtime are cut short at this length. Literals in source code are limited separately by the compiler, to 1,023 bytes. |
| `max_code` | 5368709 | 4096 to 256m | Bytecode words a program can compile to. Each word also stores its line and file, so it costs 12 bytes. |
| `max_constants` | 1k | 64 to 16m | Distinct number literals in a program. |
| `heap_initial` | 1m | 1024 to 256m | Slots each memory arena starts with. Arenas hold arrays, structs, maps and bytes, and grow as needed. Each slot costs 12 bytes. |
| `heap_max` | 0 | 0 to 256m | The most slots one arena may grow to. `0` means no limit beyond the VM's address limit of 268,435,456 slots. A limit turns a runaway program into an out of memory error instead of using all of the computer's memory. |
| `unbuffered_output` | false | | Writes output straight away instead of buffering it. Useful when another program, such as an IDE, shows output as it arrives. `--unbuffered` sets it too. |
| `module_path` | | up to 16 | Extra folders to search for imports, after the script's own folder. Separate several with `;`, or repeat the key. Relative paths are relative to the config file. |

## When a limit is reached

The error names the setting to raise:

```
[Line 3] Runtime Error: Stack overflow (stack_size = 2048). Raise it in mylovm.conf or with -X stack_size=<value>.
```

Memory is only used as a program needs it. Strings are stored at their own length, so raising
`max_strings` or `max_string_length` costs little until the program uses them. The stack and the
bytecode buffer are reserved when the VM starts.

## For embedders

The settings live in `src/vm_options.h`. `MyloVMOpts` holds the values new VMs are created with;
change it before `vm_init()`. Each VM copies the limits into its own fields, such as
`vm->stack_capacity` and `vm->max_strings`. `mylo_vm_option_table()` describes every setting,
which is how Mylo IDE builds its panel.

The string pool is now an array of pointers, one per string, rather than a fixed 2D array.
`vm->string_pool[id]` reads the same as before, but plugins compiled against an older `vm.h`
must be rebuilt (`mylo --bind`, then compile again).

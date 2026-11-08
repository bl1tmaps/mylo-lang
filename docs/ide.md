# Mylo IDE

A lightweight editor and debugger for Mylo: one window with the script editor, a run console,
breakpoints, variables, a VM memory viewer and the VM settings.

```bash
mylo-ide                 # start with an empty script
mylo-ide game.mylo       # open scripts
```

It runs scripts with the `mylo` beside it. If there isn't one, it uses `mylo` on PATH. You can
set another in **File > Settings**.

## Panels

| Panel | What it shows |
|---|---|
| **Editor** | Tabs for open scripts, with syntax colours, completions with signatures and docs, and errors marked as you type. Click beside a line number to set a breakpoint. |
| **Console** | Program output; errors are shown in red. Click a line naming `file:line` to open it. When a script runs without the debugger, type in the box at the bottom to send it input (read with `input("prompt")`). |
| **Variables** | Locals and globals of the selected call frame, while paused. Arrays, structs and maps expand. |
| **Watch** | Expressions to check each time the program pauses: `name`, `name.field`, `name[2]`, and chains such as `points[0].x`. |
| **Call Stack** | The calls that led here. Pick a frame to see its variables and line. |
| **Breakpoints** | Every breakpoint in every script; click one to go to it. |
| **Memory** | The VM while paused (see below). |
| **VM Options** | The `mylovm.conf` settings for the script's folder, or for every script (beside mylo). See [mylovm_conf.md](mylovm_conf.md). |

Panels can be dragged and docked anywhere; **View > Reset Layout** puts them back.

### The memory viewer

- **Overview**: how much of each limit is in use (stack, strings, constants, bytecode) and each memory arena's size. A bar turns amber, then red, as it nears its limit; hover it to see which setting to raise.
- **Stack**: the value stack, newest slot first. Each call's frame is marked where it starts: its return address, then the caller's frame pointer.
- **Heap**: the raw slots of an arena, 256 at a time, with each slot's type tag and bits. Slots past the arena's head are free.
- **Strings**: the string pool. Strings are never freed, so a program that builds strings in a loop shows up here.
- **Bytecode**: the instructions around the current one, with their source lines.

## Running and debugging

| Key | Action |
|---|---|
| F5 | Start debugging, or continue |
| Ctrl+F5 | Run without the debugger |
| Shift+F5 | Stop |
| Ctrl+Shift+F5 | Restart |
| F6 | Pause |
| F9 | Toggle a breakpoint on the cursor's line |
| F10 / F11 / Shift+F11 | Step over / into / out |
| Ctrl+= / Ctrl+- | Larger or smaller text |

Scripts are saved before they run (switch this off in Settings). A runtime error stops the
debugger on the failing line, so you can look at the variables and stack before the program ends.

## How it works

The IDE runs each script in its own `mylo` process, so a crash or an endless loop can't take the
editor with it.

- **Run** starts `mylo --unbuffered script.mylo` and connects the console to its stdin, stdout and stderr.
- **Debug** starts `mylo --dap script.mylo` and talks to it over the
  [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/), as VS Code does.
  The memory viewer uses a few extra requests the adapter answers: `myloVmState`, `myloStack`,
  `myloArena`, `myloStrings` and `myloDisassemble`. They're described at the top of
  `src/debug_adapter.c`.

Both pass `--config` with the same `mylovm.conf` mylo would find on its own, and the console says
which one is used.

Completions and error markers come from compiling the script in the IDE itself, with native
libraries left unloaded.

Settings, recent files, breakpoints and the panel layout are kept in `%APPDATA%\MyloIDE` on
Windows, `~/Library/Application Support/MyloIDE` on macOS, and `~/.config/mylo-ide` on Linux.
Set `MYLO_IDE_CONFIG` to use another folder.

## Building

The IDE builds with the rest of Mylo, and CMake downloads raylib 5.5 for it:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`-DMYLO_RAYLIB_DIR=path/to/raylib` uses a local copy instead, and `-DMYLO_BUILD_IDE=OFF` leaves the
IDE out. Dear ImGui is included in `ide/third_party/imgui`.

## Testing

`ctest --test-dir build -C Release` runs three suites:

- `mylo_tests`: the language tests.
- `dap_tests`: `tests/test_dap.py` drives `mylo --dap` through real debugging sessions, including breakpoints, stepping, variables, memory requests, imports, pause and errors.
- `text_editor_fuzz`: random editing sessions against the editor. After every step it checks that the text only holds bytes that were typed, that undo and redo restore exactly the recorded text, and that the cursor stays inside the document. Build it with `-DMYLO_FUZZ_SANITIZE=ON` to add AddressSanitizer. Run it after any change to `ide/src/TextEditor.*`, and add new editing features as operations in `applyRandomOperation()`.

The IDE can also run a script of commands, which CI uses to debug a script through the UI on a
virtual display:

```bash
mylo-ide --automate ide/tests/smoke.txt ide/tests/smoke.mylo
```

The commands are listed above `IdeApp::runAutomation()` in `ide/src/ide_app.cpp`. They include
`debug`, `breakpoint N`, `waitpaused`, `stepover`, `shot FILE` and `quit`. The IDE exits with
code 2 if a step times out.

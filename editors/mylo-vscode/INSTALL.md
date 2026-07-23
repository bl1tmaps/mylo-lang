# Installing the Mylo VSCode Extension

The Mylo VSCode extension provides syntax highlighting, a Language Server (autocomplete, etc.), and a Debug Adapter. The extension is built and packaged automatically via CMake, which bundles the `mylo` and `mylo-lsp` binaries directly into the extension.

## Prerequisites
- **CMake** (3.5+)
- **C Compiler** (GCC, Clang, or MSVC)
- **Node.js & npm** (required to package the VSCode extension)

## 1. Building the Extension
Navigate to the extension directory and use CMake to build and package the extension. This will compile the core language and language server binaries, place them in the extension's `bin/` folder, and run the VSCE packager.

```bash
cd editors/mylo-vscode
cmake -B build
cmake --build build --target package-vsix
```
*Note: On Windows, the build command will automatically compile the executables using your configured generator (e.g., Visual Studio).*

## 2. Installing the VSIX Package
Once the build is complete, a `.vsix` file (e.g., `mylo-debug-0.1.7.vsix`) will be generated in the `editors/mylo-vscode` folder.

You can install this directly via the command line:

```bash
code --install-extension mylo-debug-*.vsix
```

Alternatively, you can install it through the VSCode UI:
1. Open VSCode.
2. Go to the **Extensions** view (`Ctrl+Shift+X` or `Cmd+Shift+X`).
3. Click the `...` (Views and More Actions) menu at the top right of the Extensions pane.
4. Select **Install from VSIX...** and choose the generated `.vsix` file.

## 3. Verify
Restart VSCode and check your Extensions list. You should now see the **Mylo Debugger** extension installed. When you open a `.mylo` file, the Language Server will start automatically, and you can debug your scripts seamlessly!
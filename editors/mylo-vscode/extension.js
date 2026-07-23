const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

function activate(context) {
    // Determine the path to the bundled binaries
    const binDir = path.join(context.extensionPath, 'bin');
    const myloExt = process.platform === 'win32' ? '.exe' : '';
    
    // CMake on Windows typically creates Debug/Release subdirectories
    let myloPath = path.join(binDir, `mylo${myloExt}`);
    let myloLspPath = path.join(binDir, `mylo-lsp${myloExt}`);
    
    if (!fs.existsSync(myloPath) && fs.existsSync(path.join(binDir, 'Debug', `mylo${myloExt}`))) {
        myloPath = path.join(binDir, 'Debug', `mylo${myloExt}`);
        myloLspPath = path.join(binDir, 'Debug', `mylo-lsp${myloExt}`);
    } else if (!fs.existsSync(myloPath) && fs.existsSync(path.join(binDir, 'Release', `mylo${myloExt}`))) {
        myloPath = path.join(binDir, 'Release', `mylo${myloExt}`);
        myloLspPath = path.join(binDir, 'Release', `mylo-lsp${myloExt}`);
    }

    // --- Language Server Setup ---
    if (fs.existsSync(myloLspPath)) {
        const serverOptions = {
            run: { command: myloLspPath, transport: TransportKind.stdio },
            debug: { command: myloLspPath, transport: TransportKind.stdio }
        };

        const clientOptions = {
            documentSelector: [{ scheme: 'file', language: 'mylo' }],
            synchronize: {
                fileEvents: vscode.workspace.createFileSystemWatcher('**/*.mylo')
            }
        };

        client = new LanguageClient(
            'myloLanguageServer',
            'Mylo Language Server',
            serverOptions,
            clientOptions
        );

        client.start();
    } else {
        vscode.window.showWarningMessage('Mylo Language Server executable not found in bin/. LSP will be disabled.');
    }

    // --- Debug Adapter Setup ---
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('mylo', {
        createDebugAdapterDescriptor: (session, executable) => {
            if (!fs.existsSync(myloPath)) {
                vscode.window.showErrorMessage(`Mylo executable not found at: ${myloPath}`);
                return undefined;
            }

            const program = session.configuration.program;

            if (!program) {
                vscode.window.showErrorMessage("No program specified in launch.json");
                return undefined;
            }

            const cwd = session.configuration.cwd || (vscode.workspace.workspaceFolders ? vscode.workspace.workspaceFolders[0].uri.fsPath : undefined);
            const envConfig = session.configuration.env || {};
            const spawnedEnv = { ...process.env, ...envConfig };

            return new vscode.DebugAdapterExecutable(
                myloPath,
                ['--dap', program],
                {
                    env: spawnedEnv,
                    cwd: cwd
                }
            );
        }
    }));
}

function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

module.exports = { activate, deactivate };

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
            command: myloLspPath,
            args: [],
            transport: TransportKind.stdio,
            options: { env: process.env }
        };

        const clientOptions = {
            documentSelector: [{ scheme: 'file', language: 'mylo' }],
            synchronize: {
                fileEvents: vscode.workspace.createFileSystemWatcher('**/.clientrc')
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

    // --- Debug Adapter Tracker for Logging ---
    const outputChannel = vscode.window.createOutputChannel("Mylo DAP Logs");
    outputChannel.appendLine("Mylo extension activated!");
    outputChannel.show(true);

    context.subscriptions.push(vscode.debug.registerDebugAdapterTrackerFactory('mylo', {
        createDebugAdapterTracker(session) {
            outputChannel.appendLine(`\n--- Starting Debug Session for ${session.name} ---`);
            return {
                onWillReceiveMessage: m => outputChannel.appendLine(`VSCode -> Adapter: ${JSON.stringify(m)}`),
                onDidSendMessage: m => outputChannel.appendLine(`Adapter -> VSCode: ${JSON.stringify(m)}`),
                onError: e => outputChannel.appendLine(`ERROR: ${e.message}`),
                onExit: code => outputChannel.appendLine(`EXIT: Adapter exited with code ${code}`)
            };
        }
    }));

    // --- Debug Adapter Setup ---
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('mylo', {
        createDebugAdapterDescriptor: (session, executable) => {
            outputChannel.appendLine(`Preparing to launch adapter: ${myloPath}`);
            if (!fs.existsSync(myloPath)) {
                vscode.window.showErrorMessage(`Mylo executable not found at: ${myloPath}`);
                outputChannel.appendLine(`Executable not found!`);
                return undefined;
            }

            const program = session.configuration.program;
            outputChannel.appendLine(`Program from launch.json: ${program}`);

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

    // --- Init Project Command Setup ---
    let myloInitPath = path.join(binDir, `mylo-vsproject${myloExt}`);
    if (!fs.existsSync(myloInitPath) && fs.existsSync(path.join(binDir, 'Debug', `mylo-vsproject${myloExt}`))) {
        myloInitPath = path.join(binDir, 'Debug', `mylo-vsproject${myloExt}`);
    } else if (!fs.existsSync(myloInitPath) && fs.existsSync(path.join(binDir, 'Release', `mylo-vsproject${myloExt}`))) {
        myloInitPath = path.join(binDir, 'Release', `mylo-vsproject${myloExt}`);
    }

    const { execFile } = require('child_process');
    context.subscriptions.push(vscode.commands.registerCommand('mylo.initProject', async () => {
        if (!fs.existsSync(myloInitPath)) {
            vscode.window.showErrorMessage(`Mylo project initializer not found at: ${myloInitPath}`);
            return;
        }

        const workspaceFolders = vscode.workspace.workspaceFolders;
        let targetDir = '';

        if (workspaceFolders && workspaceFolders.length > 0) {
            targetDir = workspaceFolders[0].uri.fsPath;
        } else {
            const result = await vscode.window.showOpenDialog({
                canSelectFiles: false,
                canSelectFolders: true,
                canSelectMany: false,
                openLabel: 'Select folder to initialize Mylo project'
            });
            if (result && result.length > 0) {
                targetDir = result[0].fsPath;
            } else {
                return; // Cancelled
            }
        }

        const projectName = await vscode.window.showInputBox({
            prompt: 'Enter a project name to create a new folder, or leave empty to initialize in the current folder.',
            placeHolder: 'my-new-app'
        });

        if (projectName === undefined) {
            return; // Cancelled
        }

        const args = projectName.trim() ? [projectName.trim()] : [];

        execFile(myloInitPath, args, { cwd: targetDir }, (error, stdout, stderr) => {
            if (error) {
                vscode.window.showErrorMessage(`Failed to initialize Mylo project: ${stderr || error.message}`);
                return;
            }
            vscode.window.showInformationMessage('Mylo project initialized successfully!');
            
            if (projectName.trim()) {
                const newProjectUri = vscode.Uri.file(path.join(targetDir, projectName.trim()));
                vscode.commands.executeCommand('vscode.openFolder', newProjectUri);
            }
        });
    }));
}

function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

module.exports = { activate, deactivate };

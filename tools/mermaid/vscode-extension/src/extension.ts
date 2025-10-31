import * as vscode from 'vscode';
import { PreviewPanel } from './previewProvider';

let updateTimer: NodeJS.Timeout | undefined;

export function activate(context: vscode.ExtensionContext) {
    console.log('Cler Flowgraph Preview extension activated');

    // Command to open preview
    const previewCommand = vscode.commands.registerCommand(
        'cler.openPreview',
        () => {
            const editor = vscode.window.activeTextEditor;
            if (editor && editor.document.languageId === 'cpp') {
                PreviewPanel.createOrShow(context.extensionUri, editor.document);
            } else {
                vscode.window.showWarningMessage('Please open a C++ file to preview flowgraph');
            }
        }
    );

    // Auto-update on save
    const saveWatcher = vscode.workspace.onDidSaveTextDocument(doc => {
        const config = vscode.workspace.getConfiguration('cler');
        const autoUpdate = config.get<boolean>('autoUpdate', true);

        if (autoUpdate && doc.languageId === 'cpp' && PreviewPanel.currentPanel) {
            PreviewPanel.currentPanel.update(doc);
        }
    });

    // Auto-update on change (debounced)
    const changeWatcher = vscode.workspace.onDidChangeTextDocument(event => {
        const config = vscode.workspace.getConfiguration('cler');
        const autoUpdate = config.get<boolean>('autoUpdate', true);
        const delay = config.get<number>('updateDelay', 500);

        if (autoUpdate && event.document.languageId === 'cpp' && PreviewPanel.currentPanel) {
            if (updateTimer) {
                clearTimeout(updateTimer);
            }
            updateTimer = setTimeout(() => {
                PreviewPanel.currentPanel?.update(event.document);
            }, delay);
        }
    });

    context.subscriptions.push(previewCommand, saveWatcher, changeWatcher);
}

export function deactivate() {
    if (updateTimer) {
        clearTimeout(updateTimer);
    }
}

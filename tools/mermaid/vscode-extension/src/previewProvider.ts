import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import { exec } from 'child_process';
import { promisify } from 'util';

const execAsync = promisify(exec);

export class PreviewPanel {
    public static currentPanel: PreviewPanel | undefined;
    private readonly _panel: vscode.WebviewPanel;
    private readonly _extensionUri: vscode.Uri;
    private _disposables: vscode.Disposable[] = [];
    private _isUpdating: boolean = false;

    public static createOrShow(extensionUri: vscode.Uri, document: vscode.TextDocument) {
        const column = vscode.ViewColumn.Beside;

        // If we already have a panel, show it
        if (PreviewPanel.currentPanel) {
            PreviewPanel.currentPanel._panel.reveal(column);
            PreviewPanel.currentPanel.update(document);
            return;
        }

        // Otherwise, create a new panel
        const panel = vscode.window.createWebviewPanel(
            'clerFlowgraphPreview',
            'Flowgraph Preview',
            column,
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                localResourceRoots: [vscode.Uri.joinPath(extensionUri, 'media')]
            }
        );

        PreviewPanel.currentPanel = new PreviewPanel(panel, extensionUri);
        PreviewPanel.currentPanel.update(document);
    }

    private constructor(panel: vscode.WebviewPanel, extensionUri: vscode.Uri) {
        this._panel = panel;
        this._extensionUri = extensionUri;

        // Listen for when the panel is disposed
        this._panel.onDidDispose(() => this.dispose(), null, this._disposables);
    }

    public async update(document: vscode.TextDocument) {
        if (this._isUpdating) {
            return; // Prevent concurrent updates
        }

        this._isUpdating = true;
        const webview = this._panel.webview;

        try {
            // Show loading state
            webview.html = this.getLoadingHtml();

            // Generate Mermaid diagram
            const mermaidContent = await this.generateMermaid(document.fileName);

            // Update webview with diagram
            webview.html = this.getHtmlForWebview(webview, mermaidContent, document.fileName);

        } catch (error: any) {
            webview.html = this.getErrorHtml(error);
        } finally {
            this._isUpdating = false;
        }
    }

    private async generateMermaid(filePath: string): Promise<string> {
        // Find cler-mermaid executable
        const toolPath = await this.findToolPath();
        const tmpDir = path.join(require('os').tmpdir(), 'cler-preview');
        const outputPath = path.join(tmpDir, `preview-${Date.now()}`);

        // Ensure tmp directory exists
        if (!fs.existsSync(tmpDir)) {
            fs.mkdirSync(tmpDir, { recursive: true });
        }

        try {
            const { stdout, stderr } = await execAsync(
                `"${toolPath}" "${filePath}" -o "${outputPath}"`,
                { timeout: 10000 }
            );

            // Check if output file was created
            const mdPath = `${outputPath}.md`;
            if (!fs.existsSync(mdPath)) {
                throw new Error(`Output file not created: ${mdPath}`);
            }

            // Read and parse Mermaid content
            const content = fs.readFileSync(mdPath, 'utf8');

            // Clean up temp file
            try {
                fs.unlinkSync(mdPath);
            } catch (e) {
                // Ignore cleanup errors
            }

            // Extract mermaid code (remove fences)
            const match = content.match(/```mermaid\n([\s\S]*?)\n```/);
            if (!match) {
                throw new Error('No Mermaid diagram found in output');
            }

            return match[1];

        } catch (error: any) {
            if (error.code === 'ENOENT') {
                throw new Error(`cler-mermaid tool not found at: ${toolPath}\n\nPlease run install.sh or configure the path in settings.`);
            }
            throw new Error(`Failed to generate diagram: ${error.message}`);
        }
    }

    private async findToolPath(): Promise<string> {
        const config = vscode.workspace.getConfiguration('cler');
        const configuredPath = config.get<string>('toolPath');

        // 1. Try configured path
        if (configuredPath && fs.existsSync(configuredPath)) {
            return configuredPath;
        }

        // 2. Try bundled with extension
        const bundledPath = path.join(this._extensionUri.fsPath, 'bin', 'cler-mermaid');
        if (fs.existsSync(bundledPath)) {
            return bundledPath;
        }

        // 3. Try in PATH
        try {
            const { stdout } = await execAsync('which cler-mermaid');
            const pathInSystem = stdout.trim();
            if (pathInSystem && fs.existsSync(pathInSystem)) {
                return pathInSystem;
            }
        } catch (e) {
            // Not in PATH
        }

        // 4. Try common locations
        const commonPaths = [
            path.join(require('os').homedir(), '.local', 'bin', 'cler-mermaid'),
            '/usr/local/bin/cler-mermaid',
            path.join(this._extensionUri.fsPath, '..', 'mermaid', 'build', 'cler-mermaid')
        ];

        for (const p of commonPaths) {
            if (fs.existsSync(p)) {
                return p;
            }
        }

        throw new Error('cler-mermaid tool not found');
    }

    private getLoadingHtml(): string {
        return `<!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>
                body {
                    padding: 20px;
                    background: var(--vscode-editor-background);
                    color: var(--vscode-editor-foreground);
                    font-family: var(--vscode-font-family);
                    display: flex;
                    align-items: center;
                    justify-content: center;
                    height: 100vh;
                }
                .loading {
                    text-align: center;
                }
            </style>
        </head>
        <body>
            <div class="loading">
                <p>⚙️ Generating flowgraph...</p>
            </div>
        </body>
        </html>`;
    }

    private getHtmlForWebview(webview: vscode.Webview, mermaidContent: string, filePath: string): string {
        const fileName = path.basename(filePath);

        return `<!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <script src="https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"></script>
            <style>
                body {
                    padding: 20px;
                    background: var(--vscode-editor-background);
                    color: var(--vscode-editor-foreground);
                    font-family: var(--vscode-font-family);
                    margin: 0;
                }
                .header {
                    padding: 10px 0;
                    border-bottom: 1px solid var(--vscode-panel-border);
                    margin-bottom: 20px;
                }
                .filename {
                    font-size: 14px;
                    color: var(--vscode-descriptionForeground);
                }
                #diagram {
                    text-align: center;
                    padding: 20px;
                }
                .mermaid {
                    display: inline-block;
                }
            </style>
        </head>
        <body>
            <div class="header">
                <div class="filename">📊 ${fileName}</div>
            </div>
            <div id="diagram">
                <pre class="mermaid">
${mermaidContent}
                </pre>
            </div>
            <script>
                mermaid.initialize({
                    startOnLoad: true,
                    theme: 'default',
                    flowchart: {
                        useMaxWidth: true,
                        htmlLabels: true,
                        curve: 'basis'
                    }
                });
            </script>
        </body>
        </html>`;
    }

    private getErrorHtml(error: any): string {
        return `<!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <style>
                body {
                    padding: 20px;
                    background: var(--vscode-editor-background);
                    color: var(--vscode-editor-foreground);
                    font-family: var(--vscode-font-family);
                }
                .error {
                    color: var(--vscode-errorForeground);
                    padding: 20px;
                    border: 1px solid var(--vscode-errorBorder);
                    border-radius: 4px;
                    background: var(--vscode-inputValidation-errorBackground);
                }
                h3 {
                    margin-top: 0;
                }
                pre {
                    white-space: pre-wrap;
                    word-wrap: break-word;
                }
            </style>
        </head>
        <body>
            <div class="error">
                <h3>❌ Failed to generate flowgraph</h3>
                <pre>${error.message || error}</pre>
            </div>
        </body>
        </html>`;
    }

    public dispose() {
        PreviewPanel.currentPanel = undefined;

        this._panel.dispose();

        while (this._disposables.length) {
            const disposable = this._disposables.pop();
            if (disposable) {
                disposable.dispose();
            }
        }
    }
}

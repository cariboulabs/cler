import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import { exec } from 'child_process';
import { promisify } from 'util';

const execAsync = promisify(exec);

export class PreviewPanel {
    public static currentPreview: { cppFile: string; mdFile: vscode.Uri } | undefined;
    private static _isUpdating: boolean = false;

    public static async createOrShow(extensionUri: vscode.Uri, document: vscode.TextDocument) {
        try {
            // Generate the Mermaid markdown file
            const mdFilePath = await PreviewPanel.generateMermaid(document.fileName);
            const mdFileUri = vscode.Uri.file(mdFilePath);

            // Store current preview info
            PreviewPanel.currentPreview = {
                cppFile: document.fileName,
                mdFile: mdFileUri
            };

            // Show markdown preview directly (requires Markdown Preview Mermaid Support extension)
            await vscode.commands.executeCommand('markdown.showPreview', mdFileUri);

        } catch (error: any) {
            vscode.window.showErrorMessage(`Failed to generate flowgraph: ${error.message}`);
        }
    }

    public static async update(document: vscode.TextDocument) {
        if (PreviewPanel._isUpdating) {
            return; // Prevent concurrent updates
        }

        // Only update if this is the file we're previewing
        if (!PreviewPanel.currentPreview || PreviewPanel.currentPreview.cppFile !== document.fileName) {
            return;
        }

        PreviewPanel._isUpdating = true;

        try {
            // Regenerate the Mermaid file
            const mdFilePath = await PreviewPanel.generateMermaid(document.fileName);

            // The markdown preview will auto-refresh when the file changes
            // No need to manually trigger refresh

        } catch (error: any) {
            console.error('Failed to update preview:', error);
        } finally {
            PreviewPanel._isUpdating = false;
        }
    }

    private static async generateMermaid(filePath: string): Promise<string> {
        // Find cler-mermaid executable
        const toolPath = await PreviewPanel.findToolPath();
        const tmpDir = path.join(require('os').tmpdir(), 'cler-preview');
        const baseName = path.basename(filePath, path.extname(filePath));
        const outputPath = path.join(tmpDir, `${baseName}-flowgraph`);

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

            return mdPath;

        } catch (error: any) {
            if (error.code === 'ENOENT') {
                throw new Error(`cler-mermaid tool not found at: ${toolPath}\n\nPlease run install.sh or configure the path in settings.`);
            }
            throw new Error(`Failed to generate diagram: ${error.message}`);
        }
    }

    private static async findToolPath(): Promise<string> {
        const config = vscode.workspace.getConfiguration('cler');
        const configuredPath = config.get<string>('toolPath');

        // 1. Try configured path
        if (configuredPath && fs.existsSync(configuredPath)) {
            return configuredPath;
        }

        // 2. Try in workspace
        const workspaceFolders = vscode.workspace.workspaceFolders;
        if (workspaceFolders) {
            const workspacePath = path.join(
                workspaceFolders[0].uri.fsPath,
                'tools',
                'mermaid',
                'build',
                'cler-mermaid'
            );
            if (fs.existsSync(workspacePath)) {
                return workspacePath;
            }
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
        ];

        for (const p of commonPaths) {
            if (fs.existsSync(p)) {
                return p;
            }
        }

        throw new Error('cler-mermaid tool not found');
    }
}

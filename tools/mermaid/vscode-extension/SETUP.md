# Quick Setup Guide

## What was created

```
tools/mermaid/
├── build/                    # C++ build artifacts
├── src/                      # C++ source code
├── CMakeLists.txt
└── vscode-extension/         # VS Code extension
    ├── src/
    │   ├── extension.ts
    │   └── previewProvider.ts
    ├── package.json
    ├── tsconfig.json
    ├── install.sh            # Automated installer ⭐
    ├── uninstall.sh
    ├── README.md
    └── SETUP.md
```

## Installation (One Command!)

```bash
cd tools/mermaid/vscode-extension
./install.sh
```

This will:
- ✓ Check prerequisites
- ✓ Build cler-mermaid C++ tool
- ✓ Install to ~/.local/bin
- ✓ Install npm dependencies
- ✓ Compile TypeScript
- ✓ Package & install .vsix to VS Code

## Usage

1. Open a Cler C++ file in VS Code
2. Click the 📊 graph icon in toolbar
   OR
   Ctrl+Shift+P → "Open Cler Flowgraph Preview"

3. Preview opens side-by-side
4. Edits update automatically (500ms debounce)

## Configuration

VS Code Settings → Search "cler":

- `cler.toolPath` - Custom path to cler-mermaid (optional)
- `cler.autoUpdate` - Auto-refresh on changes (default: true)
- `cler.updateDelay` - Debounce delay in ms (default: 500)

## Testing without installation

```bash
npm install
npm run compile
code .
# Press F5 to launch Extension Development Host
```

## Architecture

```
User edits .cpp file
    ↓
Extension watches changes (debounced)
    ↓
Runs: cler-mermaid input.cpp -o /tmp/preview
    ↓
Parses .md output
    ↓
WebView renders with Mermaid.js
    ↓
Live diagram displayed
```

## What makes this robust

From the 10 robustness improvements we made:
- ✓ RAII tree wrapper (no memory leaks)
- ✓ Exception safety (errors shown in preview)
- ✓ Validation (catches bad flowgraphs)
- ✓ HTML escaping (safe template rendering)
- ✓ Fast pre-screening (skips non-flowgraph files)
- ✓ Nested template support (handles complex types)
- ✓ Per-file error recovery (batch processing resilient)
- ✓ Verbose mode & statistics (in C++ tool)
- ✓ Multiple tool path strategies (auto-detect)
- ✓ Debounced updates (smooth editing)

## Troubleshooting

**"cler-mermaid not found"**
→ Run `./install.sh` or add ~/.local/bin to PATH

**Preview not updating**
→ Check Settings: `cler.autoUpdate` = true

**Build errors**
→ Ensure prerequisites: `node npm cmake g++ code`

## Uninstall

```bash
./uninstall.sh
```

Or manually:
```bash
code --uninstall-extension cler.cler-flowgraph-preview
rm ~/.local/bin/cler-mermaid
```

# Cler Flowgraph Preview - VS Code Extension

Live Mermaid flowchart preview for Cler C++ flowgraph files.

## Features

- 📊 **Live Preview**: Visualize Cler flowgraphs as you edit
- ⚡ **Auto-Update**: Automatically refreshes on file changes (configurable)
- 🎨 **Mermaid Rendering**: Clean, professional flowchart diagrams
- 🔧 **Zero Configuration**: Works out of the box after installation
- 🚀 **Fast**: Debounced updates for smooth editing experience

## Installation

### Automated Installation (Recommended)

```bash
cd tools/mermaid/vscode-extension
./install.sh
```

The script will:
1. ✓ Check prerequisites (node, npm, cmake, g++, VS Code)
2. ✓ Build the `cler-mermaid` C++ tool
3. ✓ Install `cler-mermaid` to `~/.local/bin`
4. ✓ Install npm dependencies
5. ✓ Compile the TypeScript extension
6. ✓ Package and install the `.vsix` to VS Code

**Prerequisites**:
- Node.js & npm
- CMake & g++
- VS Code with CLI (`code` command)

Install prerequisites on Ubuntu/Debian:
```bash
sudo apt-get install nodejs npm cmake g++
```

### Manual Installation

1. **Build cler-mermaid**:
   ```bash
   cd tools/mermaid
   cmake -B build
   cmake --build build
   sudo cp build/cler-mermaid /usr/local/bin/
   ```

2. **Package extension**:
   ```bash
   cd vscode-extension
   npm install
   npm run compile
   npx vsce package
   ```

3. **Install .vsix**:
   ```bash
   code --install-extension cler-flowgraph-preview-1.0.0.vsix
   ```

## Usage

### Open Preview

1. Open a Cler C++ flowgraph file (`.cpp`)
2. Click the **graph icon** (📊) in the editor toolbar
   - OR use Command Palette: `Ctrl+Shift+P` → "Open Cler Flowgraph Preview"
3. Preview panel opens side-by-side with your code

### Auto-Update Behavior

- **On Save**: Preview updates when you save the file
- **On Change**: Preview updates after 500ms of inactivity (configurable)

### Supported Files

Any C++ file containing:
- `BlockRunner()` calls
- `make_*_flowgraph()` functions
- Cler block declarations

## Configuration

Open VS Code Settings (`Ctrl+,`) and search for "cler":

### `cler.toolPath`
- **Type**: `string`
- **Default**: `""` (auto-detect)
- **Description**: Path to `cler-mermaid` executable

Leave empty to auto-detect from:
1. Extension bundled version (`bin/cler-mermaid`)
2. System PATH
3. `~/.local/bin/cler-mermaid`
4. Common install locations

### `cler.autoUpdate`
- **Type**: `boolean`
- **Default**: `true`
- **Description**: Automatically update preview on file changes

### `cler.updateDelay`
- **Type**: `number`
- **Default**: `500` (milliseconds)
- **Description**: Delay before updating preview after typing

## Troubleshooting

### "cler-mermaid tool not found"

**Solution 1**: Run the installer
```bash
cd tools/vscode-extension
./install.sh
```

**Solution 2**: Configure tool path manually
1. Open VS Code Settings
2. Search for "cler.toolPath"
3. Set to your `cler-mermaid` location

**Solution 3**: Add to PATH
```bash
# Add to ~/.bashrc or ~/.zshrc
export PATH="$HOME/.local/bin:$PATH"
```

### Preview not updating

1. Check file contains valid Cler flowgraph code
2. Verify `cler.autoUpdate` is enabled in settings
3. Check Output panel (View → Output → Cler Flowgraph) for errors

### Preview shows error

- Ensure C++ syntax is valid
- Check that blocks are properly connected
- View detailed error in preview panel

## Uninstallation

```bash
cd tools/vscode-extension
./uninstall.sh
```

Or manually:
```bash
code --uninstall-extension cler.cler-flowgraph-preview
rm ~/.local/bin/cler-mermaid
```

## Architecture

```
Extension (TypeScript)
    ↓
Runs cler-mermaid (C++)
    ↓
Generates .md with Mermaid
    ↓
WebView renders with Mermaid.js
```

### Components

- **extension.ts**: Main extension logic, command registration
- **previewProvider.ts**: WebView panel management, tool execution
- **cler-mermaid**: C++ tool using tree-sitter for AST parsing

## Development

### Build from source

```bash
npm install
npm run compile
```

### Watch mode

```bash
npm run watch
```

### Debug

1. Open in VS Code: `code .`
2. Press `F5` to launch Extension Development Host
3. Open a Cler C++ file and test preview

### Package

```bash
npx vsce package
```

## What it visualizes

- **Blocks**: All Cler block declarations (variables ending in `Block`)
- **Connections**: From `BlockRunner()` function calls
- **Template Parameters**: Displayed in diagram nodes
- **Block Types**: Color-coded shapes
  - 🔵 Sources (stadium shape)
  - 🟣 Sinks (trapezoid shape)
  - 🟢 Processing blocks (rectangle shape)

## Example

**Input** (`example.cpp`):
```cpp
#include <cler.hpp>

SourceCWBlock<float> source(440.0f);
ProcessBlock processor;
SinkBlock<float> sink;

int main() {
    auto fg = cler::make_test_flowgraph(
        BlockRunner(&source, &processor.in),
        BlockRunner(&processor, &sink.in)
    );
    return 0;
}
```

**Output**: Live Mermaid flowchart showing:
```
source → processor → sink
```

## Requirements

- VS Code 1.70.0 or higher
- `cler-mermaid` executable (installed by `install.sh`)

## License

Same as Cler project

## Support

For issues, see the main Cler repository

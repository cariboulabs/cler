# Cler Flow - Visual Flowgraph Editor

A visual flowgraph editor for the Cler DSP Framework, built with Tauri and Svelte.

## Current Status: Demo Version

This version runs as a web application with mock data to demonstrate the concept. The full Tauri version requires system dependencies that need installation.

## Running the Demo

```bash
npm install
npm run dev
```

Then open http://localhost:5173 in your browser.

## Features Demonstrated

- **Block Palette**: Categorized blocks (Sources, Math, Sinks)
- **Visual Editor**: Drag-and-drop blocks onto canvas
- **Connections**: Click output ports, then input ports to connect
- **Property Panel**: Select blocks to edit parameters
- **Code Generation**: Click "Build" to see generated C++ code

## Demo Usage

1. Drag blocks from the palette (left) to the canvas (center)
2. Click on output ports (right side of blocks), then input ports (left side) to connect
3. Select blocks to edit parameters in the property panel (right)
4. Click "Build" to see generated C++ and CMake code

## Full Version Architecture

The complete implementation includes:

### Backend (Rust)
- **Block Parser** - Uses tree-sitter-cpp to parse C++ headers
- **Library Manager** - Remembers loaded directories
- **Code Generator** - Generates C++ and CMake files
- **Build System** - Compiles and runs generated projects

### Frontend (Svelte)
- **Visual Graph Editor** - Current demo interface
- **File Dialog Integration** - Load custom block libraries
- **Real-time Validation** - Type checking for connections

## Installation Requirements (Full Version)

For the full Tauri version, you'll need:

```bash
# Ubuntu/Debian
sudo apt-get install libwebkit2gtk-4.0-dev libgtk-3-dev libgtksourceview-3.0-dev

# Or for newer systems
sudo apt-get install libwebkit2gtk-4.1-dev libjavascriptcoregtk-4.1-dev
```

## Project Structure

```
cler-flow/
├── src/                    # Svelte frontend
│   ├── App.svelte         # Main application
│   └── lib/
│       ├── BlockPalette.svelte
│       ├── GraphEditor.svelte
│       └── PropertyPanel.svelte
├── src-tauri/             # Rust backend
│   ├── src/
│   │   ├── parser/        # C++ parsing
│   │   ├── generator/     # Code generation
│   │   └── builder/       # Build system
│   └── templates/         # Code templates
└── PLAN.md               # Detailed implementation plan
```

## Next Steps

1. Fix system dependencies for full Tauri build
2. Enhance C++ parser to extract constructor parameters
3. Implement visual connection rendering
4. Add save/load functionality
5. Integrate with actual build system

## Design Goals

- **Visual**: Intuitive drag-and-drop interface like GNU Radio Companion
- **Type-safe**: Connection validation at design time
- **Efficient**: Generates optimized C++ code
- **Extensible**: Easy to add custom block libraries
- **Cross-platform**: Desktop app for Windows, Mac, Linux
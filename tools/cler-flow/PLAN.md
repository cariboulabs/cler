# cler-flow: Visual Flowgraph Editor for Cler DSP Framework

## Overview

cler-flow is a desktop application for visually creating and editing Cler DSP flowgraphs. Users can drag-and-drop blocks, connect them, configure parameters, and generate executable C++ code with CMake build files.

## Core Requirements

### 1. Block Discovery & Parsing
- **Parse C++ header files** to extract block definitions from:
  - `/desktop_blocks/` directory (loaded by default)
  - Custom user-defined block libraries via "Load Blocks" button
- **Extract using tree-sitter-cpp**:
  - Class inheritance from `BlockBase`
  - Constructor parameters and their types
  - Input/output channel definitions
  - Template parameters
- **Generate intermediate TOML** for block metadata caching
- **Library memory**:
  - Remember previously loaded directories
  - Persist across application restarts
  - Quick reload from recent libraries list

### 2. Visual Graph Editor
- **Drag-and-drop** blocks from categorized palette
- **Connection validation**:
  - Type checking (float, complex<float>, etc.)
  - Prevent multiple connections to single input
  - Highlight unconnected required channels
- **Visual feedback**:
  - Valid/invalid connection indicators
  - Type mismatch warnings
  - Missing connection errors

### 3. Block Parameter Configuration
- **Dynamic property panel** based on block type
- **Parameter types**:
  - Numeric (float, int) with min/max/step
  - String values
  - Enum/dropdown selections
  - Boolean checkboxes
- **Live validation** of parameter values
- **Default values** from constructor analysis

### 4. Code Generation & Execution
- **Generate C++ file**:
  - Proper includes for used blocks
  - Block instantiation with parameters
  - BlockRunner connections
  - Flowgraph configuration
- **Generate CMakeLists.txt**:
  - Find Cler package
  - Link appropriate libraries
  - Set build options
- **Build & Run options**:
  - Build only
  - Build and run
  - Run with live output streaming

### 5. Flowgraph Configuration
- **Runtime configuration**:
  - Adaptive sleep settings
  - Buffer sizes
  - Thread priorities
- **Save/Load** configuration presets
- **Performance profiling** integration

### 6. Future: Reverse Engineering
- **Parse existing C++ files** to recreate visual graphs
- **Maintain comments** and custom code sections
- **Round-trip editing** support

## Technical Architecture

### Technology Stack
- **Tauri 2.0** - Desktop framework
- **Rust** - Backend logic
- **Svelte/SvelteKit** - Frontend UI
- **Svelvet** or **React Flow** - Graph visualization
- **tree-sitter-cpp** - C++ parsing
- **Tera** - Template engine

### Project Structure
```
cler-flow/
├── src-tauri/          # Rust backend
│   ├── src/
│   │   ├── main.rs
│   │   ├── parser/     # C++ parsing with tree-sitter
│   │   ├── generator/  # Code generation
│   │   ├── builder/    # CMake & compilation
│   │   └── state/      # Graph state management
│   └── Cargo.toml
├── src/                # Svelte frontend
│   ├── lib/
│   │   ├── components/ # UI components
│   │   ├── graph/      # Graph editor
│   │   └── stores/     # State management
│   └── routes/         # App pages
├── block-cache/        # TOML block definitions
├── templates/          # Code generation templates
└── config/             # User preferences & library memory
    └── libraries.json  # Remembered library paths
```

### Data Flow
```
C++ Headers → tree-sitter → Block Metadata (TOML) → UI Block Palette
                                                         ↓
User Actions → Graph State → Validation → Code Generation → Build → Execute
                   ↑                           ↓
                   └─────── Save/Load ─────────┘
```

## Implementation Phases

### Phase 1: Block Discovery (Week 1)
- [ ] Setup Tauri project structure
- [ ] Implement tree-sitter C++ parser
- [ ] Extract block definitions from headers
- [ ] Generate TOML metadata cache
- [ ] Create Rust data structures for blocks
- [ ] Implement library memory system

### Phase 2: Visual Editor (Week 2-3)
- [ ] Integrate graph visualization library
- [ ] Implement drag-and-drop from palette
- [ ] Add connection validation logic
- [ ] Create parameter configuration panel
- [ ] Add visual error indicators
- [ ] Add "Load Blocks" button with directory picker

### Phase 3: Code Generation (Week 3-4)
- [ ] Create Tera templates for C++ generation
- [ ] Implement CMakeLists.txt generation
- [ ] Add proper include path resolution
- [ ] Handle template type specialization

### Phase 4: Build & Execute (Week 4-5)
- [ ] Integrate with CMake via Rust
- [ ] Implement build process management
- [ ] Add output streaming to UI
- [ ] Error handling and reporting

### Phase 5: Polish & Features (Week 5-6)
- [ ] Save/Load graph files (.cflow format)
- [ ] Flowgraph configuration UI
- [ ] Keyboard shortcuts
- [ ] Undo/Redo support
- [ ] Block search and filtering
- [ ] Recent libraries quick access

## Block Metadata Format (TOML)

```toml
[[blocks]]
name = "SourceCW"
display_name = "CW Source"
category = "Sources"
header = "sources/source_cw.hpp"
class_name = "SourceCWBlock"
description = "Continuous wave signal generator"
library_path = "/home/user/cler/desktop_blocks"

[blocks.template]
parameters = ["T"]
default = "float"
options = ["float", "std::complex<float>"]

[[blocks.parameters]]
name = "name"
type = "string"
position = 0
required = true

[[blocks.parameters]]
name = "amplitude"
type = "T"
position = 1
default = "1.0"
min = "0.0"
max = "10.0"

[[blocks.parameters]]
name = "frequency"
type = "float"
position = 2
default = "1000.0"
units = "Hz"

[[blocks.parameters]]
name = "sample_rate"
type = "float"
position = 3
default = "48000.0"
units = "Hz"

[[blocks.outputs]]
name = "out"
type = "T"
index = 0
```

## Library Memory Format (JSON)

```json
{
  "default_libraries": [
    "/home/user/cler/desktop_blocks"
  ],
  "user_libraries": [
    {
      "path": "/home/user/my_blocks",
      "name": "My Custom Blocks",
      "last_loaded": "2024-01-15T10:30:00Z"
    }
  ],
  "recent_limit": 10
}
```

## Key Implementation Details

### C++ Parsing Strategy
1. Use tree-sitter queries to find classes inheriting from `BlockBase`
2. Parse constructor parameters and their default values
3. Identify channel declarations (inputs owned by block)
4. Extract template parameters and constraints
5. Cache results in TOML for fast loading

### Type System
- Map C++ types to internal representation
- Handle template specializations
- Validate connections at design time
- Support common types: float, complex<float>, int, etc.

### Error Handling
- Parser errors → Show which blocks couldn't be loaded
- Connection errors → Visual feedback + tooltips
- Build errors → Display compiler output
- Runtime errors → Stream process output

### Library Management
- Auto-load desktop_blocks on startup
- "Load Blocks" button opens directory picker
- Parse all .hpp files in selected directory
- Remember loaded libraries in config
- Show library source in block palette
- Allow unloading/reloading libraries

## Success Criteria
- [ ] Can load all blocks from desktop_blocks/
- [ ] Can load custom block libraries with memory
- [ ] Intuitive drag-and-drop interface
- [ ] Type-safe connections with clear error messages
- [ ] Generated code compiles and runs correctly
- [ ] Performance comparable to hand-written code
- [ ] Can generate basic flowgraphs using desktop_blocks
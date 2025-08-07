# cler-flow: Reconstructing core-nodes for CLER

## Vision

Rebuild core-nodes incrementally, keeping its excellent UI/UX while fixing fundamental architectural issues to support CLER's compile-time DSP flowgraph generation.

## Strategy: Copy Good, Fix Bad, Add New

### What to Keep from core-nodes
- ✅ **Visual Design** - Professional, clean interface
- ✅ **CoreDiagram** - Sophisticated connection routing with splines
- ✅ **Interaction Model** - Pan, zoom, selection, drag behaviors  
- ✅ **Docking System** - ImGui docking with saved layouts
- ✅ **Node Rendering** - Collapsible nodes, port inversion, shadows
- ✅ **Property Inspector** - Parameter editing with proper widgets
- ✅ **File Operations** - Save/load projects (adapt format)
- ✅ **Context Menus** - Right-click operations
- ✅ **Keyboard Shortcuts** - Standard editor operations

### What to Fix
- ❌ **Runtime Polymorphism** → Template-based compile-time design
- ❌ **Fixed Port Types** → Support arbitrary C++ types from CLER
- ❌ **Manual Node Registry** → Auto-discover blocks from .hpp files
- ❌ **String-based Connections** → Type-safe connections
- ❌ **No Code Generation** → Generate compilable CLER C++ code
- ❌ **XML with Attributes** → JSON with embedded block metadata

### What to Add
- 🆕 **C++ Header Parser** - Import blocks directly from CLER headers
- 🆕 **Code Generation** - Generate valid CLER flowgraph code
- 🆕 **Block Metadata System** - Store parsed block info with saved graphs
- 🆕 **Compile & Run** - Build and execute generated code
- 🆕 **CLER Concepts** - Inputs as owned channels, outputs as procedure params
- 🆕 **Template Support** - Handle templated blocks properly

## Reconstruction Phases

### Phase 1: Foundation (Week 1)
Start with core-nodes structure but modernize the foundation:

- [ ] Copy core-nodes project structure
- [ ] Set up CMake with conditional ImGui (standalone or from CLER)
- [ ] Port `GuiApp` base class for window management
- [ ] Create `BlockSpec` - Modern block metadata (not CoreNode)
- [ ] Implement `VisualNode` - Rendering based on BlockSpec
- [ ] **Checkpoint**: Basic window with modern block representation

### Phase 2: Diagram System (Week 2)
Port CoreDiagram but with improvements:

- [ ] Port `CoreDiagram` → `FlowCanvas` with cleaner separation
- [ ] Copy connection routing algorithms (the good spline math)
- [ ] Port node rendering (shadows, collapse, titles)
- [ ] Fix: Use type-safe connections instead of void* links
- [ ] Fix: Support variable number of ports (CLER arrays)
- [ ] **Checkpoint**: Can create and connect nodes visually

### Phase 3: Interaction & UI (Week 3)
Port the polish that makes core-nodes good:

- [ ] Port property inspector with BlockSpec integration
- [ ] Port context menus and operations
- [ ] Port keyboard shortcuts
- [ ] Port selection system (box select, multi-select)
- [ ] Add node library browser (improved with categories)
- [ ] **Checkpoint**: Full interactive editor experience

### Phase 4: Persistence (Week 4)
New save format with embedded metadata:

- [ ] Design JSON format with embedded BlockSpecs
- [ ] Save flowgraph + all block definitions
- [ ] Load and reconstruct graphs
- [ ] Migration tool from core-nodes XML
- [ ] **Checkpoint**: Can save/load complete projects

### Phase 5: CLER Integration (Week 5-6)
The new functionality:

- [ ] C++ header parser (tree-sitter or libclang)
- [ ] Auto-discover blocks from .hpp files
- [ ] Code generator for CLER flowgraphs
- [ ] Template parameter handling
- [ ] Compile & run integration
- [ ] **Checkpoint**: Generate working CLER code

### Phase 6: Polish (Week 7-8)
Make it production ready:

- [ ] Performance optimization
- [ ] Error handling and validation
- [ ] Documentation
- [ ] Example projects
- [ ] Test suite

## Technical Architecture

### Core Classes (Modernized)

```cpp
// Modern block specification (replaces CoreNode)
class BlockSpec {
    // Discovered from C++ headers
    std::string class_name;
    std::vector<TemplateParam> template_params;
    std::vector<ConstructorParam> constructor_params;
    std::vector<PortSpec> input_ports;   // Owned channels
    std::vector<PortSpec> output_ports;  // Procedure parameters
};

// Visual representation (rendering only)
class VisualNode {
    std::shared_ptr<BlockSpec> spec;
    ImVec2 position;
    std::map<string, string> param_values;
    // Rendering based on spec, not inheritance
};

// Canvas (improved CoreDiagram)
class FlowCanvas {
    std::vector<VisualNode> nodes;
    std::vector<Connection> connections;
    // Keep CoreDiagram's good algorithms
    // Add type-safe connection validation
};

// Code generator (new)
class CodeGenerator {
    std::string generate(const FlowCanvas& canvas);
};

// Block discovery (new)
class BlockParser {
    BlockSpec parse_header(const std::string& hpp_file);
};
```

### Key Improvements Over core-nodes

1. **Separation of Concerns**
   - BlockSpec: Pure data (what a block is)
   - VisualNode: Visual representation (where/how it's displayed)
   - CodeGenerator: Output (what code to generate)

2. **Type Safety**
   - Connections validated at design time
   - Generated code guaranteed to compile
   - No runtime type errors

3. **Zero Configuration**
   - Import blocks from headers directly
   - No manual XML node definitions
   - Auto-discover everything from C++

4. **Modern C++**
   - Smart pointers throughout
   - Move semantics
   - No raw pointer management

## Development Guidelines

### When Copying from core-nodes
1. **Check if it needs modernization** - Update to C++17 idioms
2. **Preserve the good algorithms** - Especially spline routing
3. **Fix the architecture** - Separate concerns properly
4. **Add comments** - Document why things work

### When Adding New Features
1. **Keep it simple** - Don't over-engineer
2. **Test incrementally** - Checkpoint after each phase
3. **Maintain compatibility** - Can load old projects (with migration)

### Quality Checkpoints
After each phase, verify:
- [ ] Code compiles without warnings
- [ ] UI remains responsive
- [ ] Can perform basic operations
- [ ] No memory leaks
- [ ] Saves/loads correctly

## Success Metrics

1. **UI Quality**: Matches core-nodes polish and usability
2. **Code Generation**: Produces valid, efficient CLER code
3. **Performance**: Handles 100+ nodes without lag
4. **Reliability**: No crashes during normal use
5. **Maintainability**: Clear architecture, easy to extend

## Next Steps

1. Clear current attempt: `rm -rf src/ include/`
2. Copy core-nodes structure
3. Start Phase 1 with modernized foundation
4. Regular checkpoints to ensure we're on track

## References

- Original core-nodes: `/home/alon/repos/cler/cler_flow/reference/core-nodes/`
- CLER architecture: `/home/alon/repos/cler/ai-bringup.md`
- Target: Generate code like examples in `/home/alon/repos/cler/desktop_examples/`
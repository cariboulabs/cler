# AI Context for Core-Nodes Fork - Cler DSP Frontend Integration

## Project Goal
Transform core-nodes into a production-ready visual flowgraph editor for the Cler DSP framework, similar to GNU Radio Companion but with better wire routing and a cleaner codebase.

## Key Requirements
1. **Block Auto-Discovery**: Parse `*_block.cpp` files from Cler to automatically generate node types
2. **Parameter Editing**: Real-time parameter changes without recompilation
3. **Smart Wire Routing**: Keep the excellent wire re-arrangement from core-nodes
4. **Save/Load Graphs**: Robust serialization with type validation
5. **Type Safety**: Match Cler's compile-time type checking at the GUI level

## Critical Issues to Fix (Priority Order)

### 1. Memory Management (CRITICAL)
- **Current**: Raw pointers (`std::vector<CoreNode*>`) with manual delete
- **Fix**: Convert to `std::shared_ptr<CoreNode>` throughout
- **Files**: `CoreDiagram.hpp/cpp`, `CoreLibrary.hpp/cpp`

### 2. Exception Safety (HIGH)
- **Current**: No exception handling for file I/O, XML parsing
- **Fix**: Add try-catch blocks, especially in `SaveToFile()`, `LoadFromFile()`
- **Files**: `MyApp.cpp`, `CoreDiagram.cpp`

### 3. Null Pointer Checks (HIGH)
- **Current**: Many unchecked dereferences (e.g., `link.inputNode->GetName()`)
- **Fix**: Add defensive checks before all pointer operations
- **Files**: Throughout, especially `CoreDiagram.cpp`

### 4. Factory Pattern for Blocks (MEDIUM)
- **Current**: Hardcoded block types in `CoreLibrary::GetNode()`
- **Fix**: Dynamic registration system for Cler blocks
- **New Files**: Create `BlockRegistry.hpp/cpp`

### 5. Resource Management (MEDIUM)
- **Current**: No RAII patterns
- **Fix**: Use scope guards, smart pointers
- **Files**: File operations in `MyApp.cpp`

## Integration Architecture

### Block Discovery System
```cpp
// New file: ClerBlockInterface.hpp
class ClerBlockInterface {
    // Metadata parsed from Cler block files
    struct BlockInfo {
        std::string name;
        std::string category;
        std::vector<PortInfo> inputs;
        std::vector<PortInfo> outputs;
        std::vector<ParamInfo> parameters;
    };
    
    // Factory to create GUI nodes from Cler blocks
    static std::shared_ptr<CoreNode> CreateFromClerBlock(const BlockInfo& info);
};
```

### Parameter Binding
```cpp
// Extend CoreNode to support dynamic parameters
class CoreNode {
    // Add parameter system
    std::map<std::string, std::variant<float, int, bool, std::string>> parameters;
    virtual void OnParameterChanged(const std::string& name) {}
};
```

### Type System Integration
```cpp
// Match Cler's type system
enum class ClerDataType {
    Float,
    ComplexFloat,
    Int32,
    UInt8,
    Custom
};

// Validate connections at GUI level
bool ValidateConnection(const PortInfo& output, const PortInfo& input);
```

## File Structure Plan
```
core-nodes/
├── core-nodes/
│   ├── CoreNode.hpp/cpp          # Base node class (fix memory issues)
│   ├── CoreDiagram.hpp/cpp       # Main diagram (fix pointer management)
│   ├── CoreLibrary.hpp/cpp       # Convert to dynamic registry
│   ├── ClerBlockInterface.hpp/cpp # NEW: Cler integration layer
│   ├── BlockRegistry.hpp/cpp     # NEW: Dynamic block registration
│   └── TypeSystem.hpp            # NEW: Type validation
├── examples/
│   └── cler_integration/         # NEW: Example Cler blocks
└── AI_CONTEXT.md                # This file
```

## Testing Strategy
1. **Memory Safety**: Valgrind tests for leak detection
2. **Type Safety**: Unit tests for connection validation
3. **Save/Load**: Round-trip tests with complex graphs
4. **Parameter Updates**: Real-time parameter change tests

## Build Integration
```cmake
# Add to CMakeLists.txt
option(BUILD_CLER_INTEGRATION "Build Cler DSP integration" ON)
if(BUILD_CLER_INTEGRATION)
    add_subdirectory(cler_integration)
endif()
```

## Success Criteria
- [ ] No memory leaks or crashes
- [ ] Exception-safe file operations
- [ ] Dynamic block loading from Cler
- [ ] Type-safe connections
- [ ] Real-time parameter updates
- [ ] Robust save/load with validation
- [ ] Clean, maintainable code

## Development Notes
- Preserve the excellent wire routing algorithms
- Keep the minimal footprint advantage
- Focus on safety and extensibility over features
- Maintain compatibility with existing ImGui setup

## Context for AI Assistants
When working on this fork:
1. Prioritize memory safety fixes first
2. Preserve existing wire routing logic (it's the main advantage)
3. Use modern C++ patterns (C++17/20)
4. Add comprehensive error handling
5. Document all Cler-specific additions
6. Keep changes modular for easy upstream merging if desired

## Related Files to Review
- `/home/alon/repos/cler/ai-bringup.md` - Cler framework documentation
- `/home/alon/repos/cler/desktop_blocks/` - Example Cler blocks to support
- Original core-nodes repo: https://github.com/onurae/core-nodes

## Quick Start for AI
```bash
# After forking
git clone [your-fork-url]
cd core-nodes

# First priority: Fix memory management
# Start with CoreDiagram.hpp/cpp - convert raw pointers to shared_ptr

# Second: Add exception handling
# Focus on MyApp.cpp file operations

# Third: Create Cler integration layer
# New files in cler_integration/
```

This project aims to create a lightweight, safe, and extensible visual programming interface for the Cler DSP framework while maintaining the excellent wire routing from the original core-nodes project.
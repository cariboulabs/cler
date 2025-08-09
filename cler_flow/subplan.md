# CLER Flow Block Import System - Implementation Plan

## Overview
Design and implement a system to automatically import CLER DSP blocks from C++ headers, extracting metadata directly from the source code without requiring special comments or markup files.

## Goals
- Parse C++ headers to extract block definitions
- Automatically identify CLER blocks (classes inheriting from `cler::BlockBase`)
- Extract metadata: class names, template parameters, constructor arguments, input/output channels
- Build a searchable library of available blocks
- Enable drag-and-drop instantiation in the flow editor

## Technical Approach

### 1. Parser Selection
**Decision**: Use libclang for C++ AST parsing
- **Rationale**: Robust, handles complex C++ templates and syntax
- **Alternatives considered**: regex (too fragile), tree-sitter (less mature for C++)
- **Implementation**: libclang C API with visitor pattern for AST traversal

### 2. Architecture

```
BlockParser (libclang wrapper)
    ├── parseHeader() - Parse single .hpp file
    ├── isBlockHeader() - Quick check for BlockBase inheritance
    └── visitNode() - AST visitor callback

BlockLibraryScanner (directory scanner)
    ├── scanDirectory() - Recursive directory traversal
    ├── scanDesktopBlocks() - Built-in library scanner
    └── extractCategory() - Derive categories from paths

BlockMetadata (data structure)
    ├── Basic info (class_name, header_path, category)
    ├── Template parameters (name, type, defaults)
    ├── Constructor parameters (type, name, defaults)
    ├── Input channels (from member Channel<T> fields)
    └── Output channels (from procedure() parameters)
```

### 3. Metadata Extraction Process

1. **Quick Filter**: Check if file contains "BlockBase" inheritance
2. **AST Parsing**: Use libclang to build abstract syntax tree
3. **Class Detection**: Find classes/structs inheriting from `cler::BlockBase`
4. **Template Extraction**: Identify template parameters (typename T, etc.)
5. **Constructor Analysis**: Extract constructor parameters and types
6. **Channel Discovery**:
   - Input channels: Member variables of type `Channel<T>`
   - Output channels: Parameters in `procedure()` method

### 4. Implementation Status

#### ✅ Completed
- Created `block_parser.hpp` with data structures
- Implemented `block_parser.cpp` with libclang integration
- Added AST visitor for metadata extraction
- Created library scanner for directory traversal
- Updated CMakeLists.txt for libclang detection and linking

#### 🔄 In Progress
- Testing parser with real CLER blocks
- Handling edge cases (variadic templates, complex types)

#### 📋 TODO
- Create UI for library management
- Add block search/filter functionality
- Implement drag-and-drop from library to canvas
- Cache parsed metadata for performance
- Handle include path resolution
- Support for custom block libraries

## File Structure

```
cler_flow/
├── src/
│   ├── block_parser.hpp      # Parser interface and data structures
│   ├── block_parser.cpp      # libclang implementation
│   ├── block_library.hpp     # Library management (existing)
│   └── block_library.cpp     # Library UI and storage
└── CMakeLists.txt            # Updated with libclang detection
```

## Usage Example

```cpp
// Scan desktop_blocks directory
BlockLibraryScanner scanner;
auto library = scanner.scanDesktopBlocks();

// Access blocks by category
for (const auto& [category, blocks] : library.blocks_by_category) {
    std::cout << "Category: " << category << std::endl;
    for (const auto* block : blocks) {
        std::cout << "  - " << block->class_name << std::endl;
    }
}

// Parse individual header
BlockParser parser;
BlockMetadata metadata = parser.parseHeader("path/to/block.hpp");
if (metadata.is_valid) {
    // Use metadata to create visual node
}
```

## Integration Points

### With Visual Editor
- Library panel in UI showing categorized blocks
- Drag handler to create new visual nodes
- Template/constructor parameter dialogs

### With Code Generation
- Use extracted metadata for accurate instantiation
- Proper template argument substitution
- Correct constructor parameter passing

## Challenges & Solutions

### Challenge 1: Template Complexity
CLER blocks often use complex templates (variadic, SFINAE, etc.)
**Solution**: Focus on common patterns, provide manual override for edge cases

### Challenge 2: Include Paths
Headers may have dependencies not in standard paths
**Solution**: Allow configurable include paths, use CLER's include directory

### Challenge 3: Performance
Parsing many headers can be slow
**Solution**: Cache metadata, parse on-demand, background scanning

### Challenge 4: Incomplete Types
Some types may not be fully defined in headers
**Solution**: Store type names as strings, validate during code generation

## Testing Strategy

1. **Unit Tests**: Test parser with synthetic headers
2. **Integration Tests**: Parse actual CLER blocks
3. **Edge Cases**: Variadic templates, nested classes, macros
4. **Performance**: Measure parsing time for desktop_blocks

## Future Enhancements

1. **Hot Reload**: Watch headers for changes, update library
2. **Documentation**: Extract comments as tooltips
3. **Validation**: Check for valid CLER block structure
4. **Templates**: Support block templates/presets
5. **Search**: Full-text search in block descriptions
6. **Icons**: Generate or assign icons based on block type

## Dependencies

- libclang (LLVM C API)
- C++17 filesystem API
- ImGui for UI components

## Timeline Estimate

- [x] Parser implementation (2-3 hours) - DONE
- [ ] UI integration (2-3 hours)
- [ ] Testing with real blocks (1-2 hours)
- [ ] Documentation and polish (1 hour)

Total: ~6-9 hours for complete implementation
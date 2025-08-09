# CLER Flow Library Management System

## Features Implemented

### 1. TOML Caching System
- **Location**: `~/.cache/cler-flow/block_library_cache.toml`
- **Validation**: Based on file modification times
- **Performance**: Instant loading on cache hit (25 blocks currently cached)
- **Library**: toml++ header-only library

### 2. Library Organization
- **Hierarchy**: Libraries → Categories → Blocks
- **Structure**:
  ```
  Desktop Blocks (25 blocks)
  ├── Sources
  │   ├── CW Source
  │   ├── Noise Source
  │   └── ...
  ├── Math
  │   ├── Add Block
  │   ├── Multiply
  │   └── ...
  └── Sinks
      └── File Sink
  ```

### 3. User Interface

#### Load Library Button
- Click "Load Library" at the top of the library panel
- Enter path to directory or .hpp file
- Enter library name
- Automatically scans for CLER blocks

#### Right-Click Context Menus

**On Libraries:**
- "Update Library" - Re-scan all blocks in the library
- "Remove Library" - Remove library from the list

**On Blocks:**
- "Update Block" - Re-parse individual block header

### 4. Block Interaction Methods
- **Double-click**: Add block to canvas
- **Drag & Drop**: Drag block to desired position on canvas
- **Right-click**: Update block metadata

### 5. Implementation Files

```
src/
├── block_cache.hpp       # Cache interface
├── block_cache.cpp       # TOML cache implementation
├── block_library.hpp     # Library management interface
├── block_library.cpp     # UI and library logic
├── block_parser.hpp      # libclang parsing interface
├── block_parser.cpp      # C++ header parsing
└── block_spec.hpp        # Block metadata structures
```

### 6. Key Functions

```cpp
// Load a custom library
BlockLibrary::LoadLibrary(path, library_name)

// Update single block
BlockLibrary::UpdateBlock(block_spec)

// Update entire library
BlockLibrary::UpdateLibrary(library_name)

// Cache management
BlockCache::saveToCache(blocks, source_path)
BlockCache::loadFromCache()
BlockCache::isCacheValid(source_path)
```

## Usage

1. **First Run**: Application scans desktop_blocks directory, shows progress bar
2. **Subsequent Runs**: Loads from cache instantly
3. **Import Custom Library**: Click "Load Library", enter path and name
4. **Update Metadata**: Right-click on any block or library to refresh
5. **Add to Canvas**: Double-click or drag blocks to canvas

## Technical Details

- **Threading**: Background parsing with progress tracking
- **Cache Format**: TOML with metadata and timestamps
- **Parser**: libclang for accurate C++ parsing
- **UI Framework**: ImGui with tree views and context menus
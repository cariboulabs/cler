# Cler WASM Demo Infrastructure

WebAssembly build system for Cler DSP framework demos with interactive browser support.

## 🎯 Overview

This infrastructure enables building Cler desktop examples as WebAssembly demos that run directly in browsers with full pthread support, interactive controls, and professional UI.

## 🚧 Current Status - FINAL PTHREAD LINKING ISSUE

**What Works:**
- ✅ Clean CMake Integration: No scripts needed, standard build flow
- ✅ Conditional GUI Compilation: Uses `CLER_BUILD_WASM_EXAMPLES` instead of `EMSCRIPTEN`
- ✅ FetchContent Dependencies: Proper dependency management
- ✅ Build Safety: Prevents common emmake/make mistakes
- ✅ Demo Templates: HTML templates with restart/fullscreen functionality  
- ✅ Demo Gallery: Professional gallery page at `/docs/demos/index.html`
- ✅ CORS Support: SharedArrayBuffer headers for pthread compatibility
- ✅ DRY Architecture: Direct references to original source files
- ✅ GLFW Symbol Resolution: Fixed by using correct flag order and library selection

**Current Issue:**
- 🔧 **Pthread Shared Memory Compilation**: Need `-pthread` at compile time for atomics/bulk-memory

**Error:**
```
wasm-ld: error: --shared-memory is disallowed by ... because it was not compiled with 'atomics' or 'bulk-memory' features.
```

**Solution Applied (needs testing):**
- Added `-pthread` to both `target_compile_options()` and `target_link_options()`
- Applied to both WASM demos and GUI library when `CLER_BUILD_WASM_EXAMPLES=ON`

## 🚀 Quick Start

### Build and Run Demos
```bash
# From project root - automatic emsdk setup
./build_wasm.sh

# Or manual:
mkdir build && cd build
emcmake cmake .. -DCLER_BUILD_WASM_EXAMPLES=ON -DCLER_BUILD_BLOCKS_GUI=ON -DCLER_BUILD_EXAMPLES=OFF
emmake make

# Start demo server
cd ../docs  
python3 simple_server.py          # Start server with CORS headers
# Open http://localhost:8000/demos/
```

## 🏗️ Architecture

### Critical Design Decisions

**1. CMake Subdirectory Order Issue (RESOLVED)**
- **Problem**: `add_subdirectory(desktop_blocks)` at line 65, `add_subdirectory(wasm)` at line 85
- **Issue**: `EMSCRIPTEN` variable undefined when GUI library processes
- **Solution**: Use `CLER_BUILD_WASM_EXAMPLES` flag instead of `EMSCRIPTEN` in GUI library

**2. GLFW Linking Issue (RESOLVED)**
- **Problem**: Mixed desktop GLFW (-lglfw) with Emscripten's built-in GLFW
- **Solution**: Removed `-lglfw` flag, use only `-sUSE_GLFW=3` (no space format)
- **Result**: GLFW symbols now properly resolved

**3. Pthread Shared Memory Issue (NEEDS TESTING)**
- **Problem**: Pthread requires atomics/bulk-memory compilation features
- **Solution**: Added `-pthread` to compile options for WASM targets
- **Status**: Applied but not yet tested

### File Structure
```
/cler/
├── build_wasm.sh                   # Automated build script with emsdk setup
├── wasm/
│   ├── CMakeLists.txt              # WASM build configuration
│   ├── README.md                   # This file
│   └── html/
│       ├── demo_template.html      # Template with restart/fullscreen
│       └── demo_gallery.html       # Professional gallery template
├── docs/
│   ├── demos/
│   │   ├── index.html             # Generated gallery page
│   │   ├── mass_spring_damper/    # Generated demo (after build)
│   │   └── hello_world/           # Generated demo (after build)
│   └── simple_server.py           # Server with CORS headers
└── desktop_blocks/gui/CMakeLists.txt  # Modified for conditional WASM compilation
```

## 🔧 Technical Implementation

### Emscripten Configuration
```cmake
# Key WASM compilation flags (no spaces in -s flags)
-sUSE_GLFW=3                    # GLFW for window management  
-sUSE_WEBGL2=1                  # WebGL2 for rendering
-sUSE_PTHREADS=1                # pthread support
-sPTHREAD_POOL_SIZE=4           # Pre-allocated worker threads
-sPROXY_TO_PTHREAD=1            # Run main() in pthread
-sALLOW_MEMORY_GROWTH=1         # Dynamic memory allocation
-sMODULARIZE=1                  # Module-based loading
-sEXPORT_NAME=ClerDemo          # Global export name
```

### Pthread Support
```cmake
# Both compile and link time flags required
target_compile_options(wasm_target PRIVATE -pthread)
target_link_options(wasm_target PRIVATE -pthread)
```

### Conditional Compilation Pattern
```cmake
# GUI Library - Use build flag not EMSCRIPTEN variable
if(NOT CLER_BUILD_WASM_EXAMPLES)
    # Desktop: find and link system GLFW/OpenGL
    find_package(glfw3 REQUIRED)
    target_link_libraries(blocks_gui PUBLIC glfw OpenGL::GL)
else()
    # WASM: enable pthread, use Emscripten's built-in GLFW/OpenGL
    target_compile_options(blocks_gui PUBLIC -pthread)
    target_link_options(blocks_gui PUBLIC -pthread)
endif()
```

## 🎮 Demo Features

### Interactive Controls
- **🔄 Restart Demo**: Button to reload/restart the application
- **⛶ Fullscreen**: Toggle fullscreen mode for immersive experience
- **📱 Responsive**: Works on desktop and mobile browsers

### Technical Features  
- **pthread Support**: Full Emscripten pthread for Cler flowgraph mode
- **SharedArrayBuffer**: Proper CORS headers for modern browser compatibility
- **ImGui Rendering**: WebGL-based GUI with interactive controls
- **Real-time DSP**: Live signal processing and visualization

## 🎪 Available Demos

### Mass-Spring-Damper System
- **File**: `mass_spring_damper.cpp` → `wasm_mass_spring_damper`
- **Features**: Interactive physics simulation with PID control
- **Controls**: Real-time parameter adjustment, spring visualization
- **Tags**: Physics, Control Systems, PID, Real-time

### Hello World Signal Processing  
- **File**: `hello_world.cpp` → `wasm_hello_world`
- **Features**: Basic signal generation and real-time plotting
- **Controls**: Signal parameter adjustment, plot visualization
- **Tags**: Basics, Signal Generation, Visualization, Flowgraph

## 🔨 Development

### Adding New Demos
1. **Add to CMakeLists.txt**:
   ```cmake
   add_wasm_demo(your_demo_name your_source_file.cpp)
   ```

2. **Executable Name**: Creates `wasm_your_demo_name` 
3. **Generated Files**: 
   - `/docs/demos/your_demo_name/index.html`
   - `/docs/demos/your_demo_name/demo.wasm`
   - `/docs/demos/your_demo_name/demo.js`

## 🔍 Issue Resolution History

### RESOLVED: GLFW Symbol Resolution
- **Issue**: `undefined symbol: glfwInit` and other GLFW functions during linking
- **Root Cause**: CMake subdirectory order - GUI library processes before WASM, so `EMSCRIPTEN` undefined
- **Solution**: Use `CLER_BUILD_WASM_EXAMPLES` flag instead of `EMSCRIPTEN` in GUI library
- **Technical Details**:
  - Root CMakeLists.txt: `add_subdirectory(desktop_blocks)` at line 65
  - Root CMakeLists.txt: `add_subdirectory(wasm)` at line 85
  - When GUI CMakeLists.txt processes, `EMSCRIPTEN` not yet available
  - Fixed by changing GUI library conditionals from `if(NOT EMSCRIPTEN)` to `if(NOT CLER_BUILD_WASM_EXAMPLES)`
  - Also removed redundant `-lglfw` flag, using only Emscripten's built-in GLFW via `-sUSE_GLFW=3`

### CURRENT: Pthread Shared Memory Issue
- **Issue**: `wasm-ld: error: --shared-memory is disallowed ... because it was not compiled with 'atomics' or 'bulk-memory' features`
- **Root Cause**: pthread requires compilation with `-pthread` flag to enable atomics/bulk-memory
- **Solution Applied**: Added `-pthread` to both compile and link options for WASM targets
- **Status**: Applied to both WASM demos and GUI library, needs clean rebuild testing
- **Next Step**: Clean build and test: `rm -rf build && ./build_wasm.sh`

## 🚨 Important Notes

### Browser Requirements
- **Modern Browser**: Chrome 79+, Firefox 79+, Safari 14+, Edge 79+
- **SharedArrayBuffer**: Required for pthread support
- **WebGL2**: For ImGui rendering
- **CORS Headers**: `Cross-Origin-Embedder-Policy: require-corp` and `Cross-Origin-Opener-Policy: same-origin`

### Build Requirements
- **emcmake/emmake**: Must use Emscripten wrappers, not regular cmake/make
- **Clean Builds**: Pthread changes require complete rebuild (`rm -rf build`)
- **emsdk**: Automatically handled by `build_wasm.sh` script

### Performance Notes
- **Warning**: `USE_PTHREADS + ALLOW_MEMORY_GROWTH may run non-wasm code slowly`
- **Recommendation**: Consider disabling memory growth for production if performance critical
- **Threading**: 4 worker threads pre-allocated via `PTHREAD_POOL_SIZE=4`

## 📚 References

- **Emscripten Pthreads**: https://emscripten.org/docs/porting/pthreads.html
- **SharedArrayBuffer**: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/SharedArrayBuffer
- **WebAssembly Atomics**: https://github.com/WebAssembly/design/issues/1271
- **Cler Framework**: ../README.md
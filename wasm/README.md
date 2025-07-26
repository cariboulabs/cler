# Cler WASM Demo Infrastructure

WebAssembly build system for Cler DSP framework demos with interactive browser support.

## 🎯 Overview

This infrastructure enables building Cler desktop examples as WebAssembly demos that run directly in browsers with full pthread support, interactive controls, and professional UI.

## 🚧 Current Status - WORK IN PROGRESS

**What Works:**
- ✅ Clean CMake Integration: No scripts needed, standard build flow
- ✅ Conditional GUI Compilation: Desktop vs WASM library selection
- ✅ FetchContent Dependencies: Proper dependency management
- ✅ Build Safety: Prevents common emmake/make mistakes
- ✅ Demo Templates: HTML templates with restart/fullscreen functionality  
- ✅ Demo Gallery: Professional gallery page at `/docs/demos/index.html`
- ✅ CORS Support: SharedArrayBuffer headers for pthread compatibility
- ✅ DRY Architecture: Direct references to original source files

**Current Issue:**
- 🔧 **RESOLVED: GLFW Symbol Resolution**: Fixed by using `CLER_BUILD_WASM_EXAMPLES` instead of `EMSCRIPTEN` in GUI library

**Build Status:**
- ✅ CMake Configuration: Works correctly
- ✅ Compilation: C++ source compiles successfully  
- ✅ Linking: GLFW symbols now resolved via Emscripten's built-in GLFW

## 🚀 Planned Quick Start (After Cleanup)

### Build and Run Demos
```bash
# From project root - no separate scripts needed
mkdir build
cd build
emcmake cmake .. -DCLER_BUILD_WASM_EXAMPLES=ON -DCLER_BUILD_BLOCKS_GUI=ON
emmake make

# Start demo server
cd ../docs  
python3 simple_server.py          # Start server with CORS headers
# Open http://localhost:8000/demos/
```

## 🏗️ Architecture

### File Structure
```
/cler/
├── wasm/
│   ├── CMakeLists.txt              # WASM build configuration
│   ├── build_wasm.sh               # Automated build script
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
└── apt_list.txt                   # System dependencies
```

### Build Process
1. **emsdk Setup**: Auto-downloads and configures Emscripten SDK
2. **Shell Environment**: Ensures activation persists for build session  
3. **CMake Configuration**: Uses emcmake with proper toolchain
4. **WASM Compilation**: Builds with pthread support and Cler libraries
5. **HTML Generation**: Creates demos with restart/fullscreen controls

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

## 🔧 Technical Details

### Emscripten Configuration
```cmake
# Key WASM compilation flags
-s USE_GLFW=3                    # GLFW for window management
-s USE_WEBGL2=1                  # WebGL2 for rendering
-s USE_PTHREADS=1                # pthread support
-s PTHREAD_POOL_SIZE=4           # Pre-allocated worker threads
-s PROXY_TO_PTHREAD=1            # Run main() in pthread
-s ALLOW_MEMORY_GROWTH=1         # Dynamic memory allocation
-s MODULARIZE=1                  # Module-based loading
-s EXPORT_NAME='ClerDemo'        # Global export name
```

### Browser Requirements
- **Modern Browser**: Chrome 79+, Firefox 79+, Safari 14+, Edge 79+
- **SharedArrayBuffer**: Required for pthread support
- **WebGL2**: For ImGui rendering
- **WebAssembly**: Basic WASM support (universal in modern browsers)

### CORS Headers
```
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Opener-Policy: same-origin
```

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

### Build Script Details
```bash
# Stage 1: Setup emsdk environment
git clone https://github.com/emscripten-core/emsdk.git _deps/emsdk-src
cd _deps/emsdk-src && git pull && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh

# Stage 2: Build with proper environment  
emcmake cmake -DCLER_BUILD_WASM_EXAMPLES=ON -DCLER_BUILD_BLOCKS_GUI=ON
emmake make

# Note: Emscripten flags are applied only during linking, not compilation
```

### Known Issues & Solutions

#### Directory Path Issue
**Problem**: `cd: ../../wasm/build-wasm: No such directory`  
**Cause**: Build script path navigation after emsdk setup
**Solution**: Fix path in build_wasm.sh line 53 from `../../wasm/build-wasm` to `../build-wasm`

#### Emscripten Flags Issue
**Problem**: `clang++: error: unknown argument: '-s USE_PTHREADS=1'` during compilation
**Cause**: Emscripten flags being applied to compilation step where they're not recognized
**Solution**: Remove `target_compile_options()` for Emscripten flags, use only `target_link_options()`

#### Shell Environment 
**Problem**: emsdk activation only works in current shell
**Solution**: Build script handles this by running setup and build in same shell session

#### CMake Toolchain
**Problem**: CMake needs Emscripten toolchain file
**Solution**: emcmake automatically sets correct toolchain after emsdk activation

#### Fixed Issues & Solutions

**🔧 RESOLVED: GLFW Symbol Resolution**
- **Issue**: `undefined symbol: glfwInit` and other GLFW functions during linking
- **Root Cause**: CMake subdirectory order - GUI library processes before WASM, so `EMSCRIPTEN` undefined
- **Solution**: Use `CLER_BUILD_WASM_EXAMPLES` flag instead of `EMSCRIPTEN` in GUI library
- **Technical Details**:
  - Root CMakeLists.txt: `add_subdirectory(desktop_blocks)` at line 65
  - Root CMakeLists.txt: `add_subdirectory(wasm)` at line 85
  - When GUI CMakeLists.txt processes, `EMSCRIPTEN` not yet available
  - Fixed by changing GUI library conditionals from `if(NOT EMSCRIPTEN)` to `if(NOT CLER_BUILD_WASM_EXAMPLES)`
  - Also removed redundant `-lglfw` flag, using only Emscripten's built-in GLFW via `-sUSE_GLFW=3`

**🔧 RESOLVED: WASM Build System Structure**
- **Issue**: build_wasm.sh script complexity, separate build directories
- **Solution**: Integrated into root CMake, standard build flow
- **Reference**: WebGUI Makefile approach with emcc directly

**🔧 RESOLVED: Dependency Management**
- **Issue**: git clone for emsdk, complex path management  
- **Solution**: Use CMake FetchContent for cleaner dependency handling

**🔧 RESOLVED: Default Build Configuration**
- **Issue**: WASM examples enabled by default, causes confusion
- **Solution**: Default OFF, require explicit enabling with proper flags

## 🔍 Debugging

### Build Issues
```bash
# Check emsdk installation
ls _deps/emsdk-src/upstream/emscripten/emcc

# Verify environment 
source _deps/emsdk-src/emsdk_env.sh && which emcc

# Test CMake detection
emcmake cmake --version
```

### Runtime Issues
```bash
# Check browser console for errors
# Verify SharedArrayBuffer support: typeof SharedArrayBuffer !== 'undefined'
# Check CORS headers in Network tab
```

### Server Issues  
```bash
# Test CORS headers
curl -I http://localhost:8000/demos/
# Should show: Cross-Origin-Embedder-Policy: require-corp
```

## 📚 References

- **Emscripten**: https://emscripten.org/docs/getting_started/downloads.html
- **emsdk**: https://github.com/emscripten-core/emsdk  
- **Cler Framework**: ../README.md
- **WebAssembly**: https://webassembly.org/
- **SharedArrayBuffer**: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/SharedArrayBuffer
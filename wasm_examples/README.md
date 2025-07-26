# Cler WASM Examples - Streamlined Mode

Clean, simplified WebAssembly examples using Cler's streamlined execution mode.

## 🎯 Design Philosophy

- **Streamlined Only**: No threading, no pthread complexity
- **Minimal Dependencies**: Core Cler framework only
- **High Performance**: Stack allocation, efficient processing
- **Educational**: Clear examples of DSP concepts

## 🚀 Quick Start

### Prerequisites
```bash
# Install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### Build and Run
```bash
# From this directory
./build.sh

# Start test server
cd build
python3 ../serve.py

# Open browser to http://localhost:8080/
```

## 📁 Examples

### `streamlined_throughput.cpp`
- **Purpose**: Demonstrates basic streamlined flowgraph execution
- **Signal Chain**: Source → Adder → Gain → Throughput Sink
- **Features**: Real-time throughput measurement, efficient processing
- **Learning**: Manual execution control, channel management

## 🏗️ Architecture

### No Threading
- Uses Cler's streamlined mode (manual `procedure()` calls)
- No pthread dependencies or shared memory complexity
- Simpler build process, smaller bundle size

### Stack Allocation
- Fixed-size channel buffers for predictable memory usage
- No dynamic allocation in hot paths
- WASM-optimized for better performance

### Minimal Dependencies
- Links only with `cler::cler` (core framework)
- No GUI, no desktop blocks, no external libraries
- Clean separation from desktop functionality

## 🔧 Adding New Examples

1. **Create source file**: `my_example.cpp`
2. **Add to CMakeLists.txt**: `add_wasm_example(my_example my_example.cpp)`
3. **Build**: `./build.sh`
4. **Test**: Open `http://localhost:8080/my_example.html`

## 🧹 Clean Architecture

This replaces the previous complex WASM build system with:
- ✅ No `CLER_BUILD_WASM_EXAMPLES` conditionals
- ✅ No pthread linking issues
- ✅ No mixed desktop/WASM dependencies
- ✅ Streamlined-first approach
- ✅ Educational focus

Perfect for learning Cler concepts without the complexity of threaded execution.
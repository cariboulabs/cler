#!/bin/bash

# Simple WASM build script for Cler examples
# Usage: ./build_wasm.sh

set -e  # Exit on any error

echo "🚀 Building Cler WASM Examples"
echo "================================"

# Check if emcmake is available, if not set up emsdk
if ! command -v emcmake &> /dev/null; then
    echo "📦 Setting up emsdk (first time only)..."
    
    # Create deps directory if it doesn't exist
    mkdir -p _deps
    
    # Clone or update emsdk
    if [ ! -d "_deps/emsdk" ]; then
        echo "   Cloning emsdk..."
        git clone https://github.com/emscripten-core/emsdk.git _deps/emsdk
    else
        echo "   Updating emsdk..."
        cd _deps/emsdk && git pull && cd ../..
    fi
    
    # Install and activate latest emsdk
    echo "   Installing latest emsdk..."
    cd _deps/emsdk
    ./emsdk update
    ./emsdk install latest
    ./emsdk activate latest
    
    # Source the environment
    echo "   Activating emsdk environment..."
    source ./emsdk_env.sh
    cd ../..
    
    echo "✅ emsdk setup complete"
else
    echo "✅ Emscripten detected: $(emcc --version | head -n1)"
fi

# Create and enter build directory
echo "📁 Setting up build directory..."
rm -rf build
mkdir build
cd build

# Configure with emcmake
echo "⚙️  Configuring CMake with Emscripten..."
emcmake cmake .. \
    -DCLER_BUILD_WASM_EXAMPLES=ON \
    -DCLER_BUILD_BLOCKS_GUI=ON \
    -DCLER_BUILD_EXAMPLES=OFF

# Build with emmake
echo "🔨 Building WASM examples..."
emmake make -j$(nproc)

echo ""
echo "✅ Build completed successfully!"
echo ""
echo "📂 Generated files:"
echo "   - docs/demos/mass_spring_damper/"
echo "   - docs/demos/hello_world/"
echo ""
echo "🌐 To test the demos:"
echo "   cd docs"
echo "   python3 simple_server.py"
echo "   # Open http://localhost:8000/demos/"
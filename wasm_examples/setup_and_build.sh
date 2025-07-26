#!/bin/bash
set -e

echo "=== Cler WASM Setup and Build ==="

#check if /tmp is a valid directory
if [ ! -d "/tmp" ]; then
    mkdir -p /tmp
    echo "Created /tmp directory"
fi

# Check if emsdk already exists in /tmp
if [ ! -d "/tmp/emsdk" ]; then
    echo "📦 Installing Emscripten SDK to /tmp..."
    cd /tmp
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk
    ./emsdk install latest
    ./emsdk activate latest
    cd - > /dev/null
    echo "✅ Emscripten installed"
else
    echo "✅ Emscripten already installed"
fi

# Source the environment and activate
echo "🔧 Setting up Emscripten environment..."
export EMSDK="/tmp/emsdk"
export PATH="$EMSDK:$EMSDK/upstream/emscripten:$PATH"
source /tmp/emsdk/emsdk_env.sh

# Verify emcc and emcmake are available
if ! command -v emcc &> /dev/null; then
    echo "❌ Error: emcc still not found after setup"
    exit 1
fi

if ! command -v emcmake &> /dev/null; then
    echo "❌ Error: emcmake still not found after setup"
    exit 1
fi

echo "✅ Emscripten environment ready"
echo "📋 Emscripten version: $(emcc --version | head -n1)"

# Now build
echo "🔨 Building Cler WASM examples..."

# Clean previous build
echo "Cleaning previous build..."
rm -rf build
mkdir -p build
cd build

# Configure with emscripten - point to wasm_examples directory
echo "⚙️  Configuring with emscripten..."
emcmake cmake ..

# Build
echo "🔨 Building..."
emmake make -j$(nproc)

echo ""
echo "🎉 Build complete!"
echo "Generated files:"
ls -la *.html *.js *.wasm 2>/dev/null || echo "No output files found"

echo ""
echo "🌐 To test:"
echo "  python3 ../serve.py"
echo "  Open http://localhost:8080/streamlined_throughput.html"
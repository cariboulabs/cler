#!/bin/bash
set -e

echo "=== Cler WASM Setup and Build ==="

# Check if emsdk already exists
if [ ! -d "../emsdk" ]; then
    echo "📦 Installing Emscripten SDK..."
    cd ..
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk
    ./emsdk install latest
    ./emsdk activate latest
    cd ../wasm_examples
    echo "✅ Emscripten installed"
else
    echo "✅ Emscripten already installed"
fi

# Source the environment
echo "🔧 Setting up Emscripten environment..."
source ../emsdk/emsdk_env.sh

# Verify emcc is available
if ! command -v emcc &> /dev/null; then
    echo "❌ Error: emcc still not found after setup"
    exit 1
fi

echo "✅ Emscripten environment ready"
echo "📋 Emscripten version: $(emcc --version | head -n1)"

# Now build
echo "🔨 Building Cler WASM examples..."

# Clean previous build
rm -rf build
mkdir -p build
cd build

# Configure with emscripten
echo "⚙️  Configuring with emscripten..."
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release

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
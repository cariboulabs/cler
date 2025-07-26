#!/bin/bash
# Simple test to validate CMake syntax

cd /home/alon/repos/cler

# Create a minimal test build directory
mkdir -p test-cmake-wasm
cd test-cmake-wasm

# Try to configure without Emscripten (should fail gracefully)
echo "Testing CMake syntax with WASM disabled..."
cmake .. -DCLER_BUILD_WASM_EXAMPLES=OFF > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✅ CMake configuration syntax is valid"
else
    echo "❌ CMake configuration has syntax errors"
    cmake .. -DCLER_BUILD_WASM_EXAMPLES=OFF
fi

# Clean up
cd ..
rm -rf test-cmake-wasm
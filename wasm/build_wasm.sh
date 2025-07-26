#!/bin/bash
# Build script for Cler WASM demos

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Cler WASM Demo Builder${NC}"
echo "========================="

# Check if we're in the right directory
if [ ! -f "../CMakeLists.txt" ]; then
    echo -e "${RED}Error: This script must be run from the cler/wasm directory${NC}"
    exit 1
fi

# Go to repo root for build
cd ..

# Stage 1: Download and setup emsdk
echo -e "${YELLOW}Setting up Emscripten SDK...${NC}"

# Download emsdk if not already present
if [ ! -d "_deps/emsdk-src" ]; then
    echo -e "Downloading emsdk..."
    git clone https://github.com/emscripten-core/emsdk.git _deps/emsdk-src
fi

# Setup emsdk in a single shell session
echo -e "Installing and activating Emscripten..."
cd _deps/emsdk-src

# Update emsdk (use git pull since we cloned from GitHub)
if [ -d ".git" ]; then
    echo "Updating emsdk via git pull..."
    git pull
else
    echo "Updating emsdk registry..."
    ./emsdk update
fi

./emsdk install latest
./emsdk activate latest

# Source the environment
source ./emsdk_env.sh

# Go back to build directory
cd ../../wasm
mkdir -p build-wasm
cd build-wasm

# Stage 2: Configure with Emscripten toolchain
echo -e "\n${YELLOW}Configuring with CMake using Emscripten toolchain...${NC}"

emcmake cmake ../.. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCLER_BUILD_WASM_EXAMPLES=ON \
    -DCLER_BUILD_BLOCKS_GUI=ON \
    -DCLER_BUILD_EXAMPLES=OFF \
    -DCLER_BUILD_PERFORMANCE=OFF \
    -DCLER_BUILD_TESTS=OFF \
    -DCLER_BUILD_BLOCKS_LIQUID=OFF \


# Build
echo -e "\n${YELLOW}Building WASM demos...${NC}"
emmake make

# Create assets directory if needed
mkdir -p ../assets

echo -e "\n${GREEN}Build complete!${NC}"
echo -e "Demos have been generated in: ${YELLOW}../../docs/demos/${NC}"

# Set up local server with proper headers for SharedArrayBuffer
echo -e "\n${YELLOW}Setting up local server script...${NC}"
cat > ../serve_demos.py << 'EOF'
#!/usr/bin/env python3
import http.server
import socketserver
import os

class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Headers required for SharedArrayBuffer
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        super().end_headers()

os.chdir('../../docs')
PORT = 8000

with socketserver.TCPServer(("", PORT), MyHTTPRequestHandler) as httpd:
    print(f"Server running at http://localhost:{PORT}/demos/")
    print("Press Ctrl+C to stop")
    httpd.serve_forever()
EOF

chmod +x ../serve_demos.py

echo -e "\n${GREEN}Success! To test the demos locally:${NC}"
echo -e "  cd wasm && ./serve_demos.py"
echo -e "  Then open: ${YELLOW}http://localhost:8000/demos/${NC}"
echo -e "\n${YELLOW}Note:${NC} The demos require SharedArrayBuffer support."
echo -e "The local server script includes the necessary CORS headers."
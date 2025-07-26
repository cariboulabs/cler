# WASM Examples Usage

Clean, simple WebAssembly build for Cler demos.

## Quick Start

```bash
# From project root - no scripts needed
mkdir build
cd build

# Configure with Emscripten (emsdk must be installed and activated)
emcmake cmake .. \
  -DCLER_BUILD_WASM_EXAMPLES=ON \
  -DCLER_BUILD_BLOCKS_GUI=ON \
  -DCLER_BUILD_BLOCKS_LIQUID=ON

# ⚠️  CRITICAL: Use emmake make, NOT regular make
emmake make

# Serve demos with CORS headers
cd ../docs
python3 -c "
import http.server
import socketserver

class CORSHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        super().end_headers()

with socketserver.TCPServer(('', 8000), CORSHandler) as httpd:
    print('Demo server: http://localhost:8000/demos/')
    httpd.serve_forever()
"
```

## What This Builds

- `wasm_mass_spring_damper` - Interactive physics simulation
- `wasm_hello_world` - Basic signal processing demo
- Professional gallery at `/docs/demos/index.html`
- Individual demos with restart/fullscreen controls

## Architecture

Following WebGUI pattern:
- Emscripten provides GLFW/OpenGL via `-s USE_GLFW=3 -lGL`
- Conditional compilation: system libraries for desktop, Emscripten for WASM
- Direct linking to `cler::cler_desktop_blocks` (DRY principle)
- FetchContent for dependencies, no custom scripts

## Troubleshooting

### "undefined symbol: glfwWindowHint" errors
**Problem**: You used `make` instead of `emmake make`
**Solution**: Always use `emmake make` after `emcmake cmake`

### Desktop examples building with WASM
**Problem**: Regular desktop examples being compiled with Emscripten  
**Solution**: This is now prevented automatically (`NOT EMSCRIPTEN` check)

### Missing emsdk
**Problem**: `emcmake` command not found
**Solution**: Install and activate emsdk:
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```
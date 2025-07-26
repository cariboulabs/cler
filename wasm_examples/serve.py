#!/usr/bin/env python3
"""
Simple HTTP server for testing WASM examples locally.
Serves files with proper MIME types for WebAssembly.
"""

import http.server
import socketserver
import os
import sys
from pathlib import Path

class WasmHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Simple headers for local WASM testing
        super().end_headers()

    def guess_type(self, path):
        base_result = super().guess_type(path)
        if isinstance(base_result, tuple):
            mime_type, encoding = base_result
        else:
            mime_type, encoding = base_result, None
            
        if path.endswith('.wasm'):
            return 'application/wasm'
        elif path.endswith('.js'):
            return 'application/javascript'
        return mime_type

def main():
    port = 8080
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    # Stay in current working directory (don't change)
    
    with socketserver.TCPServer(("", port), WasmHandler) as httpd:
        print(f"Serving WASM examples at http://localhost:{port}/")
        print("Available examples:")
        for html_file in Path(".").glob("*.html"):
            print(f"  http://localhost:{port}/{html_file.name}")
        print("\nPress Ctrl+C to stop the server")
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")

if __name__ == "__main__":
    main()
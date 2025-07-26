#!/usr/bin/env python3
import http.server
import socketserver
import os
import threading

class CORSHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        super().end_headers()

# Change to docs directory
os.chdir('/home/alon/repos/cler/docs')

# Start server
PORT = 8000
httpd = socketserver.TCPServer(("", PORT), CORSHTTPRequestHandler)

print(f"🚀 Demo server running at: http://localhost:{PORT}/demos/")
print("Features available:")
print("  ✅ Re-run functionality (restart button)")
print("  ✅ Fullscreen support") 
print("  ✅ Professional demo gallery")
print("  ✅ SharedArrayBuffer CORS headers")
print("\nPress Ctrl+C to stop server")

try:
    httpd.serve_forever()
except KeyboardInterrupt:
    print("\nServer stopped")
    httpd.shutdown()
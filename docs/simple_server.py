#!/usr/bin/env python3
import http.server
import socketserver
import os

class CORSHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        super().end_headers()

os.chdir('/home/alon/repos/cler/docs')
httpd = socketserver.TCPServer(("", 8000), CORSHandler)
print("Server running at http://localhost:8000/demos/")
httpd.serve_forever()
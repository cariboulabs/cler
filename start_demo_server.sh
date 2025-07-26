#!/bin/bash
cd /home/alon/repos/cler/docs
echo "Starting demo server at http://localhost:8000/demos/"
echo "Press Ctrl+C to stop"
python3 -m http.server 8000
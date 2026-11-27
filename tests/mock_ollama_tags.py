#!/usr/bin/env python3
"""Verify the model resolver uses the actual Ollama /api/tags endpoint."""
import json
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/api/tags":
            self.send_error(404)
            return
        payload=json.dumps({"models":[
            {"name":"qwen3:8b"},
            {"name":"adam1:1.0"},
        ]}).encode()
        self.send_response(200)
        self.send_header("Content-Type","application/json")
        self.send_header("Content-Length",str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def log_message(self,*_): pass

if __name__=="__main__":
    server=ThreadingHTTPServer(("127.0.0.1",0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        endpoint=f"http://127.0.0.1:{server.server_port}"
        subprocess.run([sys.argv[1],endpoint],check=True,timeout=10)
    finally:
        server.shutdown()
        server.server_close()

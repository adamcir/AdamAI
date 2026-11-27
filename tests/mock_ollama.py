#!/usr/bin/env python3
"""Deterministic local mock of Ollama tool-call loop (no external networking)."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import subprocess
import sys
import threading

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        req = json.loads(self.rfile.read(length))
        assert req.get("tools") and req["tools"][0]["function"]["name"] == "web_fetch"
        if len(req["messages"]) <= 2:
            reply = {"message": {"role": "assistant", "tool_calls": [
                {"type": "function", "function": {"index": 0, "name": "web_fetch",
                  "arguments": {"url": "https://127.0.0.1/private"}}}]}, "done": False}
        else:
            assert any(m.get("role") == "tool" and "failed" in m.get("content", "")
                       for m in req["messages"]), req
            reply = {"message": {"role": "assistant", "content": "This is my final answer"}, "done": False}
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.end_headers()
        self.wfile.write((json.dumps(reply) + "\n").encode())
        self.wfile.flush()
        self.wfile.write(b'{"done":true}\n')
        self.wfile.flush()

    def log_message(self, *_):
        pass

if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        subprocess.run([sys.argv[1], str(server.server_port)], check=True, timeout=15)
    finally:
        server.shutdown()
        server.server_close()

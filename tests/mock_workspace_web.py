#!/usr/bin/env python3
"""Mock Ollama function calls: workspace switch approved, web URL denied."""
import json
import subprocess
import sys
import threading
import tempfile
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# The executable creates a temporary directory itself. We need to know its
# second workspace. The first call supplies it via the test-only user prompt?
# Instead use the environment-mounted path as a shell argument from C below.
TARGET = None

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        req = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        tools = {t["function"]["name"] for t in req.get("tools", [])}
        assert {"web_fetch","request_workspace","read_file","write_file"} <= tools
        results = [m for m in req["messages"] if m.get("role") == "tool"]
        if len(results)==0:
            reply={"message":{"role":"assistant","tool_calls":[{
                "type":"function","function":{"name":"request_workspace",
                "arguments":{"path":TARGET}}}]},"done":False}
        elif len(results)==1:
            assert "User approved workspace change" in results[0]["content"], results
            reply={"message":{"role":"assistant","tool_calls":[{
                "type":"function","function":{"name":"web_fetch",
                "arguments":{"url":"https://example.com/?private=do-not-send"}}}]},"done":False}
        else:
            assert len(results)==2,results
            assert "Web request rejected by user" in results[1]["content"], results
            reply={"message":{"role":"assistant","content":"This is my final answer"},"done":False}
        raw=(json.dumps(reply)+"\n"+json.dumps({"done":True})+"\n").encode()
        self.send_response(200)
        self.send_header("Content-Type","application/x-ndjson")
        self.end_headers()
        self.wfile.write(raw)
        self.wfile.flush()
    def log_message(self,*_): pass

if __name__=="__main__":
    server=ThreadingHTTPServer(("127.0.0.1",0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        # mock binary creates path internally: let it communicate path through
        # a preparatory subprocess argument supplied via a stable temp fixture.
        import os
        with tempfile.TemporaryDirectory(prefix="adamai-mock-workspace-") as root:
            TARGET=str(Path(root))
            subprocess.run([sys.argv[1],str(server.server_port),TARGET],check=True,timeout=20)
    finally:
        server.shutdown()
        server.server_close()

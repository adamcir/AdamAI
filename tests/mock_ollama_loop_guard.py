#!/usr/bin/env python3
"""Force repeated identical tool requests, require final response without tools."""
import json
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer

calls=[]
class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        req=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        calls.append(req)
        has_tools=bool(req.get("tools"))
        if has_tools:
            reply={"message":{"role":"assistant","tool_calls":[{
                "type":"function","function":{"name":"web_fetch",
                "arguments":{"url":"https://127.0.0.1/repeated"}}}]},"done":False}
        else:
            assert len(calls) <= 4, len(calls)
            reply={"message":{"role":"assistant",
                "content":"Unable to verify the place from the available sources."},
                "done":False}
        payload=(json.dumps(reply)+"\n"+json.dumps({"done":True})+"\n").encode()
        self.send_response(200)
        self.send_header("Content-Type","application/x-ndjson")
        self.send_header("Content-Length",str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def log_message(self,*_): pass

if __name__=="__main__":
    server=ThreadingHTTPServer(("127.0.0.1",0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        subprocess.run([sys.argv[1],str(server.server_port)],check=True,timeout=12)
        assert len(calls)==4,len(calls)
        assert all(req.get("tools") for req in calls[:3])
        assert not calls[-1].get("tools")
    finally:
        server.shutdown()
        server.server_close()

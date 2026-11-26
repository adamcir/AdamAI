#!/usr/bin/env python3
"""Local Ollama mock validating FS tool protocol and mandatory user approval."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import subprocess
import sys
import threading

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        data = self.rfile.read(int(self.headers['Content-Length']))
        req = json.loads(data)
        names = {t['function']['name'] for t in req.get('tools', [])}
        assert {'list_directory','read_file','write_file'} <= names, names
        assert 'web_fetch' not in names, 'FS mode must disable web access'
        messages = req['messages']
        tool_msgs = [m for m in messages if m.get('role') == 'tool']
        if not tool_msgs:
            calls = [
                {'type': 'function', 'function': {'name':'list_directory','arguments': {'path':'.'}}},
                {'type': 'function', 'function': {'name':'read_file','arguments': {'path':'note.txt'}}}
            ]
            reply = {'message': {'role':'assistant','tool_calls':calls}, 'done':False}
        elif len(tool_msgs) == 2:
            assert 'note.txt' in tool_msgs[0]['content']
            assert 'original' in tool_msgs[1]['content']
            reply = {'message': {'role':'assistant','tool_calls':[{
                'type':'function','function':{'name':'write_file','arguments':{
                    'path':'note.txt','content':'updated by AdamAI\n'}}
            }]},'done':False}
        else:
            assert len(tool_msgs)==3, len(tool_msgs)
            assert 'Approved and wrote' in tool_msgs[2]['content'], tool_msgs[2]
            reply={'message':{'role':'assistant','content':'This is my final answer'},'done':False}
        self.send_response(200)
        self.send_header('Content-Type','application/x-ndjson')
        self.end_headers()
        self.wfile.write((json.dumps(reply)+'\n').encode())
        self.wfile.write(b'{"done":true}\n')
        self.wfile.flush()

    def log_message(self, *_):
        pass

if __name__ == '__main__':
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    try:
        subprocess.run([sys.argv[1],str(server.server_port)],check=True,timeout=20)
    finally:
        server.shutdown()
        server.server_close()

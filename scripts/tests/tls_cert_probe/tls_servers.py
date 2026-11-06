# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Local HTTPS servers, one per certificate case, for the viewer TLS probe.
Each answers any POST/GET with a minimal XML-RPC methodResponse.
Exits by itself after LIFETIME seconds.  Usage: tls_servers.py <pki dir> [lifetime]
The certificates come from run_probe.ps1 (generated per run, never committed)."""
import http.server, ssl, sys, threading, os, time
PKI = sys.argv[1]
LIFETIME = float(sys.argv[2]) if len(sys.argv) > 2 else 300
CASES = {"valid": 18441, "expired": 18442, "untrusted": 18443, "wronghost": 18444, "selfsigned": 18445}
BODY = (b'<?xml version="1.0"?><methodResponse><params><param><value><struct>'
        b'<member><name>probe</name><value><string>ok</string></value></member>'
        b'</struct></value></param></params></methodResponse>')
class H(http.server.BaseHTTPRequestHandler):
    def _reply(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        if n: self.rfile.read(n)
        self.send_response(200)
        self.send_header("Content-Type", "text/xml")
        self.send_header("Content-Length", str(len(BODY)))
        self.end_headers(); self.wfile.write(BODY)
    do_POST = do_GET = _reply
    def log_message(self, fmt, *a):
        sys.stderr.write("%s %s\n" % (self.server.case, fmt % a))
for case, port in CASES.items():
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), H)
    srv.case = case
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(os.path.join(PKI, case + "-chain.pem"), os.path.join(PKI, case + ".key"))
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
print("serving", CASES, flush=True)
time.sleep(LIFETIME)

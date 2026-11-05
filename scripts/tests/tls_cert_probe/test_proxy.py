# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Minimal local proxies for the TLS qualification: an HTTP proxy (CONNECT
tunnels and absolute-URI GET/POST) and a SOCKS5 proxy (no authentication,
CONNECT, and a UDP ASSOCIATE that answers with a bound port, which is all the
viewer's startup handshake needs). 127.0.0.1 only. Every request is logged to
stderr as "<kind> <target>". Exits by itself after LIFETIME seconds.

Usage: test_proxy.py <http port> <socks port> [lifetime]
"""
import select
import socket
import socketserver
import struct
import sys
import threading
import time
import urllib.parse

HTTP_PORT, SOCKS_PORT = int(sys.argv[1]), int(sys.argv[2])
LIFETIME = float(sys.argv[3]) if len(sys.argv) > 3 else 300


def log(*a):
    sys.stderr.write(" ".join(str(x) for x in a) + "\n")
    sys.stderr.flush()


def pipe(a, b):
    socks = [a, b]
    try:
        while True:
            r, _, x = select.select(socks, [], socks, 60)
            if x or not r:
                return
            for s in r:
                data = s.recv(65536)
                if not data:
                    return
                (b if s is a else a).sendall(data)
    except OSError:
        return


class HttpProxy(socketserver.StreamRequestHandler):
    def handle(self):
        line = self.rfile.readline().decode("latin-1").strip()
        if not line:
            return
        method, target, _ = line.split(" ", 2)
        headers = []
        while True:
            h = self.rfile.readline()
            if h in (b"\r\n", b"\n", b""):
                break
            headers.append(h)
        if method == "CONNECT":
            host, port = target.rsplit(":", 1)
            log("HTTP-CONNECT", target)
            up = socket.create_connection((host, int(port)), timeout=20)
            self.wfile.write(b"HTTP/1.1 200 Connection established\r\n\r\n")
            self.wfile.flush()
            pipe(self.connection, up)
            up.close()
            return
        u = urllib.parse.urlsplit(target)
        log("HTTP-" + method, target)
        up = socket.create_connection((u.hostname, u.port or 80), timeout=20)
        path = (u.path or "/") + ("?" + u.query if u.query else "")
        up.sendall(("%s %s HTTP/1.1\r\n" % (method, path)).encode())
        for h in headers:
            if not h.lower().startswith(b"proxy-"):
                up.sendall(h)
        up.sendall(b"\r\n")
        pipe(self.connection, up)
        up.close()


class Socks5(socketserver.StreamRequestHandler):
    def handle(self):
        c = self.connection
        ver, n = c.recv(2)
        methods = c.recv(n)
        if 0 not in methods:
            c.sendall(b"\x05\xff")
            return
        c.sendall(b"\x05\x00")
        ver, cmd, _, atyp = c.recv(4)
        if atyp == 1:
            host = socket.inet_ntoa(c.recv(4))
        elif atyp == 3:
            host = c.recv(c.recv(1)[0]).decode()
        else:
            host = socket.inet_ntop(socket.AF_INET6, c.recv(16))
        port = struct.unpack("!H", c.recv(2))[0]
        if cmd == 1:
            log("SOCKS-CONNECT", "%s:%d" % (host, port))
            try:
                up = socket.create_connection((host, port), timeout=20)
            except OSError:
                c.sendall(b"\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00")
                return
            c.sendall(b"\x05\x00\x00\x01" + socket.inet_aton("127.0.0.1") + struct.pack("!H", 0))
            pipe(c, up)
            up.close()
        elif cmd == 3:
            udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp.bind(("127.0.0.1", 0))
            log("SOCKS-UDP-ASSOCIATE", "%s:%d" % (host, port))
            c.sendall(b"\x05\x00\x00\x01" + socket.inet_aton("127.0.0.1") + struct.pack("!H", udp.getsockname()[1]))
            # keep the association open while the control connection lives
            try:
                while c.recv(1):
                    pass
            except OSError:
                pass
            udp.close()
        else:
            c.sendall(b"\x05\x07\x00\x01\x00\x00\x00\x00\x00\x00")


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


for port, handler in ((HTTP_PORT, HttpProxy), (SOCKS_PORT, Socks5)):
    s = Server(("127.0.0.1", port), handler)
    threading.Thread(target=s.serve_forever, daemon=True).start()
log("proxies on", HTTP_PORT, SOCKS_PORT)
time.sleep(LIFETIME)

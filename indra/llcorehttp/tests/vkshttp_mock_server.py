#!/usr/bin/env python3
"""\
@file   vkshttp_mock_server.py
@brief  Scripted local HTTP/1.1 server for the llcorehttp parity test.

Runs the test executable given on the command line while serving a
scripted set of behaviours on a loopback port, and records every request
it receives (arrival time, connection, exact request head, body) as JSON
lines so the test can compare the request sequences of the HTTP layer
against the recorded pre-port behaviour. No external network is used.

Usage: vkshttp_mock_server.py [--log FILE] [--serve PORT] [--] EXE [ARGS...]

With --serve PORT it only serves (for the benches and for manual runs).
Otherwise it picks a free port, exports LL_TEST_PORT and VKS_HTTP_MOCK_LOG
to the child, runs it and exits with the child's return code.

Paths served (every other path is a 404):
  /mark/<name>            200, empty; a marker in the log
  /ok/                    200, an LLSD XML body
  /large/<n>              n deterministic bytes; honours Range (206/416)
  /echo/                  200, the request body echoed with its content type
  /status/<code>          that status code, a text body (none for 204/304)
  /flaky/<k>/<n>[/retry/<s>]  the first n requests of key k are 503
                          (with Retry-After: s when given), then 200
  /timeout/               never answered; the connection stays open
  /partial/<n>            Content-Length n, n/2 bytes, then a clean close
  /reset/<n>              Content-Length n, n/2 bytes, then an RST
  /redirect/<n>           302 to /redirect/<n-1>, ending at /ok/
  /slow/<ms>              200 after ms milliseconds
  /headers/               200, request headers reflected as X-Reflect-<name>
  /cookie/set             200 with Set-Cookie: vks=1
  /cookie/get             200, body "cookie:<Cookie header>"
  /nolength/              200 without Content-Length, then close
  /badrange/              206 with an inconsistent Content-Range
  /chunked/               200 with a chunked body
  /gzip/                  200 with a gzip-encoded body

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Vulkanstorm Viewer Source Code
Copyright (C) 2026, Vulkanstorm Viewer Project
Licensed under the GNU Lesser General Public License, version 2.1.
$/LicenseInfo$
"""

import base64
import gzip
import json
import os
import socket
import struct
import subprocess
import sys
import threading
import time

LLSD_OK = (b'<?xml version="1.0" ?><llsd><map><key>reply</key>'
           b'<string>success</string><key>status</key><integer>200</integer>'
           b'</map></llsd>')


def pattern(n):
    """Deterministic body of n bytes."""
    return bytes(((i * 7 + 13) ^ (i >> 8)) & 0xFF for i in range(n))


class Recorder:
    def __init__(self, path):
        self.lock = threading.Lock()
        self.t0 = time.perf_counter()
        self.f = open(path, "a", encoding="utf-8") if path else None
        self.conn_id = 0

    def now_ms(self):
        return (time.perf_counter() - self.t0) * 1000.0

    def next_conn(self):
        with self.lock:
            self.conn_id += 1
            return self.conn_id

    def emit(self, record):
        if self.f is None:
            return
        line = json.dumps(record, separators=(",", ":"))
        with self.lock:
            self.f.write(line + "\n")
            self.f.flush()


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.flaky = {}

    def flaky_count(self, key):
        with self.lock:
            n = self.flaky.get(key, 0)
            self.flaky[key] = n + 1
            return n


def parse_head(head):
    lines = head.split(b"\r\n")
    request_line = lines[0].decode("latin-1")
    parts = request_line.split(" ")
    if len(parts) != 3:
        raise ValueError("bad request line %r" % request_line)
    method, target, version = parts
    headers = []
    for line in lines[1:]:
        if not line:
            continue
        name, _, value = line.decode("latin-1").partition(":")
        headers.append([name, value.strip()])
    return method, target, version, headers


def header(headers, name):
    name = name.lower()
    for n, v in headers:
        if n.lower() == name:
            return v
    return None


class Connection(threading.Thread):
    def __init__(self, sock, recorder, state):
        super().__init__(daemon=True)
        self.sock = sock
        self.rec = recorder
        self.state = state
        self.conn = recorder.next_conn()
        self.seq = 0
        self.buf = b""

    def recv_until(self, marker):
        while marker not in self.buf:
            data = self.sock.recv(65536)
            if not data:
                return False
            self.buf += data
        return True

    def recv_exact(self, n):
        while len(self.buf) < n:
            data = self.sock.recv(65536)
            if not data:
                return False
            self.buf += data
        return True

    def read_request(self):
        if not self.recv_until(b"\r\n\r\n"):
            return None
        end = self.buf.index(b"\r\n\r\n")
        raw_head = self.buf[:end + 4]
        self.buf = self.buf[end + 4:]
        method, target, version, headers = parse_head(raw_head[:-4])
        body = b""
        te = header(headers, "transfer-encoding")
        cl = header(headers, "content-length")
        chunked_upload = False
        if te and "chunked" in te.lower():
            chunked_upload = True
            while True:
                if not self.recv_until(b"\r\n"):
                    return None
                line, _, rest = self.buf.partition(b"\r\n")
                self.buf = rest
                size = int(line.split(b";")[0].strip() or b"0", 16)
                if size == 0:
                    if not self.recv_until(b"\r\n"):
                        return None
                    self.buf = self.buf[self.buf.index(b"\r\n") + 2:]
                    break
                if not self.recv_exact(size + 2):
                    return None
                body += self.buf[:size]
                self.buf = self.buf[size + 2:]
        elif cl:
            n = int(cl)
            if not self.recv_exact(n):
                return None
            body = self.buf[:n]
            self.buf = self.buf[n:]
        return dict(method=method, target=target, version=version,
                    headers=headers, body=body, raw_head=raw_head,
                    chunked_upload=chunked_upload)

    def run(self):
        try:
            while True:
                req = self.read_request()
                if req is None:
                    break
                self.seq += 1
                t = self.rec.now_ms()
                record = dict(ev="req", t=round(t, 3), conn=self.conn, seq=self.seq,
                              method=req["method"], target=req["target"],
                              version=req["version"], headers=req["headers"],
                              body=base64.b64encode(req["body"]).decode("ascii"),
                              body_len=len(req["body"]),
                              raw_head=base64.b64encode(req["raw_head"]).decode("ascii"),
                              chunked_upload=req["chunked_upload"])
                if req["target"].startswith("/mark/"):
                    record["marker"] = req["target"][len("/mark/"):]
                self.rec.emit(record)
                keep = self.respond(req)
                self.rec.emit(dict(ev="rsp", t=round(self.rec.now_ms(), 3), conn=self.conn, seq=self.seq))
                if not keep:
                    break
        except (ConnectionError, OSError, ValueError):
            pass
        finally:
            try:
                self.sock.close()
            except OSError:
                pass

    def send_response(self, status, reason, headers, body, content_length=True, close=False):
        lines = ["HTTP/1.1 %d %s" % (status, reason)]
        if content_length and status not in (204, 304):
            lines.append("Content-Length: %d" % len(body))
        if close:
            lines.append("Connection: close")
        for name, value in headers:
            lines.append("%s: %s" % (name, value))
        head = ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1")
        self.sock.sendall(head + (b"" if status in (204, 304) else body))

    def respond(self, req):
        target = req["target"]
        method = req["method"]
        parts = [p for p in target.split("?")[0].split("/") if p]
        kind = parts[0] if parts else ""
        if kind == "mark":
            self.send_response(200, "OK", [("Content-Type", "text/plain")], b"")
        elif kind == "ok":
            self.send_response(200, "OK", [("Content-Type", "application/llsd+xml")], LLSD_OK)
        elif kind == "large":
            n = int(parts[1])
            full = pattern(n)
            rng = header(req["headers"], "range")
            if rng and rng.startswith("bytes="):
                first, _, last = rng[len("bytes="):].partition("-")
                first = int(first)
                last = int(last) if last else n - 1
                if first >= n:
                    self.send_response(416, "Range Not Satisfiable",
                                       [("Content-Range", "bytes */%d" % n),
                                        ("Content-Type", "text/plain")], b"out of range")
                else:
                    last = min(last, n - 1)
                    self.send_response(206, "Partial Content",
                                       [("Content-Range", "bytes %d-%d/%d" % (first, last, n)),
                                        ("Content-Type", "application/octet-stream")],
                                       full[first:last + 1])
            else:
                self.send_response(200, "OK", [("Content-Type", "application/octet-stream")], full)
        elif kind == "echo":
            ctype = header(req["headers"], "content-type") or "text/plain"
            self.send_response(200, "OK", [("Content-Type", ctype), ("X-Echo-Method", method)],
                               req["body"])
        elif kind == "status":
            code = int(parts[1])
            self.send_response(code, "Status", [("Content-Type", "text/plain")],
                               ("status %d" % code).encode("ascii"))
        elif kind == "flaky":
            key = parts[1]
            limit = int(parts[2])
            retry = None
            if len(parts) >= 5 and parts[3] == "retry":
                retry = parts[4]
            count = self.state.flaky_count(key)
            if count < limit:
                hdrs = [("Content-Type", "text/plain")]
                if retry is not None:
                    hdrs.append(("Retry-After", retry))
                self.send_response(503, "Service Unavailable", hdrs, b"flaky")
            else:
                self.send_response(200, "OK", [("Content-Type", "application/llsd+xml")], LLSD_OK)
        elif kind == "timeout":
            # Hold the connection until the client gives up.
            try:
                while self.sock.recv(65536):
                    pass
            except OSError:
                pass
            return False
        elif kind in ("partial", "reset"):
            n = int(parts[1])
            full = pattern(n)
            head = ("HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: application/octet-stream\r\n\r\n" % n).encode("latin-1")
            self.sock.sendall(head + full[:n // 2])
            time.sleep(0.05)
            if kind == "reset":
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            return False
        elif kind == "redirect":
            n = int(parts[1])
            location = "/ok/" if n <= 1 else "/redirect/%d" % (n - 1)
            self.send_response(302, "Found", [("Location", location), ("Content-Type", "text/plain")], b"moved")
        elif kind == "slow":
            time.sleep(int(parts[1]) / 1000.0)
            self.send_response(200, "OK", [("Content-Type", "application/llsd+xml")], LLSD_OK)
        elif kind == "headers":
            hdrs = [("X-Reflect-" + name, value) for name, value in req["headers"]]
            hdrs.append(("Content-Type", "text/plain"))
            self.send_response(200, "OK", hdrs, b"reflected")
        elif kind == "cookie":
            if parts[1] == "set":
                self.send_response(200, "OK", [("Set-Cookie", "vks=1; Path=/"), ("Content-Type", "text/plain")], b"set")
            else:
                c = header(req["headers"], "cookie") or ""
                self.send_response(200, "OK", [("Content-Type", "text/plain")], ("cookie:" + c).encode("latin-1"))
        elif kind == "nolength":
            self.send_response(200, "OK", [("Content-Type", "text/plain")], b"no length body",
                               content_length=False, close=True)
            return False
        elif kind == "badrange":
            self.send_response(206, "Partial Content",
                               [("Content-Range", "bytes 10-5/100"), ("Content-Type", "text/plain")], b"bad")
        elif kind == "chunked":
            body = pattern(1000)
            chunks = b""
            for i in range(0, len(body), 300):
                piece = body[i:i + 300]
                chunks += ("%x\r\n" % len(piece)).encode("ascii") + piece + b"\r\n"
            chunks += b"0\r\n\r\n"
            head = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Type: application/octet-stream\r\n\r\n"
            self.sock.sendall(head + chunks)
        elif kind == "gzip":
            body = gzip.compress(pattern(2000), mtime=0)
            self.send_response(200, "OK", [("Content-Encoding", "gzip"), ("Content-Type", "application/octet-stream")], body)
        else:
            self.send_response(404, "Not Found", [("Content-Type", "text/plain")], b"not found")
        return True


def serve(port, recorder, state, stop):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(64)
    srv.settimeout(0.2)
    port = srv.getsockname()[1]
    stop["port"] = port
    stop["ready"].set()
    while not stop["flag"]:
        try:
            sock, _ = srv.accept()
        except socket.timeout:
            continue
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        Connection(sock, recorder, state).start()
    srv.close()


def main(argv):
    log = None
    port = 0
    serve_only = False
    i = 0
    while i < len(argv):
        if argv[i] == "--log":
            log = argv[i + 1]
            i += 2
        elif argv[i] == "--serve":
            port = int(argv[i + 1])
            serve_only = True
            i += 2
        elif argv[i] == "--":
            i += 1
            break
        else:
            break
    cmd = argv[i:]
    if log is None:
        log = os.environ.get("VKS_HTTP_MOCK_LOG")
    temp_log = None
    if log is None and not serve_only:
        import tempfile
        fd, temp_log = tempfile.mkstemp(prefix="vkshttp-mock-", suffix=".jsonl")
        os.close(fd)
        log = temp_log
    recorder = Recorder(log)
    state = State()
    stop = dict(flag=False, ready=threading.Event(), port=None)
    thread = threading.Thread(target=serve, args=(port, recorder, state, stop), daemon=True)
    thread.start()
    stop["ready"].wait()
    if serve_only:
        print("serving on 127.0.0.1:%d" % stop["port"], flush=True)
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        return 0
    env = dict(os.environ)
    env["LL_TEST_PORT"] = str(stop["port"])
    if log:
        env["VKS_HTTP_MOCK_LOG"] = log
    rc = subprocess.call(cmd, env=env)
    stop["flag"] = True
    thread.join()
    if recorder.f is not None:
        recorder.f.close()
    if temp_log is not None:
        try:
            os.remove(temp_log)
        except OSError:
            pass
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

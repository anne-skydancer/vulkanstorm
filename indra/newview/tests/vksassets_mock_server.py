#!/usr/bin/env python3
"""\
@file   vksassets_mock_server.py
@brief  Scripted local HTTP/1.1 server for the asset pipeline parity tests.

Runs the test executable given on the command line while serving textures
and assets from a directory the test configures, on a loopback port, and
records every request it receives (arrival time, connection, target, the
Range header) as JSON lines so the test can compare the request sequences
of the texture fetcher and the asset fetch against the recorded pre-port
behaviour. No external network is used.

Usage: vksassets_mock_server.py [--log FILE] [--serve PORT] [--] EXE [ARGS...]

With --serve PORT it only serves (for manual runs). Otherwise it picks a
free port, exports LL_TEST_PORT and VKS_HTTP_MOCK_LOG to the child, runs it
and exits with the child's return code.

Paths served (every other path is a 404):
  /config/?dir=<path>           sets the directory served; 200
  /mark/<name>                  200, empty; a marker in the log
  /texture/?texture_id=<uuid>   <dir>/<uuid>.j2c with Range support (206,
                                416), scripted by the first two hex digits
                                of the uuid:
                                  4f  404 always
                                  50  503 always
                                  51  503 on the first request, then normal
                                  60  at most 3000 bytes of any range
                                  61  the range served starts 97 bytes early
                                  70  200 with the whole file, Range ignored
                                  41  416 for any range not starting at 0
  /asset/?<type>_id=<uuid>      <dir>/<uuid>.<type>, whole (200), 404 when
                                absent; a uuid starting with 50 is a 503

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Vulkanstorm Viewer Source Code
Copyright (C) 2026, Vulkanstorm Viewer Project
Licensed under the GNU Lesser General Public License, version 2.1.
$/LicenseInfo$
"""

import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.parse


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
        self.dir = None
        self.counts = {}

    def count(self, key):
        with self.lock:
            n = self.counts.get(key, 0)
            self.counts[key] = n + 1
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


def parse_range(rng, size):
    """The first and last byte of a bytes=first-last header for a body of
    size bytes, or None for an unsatisfiable range."""
    if not rng or not rng.startswith("bytes="):
        return None
    first, _, last = rng[len("bytes="):].partition("-")
    first = int(first) if first else 0
    last = int(last) if last else size - 1
    if first >= size:
        return None
    return first, min(last, size - 1)


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
        cl = header(headers, "content-length")
        if cl:
            n = int(cl)
            if not self.recv_exact(n):
                return None
            body = self.buf[:n]
            self.buf = self.buf[n:]
        return dict(method=method, target=target, version=version, headers=headers, body=body)

    def run(self):
        try:
            while True:
                req = self.read_request()
                if req is None:
                    break
                self.seq += 1
                record = dict(ev="req", t=round(self.rec.now_ms(), 3), conn=self.conn, seq=self.seq,
                              method=req["method"], target=req["target"],
                              range=header(req["headers"], "range") or "")
                keep, status = self.respond(req)
                record["status"] = status
                self.rec.emit(record)
                if not keep:
                    break
        except (ConnectionError, OSError, ValueError):
            pass
        finally:
            try:
                self.sock.close()
            except OSError:
                pass

    def send_response(self, status, reason, headers, body):
        lines = ["HTTP/1.1 %d %s" % (status, reason)]
        lines.append("Content-Length: %d" % len(body))
        for name, value in headers:
            lines.append("%s: %s" % (name, value))
        head = ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1")
        self.sock.sendall(head + body)
        return status

    def not_found(self):
        return self.send_response(404, "Not Found", [("Content-Type", "text/plain")], b"not found")

    def load(self, name):
        if not self.state.dir:
            return None
        path = os.path.join(self.state.dir, name)
        try:
            with open(path, "rb") as f:
                return f.read()
        except OSError:
            return None

    def respond(self, req):
        target = req["target"]
        path, _, query = target.partition("?")
        parts = [p for p in path.split("/") if p]
        kind = parts[0] if parts else ""
        params = urllib.parse.parse_qs(query)
        if kind == "config":
            d = params.get("dir", [""])[0]
            with self.state.lock:
                self.state.dir = d
            return True, self.send_response(200, "OK", [("Content-Type", "text/plain")], b"ok")
        if kind == "mark":
            return True, self.send_response(200, "OK", [("Content-Type", "text/plain")], b"")
        if kind == "texture":
            uuid = params.get("texture_id", [""])[0]
            behaviour = uuid[:2]
            if behaviour == "4f":
                return True, self.not_found()
            if behaviour == "50":
                return True, self.send_response(503, "Service Unavailable", [("Content-Type", "text/plain")], b"busy")
            if behaviour == "51" and self.state.count(uuid) == 0:
                return True, self.send_response(503, "Service Unavailable", [("Content-Type", "text/plain")], b"busy")
            data = self.load(uuid + ".j2c")
            if data is None:
                return True, self.not_found()
            size = len(data)
            rng = header(req["headers"], "range")
            ctype = [("Content-Type", "image/x-j2c")]
            if behaviour == "70" or not rng:
                return True, self.send_response(200, "OK", ctype, data)
            r = parse_range(rng, size)
            if r is None:
                return True, self.send_response(416, "Range Not Satisfiable",
                                                [("Content-Range", "bytes */%d" % size)] + ctype, b"")
            first, last = r
            if behaviour == "41" and first > 0:
                return True, self.send_response(416, "Range Not Satisfiable",
                                                [("Content-Range", "bytes */%d" % size)] + ctype, b"")
            if behaviour == "60":
                last = min(last, first + 3000 - 1)
            if behaviour == "61":
                first = max(0, first - 97)
            body = data[first:last + 1]
            return True, self.send_response(206, "Partial Content",
                                            [("Content-Range", "bytes %d-%d/%d" % (first, last, size))] + ctype, body)
        if kind == "asset":
            for key, values in params.items():
                if key.endswith("_id"):
                    uuid = values[0]
                    atype = key[:-3]
                    if uuid[:2] == "50":
                        return True, self.send_response(503, "Service Unavailable", [("Content-Type", "text/plain")], b"busy")
                    data = self.load(uuid + "." + atype)
                    if data is None:
                        return True, self.not_found()
                    return True, self.send_response(200, "OK", [("Content-Type", "application/octet-stream")], data)
            return True, self.not_found()
        return True, self.not_found()


def serve(port, recorder, state, stop):
    # The listening socket: a failure to bind or listen is reported to the
    # wrapper, which would otherwise wait for readiness forever.
    try:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(64)
        srv.settimeout(0.2)
        stop["port"] = srv.getsockname()[1]
    except OSError as e:
        stop["error"] = e
        stop["ready"].set()
        return
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
        fd, temp_log = tempfile.mkstemp(prefix="vksassets-mock-", suffix=".jsonl")
        os.close(fd)
        log = temp_log
    recorder = Recorder(log)
    state = State()
    stop = dict(flag=False, ready=threading.Event(), port=None, error=None)
    thread = threading.Thread(target=serve, args=(port, recorder, state, stop), daemon=True)
    thread.start()
    if not stop["ready"].wait(30) or stop["error"] is not None:
        sys.stderr.write("vksassets_mock_server: cannot listen on 127.0.0.1:%d: %s\n"
                         % (port, stop["error"] or "no readiness within 30 s"))
        return 2
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

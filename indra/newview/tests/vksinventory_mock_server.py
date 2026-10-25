#!/usr/bin/env python3
"""\
@file   vksinventory_mock_server.py
@brief  Loopback AIS (InventoryAPIv3 / LibraryAPIv3) and legacy inventory
        capability server for the inventory fetch parity test (recorder R3b).

Runs the test executable given on the command line while serving an
inventory the test describes, on a loopback port the operating system
assigns, and records every request it receives (arrival order, connection,
exact request head, body, status and response) as JSON lines, so the test
can compare what the viewer sends with the recorded pre-port behaviour.
No external network is used.

Usage: vksinventory_mock_server.py [--log FILE] [--serve] [--] EXE [ARGS...]
(--log FILE is truncated first.)

The listening socket is bound to 127.0.0.1 port 0 in the main thread before
anything else runs; a bind or listen failure exits with status 2 at once
(there is no readiness wait that could hang). The port is exported to the
child as LL_TEST_PORT and the log path as VKS_HTTP_MOCK_LOG. With --serve it
only serves and prints the port (manual runs).

Capabilities (paths under /cap/):
  inv   InventoryAPIv3      lib   LibraryAPIv3
  fid2  FetchInventoryDescendents2     fld2  FetchLibDescendents2
  fi2   FetchInventory2     fl2   FetchLib2
  cic   CreateInventoryCategory

AIS (inv and lib):
  GET    /category/<id|current>/children?depth=D[&children=a,b,...]
  GET    /category/<id|current>/links       GET /category/<id>/categories?depth=D
  GET    /item/<id>                         GET /orphans
  POST   /category/<parent>?tid=T           create categories / links / items
  PUT    /category/<id>/links?tid=T         slam a folder's links
  PATCH  /category/<id>, /item/<id>         update
  DELETE /category/<id>, /item/<id>, /category/<id>/children
  COPY   /category/<src>?tid=T[,depth=0]    (lib) with Destination: <dest>

Control (logged as "ctl" events, never part of a scenario's request log):
  POST /control/world   the inventory, in the line format the test writes
  POST /control/mode    key=value lines (below); resets the scripted counters
  GET  /control/mark/<name>

Modes: fail_first=<n> fail_status=<code> (the first n AIS requests fail with
that status and an empty LLSD map body), fail_subset=<n> (the first n subset
fetches fail with 404), delay_ms=<ms> (every response is held that long),
legacy_error=<n> (a legacy folder request naming an e0... folder gets a
200-with-error body n times).

Behaviour scripted by the first two hex digits of the folder or item id in
the request path (AIS) or body (legacy):
  f3  403 for a full children fetch at depth > 0 (LLSD error body); a
      subset fetch at depth > 0 naming an f3 or f4 child is refused the
      same way; a subset of an f3 / f4 folder's own children is served
  f4  403 for every full children fetch; a legacy folder request naming
      one: 403
  52  503 for the first two requests of that id, then normal
  5e  503 with Retry-After: 1 for a children fetch at depth > 0 without a
      children= list, always
  53  503 for the first write request of that id, then normal
  e0  legacy folder requests: 200 with an "error" map (see legacy_error)
  4b  the first subset fetch naming a 4b... child: 404 (once in a world)
  4c  every children fetch of that folder: 404

Held replies (mode hold=1): each request is logged on arrival ("arr" with
held=true) and answered only when POST /control/release names its seq (one
per line) or "all"; the reply is computed at release, so writes change the
world in release order. A request whose head does not parse is logged as a
"bad" event (the test fails on it).

Response shapes follow what the viewer accepts (llaisapi.cpp AISUpdate,
llinventorymodelbackgroundfetch.cpp BGFolderHttpHandler); the real servers'
shapes at the depth limit and in error bodies are not known (see the
fixture README).

$LicenseInfo:firstyear=2026&license=viewerlgpl$
Vulkanstorm Viewer Source Code
Copyright (C) 2026, Vulkanstorm Viewer Project
Licensed under the GNU Lesser General Public License, version 2.1.
$/LicenseInfo$
"""

import base64
import hashlib
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
import xml.etree.ElementTree as ET

NULL = "00000000-0000-0000-0000-000000000000"
LOG_BODY_LIMIT = 2_000_000

FT_CURRENT_OUTFIT = 46
AT_LINK = 24
AT_LINK_FOLDER = 25


# ---- LLSD XML ------------------------------------------------------------

def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def x_str(s):
    return "<string>%s</string>" % esc(s) if s else "<string />"


def x_uuid(u):
    return "<uuid>%s</uuid>" % u


def x_int(n):
    return "<integer>%d</integer>" % n


def x_bool(b):
    return "<boolean>%s</boolean>" % ("true" if b else "false")


def x_map(pairs):
    if not pairs:
        return "<map />"
    return "<map>" + "".join("<key>%s</key>%s" % (esc(k), v) for k, v in pairs) + "</map>"


def x_array(values):
    if not values:
        return "<array />"
    return "<array>" + "".join(values) + "</array>"


def x_doc(inner):
    return ('<?xml version="1.0" ?>\n<llsd>\n' + inner + "\n</llsd>\n").encode("utf-8")


def parse_llsd(data):
    """LLSD XML -> Python (dict, list, str, int, bool, float, None); uuids as
    lowercase strings."""
    if not data:
        return None
    root = ET.fromstring(data)
    if root.tag != "llsd" or len(root) == 0:
        return None

    def conv(e):
        t = e.tag
        text = e.text or ""
        if t == "map":
            out = {}
            kids = list(e)
            for i in range(0, len(kids) - 1, 2):
                out[kids[i].text or ""] = conv(kids[i + 1])
            return out
        if t == "array":
            return [conv(k) for k in e]
        if t == "string" or t == "uri" or t == "date":
            return text
        if t == "uuid":
            return text.strip().lower() or NULL
        if t == "integer":
            return int(text.strip() or "0")
        if t == "real":
            return float(text.strip() or "0")
        if t == "boolean":
            v = text.strip().lower()
            return v in ("1", "true")
        if t == "binary":
            return base64.b64decode(text)
        return None
    return conv(root[0])


# ---- the inventory --------------------------------------------------------

def unq(s):
    return urllib.parse.unquote(s)


class World:
    def __init__(self):
        self.folders = {}
        self.items = {}
        self.child_f = {}
        self.child_i = {}
        self.agent = NULL
        self.libowner = NULL
        self.root = NULL
        self.libroot = NULL
        self.created = 0

    def load(self, text):
        self.__init__()
        for line in text.splitlines():
            if not line or line.startswith("#"):
                continue
            p = line.split(" ")
            k = p[0]
            if k == "agent":
                self.agent = p[1]
            elif k == "libowner":
                self.libowner = p[1]
            elif k == "root":
                self.root = p[1]
            elif k == "libroot":
                self.libroot = p[1]
            elif k == "F":
                self.add_folder(dict(id=p[1], parent=p[2], type=int(p[3]), version=int(p[4]), owner=p[5],
                                     name=unq(p[6])))
            elif k == "I":
                self.add_item(dict(kind="I", id=p[1], parent=p[2], owner=p[3], creator=p[4], asset=p[5],
                                   type=int(p[6]), inv_type=int(p[7]), flags=int(p[8]), created=int(p[9]),
                                   base=int(p[10]), owner_mask=int(p[11]), group_mask=int(p[12]),
                                   everyone=int(p[13]), next=int(p[14]), sale_type=int(p[15]),
                                   sale_price=int(p[16]), name=unq(p[17]), desc=unq(p[18])))
            elif k == "L":
                self.add_item(dict(kind="L", id=p[1], parent=p[2], owner=p[3], linked=p[4], type=int(p[5]),
                                   inv_type=int(p[6]), created=int(p[7]), name=unq(p[8]), desc=unq(p[9])))

    def add_folder(self, f):
        self.folders[f["id"]] = f
        self.child_f.setdefault(f["parent"], []).append(f["id"])

    def add_item(self, it):
        self.items[it["id"]] = it
        self.child_i.setdefault(it["parent"], []).append(it["id"])

    def new_id(self, tag):
        self.created += 1
        return "%s-0000-4000-8000-%012x" % (tag, self.created)

    def cof(self):
        for fid, f in self.folders.items():
            if f["type"] == FT_CURRENT_OUTFIT:
                return fid
        return None

    def descendents(self, fid):
        return len(self.child_f.get(fid, [])) + len(self.child_i.get(fid, []))

    def bump(self, fid):
        f = self.folders.get(fid)
        if f is None:
            return None
        f["version"] += 1
        return f["version"]

    def remove_item(self, iid):
        it = self.items.pop(iid, None)
        if it is not None:
            lst = self.child_i.get(it["parent"], [])
            if iid in lst:
                lst.remove(iid)
        return it

    def remove_tree(self, fid, cats, items, include_self=True):
        for c in list(self.child_f.get(fid, [])):
            self.remove_tree(c, cats, items, True)
        for i in list(self.child_i.get(fid, [])):
            self.remove_item(i)
            items.append(i)
        if include_self:
            f = self.folders.pop(fid, None)
            if f is not None:
                lst = self.child_f.get(f["parent"], [])
                if fid in lst:
                    lst.remove(fid)
            self.child_f.pop(fid, None)
            self.child_i.pop(fid, None)
            cats.append(fid)


# ---- rendering ------------------------------------------------------------

def item_pairs(w, it):
    if it["kind"] == "L":
        pairs = [("item_id", x_uuid(it["id"])), ("parent_id", x_uuid(it["parent"])),
                 ("linked_id", x_uuid(it["linked"])), ("type", x_int(it["type"])),
                 ("inv_type", x_int(it["inv_type"])), ("name", x_str(it["name"])),
                 ("desc", x_str(it["desc"])), ("created_at", x_int(it["created"])),
                 ("agent_id", x_uuid(it["owner"]))]
        target = w.items.get(it["linked"])
        if target is not None and target["kind"] == "I":
            pairs.append(("_embedded", x_map([("item", x_map(item_pairs(w, target)))])))
        elif it["linked"] in w.folders:
            pairs.append(("_embedded", x_map([("category", cat_xml(w, it["linked"], -1))])))
        return pairs
    perms = x_map([("creator_id", x_uuid(it["creator"])), ("owner_id", x_uuid(it["owner"])),
                   ("last_owner_id", x_uuid(it["owner"])), ("group_id", x_uuid(NULL)),
                   ("is_owner_group", x_bool(False)), ("base_mask", x_int(it["base"])),
                   ("owner_mask", x_int(it["owner_mask"])), ("group_mask", x_int(it["group_mask"])),
                   ("everyone_mask", x_int(it["everyone"])), ("next_owner_mask", x_int(it["next"]))])
    sale = x_map([("sale_type", x_int(it["sale_type"])), ("sale_price", x_int(it["sale_price"]))])
    return [("item_id", x_uuid(it["id"])), ("parent_id", x_uuid(it["parent"])),
            ("asset_id", x_uuid(it["asset"])), ("type", x_int(it["type"])),
            ("inv_type", x_int(it["inv_type"])), ("flags", x_int(it["flags"])),
            ("name", x_str(it["name"])), ("desc", x_str(it["desc"])),
            ("created_at", x_int(it["created"])), ("permissions", perms), ("sale_info", sale),
            ("agent_id", x_uuid(it["owner"]))]


def cat_pairs(w, fid):
    f = w.folders[fid]
    return [("category_id", x_uuid(fid)), ("parent_id", x_uuid(f["parent"])), ("name", x_str(f["name"])),
            ("type_default", x_int(f["type"])), ("agent_id", x_uuid(f["owner"])),
            ("version", x_int(f["version"]))]


def cat_xml(w, fid, depth, subset=None, links_only=False):
    """A category; its _embedded content when depth >= 0 (every child
    category at depth - 1). subset: only these child categories, no items or
    links. links_only: only the links, each with its target embedded."""
    pairs = cat_pairs(w, fid)
    if depth >= 0:
        emb = []
        if links_only:
            links = [i for i in w.child_i.get(fid, []) if w.items[i]["kind"] == "L"]
            emb.append(("links", x_map([(i, x_map(item_pairs(w, w.items[i]))) for i in links])))
        else:
            cats = [c for c in w.child_f.get(fid, []) if subset is None or c in subset]
            emb.append(("categories", x_map([(c, cat_xml(w, c, depth - 1)) for c in cats])))
            if subset is None:
                kids = w.child_i.get(fid, [])
                emb.append(("links", x_map([(i, x_map(item_pairs(w, w.items[i]))) for i in kids
                                            if w.items[i]["kind"] == "L"])))
                emb.append(("items", x_map([(i, x_map(item_pairs(w, w.items[i]))) for i in kids
                                            if w.items[i]["kind"] == "I"])))
        pairs.append(("_embedded", x_map(emb)))
    return x_map(pairs)


def legacy_cat(w, fid):
    f = w.folders[fid]
    return x_map([("category_id", x_uuid(fid)), ("parent_id", x_uuid(f["parent"])), ("name", x_str(f["name"])),
                  ("type_default", x_int(f["type"]))])


def error_body(code, text):
    return x_doc(x_map([("error_code", x_int(code)), ("error_description", x_str(text))]))


# ---- the server --------------------------------------------------------------

class Recorder:
    def __init__(self, path):
        self.lock = threading.Lock()
        self.t0 = time.perf_counter()
        self.f = open(path, "w", encoding="utf-8") if path else None
        self.conn_id = 0
        self.seq = 0

    def now_ms(self):
        return (time.perf_counter() - self.t0) * 1000.0

    def next_conn(self):
        with self.lock:
            self.conn_id += 1
            return self.conn_id

    def arrival(self, record):
        with self.lock:
            self.seq += 1
            record["seq"] = self.seq
            self._write(record)
            return self.seq

    def emit(self, record):
        with self.lock:
            self._write(record)

    def _write(self, record):
        if self.f is None:
            return
        self.f.write(json.dumps(record, separators=(",", ":")) + "\n")
        self.f.flush()


class State:
    def __init__(self):
        self.lock = threading.Lock()
        # Replies held until released (mode hold=1): seq -> Event.
        self.held = {}
        self.held_lock = threading.Lock()
        self.world = World()
        self.mode = {}
        self.counts = {}
        self.ais_requests = 0
        self.subset_requests = 0

    def count(self, key):
        n = self.counts.get(key, 0)
        self.counts[key] = n + 1
        return n


class Response:
    def __init__(self, status, reason, body=b"", ctype="application/llsd+xml", headers=None):
        self.status = status
        self.reason = reason
        self.body = body
        self.ctype = ctype
        self.headers = headers or []


def ok(body):
    return Response(200, "OK", x_doc(body))


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


def parse_query(query):
    """depth=0&children=a,b and tid=T,depth=0 (the comma form CopyLibraryCategory sends)."""
    out = {}
    for part in query.split("&") if query else []:
        k, _, v = part.partition("=")
        if k == "tid" and "," in v:
            v, _, rest = v.partition(",")
            for extra in rest.split(","):
                ek, _, ev = extra.partition("=")
                out[ek] = ev
        out[k] = v
    return out


class Connection(threading.Thread):
    def __init__(self, sock, recorder, state):
        super().__init__(daemon=True)
        self.sock = sock
        self.rec = recorder
        self.state = state
        self.conn = recorder.next_conn()
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
        try:
            method, target, version, headers = parse_head(raw_head[:-4])
        except ValueError as e:
            # Logged, never silently dropped: the test fails on it.
            self.rec.emit(dict(ev="bad", conn=self.conn, t_in=round(self.rec.now_ms(), 3),
                               head=base64.b64encode(raw_head).decode("ascii"), error=str(e)))
            raise
        t_in = self.rec.now_ms()
        expect = header(headers, "expect")
        if expect and expect.lower() == "100-continue":
            self.sock.sendall(b"HTTP/1.1 100 Continue\r\n\r\n")
        body = b""
        te = header(headers, "transfer-encoding")
        cl = header(headers, "content-length")
        if te and "chunked" in te.lower():
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
        return dict(method=method, target=target, version=version, headers=headers, body=body,
                    raw_head=raw_head, t_in=t_in)

    def run(self):
        try:
            while True:
                req = self.read_request()
                if req is None:
                    break
                control = req["target"].startswith("/control/")
                record = dict(ev="ctl" if control else "req", conn=self.conn, t_in=round(req["t_in"], 3),
                              method=req["method"], target=req["target"])
                if not control:
                    hold = self.state.mode.get("hold", "0") == "1"
                    ev = threading.Event()
                    if hold:
                        # Registered before the arrival is logged, so a release
                        # the test sends after reading the arrival finds it.
                        with self.state.held_lock:
                            seq = self.rec.arrival(dict(ev="arr", conn=self.conn, t_in=round(req["t_in"], 3),
                                                        method=req["method"], target=req["target"], held=True))
                            self.state.held[seq] = ev
                        ev.wait()
                    else:
                        seq = self.rec.arrival(dict(ev="arr", conn=self.conn, t_in=round(req["t_in"], 3),
                                                    method=req["method"], target=req["target"]))
                    record["seq"] = seq
                try:
                    rsp = self.respond(req)
                except Exception as e:  # a mock bug: answer 500 and log it
                    rsp = Response(500, "Mock Error", ("mock error: %r" % e).encode("utf-8"), "text/plain")
                    record["mock_error"] = repr(e)
                delay = int(self.state.mode.get("delay_ms", "0") or 0)
                if delay and not control:
                    time.sleep(delay / 1000.0)
                # The record is written before the response is sent, so the
                # client never sees a response whose record is not yet logged.
                record.update(t_out=round(self.rec.now_ms(), 3),
                              head=base64.b64encode(req["raw_head"]).decode("ascii"),
                              body=base64.b64encode(req["body"]).decode("ascii"),
                              status=rsp.status, rlen=len(rsp.body),
                              rsha=hashlib.sha256(rsp.body).hexdigest(), rtype=rsp.ctype,
                              rheaders=rsp.headers)
                if len(rsp.body) <= LOG_BODY_LIMIT:
                    record["rbody"] = base64.b64encode(rsp.body).decode("ascii")
                self.rec.emit(record)
                self.send(rsp)
        except (ConnectionError, OSError, ValueError):
            pass
        finally:
            try:
                self.sock.close()
            except OSError:
                pass

    def send(self, rsp):
        lines = ["HTTP/1.1 %d %s" % (rsp.status, rsp.reason), "Content-Length: %d" % len(rsp.body)]
        if rsp.ctype:
            lines.append("Content-Type: %s" % rsp.ctype)
        for name, value in rsp.headers:
            lines.append("%s: %s" % (name, value))
        self.sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + rsp.body)

    # ---- dispatch ----------------------------------------------------------

    def respond(self, req):
        path, _, query = req["target"].partition("?")
        parts = [p for p in path.split("/") if p]
        st = self.state
        with st.lock:
            if parts[:1] == ["control"]:
                return self.control(req, parts[1:])
            if len(parts) >= 2 and parts[0] == "cap":
                cap = parts[1]
                rest = parts[2:]
                if cap in ("inv", "lib"):
                    return self.ais(req, cap, rest, parse_query(query))
                if cap in ("fid2", "fld2"):
                    return self.legacy_folders(req)
                if cap in ("fi2", "fl2"):
                    return self.legacy_items(req)
                if cap == "cic":
                    return self.create_category_cap(req)
        return Response(404, "Not Found", b"not found", "text/plain")

    def control(self, req, parts):
        st = self.state
        if parts[:1] == ["world"]:
            st.world.load(req["body"].decode("utf-8"))
            st.counts = {}
            st.ais_requests = 0
            st.subset_requests = 0
            return Response(200, "OK", b"ok", "text/plain")
        if parts[:1] == ["mode"]:
            with st.held_lock:
                for ev in st.held.values():
                    ev.set()
                st.held.clear()
            st.mode = {}
            for line in req["body"].decode("utf-8").splitlines():
                k, _, v = line.partition("=")
                if k:
                    st.mode[k.strip()] = v.strip()
            st.counts = {}
            st.ais_requests = 0
            st.subset_requests = 0
            return Response(200, "OK", b"ok", "text/plain")
        if parts[:1] == ["mark"]:
            return Response(200, "OK", b"", "text/plain")
        if parts[:1] == ["release"]:
            # Body: the seq numbers to release, one per line, or "all".
            body = req["body"].decode("utf-8").split()
            released = 0
            with st.held_lock:
                keys = list(st.held.keys()) if body == ["all"] else [int(x) for x in body]
                for k in keys:
                    ev = st.held.pop(k, None)
                    if ev is not None:
                        ev.set()
                        released += 1
            return Response(200, "OK", str(released).encode("ascii"), "text/plain")
        return Response(404, "Not Found", b"not found", "text/plain")

    # ---- AIS -----------------------------------------------------------------

    def ais(self, req, cap, rest, q):
        st = self.state
        w = st.world
        method = req["method"]
        kind = rest[0] if rest else ""
        oid = rest[1] if len(rest) > 1 else ""
        sub = rest[2] if len(rest) > 2 else ""
        if kind == "category" and oid == "current":
            oid = w.cof() or NULL
        depth = int(q.get("depth", "0") or 0) if "depth" in q else None
        has_children = "children" in q
        st.ais_requests += 1

        # scripted failures
        fail_first = int(st.mode.get("fail_first", "0") or 0)
        if fail_first and st.ais_requests <= fail_first:
            code = int(st.mode.get("fail_status", "404"))
            return Response(code, "Scripted Failure", x_doc("<map />"))
        if has_children and method == "GET":
            st.subset_requests += 1
            if st.subset_requests <= int(st.mode.get("fail_subset", "0") or 0):
                return Response(404, "Scripted Failure", error_body(404, "subset failed"))
        p = oid[:2]
        is_children = method == "GET" and kind == "category" and sub == "children"
        if is_children and has_children and (depth or 0) > 0:
            # A subset naming a folder that is too large is refused whole.
            if any(c[:2] in ("f3", "f4") for c in q["children"].split(",")):
                return Response(403, "Forbidden", error_body(403, "too many descendents"))
        if is_children and has_children and any(c[:2] == "4b" for c in q["children"].split(",")) \
                and st.count("4b-subset") < 1:
            # The first subset naming a 4b... child fails (c02): by content,
            # not by arrival order.
            return Response(404, "Scripted Failure", error_body(404, "subset failed"))
        if is_children and p == "4c":
            return Response(404, "Not Found", error_body(404, "category not found"))
        if is_children and p == "f4" and not has_children:
            return Response(403, "Forbidden", error_body(403, "too many descendents"))
        if is_children and p == "f3" and (depth or 0) > 0 and not has_children:
            return Response(403, "Forbidden", error_body(403, "too many descendents"))
        if p == "52" and method == "GET" and st.count("52:" + oid) < 2:
            return Response(503, "Service Unavailable", b"busy", "text/plain")
        if is_children and p == "5e" and (depth or 0) > 0 and not has_children:
            return Response(503, "Service Unavailable", b"busy", "text/plain", [("Retry-After", "1")])
        if p == "53" and method != "GET" and st.count("53:" + oid) < 1:
            return Response(503, "Service Unavailable", b"busy", "text/plain")

        body = parse_llsd(req["body"]) if req["body"] else None
        if method == "GET":
            if kind == "orphans":
                orphans = [i for i in w.child_i.get(NULL, [])]
                return ok(x_map([("_embedded", x_map([("items", x_map([(i, x_map(item_pairs(w, w.items[i])))
                                                                       for i in orphans]))]))]))
            if kind == "item":
                it = w.items.get(oid)
                if it is None:
                    return Response(404, "Not Found", error_body(404, "item not found"))
                return ok(x_map(item_pairs(w, it)))
            if kind == "category":
                if oid not in w.folders:
                    return Response(404, "Not Found", error_body(404, "category not found"))
                if sub == "children":
                    subset = None
                    if has_children:
                        subset = set(q["children"].split(","))
                    return ok(cat_xml(w, oid, depth if depth is not None else 0, subset))
                if sub == "links":
                    return ok(cat_xml(w, oid, 0, None, True))
                if sub == "categories":
                    return ok(cat_xml(w, oid, depth if depth is not None else 0))
            return Response(404, "Not Found", error_body(404, "not found"))
        if method == "POST" and kind == "category":
            return self.ais_create(oid, body)
        if method == "PUT" and kind == "category" and sub == "links":
            return self.ais_slam(oid, body)
        if method == "PATCH" and kind == "item":
            it = w.items.get(oid)
            if it is None:
                return Response(404, "Not Found", error_body(404, "item not found"))
            if isinstance(body, dict):
                if "name" in body:
                    it["name"] = body["name"]
                if "desc" in body:
                    it["desc"] = body["desc"]
            v = w.bump(it["parent"])
            pairs = item_pairs(w, it) + [("_updated_items", x_array([x_uuid(oid)])),
                                         ("_updated_category_versions", x_map([(it["parent"], x_int(v))]))]
            return ok(x_map(pairs))
        if method == "PATCH" and kind == "category":
            f = w.folders.get(oid)
            if f is None:
                return Response(404, "Not Found", error_body(404, "category not found"))
            if isinstance(body, dict):
                if "name" in body:
                    f["name"] = body["name"]
                if "type_default" in body:
                    f["type"] = int(body["type_default"])
            v = w.bump(oid)
            pairs = cat_pairs(w, oid) + [("_updated_categories", x_array([x_uuid(oid)])),
                                         ("_updated_category_versions", x_map([(oid, x_int(v))]))]
            return ok(x_map(pairs))
        if method == "DELETE" and kind == "item":
            it = w.remove_item(oid)
            if it is None:
                return Response(410, "Gone", b"")
            v = w.bump(it["parent"])
            return ok(x_map([("_category_items_removed", x_array([x_uuid(oid)])),
                             ("_updated_category_versions", x_map([(it["parent"], x_int(v))]))]))
        if method == "DELETE" and kind == "category":
            f = w.folders.get(oid)
            if f is None:
                return Response(410, "Gone", b"")
            cats, items = [], []
            w.remove_tree(oid, cats, items, include_self=(sub != "children"))
            target = oid if sub == "children" else f["parent"]
            v = w.bump(target)
            return ok(x_map([("_categories_removed", x_array([x_uuid(c) for c in cats])),
                             ("_category_items_removed", x_array([x_uuid(i) for i in items])),
                             ("_updated_category_versions", x_map([(target, x_int(v))]))]))
        if method == "COPY" and kind == "category":
            dest = (header(req["headers"], "destination") or "").lower()
            return self.ais_copy(oid, dest, q.get("depth") != "0")
        return Response(400, "Bad Request", error_body(400, "unsupported"))

    def ais_create(self, parent, body):
        w = self.state.world
        if parent not in w.folders or not isinstance(body, dict):
            return Response(404, "Not Found", error_body(404, "parent not found"))
        created_c, created_i = [], []
        emb_c, emb_l = [], []
        for c in body.get("categories", []) or []:
            cid = w.new_id("c1000000")
            w.add_folder(dict(id=cid, parent=parent, type=int(c.get("type_default", -1)), version=1,
                              owner=w.agent, name=c.get("name", "")))
            created_c.append(cid)
            emb_c.append((cid, cat_xml(w, cid, 0)))
        for l in body.get("links", []) or []:
            lid = w.new_id("b1000000")
            w.add_item(dict(kind="L", id=lid, parent=parent, owner=w.agent, linked=l.get("linked_id", NULL),
                            type=int(l.get("type", AT_LINK)), inv_type=int(l.get("inv_type", -1)),
                            created=1700000000 + w.created, name=l.get("name", ""), desc=l.get("desc", "")))
            created_i.append(lid)
            emb_l.append((lid, x_map(item_pairs(w, w.items[lid]))))
        v = w.bump(parent)
        pairs = []
        if created_c:
            pairs.append(("_created_categories", x_array([x_uuid(c) for c in created_c])))
        if created_i:
            pairs.append(("_created_items", x_array([x_uuid(i) for i in created_i])))
        pairs.append(("_updated_category_versions", x_map([(parent, x_int(v))])))
        emb = []
        if emb_c:
            emb.append(("categories", x_map(emb_c)))
        if emb_l:
            emb.append(("links", x_map(emb_l)))
        pairs.append(("_embedded", x_map(emb)))
        return ok(x_map(pairs))

    def ais_slam(self, fid, body):
        w = self.state.world
        if fid not in w.folders:
            return Response(404, "Not Found", error_body(404, "category not found"))
        removed = [i for i in w.child_i.get(fid, []) if w.items[i]["kind"] == "L"]
        for i in removed:
            w.remove_item(i)
        created, emb = [], []
        for l in body if isinstance(body, list) else []:
            lid = w.new_id("b1000000")
            w.add_item(dict(kind="L", id=lid, parent=fid, owner=w.agent, linked=l.get("linked_id", NULL),
                            type=int(l.get("type", AT_LINK)), inv_type=int(l.get("inv_type", -1)),
                            created=1700000000 + w.created, name=l.get("name", ""), desc=l.get("desc", "")))
            created.append(lid)
            emb.append((lid, x_map(item_pairs(w, w.items[lid]))))
        v = w.bump(fid)
        return ok(x_map([("_created_items", x_array([x_uuid(i) for i in created])),
                         ("_category_items_removed", x_array([x_uuid(i) for i in removed])),
                         ("_updated_category_versions", x_map([(fid, x_int(v))])),
                         ("_embedded", x_map([("links", x_map(emb))]))]))

    def ais_copy(self, src, dest, subfolders):
        w = self.state.world
        if src not in w.folders or dest not in w.folders:
            return Response(404, "Not Found", error_body(404, "category not found"))
        created_c, created_i = [], []

        def copy(fid, parent):
            f = w.folders[fid]
            nid = w.new_id("c2000000")
            w.add_folder(dict(id=nid, parent=parent, type=f["type"], version=1, owner=w.agent, name=f["name"]))
            created_c.append(nid)
            for i in list(w.child_i.get(fid, [])):
                it = dict(w.items[i])
                it["id"] = w.new_id("a2000000")
                it["parent"] = nid
                it["owner"] = w.agent
                w.add_item(it)
                created_i.append(it["id"])
            if subfolders:
                for c in list(w.child_f.get(fid, [])):
                    copy(c, nid)
            return nid

        top = copy(src, dest)
        v = w.bump(dest)
        return ok(x_map([("_created_categories", x_array([x_uuid(c) for c in created_c])),
                         ("_created_items", x_array([x_uuid(i) for i in created_i])),
                         ("_updated_category_versions", x_map([(dest, x_int(v))])),
                         ("_embedded", x_map([("categories", x_map([(top, cat_xml(w, top, 50))]))]))]))

    # ---- legacy capabilities ---------------------------------------------------

    def legacy_folders(self, req):
        st = self.state
        w = st.world
        body = parse_llsd(req["body"]) or {}
        folders = (body.get("folders") or []) if isinstance(body, dict) else []
        ids = [str(f.get("folder_id", NULL)).lower() for f in folders]
        if any(i.startswith("f4") for i in ids):
            return Response(403, "Forbidden", b"too large", "text/plain")
        err_n = int(st.mode.get("legacy_error", "0") or 0)
        for i in ids:
            if i.startswith("e0") and st.count("e0:" + i) < err_n:
                return ok(x_map([("error", x_map([("identifier", x_str("Development")),
                                                   ("message", x_str("scripted 200-with-error"))]))]))
        out, bad = [], []
        for i in ids:
            if i == NULL:
                kids = w.child_i.get(NULL, [])
                out.append(x_map([("agent_id", x_uuid(w.agent)), ("owner_id", x_uuid(w.agent)),
                                  ("folder_id", x_uuid(NULL)), ("version", x_int(0)),
                                  ("descendents", x_int(len(kids))), ("categories", x_array([])),
                                  ("items", x_array([x_map(item_pairs(w, w.items[k])) for k in kids]))]))
                continue
            f = w.folders.get(i)
            if f is None:
                bad.append(x_map([("folder_id", x_uuid(i)), ("error", x_str("Unknown"))]))
                continue
            out.append(x_map([("agent_id", x_uuid(w.agent)), ("owner_id", x_uuid(f["owner"])),
                              ("folder_id", x_uuid(i)), ("version", x_int(f["version"])),
                              ("descendents", x_int(w.descendents(i))),
                              ("categories", x_array([legacy_cat(w, c) for c in w.child_f.get(i, [])])),
                              ("items", x_array([x_map(item_pairs(w, w.items[k])) for k in w.child_i.get(i, [])]))]))
        pairs = [("folders", x_array(out))]
        if bad:
            pairs.append(("bad_folders", x_array(bad)))
        return ok(x_map(pairs))

    def legacy_items(self, req):
        w = self.state.world
        body = parse_llsd(req["body"]) or {}
        items = (body.get("items") or []) if isinstance(body, dict) else []
        out = []
        for e in items:
            it = w.items.get(str(e.get("item_id", NULL)).lower())
            if it is not None:
                out.append(x_map(item_pairs(w, it)))
        return ok(x_map([("agent_id", x_uuid(w.agent)), ("items", x_array(out))]))

    def create_category_cap(self, req):
        w = self.state.world
        body = parse_llsd(req["body"]) or {}
        fid = str(body.get("folder_id", NULL)).lower()
        parent = str(body.get("parent_id", NULL)).lower()
        if parent not in w.folders:
            return Response(404, "Not Found", error_body(404, "parent not found"))
        w.add_folder(dict(id=fid, parent=parent, type=int(body.get("type", -1)), version=1, owner=w.agent,
                          name=body.get("name", "")))
        w.bump(parent)
        return ok(x_map([("folder_id", x_uuid(fid)), ("parent_id", x_uuid(parent)),
                         ("type", x_int(int(body.get("type", -1)))), ("name", x_str(body.get("name", "")))]))


def accept_loop(srv, recorder, state, stop):
    while not stop["flag"]:
        try:
            sock, _ = srv.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        Connection(sock, recorder, state).start()
    srv.close()


def main(argv):
    log = None
    serve_only = False
    i = 0
    while i < len(argv):
        if argv[i] == "--log":
            log = argv[i + 1]
            i += 2
        elif argv[i] == "--serve":
            serve_only = True
            i += 1
        elif argv[i] == "--":
            i += 1
            break
        else:
            break
    cmd = argv[i:]
    # Bind first, in this thread, to an OS-assigned loopback port.
    try:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.bind(("127.0.0.1", 0))
        srv.listen(128)
        srv.settimeout(0.2)
        port = srv.getsockname()[1]
    except OSError as e:
        sys.stderr.write("vksinventory_mock_server: cannot listen on 127.0.0.1: %s\n" % e)
        return 2
    if log is None:
        log = os.environ.get("VKS_HTTP_MOCK_LOG")
    temp_log = None
    if log is None and not serve_only:
        import tempfile
        fd, temp_log = tempfile.mkstemp(prefix="vksinventory-mock-", suffix=".jsonl")
        os.close(fd)
        log = temp_log
    recorder = Recorder(log)
    state = State()
    stop = dict(flag=False)
    thread = threading.Thread(target=accept_loop, args=(srv, recorder, state, stop), daemon=True)
    thread.start()
    if serve_only:
        print("serving on 127.0.0.1:%d" % port, flush=True)
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        return 0
    env = dict(os.environ)
    env["LL_TEST_PORT"] = str(port)
    env["VKS_HTTP_MOCK_LOG"] = log
    rc = subprocess.call(cmd, env=env)
    stop["flag"] = True
    thread.join(5)
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

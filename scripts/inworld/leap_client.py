# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Minimal LEAP (LLSD Event API Plugin) client for the viewer's --leap option.

Wire protocol (indra/llcommon/llleap.cpp):

* viewer -> plugin (our stdin):  "<length>:" + "<? LLSD/Binary ?>\\n" + binary LLSD
  The payload is a map {"pump": <name>, "data": <LLSD>}.
* plugin -> viewer (our stdout): "<length>:" + LLSD notation (no header needed)
  The payload is a map {"pump": <name>, "data": <LLSD>}; the viewer posts "data" on pump "pump".
* The first message from the viewer is the handshake: pump = the viewer's reply pump for us,
  data = {"command": <name of the LEAP command pump>, "features": {...}}.
* Operations of an LLEventAPI are posted on the pump named like the API, with data
  {"op": ..., "reply": <our reply pump>, "reqid": <n>, ...}; the reply carries "reqid" back.

Never print to stdout: it is the protocol channel. Log to stderr (the viewer copies stderr lines into its
log). Only the standard library is used.
"""

import datetime
import io
import struct
import threading
import time
import uuid


class LeapError(Exception):
    """An operation failed (reply carried an "error", or the protocol was violated)."""


class LeapTimeout(LeapError):
    pass


class ConnectionLost(Exception):
    """The connection to the viewer is gone. Deliberately not a LeapError: handlers of a failed operation (a reply
    carrying an "error", a timeout) must never swallow a dead connection and carry on."""


class ViewerGone(ConnectionLost):
    """The pipe from the viewer reached end of file (the viewer exited, crashed or closed the pipe)."""


class ProtocolError(ConnectionLost):
    """The bytes from the viewer cannot be parsed: the connection is lost, but the viewer has not necessarily exited."""

    def __init__(self, msg, evidence=b"", frame=None):
        super().__init__(msg)
        self.evidence = evidence      # bytes seen where a length prefix was expected
        self.frame = frame            # the whole frame that did not parse


def hexdump(data, center=0, span=64):
    """Hex and ASCII of `span` bytes around `center`, for error messages."""
    lo = max(0, center - span // 2)
    chunk = data[lo:lo + span]
    return "bytes %d..%d of %d: %s | %s" % (lo, lo + len(chunk), len(data), chunk.hex(" "),
                                              "".join(chr(b) if 32 <= b < 127 else "." for b in chunk))


def find_repeated_block(data, gap=4096, block=64):
    """Offset i where data[i:i+block] == data[i+gap:i+gap+block], or None: a chunk of `gap` bytes written twice, as the
    viewer's WritePipeImpl can do on Windows (llprocess.cpp, EAGAIN with 0 bytes reported although the chunk went out)."""
    for i in range(0, len(data) - gap - block + 1):
        if data[i:i + block] == data[i + gap:i + gap + block]:
            return i
    return None


class LlsdParseError(ValueError):
    def __init__(self, msg, offset):
        super().__init__(msg)
        self.offset = offset


# ---------------------------------------------------------------------------------------------
# LLSD notation writer (plugin -> viewer)
# ---------------------------------------------------------------------------------------------

def _notation_string(s):
    out = ["'"]
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == "'":
            out.append("\\'")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    out.append("'")
    return "".join(out)


def notation_dumps(value):
    """Serialise a Python value to LLSD notation (str)."""
    if value is None:
        return "!"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, int):
        return "i%d" % value
    if isinstance(value, float):
        if value != value or value in (float("inf"), float("-inf")):
            raise ValueError("NaN/inf cannot be sent as LLSD real")
        return "r%s" % repr(value)
    if isinstance(value, uuid.UUID):
        return "u%s" % value
    if isinstance(value, str):
        return _notation_string(value)
    if isinstance(value, (bytes, bytearray)):
        return "b16" + '"' + bytes(value).hex().upper() + '"'
    if isinstance(value, dict):
        return "{" + ",".join(_notation_string(str(k)) + ":" + notation_dumps(v) for k, v in value.items()) + "}"
    if isinstance(value, (list, tuple)):
        return "[" + ",".join(notation_dumps(v) for v in value) + "]"
    raise TypeError("cannot serialise %r as LLSD" % type(value))


# ---------------------------------------------------------------------------------------------
# LLSD parsers (viewer -> plugin is binary; notation is parsed too for the fake peer and robustness)
# ---------------------------------------------------------------------------------------------

_BINARY_HEADER = b"<? LLSD/Binary ?>"
_NOTATION_HEADER = b"<? llsd/notation ?>"


class _Cursor:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ValueError("truncated LLSD")
        chunk = self.data[self.pos:self.pos + n]
        self.pos += n
        return chunk

    def byte(self):
        return self.take(1)

    def peek(self):
        if self.pos >= len(self.data):
            raise ValueError("truncated LLSD")
        return self.data[self.pos:self.pos + 1]


def _binary_value(c, depth=0):
    if depth > 96:
        raise ValueError("LLSD nested too deeply")
    t = c.byte()
    if t == b"!":
        return None
    if t == b"1":
        return True
    if t == b"0":
        return False
    if t == b"i":
        return struct.unpack(">i", c.take(4))[0]
    if t == b"r":
        return struct.unpack(">d", c.take(8))[0]
    if t == b"u":
        return uuid.UUID(bytes=c.take(16))
    if t == b"d":
        return struct.unpack("<d", c.take(8))[0]  # seconds since the epoch (host order, little-endian on x86)
    if t in (b"s", b"l", b"b"):
        n = struct.unpack(">I", c.take(4))[0]
        raw = c.take(n)
        return raw if t == b"b" else raw.decode("utf-8", "replace")
    if t in (b"'", b'"'):
        return _delimited(c, t)
    if t == b"[":
        n = struct.unpack(">I", c.take(4))[0]
        out = [_binary_value(c, depth + 1) for _ in range(n)]
        if c.byte() != b"]":
            raise ValueError("binary array not closed")
        return out
    if t == b"{":
        n = struct.unpack(">I", c.take(4))[0]
        out = {}
        for _ in range(n):
            k = c.byte()
            if k == b"k":
                klen = struct.unpack(">I", c.take(4))[0]
                key = c.take(klen).decode("utf-8", "replace")
            elif k in (b"'", b'"'):
                key = _delimited(c, k)
            else:
                raise ValueError("bad binary map key marker %r" % k)
            out[key] = _binary_value(c, depth + 1)
        if c.byte() != b"}":
            raise ValueError("binary map not closed")
        return out
    raise ValueError("unknown binary LLSD type %r" % t)


def _delimited(c, delim):
    out = bytearray()
    while True:
        ch = c.byte()
        if ch == delim:
            return out.decode("utf-8", "replace")
        if ch == b"\\":
            esc = c.byte()
            out += {b"n": b"\n", b"r": b"\r", b"t": b"\t", b"a": b"\a", b"b": b"\b", b"f": b"\f",
                    b"v": b"\v", b"0": b"\0"}.get(esc, esc)
        else:
            out += ch


def _skip_ws(c):
    while c.pos < len(c.data) and c.data[c.pos:c.pos + 1] in (b" ", b"\t", b"\r", b"\n"):
        c.pos += 1


def _notation_value(c, depth=0):
    if depth > 96:
        raise ValueError("LLSD nested too deeply")
    _skip_ws(c)
    t = c.byte()
    if t == b"!":
        return None
    if t == b"{":
        out = {}
        _skip_ws(c)
        if c.peek() == b"}":
            c.byte()
            return out
        while True:
            _skip_ws(c)
            q = c.byte()
            if q not in (b"'", b'"'):
                raise ValueError("notation map key must be quoted")
            key = _delimited(c, q)
            _skip_ws(c)
            if c.byte() != b":":
                raise ValueError("notation map: ':' expected")
            out[key] = _notation_value(c, depth + 1)
            _skip_ws(c)
            sep = c.byte()
            if sep == b"}":
                return out
            if sep != b",":
                raise ValueError("notation map: ',' or '}' expected")
    if t == b"[":
        out = []
        _skip_ws(c)
        if c.peek() == b"]":
            c.byte()
            return out
        while True:
            out.append(_notation_value(c, depth + 1))
            _skip_ws(c)
            sep = c.byte()
            if sep == b"]":
                return out
            if sep != b",":
                raise ValueError("notation array: ',' or ']' expected")
    if t in (b"'", b'"'):
        return _delimited(c, t)
    if t == b"i":
        return int(_number_text(c))
    if t == b"r":
        return float(_number_text(c))
    if t == b"u":
        return uuid.UUID(c.take(36).decode("ascii"))
    if t in (b"t", b"T"):
        _word(c)
        return True
    if t in (b"f", b"F"):
        _word(c)
        return False
    if t == b"1":
        return True
    if t == b"0":
        return False
    if t == b"d":
        q = c.byte()
        return _delimited(c, q)
    if t == b"l":
        q = c.byte()
        return _delimited(c, q)
    if t == b"s":
        n = int(_number_text(c, 0))  # s(len)"..."
        raise ValueError("sized notation strings are not supported (len %d)" % n)
    raise ValueError("unknown notation LLSD type %r" % t)


def _number_text(c, _unused=None):
    start = c.pos
    while c.pos < len(c.data) and c.data[c.pos:c.pos + 1] in b"+-0123456789.eE":
        c.pos += 1
    return c.data[start:c.pos].decode("ascii")


def _word(c):
    while c.pos < len(c.data) and c.data[c.pos:c.pos + 1].isalpha():
        c.pos += 1


def llsd_loads(data):
    """Parse a binary or notation LLSD document (with or without header). Raises LlsdParseError with the offset."""
    if data.startswith(_BINARY_HEADER):
        skip = len(_BINARY_HEADER)
        while data[skip:skip + 1] == bytes([10]):
            skip += 1
        c = _Cursor(data[skip:])
        base, parser = skip, _binary_value
    elif data.lower().startswith(_NOTATION_HEADER):
        c = _Cursor(data[len(_NOTATION_HEADER):])
        base, parser = len(_NOTATION_HEADER), _notation_value
    elif data[:1] in (b"{", b"[", b"!"):
        # an unheadered document: the viewer's own default is notation
        c = _Cursor(data)
        base, parser = 0, _notation_value
    else:
        raise LlsdParseError("unrecognised LLSD document start %r" % data[:12], 0)
    try:
        return parser(c)
    except ValueError as exc:
        raise LlsdParseError(str(exc), base + c.pos) from exc


# ---------------------------------------------------------------------------------------------
# Binary writer: used only by the fake viewer in the tests (the viewer sends binary)
# ---------------------------------------------------------------------------------------------

def binary_dumps(value, header=True):
    out = io.BytesIO()
    if header:
        out.write(_BINARY_HEADER + b"\n")
    _binary_write(out, value)
    return out.getvalue()


def _binary_write(out, v):
    if v is None:
        out.write(b"!")
    elif v is True:
        out.write(b"1")
    elif v is False:
        out.write(b"0")
    elif isinstance(v, int):
        out.write(b"i" + struct.pack(">i", v))
    elif isinstance(v, float):
        out.write(b"r" + struct.pack(">d", v))
    elif isinstance(v, uuid.UUID):
        out.write(b"u" + v.bytes)
    elif isinstance(v, str):
        raw = v.encode("utf-8")
        out.write(b"s" + struct.pack(">I", len(raw)) + raw)
    elif isinstance(v, (bytes, bytearray)):
        out.write(b"b" + struct.pack(">I", len(v)) + bytes(v))
    elif isinstance(v, datetime.datetime):
        out.write(b"d" + struct.pack("<d", v.timestamp()))
    elif isinstance(v, dict):
        out.write(b"{" + struct.pack(">I", len(v)))
        for k, val in v.items():
            raw = str(k).encode("utf-8")
            out.write(b"k" + struct.pack(">I", len(raw)) + raw)
            _binary_write(out, val)
        out.write(b"}")
    elif isinstance(v, (list, tuple)):
        out.write(b"[" + struct.pack(">I", len(v)))
        for val in v:
            _binary_write(out, val)
        out.write(b"]")
    else:
        raise TypeError("cannot serialise %r" % type(v))


# ---------------------------------------------------------------------------------------------
# Framing
# ---------------------------------------------------------------------------------------------

def read_frame(stream):
    """Read one "<len>:<payload>" frame; returns bytes, or None at EOF."""
    digits = b""
    while True:
        ch = stream.read(1)
        if not ch:
            return None if not digits else _eof_in_frame()
        if ch == b":":
            break
        if not ch.isdigit():
            raise ProtocolError("%r in the length prefix after %r" % (ch, digits), evidence=digits + ch)
        digits += ch
        if len(digits) > 12:
            raise ProtocolError("length prefix too long", evidence=digits)
    n = int(digits or b"0")
    payload = b""
    while len(payload) < n:
        chunk = stream.read(n - len(payload))
        if not chunk:
            return _eof_in_frame()
        payload += chunk
    return payload


def _eof_in_frame():
    raise ViewerGone("pipe reached end of file in the middle of a frame")


def write_frame(stream, payload):
    stream.write(b"%d:" % len(payload) + payload)
    stream.flush()


# ---------------------------------------------------------------------------------------------
# Session
# ---------------------------------------------------------------------------------------------

class LeapSession:
    """One LEAP connection. Construct with the binary read and write streams (stdin.buffer / stdout.buffer)."""

    def __init__(self, rstream, wstream, log=None, default_timeout=30.0):
        self._r = rstream
        self._w = wstream
        self._log = log or (lambda msg: None)
        self.default_timeout = default_timeout
        self.reply_pump = None
        self.command_pump = None
        self.features = {}
        self._wlock = threading.Lock()
        self._cv = threading.Condition()
        self._pending = {}      # reqid -> reply (or None while waiting)
        self._events = []       # (pump, data) not matched to a request
        self._discard = set()   # reqids of fire() operations: their replies are only checked for errors
        self._recent = set()    # reqids forgotten by the last forget_fired(): a late error reply for one is still reported
        self.late_errors = 0    # error replies for unknown reqids (the first 5 are logged at WARN)
        self._fired_errors = []
        self._next_id = 1
        self._closed = False
        self.diag_dir = None     # where a protocol error writes bad_frame.bin / bad_frame.txt
        self._prev_frame = None
        self._nonmap_dropped = 0       # unstamped non-map replies dropped by the reader
        self._nonmap_types = set()     # their types seen so far (the first of each type is logged)
        self._broken = None      # why the connection was given up on (protocol error), None if it was EOF
        self._thread = None

    # -- startup --------------------------------------------------------------------------------
    def start(self, handshake_timeout=60.0):
        """Start the reader thread and wait up to handshake_timeout seconds for the viewer's handshake."""
        self._thread = threading.Thread(target=self._reader, name="leap-reader", daemon=True)
        self._thread.start()
        deadline = time.monotonic() + handshake_timeout
        with self._cv:
            while self.command_pump is None:
                if self._closed:
                    raise self.gone("the handshake")
                left = deadline - time.monotonic()
                if left <= 0:
                    raise LeapTimeout("no LEAP handshake from the viewer in %.0f s" % handshake_timeout)
                self._cv.wait(left)
        return self

    def _reader(self):
        try:
            while True:
                frame = read_frame(self._r)
                if frame is None:
                    break
                try:
                    msg = llsd_loads(frame)
                except LlsdParseError as exc:
                    # A frame that cannot be parsed means the byte stream is damaged (the next length prefix cannot
                    # be trusted either): give up with the evidence rather than guess.
                    dup = find_repeated_block(frame)
                    raise ProtocolError("frame of %d bytes is not LLSD: %s at offset %d; %s%s" % (
                        len(frame), exc, exc.offset, hexdump(frame, exc.offset),
                        ("; repeated 4 KB block at offset %d: matches the viewer's WritePipeImpl EAGAIN duplicate "
                         "(llprocess.cpp)" % dup) if dup is not None else ""), frame=frame) from exc
                self._prev_frame = frame
                pump = msg.get("pump")
                data = msg.get("data")
                with self._cv:
                    if self.command_pump is None:
                        info = data if isinstance(data, dict) else {}
                        self.reply_pump = pump
                        self.features = info.get("features") or {}
                        self.command_pump = info.get("command") or None
                        if self.command_pump is None:
                            self._log("bad handshake: %r" % (msg,))
                        self._cv.notify_all()
                        continue
                    if pump == self.reply_pump and not isinstance(data, dict):
                        # A non-map reply (the viewer's LLReqID::stamp cannot add a reqid to one, e.g. UI.setSelectedByValue):
                        # it can match no request, so it is counted and dropped rather than queued as an event.
                        self._nonmap_dropped += 1
                        if type(data).__name__ not in self._nonmap_types:
                            self._nonmap_types.add(type(data).__name__)
                            self._log("debug: unstamped non-map reply %.200r (%s) on the reply pump dropped (the first of each "
                                      "type is logged; the count is in a timeout message)" % (data, type(data).__name__))
                        continue
                    reqid = data.get("reqid") if isinstance(data, dict) else None
                    if pump == self.reply_pump and reqid is not None:
                        if reqid in self._pending:
                            self._pending[reqid] = data
                        elif reqid in self._discard:
                            self._discard.discard(reqid)
                            if data.get("error"):
                                self._fired_errors.append(str(data["error"]))
                        elif reqid in self._recent:
                            self._recent.discard(reqid)
                            if data.get("error"):
                                self._fired_errors.append(str(data["error"]))
                        elif data.get("error"):
                            # a late reply to a request that timed out: dropped, but an error is worth a line
                            self.late_errors += 1
                            if self.late_errors <= 5:
                                self._log("WARNING error reply for unknown reqid %r dropped: %s%s" % (
                                    reqid, str(data["error"])[:200],
                                    " (further ones are only counted)" if self.late_errors == 5 else ""))
                    else:
                        self._events.append((pump, data))
                    self._cv.notify_all()
        except ProtocolError as exc:
            self._broken = str(exc)
            self._save_evidence(exc)
            self._log("PROTOCOL ERROR, connection from the viewer given up: %s" % exc)
        except ViewerGone as exc:
            self._log("reader stopped: %s" % exc)
        except Exception as exc:  # noqa: BLE001
            self._broken = "reader failed: %r" % exc
            self._log("reader failed: %r" % exc)
        finally:
            with self._cv:
                self._closed = True
                self._cv.notify_all()

    def _save_evidence(self, exc):
        """Write what was seen to <diag_dir>: bad_frame.bin (the whole frame that did not parse, or the bytes seen where a
        length prefix was expected) and bad_frame.txt (the last 64 bytes of the previous good frame)."""
        d = self.diag_dir
        if not d:
            return
        try:
            d = __import__("pathlib").Path(d)
            d.mkdir(parents=True, exist_ok=True)
            (d / "bad_frame.bin").write_bytes(exc.frame if exc.frame is not None else exc.evidence)
            nl = chr(10)
            note = "%s%s%sprevious good frame, last 64 bytes (%d bytes long):%s%s%s" % (
                exc, nl, nl, len(self._prev_frame or b""), nl, (self._prev_frame or b"")[-64:].hex(" "), nl)
            (d / "bad_frame.txt").write_text(note, encoding="utf-8")
            self._log("evidence written to %s (bad_frame.bin, bad_frame.txt)" % d)
        except OSError as oserr:
            self._log("could not write the evidence: %s" % oserr)

    @property
    def closed(self):
        return self._closed

    def gone(self, what="a reply"):
        """The exception to raise because the connection is gone: ProtocolError after a parse failure (the viewer may
        well be running), ViewerGone after end of file."""
        if self._broken:
            return ProtocolError("connection to the viewer lost by a protocol error while waiting for %s: %s" % (
                what, self._broken))
        return ViewerGone("the pipe from the viewer reached end of file while waiting for %s" % what)

    # -- sending --------------------------------------------------------------------------------
    def post(self, pump, data):
        payload = notation_dumps({"pump": pump, "data": data}).encode("utf-8")
        with self._wlock:
            try:
                write_frame(self._w, payload)
            except (BrokenPipeError, OSError) as exc:
                raise ViewerGone("write failed: %s" % exc) from exc

    def fire(self, pump, op, /, **args):
        """Send an operation whose reply we do not wait for. A reply key is still sent: an operation the viewer
        rejects (bad name or arguments) throws in the viewer's dispatcher when there is no reply pump to tell; the
        reply is dropped by the reader and only its error, if any, is kept (see take_errors). The reqid stays in
        _discard until the reply with it arrives or forget_fired() is called: an operation with no reply, or a bare
        non-map reply (no reqid), would otherwise leave it there forever."""
        with self._cv:
            reqid = self._next_id
            self._next_id += 1
            self._discard.add(reqid)
        data = {"op": op, "reply": self.reply_pump, "reqid": reqid}
        data.update(args)
        self.post(pump, data)

    def forget_fired(self):
        """Forget the fire() operations still waiting for a reply. Call it only after a ping reply: it follows every
        earlier reply on the wire (LLLeap posts each frame synchronously in order), so an error map for a fired
        operation has already been recorded for take_errors(). Single-threaded callers only; if the ping times out the
        set is left as it is and a late error is reported at the next barrier. The forgotten reqids are kept in
        _recent until the following call, so an error that still arrives late is reported by take_errors() too."""
        with self._cv:
            self._recent = self._discard
            self._discard = set()

    def take_errors(self):
        """Errors reported by fire() operations since the last call."""
        with self._cv:
            out, self._fired_errors = self._fired_errors, []
        return out

    def request(self, pump, op, /, timeout=None, **args):
        """Send an operation and wait for its reply. Raises LeapError if the reply has an "error"."""
        timeout = self.default_timeout if timeout is None else timeout
        with self._cv:
            reqid = self._next_id
            self._next_id += 1
            self._pending[reqid] = None
            dropped_at_start = self._nonmap_dropped
        data = {"op": op, "reply": self.reply_pump, "reqid": reqid}
        data.update(args)
        try:
            self.post(pump, data)
            deadline = time.monotonic() + timeout
            with self._cv:
                while self._pending[reqid] is None:
                    if self._closed:
                        raise self.gone("%s.%s" % (pump, op))
                    left = deadline - time.monotonic()
                    if left <= 0:
                        n = self._nonmap_dropped - dropped_at_start
                        extra = " (%d unstamped non-map replies were dropped)" % n if n > 0 else ""
                        raise LeapTimeout("no reply to %s.%s in %.0f s%s" % (pump, op, timeout, extra))
                    self._cv.wait(left)
                reply = self._pending[reqid]
        finally:
            with self._cv:
                self._pending.pop(reqid, None)
        if isinstance(reply, dict) and reply.get("error"):
            raise LeapError("%s.%s: %s" % (pump, op, reply["error"]))
        return reply

    def ping(self, timeout=None):
        """Round trip through the LEAP command pump: everything posted before it has been handled."""
        return self.request(self.command_pump, "ping", timeout=timeout)

    def listen(self, source, listener="leap_light_check"):
        """Have the viewer forward events of pump `source` to us (they arrive via wait_event)."""
        return self.request(self.command_pump, "listen", source=source, listener=listener)

    # -- events ---------------------------------------------------------------------------------
    def wait_event(self, pump, predicate=None, timeout=None):
        """Return the data of the first queued event on `pump` for which predicate(data) is true."""
        timeout = self.default_timeout if timeout is None else timeout
        deadline = time.monotonic() + timeout
        with self._cv:
            while True:
                for i, (p, d) in enumerate(self._events):
                    if p == pump and (predicate is None or predicate(d)):
                        del self._events[i]
                        return d
                if self._closed:
                    raise self.gone("an event on %s" % pump)
                left = deadline - time.monotonic()
                if left <= 0:
                    raise LeapTimeout("no event on %s in %.0f s" % (pump, timeout))
                self._cv.wait(left)

    def drain_events(self, pump=None):
        with self._cv:
            if pump is None:
                out, self._events = self._events, []
            else:
                out = [e for e in self._events if e[0] == pump]
                self._events = [e for e in self._events if e[0] != pump]
        return out

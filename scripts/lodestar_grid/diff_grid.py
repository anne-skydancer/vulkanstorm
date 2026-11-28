# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Lodestar grid test: compare what the viewer uploaded with what the server stored.

Stdlib only (Python 3.13). Reads, from the viewer's logs folder:
  whole_model_body_N.xml / whole_model_fee_request_N.xml   the uploaded asset (MeshUploadLogXML)
  whole_model_fee_response_N.xml                           the quotes
  whole_model_upload_response_N.xml                        the server's answer (new asset id)
  lodestar-grid-<uuid>.bin                                 the stored asset (LodestarGridTestDump)

N is the viewer's per-session counter, one per "Calculate weights & fee". Each N is classified by
what its uploaded asset contains: variant 0 (control), 1, 2 or 3. A stored asset is paired with
its upload by the new asset id in the upload response, else by order (header date against N).

Usage:
  python diff_grid.py LOGS_DIR            report, exit 0 when every check passes, 1 otherwise
  python diff_grid.py --self-test         run the checks on synthetic data
"""
import argparse
import base64
import re
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

LODS = ("lowest_lod", "low_lod", "medium_lod", "high_lod")
BLOCK_NAMES = LODS + ("physics_mesh", "physics_convex", "skin", "lodestar_block")
COMPRESSED = set(BLOCK_NAMES) - {"lodestar_block"}
FEE_FIELDS = ("upload_price", "resource_cost", "model_streaming_cost", "simulation_cost", "physics_cost")


def grid_bytes(seed):
    """The 64 bytes the viewer writes (LLModel::writeModelToStream, lodestar_grid_bytes)."""
    return bytes((seed + i) & 0xFF for i in range(64))


V1_VALUES, V2_VALUE, V3_BLOCK, V3_TRAILER = grid_bytes(0x10), grid_bytes(0x20), grid_bytes(0x30), grid_bytes(0x40)


# ---------------------------------------------------------------- binary LLSD

class Date(float):
    """An LLSD date, seconds since the epoch."""


def parse_binary(buf, i=0):
    """Parse one binary LLSD value at buf[i:]; return (value, next index)."""
    t = buf[i:i + 1]
    i += 1
    if t == b"{":
        n = struct.unpack(">I", buf[i:i + 4])[0]
        i += 4
        out = {}
        for _ in range(n):
            if buf[i:i + 1] != b"k":
                raise ValueError("map key expected at %d" % i)
            kl = struct.unpack(">I", buf[i + 1:i + 5])[0]
            i += 5
            key = buf[i:i + kl].decode("utf-8", "replace")
            i += kl
            out[key], i = parse_binary(buf, i)
        if buf[i:i + 1] != b"}":
            raise ValueError("map end expected at %d" % i)
        return out, i + 1
    if t == b"[":
        n = struct.unpack(">I", buf[i:i + 4])[0]
        i += 4
        out = []
        for _ in range(n):
            v, i = parse_binary(buf, i)
            out.append(v)
        if buf[i:i + 1] != b"]":
            raise ValueError("array end expected at %d" % i)
        return out, i + 1
    if t == b"i":
        return struct.unpack(">i", buf[i:i + 4])[0], i + 4
    if t == b"r":
        return struct.unpack(">d", buf[i:i + 8])[0], i + 8
    if t == b"u":
        return "uuid:" + buf[i:i + 16].hex(), i + 16
    if t == b"d":
        return Date(struct.unpack("<d", buf[i:i + 8])[0]), i + 8
    if t in (b"s", b"l", b"b"):
        n = struct.unpack(">I", buf[i:i + 4])[0]
        i += 4
        raw = buf[i:i + n]
        return (bytes(raw) if t == b"b" else raw.decode("utf-8", "replace")), i + n
    if t == b"1":
        return True, i
    if t == b"0":
        return False, i
    if t == b"!":
        return None, i
    raise ValueError("unknown LLSD tag %r at %d" % (t, i - 1))


def write_binary(v):
    """Binary LLSD writer, for the self-test."""
    if isinstance(v, bool):
        return b"1" if v else b"0"
    if isinstance(v, Date):
        return b"d" + struct.pack("<d", float(v))
    if isinstance(v, int):
        return b"i" + struct.pack(">i", v)
    if isinstance(v, float):
        return b"r" + struct.pack(">d", v)
    if isinstance(v, bytes):
        return b"b" + struct.pack(">I", len(v)) + v
    if isinstance(v, str):
        if v.startswith("uuid:"):
            return b"u" + bytes.fromhex(v[5:])
        raw = v.encode()
        return b"s" + struct.pack(">I", len(raw)) + raw
    if isinstance(v, dict):
        out = b"{" + struct.pack(">I", len(v))
        for k, x in v.items():
            raw = k.encode()
            out += b"k" + struct.pack(">I", len(raw)) + raw + write_binary(x)
        return out + b"}"
    if isinstance(v, list):
        return b"[" + struct.pack(">I", len(v)) + b"".join(write_binary(x) for x in v) + b"]"
    if v is None:
        return b"!"
    raise TypeError(type(v))


# ---------------------------------------------------------------- XML LLSD

def parse_xml_node(node):
    tag = node.tag
    text = node.text or ""
    if tag == "map":
        kids = list(node)
        return {kids[i].text or "": parse_xml_node(kids[i + 1]) for i in range(0, len(kids) - 1, 2)}
    if tag == "array":
        return [parse_xml_node(k) for k in node]
    if tag == "integer":
        return int(text or 0)
    if tag == "real":
        return float(text or 0)
    if tag == "boolean":
        return text.strip().lower() in ("1", "true")
    if tag == "uuid":
        return "uuid:" + text.strip().replace("-", "") if text.strip() else None
    if tag == "binary":
        return base64.b64decode("".join(text.split())) if text.strip() else b""
    if tag in ("string", "uri"):
        return text
    if tag == "date":
        return text
    return None


def read_xml_llsd(path):
    root = ET.parse(path).getroot()
    kids = list(root)
    return parse_xml_node(kids[0]) if kids else None


def xml_node(v, parent):
    def add(tag, text=None):
        e = ET.SubElement(parent, tag)
        e.text = text
        return e
    if isinstance(v, bool):
        add("boolean", "true" if v else "false")
    elif isinstance(v, int):
        add("integer", str(v))
    elif isinstance(v, float):
        add("real", repr(v))
    elif isinstance(v, bytes):
        add("binary", base64.b64encode(v).decode()).set("encoding", "base64")
    elif isinstance(v, str):
        if v.startswith("uuid:"):
            h = v[5:]
            add("uuid", "-".join((h[:8], h[8:12], h[12:16], h[16:20], h[20:])))
        else:
            add("string", v)
    elif isinstance(v, dict):
        m = add("map")
        for k, x in v.items():
            ET.SubElement(m, "key").text = k
            xml_node(x, m)
    elif isinstance(v, list):
        a = add("array")
        for x in v:
            xml_node(x, a)
    else:
        add("undef")


def write_xml_llsd(path, v):
    root = ET.Element("llsd")
    xml_node(v, root)
    Path(path).write_bytes(b'<?xml version="1.0" ?>' + ET.tostring(root))


# ---------------------------------------------------------------- assets

DEPRECATED_HEADER = b"<? LLSD/Binary ?>\n"


class Asset:
    """A mesh asset: header map, then the blocks the header names, then whatever else follows."""

    def __init__(self, data):
        self.data = data
        skip = len(DEPRECATED_HEADER) if data.startswith(DEPRECATED_HEADER) else 0
        self.header, end = parse_binary(data, skip)
        if not isinstance(self.header, dict):
            raise ValueError("header is not a map")
        self.base = end                    # block offsets count from here
        self.blocks = {}                   # name -> (offset, size, raw bytes)
        for name, entry in self.header.items():
            if isinstance(entry, dict) and isinstance(entry.get("offset"), int) and isinstance(entry.get("size"), int):
                if entry["size"] > 0:
                    a = self.base + entry["offset"]
                    self.blocks[name] = (entry["offset"], entry["size"], data[a:a + entry["size"]])
        self.referenced_end = max((o + s for o, s, _ in self.blocks.values()), default=0)
        self.tail = data[self.base + self.referenced_end:]

    def block(self, name):
        return self.blocks[name][2] if name in self.blocks else None

    def decoded(self, name):
        """The block as a Python value (zlib then binary LLSD), or its raw bytes for lodestar_block."""
        raw = self.block(name)
        if raw is None:
            return None
        if name not in COMPRESSED:
            return raw
        try:
            return parse_binary(zlib.decompress(raw))[0]
        except (zlib.error, ValueError, struct.error):
            return None

    def layout(self):
        return [n for n, _ in sorted(((n, b[0]) for n, b in self.blocks.items()), key=lambda x: x[1])]


def detect_variant(asset):
    """Which grid variant an uploaded asset was written as; 0 when it shows none of them."""
    if "lodestar_block" in asset.header:
        return 3
    if "lodestar" in asset.header or "lodestar" in (asset.header.get("high_lod") or {}):
        return 1
    low = asset.decoded("lowest_lod")
    if isinstance(low, list) and low and isinstance(low[0], dict) and "lodestar" in low[0]:
        return 2
    return 0


def value_check(variant, stored):
    """(ok, text): is the variant's value present in the stored asset?"""
    if variant == 1:
        top = isinstance(stored.header.get("lodestar"), dict)
        inner = isinstance(stored.header.get("high_lod"), dict) and "lodestar" in stored.header["high_lod"]
        ok = top and inner
        return ok, "top-level map %s, high_lod key %s" % ("kept" if top else "dropped", "kept" if inner else "dropped")
    if variant == 2:
        low = stored.decoded("lowest_lod")
        got = low[0].get("lodestar") if isinstance(low, list) and low and isinstance(low[0], dict) else None
        if got == V2_VALUE:
            return True, "64 bytes in Lowest first face map kept, identical"
        return False, "Lowest first face map: " + ("value differs" if got is not None else "key dropped")
    if variant == 3:
        entry = stored.header.get("lodestar_block")
        blk = stored.block("lodestar_block")
        key = "kept" if entry is not None else "dropped"
        data = "identical" if blk == V3_BLOCK else ("differs" if blk is not None else "missing")
        trailer = "kept" if V3_TRAILER in stored.data else "dropped"
        return (entry is not None and blk == V3_BLOCK), "header key %s, block %s, trailing 64 bytes %s" % (key, data, trailer)
    return True, "control"


# ---------------------------------------------------------------- fees

def find_key(tree, key):
    """First value stored under `key` anywhere in the tree (maps and arrays), else None."""
    if isinstance(tree, dict):
        if key in tree:
            return tree[key]
        for v in tree.values():
            r = find_key(v, key)
            if r is not None:
                return r
    elif isinstance(tree, list):
        for v in tree:
            r = find_key(v, key)
            if r is not None:
                return r
    return None


def fee_fields(resp):
    out = {k: find_key(resp, k) for k in FEE_FIELDS}
    sp = find_key(resp, "streaming_params")
    for lod in LODS:
        out["streaming_params." + lod] = sp.get(lod) if isinstance(sp, dict) else None
    return out


# ---------------------------------------------------------------- the test

def short(b, n=12):
    return b[:n].hex() + ("..." if len(b) > n else "")


class Report:
    def __init__(self):
        self.lines, self.failed = [], 0

    def say(self, text=""):
        self.lines.append(text)

    def check(self, ok, text):
        if not ok:
            self.failed += 1
        self.lines.append(("  PASS  " if ok else "  FAIL  ") + text)

    def info(self, text):
        self.lines.append("  info  " + text)


def compare_header_keys(rep, label, a, b):
    added = sorted(set(b.header) - set(a.header))
    dropped = sorted(set(a.header) - set(b.header))
    rep.info("%s header keys added: %s; dropped: %s" % (label, added or "none", dropped or "none"))
    for k in sorted(set(a.header) & set(b.header)):
        if isinstance(a.header[k], dict) and isinstance(b.header[k], dict):
            sub_added = sorted(set(b.header[k]) - set(a.header[k]))
            sub_dropped = sorted(set(a.header[k]) - set(b.header[k]))
            if sub_added or sub_dropped:
                rep.info("%s header[%s] keys added: %s; dropped: %s" % (label, k, sub_added or "none", sub_dropped or "none"))


def compare_blocks(rep, label, a, b, names=None):
    """Every block of a against b, byte for byte; returns the list of names that differ."""
    differ = []
    for name in sorted(set(a.blocks) | set(b.blocks), key=lambda n: (BLOCK_NAMES.index(n) if n in BLOCK_NAMES else 99, n)):
        if names is not None and name not in names:
            continue
        ba, bb = a.block(name), b.block(name)
        if ba is None or bb is None:
            rep.info("%s block %s: %s" % (label, name, "only in the second" if ba is None else "only in the first"))
            differ.append(name)
        elif ba == bb:
            rep.info("%s block %s: identical, %d bytes" % (label, name, len(ba)))
        else:
            same = a.decoded(name) == b.decoded(name)
            rep.info("%s block %s: BYTES DIFFER (%d vs %d), content %s" % (label, name, len(ba), len(bb), "equal" if same else "differs"))
            differ.append(name)
    return differ


def load_run(logs):
    """Collect uploads by N, stored assets by uuid."""
    logs = Path(logs)
    runs = {}
    for p in logs.glob("whole_model_*_*.xml"):
        m = re.fullmatch(r"whole_model_(body|fee_request|fee_response|upload_response)_(\d+)\.xml", p.name)
        if m:
            runs.setdefault(int(m.group(2)), {})[m.group(1)] = p
    stored = {}
    for p in logs.glob("lodestar-grid-*.bin"):
        m = re.fullmatch(r"lodestar-grid-([0-9a-fA-F-]{36})\.bin", p.name)
        if m:
            stored[m.group(1).replace("-", "").lower()] = p
    return runs, stored


def uploaded_asset(files):
    """The first mesh_list entry the viewer sent for one N, as an Asset (None when not logged)."""
    for key in ("body", "fee_request"):
        if key in files:
            tree = read_xml_llsd(files[key])
            ml = find_key(tree, "mesh_list")
            if isinstance(ml, list) and ml and isinstance(ml[0], bytes):
                return Asset(ml[0])
    return None


def analyse(logs):
    rep = Report()
    runs, stored = load_run(logs)
    rep.say("Lodestar grid test: %s" % logs)
    rep.say("  %d logged fee request(s), %d stored asset dump(s)" % (len(runs), len(stored)))

    ups = {}
    for n in sorted(runs):
        a = uploaded_asset(runs[n])
        if a is not None:
            ups[n] = a
    by_variant = {}
    for n, a in ups.items():
        by_variant.setdefault(detect_variant(a), []).append(n)
    rep.say()
    rep.say("Uploads found (N is the viewer's counter): " + ", ".join("variant %d: N=%s" % (v, by_variant[v]) for v in sorted(by_variant)))
    rep.check(0 in by_variant, "a control (variant 0) upload is logged")
    for v in (1, 2, 3):
        rep.check(v in by_variant, "variant %d upload is logged" % v)
    if 0 not in by_variant:
        return rep

    # pair stored assets with uploads: new asset id first, then order of the header date
    pairing, method = {}, "new asset id"
    for n, files in runs.items():
        if "upload_response" in files and n in ups:
            nid = find_key(read_xml_llsd(files["upload_response"]), "new_asset")
            if isinstance(nid, str):
                key = nid.replace("uuid:", "").replace("-", "").lower()
                if key in stored:
                    pairing[n] = key
    uploaded_ns = sorted(n for n in ups if "upload_response" in runs[n])
    if len(pairing) < len(uploaded_ns) and len(stored) == len(uploaded_ns):
        method = "order of the header date"
        dated = sorted(stored, key=lambda k: float(Asset(stored[k].read_bytes()).header.get("date") or 0))
        pairing = dict(zip(uploaded_ns, dated))
    rep.say("Stored assets paired with uploads by %s: %s" % (method, {n: k[:8] for n, k in sorted(pairing.items())}))

    control_n = by_variant[0][0]
    control_up = ups[control_n]
    control_st = Asset(stored[pairing[control_n]].read_bytes()) if control_n in pairing else None
    control_fee = fee_fields(read_xml_llsd(runs[control_n]["fee_response"])) if "fee_response" in runs[control_n] else None
    rep.check(control_st is not None, "control: stored asset dumped and paired")

    for v in (0, 1, 2, 3):
        for n in by_variant.get(v, [])[:1]:
            up = ups[n]
            rep.say()
            rep.say("== variant %d (N=%d)%s ==" % (v, n, "  [control]" if v == 0 else ""))
            if v:
                compare_header_keys(rep, "uploaded vs control uploaded:", control_up, up)
                compare_blocks(rep, "uploaded vs control uploaded:", control_up, up)
            rep.info("uploaded asset length %d bytes, block order %s" % (len(up.data), up.layout()))
            if n not in pairing:
                rep.check(False, "stored asset available")
                continue
            st = Asset(stored[pairing[n]].read_bytes())
            rep.info("stored asset length %d bytes, block order %s" % (len(st.data), st.layout()))
            compare_header_keys(rep, "stored vs uploaded:", up, st)
            compare_blocks(rep, "stored vs uploaded:", up, st)
            rep.info("bytes after the last referenced block: uploaded %d, stored %d" % (len(up.tail), len(st.tail)))
            ok, text = value_check(v, st)
            rep.check(ok, "value present in the stored asset: " + text)
            if control_st is not None and v:
                other = [x for x in BLOCK_NAMES if (x in st.blocks or x in control_st.blocks) and x not in ("lodestar_block",) and not (v == 2 and x == "lowest_lod")]
                differ = compare_blocks(rep, "stored vs control stored:", st, control_st, set(other))
                rep.check(not differ, "every other stored block equals the control's" + ("" if not differ else " (differs: %s)" % differ))
            fee = fee_fields(read_xml_llsd(runs[n]["fee_response"])) if "fee_response" in runs[n] else None
            if fee is None or control_fee is None:
                rep.check(False, "fee response logged")
                continue
            for k, val in fee.items():
                ref = control_fee[k]
                if v == 0:
                    rep.info("fee %s = %s" % (k, val))
                elif val == ref:
                    rep.check(True, "fee %s = %s equals the control's" % (k, val))
                elif v == 2:
                    rep.info("fee %s = %s, control %s (variant 2 may differ by its added bytes: reported, not a failure)" % (k, val, ref))
                else:
                    rep.check(False, "fee %s = %s, control %s" % (k, val, ref))
    rep.say()
    rep.say("Result: %s (%d check(s) failed)" % ("PASS" if not rep.failed else "FAIL", rep.failed))
    return rep


# ---------------------------------------------------------------- self-test

def synth_asset(variant, with_fee_tail=False):
    """An uploaded asset the way the viewer writes it (skin and physics first, then LODs)."""
    faces = lambda n: [{"Position": b"p" * n, "Normal": b"n" * n, "TexCoord0": b"t" * n}]
    blocks = {"physics_convex": {"HullList": b"h" * 20}}
    for i, lod in enumerate(LODS):
        blocks[lod] = faces(8 + i * 4)
    if variant == 2:
        blocks["lowest_lod"][0]["lodestar"] = V2_VALUE
    header, body, off = {}, b"", 0
    for name in ("physics_convex",) + LODS:
        z = zlib.compress(write_binary(blocks[name]))
        header[name] = {"offset": off, "size": len(z)}
        body += z
        off += len(z)
    if variant == 1:
        header["lodestar"] = {"version": 1, "values": V1_VALUES}
        header["high_lod"]["lodestar"] = {"version": 1}
    if variant == 3:
        header["lodestar_block"] = {"offset": off, "size": 64}
        body += V3_BLOCK + V3_TRAILER
    return write_binary(header) + body


def synth_store(up, survive):
    """What a server could do: re-serialise with LODs first, add its keys, drop what it does not know."""
    a = Asset(up)
    header, body, off = {}, b"", 0
    for name in LODS + ("physics_convex",):
        if name in a.blocks:
            raw = a.block(name)
            header[name] = {"offset": off, "size": len(raw)}
            body += raw
            off += len(raw)
    if "lodestar" in survive:
        if "lodestar" in a.header:
            header["lodestar"] = a.header["lodestar"]
        if isinstance(a.header.get("high_lod"), dict) and "lodestar" in a.header["high_lod"]:
            header["high_lod"]["lodestar"] = a.header["high_lod"]["lodestar"]
    if "lodestar_block" in survive and "lodestar_block" in a.header:
        header["lodestar_block"] = {"offset": off, "size": 64}
        body += a.block("lodestar_block")
    header["creator"] = "uuid:" + "ab" * 16
    header["date"] = Date(1.0e9 + len(up))
    return write_binary(header) + body


def self_test():
    ok = True

    def expect(cond, text):
        nonlocal ok
        print(("ok    " if cond else "FAIL  ") + text)
        ok = ok and cond

    # parser / writer round trip, including every tag
    sample = {"a": 1, "b": 2.5, "c": "x", "d": b"\x00\x01", "e": [True, False, None], "f": "uuid:" + "01" * 16, "g": Date(12.0)}
    expect(parse_binary(write_binary(sample))[0] == sample, "binary LLSD round trip")
    expect(read_xml_roundtrip(sample), "XML LLSD round trip")
    expect(detect_variant(Asset(synth_asset(0))) == 0, "detect variant 0")
    for v in (1, 2, 3):
        expect(detect_variant(Asset(synth_asset(v))) == v, "detect variant %d" % v)
    a3 = Asset(synth_asset(3))
    expect(a3.tail == V3_TRAILER and a3.block("lodestar_block") == V3_BLOCK, "variant 3 block and trailing bytes located")
    expect(Asset(DEPRECATED_HEADER + synth_asset(0)).header.keys() == Asset(synth_asset(0)).header.keys(), "deprecated header prefix skipped")

    # a server that drops unknown keys and trailing bytes but copies LOD blocks (the survey's prediction)
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        fees = {0: 10, 1: 10, 2: 11, 3: 10}
        for v in range(4):
            up = synth_asset(v)
            n = v + 1
            write_xml_llsd(d / ("whole_model_body_%d.xml" % n), {"mesh_list": [up]})
            fee = {"state": "upload", "upload_price": fees[v], "data": {"resource_cost": 1.5, "model_streaming_cost": 0.5, "simulation_cost": 0.25, "physics_cost": 0.1,
                                                                        "streaming_params": {lod: 100 + i for i, lod in enumerate(LODS)}}}
            write_xml_llsd(d / ("whole_model_fee_response_%d.xml" % n), fee)
            uuid = "%02x" % (v + 1) * 16
            write_xml_llsd(d / ("whole_model_upload_response_%d.xml" % n), {"state": "complete", "new_asset": "uuid:" + uuid})
            survive = set()   # the server copies the LOD blocks as is, so variant 2's value survives inside Lowest
            (d / ("lodestar-grid-%s.bin" % "-".join((uuid[:8], uuid[8:12], uuid[12:16], uuid[16:20], uuid[20:])))).write_bytes(synth_store(up, survive))
        rep = analyse(d)
        text = "\n".join(rep.lines)
        expect("variant 0: N=[1], variant 1: N=[2], variant 2: N=[3], variant 3: N=[4]" in text, "classifies the four uploads")
        expect("by new asset id" in text, "pairs stored assets by new asset id")
        expect(rep.failed == 2, "synthetic server: %d failing checks (expected 2: the variant 1 and variant 3 values are dropped)" % rep.failed)
        expect("  FAIL  value present in the stored asset: top-level map dropped, high_lod key dropped" in text, "variant 1 dropped is reported")
        expect("  PASS  value present in the stored asset: 64 bytes in Lowest first face map kept, identical" in text, "variant 2 kept is reported")
        expect("header key dropped, block missing, trailing 64 bytes dropped" in text, "variant 3 dropped is reported")
        expect("variant 2 may differ by its added bytes" in text, "variant 2 fee delta reported, not failed")
        expect("block order ['physics_convex', 'lowest_lod'" in text and "block order ['lowest_lod'" in text, "block order uploaded versus stored shown")

        # by-date fallback: no new_asset in the responses
        for n in range(1, 5):
            write_xml_llsd(d / ("whole_model_upload_response_%d.xml" % n), {"state": "complete"})
        text2 = "\n".join(analyse(d).lines)
        expect("by order of the header date" in text2, "falls back to header date order")

        # a server that keeps everything passes every check
        for p in d.glob("lodestar-grid-*.bin"):
            p.unlink()
        for v in range(4):
            uuid = "%02x" % (v + 1) * 16
            write_xml_llsd(d / ("whole_model_upload_response_%d.xml" % (v + 1)), {"state": "complete", "new_asset": "uuid:" + uuid})
            (d / ("lodestar-grid-%s.bin" % "-".join((uuid[:8], uuid[8:12], uuid[12:16], uuid[16:20], uuid[20:])))).write_bytes(
                synth_store(synth_asset(v), {"lodestar", "lodestar_block", "lowest_lod"}))
        rep3 = analyse(d)
        expect(rep3.failed == 0, "a server that keeps everything: 0 failing checks (%d)" % rep3.failed)
    print("self-test " + ("passed" if ok else "FAILED"))
    return 0 if ok else 1


def read_xml_roundtrip(sample):
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "x.xml"
        s = {k: v for k, v in sample.items() if k != "g"}
        s["e"] = [True, False]
        write_xml_llsd(p, s)
        return read_xml_llsd(p) == s


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("logs", nargs="?", help="the viewer's logs folder")
    ap.add_argument("--self-test", action="store_true", help="run on synthetic data")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.logs:
        ap.error("give the logs folder or --self-test")
    rep = analyse(args.logs)
    print("\n".join(rep.lines))
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

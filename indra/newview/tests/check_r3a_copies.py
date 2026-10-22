#!/usr/bin/env python3
"""Checks the frozen copies in llaisupdate_parity_stubs.cpp against the live source.

usage: python indra/newview/tests/check_r3a_copies.py

Each copy in the stubs file is fenced:

    // ---- VKS-COPY-BEGIN <file> <lines> blob <hash> : <what>
    ...verbatim text...
    // ---- VKS-COPY-END

Lines ending in "// VKS-HARNESS" are the harness's additions (recording) and
are dropped before the comparison. The copy's first line is a signature (or a
class head); the live text is found by that line in indra/newview/<file> and
taken through its matching closing brace. Both sides are compared after
whitespace normalisation (leading and trailing blanks dropped, runs of blanks
collapsed, empty lines dropped). The line range and blob hash in the marker
are those at copy time; a changed blob is reported as information, a changed
text as a failure.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NEWVIEW = os.path.dirname(HERE)
STUBS = os.path.join(HERE, "llaisupdate_parity_stubs.cpp")
BEGIN = re.compile(r"^// ---- VKS-COPY-BEGIN (\S+) (\S+) blob ([0-9a-f]{40}) : (.*)$")


def norm(lines):
    out = []
    for line in lines:
        s = " ".join(line.split())
        if s:
            out.append(s)
    return out


def live_region(lines, signature):
    sig = " ".join(signature.split())
    for i, line in enumerate(lines):
        if " ".join(line.split()) == sig:
            depth = 0
            seen = False
            for j in range(i, len(lines)):
                for ch in lines[j]:
                    if ch == "{":
                        depth += 1
                        seen = True
                    elif ch == "}":
                        depth -= 1
                if seen and depth == 0:
                    return lines[i:j + 1]
            return None
    return None


def blob_hash(path):
    try:
        return subprocess.run(["git", "hash-object", path], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "?"


def main():
    with open(STUBS, encoding="utf-8") as f:
        stub = f.read().splitlines()
    copies = []
    i = 0
    while i < len(stub):
        m = BEGIN.match(stub[i])
        if m:
            j = i + 1
            while j < len(stub) and not stub[j].startswith("// ---- VKS-COPY-END"):
                j += 1
            body = [x for x in stub[i + 1:j] if not x.rstrip().endswith("// VKS-HARNESS")]
            copies.append((m.group(1), m.group(2), m.group(3), m.group(4), body))
            i = j
        i += 1

    failures = 0
    blobs = {}
    for fname, rng, blob, what, body in copies:
        path = os.path.join(NEWVIEW, fname)
        if path not in blobs:
            with open(path, encoding="utf-8", errors="replace") as f:
                blobs[path] = (f.read().splitlines(), blob_hash(path))
        live_lines, live_blob = blobs[path]
        signature = next(x for x in body if x.strip())
        live = live_region(live_lines, signature)
        note = "" if live_blob == blob else " (blob now %s)" % live_blob
        if live is None:
            print("FAIL %s %s %s: signature not found: %s" % (fname, rng, what, signature.strip()))
            failures += 1
            continue
        a, b = norm(body), norm(live)
        if a != b:
            failures += 1
            k = next((n for n in range(min(len(a), len(b))) if a[n] != b[n]), min(len(a), len(b)))
            print("FAIL %s %s %s%s" % (fname, rng, what, note))
            print("  copy: %s" % (a[k] if k < len(a) else "(end)"))
            print("  live: %s" % (b[k] if k < len(b) else "(end)"))
        else:
            print("ok   %s %s %s%s" % (fname, rng, what, note))
    print("%d copies, %d differ" % (len(copies), failures))
    return 1 if failures or not copies else 0


if __name__ == "__main__":
    sys.exit(main())

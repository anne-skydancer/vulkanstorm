#!/usr/bin/env python3
"""Checks that every frozen copy in llinventory_oracle.{h,cpp} still matches
the live source it was copied from (vkstorm-rust rung seven, recorder R1).

Each copied region in the oracle files starts with

    // ---- VKS-ORACLE-COPY BEGIN
    // source: indra/<path>
    // lines: <first>-<last>
    // blob: <git hash-object of the source at copy time>
    // copied: <what>
    // substitutions: ...
    // ----

and ends with "// ---- VKS-ORACLE-COPY END". The only substitutions are the
removal of #include lines and of the forward declarations
"class LLMessageSystem;" and "class LLUUID;" (which inside the namespace would
declare new, incomplete classes); the namespace itself is outside the regions.

The check compares the region with the named line range of the live source,
whitespace-normalised (runs of blanks collapsed, blank lines dropped) after
the same removals. When the live file has changed (different blob) but the
region still matches, the copy is reported as current with a moved/edited
note; when the text differs, the differing lines are printed.

After the port replaces the live sources, check the copies against the
commit they were made from instead: --at <commit> reads every source with
"git show <commit>:<path>" (the copies were made at 472ecf270d).

usage: python check_r1_copies.py [--at <commit>] [viewer root]   (default root: the repository of this file)
exit status: 0 when every region matches.
"""
import difflib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLE_FILES = ["llinventory_oracle.h", "llinventory_oracle.cpp"]
INCLUDE_RE = re.compile(r"^\s*#\s*include\b")
FWD_RE = re.compile(r"^\s*class\s+(LLMessageSystem|LLUUID)\s*;\s*$")


def normalise(lines):
    out = []
    for line in lines:
        if INCLUDE_RE.match(line) or FWD_RE.match(line):
            continue
        n = " ".join(line.split())
        if n:
            out.append(n)
    return out


def regions(path):
    with open(path, "rb") as f:
        lines = f.read().decode("latin-1").split("\n")
    i = 0
    while i < len(lines):
        if lines[i].strip() == "// ---- VKS-ORACLE-COPY BEGIN":
            meta = {}
            j = i + 1
            while lines[j].strip() != "// ----":
                m = re.match(r"^// (\w+): (.*)$", lines[j].strip())
                if m:
                    meta[m.group(1)] = m.group(2)
                j += 1
            k = j + 1
            while lines[k].strip() != "// ---- VKS-ORACLE-COPY END":
                k += 1
            yield meta, lines[j + 1:k]
            i = k + 1
        else:
            i += 1


def main():
    args = sys.argv[1:]
    at = None
    if len(args) >= 2 and args[0] == "--at":
        at = args[1]
        args = args[2:]
    root = args[0] if args else subprocess.check_output(
        ["git", "-C", HERE, "rev-parse", "--show-toplevel"]).decode().strip()
    failures = 0
    count = 0
    for name in ORACLE_FILES:
        for meta, body in regions(os.path.join(HERE, name)):
            count += 1
            src = meta["source"]
            first, last = (int(x) for x in meta["lines"].split("-"))
            if at:
                data = subprocess.check_output(["git", "-C", root, "show", "%s:%s" % (at, src)])
                blob = subprocess.check_output(["git", "-C", root, "rev-parse", "%s:%s" % (at, src)]).decode().strip()
            else:
                with open(os.path.join(root, src), "rb") as f:
                    data = f.read()
                blob = subprocess.check_output(["git", "-C", root, "hash-object", src]).decode().strip()
            live = data.decode("latin-1").split("\n")
            expect = normalise(live[first - 1:last])
            got = normalise(body)
            label = "%s %s:%s (%s)" % (name, src, meta["lines"], meta.get("copied", ""))
            if expect == got:
                note = "" if blob == meta["blob"] else " [source blob changed from %s to %s; region text unchanged]" % (meta["blob"][:10], blob[:10])
                print("OK   %s%s" % (label, note))
            else:
                failures += 1
                print("DIFF %s: the copy no longer matches the live source (blob %s, copied from %s)" % (label, blob[:10], meta["blob"][:10]))
                for d in list(difflib.unified_diff(got, expect, "copy", "live", lineterm="", n=1))[:40]:
                    print("     " + d)
    print("%d regions checked, %d differ" % (count, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

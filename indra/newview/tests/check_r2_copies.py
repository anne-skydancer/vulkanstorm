#!/usr/bin/env python3
"""Checks recorder R2's frozen copies against the live viewer source.

usage: python indra/newview/tests/check_r2_copies.py [repository root]

llinventorymodel_parity_generic.cpp holds code copied verbatim from viewer
sources that the parity test cannot link (the GenericMessage handlers of
llviewergenericmessage.cpp and the inventory part of
LLAppViewer::disconnectViewer). Each copy is delimited by

    // FROZEN COPY begin: <source path>, <function>, lines <a>-<b>, blob <hash>
    ...
    // FROZEN COPY end

The check compares each copy, whitespace-normalised, with the live source:
first at the recorded lines, then anywhere in the file (the code may have
moved). No substitutions are applied: the one documented substitution
(disconnectViewer's member mSecondInstance becomes a parameter of the same
name) leaves the copied text unchanged. Exit status 0 when every copy
matches; the blob hash differing from the recorded one is reported but is
not a failure when the copied text still matches.
"""
import os
import re
import subprocess
import sys

COPY_FILE = os.path.join("indra", "newview", "tests", "llinventorymodel_parity_generic.cpp")
BEGIN = re.compile(r"//\s*FROZEN COPY begin:\s*(?P<path>[^,]+),\s*(?P<func>[^,]+),\s*lines\s*(?P<a>\d+)-(?P<b>\d+),\s*blob\s*(?P<blob>[0-9a-f]+)")


def norm(lines):
    return [" ".join(l.split()) for l in lines if l.strip()]


def blob_of(root, path):
    try:
        return subprocess.run(["git", "-C", root, "hash-object", path], check=True,
                              capture_output=True, text=True).stdout.strip()
    except Exception:
        return "?"


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    with open(os.path.join(root, COPY_FILE), encoding="utf-8") as f:
        text = f.read().splitlines()
    copies = []
    i = 0
    while i < len(text):
        m = BEGIN.search(text[i])
        if m:
            j = i + 1
            while j < len(text) and "FROZEN COPY end" not in text[j]:
                j += 1
            copies.append((m, text[i + 1:j]))
            i = j
        i += 1
    failures = 0
    for m, body in copies:
        path = m.group("path").strip()
        live_path = os.path.join(root, path)
        with open(live_path, encoding="utf-8", errors="replace") as f:
            live = f.read().splitlines()
        a, b = int(m.group("a")), int(m.group("b"))
        want = norm(body)
        at_lines = norm(live[a - 1:b])
        where = None
        if at_lines == want:
            where = "at lines %d-%d" % (a, b)
        else:
            nl = norm(live)
            for k in range(len(nl) - len(want) + 1):
                if nl[k:k + len(want)] == want:
                    where = "moved (found elsewhere in the file)"
                    break
        blob = blob_of(root, path)
        blob_note = "blob unchanged" if blob == m.group("blob") else "blob now %s (recorded %s)" % (blob, m.group("blob"))
        if where:
            print("OK    %s %s: %s; %s" % (path, m.group("func").strip(), where, blob_note))
        else:
            failures += 1
            print("FAIL  %s %s: the copy no longer matches the live source; %s" % (path, m.group("func").strip(), blob_note))
    print("%d copies checked, %d failing" % (len(copies), failures))
    return 1 if failures or not copies else 0


if __name__ == "__main__":
    sys.exit(main())

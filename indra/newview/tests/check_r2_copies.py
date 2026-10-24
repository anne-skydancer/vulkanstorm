#!/usr/bin/env python3
"""Checks recorder R2's frozen copies against the live viewer source.

usage: python indra/newview/tests/check_r2_copies.py [repository root]

INTEGRATION_TEST_llinventorymodel_parity runs this script in its closing
test, so it is never only a manual step.

llinventorymodel_parity_generic.cpp holds code copied verbatim from viewer
sources the parity test cannot link (the GenericMessage handlers of
llviewergenericmessage.cpp and the inventory part of
LLAppViewer::disconnectViewer). Each copy is delimited by

    // FROZEN COPY begin: <source path>, <function>, lines <a>-<b>, blob <hash>
    ...
    // FROZEN COPY end

Comparison (both sides):
  - comments and "#if 0 ... #endif" blocks are removed; string and character
    literals are kept verbatim; outside literals whitespace runs collapse to
    one space within a line; blank lines are dropped;
  - a "//" comment containing ';' inside a copied region of the live source
    (a statement folded onto a comment line) is flagged as a failure;
  - each region must match exactly once in the live file.

Identifier fence: every live line (comments removed) that mentions one of a
source's guarded identifiers must lie inside a matched region or in the
allowlist below; otherwise new code that touches the copied behaviour has
appeared outside the copies.

Substitutions: none. disconnectViewer's member mSecondInstance becomes a
parameter of the same name, which leaves the copied text unchanged.

Exit status 0 when everything matches. A changed blob hash is reported but is
not a failure when the copied text still matches.
"""
import os
import re
import subprocess
import sys

COPY_FILE = os.path.join("indra", "newview", "tests", "llinventorymodel_parity_generic.cpp")
BEGIN = re.compile(r"//\s*FROZEN COPY begin:\s*(?P<path>[^,]+),\s*(?P<func>[^,]+),\s*lines\s*(?P<a>\d+)-(?P<b>\d+),\s*blob\s*(?P<blob>[0-9a-f]+)")

# source path -> (guarded identifiers, allowlisted live lines (normalised))
FENCES = {
    "indra/newview/llviewergenericmessage.cpp": (
        ["gGenericDispatcher", "unpackMessage", "unpackLargeMessage"],
        ["LLDispatcher gGenericDispatcher;"],
    ),
    "indra/newview/llappviewer.cpp": (
        ["gInventory.cache("],
        [],
    ),
}


def normalise(lines, flag_folded=False):
    """Returns [(line number, normalised text)] and the folded-comment lines."""
    out = []
    folded = []
    in_block = False
    if0_depth = 0
    for no, raw in enumerate(lines, 1):
        stripped = raw.strip()
        if if0_depth:
            if re.match(r"#\s*if", stripped):
                if0_depth += 1
            elif re.match(r"#\s*endif", stripped):
                if0_depth -= 1
            continue
        if not in_block and re.match(r"#\s*if\s+0\b", stripped):
            if0_depth = 1
            continue
        text = []
        i = 0
        n = len(raw)
        while i < n:
            if in_block:
                end = raw.find("*/", i)
                if end < 0:
                    i = n
                    break
                in_block = False
                i = end + 2
                text.append(" ")
                continue
            ch = raw[i]
            if ch in "\"'":
                j = i + 1
                while j < n and raw[j] != ch:
                    j += 2 if raw[j] == "\\" else 1
                text.append(raw[i:j + 1])
                i = j + 1
                continue
            if raw.startswith("//", i):
                if flag_folded and ";" in raw[i + 2:]:
                    folded.append(no)
                break
            if raw.startswith("/*", i):
                in_block = True
                i += 2
                continue
            text.append(ch)
            i += 1
        joined = "".join(text)
        # collapse whitespace outside literals
        parts = re.split(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')', joined)
        parts = [p if k % 2 else " ".join(p.split()) for k, p in enumerate(parts)]
        line = "".join(parts).strip()
        if line:
            out.append((no, line))
    return out, folded


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
    covered = {}  # path -> set of live line numbers in matched regions
    for m, body in copies:
        path = m.group("path").strip()
        func = m.group("func").strip()
        with open(os.path.join(root, path), encoding="utf-8", errors="replace") as f:
            live_raw = f.read().splitlines()
        live, _ = normalise(live_raw)
        want = [t for _, t in normalise(body)[0]]
        texts = [t for _, t in live]
        hits = [k for k in range(len(texts) - len(want) + 1) if texts[k:k + len(want)] == want]
        blob = blob_of(root, path)
        blob_note = "blob unchanged" if blob == m.group("blob") else "blob now %s (recorded %s)" % (blob, m.group("blob"))
        if len(hits) != 1:
            failures += 1
            print("FAIL  %s %s: %d matches in the live source (exactly one required); %s" % (path, func, len(hits), blob_note))
            continue
        first_no = live[hits[0]][0]
        last_no = live[hits[0] + len(want) - 1][0]
        _, folded = normalise(live_raw[first_no - 1:last_no], flag_folded=True)
        if folded:
            failures += 1
            print("FAIL  %s %s: statement folded onto a // comment at live line(s) %s" % (path, func, [first_no - 1 + k for k in folded]))
            continue
        covered.setdefault(path, set()).update(range(first_no, last_no + 1))
        where = "at lines %d-%d" % (first_no, last_no)
        if (first_no, last_no) != (int(m.group("a")), int(m.group("b"))):
            where += " (recorded %s-%s)" % (m.group("a"), m.group("b"))
        print("OK    %s %s: %s; %s" % (path, func, where, blob_note))
    for path, (idents, allow) in FENCES.items():
        with open(os.path.join(root, path), encoding="utf-8", errors="replace") as f:
            live, _ = normalise(f.read().splitlines())
        for no, line in live:
            if any(ident in line for ident in idents):
                if no in covered.get(path, set()) or line in allow:
                    continue
                failures += 1
                print("FAIL  fence %s line %d touches a guarded identifier outside the copies: %s" % (path, no, line))
    print("%d copies checked, %d failing" % (len(copies), failures))
    return 1 if failures or not copies else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Checks the task-inventory oracle's frozen copies against the live source.

usage: python indra/newview/tests/check_r4a_copies.py

Run by INTEGRATION_TEST_llinventory_task_oracle itself (test 8), so a drift
fails the build's test step.

Regions. Each region of tests/llinventory_task_oracle.cpp (copied code) and
tests/llinventory_task_oracle_decls.h (live declarations and constructor and
destructor lines the oracle mirrors by hand) between
    // >>> VKS-R4A-COPY <id> <path> lines <first>-<last> blob <hash>
and
    // <<< VKS-R4A-COPY <id>
is the text of lines <first>-<last> of <path> as it was at blob <hash>; the
line after the header names what it holds. Nothing in a region is edited:
the copies resolve the viewer singletons to the oracle's fakes by name lookup.

A region passes when, after normalisation, its lines occur exactly once, as
consecutive lines, in the normalised live file. Normalisation, applied to
both sides:
  - comments (// and /* */) are removed; string and character literals are
    kept verbatim (a "//" in a string is not a comment);
  - outside literals, whitespace within a line collapses to one space and is
    trimmed; lines left empty are dropped; line breaks are significant;
  - in the live file only, lines inside "#if 0" blocks (up to the matching
    #else, #elif or #endif) are dropped.
So reindenting is accepted; a statement folded onto a // comment line, code
moved under #if 0 or /* */ with a changed version after it, a duplicated
region, or any change of a token or literal is a failure.

Identifier fence. Every line of the live llviewerobject.cpp and
llviewerobject.h (after the same normalisation) that names one of the
task-inventory members (mInventory*, mInvRequest*, mExpectedInventorySerialNum,
mPendingInventoryItemsIDs, mInventoryCallbacks) must lie within a checked
region or be listed in ALLOWLIST below; otherwise new code touching the state
outside the frozen regions would go unnoticed.

The script also reports where each region now lives and whether its file's
blob changed, and exits non-zero on any failure.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REGION_FILES = [os.path.join(HERE, "llinventory_task_oracle.cpp"),
                os.path.join(HERE, "llinventory_task_oracle_decls.h")]
FENCE_FILES = ["indra/newview/llviewerobject.cpp", "indra/newview/llviewerobject.h"]
FENCE = re.compile(r"\b(mInventory\w*|mInvRequest\w*|mExpectedInventorySerialNum|mPendingInventoryItemsIDs)\b")
# (path, normalised line text) pairs allowed outside the regions. Empty: every
# live use is inside a checked region.
ALLOWLIST = []

HEAD = re.compile(r"^// >>> VKS-R4A-COPY (\S+) (\S+) lines (\d+)-(\d+) blob ([0-9a-f]{40})$")
TAIL = re.compile(r"^// <<< VKS-R4A-COPY (\S+)$")
IF0 = re.compile(r"^#\s*if\s+0\b")
IFX = re.compile(r"^#\s*if(n?def)?\b")
ELSE = re.compile(r"^#\s*(else|elif)\b")
ENDIF = re.compile(r"^#\s*endif\b")


def normalise(text, strip_if0):
    """Returns one entry per input line: the line with comments removed,
    whitespace outside literals collapsed, trimmed; '' when nothing is left
    (or the line is inside an #if 0 block and strip_if0)."""
    out = []
    cur = []
    pending_space = False
    state = "code"  # code, line, block, str, chr
    i = 0
    n = len(text)

    def emit(ch):
        nonlocal pending_space
        if pending_space and cur:
            cur.append(" ")
        pending_space = False
        cur.append(ch)

    while i < n:
        c = text[i]
        if c == "\n":
            if state == "line":
                state = "code"
            out.append("".join(cur).strip())
            cur = []
            pending_space = False
            i += 1
            continue
        if state == "code":
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                state = "line"
                pending_space = True
                i += 2
                continue
            if c == "/" and i + 1 < n and text[i + 1] == "*":
                state = "block"
                pending_space = True
                i += 2
                continue
            if c in " \t\r\f\v":
                pending_space = True
                i += 1
                continue
            if c == '"':
                state = "str"
            elif c == "'":
                state = "chr"
            emit(c)
            i += 1
            continue
        if state == "line":
            i += 1
            continue
        if state == "block":
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                state = "code"
                pending_space = True
                i += 2
            else:
                i += 1
            continue
        # inside a string or character literal: verbatim
        cur.append(c)
        if c == "\\" and i + 1 < n and text[i + 1] != "\n":
            cur.append(text[i + 1])
            i += 2
            continue
        if (state == "str" and c == '"') or (state == "chr" and c == "'"):
            state = "code"
        i += 1
    out.append("".join(cur).strip())

    if strip_if0:
        depth = 0
        for k, line in enumerate(out):
            if depth == 0:
                if IF0.match(line):
                    depth = 1
                    out[k] = ""
                continue
            if IFX.match(line):
                depth += 1
            elif ENDIF.match(line):
                depth -= 1
            elif depth == 1 and ELSE.match(line):
                depth = 0
            out[k] = ""
    return out


def blob(path):
    try:
        return subprocess.check_output(["git", "-C", REPO, "hash-object", path], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "?"


def regions():
    out = []
    for path in REGION_FILES:
        lines = open(path, encoding="utf-8").read().replace("\r", "").split("\n")
        i = 0
        while i < len(lines):
            m = HEAD.match(lines[i])
            if not m:
                i += 1
                continue
            rid, live, first, last, recorded_blob = m.groups()
            j = i + 2  # the line after the header names what the region holds
            body = []
            while j < len(lines) and not TAIL.match(lines[j]):
                body.append(lines[j])
                j += 1
            if j >= len(lines) or TAIL.match(lines[j]).group(1) != rid:
                raise SystemExit("unterminated or mismatched copy region %s in %s" % (rid, path))
            out.append((rid, live, int(first), int(last), recorded_blob, "\n".join(body)))
            i = j + 1
    return out


live_cache = {}


def live(path):
    if path not in live_cache:
        text = open(os.path.join(REPO, path), encoding="utf-8").read().replace("\r", "")
        norm = normalise(text, True)
        seq = [(k + 1, t) for k, t in enumerate(norm) if t]
        live_cache[path] = (norm, seq)
    return live_cache[path]


def find_all(seq, want):
    texts = [t for _, t in seq]
    n = len(want)
    hits = []
    if n == 0:
        return hits
    first = want[0]
    for s in range(0, len(texts) - n + 1):
        if texts[s] == first and texts[s:s + n] == want:
            hits.append((seq[s][0], seq[s + n - 1][0]))
    return hits


def main():
    regs = regions()
    if not regs:
        print("no copy regions found")
        return 1
    failures = 0
    spans = {}
    for rid, path, first, last, recorded_blob, body in regs:
        body_norm = normalise(body, False)
        want = [t for t in body_norm if t]
        code_idx = [k for k, t in enumerate(body_norm) if t]
        expected_span = (first + code_idx[0], first + code_idx[-1]) if code_idx else (first, last)
        _, seq = live(path)
        hits = find_all(seq, want)
        notes = []
        current_blob = blob(os.path.join(REPO, path))
        if current_blob != recorded_blob:
            notes.append("file blob changed %s -> %s" % (recorded_blob[:10], current_blob[:10]))
        if len(hits) == 1:
            where = hits[0]
            spans.setdefault(path, []).append(where)
            if where != expected_span:
                notes.append("code now at lines %d-%d (recorded %d-%d)" % (where + expected_span))
            status = "OK  "
        else:
            failures += 1
            status = "FAIL"
            notes.insert(0, "matches %d times in the live file (must be exactly once)" % len(hits))
        print("%s %s %s lines %d-%d (%d normalised lines)%s" % (
            status, rid, path, first, last, len(want), ("; " + "; ".join(notes)) if notes else ""))

    fence_failures = 0
    for path in FENCE_FILES:
        norm, _ = live(path)
        covered = spans.get(path, [])
        for k, text in enumerate(norm):
            if not text or not FENCE.search(text):
                continue
            line = k + 1
            if any(a <= line <= b for a, b in covered):
                continue
            if (path, text) in ALLOWLIST:
                continue
            fence_failures += 1
            print("FENCE %s:%d uses task-inventory state outside the checked regions: %s" % (path, line, text))
    print("%d regions, %d identical, %d differ; identifier fence: %d line(s) outside the regions"
          % (len(regs), len(regs) - failures, failures, fence_failures))
    return 1 if failures or fence_failures else 0


if __name__ == "__main__":
    sys.exit(main())

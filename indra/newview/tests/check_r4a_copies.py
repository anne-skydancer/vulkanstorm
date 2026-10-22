#!/usr/bin/env python3
"""Checks the frozen copies in llinventory_task_oracle.cpp against the live source.

usage: python indra/newview/tests/check_r4a_copies.py

Each region of tests/llinventory_task_oracle.cpp between
    // >>> VKS-R4A-COPY <id> <path> lines <first>-<last> blob <hash>
and
    // <<< VKS-R4A-COPY <id>
is a verbatim copy of lines <first>-<last> of <path> as it was at blob <hash>
(the second header line names the copied functions). No substitutions are
made in the copied text: the copies resolve the viewer singletons and classes
they use to the fakes in llinventory_task_oracle.h by ordinary name lookup in
the vks_task_oracle namespace.

A region passes when its whitespace-normalised text (every run of whitespace
collapsed to one space) occurs in the whitespace-normalised live file. The
script also reports where the region now lives, whether the line range and
the file's blob hash are still the recorded ones, and exits non-zero if any
region is missing.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
COPIES = os.path.join(HERE, "llinventory_task_oracle.cpp")

HEAD = re.compile(r"^// >>> VKS-R4A-COPY (\S+) (\S+) lines (\d+)-(\d+) blob ([0-9a-f]{40})$")
TAIL = re.compile(r"^// <<< VKS-R4A-COPY (\S+)$")


def norm(text):
    return " ".join(text.split())


def blob(path):
    try:
        return subprocess.check_output(["git", "-C", REPO, "hash-object", path], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "?"


def regions():
    lines = open(COPIES, encoding="utf-8").read().split("\n")
    out = []
    i = 0
    while i < len(lines):
        m = HEAD.match(lines[i])
        if not m:
            i += 1
            continue
        rid, path, first, last, recorded_blob = m.groups()
        # the line after the header names the functions; the copy follows it
        j = i + 2
        body = []
        while j < len(lines) and not TAIL.match(lines[j]):
            body.append(lines[j])
            j += 1
        if j >= len(lines) or TAIL.match(lines[j]).group(1) != rid:
            raise SystemExit("unterminated or mismatched copy region %s" % rid)
        out.append((rid, path, int(first), int(last), recorded_blob, "\n".join(body)))
        i = j + 1
    return out


def locate(live_lines, body_lines):
    """1-based line range of body in live, comparing normalised lines."""
    want = [norm(l) for l in body_lines]
    have = [norm(l) for l in live_lines]
    n = len(want)
    for s in range(0, len(have) - n + 1):
        if have[s:s + n] == want:
            return s + 1, s + n
    return None


def main():
    failures = 0
    regs = regions()
    if not regs:
        print("no copy regions found in", COPIES)
        return 1
    for rid, path, first, last, recorded_blob, body in regs:
        live_path = os.path.join(REPO, path)
        live = open(live_path, encoding="utf-8").read().replace("\r", "")
        ok = norm(body) in norm(live)
        live_lines = live.split("\n")
        where = locate(live_lines, body.split("\n")) if ok else None
        current_blob = blob(live_path)
        notes = []
        if where and where != (first, last):
            notes.append("now at lines %d-%d" % where)
        if current_blob != recorded_blob:
            notes.append("file blob changed %s -> %s" % (recorded_blob[:10], current_blob[:10]))
        status = "OK  " if ok else "FAIL"
        if not ok:
            failures += 1
        print("%s %s %s lines %d-%d (%d lines)%s" % (
            status, rid, path, first, last, len(body.split("\n")),
            ("; " + "; ".join(notes)) if notes else ""))
    print("%d regions, %d identical, %d differ" % (len(regs), len(regs) - failures, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

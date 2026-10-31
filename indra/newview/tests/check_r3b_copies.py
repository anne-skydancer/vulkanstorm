#!/usr/bin/env python3
"""Checks recorder R3b's frozen copies against the live viewer source.

usage: python indra/newview/tests/check_r3b_copies.py [repository root]

Run by INTEGRATION_TEST_llinvfetch_parity (test 1) on every test run.

llinvfetch_parity_support.cpp holds code copied verbatim from viewer
sources the fetch parity test cannot link: the inventory steps of
llstartup.cpp (STATE_INVENTORY_SEND / SKEL / SEND2 / CALLBACKS, the lost
items request) and the COF fetch helpers of llappearancemgr.cpp
(callAfterCOFFetch, callAfterCategoryFetch, callAfterCategoryLinksFetch,
CallAfterCategoryFetchStage1's constructor and startFetch,
LLAppearanceMgr::getCOF and initCOFID). Each copy is delimited by

    // FROZEN COPY begin: <source path>, <function>, lines <a>-<b>, blob <hash>
    ...
    // FROZEN COPY end

There are no textual substitutions: the names the copies call that differ
(do_startup_frame, nullary_func_t, CallAfterCategoryFetchStage1::done) are
defined in the test's own namespace and documented in the fixture README.

The comparison:
  * comments (// and /* */, outside literals) and #if 0 blocks are removed
    from both texts; whitespace is collapsed outside string and char
    literals, within each line; literals are compared verbatim; empty lines
    are dropped;
  * the copy's lines must occur exactly once in the live file (anywhere: the
    code may move; the recorded line range is reported, not required);
  * a // comment containing a ';' in a copied region or in the live text it
    matches is flagged: a statement folded onto a comment line;
  * an identifier fence guards what the copies depend on outside the copied
    functions: every live line that mentions a fenced identifier must lie
    inside a region that matched, or be in the allowlist below (with the
    reason it does not change what the copies do); an allowlisted line may
    occur once outside the regions (a second copy of it fails).

Frozen originals. Sub-rung R3b moved the scheduler of
llinventorymodelbackgroundfetch.{h,cpp} to Rust; the bench of
INTEGRATION_TEST_llinvfetch_parity (test 3) compares it with the C++ as it
was, kept in llinvfetch_bench_frozen.cpp between

    // FROZEN ORIGINAL begin: <source path>, blob <hash>
    ...
    // FROZEN ORIGINAL end

(the whole file at the commit R3b started from, 3846816a16, with its
#include lines removed). The live file no longer holds that code, so each
region is checked against the SHA-256 of the original's normalised text
(as above, #include lines removed), recorded in FROZEN_ORIGINALS: no git
history is needed, so shallow clones check it too.

Exit status 0 when everything matches. A blob differing from the recorded
one is reported but is not a failure when the copied text still matches.
"""
import hashlib
import os
import re
import subprocess
import sys

COPY_FILE = os.path.join("indra", "newview", "tests", "llinvfetch_parity_support.cpp")
FROZEN_FILE = os.path.join("indra", "newview", "tests", "llinvfetch_bench_frozen.cpp")
FROZEN_BEGIN = re.compile(r"//\s*FROZEN ORIGINAL begin:\s*(?P<path>[^,]+),\s*blob\s*(?P<blob>[0-9a-f]+)")
INCLUDE_RE = re.compile(r"^\s*#\s*include\b")
# The SHA-256 of each original's normalised lines (prepare(), #include lines
# removed, joined with newlines), at 3846816a16.
FROZEN_ORIGINALS = {
    "indra/newview/llinventorymodelbackgroundfetch.h": "2f4116f648a5368b67e1fbeb85e66fc0dc3d5ba573ad5b0b4aae00c77b69464f",
    "indra/newview/llinventorymodelbackgroundfetch.cpp": "1f185a44a774cdefcb321287d5f096b4bda8e08f659aaa48ba38b4566c3f2ab4",
}
BEGIN = re.compile(r"//\s*FROZEN COPY begin:\s*(?P<path>[^,]+),\s*(?P<func>[^,]+),\s*lines\s*(?P<a>\d+)-(?P<b>\d+),\s*blob\s*(?P<blob>[0-9a-f]+)")

# Live lines that mention a fenced identifier outside the copied regions,
# with the reason they do not affect the copies (normalised text).
FENCES = {
    "indra/newview/llappearancemgr.cpp": {
        "identifiers": ["mCOFID", "CallAfterCategoryFetchStage1", "callAfterCOFFetch", "callAfterCategoryFetch",
                        "callAfterCategoryLinksFetch"],
        "allow": {
            "mCOFID.setNull();": "LLAppearanceMgr::cleanup at logout",
            "class CallAfterCategoryFetchStage1: public LLInventoryFetchDescendentsObserver": "the class head; its base is linked",
            "~CallAfterCategoryFetchStage1()": "the empty destructor",
            "CallAfterCategoryFetchStage1* stage1 = new CallAfterCategoryFetchStage1(cat_id, cb);": "inside callAfterCategoryFetch (copied); listed for the declaration form",
        },
        "allow_patterns": [
            # callers of the COF helpers elsewhere in the file (wearing,
            # outfits): they call the copied functions, they do not change them
            r"callAfterCategoryFetch\(",
            r"callAfterCategoryLinksFetch\(",
            r"callAfterCOFFetch\(",
        ],
    },
    "indra/newview/llappearancemgr.h": {
        "identifiers": ["mCOFID"],
        "allow": {"LLUUID mCOFID;": "the member declaration (type checked by the compiler)"},
        "allow_patterns": [],
    },
    "indra/newview/llstartup.cpp": {
        "identifiers": ["findLostItems", "loadSkeleton", "createCommonSystemCategories", "isSysFoldersReady",
                        "inventory-lib-root", "inventory-lib-owner", "inventory-skel-lib", "inventory-skeleton",
                        "initCOFID", "LLInventoryModelBackgroundFetch"],
        "allow": {
            'LL_WARNS("AppInit") << "Problem loading inventory-skel-lib" << LL_ENDL;': "a log line inside the copied STATE_INVENTORY_SKEL",
            "LLAppearanceMgr::getInstance()->initCOFID();": "a later re-initialisation of the COF id (not on the login fetch path)",
            "LLInventoryModelBackgroundFetch::instance().start();": "set_flags_and_update_appearance's start() after login (a no-op once the walk started; survey 3 section 2.8)",
        },
        "allow_patterns": [],
    },
}


def strip_comments(text):
    """Removes // and /* */ comments outside string and char literals.
    Returns (text, folded) where folded lists (line number, comment) of //
    comments containing ';'."""
    out = []
    folded = []
    i, n = 0, len(text)
    line = 1
    state = None  # None, '"', "'", '//', '/*'
    comment_start = 0
    while i < n:
        c = text[i]
        if state is None:
            if c == '/' and i + 1 < n and text[i + 1] == '/':
                state = '//'
                comment_start = i
                i += 2
                continue
            if c == '/' and i + 1 < n and text[i + 1] == '*':
                state = '/*'
                i += 2
                continue
            if c in ('"', "'"):
                state = c
            out.append(c)
        elif state in ('"', "'"):
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == state or c == '\n':
                state = None
        elif state == '//':
            if c == '\n':
                comment = text[comment_start:i]
                if ';' in comment:
                    folded.append((line, comment.strip()))
                state = None
                out.append(c)
        elif state == '/*':
            if c == '*' and i + 1 < n and text[i + 1] == '/':
                state = None
                i += 2
                continue
            if c == '\n':
                out.append(c)
        if c == '\n':
            line += 1
        i += 1
    return "".join(out), folded


def strip_if0(lines):
    out = []
    depth = 0
    for l in lines:
        s = l.strip()
        if depth == 0 and re.match(r"#\s*if\s+0\b", s):
            depth = 1
            out.append("")
            continue
        if depth:
            if re.match(r"#\s*if", s):
                depth += 1
            elif re.match(r"#\s*endif", s):
                depth -= 1
            out.append("")
            continue
        out.append(l)
    return out


def norm_line(l):
    """Collapses whitespace outside literals; literals verbatim."""
    out = []
    i, n = 0, len(l)
    pending_space = False
    while i < n:
        c = l[i]
        if c in ('"', "'"):
            j = i + 1
            while j < n and l[j] != c:
                j += 2 if l[j] == '\\' else 1
            if pending_space and out:
                out.append(' ')
            pending_space = False
            out.append(l[i:j + 1])
            i = j + 1
            continue
        if c.isspace():
            pending_space = True
            i += 1
            continue
        if pending_space and out:
            out.append(' ')
        pending_space = False
        out.append(c)
        i += 1
    return "".join(out)


def prepare(text):
    """Returns (normalised non-empty lines, their 1-based source line numbers, folded comments)."""
    stripped, folded = strip_comments(text)
    lines = strip_if0(stripped.split("\n"))
    norm, numbers = [], []
    for k, l in enumerate(lines):
        nl = norm_line(l)
        if nl:
            norm.append(nl)
            numbers.append(k + 1)
    return norm, numbers, folded


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
            copies.append((m, "\n".join(text[i + 1:j]), i + 2))
            i = j
        i += 1
    failures = 0
    live_cache = {}
    matched_spans = {}  # path -> list of (first, last) source lines
    for m, body, copy_line in copies:
        path = m.group("path").strip().replace("\\", "/")
        func = m.group("func").strip()
        if path not in live_cache:
            with open(os.path.join(root, path), encoding="utf-8", errors="replace") as f:
                live_cache[path] = prepare(f.read())
        live, numbers, live_folded = live_cache[path]
        want, _, copy_folded = prepare(body)
        problems = []
        for ln, comment in copy_folded:
            problems.append("statement folded onto a comment in the copy (copy line %d): %s" % (copy_line + ln - 1, comment))
        hits = [k for k in range(len(live) - len(want) + 1) if live[k:k + len(want)] == want] if want else []
        a, b = int(m.group("a")), int(m.group("b"))
        where = ""
        if len(hits) != 1:
            problems.append("the copy matches the live source %d times (exactly one required)" % len(hits))
        else:
            first, last = numbers[hits[0]], numbers[hits[0] + len(want) - 1]
            matched_spans.setdefault(path, []).append((first, last))
            where = "at lines %d-%d" % (first, last) + ("" if (first, last) == (a, b) else " (recorded %d-%d)" % (a, b))
            for ln, comment in live_folded:
                if first <= ln <= last:
                    problems.append("statement folded onto a comment in the live source (line %d): %s" % (ln, comment))
        blob = blob_of(root, path)
        blob_note = "blob unchanged" if blob == m.group("blob") else "blob now %s (recorded %s)" % (blob, m.group("blob"))
        if problems:
            failures += 1
            print("FAIL  %s %s: %s; %s" % (path, func, "; ".join(problems), blob_note))
        else:
            print("OK    %s %s: %s; %s" % (path, func, where, blob_note))
    # The identifier fence.
    for path, fence in FENCES.items():
        with open(os.path.join(root, path), encoding="utf-8", errors="replace") as f:
            raw = f.read()
        stripped, _ = strip_comments(raw)
        lines = strip_if0(stripped.split("\n"))
        spans = matched_spans.get(path, [])
        bad = []
        used = {}
        for k, l in enumerate(lines):
            nl = norm_line(l)
            if not nl:
                continue
            if not any(re.search(r"(?<![A-Za-z0-9_])" + re.escape(ident) + r"(?![A-Za-z0-9_])", nl) or
                       ('-' in ident and ident in nl) for ident in fence["identifiers"]):
                continue
            ln = k + 1
            if any(a <= ln <= b for a, b in spans):
                continue
            if nl in fence["allow"]:
                used[nl] = used.get(nl, 0) + 1
                if used[nl] == 1:
                    continue
            if any(re.search(p, nl) for p in fence["allow_patterns"]) and not nl.startswith("void "):
                continue
            bad.append("%d: %s" % (ln, nl))
        if bad:
            failures += 1
            print("FAIL  fence %s: lines outside the copied regions and the allowlist:\n        %s" % (path, "\n        ".join(bad)))
        else:
            print("OK    fence %s (%s)" % (path, ", ".join(fence["identifiers"])))
    # The frozen originals of the bench.
    frozen = 0
    with open(os.path.join(root, FROZEN_FILE), encoding="utf-8") as f:
        ftext = f.read().splitlines()
    seen = set()
    i = 0
    while i < len(ftext):
        m = FROZEN_BEGIN.search(ftext[i])
        if m:
            j = i + 1
            while j < len(ftext) and "FROZEN ORIGINAL end" not in ftext[j]:
                j += 1
            path = m.group("path").strip().replace("\\", "/")
            frozen += 1
            seen.add(path)
            want = FROZEN_ORIGINALS.get(path)
            got = original_digest("\n".join(ftext[i + 1:j]))
            if want is None:
                failures += 1
                print("FAIL  frozen original %s: not in FROZEN_ORIGINALS" % path)
            elif got != want:
                failures += 1
                print("FAIL  frozen original %s: the copy differs from the original (sha256 %s, recorded %s)" % (path, got, want))
            else:
                print("OK    frozen original %s (blob %s)" % (path, m.group("blob")))
            i = j
        i += 1
    for path in FROZEN_ORIGINALS:
        if path not in seen:
            failures += 1
            print("FAIL  frozen original %s: missing from %s" % (path, FROZEN_FILE))
    print("%d copies checked, %d fences, %d frozen originals, %d failing" % (len(copies), len(FENCES), frozen, failures))
    return 1 if failures or not copies else 0


def original_digest(text):
    """The SHA-256 of a source's normalised lines without its #include lines."""
    kept = "\n".join(l for l in text.replace("\r\n", "\n").split("\n") if not INCLUDE_RE.match(l))
    norm, _, _ = prepare(kept)
    return hashlib.sha256("\n".join(norm).encode("utf-8")).hexdigest()


if __name__ == "__main__":
    sys.exit(main())

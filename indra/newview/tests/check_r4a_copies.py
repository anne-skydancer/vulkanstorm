#!/usr/bin/env python3
"""Checks the task-inventory oracle's frozen copies against the live source.

usage: python indra/newview/tests/check_r4a_copies.py [--print-fence] [--record-conditionals]

Run by INTEGRATION_TEST_llinventory_task_oracle itself (test 8), so a drift
fails the build's test step.

Regions. Each region of tests/llinventory_task_oracle.cpp (copied code) and
tests/llinventory_task_oracle_decls.h (live declarations, constructor and
destructor lines and helpers the oracle mirrors by hand) starts with
    // >>> VKS-R4A-COPY <id> <path> lines <first>-<last> blob <hash>
    // <what the region holds>
    // conditionals: <the preprocessor conditionals enclosing it at copy time>
and ends with
    // <<< VKS-R4A-COPY <id>
Its body is the text of lines <first>-<last> of <path> as it was at blob
<hash>. Nothing in a region is edited: the copies resolve the viewer
singletons to the oracle's fakes by name lookup.

Normalisation, the same for the copy and the live file (after R1's
check_r1_copies.py): backslash-newline line splices are joined first
(translation phase 2), so a statement spliced onto a // line becomes part of
the comment; "#if 0" blocks are removed (up to the matching #else, #elif or
#endif); comments are removed outside string and character literals; within
each line, whitespace outside literals collapses to one space; literals are
kept verbatim; empty lines are dropped.

A region passes when its normalised lines occur exactly once, consecutively,
in the normalised live file, and the preprocessor conditionals enclosing that
match are the ones recorded for the region: a conditional that now wraps it,
such as "#if 1 <new> #else <original> #endif" or "#ifdef OPENSIM <new> #else
<original> #endif", fails, because the matched text may be a dead branch.

Identifier fence. For each fenced file, every normalised live line that names
a fenced identifier (the task-inventory members, the task-inventory methods,
and in llviewerinventory the LLViewerInventoryItem members and helpers the
copies use) must lie inside a matched region or be listed in that file's
allowlist. So a new call site (a new requestInventory() call), a removed or
changed one inside a region (the destructor's clearInventoryListeners(), which
lies in region C3), or a new use of the state outside the regions fails until
it is reviewed and copied or allowlisted. permYouOwner()'s body, a decision
input outside the rung, is a checked region (P1).

--record-conditionals rewrites the "// conditionals:" line of every region
from the live source (used once, when the regions are made); --print-fence
lists the fenced lines outside the regions.

The script reports where each region now lives and whether its file's blob
changed, and exits non-zero on any failure.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REGION_FILES = [os.path.join(HERE, "llinventory_task_oracle.cpp"),
                os.path.join(HERE, "llinventory_task_oracle_decls.h")]

OBJECT_IDS = [
    # state
    r"mInventory\w*", r"mInvRequest\w*", "mExpectedInventorySerialNum", "mPendingInventoryItemsIDs",
    # the task-inventory methods (calls and declarations)
    "requestInventory", "dirtyInventory", "clearInventoryListeners", "hasInventoryListeners",
    "registerInventoryListener", "removeInventoryListener", "isInventoryPending", "isInventoryDirty",
    "fetchInventoryFromServer", "fetchInventoryDelayed", "fetchInventoryDelayedCoro", "fetchInventoryFromCapCoro",
    "doInventoryCallback", "deleteInventoryItem", "doUpdateInventory", "loadTaskInvFile", "loadTaskInvLLSD",
    "processTaskInv", "processTaskInvFile", "updateInventory", "updateInventoryLocal", "updateMaterialInventory",
    "updateViewerInventoryAsset", "removeInventory", "moveInventory", "saveScript", "createInventoryItem",
    "createInventoryItemCoro", "getInventoryRoot", "getInventoryObject", "getInventoryItem", "getInventoryContents",
    "countInventoryContents", "getInventoryItemByAsset", "isAssetInInventory", "getInventorySerial",
]
INVENTORY_IDS = ["mIsComplete", "mTransactionID", "mIsRunning", "mIsFaulted",
                 "LLLocalizedInventoryItemsDictionary", "localizeInventoryObjectName", "copyViewerItem"]

# (live file, identifiers, allowlist of normalised lines outside the regions),
# as reviewed at 8dbe68aa69 (the live sources unchanged since 472ecf270d),
# re-reviewed at 6f23ef84f0 (vkstorm-rust; no new fenced lines outside the regions).
FENCES = [
    ("indra/newview/llviewerobject.cpp", OBJECT_IDS, [
    ]),
    ("indra/newview/llviewerobject.h", OBJECT_IDS, [
    ]),
    # llviewerinventory: uses of the members and helpers outside the copies
    # (the UDP item update, fetchFromServer, the dictionary's other users,
    # the category copy, and the header's declarations and accessors).
    ("indra/newview/llviewerinventory.cpp", INVENTORY_IDS, [
        'if(!mIsComplete)',
        'msg->addUUIDFast(_PREHASH_TransactionID, mTransactionID);',
        'LLLocalizedInventoryItemsDictionary::getInstance()->localizeInventoryObjectName(mName);',
        'for (dictionary_iter = LLLocalizedInventoryItemsDictionary::getInstance()->mInventoryItemsDict.begin();',
        'dictionary_iter != LLLocalizedInventoryItemsDictionary::getInstance()->mInventoryItemsDict.end();',
        'new_item->copyViewerItem(obj);',
    ]),
    ("indra/newview/llviewerinventory.h", INVENTORY_IDS, [
        'void copyViewerItem(const LLViewerInventoryItem* other);',
        'bool isFinished() const { return mIsComplete; }',
        'void setComplete(bool complete) { mIsComplete = complete; }',
        'LLTransactionID getTransactionID() const { return mTransactionID; }',
        'bool getIsRunning() const { return mIsRunning; }',
        'bool getIsFaulted() const { return mIsFaulted; }',
        'bool mIsComplete;',
        'LLTransactionID mTransactionID;',
        'bool mIsRunning = false;',
        'bool mIsFaulted = false;',
    ]),
]

HEAD = re.compile(r"^// >>> VKS-R4A-COPY (\S+) (\S+) lines (\d+)-(\d+) blob ([0-9a-f]{40})$")
TAIL = re.compile(r"^// <<< VKS-R4A-COPY (\S+)$")
COND_LINE = re.compile(r"^// conditionals: ?(.*)$")
COND_OPEN_RE = re.compile(r"^#\s*(if|ifdef|ifndef)\b")
COND_ELSE_RE = re.compile(r"^#\s*(else|elif)\b")
COND_END_RE = re.compile(r"^#\s*endif\b")


def splice_lines(text):
    """Joins backslash-newline splices first, as translation phase 2 does."""
    return text.replace("\\\r\n", "").replace("\\\n", "")


def strip_if0(text):
    out = []
    depth = 0
    for line in text.split("\n"):
        s = line.strip()
        if depth == 0:
            if re.match(r"^#\s*if\s+0\b", s):
                depth = 1
                out.append("")
                continue
            out.append(line)
        else:
            if re.match(r"^#\s*if", s):
                depth += 1
            elif re.match(r"^#\s*endif\b", s):
                depth -= 1
            elif depth == 1 and re.match(r"^#\s*(else|elif)\b", s):
                depth = 0  # "#if 0 ... #else ..." keeps the else branch
            out.append("")
    return "\n".join(out)


def strip_comments(text):
    """Removes // and /* */ comments outside literals; keeps the line structure."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(text[i:j])
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def normalise_line(line):
    """Collapses whitespace outside string and character literals."""
    out = []
    i = 0
    n = len(line)
    pending_space = False
    while i < n:
        c = line[i]
        if c in "\"'":
            j = i + 1
            while j < n and line[j] != c:
                j += 2 if line[j] == "\\" else 1
            j = min(j + 1, n)
            if pending_space and out:
                out.append(" ")
            pending_space = False
            out.append(line[i:j])
            i = j
            continue
        if c.isspace():
            pending_space = True
        else:
            if pending_space and out:
                out.append(" ")
            pending_space = False
            out.append(c)
        i += 1
    return "".join(out)


def normalise(text):
    out = []
    for line in strip_comments(strip_if0(splice_lines(text.replace("\r", "")))).split("\n"):
        n = normalise_line(line)
        if n:
            out.append(n)
    return out


def conditional_context(lines, index):
    """The conditionals enclosing normalised line `index`: one entry per open
    #if/#ifdef/#ifndef (with any #else/#elif already passed), joined with " | "."""
    stack = []
    for line in lines[:index]:
        if COND_OPEN_RE.match(line):
            stack.append([line])
        elif COND_ELSE_RE.match(line) and stack:
            stack[-1].append(line)
        elif COND_END_RE.match(line) and stack:
            stack.pop()
    return " | ".join(" / ".join(e) for e in stack)


def find_runs(hay, needle):
    k = len(needle)
    if k == 0:
        return []
    first = needle[0]
    return [i for i in range(len(hay) - k + 1) if hay[i] == first and hay[i:i + k] == needle]


def blob(path):
    try:
        return subprocess.check_output(["git", "-C", REPO, "hash-object", path], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "?"


def regions():
    """(file, header line index, id, path, first, last, blob, conditionals or None, body)."""
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
            cond = None
            if j < len(lines):
                cm = COND_LINE.match(lines[j])
                if cm:
                    cond = cm.group(1)
                    j += 1
            body = []
            while j < len(lines) and not TAIL.match(lines[j]):
                body.append(lines[j])
                j += 1
            if j >= len(lines) or TAIL.match(lines[j]).group(1) != rid:
                raise SystemExit("unterminated or mismatched copy region %s in %s" % (rid, path))
            out.append((path, i, rid, live, int(first), int(last), recorded_blob, cond, "\n".join(body)))
            i = j + 1
    return out


live_cache = {}


def live(path):
    if path not in live_cache:
        live_cache[path] = normalise(open(os.path.join(REPO, path), encoding="utf-8").read())
    return live_cache[path]


def record_conditionals(regs):
    edits = {}
    for path, head, rid, src, first, last, recorded_blob, cond, body in regs:
        runs = find_runs(live(src), normalise(body))
        if len(runs) != 1:
            raise SystemExit("%s: %d matches; conditionals not recorded" % (rid, len(runs)))
        edits.setdefault(path, []).append((head, cond is not None, conditional_context(live(src), runs[0])))
    for path, items in edits.items():
        lines = open(path, encoding="utf-8").read().split("\n")
        for head, has, ctx in sorted(items, reverse=True):
            text = "// conditionals: " + ctx if ctx else "// conditionals:"
            if has:
                lines[head + 2] = text
            else:
                lines.insert(head + 2, text)
        open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
        print("conditionals recorded in", path)


def main():
    args = sys.argv[1:]
    regs = regions()
    if "--record-conditionals" in args:
        record_conditionals(regs)
        return 0
    print_fence = "--print-fence" in args
    failures = 0
    covered = {}
    for path, head, rid, src, first, last, recorded_blob, cond, body in regs:
        want = normalise(body)
        live_lines = live(src)
        runs = find_runs(live_lines, want)
        notes = []
        current_blob = blob(os.path.join(REPO, src))
        if current_blob != recorded_blob:
            notes.append("file blob changed %s -> %s" % (recorded_blob[:10], current_blob[:10]))
        status = "OK  "
        if cond is None:
            failures += 1
            status = "FAIL"
            notes.insert(0, "no recorded conditionals line")
        elif len(runs) != 1:
            failures += 1
            status = "FAIL"
            notes.insert(0, "matches %d times in the live file (must be exactly once)" % len(runs))
        else:
            ctx = conditional_context(live_lines, runs[0])
            if ctx != cond:
                failures += 1
                status = "FAIL"
                notes.insert(0, "now inside the preprocessor conditionals [%s]; at copy time [%s]" % (ctx, cond))
            else:
                covered.setdefault(src, set()).update(range(runs[0], runs[0] + len(want)))
        print("%s %s %s lines %d-%d (%d normalised lines)%s" % (
            status, rid, src, first, last, len(want), ("; " + "; ".join(notes)) if notes else ""))

    fence_failures = 0
    for src, ids, allowlist in FENCES:
        word = re.compile(r"\b(" + "|".join(ids) + r")\b")
        listed = set(allowlist)
        outside = 0
        for idx, text in enumerate(live(src)):
            if not word.search(text) or idx in covered.get(src, set()):
                continue
            outside += 1
            if print_fence:
                print("FENCE-LINE %s\t%s" % (src, text))
            elif text not in listed:
                fence_failures += 1
                print("FENCE %s: a line naming %s lies outside the checked regions and the allowlist: %s"
                      % (src, ", ".join(sorted(set(word.findall(text)))), text))
        if not print_fence:
            print("fence %s: %d line(s) outside the regions, %d allowlisted" % (src, outside, len(listed)))
    print("%d regions, %d identical, %d differ; identifier fence: %d line(s) outside the regions and allowlists"
          % (len(regs), len(regs) - failures, failures, fence_failures))
    return 1 if failures or fence_failures else 0


if __name__ == "__main__":
    sys.exit(main())

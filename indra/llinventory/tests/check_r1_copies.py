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

Normalisation (the same for the copy and the live file): backslash-newline
line splices are joined first (translation phase 2), so a statement spliced
onto a `//` line is inside the comment; `#if 0` blocks are removed; comments
are removed (`//` and `/* */`, outside literals), so a statement folded into a
comment disappears and is flagged; #include lines and
the two forward declarations are removed; within each line, runs of
whitespace outside string ("...") and character ('...') literals become one
space; literals are kept verbatim; blank lines are dropped.

A region matches when its normalised text occurs exactly once, as a run of
whole lines, in the normalised live file: zero matches (the code changed) and
more than one (ambiguous) both fail. The preprocessor conditionals enclosing
the match must also be the ones that enclosed the region in the original
source (recorded in the region's "conditionals" line; an include guard for a
header): a conditional that now wraps the region, such as
"#if 1 <new> #else <original> #endif", fails, because the matched text may be
a dead branch. A conditional that splits a region breaks the match itself.

The identifier fence guards the state the copies depend on outside the copied
functions (the viewer subclasses' members, their types, initialisers and the
helpers the copies call): every live line of a fenced file that mentions a
fenced identifier must lie inside a matched region or be listed, normalised,
in that fence's allowlist. A new or changed line that touches a fenced member
therefore fails until it is reviewed and either copied or allowlisted.

The build runs this check after INTEGRATION_TEST_llinventory_parity (a
post-build step in indra/llinventory/CMakeLists.txt; the copied and fenced
sources are dependencies of the oracle, so a change to any of them rebuilds
the test and re-runs the check). When a port step replaces a copied source on
purpose, it narrows that run with --only to the sources it did not replace
(for example --only indra/newview --allow-partial, which keeps the viewer
subclass copies and their fence checked) and checks the frozen copies once against the commit
they were made from with --at 040ec69cfe.

A partial run (--only) is refused unless --allow-partial is also given.

usage: python check_r1_copies.py [--at <commit>] [--only <source prefix>... --allow-partial] [--print-fence] [viewer root]
       (default root: the repository of this file)
exit status: 0 when every checked region matches and every fence holds.
"""
import difflib
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLE_FILES = ["llinventory_oracle.h", "llinventory_oracle.cpp"]
INCLUDE_RE = re.compile(r"^\s*#\s*include\b")
FWD_RE = re.compile(r"^\s*class\s+(LLMessageSystem|LLUUID)\s*;\s*$")

# The identifier fences: (source, identifiers, allowlist of normalised lines).
VIEWER_IDS = ["mTransactionID", "mOwnerID", "mVersion", "INV_OWNER_ID", "INV_VERSION", "getCRC32",
              "packMessage", "exportLLSD", "importLLSD", "setVersion", "VERSION_UNKNOWN"]
FENCES = [
    # The allowlists hold every line outside the copies that touches a fenced identifier, normalised,
    # as reviewed at 472ecf270d (unchanged at 040ec69cfe): declarations, initialisers, copies and accessors of the members,
    # the senders that call packMessage, and the version checks of the fetch logic.
    ("indra/newview/llviewerinventory.h", VIEWER_IDS, [
        'LLTransactionID getTransactionID() const { return mTransactionID; }',
        'LLTransactionID mTransactionID;',
        'LLUUID mOwnerID;',
        'S32 mVersion;',
        'const LLUUID& getOwnerID() const { return mOwnerID; }',
        'enum { VERSION_UNKNOWN = -1, VERSION_INITIAL = 1 };',
        'virtual U32 getCRC32() const;',
        'virtual bool importLLSD(const std::string& label, const LLSD& value);',
        'virtual void exportLLSD(LLSD &sd) const;',
        'virtual void packMessage(LLMessageSystem* msg) const;',
        'void setVersion(S32 version);',
    ]),
    ("indra/newview/llviewerinventory.cpp", VIEWER_IDS, [
        'if (cat->getVersion() == LLViewerInventoryCategory::VERSION_UNKNOWN)',
        'if((VERSION_UNKNOWN == getVersion())',
        'mOwnerID = other->mOwnerID;',
        'mOwnerID(owner_id),',
        'mTransactionID = other->mTransactionID;',
        'mTransactionID = transaction_id;',
        'mTransactionID.setNull();',
        'mVersion(LLViewerInventoryCategory::VERSION_UNKNOWN),',
        'msg->addUUID("OwnerID", mOwnerID);',
        'msg->addUUIDFast(_PREHASH_TransactionID, mTransactionID);',
        'new_cat->packMessage(msg);',
        'new_item->packMessage(msg);',
        'packMessage(msg);',
        'return mVersion;',
        'setVersion(other->getVersion());',
        'update_item->packMessage(msg);',
    ]),
]


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
                # "#if 0 ... #else ..." keeps the else branch.
                depth = 0
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


def splice_lines(text):
    """Joins backslash-newline line splices first, as translation phase 2 does, so that
    a statement spliced onto a // comment line becomes part of the comment."""
    return text.replace("\\\r\n", "").replace("\\\n", "")


COND_OPEN_RE = re.compile(r"^#\s*(if|ifdef|ifndef)\b")
COND_ELSE_RE = re.compile(r"^#\s*(else|elif)\b")
COND_END_RE = re.compile(r"^#\s*endif\b")


def conditional_context(lines, index):
    """The preprocessor conditionals enclosing normalised line `index`: one entry per
    open #if/#ifdef/#ifndef (with any #else/#elif already passed), joined with " | "."""
    stack = []
    for line in lines[:index]:
        if COND_OPEN_RE.match(line):
            stack.append([line])
        elif COND_ELSE_RE.match(line) and stack:
            stack[-1].append(line)
        elif COND_END_RE.match(line) and stack:
            stack.pop()
    return " | ".join(" / ".join(e) for e in stack)


def region_context(source_text, region_lines):
    """(match count, conditional context of the first match) of a region in a source."""
    live = normalise(source_text)
    runs = find_runs(live, normalise("\n".join(region_lines)))
    return len(runs), (conditional_context(live, runs[0]) if runs else None)


def normalise(text):
    """The normalised lines of a text."""
    out = []
    for line in strip_comments(strip_if0(splice_lines(text))).split("\n"):
        if INCLUDE_RE.match(line) or FWD_RE.match(line):
            continue
        n = normalise_line(line)
        if n:
            out.append(n)
    return out


def find_runs(hay, needle):
    k = len(needle)
    if k == 0:
        return []
    first = needle[0]
    return [i for i in range(len(hay) - k + 1) if hay[i] == first and hay[i:i + k] == needle]


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


def git_blob(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def main():
    args = sys.argv[1:]
    at = None
    only = []
    print_fence = False
    allow_partial = False
    while args and args[0].startswith("--"):
        if args[0] == "--at" and len(args) >= 2:
            at = args[1]
            args = args[2:]
        elif args[0] == "--only" and len(args) >= 2:
            only.append(args[1])
            args = args[2:]
        elif args[0] == "--allow-partial":
            allow_partial = True
            args = args[1:]
        elif args[0] == "--print-fence":
            print_fence = True
            args = args[1:]
        else:
            print(__doc__)
            return 2
    if only and not allow_partial:
        print("--only checks part of the copies; it needs the explicit --allow-partial flag")
        return 2
    root = args[0] if args else subprocess.check_output(
        ["git", "-C", HERE, "rev-parse", "--show-toplevel"]).decode().strip()
    oracle_dir = os.path.join(root, "indra", "llinventory", "tests")
    if not os.path.isfile(os.path.join(oracle_dir, ORACLE_FILES[0])):
        oracle_dir = HERE

    cache = {}

    def live(src):
        if src not in cache:
            if at:
                data = subprocess.check_output(["git", "-C", root, "show", "%s:%s" % (at, src)])
            else:
                with open(os.path.join(root, src), "rb") as f:
                    data = f.read()
            cache[src] = (data, normalise(data.decode("latin-1")))
        return cache[src]

    failures = 0
    count = 0
    covered = {}  # source -> set of normalised line indices inside matched regions
    for name in ORACLE_FILES:
        for meta, body in regions(os.path.join(oracle_dir, name)):
            src = meta["source"]
            if only and not any(src.startswith(p) for p in only):
                continue
            count += 1
            data, live_lines = live(src)
            blob = git_blob(data)
            got = normalise("\n".join(body))
            runs = find_runs(live_lines, got)
            label = "%s %s:%s (%s)" % (name, src, meta["lines"], meta.get("copied", ""))
            if len(runs) == 1 and conditional_context(live_lines, runs[0]) != meta.get("conditionals", ""):
                # A conditional that did not enclose the region in the original source now
                # does (for example "#if 1 <new> #else <original> #endif"): the matched text
                # may be a dead branch.
                failures += 1
                print("DIFF %s: the region is now inside the preprocessor conditionals [%s]; at copy time [%s]" % (
                    label, conditional_context(live_lines, runs[0]), meta.get("conditionals", "")))
                continue
            if len(runs) == 1:
                covered.setdefault(src, set()).update(range(runs[0], runs[0] + len(got)))
                note = "" if blob == meta["blob"] else " [source blob changed from %s to %s; region text unchanged]" % (meta["blob"][:10], blob[:10])
                print("OK   %s%s" % (label, note))
                continue
            failures += 1
            if runs:
                print("DIFF %s: the copy matches %d places in the live source (lines must match exactly once)" % (label, len(runs)))
                continue
            print("DIFF %s: the copy no longer matches the live source (blob %s, copied from %s)" % (label, blob[:10], meta["blob"][:10]))
            first, last = (int(x) for x in meta["lines"].split("-"))
            expect = normalise("\n".join(data.decode("latin-1").split("\n")[first - 1:last]))
            for d in list(difflib.unified_diff(got, expect, "copy", "live", lineterm="", n=1))[:40]:
                print("     " + d)

    for src, ids, allowlist in FENCES:
        if only and not any(src.startswith(p) for p in only):
            continue
        _, live_lines = live(src)
        word = re.compile(r"\b(" + "|".join(map(re.escape, ids)) + r")\b")
        listed = set(allowlist)
        fenced = 0
        for idx, line in enumerate(live_lines):
            if not word.search(line) or idx in covered.get(src, set()):
                continue
            fenced += 1
            if print_fence:
                print("FENCE\t%s\t%s" % (src, line))
            elif line not in listed:
                failures += 1
                print("DIFF fence %s: a line touching %s is neither copied nor allowlisted: %s" % (
                    src, ", ".join(sorted(set(word.findall(line)))), line))
        print("OK   fence %s: %d lines outside the copies, all allowlisted" % (src, fenced) if not print_fence else "")
    print("%d regions checked, %d differ" % (count, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

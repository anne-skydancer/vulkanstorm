#!/usr/bin/env python3
"""The AIS update harness's drift guard (recorder R3a).

usage: python indra/newview/tests/check_r3a_copies.py [--print-fingerprints | --print-fence]

Run by INTEGRATION_TEST_llaisupdate_parity (test 3) on every build of the
test; it exits non-zero on any finding. Three checks:

1. Frozen copies. Each copy in llaisupdate_parity_stubs.cpp is fenced:

       // ---- VKS-COPY-BEGIN <file> <lines> blob <hash> : <what>
       ...verbatim text...
       // ---- VKS-COPY-END

   Both sides are reduced the same way: backslash-newline splices are
   joined first (as the compiler does), then comments are removed outside
   string and character literals, `#if 0` blocks are removed from the live
   text, whitespace runs are collapsed outside literals (literals are
   compared verbatim), empty lines are dropped. The copy's first line (its
   signature) must occur exactly once in the reduced live file, compared
   with all whitespace removed (a re-spaced copy of the signature counts),
   and the live region must lie outside every preprocessor conditional
   (`#if 1 <changed> #else <original>` cannot hide a stale copy); the live
   region is that line through its matching closing brace. Lines ending in
   "// VKS-HARNESS" are the recorder's additions: each must match one of
   HARNESS_FORMS and be free of side effects (no assignment but to the op
   being recorded, no ++/--, calls only to HARNESS_CALLS); they are dropped
   before the comparison. A `//` comment inside a copy that holds code
   punctuation (; { }) is flagged: a statement folded onto a comment line
   would escape the comparison.

2. Fingerprints of the live code the stub emulates instead of copying
   (the model functions gInventory stands in for, the fetch() gate, the
   default arguments the recordings depend on): SHA-256 of the reduced
   region, and the region's enclosing preprocessor conditionals (recorded
   in the table; a region must match exactly once, signature compared
   without whitespace). A change fails until the stub is reviewed against
   it and the fingerprint in check_r3a_fingerprints.txt is updated
   (--print-fingerprints prints the current table).

3. Identifier fence. Every live line of llviewerinventory.cpp and
   llviewerinventory.h that touches the members the copies depend on
   (FENCED) must lie in a checked copy, in a fingerprinted region, or in
   check_r3a_fence.txt (reduced line -> count, each reviewed: they are in
   functions the harness never reaches, or declarations and accessors of
   the header). A new use elsewhere fails (--print-fence prints the lines
   outside regions). Also every `#define` / `#undef` in the four live files
   (llviewerinventory.cpp/.h, llinventorymodel.cpp/.h) that names a fenced
   member or a copied, fingerprinted or recorded function fails: a macro
   could change what a checked region compiles to.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NEWVIEW = os.path.dirname(HERE)
STUBS = os.path.join(HERE, "llaisupdate_parity_stubs.cpp")
BEGIN = re.compile(r"^// ---- VKS-COPY-BEGIN (\S+) (\S+) blob ([0-9a-f]{40}) : (.*)$")

# The only shapes a harness line may take inside a copy: recording.
HARNESS_FORMS = [
    re.compile(r"^\s*bool vks_record_it = vks_ais::recording\(\); size_t vks_op_index = vks_ais::st\(\)\.ops\.size\(\); // VKS-HARNESS$"),
    re.compile(r"^\s*if \(vks_record_it\) \{ LLSD (op|m) = LLSD::emptyMap\(\);( (op|m)\[\"\w+\"\] = [^;{}]*;| if \(cat\) \{( m\[\"\w+\"\] = [^;{}]*;)+ \}| record\(op\);| vks_ais::st\(\)\.ops\[\(LLSD::Integer\)vks_op_index\]\[\"model\"\] = m;)+ \} // VKS-HARNESS$"),
    re.compile(r"^\s*vks_ais::Internal vks_guard; // VKS-HARNESS$"),
    re.compile(r"^\s*if \(vks_ais::recording\(\)\) \{ LLSD op = LLSD::emptyMap\(\);( op\[\"\w+\"\] = [^;{}]*;)+ record\(op\); \} // VKS-HARNESS$"),
]

# Calls a harness line may make (none has side effects on the model).
HARNESS_CALLS = {"recording", "st", "size", "emptyMap", "record", "is_model_item", "steal", "CatVersionTag",
                 "getDescendentCount", "Integer"}

# Names a #define/#undef in the live files must not touch, besides FENCED and
# the copied and fingerprinted functions.
MACRO_GUARDED = {"setVersion", "getVersion", "setDescendentCount", "getDescendentCount", "accountForUpdate",
                 "getCategory", "getItem", "updateItem", "updateCategory", "onObjectDeletedFromServer",
                 "notifyObservers", "addChangedMask", "findCategoryUUIDForType", "fetch", "fetchDescendentsOf",
                 "copyViewerItem", "copyItem", "copyViewerCategory", "copyCategory", "unpackMessage", "fromLLSD",
                 "localizeName", "localizeInventoryObjectName", "updateParentOnServer", "getLinkedItem",
                 "getLinkedCategory", "getType", "getName", "setTransactionID", "getViewerDescendentCount",
                 "setParent", "setComplete", "LLCategoryUpdate", "getChangedIDs", "sendReliableMessage"}
MACRO_FILES = ["llviewerinventory.cpp", "llviewerinventory.h", "llinventorymodel.cpp", "llinventorymodel.h"]

# Emulated, not copied: (file, signature line, kind) -> sha256 of the reduced
# region ("block": through the matching brace; "stmt": through the first ';').
FINGERPRINTS = {
    ("llviewerinventory.cpp", "bool LLViewerInventoryCategory::fetch(S32 expiry_seconds)", "block"): "",
    ("llviewerinventory.cpp", "S32 LLViewerInventoryCategory::getVersion() const", "block"): "",
    ("llviewerinventory.cpp", "void LLViewerInventoryCategory::setVersion(S32 version)", "block"): "",
    ("llviewerinventory.cpp", "S32 LLViewerInventoryCategory::getViewerDescendentCount() const", "block"): "",
    ("llviewerinventory.cpp", "LLLocalizedInventoryItemsDictionary::LLLocalizedInventoryItemsDictionary()", "block"): "",
    ("llviewerinventory.h", "bool fetch(S32 expiry_seconds = 10);", "stmt"): "",
    ("llinventorymodel.cpp", "LLViewerInventoryItem* LLInventoryModel::getItem(const LLUUID& id) const", "block"): "",
    ("llinventorymodel.cpp", "LLViewerInventoryCategory* LLInventoryModel::getCategory(const LLUUID& id) const", "block"): "",
    ("llinventorymodel.cpp", "const LLUUID LLInventoryModel::findCategoryUUIDForTypeInRoot(", "block"): "",
    ("llinventorymodel.cpp", "const LLUUID LLInventoryModel::findCategoryUUIDForType(LLFolderType::EType preferred_type) const", "block"): "",
    ("llinventorymodel.cpp", "void LLInventoryModel::addChangedMaskForLinks(const LLUUID& object_id, U32 mask)", "block"): "",
    ("llinventorymodel.cpp", "LLInventoryModel::item_array_t LLInventoryModel::collectLinksTo(const LLUUID& id)", "block"): "",
    ("llinventorymodel.cpp", "U32 LLInventoryModel::updateItem(const LLViewerInventoryItem* item, U32 mask)", "block"): "",
    ("llinventorymodel.cpp", "void LLInventoryModel::updateCategory(const LLViewerInventoryCategory* cat, U32 mask)", "block"): "",
    ("llinventorymodel.cpp", "void LLInventoryModel::onObjectDeletedFromServer(const LLUUID& object_id, bool fix_broken_links, bool update_parent_version, bool do_notify_observers)", "block"): "",
    ("llinventorymodel.cpp", "void LLInventoryModel::deleteObject(const LLUUID& id, bool fix_broken_links, bool do_notify_observers)", "block"): "",
    ("llinventorymodel.cpp", "void LLInventoryModel::notifyObservers(const LLUUID& transaction_id)", "block"): "",
    ("llinventorymodel.cpp", "void LLInventoryModel::addChangedMask(U32 mask, const LLUUID& referent)", "block"): "",
    ("llinventorymodel.cpp", "bool LLInventoryModel::fetchDescendentsOf(const LLUUID& folder_id) const", "block"): "",
    ("llinventorymodel.cpp", "LLInventoryModel::LLInventoryModel()", "block"): "",
    ("llinventorymodel.h", "U32 updateItem(const LLViewerInventoryItem* item, U32 mask = 0);", "stmt"): "",
    ("llinventorymodel.h", "void updateCategory(const LLViewerInventoryCategory* cat, U32 mask = 0);", "stmt"): "",
    ("llinventorymodel.h", "void onObjectDeletedFromServer(const LLUUID& item_id,", "stmt"): "",
    ("llinventorymodel.h", "struct LLCategoryUpdate", "block"): "",
    ("llinventorymodel.h", "const changed_items_t& getChangedIDs() const { return mChangedItemIDs; }", "stmt"): "",
}

FENCED = ["mIsComplete", "mTransactionID", "mIsRunning", "mIsFaulted", "mOwnerID", "mVersion",
          "mDescendentCount", "mFetching", "mDescendentsRequested"]
FENCE_FILES = ["llviewerinventory.cpp", "llviewerinventory.h"]
FINGERPRINT_TABLE = os.path.join(HERE, "check_r3a_fingerprints.txt")


# ---------------------------------------------------------------------------
# Reduction
# ---------------------------------------------------------------------------

def reduce_text(text, drop_if0=False):
    """Returns a list of (original line number, reduced line, comment text)
    with comments removed outside literals and whitespace collapsed outside
    literals; empty lines are dropped."""
    # backslash-newline splices first, as translation phase 2 does
    text = re.sub(r"\\\r?\n", "", text)
    out = []
    i = 0
    n = len(text)
    line_no = 1
    cur = []
    comments = []
    in_block = False
    while i < n:
        c = text[i]
        if in_block:
            if text.startswith("*/", i):
                in_block = False
                i += 2
                cur.append(" ")
                continue
            if c == "\n":
                out.append((line_no, "".join(cur), " ".join(comments)))
                cur, comments = [], []
                line_no += 1
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append(text[i + 2:j])
            i = j
            continue
        if text.startswith("/*", i):
            in_block = True
            i += 2
            continue
        if c in "\"'":
            # a literal, verbatim (raw strings are not used in these files)
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 1
                j += 1
            cur.append("\x00" + text[i:j + 1] + "\x00")
            i = j + 1
            continue
        if c == "\n":
            out.append((line_no, "".join(cur), " ".join(comments)))
            cur, comments = [], []
            line_no += 1
            i += 1
            continue
        cur.append(c)
        i += 1
    if cur or comments:
        out.append((line_no, "".join(cur), " ".join(comments)))

    def collapse(s):
        parts = s.split("\x00")
        res = []
        for k, part in enumerate(parts):
            res.append(part if k % 2 == 1 else " ".join(part.split()))
        return "".join(res).strip()

    reduced = [(ln, collapse(s), cm) for ln, s, cm in out]
    if drop_if0:
        kept = []
        depth = 0
        for ln, s, cm in reduced:
            if depth:
                if re.match(r"^#\s*if", s):
                    depth += 1
                elif re.match(r"^#\s*endif", s):
                    depth -= 1
                elif depth == 1 and re.match(r"^#\s*(else|elif)", s):
                    depth = 0
                continue
            if re.match(r"^#\s*if\s+0\b", s):
                depth = 1
                continue
            kept.append((ln, s, cm))
        reduced = kept
    return [(ln, s, cm) for ln, s, cm in reduced if s]


def braces(s):
    s = re.sub(r"\x00[^\x00]*\x00", "", s)
    return s.count("{") - s.count("}"), "{" in s


def strip_literal_marks(lines):
    return [s.replace("\x00", "") for s in lines]


def region(reduced, start, kind):
    """The reduced lines from index start through the matching brace
    ("block") or the first ';' ("stmt")."""
    if kind == "stmt":
        for j in range(start, len(reduced)):
            if reduced[j][1].endswith(";") or reduced[j][1].endswith("}"):
                return start, j
        return None
    depth = 0
    seen = False
    for j in range(start, len(reduced)):
        d, has = braces(reduced[j][1])
        depth += d
        seen = seen or has
        if seen and depth == 0:
            return start, j
    return None


def nows(s):
    return re.sub(r"\s+", "", s.replace("\x00", ""))


def find_once(reduced, signature):
    sig = nows(reduce_text(signature)[0][1])
    hits = [k for k, (_, s, _) in enumerate(reduced) if nows(s) == sig]
    return hits


_conds = {}


def conditionals(fname):
    """For each reduced line of a live file, the stack of preprocessor
    conditionals around it (#if 0 blocks are already gone)."""
    if fname not in _conds:
        stack = []
        out = []
        for _, s, _ in live(fname):
            t = s.replace("\x00", "")
            m = re.match(r"^#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", t)
            if m:
                kind = m.group(1)
                if kind in ("if", "ifdef", "ifndef"):
                    stack.append(t)
                elif kind in ("elif", "else") and stack:
                    stack[-1] = stack[-1] + " | " + t
                elif kind == "endif" and stack:
                    stack.pop()
            out.append(tuple(stack))
        _conds[fname] = out
    return _conds[fname]


def check_harness_line(line):
    """None if the harness line only records; else the reason."""
    t = re.sub(r'"[^"]*"', '""', line.split("// VKS-HARNESS")[0])
    if "++" in t or "--" in t:
        return "increments or decrements"
    u = re.sub(r"(op|m)\[\"\"\]\s*=(?!=)", "", t)
    u = re.sub(r"vks_ais::st\(\)\.ops\[\(LLSD::Integer\)vks_op_index\]\[\"\"\]\s*=(?!=)", "", u)
    u = re.sub(r"\b(bool vks_record_it|size_t vks_op_index|LLSD op|LLSD m)\s*=(?!=)", "", u)
    u = re.sub(r"(==|!=|<=|>=)", "", u)
    if "=" in u:
        return "assigns to something other than the op being recorded"
    for name in re.findall(r"([A-Za-z_]\w*)\s*\(", t):
        if name not in HARNESS_CALLS and name not in ("if",):
            return "calls %s()" % name
    return None


_cache = {}


def live(fname):
    if fname not in _cache:
        with open(os.path.join(NEWVIEW, fname), encoding="utf-8", errors="replace") as f:
            _cache[fname] = reduce_text(f.read(), drop_if0=True)
    return _cache[fname]


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

def check_copies(failures, covered):
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
            copies.append((m.group(1), m.group(2), m.group(4), stub[i + 1:j]))
            i = j
        i += 1
    for fname, rng, what, body in copies:
        kept = []
        for line in body:
            if line.rstrip().endswith("// VKS-HARNESS"):
                if not any(form.match(line) for form in HARNESS_FORMS):
                    failures.append("copy %s %s: harness line not of an allowed form: %s" % (fname, what, line.strip()))
                why = check_harness_line(line)
                if why:
                    failures.append("copy %s %s: harness line %s: %s" % (fname, what, why, line.strip()))
                continue
            kept.append(line)
        copy = reduce_text("\n".join(kept))
        for _, s, cm in copy:
            if re.search(r"[;{}]", cm):
                failures.append("copy %s %s: code punctuation in a comment (folded statement?): //%s" % (fname, what, cm))
        reduced = live(fname)
        hits = find_once(reduced, copy[0][1].replace("\x00", ""))
        if len(hits) != 1:
            failures.append("copy %s %s: signature found %d times in the live file" % (fname, what, len(hits)))
            continue
        reg = region(reduced, hits[0], "block")
        if reg is None:
            failures.append("copy %s %s: no end of the live region" % (fname, what))
            continue
        conds = set(conditionals(fname)[reg[0]:reg[1] + 1])
        if conds != {()}:
            failures.append("copy %s %s: the live region lies inside preprocessor conditionals %s" % (
                fname, what, sorted(conds)))
        covered.setdefault(fname, set()).update(range(reg[0], reg[1] + 1))
        a = [s for _, s, _ in copy]
        b = [s for _, s, _ in reduced[reg[0]:reg[1] + 1]]
        if a != b:
            k = next((x for x in range(min(len(a), len(b))) if a[x] != b[x]), min(len(a), len(b)))
            failures.append("copy %s %s differs from the live text:\n  copy: %s\n  live: %s" % (
                fname, what, a[k].replace("\x00", "") if k < len(a) else "(end)",
                b[k].replace("\x00", "") if k < len(b) else "(end)"))
        else:
            print("ok   copy %s %s %s" % (fname, rng, what))
    return len(copies)


def fingerprints(covered):
    table = {}
    problems = []
    for (fname, sig, kind) in FINGERPRINTS:
        reduced = live(fname)
        hits = find_once(reduced, sig)
        if len(hits) != 1:
            problems.append("fingerprint %s '%s': found %d times" % (fname, sig, len(hits)))
            continue
        reg = region(reduced, hits[0], kind)
        if reg is None:
            problems.append("fingerprint %s '%s': no end of region" % (fname, sig))
            continue
        covered.setdefault(fname, set()).update(range(reg[0], reg[1] + 1))
        text = "\n".join(strip_literal_marks([s for _, s, _ in reduced[reg[0]:reg[1] + 1]]))
        conds = sorted(set(conditionals(fname)[reg[0]:reg[1] + 1]))
        text += "\n#conditionals " + repr(conds)
        table[(fname, sig, kind)] = hashlib.sha256(text.encode("utf-8")).hexdigest()
    return table, problems


def read_table():
    out = {}
    if os.path.exists(FINGERPRINT_TABLE):
        with open(FINGERPRINT_TABLE, encoding="utf-8") as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                digest, fname, kind, sig = line.rstrip("\n").split("\t")
                out[(fname, sig, kind)] = digest
    return out


def fence(covered):
    found = {}
    for fname in FENCE_FILES:
        reduced = live(fname)
        cov = covered.get(fname, set())
        pat = re.compile(r"\b(%s)\b" % "|".join(FENCED))
        for k, (ln, s, _) in enumerate(reduced):
            if k in cov:
                continue
            if pat.search(re.sub(r"\x00[^\x00]*\x00", "", s)):
                key = (fname, s.replace("\x00", ""))
                found[key] = found.get(key, 0) + 1
    return found


def macro_fence(names):
    found = []
    pat = re.compile(r"^\s*#\s*(define|undef)\b(.*)$")
    for fname in MACRO_FILES:
        with open(os.path.join(NEWVIEW, fname), encoding="utf-8", errors="replace") as f:
            text = re.sub(r"\\\r?\n", " ", f.read())
        for line in text.splitlines():
            m = pat.match(line)
            if m:
                hit = set(re.findall(r"[A-Za-z_]\w*", m.group(2))) & names
                if hit:
                    found.append("%s: %s (names %s)" % (fname, line.strip(), ", ".join(sorted(hit))))
    return found


def read_fence_allow():
    out = {}
    path = os.path.join(HERE, "check_r3a_fence.txt")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                count, fname, text = line.rstrip("\n").split("\t", 2)
                out[(fname, text)] = int(count)
    return out


def main():
    failures = []
    covered = {}
    ncopies = check_copies(failures, covered)
    table, problems = fingerprints(covered)
    failures += problems
    if "--print-fingerprints" in sys.argv:
        print("# check_r3a_copies.py fingerprints: sha256 <TAB> file <TAB> kind <TAB> signature")
        for key in sorted(table):
            print("%s\t%s\t%s\t%s" % (table[key], key[0], key[2], key[1]))
        return 0
    fence_found = fence(covered)
    if "--print-fence" in sys.argv:
        print("# check_r3a_copies.py fence allowlist: count <TAB> file <TAB> reduced line")
        for (fname, text), count in sorted(fence_found.items()):
            print("%d\t%s\t%s" % (count, fname, text))
        return 0
    committed = read_table()
    for key, digest in sorted(table.items()):
        if committed.get(key) != digest:
            failures.append("emulated code changed: %s '%s' (fingerprint %s, committed %s): review the stub "
                            "against it, then update check_r3a_fingerprints.txt" % (key[0], key[1], digest, committed.get(key)))
        else:
            print("ok   fingerprint %s %s" % (key[0], key[1]))
    for key in committed:
        if key not in table:
            failures.append("fingerprint table names an unknown region: %s '%s'" % (key[0], key[1]))
    names = set(FENCED) | MACRO_GUARDED
    for (fname, sig, kind) in FINGERPRINTS:
        m = re.search(r"(\w+)\s*\(", sig)
        if m:
            names.add(m.group(1))
    for f in macro_fence(names):
        failures.append("macro fence: " + f)
    allow = read_fence_allow()
    for key, count in sorted(fence_found.items()):
        if allow.get(key) != count:
            failures.append("fence: %s has %d live line(s) touching a guarded member outside the checked regions "
                            "(allowed %s): %s" % (key[0], count, allow.get(key, 0), key[1]))
    for key, count in allow.items():
        if key not in fence_found:
            failures.append("fence allowlist names a line no longer present: %s: %s" % key)
    print("%d copies, %d fingerprints, %d fenced lines outside regions; %d findings" % (
        ncopies, len(table), sum(fence_found.values()), len(failures)))
    for f in failures:
        print("FAIL " + f)
    return 1 if failures or ncopies == 0 else 0


if __name__ == "__main__":
    sys.exit(main())

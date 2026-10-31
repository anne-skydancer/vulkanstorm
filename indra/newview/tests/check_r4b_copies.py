#!/usr/bin/env python3
"""Confirms that recorder R4b's frozen copies still match the live viewer source.

usage: python indra/newview/tests/check_r4b_copies.py [--root <viewer checkout>] [--at <commit>] [--write-fence]

INTEGRATION_TEST_llinventory_offer_oracle runs this script as its last test.
It checks:

  * every region of llinventory_offer_oracle_copies.{h,cpp} (compiled) and
    llinventory_offer_oracle_guards.h (text only: live code the copies depend
    on) against the live source. Both sides are normalised line by line: C and
    C++ comments and `#if 0` blocks are removed, whitespace is collapsed
    outside string and character literals (literals are compared verbatim),
    empty lines are dropped; line breaks stay significant. The documented
    substitutions (SUBSTITUTIONS) are applied to the live text first. A region
    must match exactly once in its file; regions of one ORDER group must match
    in increasing line order. A region found elsewhere than its recorded lines
    is reported MOVED (still a pass when unique and in order);
  * the frozen notification forms (R4B-FORMS table in
    llinventory_offer_oracle_fakes.cpp): every element of each form (buttons,
    ignore and any other element, in order) against
    skins/default/xui/en/notifications.xml, usetemplate expanded;
  * LL_PCODE_VOLUME against llmath/llvolume.h;
  * the identifier fence: every live line of indra/newview that touches an
    identifier of FENCE_IDENTIFIERS must lie inside a checked region or be in
    the reviewed allowlist at the end of this file (--write-fence regenerates
    it; review its diff before committing).

The port (W6/W7, rung seven sub-rung R4b) replaced the live C++ of the
regions in PORTED with calls into the Rust decisions (llinventoryoffer_vks.*);
LLOfferInfo's LLSD form (offer_info_basic) is the original C++ again and is
checked against the live source like the unported regions;
the copies stay as the oracle the Rust decisions are replayed against. For
those regions the script checks that the frozen copy itself is unchanged (the
SHA-256 of its normalised text, so no git history is needed), and with
--at <commit> it compares every region, ported or not, with the source as it
was at that commit (the port started from 3846816a16). The live C++ that the
port kept inside a ported region and that the executor relies on (KEPT_CPP)
must occur exactly once, normalised, in the live source and in the frozen
copies.

Exit status 0 when everything holds, 1 otherwise.
"""
import argparse
import glob
import hashlib
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))

# Documented substitutions: (text in the live source, text in the copy).
SUBSTITUTIONS = {
    # sendReceiveResponse and friends are private; the harness calls them
    "offer_info_class": [("private:", "public:")],
    # deterministic ids instead of LLUUID::generate()
    "give_inventory": [("transaction_id.generate();", "r4b_generate(transaction_id);")],
    # an injected clock instead of the wall clock
    "im_group_notice_case": [("LLDate::now()", "r4b_now()")],
}

# Regions of one group must be found in this order in their file.
ORDER = [
    ["im_name_clean", "im_dnd", "im_is_muted", "im_group_notice_case", "im_offer_case"],
    ["guard_process_new_message_preamble", "im_group_notice_case"],
    ["guard_offline_coro", "offline_loop"],
]

# Regions whose live C++ the port replaced: the SHA-256 of each frozen copy's
# normalised text (see normalise), recorded when the port landed.
PORTED = {
    "give_inventory_class": "739ba268adfcd48e9bf3060f3a9a146e4e6d95d40330fd1f12ef3dc89ab8af72",
    "offer_info_class": "a3473644fb4981e7450ca1704761a0f4754a1cedb758c30601f3d0d64ce3692c",
    "transfer_commonly_allowed": "9374242a668c998125251c4fb46345cfd3e834ec123a6586cebc26b26a0b903f",
    "throttle_globals": "1413b9ffc218c14911afd4a017238afa5e3edbc33ee0ddcc851803445e4f036c",
    "check_offer_throttle": "b53a169e5710e25a8318c0068a8abb85448bf73a15145997564f9eabb1bc0069",
    "offer_info_responses": "c560ed650597a40df30d54509ce6d5af890e3d5d274b3b86a8ac82004a338acc",
    "offer_info_callbacks": "f57b7e117d1a5a1042009d8bb832965cf43b817af70c978667caa0e3c2a5d0d3",
    "offer_force_response": "ac0607aed2b4fee2d5523dbb382d90b5949c0216ae129b625fcb2b63228655b0",
    "inventory_offer_handler": "aea9665014e665bc05749453dc9a7dfa0ad7c47513ae880211770e87296bac39",
    "give_inventory": "1a364ef2df06041b39088f55c34e9526e09bc0a55840629bf74a66fa7ec90510",
    "drop_functors": "e55e5fbbb0db7fc31dae3307aedd3e3448fa223478e610166d59cba4bec7e355",
    "will_object_accept": "b989ce6f6b0ea84ba40a042f99409103553480ab2ab2127de3e4716679bd9dad",
    "world_to_agent": "7ea1755672b1db718de27dadce9d557818636c58745d11f72038387665b2c991",
    "dad_share_confirm": "bbf4a3410a94e7769d3fe68c6c7de71d96ad2003d87589a80338b5ce95e31540",
    "dad_give_3d": "eb9d72b479ce1c250d789f363cd11b95f3e8d489de39626f20901702d4ebc007",
    "share_give_cb": "199adefa8f5190e6a53c7da1e85e5b29bfe4f93006b3f6434c6af6d598252488",
    "offline_loop": "a5af37d9adb7821c283677accd84145586e0d491330a9269406354add4b8ac2e",
    "im_group_notice_case": "74166fd9d20e488da207a177c58e30a3bc272f81cb82fab32609e37523a6fd73",
    "im_offer_case": "7becfa72b80141222f7859a2a517a71fabcfa8c7948759eed00357701d89daeb",
}

# The live C++ kept inside the ported regions, which the executor uses:
# (source, first line, last line), each line given by its normalised text;
# a run ends at the first line equal to `last` after `first`.
KEPT_CPP = [
    ("indra/newview/llimprocessing.cpp", "class LLPostponedOfferNotification : public LLPostponedNotification", "};"),
    ("indra/newview/llviewermessage.cpp", 'std::string LLOfferInfo::mResponderType = "offer_info";',
     'std::string LLOfferInfo::mResponderType = "offer_info";'),
    ("indra/newview/llviewermessage.cpp", "LLOfferInfo::LLOfferInfo()", "}"),
    ("indra/newview/llviewermessage.cpp", "LLOfferInfo::LLOfferInfo(const LLOfferInfo& info)", "}"),
    ("indra/newview/llviewermessage.cpp", "void LLOfferInfo::fromLLSD(const LLSD& params)", "}"),
    # LLOfferInfo's LLSD form: C++ until the inventory storage moves to Rust
    # (owner's decision; offer_info_basic is checked against the live source again)
    ("indra/newview/llviewermessage.cpp", "LLOfferInfo::LLOfferInfo(const LLSD& sd)", "}"),
    ("indra/newview/llviewermessage.cpp", "LLSD LLOfferInfo::asLLSD()", "}"),
    ("indra/newview/llviewermessage.cpp",
     "void inventory_offer_name_callback(const LLAvatarName& av_name, const std::string& from_name, const std::string& message)",
     "}"),
]

FENCE_IDENTIFIERS = [
    "LLOfferInfo", "forceResponse(IOR_", "sendReceiveResponse", "send_decline_response", "send_auto_receive_response",
    "inventory_offer_handler", "inventory_offer_callback", "inventory_task_offer_callback",
    "IM_INVENTORY_OFFERED", "IM_TASK_INVENTORY_OFFERED", "IM_GROUP_NOTICE_REQUESTED", "IM_INVENTORY_ACCEPTED",
    "IM_INVENTORY_DECLINED", "IM_TASK_INVENTORY_ACCEPTED", "IM_TASK_INVENTORY_DECLINED", "IM_GROUP_NOTICE_INVENTORY",
    "doGiveInventoryItem", "doGiveInventoryCategory", "commitGiveInventory", "isInventoryGiveAcceptable",
    "isInventoryGroupGiveAcceptable", "willObjectAcceptInventory", "move_inv_category_world_to_agent",
    "handleGiveDragAndDrop", "LLDiscardAgentOffer", "LLOpenAgentOffer", "check_offer_throttle", "isGiveToRLVOffer",
    "RLV_PUTINV_PREFIX", "AutoAcceptNewInventory", "FSUseLegacyInventoryAcceptMessages",
    "FSDismissGroupNoticeAttachmentsToTrash", "process_improved_im", "processNewMessage",
]

BEGIN = re.compile(r"^// R4B-COPY-BEGIN (\S+)\s*$")
END = re.compile(r"^// R4B-COPY-END (\S+)\s*$")
META = re.compile(r"^// (source|function|lines|blob|substitutions): ?(.*)$")


def normalise(text):
    """[(raw line number (1-based), normalised line)] with comments, `#if 0`
    blocks and blank lines removed; whitespace collapsed outside literals."""
    out = []
    in_block = False
    if0 = 0
    for lineno, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r")
        stripped = line.strip()
        if not in_block:
            if if0:
                if re.match(r"#\s*if", stripped):
                    if0 += 1
                elif re.match(r"#\s*endif", stripped):
                    if0 -= 1
                continue
            if re.match(r"#\s*if\s+0\b", stripped):
                if0 = 1
                continue
        res = []
        i = 0
        n = len(line)
        space = False
        while i < n:
            ch = line[i]
            if in_block:
                j = line.find("*/", i)
                if j < 0:
                    i = n
                    break
                in_block = False
                i = j + 2
                space = True
                continue
            if ch == "/" and i + 1 < n and line[i + 1] == "/":
                break
            if ch == "/" and i + 1 < n and line[i + 1] == "*":
                in_block = True
                i += 2
                continue
            if ch in "\"'":
                q = ch
                j = i + 1
                while j < n and line[j] != q:
                    j += 2 if line[j] == "\\" else 1
                if space and res:
                    res.append(" ")
                space = False
                res.append(line[i:j + 1])
                i = j + 1
                continue
            if ch in " \t":
                space = True
                i += 1
                continue
            if space and res:
                res.append(" ")
            space = False
            res.append(ch)
            i += 1
        s = "".join(res).strip()
        if s:
            out.append((lineno, s))
    return out


def regions(path):
    out = []
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    i = 0
    while i < len(lines):
        m = BEGIN.match(lines[i])
        if not m:
            i += 1
            continue
        cid = m.group(1)
        meta = {}
        i += 1
        while i < len(lines) and META.match(lines[i]):
            mm = META.match(lines[i])
            meta[mm.group(1)] = mm.group(2)
            i += 1
        body = []
        while i < len(lines) and not END.match(lines[i]):
            body.append(lines[i])
            i += 1
        if i >= len(lines) or END.match(lines[i]).group(1) != cid:
            raise SystemExit(f"{path}: region {cid} is not closed")
        out.append((cid, meta, "\n".join(body)))
        i += 1
    return out


def blob(root, rel):
    r = subprocess.run(["git", "hash-object", rel], cwd=root, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else "?"


def find_all(live, copy):
    """Start indices in `live` (normalised lines) where `copy` matches."""
    k = len(copy)
    if not k:
        return []
    first = copy[0]
    return [i for i in range(len(live) - k + 1) if live[i][1] == first and [l for _, l in live[i:i + k]] == copy]


def read_source(root, src, at):
    if at:
        r = subprocess.run(["git", "show", f"{at}:{src}"], cwd=root, capture_output=True)
        if r.returncode != 0:
            raise SystemExit(f"git show {at}:{src} failed")
        return r.stdout.decode("utf-8").replace("\r", "")
    with open(os.path.join(root, src), encoding="utf-8", newline="") as f:
        return f.read().replace("\r", "")


def check_regions(root, at=None):
    failures = 0
    total = 0
    found = {}      # cid -> (src, first raw line, last raw line)
    cache = {}
    for name in ("llinventory_offer_oracle_copies.h", "llinventory_offer_oracle_copies.cpp", "llinventory_offer_oracle_guards.h"):
        for cid, meta, body in regions(os.path.join(HERE, name)):
            total += 1
            src = meta["source"]
            a, b = (int(x) for x in meta["lines"].split("-"))
            key = (src, cid)
            if cid in PORTED and not at:
                digest = hashlib.sha256("\n".join(l for _, l in normalise(body)).encode("utf-8")).hexdigest()
                if digest == PORTED[cid]:
                    print(f"PORTED   {cid:34s} {src}:{a}-{b} (frozen copy unchanged)")
                else:
                    failures += 1
                    print(f"MISMATCH {cid:34s} the frozen copy of a ported region changed (sha256 {digest})")
                continue
            if src not in cache:
                cache[src] = read_source(root, src, at)
            text = cache[src]
            for live_s, copy_s in SUBSTITUTIONS.get(cid, []):
                text = text.replace(live_s, copy_s)
            live = normalise(text)
            copy = [l for _, l in normalise(body)]
            hits = find_all(live, copy)
            info = ""
            current = blob(root, src)
            if current != meta.get("blob"):
                info = f" (source blob {meta.get('blob')} -> {current})"
            if len(hits) != 1:
                failures += 1
                where = ", ".join(str(live[h][0]) for h in hits) if hits else "nowhere"
                print(f"MISMATCH {cid:34s} {src}:{a}-{b}: {len(hits)} matches ({where}){info}")
                continue
            h = hits[0]
            first, last = live[h][0], live[h + len(copy) - 1][0]
            found[cid] = (src, first, last)
            if first == a or (first >= a and last <= b):
                print(f"OK       {cid:34s} {src}:{a}-{b}{info}")
            else:
                print(f"MOVED    {cid:34s} {src}:{a}-{b} now at {first}-{last}{info}")
    for group in ORDER:
        prev = None
        for cid in group:
            if cid not in found:
                continue
            if prev and found[cid][1] < found[prev][1]:
                failures += 1
                print(f"MISMATCH order: {cid} (line {found[cid][1]}) comes before {prev} (line {found[prev][1]})")
            prev = cid
    return total, failures, found


def kept_run(lines, first, last):
    """The normalised lines from the one line equal to `first` through the
    next line equal to `last`, or None unless `first` occurs exactly once."""
    starts = [i for i, l in enumerate(lines) if l == first]
    if len(starts) != 1:
        return None
    i = starts[0]
    if first == last:
        return [lines[i]]
    for j in range(i + 1, len(lines)):
        if lines[j] == last:
            return lines[i:j + 1]
    return None


def check_kept(root):
    failures = 0
    frozen = []
    for name in ("llinventory_offer_oracle_copies.h", "llinventory_offer_oracle_copies.cpp"):
        for cid, meta, body in regions(os.path.join(HERE, name)):
            frozen.append(normalise(body))
    for src, first, last in KEPT_CPP:
        live = [l for _, l in normalise(read_source(root, src, None))]
        run = kept_run(live, first, last)
        label = f"{src}: {first[:60]}"
        if run is None:
            failures += 1
            print(f"MISMATCH kept C++ {label}: not found exactly once in the live source")
            continue
        in_copy = sum(len(find_all(f, run)) for f in frozen)
        if in_copy == 1:
            print(f"OK       kept C++ {label} ({len(run)} lines)")
        else:
            failures += 1
            print(f"MISMATCH kept C++ {label}: {in_copy} matches in the frozen copies (one expected)")
    return failures


def check_forms(root):
    fakes = open(os.path.join(HERE, "llinventory_offer_oracle_fakes.cpp"), encoding="utf-8").read()
    table = fakes.split("// R4B-FORMS-BEGIN")[1].split("// R4B-FORMS-END")[0]
    frozen = {}
    for n, kind, b, idx, dflt in re.findall(r'\{\s*"(\w+)",\s*"(\w+)",\s*"(\w*)",\s*(-?\d+),\s*(true|false)\s*\}', table):
        frozen.setdefault(n, []).append((kind, b, int(idx), dflt == "true"))
    tree = ET.parse(os.path.join(root, "indra/newview/skins/default/xui/en/notifications.xml"))
    templates = {}
    for t in tree.getroot().iter("template"):
        form = t.find("form")
        if form is not None:
            templates[t.get("name")] = form
    failures = 0
    for name, elements in frozen.items():
        node = None
        for n in tree.getroot().iter("notification"):
            if n.get("name") == name:
                node = n
                break
        if node is None:
            print(f"MISMATCH form {name}: notification not found")
            failures += 1
            continue
        form = node.find("form")
        if form is None:
            use = node.find("usetemplate")
            form = templates.get(use.get("name")) if use is not None else None
        live = []
        for el in (list(form) if form is not None else []):
            idx = el.get("index")
            live.append((el.tag, el.get("name") or "", int(idx) if idx is not None else -1, el.get("default") == "true"))
        if live == elements:
            print(f"OK       form {name}")
        else:
            failures += 1
            print(f"MISMATCH form {name}: frozen {elements} live {live}")
    return failures


def check_pcode(root):
    text = open(os.path.join(root, "indra/llmath/llvolume.h"), encoding="utf-8").read()
    m = re.search(r"LL_PCODE_VOLUME\s*=\s*(\d+)", text)
    if m and int(m.group(1)) == 9:
        print("OK       LL_PCODE_VOLUME = 9")
        return 0
    print(f"MISMATCH LL_PCODE_VOLUME: {m.group(1) if m else 'not found'}")
    return 1


def fence_lines(root, found):
    """{(file, normalised line): count} for fenced lines outside every checked region."""
    inside = {}
    for src, a, b in found.values():
        inside.setdefault(src.replace("\\", "/"), []).append((a, b))
    out = {}
    files = sorted(glob.glob(os.path.join(root, "indra/newview/*.cpp")) + glob.glob(os.path.join(root, "indra/newview/*.h")))
    for path in files:
        rel = os.path.relpath(path, root).replace("\\", "/")
        with open(path, encoding="utf-8", errors="replace", newline="") as f:
            text = f.read().replace("\r", "")
        for lineno, line in normalise(text):
            if not any(ident in line for ident in FENCE_IDENTIFIERS):
                continue
            if any(a <= lineno <= b for a, b in inside.get(rel, [])):
                continue
            out[(rel, line)] = out.get((rel, line), 0) + 1
    return out


def read_allowlist():
    text = open(os.path.abspath(__file__), encoding="utf-8").read()
    body = text.split("# R4B-FENCE-BEGIN\n")[1].split("# R4B-FENCE-END")[0]
    allow = {}
    for line in body.split("\n"):
        if not line.startswith("# "):
            continue
        count, rel, code = line[2:].split("\t", 2)
        allow[(rel, code)] = int(count)
    return allow


def write_allowlist(lines):
    path = os.path.abspath(__file__)
    text = open(path, encoding="utf-8").read()
    head, rest = text.split("# R4B-FENCE-BEGIN\n")
    tail = rest.split("# R4B-FENCE-END")[1]
    body = "".join(f"# {c}\t{rel}\t{code}\n" for (rel, code), c in sorted(lines.items()))
    open(path, "w", encoding="utf-8", newline="\n").write(head + "# R4B-FENCE-BEGIN\n" + body + "# R4B-FENCE-END" + tail)


def check_fence(root, found, write):
    lines = fence_lines(root, found)
    if write:
        write_allowlist(lines)
        print(f"fence allowlist written: {len(lines)} lines")
        return 0
    allow = read_allowlist()
    failures = 0
    for key, count in sorted(lines.items()):
        if allow.get(key, 0) != count:
            failures += 1
            print(f"FENCE    {key[0]}: {count} x {key[1]!r} (allowlisted {allow.get(key, 0)})")
    for key, count in sorted(allow.items()):
        if key not in lines:
            failures += 1
            print(f"FENCE    {key[0]}: allowlisted {count} x {key[1]!r} no longer present")
    if not failures:
        print(f"OK       fence: {len(lines)} fenced lines outside the regions, all allowlisted")
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.normpath(os.path.join(HERE, "..", "..", "..")))
    ap.add_argument("--write-fence", action="store_true")
    ap.add_argument("--at", help="compare every region with the source at this commit (no kept C++, forms or fence)")
    args = ap.parse_args()
    total, failures, found = check_regions(args.root, args.at)
    if args.at:
        print(f"{total} regions against {args.at}; {failures} mismatches")
        return 1 if failures else 0
    failures += check_kept(args.root)
    failures += check_forms(args.root)
    failures += check_pcode(args.root)
    failures += check_fence(args.root, found, args.write_fence)
    print(f"{total} regions; {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

# The identifier fence's reviewed allowlist: count, file, normalised line.
# R4B-FENCE-BEGIN
# 1	indra/newview/fsfloatercontacts.cpp	LLToolDragAndDrop::handleGiveDragAndDrop(hit_item->getUUID(), LLUUID::null, drop,
# 1	indra/newview/fsfloaterim.cpp	LLToolDragAndDrop::handleGiveDragAndDrop(mOtherParticipantUUID, mSessionID, drop,
# 1	indra/newview/llavataractions.cpp	if (LLGiveInventory::isInventoryGiveAcceptable(inv_item))
# 1	indra/newview/llavatarlistitem.cpp	return LLToolDragAndDrop::handleGiveDragAndDrop(mAvatarId, LLUUID::null, drop, cargo_type, cargo_data, accept, getAvatarName());
# 1	indra/newview/llfloateravatarpicker.cpp	return LLToolDragAndDrop::handleGiveDragAndDrop(dest_agent_id, session_id, drop,
# 1	indra/newview/llfloateropenobject.cpp	bool success = move_inv_category_world_to_agent(object_id,
# 1	indra/newview/llgiveinventory.cpp	bool LLGiveInventory::doGiveInventoryCategory(const LLUUID& to_agent,
# 1	indra/newview/llgiveinventory.cpp	bool LLGiveInventory::doGiveInventoryItem(const LLUUID& to_agent,
# 1	indra/newview/llgiveinventory.cpp	bool LLGiveInventory::isInventoryGiveAcceptable(const LLInventoryItem* item)
# 1	indra/newview/llgiveinventory.cpp	bool LLGiveInventory::isInventoryGroupGiveAcceptable(const LLInventoryItem* item)
# 1	indra/newview/llgiveinventory.h	static bool doGiveInventoryCategory(const LLUUID& to_agent,
# 1	indra/newview/llgiveinventory.h	static bool doGiveInventoryItem(const LLUUID& to_agent,
# 1	indra/newview/llgiveinventory.h	static bool isInventoryGiveAcceptable(const LLInventoryItem* item);
# 1	indra/newview/llgiveinventory.h	static bool isInventoryGroupGiveAcceptable(const LLInventoryItem* item);
# 1	indra/newview/llimprocessing.cpp	LLGiveInventory::doGiveInventoryItem(from_id, item, session_id);
# 1	indra/newview/llimprocessing.cpp	case IM_GROUP_NOTICE_REQUESTED:
# 1	indra/newview/llimprocessing.cpp	case IM_INVENTORY_ACCEPTED:
# 1	indra/newview/llimprocessing.cpp	case IM_INVENTORY_DECLINED:
# 1	indra/newview/llimprocessing.cpp	case IM_INVENTORY_OFFERED:
# 1	indra/newview/llimprocessing.cpp	case IM_TASK_INVENTORY_OFFERED:
# 1	indra/newview/llimprocessing.h	static void processNewMessage(LLUUID from_id,
# 1	indra/newview/llimview.cpp	LLGiveInventory::doGiveInventoryItem(av_id, item, session_id);
# 1	indra/newview/llimview.cpp	case IM_INVENTORY_OFFERED:
# 1	indra/newview/llinventorybridge.cpp	LLGiveInventory::doGiveInventoryCategory(
# 1	indra/newview/llinventorybridge.cpp	LLGiveInventory::doGiveInventoryItem(item->getCreatorUUID(),
# 1	indra/newview/llinventorybridge.cpp	accept = move_inv_category_world_to_agent(cat_id, mUUID, drop, callback, NULL, filter);
# 1	indra/newview/llinventorybridge.cpp	bool move_inv_category_world_to_agent(const LLUUID& object_id,
# 1	indra/newview/llinventorybridge.cpp	can_share = LLGiveInventory::isInventoryGiveAcceptable(item);
# 1	indra/newview/llinventorybridge.h	bool move_inv_category_world_to_agent(const LLUUID& object_id,
# 1	indra/newview/llinventoryfunctions.cpp	can_share = LLGiveInventory::isInventoryGiveAcceptable(item);
# 1	indra/newview/llinventorygallery.cpp	accept = move_inv_category_world_to_agent(cat_id, dest_id, drop);
# 1	indra/newview/llinventorymodel.cpp	((tfolder->getName().find(RLV_PUTINV_PREFIX) == 0)) )
# 1	indra/newview/llinventoryoffer_vks.h	bool vks_r4b_check_offer_throttle(const std::string& from_name, bool check_only);
# 1	indra/newview/llinventoryoffer_vks.h	class LLOfferInfo;
# 1	indra/newview/llinventoryoffer_vks.h	void vks_r4b_force_response(LLOfferInfo* offer, S32 response);
# 1	indra/newview/llinventoryoffer_vks.h	void vks_r4b_respond(LLOfferInfo* offer, const LLSD& notification, const LLSD& response);
# 1	indra/newview/llinventoryoffer_vks.h	void vks_r4b_send_auto_receive_response(LLOfferInfo* offer);
# 1	indra/newview/llinventoryoffer_vks.h	void vks_r4b_send_decline_response(LLOfferInfo* offer);
# 1	indra/newview/llinventoryoffer_vks.h	void vks_r4b_send_receive_response(LLOfferInfo* offer, bool accept, const LLUUID& folder);
# 1	indra/newview/llnotificationlistitem.cpp	mInventoryOffer = new LLOfferInfo(mParams.inventory_offer);
# 1	indra/newview/llnotificationlistitem.cpp	mInventoryOffer->forceResponse(IOR_ACCEPT);
# 1	indra/newview/llnotificationlistitem.cpp	mInventoryOffer->forceResponse(IOR_DECLINE);
# 1	indra/newview/llnotificationlistitem.h	LLOfferInfo* mInventoryOffer;
# 1	indra/newview/llnotificationstorage.cpp	LLResponderRegistry::StaticRegistrar sRegisterObjectGiveItem("ObjectGiveItem", &LLResponderRegistry::create<LLOfferInfo>);
# 1	indra/newview/llnotificationstorage.cpp	LLResponderRegistry::StaticRegistrar sRegisterOfferInfo("offer_info", &LLResponderRegistry::create<LLOfferInfo>);
# 1	indra/newview/llnotificationstorage.cpp	LLResponderRegistry::StaticRegistrar sRegisterUserGiveItem("UserGiveItem", &LLResponderRegistry::create<LLOfferInfo>);
# 1	indra/newview/llpanelavatar.cpp	LLToolDragAndDrop::handleGiveDragAndDrop(mAgentID, LLUUID::null, drop,
# 2	indra/newview/llpanelgroup.cpp	LLOfferInfo* inventory_offer)
# 1	indra/newview/llpanelgroup.cpp	inventory_offer->forceResponse(IOR_DECLINE);
# 2	indra/newview/llpanelgroup.h	LLOfferInfo* inventory_offer);
# 1	indra/newview/llpanelgroup.h	class LLOfferInfo;
# 1	indra/newview/llpanelgroupnotices.cpp	&& LLGiveInventory::isInventoryGroupGiveAcceptable(inv_item))
# 1	indra/newview/llpanelgroupnotices.cpp	LLOfferInfo* inventory_offer)
# 2	indra/newview/llpanelgroupnotices.cpp	mInventoryOffer->forceResponse(IOR_DECLINE);
# 1	indra/newview/llpanelgroupnotices.cpp	self->mInventoryOffer->forceResponse(IOR_ACCEPT);
# 1	indra/newview/llpanelgroupnotices.cpp	self->mInventoryOffer->forceResponse(IOR_DECLINE);
# 1	indra/newview/llpanelgroupnotices.h	LLOfferInfo* inventory_offer);
# 1	indra/newview/llpanelgroupnotices.h	LLOfferInfo* mInventoryOffer;
# 1	indra/newview/llpanelobjectinventory.cpp	if (cat && move_inv_category_world_to_agent(mUUID, LLUUID::null, false))
# 1	indra/newview/llpanelprofile.cpp	LLToolDragAndDrop::handleGiveDragAndDrop(getAvatarId(),
# 1	indra/newview/llstartup.cpp	msg->setHandlerFuncFast(_PREHASH_ImprovedInstantMessage, process_improved_im);
# 1	indra/newview/lltoastgroupnotifypanel.cpp	mInventoryOffer = new LLOfferInfo(payload["inventory_offer"]);
# 1	indra/newview/lltoastgroupnotifypanel.cpp	mInventoryOffer->forceResponse(IOR_ACCEPT);
# 1	indra/newview/lltoastgroupnotifypanel.cpp	mInventoryOffer->forceResponse(IOR_DECLINE);
# 1	indra/newview/lltoastgroupnotifypanel.h	LLOfferInfo* mInventoryOffer;
# 1	indra/newview/lltooldraganddrop.cpp	EAcceptance LLToolDragAndDrop::willObjectAcceptInventory(LLViewerObject* obj, LLInventoryItem* item, EDragAndDropType type)
# 3	indra/newview/lltooldraganddrop.cpp	EAcceptance rv = willObjectAcceptInventory(obj, item);
# 1	indra/newview/lltooldraganddrop.cpp	EAcceptance rv = willObjectAcceptInventory(root_object, item);
# 1	indra/newview/lltooldraganddrop.cpp	bool LLToolDragAndDrop::handleGiveDragAndDrop(LLUUID dest_agent, LLUUID session_id, bool drop,
# 3	indra/newview/lltooldraganddrop.cpp	if (willObjectAcceptInventory(hit_obj,item) < ACCEPT_YES_COPY_SINGLE )
# 1	indra/newview/lltooldraganddrop.cpp	rv = willObjectAcceptInventory(root_object, item, DAD_CATEGORY);
# 1	indra/newview/lltooldraganddrop.h	static EAcceptance willObjectAcceptInventory(LLViewerObject* obj, LLInventoryItem* item, EDragAndDropType type = DAD_NONE);
# 1	indra/newview/lltooldraganddrop.h	static bool handleGiveDragAndDrop(LLUUID agent, LLUUID session, bool drop,
# 1	indra/newview/lltooldraganddrop.h	static bool isInventoryDropAcceptable(LLViewerObject* obj, LLInventoryItem* item) { return (ACCEPT_YES_COPY_SINGLE <= willObjectAcceptInventory(obj, item)); }
# 1	indra/newview/llviewermessage.cpp	bool check_offer_throttle(const std::string& from_name, bool check_only)
# 1	indra/newview/llviewermessage.cpp	bool check_offer_throttle(const std::string& from_name, bool check_only);
# 1	indra/newview/llviewermessage.cpp	if (check_offer_throttle(from_name, false))
# 1	indra/newview/llviewermessage.cpp	return new LLDiscardAgentOffer(folder, object);
# 1	indra/newview/llviewermessage.cpp	return vks_r4b_check_offer_throttle(from_name, check_only);
# 1	indra/newview/llviewermessage.cpp	vks_r4b_send_decline_response(this);
# 1	indra/newview/llviewermessage.cpp	void LLOfferInfo::forceResponse(InventoryOfferResponse response)
# 1	indra/newview/llviewermessage.cpp	void LLOfferInfo::handleRespond(const LLSD& notification, const LLSD& response)
# 1	indra/newview/llviewermessage.cpp	void LLOfferInfo::sendReceiveResponse(bool accept, const LLUUID &destination_folder_id)
# 1	indra/newview/llviewermessage.cpp	void LLOfferInfo::send_decline_response(void)
# 1	indra/newview/llviewermessage.h	LLOfferInfo();
# 1	indra/newview/llviewermessage.h	LLOfferInfo(const LLOfferInfo& info);
# 1	indra/newview/llviewermessage.h	LLOfferInfo(const LLSD& sd);
# 1	indra/newview/llviewermessage.h	class LLOfferInfo : public LLNotificationResponderInterface
# 1	indra/newview/llviewermessage.h	void process_improved_im(LLMessageSystem *msg, void **user_data);
# 1	indra/newview/llviewermessage.h	void sendReceiveResponse(bool accept, const LLUUID &destination_folder_id);
# 1	indra/newview/llviewermessage.h	void send_auto_receive_response() { sendReceiveResponse(true, mFolderID); }
# 1	indra/newview/llviewermessage.h	void send_decline_response(void);
# 1	indra/newview/rlvinventory.cpp	if (0 == strPath.find(RLV_PUTINV_PREFIX))
# 1	indra/newview/rlvinventory.h	bool isGiveToRLVOffer(const LLOfferInfo& offerInfo);
# 1	indra/newview/rlvinventory.h	class LLOfferInfo;
# R4B-FENCE-END

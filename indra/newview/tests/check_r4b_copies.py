#!/usr/bin/env python3
"""Confirms that recorder R4b's frozen copies still match the live viewer source.

usage: python indra/newview/tests/check_r4b_copies.py [--root <viewer checkout>]

The offer and give oracle (INTEGRATION_TEST_llinventory_offer_oracle) runs
verbatim copies of the viewer's decision code inside namespace r4b
(llinventory_offer_oracle_copies.h/.cpp). Each copied region is delimited by
"// R4B-COPY-BEGIN <id>" and "// R4B-COPY-END <id>" and names its source file,
line range and blob (git hash-object) at copy time. This script:

  * extracts every region, applies the documented substitutions (SUBSTITUTIONS
    below) to the live text, normalises whitespace and compares; a region that
    still matches but has moved is reported with its new line range;
  * reports a changed source blob as information (the region may still match);
  * checks the frozen notification forms (R4B-FORMS table in
    llinventory_offer_oracle_fakes.cpp) against skins/default/xui/en/notifications.xml;
  * checks the LL_PCODE_VOLUME value the fakes use against llmath/llvolume.h.

Exit status 0 when everything matches, 1 otherwise.
"""
import argparse
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

BEGIN = re.compile(r"^// R4B-COPY-BEGIN (\S+)\s*$")
END = re.compile(r"^// R4B-COPY-END (\S+)\s*$")
META = re.compile(r"^// (source|function|lines|blob|substitutions): ?(.*)$")


def norm(text):
    return " ".join(text.split())


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


def find_moved(live_lines, copy_norm):
    """The 1-based line range of the live file whose normalised text equals the copy."""
    n = len(live_lines)
    starts = [k for k in range(n) if live_lines[k].strip() and copy_norm.startswith(norm(live_lines[k]))]
    for a in starts:
        acc = ""
        for b in range(a, n):
            piece = norm(live_lines[b])
            acc = (acc + " " + piece).strip() if piece else acc
            if acc == copy_norm:
                return a + 1, b + 1
            if len(acc) > len(copy_norm) or not copy_norm.startswith(acc):
                break
    return None


def check_regions(root):
    failures = 0
    total = 0
    for name in ("llinventory_offer_oracle_copies.h", "llinventory_offer_oracle_copies.cpp"):
        for cid, meta, body in regions(os.path.join(HERE, name)):
            total += 1
            src = meta["source"]
            a, b = (int(x) for x in meta["lines"].split("-"))
            with open(os.path.join(root, src), encoding="utf-8", newline="") as f:
                live_lines = f.read().replace("\r", "").split("\n")
            subs = SUBSTITUTIONS.get(cid, [])
            def apply(text):
                for live, copy in subs:
                    text = text.replace(live, copy)
                return text
            copy_norm = norm(body)
            live_norm = norm(apply("\n".join(live_lines[a - 1:b])))
            info = ""
            current = blob(root, src)
            if current != meta.get("blob"):
                info = f" (source blob {meta.get('blob')} -> {current})"
            if live_norm == copy_norm:
                print(f"OK       {cid:28s} {src}:{a}-{b}{info}")
                continue
            moved = find_moved([apply(l) for l in live_lines], copy_norm)
            if moved:
                print(f"MOVED    {cid:28s} {src}:{a}-{b} now at {moved[0]}-{moved[1]}{info}")
                continue
            failures += 1
            print(f"MISMATCH {cid:28s} {src}:{a}-{b}{info}")
    return total, failures


def check_forms(root):
    fakes = open(os.path.join(HERE, "llinventory_offer_oracle_fakes.cpp"), encoding="utf-8").read()
    table = fakes.split("// R4B-FORMS-BEGIN")[1].split("// R4B-FORMS-END")[0]
    frozen = {}
    for n, b, idx, dflt in re.findall(r'\{\s*"(\w+)",\s*"(\w+)",\s*(\d+),\s*(true|false)\s*\}', table):
        frozen.setdefault(n, []).append((b, int(idx), dflt == "true"))
    xml_path = os.path.join(root, "indra/newview/skins/default/xui/en/notifications.xml")
    tree = ET.parse(xml_path)
    templates = {}
    for t in tree.getroot().iter("template"):
        form = t.find("form")
        if form is not None:
            templates[t.get("name")] = form
    failures = 0
    for name, buttons in frozen.items():
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
        live = []
        if form is None:
            use = node.find("usetemplate")
            form = templates.get(use.get("name")) if use is not None else None
        for btn in (form.iter("button") if form is not None else []):
            live.append((btn.get("name"), int(btn.get("index")), btn.get("default") == "true"))
        if live == buttons:
            print(f"OK       form {name}")
        else:
            failures += 1
            print(f"MISMATCH form {name}: frozen {buttons} live {live}")
    return failures


def check_pcode(root):
    text = open(os.path.join(root, "indra/llmath/llvolume.h"), encoding="utf-8").read()
    m = re.search(r"LL_PCODE_VOLUME\s*=\s*(\d+)", text)
    if m and int(m.group(1)) == 9:
        print("OK       LL_PCODE_VOLUME = 9")
        return 0
    print(f"MISMATCH LL_PCODE_VOLUME: {m.group(1) if m else 'not found'}")
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.normpath(os.path.join(HERE, "..", "..", "..")))
    args = ap.parse_args()
    total, failures = check_regions(args.root)
    failures += check_forms(args.root)
    failures += check_pcode(args.root)
    print(f"{total} regions; {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

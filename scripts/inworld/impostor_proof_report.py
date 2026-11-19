#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""MEASURING BUILD ONLY: the report of the impostor PBR proof (scripts/inworld/impostor_proof_launch.ps1).

  python impostor_proof_report.py <evidence folder> [--out report.md]

Reads every impostor_proof/items.jsonl under the folder (one JSON record per impostored item per generation, written by
the measuring viewer) and the PNGs next to it, and writes report.md plus side-by-side pictures (before | after) in
<folder>/report/. Which impostored avatars wore glTF (PBR) or semi-transparent items, the material path and the blend
count of each item before and after, and the pictures.

Material path: "full PBR" (the item's real material), "stripped" (the shadow-pass binding: base colour only, the other
maps and the cut-off left over from earlier draws) or "legacy" (not glTF). Blend count: draws of the item with blending
on and colour written during the generation (blend_count; emissive redraws and merged draws of equal items add to it, so
read it as a before/after difference); blend_second is the same in the impostor's second (alpha depth) render, which the fix
makes 0.
"""
import argparse
import json
import pathlib
import sys

try:
    from PIL import Image
except ImportError:  # pictures are then linked side by side instead of composed
    Image = None


def load(root):
    records = []
    for jl in sorted(root.rglob('items.jsonl')):
        for line in jl.read_text(encoding='utf-8', errors='replace').splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            rec['_dir'] = jl.parent
            rec['_file'] = str(jl)
            records.append(rec)
    return records


def latest_by_key(records, tag):
    """The last generation of each (avatar, item) under one tag."""
    out = {}
    for r in records:
        if r.get('tag') != tag or r.get('item') is None:
            continue
        out[(r['avatar'], r['item'])] = r
    return out


def latest_png(records, tag):
    """avatar -> the last PNG path written under that tag."""
    out = {}
    for r in records:
        if r.get('tag') == tag and r.get('png'):
            p = r['_dir'] / r['png']
            if p.exists():
                out[r['avatar']] = p
    return out


def compose(before, after, dest):
    """before | after on a grey background (the impostor's alpha is its silhouette)."""
    images = []
    for p in (before, after):
        if p is None:
            images.append(None)
            continue
        im = Image.open(p).convert('RGBA')
        bg = Image.new('RGBA', im.size, (110, 110, 110, 255))
        bg.alpha_composite(im)
        images.append(bg.convert('RGB'))
    h = max(i.height for i in images if i is not None)
    w = sum((i.width if i is not None else 64) for i in images) + 8
    sheet = Image.new('RGB', (w, h), (40, 40, 40))
    x = 0
    for i in images:
        if i is not None:
            sheet.paste(i, (x, 0))
            x += i.width
        else:
            x += 64
        x += 8
    sheet.save(dest)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('root', type=pathlib.Path)
    ap.add_argument('--out', type=pathlib.Path, default=None)
    args = ap.parse_args()
    root = args.root
    records = load(root)
    if not records:
        print(f'No impostor_proof/items.jsonl records under {root}', file=sys.stderr)
        return 2

    before = latest_by_key(records, 'before')
    after = latest_by_key(records, 'after')
    before_png = latest_png(records, 'before')
    after_png = latest_png(records, 'after')
    avatars = sorted({r['avatar'] for r in records})

    def interesting(r):
        return r and (r.get('path') in ('full PBR', 'stripped') or r.get('blend_count', 0) > 0 or r.get('blend_second', 0) > 0)

    lines = ['# Impostor PBR proof: before and after', '']
    n_gen = {t: len({(r['session'], r['gen']) for r in records if r.get('tag') == t}) for t in ('before', 'after')}
    lines += [f'- records: {len(records)} item records from {len({r["_file"] for r in records})} log file(s)',
              f'- impostor generations: {n_gen["before"]} before (RenderImpostorFullPBR FALSE), {n_gen["after"]} after (TRUE)',
              f'- impostored avatars seen: {len(avatars)}', '']

    # summary of the counts that matter
    def count(d, pred):
        return sum(1 for r in d.values() if pred(r))

    lines += ['| | before | after |', '|---|---|---|',
              f'| items with a glTF (PBR) material | {count(before, lambda r: r["path"] != "legacy")} | {count(after, lambda r: r["path"] != "legacy")} |',
              f'| glTF items bound "stripped" | {count(before, lambda r: r["path"] == "stripped")} | {count(after, lambda r: r["path"] == "stripped")} |',
              f'| glTF items bound "full PBR" | {count(before, lambda r: r["path"] == "full PBR")} | {count(after, lambda r: r["path"] == "full PBR")} |',
              f'| blended items | {count(before, lambda r: r["blend_count"] > 0)} | {count(after, lambda r: r["blend_count"] > 0)} |',
              f'| items blended in the second render | {count(before, lambda r: r.get("blend_second", 0) > 0)} | {count(after, lambda r: r.get("blend_second", 0) > 0)} |',
              '']
    lines += ['Expected after the fix: no glTF item "stripped", no item blended in the second render. Before: glTF items "stripped", '
              'blended items drawn again with colour in the second render.', '']

    pics = root / 'report'
    pics.mkdir(exist_ok=True)

    shown = 0
    for avatar in avatars:
        keys = sorted({k for (a, k) in list(before) + list(after) if a == avatar})
        rows = []
        for k in keys:
            b = before.get((avatar, k))
            a = after.get((avatar, k))
            if not (interesting(b) or interesting(a)):
                continue
            ref = b or a
            rows.append((k, ref.get('rigged'), ref.get('alpha_mode') or '-', b, a))
        if not rows:
            continue
        shown += 1
        lines += [f'## {avatar}', '', '| item (seed/texture/material/rigged) | alpha | path before | path after | blend before | blend after |',
                  '|---|---|---|---|---|---|']
        for k, _, alpha, b, a in rows:
            lines.append(f'| `{k}` | {alpha} | {b["path"] if b else "-"} | {a["path"] if a else "-"} | '
                         f'{b["blend_count"] if b else "-"} (2nd: {b.get("blend_second", 0) if b else "-"}) | '
                         f'{a["blend_count"] if a else "-"} (2nd: {a.get("blend_second", 0) if a else "-"}) |')
        lines.append('')
        bp, ap_ = before_png.get(avatar), after_png.get(avatar)
        if bp or ap_:
            safe = ''.join(c if c.isalnum() or c == '-' else '_' for c in avatar)
            if Image is not None:
                dest = pics / f'{safe}.png'
                compose(bp, ap_, dest)
                lines += [f'Before (left) | after (right):', '', f'![{avatar}](report/{safe}.png)', '']
            else:
                rel = lambda p: p.relative_to(root).as_posix() if p else None
                lines += [f'Before: ![before]({rel(bp)})  After: ![after]({rel(ap_)})' if bp and ap_ else
                          f'Picture: {rel(bp or ap_)}', '']
    if shown == 0:
        lines += ['No impostored avatar wore glTF (PBR) or semi-transparent items in this session. Try a busier place.', '']

    out = args.out or (root / 'report.md')
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{out}: {shown} avatar(s) with PBR or blended items, {len(avatars)} impostored avatar(s) seen')
    return 0


if __name__ == '__main__':
    sys.exit(main())

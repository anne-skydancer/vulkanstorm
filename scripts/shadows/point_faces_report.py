# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Markdown tables from the JSON lines point_faces_bench.py writes (POINT_BENCH_OUT).
Usage: python point_faces_report.py results.jsonl"""
import json
import sys

METHODS = ['A', 'B', 'C', 'D', 'Dp', 'E', 'A_gm', 'Dp_gm']
LABEL = {'A': 'A', 'B': 'B', 'C': 'C', 'D': 'D', 'Dp': "D'", 'E': 'E', 'A_gm': 'A+masks', 'Dp_gm': "D'+masks"}


def load(path):
    rows = [json.loads(line) for line in open(path) if line.strip()]
    return rows[0]['info'], [r for r in rows[1:]]


def table(rows, scene, key, size, methods=METHODS, fmt='%.2f'):
    out = ['| mask | ' + ' | '.join(LABEL[m] for m in methods) + ' |', '|---|' + '---|'*len(methods)]
    for r in rows:
        if r['scene'] == scene and r['size'] == size:
            cells = [fmt % r['methods'][m][key] if m in r['methods'] else '' for m in methods]
            out.append('| %d | ' % r['mask_faces'] + ' | '.join(cells) + ' |')
    return '\n'.join(out)


def size_table(rows, scene, mask, key, methods=METHODS, fmt='%.3f'):
    out = ['| face | ' + ' | '.join(LABEL[m] for m in methods) + ' |', '|---|' + '---|'*len(methods)]
    for r in rows:
        if r['scene'] == scene and r['mask_faces'] == mask:
            out.append('| %d | ' % r['size'] + ' | '.join(fmt % r['methods'][m][key] for m in methods) + ' |')
    return '\n'.join(out)


def ratio_table(rows, scenes, key, num, den, size):
    out = ['| scene | ' + ' | '.join('mask %d' % k for k in (1, 2, 3, 6)) + ' |', '|---|---|---|---|---|']
    for s in scenes:
        cells = []
        for k in (1, 2, 3, 6):
            r = [x for x in rows if x['scene'] == s and x['size'] == size and x['mask_faces'] == k]
            cells.append('%.2f' % (r[0]['methods'][den][key]/r[0]['methods'][num][key]) if r else '')
        out.append('| %s | ' % s + ' | '.join(cells) + ' |')
    return '\n'.join(out)


def checks(rows):
    worst = {}
    for r in rows:
        for m, v in r['methods'].items():
            c = v['check']
            w = worst.setdefault(m, {'exact': True, 'ok': True, 'cov': 0, 'over': 0, 'covered': 0, 'dz': 0.0, 'configs': 0})
            w['configs'] += 1
            w['exact'] &= c.get('exact', True); w['ok'] &= c['ok']
            w['cov'] += c.get('coverage_mismatch', 0); w['over'] += c.get('depth_over_tol', 0)
            w['covered'] += c.get('covered_texels', 0); w['dz'] = max(w['dz'], c.get('max_abs_diff_m', 0.0))
    out = ['| method | configs | bit-exact | pass | coverage mismatches | texels over 1 mm | max linear depth diff (m) |',
           '|---|---|---|---|---|---|---|']
    for m in METHODS:
        if m in worst:
            w = worst[m]
            out.append('| %s | %d | %s | %s | %d | %d | %.2g |' % (LABEL[m], w['configs'], 'yes' if w['exact'] else 'no',
                       'yes' if w['ok'] else 'NO', w['cov'], w['over'], w['dz']))
    return '\n'.join(out)


def main(path):
    info, rows = load(path)
    print(json.dumps(info))
    scenes = []
    for r in rows:
        if r['scene'] not in scenes:
            scenes.append(r['scene'])
    for s in scenes:
        sr = [r for r in rows if r['scene'] == s]
        print('\n### %s (%d triangles, %d draws, %d groups)\n' % (s, sr[0]['triangles'], sr[0]['draws'], sr[0]['groups']))
        sizes = sorted({r['size'] for r in sr})
        ref = 256 if 256 in sizes else sizes[0]
        print('CPU ms, face %d:\n' % ref); print(table(sr, s, 'cpu_ms', ref))
        print('\nGPU ms, face %d:\n' % ref); print(table(sr, s, 'gpu_ms', ref, fmt='%.3f'))
        print('\nGL calls (draw calls), mask 6:\n')
        r6 = [r for r in sr if r['size'] == ref and r['mask_faces'] == 6]
        if r6:
            print(' | '.join('%s %d (%d)' % (LABEL[m], v['gl_calls'], v['draw_calls']) for m, v in r6[0]['methods'].items()))
    print('\n### Equal work\n'); print(checks(rows))
    plain = [s for s in scenes if not s.startswith('straddle')]
    print("\n### CPU ratio A / D'\n"); print(ratio_table(rows, plain, 'cpu_ms', 'Dp', 'A', 256))
    print("\n### GPU ratio D' / A (above 1 means D' is slower)\n"); print(ratio_table(rows, plain, 'gpu_ms', 'A', 'Dp', 256))


if __name__ == '__main__':
    main(sys.argv[1])

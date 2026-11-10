#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check that the Legacy shadow method is still Firestorm 7.2.5's code.

The Legacy shadow method (RenderShadowMethod 0) is the stock Firestorm 7.2.5 sun shadow cascades.
indra/newview/pipeline.cpp marks that code with

    // <ShadowLegacy id="NAME">
    ...
    // </ShadowLegacy>

This script extracts each marked region and compares it, ignoring indentation, blank lines and runs
of spaces, with the excerpt stored in scripts/shadows/legacy_reference/NAME.cpp (upstream text,
LGPL-2.1, recorded with the upstream commit it came from). The only differences allowed are the
substitutions listed in ALLOWED below, per region. An edit inside a region, a missing or extra
region or a changed excerpt makes the check fail, so the Legacy method cannot drift from upstream
unnoticed.

    python3 scripts/shadows/check_legacy_shadows.py
    python3 scripts/shadows/check_legacy_shadows.py --pipeline PATH          (another pipeline.cpp)
    python3 scripts/shadows/check_legacy_shadows.py --verify-reference REF   (re-check the stored
        excerpts against a git ref of the upstream viewer, e.g. upstream/Firestorm_7.2.5; for local
        use and for canary sync PRs. Each excerpt must appear unchanged in REF's pipeline.cpp)
"""
import argparse
import difflib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PIPELINE = 'indra/newview/pipeline.cpp'
REFERENCE_DIR = ROOT / 'scripts' / 'shadows' / 'legacy_reference'

# Per region: (upstream line, replacement) pairs, in normalised form, each applied to its first
# occurrence. An entry that does not occur is allowed (a region can match upstream exactly).
ALLOWED = {
    # computeSunSplitsLegacy returns false instead of leaving generateSunShadow
    'sun-splits': [('return;', 'return false;')],
    'sun-cascades': [],
    'sun-map-size': [],
}

OPEN = re.compile(r'^\s*//\s*<ShadowLegacy\s+id="([^"]+)"\s*>\s*$')
CLOSE = re.compile(r'^\s*//\s*</ShadowLegacy>\s*$')


def normalise(lines):
    out = []
    for line in lines:
        line = re.sub(r'\s+', ' ', line.strip())
        if line:
            out.append(line)
    return out


def extract_regions(text):
    """{id: [lines]} for the marked regions; raises ValueError on a malformed or repeated marker."""
    regions = {}
    current = None
    for number, line in enumerate(text.splitlines(), 1):
        opened = OPEN.match(line)
        if opened:
            if current is not None:
                raise ValueError(f'line {number}: region "{current[0]}" is not closed before another opens')
            if opened.group(1) in regions:
                raise ValueError(f'line {number}: region "{opened.group(1)}" appears twice')
            current = (opened.group(1), [])
        elif CLOSE.match(line):
            if current is None:
                raise ValueError(f'line {number}: closing marker without an opening one')
            regions[current[0]] = current[1]
            current = None
        elif current is not None:
            current[1].append(line)
    if current is not None:
        raise ValueError(f'region "{current[0]}" is not closed')
    return regions


def load_reference(name):
    path = REFERENCE_DIR / f'{name}.cpp'
    lines = [l for l in path.read_text(encoding='utf-8').splitlines() if not l.startswith('//!')]
    return normalise(lines)


def expected_lines(name):
    lines = load_reference(name)
    for old, new in ALLOWED.get(name, []):
        if old in lines:
            lines[lines.index(old)] = new
    return lines


def check(pipeline_text):
    problems = []
    try:
        regions = extract_regions(pipeline_text)
    except ValueError as e:
        return [str(e)]
    reference_names = {p.stem for p in REFERENCE_DIR.glob('*.cpp')}
    for name in sorted(reference_names - regions.keys()):
        problems.append(f'region "{name}" is missing from {PIPELINE}')
    for name in sorted(regions.keys() - reference_names):
        problems.append(f'region "{name}" in {PIPELINE} has no reference excerpt')
    for name in sorted(reference_names & regions.keys()):
        want = expected_lines(name)
        got = normalise(regions[name])
        if want != got:
            diff = difflib.unified_diff(want, got, 'upstream (with allowed substitutions)', PIPELINE, lineterm='', n=1)
            problems.append(f'region "{name}" differs from the upstream excerpt:\n' + '\n'.join(diff))
    return problems


def contains_subsequence(haystack, needle):
    n = len(needle)
    if n == 0:
        return True
    for i in range(len(haystack) - n + 1):
        if haystack[i] == needle[0] and haystack[i:i + n] == needle:
            return True
    return False


def verify_reference(ref):
    shown = subprocess.run(['git', 'show', f'{ref}:{PIPELINE}'], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
    if shown.returncode != 0:
        print(f'cannot read {PIPELINE} at {ref}: {shown.stderr.strip()}')
        return 1
    haystack = normalise(shown.stdout.splitlines())
    bad = 0
    for path in sorted(REFERENCE_DIR.glob('*.cpp')):
        found = contains_subsequence(haystack, load_reference(path.stem))
        print(f'  {"ok  " if found else "FAIL"} {path.stem}: {"found unchanged" if found else "not found"} in {ref}')
        bad += not found
    return 1 if bad else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--pipeline', type=Path, default=ROOT / PIPELINE, help='pipeline.cpp to check')
    parser.add_argument('--verify-reference', metavar='GIT_REF', help='check the stored excerpts against this upstream ref instead')
    args = parser.parse_args()
    if args.verify_reference:
        return verify_reference(args.verify_reference)
    text = args.pipeline.read_text(encoding='utf-8')
    problems = check(text)
    if problems:
        print('The Legacy shadow method no longer matches Firestorm 7.2.5:')
        for p in problems:
            print('- ' + p)
        print('\nChange code outside the <ShadowLegacy> markers, or, if the Legacy code must change, update the '
              'reference excerpt and ALLOWED in scripts/shadows/check_legacy_shadows.py with the reason.')
        return 1
    print('Legacy shadow regions match the upstream excerpts (' + ', '.join(sorted(extract_regions(text))) + ').')
    return 0


if __name__ == '__main__':
    sys.exit(main())

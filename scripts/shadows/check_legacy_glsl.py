#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""G4 of the projector shadow pool: the GLSL a Legacy shadow program compiles is the same as release's.

The pool adds code to shaders that the Legacy method also loads, each under a permutation that only the Stochastic method
defines (PROJECTOR_SHADOW_POOL in spotLightF.glsl, STOCHASTIC_SHADOW_ALPHA in the four caster alpha shaders). This checks that
with those permutations undefined, the text that remains is exactly the text of a reference ref (default
origin/vkstorm-release), apart from blank lines and indentation, and that the shaders Legacy shares without a permutation
(the sun passes, shadowUtil.glsl) are not touched at all.

    python scripts/shadows/check_legacy_glsl.py [--base origin/vkstorm-release]

Exit code 0 when identical. The conditionals on other macros are left as they are on both sides.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHADERS = 'indra/newview/app_settings/shaders'
# shaders the Legacy method loads that carry a Stochastic-only permutation
PERMUTED = [
    'class3/deferred/spotLightF.glsl',
    'class1/deferred/shadowAlphaMaskF.glsl',
    'class1/deferred/avatarAlphaShadowF.glsl',
    'class1/deferred/avatarAlphaMaskShadowF.glsl',
    'class1/deferred/pbrShadowAlphaBlendF.glsl',
]
# shaders the Legacy method loads and the pool must not touch
UNTOUCHED = [
    'class1/deferred/shadowUtil.glsl',
    'class2/deferred/sunLightF.glsl',
    'class2/deferred/sunLightSSAOF.glsl',
]
UNDEFINED = ('PROJECTOR_SHADOW_POOL', 'STOCHASTIC_SHADOW_ALPHA', 'PROJECTOR_SHADOW_NO_FACING_SKIP')
DIRECTIVE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')


def evaluate(cond):
    """True/False for a condition made of the undefined macros alone, None when it mentions anything else."""
    names = re.findall(r'[A-Za-z_][A-Za-z_0-9]*', cond)
    names = [n for n in names if n not in ('defined',)]
    if not names or not all(n in UNDEFINED for n in names):
        return None
    expr = re.sub(r'defined\s*\(\s*\w+\s*\)|defined\s+\w+', '0', cond)
    expr = re.sub(r'\b(%s)\b' % '|'.join(UNDEFINED), '0', expr)
    expr = expr.replace('&&', ' and ').replace('||', ' or ')
    expr = re.sub(r'!(?!=)', ' not ', expr)
    return bool(eval(expr, {'__builtins__': {}}, {}))


def legacy_text(text):
    """The text with the conditionals on the undefined macros resolved as Legacy sees them."""
    out = []
    stack = []   # per open conditional: ('resolved', taken_now, any_taken) or ('kept',)
    for line in text.splitlines():
        m = DIRECTIVE.match(line)
        active = all(f[1] for f in stack if f[0] == 'resolved')
        if not m:
            if active:
                out.append(line)
            continue
        kind, rest = m.group(1), m.group(2)
        if kind in ('if', 'ifdef', 'ifndef'):
            if kind == 'ifdef':
                cond = 'defined(%s)' % rest.split()[0]
            elif kind == 'ifndef':
                cond = '!defined(%s)' % rest.split()[0]
            else:
                cond = rest.split('//')[0]
            value = evaluate(cond)
            if value is None:
                stack.append(('kept', True, True))
                if active:
                    out.append(line)
            else:
                stack.append(('resolved', value, value))
        elif kind == 'elif':
            top = stack[-1]
            if top[0] == 'kept':
                if active:
                    out.append(line)
            else:
                value = evaluate(rest.split('//')[0])
                assert value is not None, line
                stack[-1] = ('resolved', value and not top[2], top[2] or value)
        elif kind == 'else':
            top = stack[-1]
            if top[0] == 'kept':
                if active:
                    out.append(line)
            else:
                stack[-1] = ('resolved', not top[2], True)
        else:  # endif
            top = stack.pop()
            if top[0] == 'kept' and all(f[1] for f in stack if f[0] == 'resolved'):
                out.append(line)
    return out


def normalise(lines):
    return [re.sub(r'\s+', ' ', l.strip()) for l in lines if l.strip()]


def show(ref, path):
    r = subprocess.run(['git', 'show', '%s:%s/%s' % (ref, SHADERS, path)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
    return r.stdout if r.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', default='origin/vkstorm-release')
    base = ap.parse_args().base
    problems = []
    for path in PERMUTED:
        old = show(base, path)
        new = (ROOT / SHADERS / path).read_text(encoding='utf-8')
        if old is None:
            problems.append('%s: not in %s' % (path, base))
            continue
        a, b = normalise(legacy_text(old)), normalise(legacy_text(new))
        if a != b:
            import difflib
            problems.append('%s differs for Legacy:\n%s' % (path, '\n'.join(difflib.unified_diff(a, b, base, 'working tree', lineterm='', n=1))))
        else:
            print('%-48s identical for Legacy (%d lines)' % (path, len(a)))
    for path in UNTOUCHED:
        old = show(base, path)
        f = ROOT / SHADERS / path
        if old is None and not f.exists():
            continue
        if old is None or not f.exists() or old.replace('\r\n', '\n') != f.read_text(encoding='utf-8').replace('\r\n', '\n'):
            problems.append('%s differs from %s (the pool must not touch it)' % (path, base))
        else:
            print('%-48s unchanged' % path)
    for p in problems:
        print('FAIL:', p)
    print('G4', 'PASS' if not problems else 'FAIL')
    return 1 if problems else 0


sys.exit(main())

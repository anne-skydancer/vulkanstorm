# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Generate the viewer's GLSL from the Slang programs (a build tool: the viewer never runs Slang).

Reads the pin (scripts/shaders/slang_pin.json) and the program list (indra/newview/slang/program_list.json), compiles
every program, permutation and stage with the pinned slangc, and writes, under
indra/newview/app_settings/shaders/generated/:

  <id>.<vert|frag|comp>.glsl   one whole program stage, without #version (the loader writes #version 460 and the
                               hook define block, then the whole file)
  programs.json                the manifest: per stage the file, hash, hooks and the interface from reflection

The Slang output is taken as it is: no text is edited back toward today's GLSL. Only these post steps run: the
#version line is dropped, #extension lines that are core in GLSL 4.60 are dropped (any other extension fails), the
header and the hook list are written, and every `((NAME))` macro in the text must be a registered hook.

  python scripts/shaders/slang_gen.py            write the generated files
  python scripts/shaders/slang_gen.py --check    generate to a scratch folder and fail on any difference (CI drift)
"""
import argparse
import hashlib
import itertools
import json
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import slang_common as common  # noqa: E402

# Extensions slangc may emit that GLSL 4.60 already has in core: the loader's `#version 460` covers them.
CORE_IN_460 = {
    'GL_ARB_texture_cube_map_array', 'GL_ARB_shader_bit_encoding', 'GL_ARB_gpu_shader5', 'GL_ARB_gpu_shader_fp64',
    'GL_ARB_shading_language_420pack', 'GL_ARB_explicit_attrib_location', 'GL_ARB_explicit_uniform_location',
    'GL_ARB_shader_image_load_store', 'GL_ARB_texture_query_lod', 'GL_ARB_texture_gather', 'GL_ARB_shader_atomic_counters',
    'GL_ARB_shader_storage_buffer_object', 'GL_ARB_compute_shader', 'GL_ARB_arrays_of_arrays',
    'GL_ARB_shader_draw_parameters', 'GL_ARB_derivative_control',
    'GL_ARB_shader_texture_image_samples', 'GL_ARB_enhanced_layouts',
}
SLANGC_FLAGS = ['-target', 'glsl', '-profile', 'glsl_460', '-no-mangle', '-line-directive-mode', 'none',
                '-matrix-layout-row-major',
                # the program's one global uniform block sits above the viewer's own UBO bindings (see GLOBALS_BINDING)
                '-fvk-bind-globals', str(common.GLOBALS_BINDING), '0']
# A hook is written `((NAME))` in the generated text (slangc wraps an intrinsic in one pair of parentheses, the Slang
# source supplies the other). A hook written without the double parentheses is only caught by the glslang link.
HOOK_USE = re.compile(r'\(\(([A-Z][A-Z0-9_]*)\)\)')
VERSION_LINE = re.compile(r'^#version .*$', re.M)
PROGRAM_ID = re.compile(r'[A-Za-z][A-Za-z0-9_]*')


class GenError(Exception):
    pass


def load_program_list(src_dir):
    data = json.loads((Path(src_dir) / 'program_list.json').read_text(encoding='utf-8'))
    hooks = data.get('hooks', {})
    for name in hooks:
        if not re.fullmatch(r'[A-Z][A-Z0-9_]*', name):
            raise GenError(f'hook name {name!r} must be upper-case letters, digits and underscores')
    ids = set()
    tasks = []
    for prog in data.get('programs', []):
        name = prog['name']
        if not PROGRAM_ID.fullmatch(name):
            raise GenError(f'program name {name!r} is not an identifier')
        entries = prog['entries']
        for stage in entries:
            if stage not in common.STAGE_EXT:
                raise GenError(f'{name}: unknown stage {stage!r}')
        axes = prog.get('permutations', {})
        names = sorted(axes)
        combos = list(itertools.product(*[[str(v) for v in axes[n]] for n in names])) if names else [()]
        for combo in combos:
            perm = dict(zip(names, combo))
            pid = name + ''.join(f'__{k}{v}' for k, v in perm.items())
            if pid in ids:
                raise GenError(f'duplicate program id {pid}')
            ids.add(pid)
            tasks.append({'id': pid, 'program': name, 'family': prog.get('family'), 'source': prog['source'],
                          'entries': entries, 'permutation': perm})
    return {'hooks': hooks, 'tasks': tasks, 'position': data.get('position')}


def _comment_text(line):
    line = line.strip()
    for lead in ('/**', '/*', '//', '*/', '*'):
        if line.startswith(lead):
            line = line[len(lead):]
    return line.removesuffix('*/').strip()


def source_header(source_path):
    """The licence comment at the top of the Slang source, re-emitted as // lines in every generated file."""
    out = []
    in_block = False
    for raw in source_path.read_text(encoding='utf-8').split('\n')[:80]:
        s = raw.strip()
        if in_block:
            out.append(_comment_text(s))
            if '*/' in s:
                in_block = False
            continue
        if s.startswith('//'):
            out.append(_comment_text(s))
        elif s.startswith('/*'):
            out.append(_comment_text(s))
            in_block = '*/' not in s
        elif not s:
            if out:
                break
        else:
            break
    out = [line for line in out if line]
    if any('SPDX-License-Identifier' in line for line in out):
        return [line for line in out if 'SPDX-License-Identifier' in line or 'Copyright' in line]
    if any('LicenseInfo' in line for line in out):
        return out  # Linden Lab and Firestorm headers travel whole, unchanged
    raise GenError(f'{source_path}: no SPDX-License-Identifier or $LicenseInfo in its leading comment')


def split_stages(text, count):
    """slangc prints every entry point to stdout, each starting with its own #version line."""
    parts = re.split(r'(?m)^(?=#version )', text)
    parts = [p for p in parts if p.strip()]
    if len(parts) != count:
        raise GenError(f'slangc printed {len(parts)} stage(s), expected {count}')
    return parts


def postprocess(text, hooks):
    """Drop #version and core extensions; return (body, hook names used). Fails on anything unknown."""
    text = text.replace('\r\n', '\n')
    text = VERSION_LINE.sub('', text, count=1).lstrip('\n')
    kept = []
    for line in text.split('\n'):
        m = re.match(r'#extension\s+(\w+)\s*:\s*(\w+)', line)
        if m:
            if m.group(1) in CORE_IN_460:
                continue
            raise GenError(f'unexpected #extension {m.group(1)} (not core in GLSL 4.60)')
        kept.append(line)
    body = '\n'.join(kept)
    used = sorted(set(HOOK_USE.findall(body)))
    unknown = [h for h in used if h not in hooks]
    if unknown:
        raise GenError(f'macro hook(s) not in program_list.json "hooks": {", ".join(unknown)}')
    return body.rstrip('\n') + '\n', used


def _type_name(t):
    kind = t.get('kind')
    if kind == 'scalar':
        return t['scalarType']
    if kind == 'vector':
        return f'{_type_name(t["elementType"])}x{t["elementCount"]}'
    if kind == 'matrix':
        return f'{_type_name(t["elementType"])}x{t["rowCount"]}x{t["columnCount"]}'
    if kind == 'resource':
        return t.get('baseShape', 'resource')
    if kind == 'array':
        return f'{_type_name(t["elementType"])}[{t.get("elementCount", 0)}]'
    return kind or 'unknown'


def _leaves(node, out):
    """Flatten an entry point's parameters and result into (leaf, binding) pairs, struct fields included."""
    t = node.get('type', {})
    if t.get('kind') == 'struct':
        for field in t.get('fields', []):
            _leaves(field, out)
        return
    if 'binding' in node:
        out.append(node)


def interface_of(reflection, entry_name):
    """One stage's interface from slangc's reflection: inputs and outputs by location, the uniform block, textures."""
    entry = next(e for e in reflection['entryPoints'] if e['name'] == entry_name)
    leaves = []
    for p in entry.get('parameters', []):
        _leaves(p, leaves)
    if 'result' in entry:
        _leaves(entry['result'], leaves)
    inputs, outputs = [], []
    for leaf in leaves:
        bind = leaf['binding']
        if bind['kind'] not in ('varyingInput', 'varyingOutput'):
            continue
        name = leaf.get('name') or leaf.get('semanticName', 'result').lower()
        item = {'name': name, 'location': bind['index'], 'type': _type_name(leaf['type'])}
        (inputs if bind['kind'] == 'varyingInput' else outputs).append(item)
    uniforms, textures = [], []
    for b in entry.get('bindings', []):
        bind = b['binding']
        ptype = next((p['type'] for p in reflection['parameters'] if p['name'] == b['name']), {})
        if bind['kind'] == 'uniform':
            uniforms.append({'name': b['name'], 'offset': bind['offset'], 'size': bind['size'], 'type': _type_name(ptype)})
        elif bind['kind'] == 'descriptorTableSlot' and bind.get('used', 1):
            textures.append({'name': b['name'], 'binding': bind['index'], 'type': _type_name(ptype)})
    key = lambda x: (x.get('location', x.get('offset', x.get('binding', 0))), x['name'])  # noqa: E731
    return {'inputs': sorted(inputs, key=key), 'outputs': sorted(outputs, key=key),
            'uniforms': sorted(uniforms, key=key), 'textures': sorted(textures, key=key)}


def compile_task(task, slangc, src_dir, hooks, strict, version):
    src = Path(src_dir) / task['source']
    header_lines = source_header(src)
    with tempfile.TemporaryDirectory() as tmp:
        refl = Path(tmp) / 'reflection.json'
        cmd = [str(slangc), str(src), *SLANGC_FLAGS, '-I', str(src_dir), '-reflection-json', str(refl)]
        for k, v in task['permutation'].items():
            cmd.append(f'-D{k}={v}')
        stages = [s for s in ('vertex', 'fragment', 'compute') if s in task['entries']]
        for s in stages:
            cmd += ['-entry', task['entries'][s], '-stage', s]
        run = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')
        diag = (run.stderr or '').strip()
        if run.returncode != 0:
            raise GenError(f'{task["id"]}: slangc failed\n{diag}')
        if strict and re.search(r'\bwarning\b', diag):
            raise GenError(f'{task["id"]}: slangc warnings (strict)\n{diag}')
        reflection = json.loads(refl.read_text(encoding='utf-8'))
    texts = split_stages(run.stdout, len(stages))
    result = {}
    for stage, text in zip(stages, texts):
        has_pos = re.search(r'\bgl_Position\b', text) is not None
        if has_pos != (stage == 'vertex'):
            raise GenError(f'{task["id"]}: the {stage} stage text {"writes" if has_pos else "lacks"} gl_Position (stage order?)')
        body, used = postprocess(text, hooks)
        perm = ', '.join(f'{k}={v}' for k, v in task['permutation'].items()) or 'none'
        head = ['// Generated by scripts/shaders/slang_gen.py. Do not edit: change the Slang source and regenerate.',
                f'// source: {Path(src_dir).name}/{task["source"]}; entry: {task["entries"][stage]}; stage: {stage}',
                f'// program: {task["id"]}; permutation: {perm}; slangc {version}']
        head += ['// ' + line for line in header_lines]
        head += ['// hooks: ' + ' '.join(used) if used else '// hooks:']
        content = '\n'.join(head) + '\n\n' + body
        fname = f'{task["id"]}.{common.STAGE_EXT[stage]}.glsl'
        result[stage] = {
            'file': fname, 'content': content, 'hooks': used, 'entry': task['entries'][stage],
            'interface': interface_of(reflection, task['entries'][stage]),
        }
    return task, result


def generate(out_dir, src_dir=None, slangc_override=None, jobs=None, strict=False, pin=None):
    """Write the generated programs and manifest into out_dir. Returns the manifest."""
    src_dir = Path(src_dir or common.SLANG_SRC)
    pin = pin or common.read_pin()
    plist = load_program_list(src_dir)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = {'generator': 1, 'slang': pin['version'], 'programs': []}
    files = {}
    if plist['tasks']:
        slangc = common.find_slangc(pin, slangc_override)
        with ThreadPoolExecutor(max_workers=jobs or os.cpu_count() or 4) as pool:
            futures = [pool.submit(compile_task, t, slangc, src_dir, plist['hooks'], strict, pin['version'])
                       for t in plist['tasks']]
            done = [f.result() for f in futures]
        for task, stages in done:
            entry = {'id': task['id'], 'program': task['program'], 'permutation': task['permutation'],
                     'source': f'{src_dir.name}/{task["source"]}', 'stages': {}}
            for stage, r in stages.items():
                data = r['content'].encode('utf-8')
                files[r['file']] = data
                entry['stages'][stage] = {'file': r['file'], 'entry': r['entry'], 'sha256': hashlib.sha256(data).hexdigest(),
                                          'hooks': r['hooks'], 'interface': r['interface']}
            manifest['programs'].append(entry)
    manifest['programs'].sort(key=lambda p: p['id'])
    files['programs.json'] = (json.dumps(manifest, indent=1, sort_keys=False) + '\n').encode('utf-8')
    for old in out_dir.glob('*.glsl'):  # only files this tool wrote; anything else in a mis-pointed --out-dir stays
        with open(old, encoding='utf-8', errors='replace') as f:
            ours = f.readline().startswith('// Generated by scripts/shaders/slang_gen.py')
        if ours:  # unlinked after the close: Windows refuses to delete an open file
            old.unlink()
    for name, data in files.items():
        (out_dir / name).write_bytes(data)
    return manifest


def diff_dirs(expected, actual):
    """Names that differ between two generated folders (missing, stale or changed)."""
    expected, actual = Path(expected), Path(actual)
    exp = {p.name: p.read_bytes() for p in expected.iterdir() if p.is_file()}
    act = {p.name: p.read_bytes() for p in actual.iterdir() if p.is_file()} if actual.is_dir() else {}
    problems = []
    for name in sorted(set(exp) | set(act)):
        if name not in act:
            problems.append(f'missing (run slang_gen.py): {name}')
        elif name not in exp:
            problems.append(f'stale (no program makes it): {name}')
        elif exp[name] != act[name]:
            problems.append(f'differs: {name}')
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src-dir', default=str(common.SLANG_SRC))
    ap.add_argument('--out-dir', default=str(common.GENERATED))
    ap.add_argument('--check', action='store_true', help='compare with --out-dir instead of writing it (CI drift)')
    ap.add_argument('--slangc', help='slangc to use (its version must still match the pin)')
    ap.add_argument('--jobs', type=int)
    ap.add_argument('--strict', action='store_true', help='slangc warnings fail the run (CI)')
    args = ap.parse_args()
    try:
        if args.check:
            with tempfile.TemporaryDirectory() as tmp:
                generate(tmp, args.src_dir, args.slangc, args.jobs, args.strict)
                problems = diff_dirs(tmp, args.out_dir)
            if problems:
                print('generated shaders drift from their Slang sources:\n  ' + '\n  '.join(problems))
                print('Regenerate with: python scripts/shaders/slang_gen.py   (never edit or hand-merge generated files)')
                sys.exit(1)
            print('generated shaders match their Slang sources')
        else:
            m = generate(args.out_dir, args.src_dir, args.slangc, args.jobs, args.strict)
            print(f'wrote {len(m["programs"])} program(s) to {args.out_dir}')
    except GenError as e:
        print(f'slang_gen: {e}')
        sys.exit(1)


if __name__ == '__main__':
    main()

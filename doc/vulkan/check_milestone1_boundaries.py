"""Check pinned source-analysis evidence; does not execute the proposed gates."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

from inventory_helpers import mask_source

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).with_name('milestone1-boundaries.json')
GATES = {'G-' + name for name in (
    'BACKEND', 'RESOURCE', 'UI', 'STARTUP', 'SESSION', 'PROTOCOL', 'CHAT',
    'FRAME', 'WORLD', 'AUX', 'SETTINGS', 'LIFETIME')}
REGISTRIES = {
    'messages': ('indra/newview/llstartup.cpp', r'\bsetHandlerFunc(?:Fast)?\s*\('),
    'settings': ('indra/newview/llviewercontrol.cpp', r'\bsetting_setup_signal_listener\s*\('),
    'floaters': ('indra/newview/llviewerfloaterreg.cpp', r'\bLLFloaterReg::add\s*\('),
    'names': ('indra/llmessage/llcachename.cpp', r'\bsetHandlerFunc(?:Fast)?\s*\('),
    'xfers': ('indra/llmessage/llxfermanager.cpp', r'\bsetHandlerFunc(?:Fast)?\s*\('),
}
REGISTRY_GATES = {'messages': 'G-PROTOCOL', 'settings': 'G-SETTINGS', 'floaters': 'G-UI',
                  'names': 'G-PROTOCOL', 'xfers': 'G-PROTOCOL'}


def census(root=ROOT):
    result = {}
    for kind, (path, pattern) in REGISTRIES.items():
        source = (root / path).read_text(encoding='utf-8')
        code = mask_source(source)
        if kind == 'settings':
            # Exclude the two wrapper definitions, retain every invocation below.
            start = code.index('void settings_setup_listeners()')
        else:
            start = 0
        structure = mask_source(source, literals=True)
        entries = []
        for match in re.finditer(pattern, code[start:]):
            begin = start + match.start()
            opening = start + match.end() - 1
            depth = 1
            end = opening + 1
            while end < len(structure) and depth:
                depth += (structure[end] == '(') - (structure[end] == ')')
                end += 1
            if depth:
                raise ValueError(f'Unclosed registry call: {path}')
            entries.append({'line': source.count('\n', 0, begin) + 1,
                            'call': ' '.join(code[begin:end].split())})
        if not entries:
            raise ValueError(f'Empty registry census: {kind}')
        result[kind] = entries
    code = mask_source((root / 'indra/newview/llstartup.h').read_text(encoding='utf-8'))
    result['states'] = re.findall(r'^\s*(STATE_[A-Z0-9_]+)\s*,?', code, re.M)
    paths = subprocess.check_output(['git', 'ls-files', 'indra'], cwd=root, text=True).splitlines()
    result['lifecycles'] = []
    for path in paths:
        if Path(path).suffix not in ('.h', '.cpp'):
            continue
        code = mask_source((root / path).read_text(encoding='utf-8', errors='replace'))
        for match in re.finditer(r'\bLL(?:Init|Destroy)Class\s*<\s*(\w+)\s*>', code):
            # Include friend references conservatively; exclude template definition T.
            if match.group(1) == 'T':
                continue
            result['lifecycles'].append({'path': path, 'line': code.count('\n', 0, match.start()) + 1,
                                         'registration': match.group()})
    return result


def validate(data, root=ROOT):
    errors = []
    if data.get('acceptance') != 'source-analysis-only' or data.get('gates_implemented') is not False:
        errors.append('Evidence must not claim runtime or implemented-gate acceptance')
    catalog = json.loads((root / 'doc/vulkan/diligent-insertion-records.json').read_text())
    if data.get('source_commit') != catalog['source_commit']:
        errors.append('Source pin differs from the insertion catalog')
    if set(data.get('gates', [])) != GATES:
        errors.append('Missing or unknown gate')
    expected_families = {f'I{i:02}' for i in range(1, 28)}
    if set(data.get('families', {})) != expected_families:
        errors.append('Incomplete insertion-family accounting')
    for name, gates in data.get('families', {}).items():
        if not gates or not set(gates) <= GATES:
            errors.append(f'Invalid gates for family {name}')
    if set(data.get('routes', {})) != {f'R{i:02}' for i in range(1, 13)}:
        errors.append('Incomplete route-root accounting')
    evidence = data.get('sources', {})
    for path, record in evidence.items():
        target = root / path
        if not target.is_file():
            errors.append(f'Missing reviewed source: {path}')
            continue
        raw = target.read_bytes()
        if hashlib.sha256(raw).hexdigest() != record['sha256']:
            errors.append(f'Stale reviewed source: {path}')
        source = raw.decode('utf-8')
        if not record.get('anchors'):
            errors.append(f'No source anchors: {path}')
        for anchor in record.get('anchors', []):
            if anchor not in mask_source(source):
                errors.append(f'Missing source anchor: {path}: {anchor}')
    for name, route in data.get('routes', {}).items():
        if not route.get('sources') or not set(route['sources']) <= set(evidence):
            errors.append(f'Unreviewed route source: {name}')
        if not route.get('gates') or not set(route['gates']) <= GATES or not route.get('cpu_outcome'):
            errors.append(f'Incomplete caller/gate contract: {name}')
    current = census(root)
    if set(data.get('states', {})) != set(current['states']):
        errors.append('Incomplete startup-state accounting')
    for state, disposition in data.get('states', {}).items():
        if disposition not in ('retain-split', 'native-fonts', 'skip-optional', 'replace-visual-wait'):
            errors.append(f'Unknown state disposition: {state}')
    for kind in REGISTRIES:
        entries = data.get('registries', {}).get(kind, [])
        if [{'line': e.get('line'), 'call': e.get('call')} for e in entries] != current[kind]:
            errors.append(f'Stale or incomplete {kind} registry')
        for entry in entries:
            if entry.get('gate') != REGISTRY_GATES[kind] or entry.get('disposition') not in ('retain-split', 'defer'):
                errors.append(f'Unclassified {kind} entry at {entry.get("line")}')
            expected_route = {'messages': 'R05', 'names': 'R05', 'xfers': 'R05',
                              'settings': 'R10', 'floaters': 'R09'}[kind]
            if entry.get('caller_route') != expected_route:
                errors.append(f'Unaccounted {kind} caller route')
        if REGISTRIES[kind][0] not in evidence:
            errors.append(f'Unhashed {kind} registry source')
    lifecycle = data.get('lifecycles', [])
    if [{k: e.get(k) for k in ('path', 'line', 'registration')} for e in lifecycle] != current['lifecycles']:
        errors.append('Stale or incomplete init/destroy registration census')
    for entry in lifecycle:
        if entry.get('gate') != 'G-LIFETIME' or entry.get('disposition') not in ('retain-cpu', 'defer'):
            errors.append('Unclassified init/destroy callback')
        if entry.get('caller_route') != 'R12':
            errors.append('Unaccounted init/destroy caller route')
        if entry['path'] not in evidence:
            errors.append('Unhashed init/destroy callback declaration: ' + entry['path'])
    return errors


def main():
    data = json.loads(MANIFEST.read_text(encoding='utf-8'))
    errors = validate(data)
    # Same scoped source pin as the broader catalog, including uncommitted edits.
    changed = subprocess.check_output(
        ['git', 'diff', '--name-only', data['source_commit'], '--',
         'indra', '3p', 'autobuild.xml', 'scripts/build_vulkan_dependencies.py'],
        cwd=ROOT, text=True).splitlines()
    if changed:
        errors.append('Scoped source differs from pin: ' + ', '.join(changed))
    if errors:
        raise SystemExit('\n'.join(errors))
    counts = ', '.join(f'{k}={len(v)}' for k, v in data['registries'].items())
    print(f'PASS: source analysis only; {len(data["routes"])} routes, '
          f'{len(data["families"])} families, {len(data["states"])} states; {counts}.')
    print('Proposed gates are not implemented; runtime remains unqualified.')


if __name__ == '__main__':
    main()

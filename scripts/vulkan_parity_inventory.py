"""Source-backed R0 acceptance inventory. Discovery never grants acceptance."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'doc/vulkan/parity'


def stable_id(kind, value):
    # Keep semantic spelling (including case), rather than line coordinates.
    return kind + '.' + value


def inventory(root=ROOT):
    rows = {}
    def add(kind, key, title, reference, owner, phase, gap, variants=()):
        identity = stable_id(kind, key)
        if identity in rows:
            row = rows[identity]
            row['reference_sources'] = sorted(set(row['reference_sources'] + reference))
            return
        rows[identity] = dict(id=identity, category=kind, title=title,
            reference_sources=sorted(reference), native_owner=owner,
            phase=phase, dependencies=['R0', 'R1'] if phase != 'R1' else ['R0'],
            variants=list(variants), implementation='unqualified',
            source_qualified=False, software_runtime_qualified=False,
            authenticated_runtime_qualified=False, evidence=[], remaining_gap=gap)

    survey = root / 'doc/vulkan/full-render-chain-survey.md'
    section = ''
    families = {
        'Geometry and simulation': ('R2', 'native scene/geometry/avatar producers'),
        'Materials and texture resources': ('R3', 'native materials/texture service'),
        'Environment, lighting and dependent views': ('R5', 'native environment/render graph'),
        'Transparency, HDR and postprocessing': ('R8', 'native render graph/finalization'),
        'Auxiliary rendering and feature owners': ('R9', 'native interaction/auxiliary/service owners'),
        'Second-pass completeness findings': ('R9', 'native production feature owners'),
        'Settings-to-native-controller inventory': ('R3', 'native settings controllers'),
    }
    # One entry for every table requirement, including the second survey pass.
    for line in survey.read_text(encoding='utf-8').splitlines():
        if line.startswith('## '): section = line[3:]
        if not line.startswith('| ') or section not in families: continue
        cells = [c.strip() for c in line.strip('|').split('|')]
        if cells[0].startswith(('System', 'Controller family', '---')): continue
        title = cells[0].split(':', 1)[0].replace('**', '').strip()
        key = re.sub(r'[^A-Za-z0-9]+', '-', title).strip('-')
        phase, owner = families[section]
        add('SURVEY', key, title, ['doc/vulkan/full-render-chain-survey.md#' +
            re.sub(r'[^a-z0-9 -]', '', section.lower()).replace(' ', '-')],
            owner, phase, 'Implement and qualify every behavior in this survey row. ' +
            ' | '.join(cells[1:]))
        row=rows[stable_id('SURVEY',key)]
        row['reference_seams']=cells[0]
        row['reference_symbols']=sorted(set(re.findall(r'`([^`]+)`',cells[0])))

    pool_path = 'indra/newview/lldrawpool.h'
    code = (root / pool_path).read_text(encoding='utf-8')
    # Definition declarations only: do not count comments or later uses.
    enums = re.findall(r'enum\s*\{(.*?)\}', code, re.S)
    for block in enums:
        block = re.sub(r'//[^\n]*|/\*.*?\*/', '', block, flags=re.S)
        for name in re.findall(r'\b((?:POOL|PASS)_[A-Z0-9_]+)\b\s*(?:=\s*[^,]+)?\s*[,\n]', block):
            variants = [v.lower() for v in ('RIGGED', 'MASK', 'ALPHA', 'GLOW') if v in name]
            add('DRAW', name, name, [pool_path + ':' + name],
                'native pass classification/material producer', 'R4',
                'Reconcile this exact reference pool/pass; qualify all producer and view variants.', variants)

    control = 'indra/newview/llviewercontrol.cpp'
    code = (root / control).read_text(encoding='utf-8')
    code = re.sub(r'//[^\n]*|/\*.*?\*/', '', code, flags=re.S)
    for group, name, handler in re.findall(
            r'setting_setup_signal_listener\(\s*(\w+),\s*"([^"]+)"\s*,\s*([\w:]+)', code):
        add('SETTING', group + '.' + name, name,
            [control + ':' + handler], 'native settings/controller effect', 'R3',
            'Review reference callback ' + handler + '; saving a value is not applying its effect.')

    # Ordinary XUI commit, enable, visible and validation callbacks across ALL skins.
    tracked = subprocess.check_output(['git', '-C', str(root), 'ls-files',
        'indra/newview/skins/**/*.xml'], text=True).splitlines()
    for relative in tracked:
        path = root / relative
        try: tree = ET.parse(path)
        except ET.ParseError as exc: raise ValueError(f'Malformed XUI: {relative}: {exc}') from exc
        for element in tree.iter():
            function = element.get('function')
            if not function: continue
            add('UI', function, function, [relative + ':' + element.tag],
                'ordinary shared UI callback with native production owner', 'R9',
                'Exercise callback and actual owner/output, including enabled/disabled and error behavior.')

    # Floaters and toolbar commands can have no XUI function attribute at all.
    for relative in subprocess.check_output(['git', '-C', str(root), 'ls-files',
            'indra/newview/*.cpp'], text=True).splitlines():
        text = (root / relative).read_text(encoding='utf-8')
        for name in re.findall(r'LLFloaterReg::add\(\s*"([^"]+)"', text):
            add('FLOATER', name, name, [relative + ':LLFloaterReg::add'],
                'shared floater controller and native resource/service adapter', 'R9',
                'Construct through the ordinary UI and exercise every production effect; registration is not wiring.')
    commands_path = root/'indra/newview/app_settings/commands.xml'
    for element in ET.parse(commands_path).getroot().iter('command'):
        name = element.get('name')
        if name:
            add('COMMAND', name, name, ['indra/newview/app_settings/commands.xml:' + name],
                'native toolbar/action controller', 'R9',
                'Exercise actual command output, availability and toggle state with its ordinary UI.')

    extras = [
        ('Startup', 'R1', 'indra/newview/llstartup.cpp', 'native startup/session',
         'Initialization, settings, authentication/consent, service readiness, failures, retry and cancellation.'),
        ('LoginProgress', 'R9', 'indra/newview/llprogressview.cpp', 'native progress/XUI',
         'Real login stage events, status/percent/message, background/logo/intro, fade and modal input.'),
        ('LogoutProgress', 'R9', 'indra/newview/llappviewer.cpp', 'native logout/progress',
         'Saving settings, real logout reply/timeout and visible progress until teardown.'),
        ('PersistentTextureCache', 'R3', 'indra/newview/lltexturecache.cpp', 'native fetch/cache subscribers',
         'Cold/warm/restart, partial data, version/corruption, size/location/clear, read-only and I/O shutdown.'),
        ('BuiltinAO', 'R2', 'indra/newview/aoengine.cpp', 'native avatar/animation/AO owner',
         'Ordinary AO controls, locomotion transitions, overrides, sit policy, teleport/reconnect and retirement.'),
        ('Underwater', 'R7', 'indra/newview/lldrawpoolwater.cpp', 'native water/graph',
         'Above/below water transitions, exclusion, haze, alpha and reference depth semantics.'),
        ('HUDProjection', 'R9', 'indra/newview/llviewerdisplay.cpp', 'native HUD/interaction views',
         'HUD attachment/text/icon projection, picking, input and material variants.'),
    ]
    for key, phase, source, owner, gap in extras:
        if not (root / source).is_file(): raise ValueError('Missing reference source: ' + source)
        add('FEATURE', key, key, [source], owner, phase, gap,
            ('underwater',) if key == 'Underwater' else ('hud',) if key == 'HUDProjection' else ())

    # Literal source references are navigation witnesses, NOT proof that a
    # callback is registered/admitted or that a native effect is functioning.
    ui_names={r['title'] for r in rows.values() if r['category']=='UI'}
    source_index={}
    native_settings={}
    for relative in subprocess.check_output(['git','-C',str(root),'ls-files',
            'indra/**/*.cpp','indra/**/*.h'],text=True).splitlines():
        literals=set(re.findall(r'"([^"\n]+)"',(root/relative).read_text(encoding='utf-8',errors='replace')))
        for name in literals & ui_names: source_index.setdefault(name,[]).append(relative)
        if relative.startswith('indra/newview/vs') and not any(tag in relative for tag in
                ('vsparity','vsnativereplay','vsnativeimreplay','vsvkdiag','vsuifixture','vsuipixeloracle')):
            for name in literals: native_settings.setdefault(name,[]).append(relative)
    for row in rows.values():
        if row['category']=='UI':
            row['shared_source_references']=sorted(source_index.get(row['title'],[]))
            row['implementation']='shared-source-present/native-effect-unqualified' if row['shared_source_references'] else 'source-registration-unresolved'
            if not row['shared_source_references']:
                row['remaining_gap']='Resolve dynamic registration or inactive/legacy XUI use; literal source search found no witness. '+row['remaining_gap']
        elif row['category']=='FLOATER':
            row['implementation']='registered/native-construction-unqualified'
        elif row['category']=='COMMAND':
            row['implementation']='declared/native-action-unqualified'
        elif row['category']=='SETTING':
            row['native_source_references']=sorted(native_settings.get(row['title'],[]))
            row['implementation']='native-reference-present/effect-unqualified' if row['native_source_references'] else 'native-controller-pending'
        elif row['category']=='DRAW':
            row['implementation']='native-reference-pass-integration-pending'
            row['native_source_references']=['indra/newview/vsworldrenderer.cpp']
    reviewed=json.loads((root/'doc/vulkan/parity/source-dispositions.json').read_text())
    for identity,review in reviewed['entries'].items():
        if identity not in rows: raise ValueError('Stale semantic source disposition: '+identity)
        rows[identity].update(review)
        rows[identity]['review_reference']='doc/vulkan/parity/source-dispositions.json'
    dependencies={'R1':['R0'],'R2':['R0','R1'],'R3':['R0','R1'],
        'R4':['R0','R1','R3'],'R5':['R0','R3','R4'],'R6':['R0','R2','R4','R5'],
        'R7':['R0','R2','R3','R4','R5','R6'],'R8':['R0','R4','R5','R6','R7'],
        'R9':['R0','R1','R2','R4']}
    hosts={'R1':['vsnativesession.cpp','vsworldscene.cpp'],
        'R2':['vsworldscene.cpp','vsworldavatar.cpp','vsworldassets.cpp'],
        'R3':['vsuiimageprovider.cpp','vsuiresources.cpp','vsworldrenderer.cpp'],
        'R4':['vsvulkancontext.cpp','vsworldrenderer.cpp'],
        'R5':['vsvulkancontext.cpp','vsworldscene.cpp'],
        'R6':['vsvulkancontext.cpp','vsworldrenderer.cpp'],
        'R7':['vsworldscene.cpp','vsworldrenderer.cpp','vsnativesession.cpp'],
        'R8':['vsvulkancontext.cpp','vsworldrenderer.cpp'],
        'R9':['vsstartupui.cpp','vsnativesession.cpp','llviewerwindow.cpp']}
    for row in rows.values():
        row['dependencies']=dependencies[row['phase']]
        row['native_integration_hosts']=['indra/newview/'+p for p in hosts[row['phase']]]
        row['owner_scope']='Allocated responsibility/integration hosts; existence of a host does not establish this feature implementation.'

    return [rows[k] for k in sorted(rows)]


def build():
    rows = inventory()
    return dict(schema=1, scope='Full parity acceptance inventory; discovery only, no runtime acceptance.',
                entries=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    data = build()
    path = OUT / 'acceptance-ledger.json'
    text = json.dumps(data, indent=2, ensure_ascii=False) + '\n'
    if args.write:
        OUT.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
    elif not path.is_file() or path.read_text(encoding='utf-8') != text:
        raise SystemExit('R0 ledger is stale; review source delta before --write.')
    counts = {c: sum(r['category'] == c for r in data['entries'])
              for c in sorted({r['category'] for r in data['entries']})}
    print(json.dumps({'entries': len(data['entries']), 'categories': counts,
                      'source_inventory_current': True, 'runtime_acceptance': False}))


if __name__ == '__main__': main()

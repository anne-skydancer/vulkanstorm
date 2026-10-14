"""Run the viewer's device-free geometry executable and preserve its evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET


def required_checks(species_count):
    return ['PASS: 136 primitive/sculpt topology, LOD and mirror/invert cases',
            'PASS: terrain patch sizes, all LODs and neighboring strides',
            f'PASS: {species_count} tree species, all four LODs, repeat and transform',
            'PASS: grass LODs, terrain, paired faces',
            'PASS: flexible simulation/render resolutions, forces and wind']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    build = args.build_directory.resolve()
    output = args.evidence.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage = build / 'newview' / ('RelWithDebInfo' if os.name == 'nt' else 'packaged')
    binary = build / 'newview' / ('RelWithDebInfo/viewer_geometry_test.exe' if os.name == 'nt' else 'viewer_geometry_test')
    species = stage / 'app_settings/trees.xml'
    result = dict(schema=1, scope='R2 CPU geometry producers', passed=False)
    started = time.monotonic()
    try:
        provenance = json.loads((build / 'parity-build-provenance.json').read_text())
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
        dirty = subprocess.check_output(['git', 'status', '--porcelain', '--', 'indra', 'scripts', 'tests'], text=True)
        if dirty.strip() or not provenance['complete'] or provenance['source_commit'] != head:
            raise RuntimeError('Geometry qualification requires a committed, complete viewer build')
        result.update(source_commit=head, executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                      tree_species_sha256=hashlib.sha256(species.read_bytes()).hexdigest())
        env = os.environ.copy()
        if os.name != 'nt':
            env['LD_LIBRARY_PATH'] = str(stage / 'lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
        run = subprocess.run([str(binary), str(species)], cwd=stage, env=env,
                             capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        (output / 'geometry.log').write_text(log, encoding='utf-8')
        count = len(ET.parse(species).getroot().findall('tree'))
        required = required_checks(count)
        if run.returncode or any(line not in log for line in required) or 'FAIL:' in log:
            raise RuntimeError(f'Geometry producer qualification failed (exit {run.returncode})')
        result.update(passed=True, checks=required, tree_species=count,
                      log_sha256=hashlib.sha256((output / "geometry.log").read_bytes()).hexdigest())
    except Exception as exception:
        result['error'] = str(exception)
    result['elapsed_seconds'] = round(time.monotonic() - started, 3)
    (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(('PASS' if result['passed'] else 'FAIL') + ': viewer CPU geometry producers')
    if not result['passed']:
        raise SystemExit(result['error'])


if __name__ == '__main__':
    main()

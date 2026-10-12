"""Bind a staged viewer to a clean source revision across the actual build."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def source():
    dirty = subprocess.check_output(['git', 'status', '--porcelain', '--', 'indra', 'scripts'], cwd=ROOT, text=True)
    if dirty.strip():
        raise RuntimeError('Parity qualification requires committed source before and after the build')
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('begin', 'finish'))
    parser.add_argument('build', type=Path)
    args = parser.parse_args()
    manifest = args.build / 'parity-build-provenance.json'
    revision = source()
    if args.phase == 'begin':
        manifest.write_text(json.dumps(dict(schema=1, source_commit=revision, complete=False), indent=2)+'\n')
        return
    record = json.loads(manifest.read_text())
    if record['source_commit'] != revision or record['complete']:
        raise RuntimeError('Missing fresh build start or source changed during build')
    candidates = list((args.build/'newview/RelWithDebInfo').glob('*-bin.exe'))
    candidates += list((args.build/'newview/packaged/bin').glob('*-bin'))
    if len(candidates) != 1:
        raise RuntimeError('Expected exactly one completely staged viewer')
    executable = candidates[0].resolve()
    if executable.stat().st_mtime < manifest.stat().st_mtime:
        raise RuntimeError('Executable predates build start; rebuild the viewer for this source')
    record.update(complete=True, executable=str(executable), executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    manifest.write_text(json.dumps(record, indent=2)+'\n')


if __name__ == '__main__':
    main()

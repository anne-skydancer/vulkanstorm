"""Install a checksum-pinned compiler cache; retain honest cache evidence."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def unpack_binary(data, pin, windows):
    if hashlib.sha256(data).hexdigest() != pin['sha256']:
        raise RuntimeError('Compiler cache archive checksum mismatch')
    # Extract only the single executable, never archive-provided paths.
    name = 'sccache.exe' if windows else 'sccache'
    if windows:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            matches = [entry for entry in archive.namelist() if entry.split('/')[-1] == name]
            if len(matches) != 1:
                raise RuntimeError('Expected one compiler cache executable')
            return archive.read(matches[0])
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        matches = [entry for entry in archive.getmembers() if entry.name.split('/')[-1] == name and entry.isfile()]
        if len(matches) != 1:
            raise RuntimeError('Expected one compiler cache executable')
        return archive.extractfile(matches[0]).read()


def msvc_environment():
    locator = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([str(locator), '-latest', '-version', '[17.0,18.0)',
        '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property', 'installationPath'], text=True).strip()
    if not installation:
        raise RuntimeError('Visual Studio 2022 C++ toolchain missing')
    developer = Path(installation) / 'Common7/Tools/VsDevCmd.bat'
    # Pass cmd its native command line: list2cmdline escapes these nested quotes
    # for a C runtime, which cmd.exe does not use to parse /c.
    result = subprocess.check_output(
        f'cmd.exe /d /s /c ""{developer}" -no_logo -arch=x64 -host_arch=x64 && set"', text=True)
    return dict(line.split('=', 1) for line in result.splitlines() if '=' in line and not line.startswith('='))


def setup():
    system = platform.system()
    lock = json.loads((ROOT / 'scripts/ci_compiler_cache.json').read_text())
    pin = lock[system]
    url = f'https://github.com/mozilla/sccache/releases/download/v{lock["version"]}/{pin["archive"]}'
    with urllib.request.urlopen(url, timeout=120) as response:
        binary = unpack_binary(response.read(), pin, system == 'Windows')
    directory = ROOT / '.ci/compiler-tool'
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / ('sccache.exe' if system == 'Windows' else 'sccache')
    executable.write_bytes(binary)
    executable.chmod(0o755)
    values = msvc_environment() if system == 'Windows' else {}
    values.update(SCCACHE_DIR=str(ROOT / '.ci/compiler-cache'), SCCACHE_CACHE_SIZE='2G',
                  SCCACHE_DIRECT='false')
    with open(os.environ['GITHUB_ENV'], 'a', encoding='utf-8') as output:
        for key, value in values.items():
            if '\n' in value or '\r' in value:
                raise RuntimeError('Multiline build environment value')
            # Export only changes, without printing environment values to logs.
            if os.environ.get(key) != value:
                output.write(f'{key}={value}\n')
    with open(os.environ['GITHUB_PATH'], 'a', encoding='utf-8') as output:
        output.write(str(directory) + '\n')
    print(f'Installed checksum-verified sccache {lock["version"]} ({system})')


def report(output):
    output.mkdir(parents=True, exist_ok=True)
    executable = shutil.which('sccache')
    if not executable:
        (output / 'compiler-cache.txt').write_text('Compiler cache setup did not complete.\n')
        return
    result = subprocess.run([executable, '--show-stats'], capture_output=True, text=True, check=True)
    (output / 'compiler-cache.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as summary:
            summary.write('\nCompiler cache statistics\n\n```text\n' + result.stdout + '\n```\n')
    subprocess.run([executable, '--stop-server'], check=True, capture_output=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('setup', 'report'))
    parser.add_argument('--evidence', type=Path, default=Path('.ci/evidence'))
    args = parser.parse_args()
    setup() if args.command == 'setup' else report(args.evidence)

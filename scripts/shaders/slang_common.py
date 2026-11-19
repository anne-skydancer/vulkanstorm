# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Shared paths, pin reading and slangc discovery for the Slang shader tooling (build tools, never shipped)."""
import json
import os
import platform
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PIN_FILE = Path(__file__).resolve().with_name('slang_pin.json')
SLANG_SRC = ROOT / 'indra/newview/slang'
GENERATED = ROOT / 'indra/newview/app_settings/shaders/generated'
LEGACY_SHADERS = ROOT / 'indra/newview/app_settings/shaders'

STAGE_EXT = {'vertex': 'vert', 'fragment': 'frag', 'compute': 'comp'}
EXT_STAGE = {v: k for k, v in STAGE_EXT.items()}


def read_pin(path=PIN_FILE):
    pin = json.loads(Path(path).read_text(encoding='utf-8'))
    if not re.fullmatch(r'\d{4}\.\d+(\.\d+)?', pin['version']):
        raise SystemExit(f'pin: bad version {pin["version"]!r}')
    return pin


def platform_key():
    machine = platform.machine().lower()
    if machine not in ('amd64', 'x86_64'):
        raise SystemExit(f'no pinned Slang archive for machine {machine!r}')
    keys = {'Windows': 'windows-x86_64', 'Linux': 'linux-x86_64'}
    if platform.system() not in keys:
        raise SystemExit(f'no pinned Slang archive for {platform.system()}')
    return keys[platform.system()]


def cache_root():
    """The tool cache lives outside the tree: VKSTORM_TOOLS_CACHE, else the user's local data folder."""
    env = os.environ.get('VKSTORM_TOOLS_CACHE')
    if env:
        return Path(env)
    if platform.system() == 'Windows':
        return Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) / 'vulkanstorm-tools'
    return Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache')) / 'vulkanstorm-tools'


def slang_dir(pin):
    return cache_root() / 'slang' / pin['version']


def slangc_path(pin):
    exe = 'slangc.exe' if platform.system() == 'Windows' else 'slangc'
    root = slang_dir(pin)
    for base in (root / 'bin', root):
        if (base / exe).is_file():
            return base / exe
    found = sorted(root.glob(f'*/bin/{exe}')) if root.is_dir() else []  # an archive with one top-level folder
    return found[0] if found else None


def slangc_version(slangc):
    out = subprocess.run([str(slangc), '-v'], capture_output=True, text=True, check=True)
    return (out.stdout + out.stderr).strip().splitlines()[-1].strip()


def find_slangc(pin, override=None):
    """The pinned slangc, with its version checked against the pin. Never a slangc found on PATH."""
    path = Path(override) if override else slangc_path(pin)
    if not path or not Path(path).is_file():
        raise SystemExit(f'pinned slangc {pin["version"]} not found; run: python scripts/shaders/slang_fetch.py')
    got = slangc_version(path)
    if got != pin['version']:
        raise SystemExit(f'slangc at {path} reports {got!r}, the pin says {pin["version"]!r}')
    return Path(path)


def header_hooks(text):
    """The hook macro names a generated file lists in its leading comment block ('// hooks: A B')."""
    for line in text.split('\n'):
        if not line.startswith('//'):
            break
        if line.startswith('// hooks:'):
            return line[len('// hooks:'):].split()
    return []


# The first uniform-block binding point a generated program's global block may use: the viewer's own UBOs
# (LLGLSLShader::UniformBlock: ReflectionProbes, GLTFJoints, GLTFNodes, GLTFMaterials, Atmosphere) hold bindings 0 to
# NUM_UNIFORM_BLOCKS - 1, so the global block starts at NUM_UNIFORM_BLOCKS (counted from the header: a new viewer UBO moves it).


def reserved_attribs():
    """The viewer's vertex attribute table, in slot order, read from LLShaderMgr (the one place that defines it)."""
    text = (ROOT / 'indra/llrender/llshadermgr.cpp').read_text(encoding='utf-8')
    return re.findall(r'mReservedAttribs\.push_back\("(\w+)"\)', text)


def reserved_block_count():
    """NUM_UNIFORM_BLOCKS of LLGLSLShader::UniformBlock, counted from the header."""
    text = (ROOT / 'indra/llrender/llglslshader.h').read_text(encoding='utf-8')
    body = re.search(r'enum UniformBlock[^{]*\{(.*?)\};', text, re.S).group(1)
    body = re.sub(r'//[^\n]*', '', body)
    return len([x for x in re.findall(r'(?m)^\s*(\w+)\s*,', body) if x != 'NUM_UNIFORM_BLOCKS'])


GLOBALS_BINDING = reserved_block_count()

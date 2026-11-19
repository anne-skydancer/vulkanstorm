# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Palette-block upload cost, hand-written loose uniform against the generated uniform block (no viewer, no login).

Dumps the two rigged bump vertex stages (today's class1/objects/bumpV.glsl + objectSkinV.glsl with the loose
`uniform mat3x4 matrixPalette[110]`, and the generated bump__HAS_SKIN1 program whose palette is a
member of the std140 block) and runs scripts/perf/bench_palette_block.cpp, which times both upload paths as the viewer
does them. Needs g++ or clang++ on PATH and a GPU; Windows OpenGL 4.6.

  python scripts/perf/bench_palette_block.py [--rounds 8] [--draws 480]
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/shaders'))
import ab_harness as ab  # noqa: E402
import slang_check as check  # noqa: E402
import slang_common as common  # noqa: E402


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--rounds', default='8')
    p.add_argument('--draws', default='480')
    p.add_argument('--verts-cells', default='40')
    p.add_argument('--generated', default=str(common.GENERATED))
    args = p.parse_args()
    out = Path(tempfile.mkdtemp(prefix='palettebench'))
    defines = {'MAX_JOINTS_PER_MESH_OBJECT': '110', 'HAS_SKIN': '1'}
    vs = ab.read_legacy('objects/bumpV.glsl', defines, 'vertex')
    vs += '\n' + (common.LEGACY_SHADERS / 'class1/avatar/objectSkinV.glsl').read_text(encoding='utf-8')
    (out / 'ref.vert').write_text(vs, encoding='utf-8')
    manifest = check.load_manifest(args.generated)
    prog = next(x for x in manifest['programs'] if x['id'] == 'bump__HAS_SKIN1')
    text = (Path(args.generated) / prog['stages']['vertex']['file']).read_text(encoding='utf-8')
    (out / 'cand.vert').write_text(check.assemble_source('vertex', text, {}), encoding='utf-8')
    (out / 'ref.frag').write_text(ab.SKIN_REF_FS, encoding='utf-8')
    (out / 'cand.frag').write_text(ab.SKIN_CAND_FS, encoding='utf-8')
    exe = out / 'bench.exe'
    cxx = 'g++'
    subprocess.run([cxx, '-O2', '-std=c++17', str(ROOT / 'scripts/perf/bench_palette_block.cpp'), '-o', str(exe),
                    '-lopengl32', '-lgdi32', '-luser32'], check=True)
    subprocess.run([str(exe), str(out), args.rounds, args.draws, args.verts_cells], check=True)


if __name__ == '__main__':
    main()

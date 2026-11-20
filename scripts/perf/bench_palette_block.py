# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Uniform upload cost, hand-written loose uniforms against the generated uniform block.

Dumps the two rigged bump vertex stages (today's class1/objects/bumpV.glsl + objectSkinV.glsl with the loose
`uniform mat3x4 matrixPalette[110]`, and the generated bump__HAS_SKIN1 program whose palette is a
member of the std140 block) and runs scripts/perf/bench_palette_block.cpp, which times both upload paths as the viewer
does them. --small-uploads uses the static bump program and updates two adjacent mat4 uniforms (128 bytes) per draw.
Needs g++ on PATH and a GPU with Windows OpenGL 4.6.

  python scripts/perf/bench_palette_block.py [--rounds 8] [--draws 480]
  python scripts/perf/bench_palette_block.py --small-uploads [--draws 3000] [--expect-renderer TEXT]
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
    p.add_argument('--rounds', type=int, default=8)
    p.add_argument('--draws', type=int)
    p.add_argument('--verts-cells', type=int)
    p.add_argument('--small-uploads', action='store_true', help='compare a 128-byte dirty range on the static bump')
    p.add_argument('--expect-renderer', help='require this substring in the native benchmark renderer name')
    p.add_argument('--generated', default=str(common.GENERATED))
    args = p.parse_args()
    draws = args.draws if args.draws is not None else (3000 if args.small_uploads else 480)
    cells = args.verts_cells if args.verts_cells is not None else (1 if args.small_uploads else 40)
    if min(args.rounds, draws, cells) < 1:
        p.error('--rounds, --draws and --verts-cells must be positive')
    if args.expect_renderer == '':
        p.error('--expect-renderer must be nonempty')
    out = Path(tempfile.mkdtemp(prefix='palettebench'))
    defines = {} if args.small_uploads else {'MAX_JOINTS_PER_MESH_OBJECT': '110', 'HAS_SKIN': '1'}
    vs = ab.read_legacy('objects/bumpV.glsl', defines, 'vertex')
    if not args.small_uploads:
        vs += '\n' + (common.LEGACY_SHADERS / 'class1/avatar/objectSkinV.glsl').read_text(encoding='utf-8')
    (out / 'ref.vert').write_text(vs, encoding='utf-8')
    manifest = check.load_manifest(args.generated)
    program_id = 'bump__HAS_SKIN0' if args.small_uploads else 'bump__HAS_SKIN1'
    prog = next(x for x in manifest['programs'] if x['id'] == program_id)
    text = (Path(args.generated) / prog['stages']['vertex']['file']).read_text(encoding='utf-8')
    (out / 'cand.vert').write_text(check.assemble_source('vertex', text, {}), encoding='utf-8')
    (out / 'ref.frag').write_text(ab.SKIN_REF_FS, encoding='utf-8')
    (out / 'cand.frag').write_text(ab.SKIN_CAND_FS, encoding='utf-8')
    exe = out / 'bench.exe'
    cxx = 'g++'
    subprocess.run([cxx, '-O2', '-std=c++17', str(ROOT / 'scripts/perf/bench_palette_block.cpp'), '-o', str(exe),
                    '-lopengl32', '-lgdi32', '-luser32'], check=True)
    cmd = [str(exe), str(out), str(args.rounds), str(draws), str(cells)]
    if args.small_uploads:
        cmd.append('--small-uploads')
    if args.expect_renderer is not None:
        cmd.extend(['--expect-renderer', args.expect_renderer])
    return subprocess.run(cmd).returncode


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the stochastic transparency Slang passes with GLSL using the viewer's 8× MSAA pass sequence."""

import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import sys

import numpy as np

import ab_cases
import ab_harness as H
import slang_check as check
import slang_common as common
import slang_gen as gen


PROGRAMS = {
    'expand': 'alphaStochasticExpand',
    'current_prog': 'alphaStochasticCurrent',
    'resolve_prog': 'alphaStochasticResolve',
    'composite': 'alphaStochasticComposite',
}


def load_fixture(opengl):
    # Use the same GL state, targets and submission order as the stochastic regression and timing fixture.
    path = common.ROOT / 'scripts/perf/test_alpha_stochastic.py'
    blocks = re.findall(r'# <<bench-shared:begin>>\n(.*?)# <<bench-shared:end>>\n',
                        path.read_text(encoding='utf-8'), re.S)
    if len(blocks) != 3:
        raise RuntimeError('stochastic regression fixture does not have its three shared blocks')
    fixture = {'__file__': str(path), '__name__': 'stochastic_shared'}
    argv = sys.argv
    try:
        sys.argv = [str(path), '--opengl', opengl]
        exec(compile('\n'.join(blocks), str(path), 'exec'), fixture)
    finally:
        sys.argv = argv
    return fixture


def read_rgba(fixture, texture=None):
    width, height = fixture['W'], fixture['H']
    pixels = np.empty((height, width, 4), dtype=np.float32)
    if texture is None:
        fixture['readbuffer'](fixture['COLOR0'])
        fixture['readpixels'](0, 0, width, height, H.GL_RGBA, H.GL_FLOAT, pixels.ctypes.data)
    else:
        fixture['bind_unit'](11, fixture['TEX2D'], texture)
        get_image = fixture['fn']('glGetTexImage', None, C.c_uint, C.c_int, C.c_uint, C.c_uint, C.c_void_p)
        get_image(fixture['TEX2D'], 0, H.GL_RGBA, H.GL_FLOAT, pixels.ctypes.data)
    return pixels


def install_candidate_draws(fixture, gl, candidates):
    # The legacy test shaders use gl_VertexID. Generated vertex stages take the viewer's position attribute.
    triangle = np.array([[-1, -1, 0], [3, -1, 0], [-1, 3, 0]], dtype=np.float32)
    g = gl.fn
    vertex_buffer = C.c_uint()
    g('glGenBuffers', None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(vertex_buffer))
    g('glBindBuffer', None, C.c_uint, C.c_uint)(H.GL_ARRAY_BUFFER, vertex_buffer.value)
    g('glBufferData', None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)(
        H.GL_ARRAY_BUFFER, triangle.nbytes, triangle.ctypes.data, H.GL_STATIC_DRAW)
    g('glEnableVertexAttribArray', None, C.c_uint)(0)
    g('glVertexAttribPointer', None, C.c_uint, C.c_int, C.c_uint, C.c_ubyte, C.c_int, C.c_void_p)(
        0, 3, H.GL_FLOAT, 0, 0, None)

    legacy_draw = fixture['draw_post']
    generated_programs = set(candidates.values())

    def draw_post(program, **uniforms):
        if program not in generated_programs:
            legacy_draw(program, **uniforms)
            return
        missing = set(uniforms) - gl.set_uniforms(program, uniforms)
        if missing:
            raise RuntimeError(f'generated stochastic pass lacks uniforms: {sorted(missing)}')
        fixture['draw'](H.GL_TRIANGLES, 0, 3)

    fixture['draw_post'] = draw_post


def read_spread(fixture, depth_reader, program, samples):
    fixture['spread'](samples, program)
    fixture['bindfbo'](fixture['FB'], fixture['scene_fbo'])
    fixture['drawbuffer'](fixture['COLOR0'])
    fixture['disable'](fixture['DEPTH_TEST'])
    fixture['disable'](fixture['BLEND'])
    fixture['use'](depth_reader)
    fixture['bind_unit'](0, fixture['TEXMS'], fixture['targets'][samples][3])
    location = fixture['loc'](depth_reader, b'first_sample')
    lanes = []
    for first in range(0, samples, 4):
        fixture['uniform1i'](location, first)
        fixture['draw'](H.GL_TRIANGLES, 0, 3)
        lanes.append(read_rgba(fixture))
    return np.concatenate(lanes, axis=-1)


def render_frames(fixture, programs, samples, reprojection, temporal):
    fixture.update(programs)
    width, height = fixture['W'], fixture['H']
    layer = fixture['L']
    history = fixture['new_temporal'](reprojection) if temporal else None
    frames = []
    # Alternating clear, known coverage, moving coverage, disappearance and reappearance.
    for index in range(12 if temporal else 3):
        fixture['TEMPORAL_FRAME'][0] = index + 1
        fixture['set_opaque'](1.0, occluder=(3 * width // 4, 0.27))
        if index in (0, 9):
            layers = []
        else:
            layers = [layer(0.27, 0.38, (0.8, 0.12, 0.02), seed=21),
                      layer(0.53, 0.62, (0.08, 0.25, 0.91), seed=41, glow=0.39)]
        if index == 10:
            layers.append(layer(0.1, 0.4, (float('nan'), 0.3, 0.2), seed=77))
        inset = min(max(0, index - 5), 3)
        fixture['LAYER_SCISSOR'] = (2 + inset, width - 2, 2, height - 2)
        try:
            rgba, bits = fixture['stochastic'](layers, samples, samples, temporal=history, read_bits=True,
                                               scene_rgba=(0.12, 0.2, 0.32, 0.15))
        finally:
            fixture['LAYER_SCISSOR'] = None
        frame = {'scene and glow': np.asarray(rgba, dtype=np.float32),
                 'sample coverage': np.asarray(bits, dtype=np.float32)}
        if history is not None:
            aux = read_rgba(fixture, history['aux'])
            frame.update({'measured color': read_rgba(fixture, history['cur']),
                          'grain and total alpha': aux[..., :2],
                          'measured depths': aux[..., 2:],
                          'drawn mask': read_rgba(fixture, history['mask'])[..., 0],
                          'history': read_rgba(fixture, history['tex'][history['idx']]),
                          'history weights': read_rgba(fixture, history['extra'][history['idx']])[..., :2]})
        frames.append(frame)
    return frames


def qualify(fixture, gl, generated):
    programs = {entry['id']: entry for entry in check.load_manifest(generated)['programs']}
    expected = set(PROGRAMS.values())
    tasks = gen.load_program_list(common.SLANG_SRC)['tasks']
    family = {task['id'] for task in tasks if task['family'] == 'stochastic-transparency-resolve'}
    if family != expected or not family.issubset(programs):
        raise RuntimeError(f'stochastic fixture, program list and manifest disagree for {sorted(expected)}')
    legacy = {key: fixture[key] for key in PROGRAMS}
    candidates = {key: ab_cases.build_candidate(gl, generated, programs[name], {}) for key, name in PROGRAMS.items()}
    install_candidate_draws(fixture, gl, candidates)
    depth_reader = fixture['program']([(H.GL_VERTEX_SHADER, fixture['screen_vertex']), (H.GL_FRAGMENT_SHADER, '''
#version 420
layout(binding=0) uniform sampler2DMS depths;
uniform int first_sample;
out vec4 lanes;
void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    for (int lane = 0; lane < 4; ++lane)
        lanes[lane] = texelFetch(depths, px, first_sample + lane).r;
}
'''.lstrip())])
    results = []

    def compare(name, reference, candidate, mode='exact'):
        result = H.compare(reference, candidate, mode=mode)
        result['case'] = name
        result['pass'] &= bool(np.isfinite(reference).all() and np.isfinite(candidate).all())
        results.append(result)

    for samples in (4, 8):
        fixture['set_opaque'](0.65, occluder=(fixture['W'] // 2, 0.27), slope=0.14, slope_y=-0.07)
        reference = read_spread(fixture, depth_reader, legacy['expand'], samples)
        candidate = read_spread(fixture, depth_reader, candidates['expand'], samples)
        if not np.any(np.ptp(reference, axis=-1) > 0):
            raise RuntimeError('depth fixture did not produce any per-sample slopes')
        compare(f'{samples}× expanded sloped and occluded depth', reference, candidate)
        fixture['set_opaque'](1.0)
        reference = read_spread(fixture, depth_reader, legacy['expand'], samples)
        candidate = read_spread(fixture, depth_reader, candidates['expand'], samples)
        if not np.all(reference == 1.0):
            raise RuntimeError('far-plane depth did not stay at 1.0')
        compare(f'{samples}× clear far depth', reference, candidate)

    exact = {'sample coverage', 'measured depths', 'drawn mask'}
    treatments = [('4× direct', 4, (0.0, 0.0), False),
                  ('8× direct', 8, (0.0, 0.0), False),
                  ('8× static history', 8, (0.0, 0.0), True),
                  ('8× reprojected history', 8, (0.375, -0.25), True)]
    for label, samples, pan, temporal in treatments:
        reprojection = fixture['pan_reproj'](pan)
        reference = render_frames(fixture, legacy, samples, reprojection, temporal)
        candidate = render_frames(fixture, candidates, samples, reprojection, temporal)
        covered = reference[1]['sample coverage']
        if not (np.any(covered != 0) and np.any(covered == 0)):
            raise RuntimeError(f'{label}: missing transparent or opaque pixels')
        if not np.any(reference[1]['scene and glow'][..., 3] > 0.15):
            raise RuntimeError(f'{label}: the glow draw did not add to the scene alpha')
        if temporal:
            weights = reference[5]['history weights'][..., 1]
            mask = reference[5]['drawn mask'] > 0
            if not (np.any(weights[mask] < 0.99) and np.any(weights[~mask] == 1)):
                raise RuntimeError(f'{label}: did not exercise both history use and rejection')
            if not (np.any(reference[8]['drawn mask']) and not np.any(reference[9]['drawn mask'])):
                raise RuntimeError(f'{label}: moving transparency did not disappear')
        for index, (ref, cand) in enumerate(zip(reference, candidate)):
            for channel, values in ref.items():
                compare(f'{label} frame {index + 1}: {channel}', values, cand[channel],
                        'exact' if channel in exact else 'tolerance')
    error = fixture['geterror']()
    if error:
        raise RuntimeError(f'stochastic A/B OpenGL error {error:#x}')
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, default=common.GENERATED)
    parser.add_argument('--opengl', default='opengl32.dll')
    parser.add_argument('--expect-renderer')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    fixture = load_fixture(args.opengl)
    gl = H.GL(get_function=fixture['fn'])
    if args.expect_renderer and args.expect_renderer.casefold() not in gl.info['renderer'].casefold():
        parser.error(f'expected GPU containing {args.expect_renderer!r}, got {gl.info["renderer"]!r}')
    results = qualify(fixture, gl, args.generated)
    failures = [result for result in results if not result['pass']]
    for result in failures:
        print(json.dumps(result))
    print(f'{len(results)} stochastic transparency comparisons, {len(failures)} failed')
    if args.report:
        manifest = args.generated / 'programs.json'
        report = {'gpu': gl.info, 'manifest_sha256': hashlib.sha256(manifest.read_bytes()).hexdigest(),
                  'programs': list(PROGRAMS.values()), 'results': results}
        args.report.write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())

# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline GL check of stage S1's sun-shadow bias in world units, no viewer launch.

S1 (llshadowcascadefit.h, LLPipeline::generateSunShadow): each cascade is the bounding sphere
of its frustum slice, an orthographic box on a square map (1456 x 1456 at 1920x1080), depth
range = the sphere plus one radius towards the light (3 r), and the bias is
RenderShadowBiasTexels (3) texels of the cascade in world units, baked into the cascade's
shadow matrix as a z offset that cancels the shader's own bias uniform (devel's, still with its
altitude term: -0.002 - 0.007 z / 3000). The first S1 build lacked that offset, so the bias in
metres followed 3 r and the altitude, and shadows went partial in skyboxes.

With the viewer's own shaders (class2 deferred/sunLightF.glsl + class1 deferred/shadowUtil.glsl
of this tree: the stock PCF with PCSS off, and PCSS on) and bench_shadow_pcss.py's scene
machinery (ray-cast exact geometry), the whole scene placed at 0 / 1000 / 2000 / 3000 / 4000 m
(ground, boxes, camera and light view at that height), shadow distance 128 m:
  * acne on a flat floor under a 5 / 10 / 20 degree sun and on a wall lit 75 degrees off its
    normal by a 10 degree sun (above its 6 cm contact line with the ground): the fraction of lit
    pixels darker than fully lit must be 0, PCF and PCSS, at every altitude, with the normal
    offset (RenderShadowNormalOffsetTexels, 3) and the world-unit bias;
  * contact: the lit gap between a 10 cm wall's base and its shadow must not depend on the
    altitude, against the first build's bias for comparison.
Exit status 0 when all checks pass. The GL window is the hidden one of gl_test_context.py.
"""
import math, sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
exec(compile((HERE / 'bench_shadow_pcss.py').read_text().split('\nfailures = []')[0], str(HERE / 'bench_shadow_pcss.py'), 'exec'))

W, H = 1920, 1080
SQUARE = (int(math.sqrt(W * H)) + 16) & ~0xF            # S1's square sun maps
def blur_happy(x): return SQUARE                          # the bench sizes its maps with this
BIAS_TEXELS = 3.0
import os
NORMAL_OFFSET = float(os.environ.get('S1_NORMAL_OFFSET', '3.0'))   # RenderShadowNormalOffsetTexels
ALTITUDES = (0.0, 1000.0, 2000.0, 3000.0, 4000.0)
def uniform_bias(z): return -0.002 - 0.007 * abs(z) / 3000.0   # devel's bindDeferredShader
NEAR = 0.5
SPLITS = [NEAR + (128.0 - NEAR) * (i / 4.0) ** 3.0 for i in range(1, 5)]
SPLITS[0] *= 1.25
CLIP = tuple(SPLITS)
ALT = [0.0]
MODE = ['s1']    # 's1' (bias in world units) or 'first' (the first build: no offset)

def scene_glsl(boxes):
    return '''
const int BOX_COUNT = %d;
const vec3 box_min[BOX_COUNT] = vec3[BOX_COUNT](%s);
const vec3 box_max[BOX_COUNT] = vec3[BOX_COUNT](%s);
const float GROUND_Y = %r;
bool trace(vec3 o, vec3 d, float tmax, out float t_hit, out vec3 n_hit)
{
    t_hit = tmax; n_hit = vec3(0.0); bool hit = false;
    if (d.y < 0.0) { float t = (GROUND_Y - o.y) / d.y; if (t >= 0.0 && t < t_hit) { t_hit = t; n_hit = vec3(0, 1, 0); hit = true; } }
    for (int i = 0; i < BOX_COUNT; ++i)
    {
        vec3 inv = 1.0 / d;
        vec3 t0 = (box_min[i] - o) * inv, t1 = (box_max[i] - o) * inv;
        vec3 tn = min(t0, t1), tf = max(t0, t1);
        float a = max(max(tn.x, tn.y), tn.z), b = min(min(tf.x, tf.y), tf.z);
        if (a <= b && a >= 0.0 && a < t_hit)
        {
            t_hit = a; hit = true;
            n_hit = (a == tn.x) ? vec3(-sign(d.x), 0, 0) : (a == tn.y) ? vec3(0, -sign(d.y), 0) : vec3(0, 0, -sign(d.z));
        }
    }
    return hit;
}
''' % (len(boxes), ', '.join('vec3(%r, %r, %r)' % tuple(b[0]) for b in boxes),
       ', '.join('vec3(%r, %r, %r)' % tuple(b[1]) for b in boxes), ALT[0])

def f32(a): return np.asarray(a, dtype=np.float32)

class S1Scene(Scene):
    def cascade_matrices(self):
        aspect = self.W / self.H
        k = math.tan(self.fovy / 2) * math.sqrt(1 + aspect * aspect)
        eye = mvec(self.inv_view, [0.0, 0.0, 0.0, 1.0])[:3]
        at = normalize([-self.view[2][0], -self.view[2][1], -self.view[2][2]])
        upv = [1.0, 0.0, 0.0] if abs(self.sun[2]) > 0.9 else [0.0, 0.0, 1.0]   # world-locked (bench: y up)
        lv = look_at(eye, [eye[i] - self.sun[i] for i in range(3)], upv)
        dist = [NEAR] + list(CLIP)
        mats = []; self.parts = []
        for j in range(4):
            n, f = dist[j] * 0.75, dist[j + 1] * 1.25
            kk = 1 + k * k
            cd = min(0.5 * (n + f) * kk, f)
            r = math.sqrt((f - cd) ** 2 + (f * k) ** 2)
            octave = 2.0 ** math.floor(math.log2(r)); r = math.ceil(r / (octave / 16)) * (octave / 16)
            c = mvec(lv, [eye[i] + at[i] * cd for i in range(3)] + [1.0])
            o = ortho(c[0] - r, c[0] + r, c[1] - r, c[1] + r, -(c[2] + 2 * r), -(c[2] - r))
            texel = 2 * r / SQUARE
            self.parts.append((o, lv, texel, 3 * r))
            mats.append(mmul(o, lv))
        return mats

    def finish(self, z):
        inv = np.linalg.inv(f32(self.view)).astype(np.float32)
        sm = []
        for o, lv, texel, rng in self.parts:
            m = (f32(TEXSPACE) @ f32(o) @ f32(lv) @ inv).astype(np.float32)
            if MODE[0] == 's1':
                t = np.eye(4, dtype=np.float32)
                t[2, 3] = 2 * abs(uniform_bias(z)) - BIAS_TEXELS * texel / rng   # depthBiasOffset
                m = (t @ m).astype(np.float32)
            sm.append(m.tolist())
        self.shadow_matrix = sm + self.shadow_matrix[4:]

def up(p): return [p[0], p[1] + ALT[0], p[2]]
DUMMY = ((500.0, -5.0, 500.0), (501.0, -4.0, 501.0))
SPOTS = [([0.0, 6.0, -4.0], [0.0, 0.0, -4.0])] * 2

def make(boxes, eye, target, sun):
    sc = S1Scene(W, H, [(tuple(up(b[0])), tuple(up(b[1]))) for b in boxes], up(eye), up(target), sun,
                 [(up(p), up(t)) for p, t in SPOTS], CLIP)
    sc.finish(ALT[0])
    return sc

def gbuffer(sc):
    bindfbo(FB, sc.gfbo); out = []
    for k in range(2):
        readbuffer(COLOR0 + k)
        buf = (F*(W*H*4))(); readpixels(0, 0, W, H, 0x1908, 0x1406, C.byref(buf))
        out.append(np.frombuffer(buf, dtype=np.float32).reshape(H, W, 4).copy())
    return out

def light_map(sc, prog, z):
    sc.bind(prog, LIGHT_SIZE); u1f(loc(prog, b'shadow_bias'), uniform_bias(z))
    texels = [part[2] for part in sc.parts]
    u4f(loc(prog, b'shadow_texel'), *texels); u1f(loc(prog, b'shadow_normal_offset'), NORMAL_OFFSET)
    draw(4, 0, 3); finish()
    img = np.frombuffer(sc.read_channel(0x1903), dtype=np.float32).reshape(H, W).copy()
    sc.unbind(); return img

def sun_from(elev, azim):
    e, a = math.radians(elev), math.radians(azim)
    return [math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a)]

PROGS = {}
def prog(v):
    if v not in PROGS: PROGS[v] = sun_program(v, False)
    return PROGS[v]

fails = []
def ok(cond, msg):
    print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
    if not cond: fails.append(msg)

def run(z):
    ALT[0] = z; res = {}
    for elev in (5.0, 10.0, 20.0):
        sc = make([DUMMY], [0.0, 1.7, 0.0], [0.0, 0.0, -12.0], sun_from(elev, 40.0))
        pos, nrm = gbuffer(sc); floor = (nrm[:, :, 1] > 0.99) & (pos[:, :, 3] > 0.5)
        for v in ('tree', 'pcss'):
            res[('floor', elev, v)] = float(np.mean(light_map(sc, prog(v), z)[floor] < 0.999)) * 100
        sc.release()
    sc = make([((-1.0, 0.0, -9.0), (1.0, 2.0, -7.0))], [5.0, 1.5, -2.0], [0.0, 1.0, -8.0], sun_from(10.0, 15.0))
    pos, nrm = gbuffer(sc)
    rv = np.array(sc.view, dtype=np.float32)[:3, :3]; nw = nrm[:, :, :3] @ rv
    sv = np.array(sc.sun_v, dtype=np.float32); ndl = nrm[:, :, :3] @ sv
    walls = (np.abs(nw[:, :, 1]) < 0.01) & (np.abs(nw[:, :, 0]) > 0.99) & (pos[:, :, 3] > 0.5) & (ndl > 0.05)
    # world height of each pixel: the 6 cm next to the ground is the contact line (the filter's
    # footprint reaches the ground's shadow there), not the face
    inv = np.linalg.inv(np.array(sc.view, dtype=np.float64))
    height = (pos[:, :, :3].astype(np.float64) @ inv[:3, :3].T + inv[:3, 3])[:, :, 1] - ALT[0]
    walls = walls & (height > 0.06)
    assert walls.sum() > 1000
    for v in ('tree', 'pcss'):
        img = light_map(sc, prog(v), z)[walls]
        res[('wall', 10.0, v)] = float(np.mean(img < 0.999)) * 100
        res[('wallmin', 10.0, v)] = float(img.min())
    sc.release()
    for mode in ('s1', 'first'):
        MODE[0] = mode
        sc = make([((-3.0, 0.0, -6.6), (3.0, 2.0, -6.5))], [0.8, 1.6, -9.5], [0.0, 0.0, -6.7], sun_from(20.0, 0.0))
        pix = []
        for k in range(1, 3000):
            try: pix.append(sc.project(up((0.0, 0.0, -6.6 - 0.001 * k))))
            except AssertionError: pix.append(None)
        for v in ('tree', 'pcss'):
            img = light_map(sc, prog(v), z); gap = float('inf')
            for k, xy in enumerate(pix):
                if xy is not None and img[xy[1], xy[0]] < 0.5:
                    gap = 0.1 * (k + 1); break
            res[('contact', mode, v)] = gap
        sc.release()
    MODE[0] = 's1'
    return res


def util_program(util_text, variant):
    """sun_program with a given shadowUtil.glsl text (to compare a committed version with this tree)."""
    sun = (root / SUN_F).read_text()
    local = VARIANT_DEFINES[variant]
    return program([(0x8B31, VERTEX), (0x8B30, stage(sun, local)), (0x8B30, stage(util_text, ['SUN_SHADOW'] + local)), (0x8B30, STUBS_F)])


def temporal(util_text, variant):
    """Fixed ground points across a 10 cm wall's shadow edge, 12-24 m from the camera (the
    cascade 1-2 transition at 128 m): the camera backs away 2 cm per frame, nothing else moves.
    Returns the largest frame-to-frame change of any point's shadow value."""
    ALT[0] = 0.0
    prog_t = util_program(util_text, variant)
    wall = ((-6.0, 0.0, -20.1), (6.0, 1.5, -20.0))
    sun = sun_from(25.0, 0.0)
    # the shadow edge on the ground lies 1.5 / tan(25 deg) = 3.2 m behind the wall (towards -z)
    pts = [(x, 0.0, -20.1 - 3.2 + dz) for x in (-2.0, 0.0, 2.0) for dz in (-0.3, -0.15, 0.0, 0.15, 0.3)]
    prev = None; worst = 0.0
    for f in range(0, 400):
        eye = [1.0, 2.0, -12.0 + 0.02 * f]
        sc = make([wall], eye, [0.0, 0.0, -23.0 + 0.02 * f], sun)
        img = light_map(sc, prog_t, 0.0)
        vals = []
        for p in pts:
            try:
                x, y = sc.project(up(p))
                vals.append(img[y, x])
            except AssertionError:
                vals.append(None)
        sc.release()
        if prev is not None:
            for a, b in zip(prev, vals):
                if a is not None and b is not None:
                    worst = max(worst, abs(float(a) - float(b)))
        prev = vals
    return worst


def main():
    print('GL:', get_string(0x1F01).decode(), flush=True)
    print(f'S1 splits at 128 m: {", ".join(f"{c:.1f}" for c in CLIP)}; maps {SQUARE}x{SQUARE}; bias {BIAS_TEXELS:g} texels\n')
    results = {z: run(z) for z in ALTITUDES}
    g = results[0.0]
    for z, r in results.items():
        for key in sorted(k for k in r if k[0] in ('floor', 'wall')):
            name = 'PCF ' if key[2] == 'tree' else 'PCSS'
            line = f'acne {key[0]:5s} sun {key[1]:4.0f} deg {name} at {z:6.0f} m: {r[key]:.3f} % (ground {g[key]:.3f} %)'
            if key[0] == 'wall':
                line += f', darkest {r[("wallmin", 10.0, key[2])]:.3f}'
            ok(r[key] < 0.01, line)   # under 0.01 %: single pixels from float32 rounding at 3000-4000 m
        for v in ('tree', 'pcss'):
            name = 'PCF ' if v == 'tree' else 'PCSS'
            s1, first, ground = r[('contact', 's1', v)], r[('contact', 'first', v)], g[('contact', 's1', v)]
            ok(abs(s1 - ground) <= 2.0, f'contact {name} at {z:6.0f} m: gap {s1:.1f} cm (ground {ground:.1f} cm); first S1 build {first:.1f} cm')
    import subprocess
    committed = subprocess.run(['git', 'show', 'HEAD:indra/newview/app_settings/shaders/class1/deferred/shadowUtil.glsl'],
                               cwd=root, capture_output=True, text=True).stdout
    current = (root / UTIL_F).read_text()
    for v in ('tree', 'pcss'):
        name = 'PCF ' if v == 'tree' else 'PCSS'
        step = temporal(committed, v) if 'shadow_texel' in committed else None
        ramp = temporal(current, v)
        line = f'temporal {name}: largest frame-to-frame change at fixed ground points, camera backing away 2 cm/frame: this tree {ramp:.3f}'
        if step is not None:
            line += f'; HEAD (stepped normal offset) {step:.3f}'
        ok(ramp < 0.25, line)
    print(f'\n{"all checks passed" if not fails else str(len(fails)) + " check(s) failed"}')
    return 1 if fails else 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline check of the sun-shadow bias without its altitude term (hotfix), no viewer launch.

LLPipeline::bindDeferredShader set
    shadow_bias = RenderShadowBias + RenderShadowBiasError * |camera z| / 3000
(-0.002 - 0.007 z / 3000), a constant in each cascade's [0,1] depth, so 4.4x wider at 3000 m.
It came from MAINT-653 "Fix for shadow acne at high altitudes" (Linden Lab, 2012-06-22,
28ccf4190e), in the pre-PBR renderer (rectangle shadow maps, another matrix chain). Today the
sun shadow views are anchored at the camera (LLPipeline::generateSunShadow, look(camera
origin, ...)), so the remaining altitude dependence is float32 rounding of large coordinates.

Part 1 (numpy, float32 as the viewer computes it): the precision loss the altitude causes.
  The receiver path: a vertex in agent space (float32) -> main view matrix (float32) -> view
  space -> mSunShadowMatrix = trans * proj * view_light * inverse(view) (glm, float32) -> depth.
  The caster path: the same vertex -> proj * view_light (float32) -> depth. The difference of
  the two depths, in metres along the light, is the self-shadowing error the bias must cover.
  Reported at 0, 25, 500, 1000, 2000, 3000 and 4000 m against the bias in metres.

Part 2 (GL, the viewer's class2 deferred/sunLightF.glsl + class1 deferred/shadowUtil.glsl, PCSS
off and on, scene machinery of bench_shadow_pcss.py): the whole scene is placed at each
altitude (ground plane, boxes, camera and light view at height z, so every shader computes with
the real magnitudes; the shadow matrices are composed in float32), cascades fitted like the
viewer's (orthographic box around the receivers, no depth margin, light view at the camera):
  * acne on a flat floor under a low sun (5, 10, 20 degrees) and on a box's walls lit at a
    grazing angle (sun 10 degrees up, 75 degrees off the wall's normal): lit pixels darker than
    fully lit, with the new bias, at every altitude: must be 0;
  * contact: a thin wall (10 cm) on the ground, sun 20 degrees: the lit gap between the wall's
    base and its shadow, near the camera (cascade 0). With the new bias it must be the same at
    every altitude (within one shadow texel) as at the ground, i.e. attached; the old bias is
    printed beside it.

Exit status 0 when all checks pass. The GL window is the hidden one of gl_test_context.py.
"""
import math, sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
bench_src = (HERE / 'bench_shadow_pcss.py').read_text()
bench_src = bench_src.split('\nfailures = []')[0]   # the bench's definitions only, not its run
exec(compile(bench_src, str(HERE / 'bench_shadow_pcss.py'), 'exec'))

W, H = 1920, 1080
BIAS = -0.002            # RenderShadowBias
BIAS_ERROR = -0.007      # RenderShadowBiasError (the dropped term)
ALTITUDES = (0.0, 25.0, 500.0, 1000.0, 2000.0, 3000.0, 4000.0)
def old_bias(z): return BIAS + BIAS_ERROR * abs(z) / 3000.0
NEW_BIAS = BIAS

fails = []
def ok(cond, msg):
    print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
    if not cond: fails.append(msg)

# ------------------------------------------------------------------ part 1: float32 analysis
def f32(a): return np.asarray(a, dtype=np.float32)

def look_f32(pos, d, up):
    """pipeline.cpp look(): rows lft, up, -dir; translation -(lft.pos), -(up.pos), dir.pos (float32)."""
    pos, d, up = f32(pos), f32(d), f32(up)
    lft = np.cross(d, up); lft = lft / np.linalg.norm(lft)
    upn = np.cross(lft, d); upn = upn / np.linalg.norm(upn)
    dn = d / np.linalg.norm(d)
    m = np.eye(4, dtype=np.float32)
    m[0, :3], m[1, :3], m[2, :3] = lft, upn, -dn
    m[0, 3], m[1, 3], m[2, 3] = -np.dot(lft, pos), -np.dot(upn, pos), np.dot(dn, pos)
    return m.astype(np.float32)

def inverse_f32(m):
    """glm::inverse in float32 (cofactors); np.linalg.inv on float32 input computes in float32."""
    return np.linalg.inv(m.astype(np.float32)).astype(np.float32)

def ortho_f32(l, r, b, t, n, f):
    m = np.zeros((4, 4), dtype=np.float32)
    m[0, 0] = 2 / (r - l); m[1, 1] = 2 / (t - b); m[2, 2] = -2 / (f - n)
    m[0, 3] = -(r + l) / (r - l); m[1, 3] = -(t + b) / (t - b); m[2, 3] = -(f + n) / (f - n); m[3, 3] = 1
    return m

def precision_loss(z, rng, count=20000, extent=30.0, depth_range=20.0):
    """Max and 99.9th percentile of |receiver depth - caster depth| in metres for vertices around a
    camera at altitude z (agent coordinates x, y within the region, z = altitude)."""
    eye = np.array([128.3, 97.6, z + 1.7])
    yaw = rng.uniform(0, 2 * np.pi); pitch = -0.3
    at = np.array([np.cos(pitch) * np.cos(yaw), np.cos(pitch) * np.sin(yaw), np.sin(pitch)])
    left = np.cross([0, 0, 1], at); left /= np.linalg.norm(left); up = np.cross(at, left)
    view = np.eye(4, dtype=np.float32)
    view[0, :3], view[1, :3], view[2, :3] = f32(-left), f32(up), f32(-at)
    view[:3, 3] = -(view[:3, :3] @ f32(eye))
    light = np.array([np.cos(0.35) * np.cos(3.5), np.cos(0.35) * np.sin(3.5), -np.sin(0.35)])  # down
    lview = look_f32(eye, light, -at)
    proj = ortho_f32(-extent, extent, -extent, extent, -depth_range / 2, depth_range / 2)
    trans = f32([[0.5, 0, 0, 0.5], [0, 0.5, 0, 0.5], [0, 0, 0.5, 0.5], [0, 0, 0, 1]])
    shadow = (trans @ proj @ lview @ inverse_f32(view)).astype(np.float32)   # mSunShadowMatrix
    caster = (proj @ lview).astype(np.float32)                                # the shadow pass MVP
    pts = eye + rng.uniform(-extent * 0.8, extent * 0.8, size=(count, 3)) * np.array([1, 1, 0.1])
    v = np.concatenate([f32(pts), np.ones((count, 1), np.float32)], axis=1)  # vertex buffer: float32
    p_view = (v @ view.T).astype(np.float32)                                  # main pass (receiver)
    d_rec = (p_view @ shadow.T)[:, 2]
    d_cas = (v @ caster.T)[:, 2] * np.float32(0.5) + np.float32(0.5)
    err_m = np.abs(d_rec.astype(np.float64) - d_cas.astype(np.float64)) * depth_range
    return float(err_m.max()), float(np.percentile(err_m, 99.9))

def part1():
    print('Part 1: float32 precision loss of the receiver vs caster depth (metres along the light)')
    print('  altitude   max error   99.9 %      | new bias, cascade 0: skybox room 0.62 cm, outdoors 4.31 cm')
    rng = np.random.default_rng(653)
    worst = {}
    for z in ALTITUDES:
        mx, p = 0.0, 0.0
        for _ in range(8):
            a, b = precision_loss(z, rng)
            mx, p = max(mx, a), max(p, b)
        worst[z] = mx
        print(f'  {z:6.0f} m   {mx * 1000:7.3f} mm  {p * 1000:7.3f} mm')
    # the smallest bias in metres the viewer applies: cascade 0 of a skybox room (1.6 m deep): 0.62 cm
    smallest_bias_m = 2 * abs(NEW_BIAS) * 1.55
    ok(worst[4000.0] < 0.25 * smallest_bias_m,
       f'precision loss at 4000 m ({worst[4000.0] * 1000:.2f} mm) is under a quarter of the smallest bias in metres ({smallest_bias_m * 1000:.1f} mm): no compensation needed')
    return worst

# ------------------------------------------------------------------ part 2: GL at altitude
ALT = [0.0]

def scene_glsl(boxes):   # replaces the bench's: the ground plane at y = ALT
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

def up(p): return [p[0], p[1] + ALT[0], p[2]]
def up_box(b): return (tuple(up(b[0])), tuple(up(b[1])))
DUMMY = ((500.0, -5.0, 500.0), (501.0, -4.0, 501.0))   # GLSL needs one box: buried far away
SPOTS = [([0.0, 6.0, -4.0], [0.0, 0.0, -4.0])] * 2
CLIP_OUTDOOR = (5.3, 32.2, 108.1, 256.0)
CLIP_ROOM = (0.7, 2.6, 8.2, 19.1)

class FittedScene(Scene):
    """Cascades as LLPipeline::generateSunShadow's ortho fit: light view at the camera, the
    light-space box of the receivers of each frustum section (clipped to the slab
    ALT <= y <= ALT + top), no depth margin; shadow matrices composed in float32."""
    TOP = 0.0
    def cascade_matrices(self):
        aspect = self.W / self.H; th = math.tan(self.fovy / 2)
        eye = mvec(self.inv_view, [0.0, 0.0, 0.0, 1.0])[:3]
        upv = [0.0, 0.0, 1.0] if abs(self.sun[1]) > 0.99 else [0.0, 1.0, 0.0]
        light_view = look_at(eye, [eye[i] - self.sun[i] for i in range(3)], upv)
        g = ALT[0]
        mats = []; self.depth_ranges = []; self.parts = []
        for j in range(4):
            n = 0.5 if j == 0 else self.clip[j-1] * 0.75; f = self.clip[j] * 1.25
            pts = []
            for sx in (-1, 1):
                for sy in (-1, 1):
                    a = mvec(self.inv_view, [sx*n*th*aspect, sy*n*th, -n, 1.0])[:3]
                    b = mvec(self.inv_view, [sx*f*th*aspect, sy*f*th, -f, 1.0])[:3]
                    for t in [k / 16.0 for k in range(17)]:
                        p = [a[i] + (b[i] - a[i]) * t for i in range(3)]
                        if p[1] < g:
                            d = [p[i] - eye[i] for i in range(3)]
                            if d[1] >= -1e-6:
                                continue
                            s = (g - eye[1]) / d[1]; p = [eye[i] + d[i] * s for i in range(3)]
                        p[1] = min(p[1], g + self.TOP)
                        pts.append(p)
            ls = [mvec(light_view, p + [1.0]) for p in pts]
            lo = [min(p[i] for p in ls) for i in range(3)]; hi = [max(p[i] for p in ls) for i in range(3)]
            hi[2] += 0.05; lo[2] -= 0.05
            self.depth_ranges.append(hi[2] - lo[2])
            o = ortho(lo[0], hi[0], lo[1], hi[1], -hi[2], -lo[2])
            self.parts.append((o, light_view))
            mats.append(mmul(o, light_view))
        return mats

    def finish(self):
        """mSunShadowMatrix = trans * proj * view_light * inverse(view), in float32 like glm."""
        inv = inverse_f32(f32(self.view))
        sm = []
        for o, lv in self.parts:
            m = (f32(TEXSPACE) @ f32(o) @ f32(lv) @ inv).astype(np.float32)
            sm.append(m.tolist())
        self.shadow_matrix = sm + self.shadow_matrix[4:]

def make(boxes, eye, target, sun, clip, top=0.0):
    FittedScene.TOP = top
    sc = FittedScene(W, H, [up_box(b) for b in boxes], up(eye), up(target), sun, [(up(p), up(t)) for p, t in SPOTS], clip)
    FittedScene.TOP = 0.0
    sc.finish()
    return sc

def gbuffer(sc):
    bindfbo(FB, sc.gfbo)
    out = []
    for k in range(2):
        readbuffer(COLOR0 + k)
        buf = (F*(W*H*4))(); readpixels(0, 0, W, H, 0x1908, 0x1406, C.byref(buf))
        out.append(np.frombuffer(buf, dtype=np.float32).reshape(H, W, 4).copy())
    return out

def light_map(sc, prog, bias):
    sc.bind(prog, LIGHT_SIZE); u1f(loc(prog, b'shadow_bias'), bias)
    draw(4, 0, 3); finish()
    img = np.frombuffer(sc.read_channel(0x1903), dtype=np.float32).reshape(H, W).copy()
    sc.unbind()
    return img

def sun_from(elev, azim):
    e, a = math.radians(elev), math.radians(azim)
    return [math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a)]

PROGS = {}
# base: stock pcfShadow (git BASE_REF of bench_shadow_pcss.py), i.e. the term removed alone;
# tree: this tree's pcfShadow (with the receiver-plane bias); pcss: PCSS on
VNAME = {'base': 'stock PCF        ', 'tree': 'PCF + plane bias  ', 'pcss': 'PCSS             '}
def prog(variant):
    if variant not in PROGS:
        PROGS[variant] = sun_program(variant, False)
    return PROGS[variant]

def acne_floor(z, rows):
    for elev in (5.0, 10.0, 20.0):
        for label, clip in (('outdoor', CLIP_OUTDOOR), ('skybox room', CLIP_ROOM)):
            sc = make([DUMMY], [0.0, 1.7, 0.0], [0.0, 0.0, -12.0], sun_from(elev, 40.0), clip)
            pos, nrm = gbuffer(sc)
            floor = (nrm[:, :, 1] > 0.99) & (pos[:, :, 3] > 0.5)
            for variant in ('base', 'tree', 'pcss'):
                new = float(np.mean(light_map(sc, prog(variant), NEW_BIAS)[floor] < 0.999)) * 100
                old = float(np.mean(light_map(sc, prog(variant), old_bias(z))[floor] < 0.999)) * 100
                rows.append(('floor', z, elev, label, variant, new, old))
            sc.release()

FOOT_BAND_MAX = 5.0   # %: measured 2.3 to 4.3 % of the foot band at 0 to 4000 m (the crease limitation, see doc/shadows/shadow-method.md)

def acne_wall(z, rows):
    # a 2 m box; its +x wall lit 75 degrees off its normal by a 10 degree sun
    box = ((-1.0, 0.0, -9.0), (1.0, 2.0, -7.0))
    sun = sun_from(10.0, 90.0 - 75.0)   # azimuth from +z towards +x: 15 deg -> 75 deg off the +x normal
    for label, clip in (('outdoor', CLIP_OUTDOOR), ('skybox room', CLIP_ROOM)):
        sc = make([box], [5.0, 1.5, -2.0], [0.0, 1.0, -8.0], sun, clip, top=2.0)
        pos, nrm = gbuffer(sc)
        sv = np.array(sc.sun_v, dtype=np.float32)
        ndl = nrm[:, :, 0] * sv[0] + nrm[:, :, 1] * sv[1] + nrm[:, :, 2] * sv[2]
        rv = np.array(sc.view, dtype=np.float32)[:3, :3]
        nw = nrm[:, :, :3] @ rv                       # world normal = R^T n_view
        walls = (np.abs(nw[:, :, 1]) < 0.01) & (np.abs(nw[:, :, 0]) > 0.99) & (pos[:, :, 3] > 0.5) & (ndl > 0.05)
        assert walls.sum() > 1000, f'wall mask too small: {walls.sum()}'
        # PCSS (stage S2): the receiver plane is the wall's and does not continue across the crease where the
        # wall meets the ground. The band of pixels within 3 cm of the ground is counted apart ('foot') with
        # its own bound, FOOT_BAND_MAX; the rest of the wall must be free of acne like the other methods' walls
        iv = np.array(sc.inv_view, dtype=np.float32)
        wall_y = pos[:, :, 0] * iv[1, 0] + pos[:, :, 1] * iv[1, 1] + pos[:, :, 2] * iv[1, 2] + iv[1, 3] - ALT[0]
        for variant in ('base', 'tree', 'pcss'):
            mask = walls & (wall_y > 0.03) if variant == 'pcss' else walls
            img_new = light_map(sc, prog(variant), NEW_BIAS)[mask]
            img_old = light_map(sc, prog(variant), old_bias(z))[mask]
            new = float(np.mean(img_new < 0.999)) * 100
            old = float(np.mean(img_old < 0.999)) * 100
            print(f'    wall {z:6.0f} m {label:11s} {variant:4s}: new mean {img_new.mean():.3f}, <0.5 {np.mean(img_new < 0.5) * 100:.2f} %, min {img_new.min():.3f} | '
                  f'old mean {img_old.mean():.3f}, <0.5 {np.mean(img_old < 0.5) * 100:.2f} %', flush=True)
            rows.append(('wall', z, 10.0, label, variant, new, old))
            if variant == 'pcss':
                band = walls & (wall_y <= 0.03)
                assert band.sum() > 10, f'wall foot band too small: {band.sum()}'
                rows.append(('foot', z, 10.0, label, variant,
                             float(np.mean(light_map(sc, prog(variant), NEW_BIAS)[band] < 0.999)) * 100,
                             float(np.mean(light_map(sc, prog(variant), old_bias(z))[band] < 0.999)) * 100))
        sc.release()

def contact(z, results):
    # thin wall 6 m wide, 2 m tall, 10 cm thick; sun 20 degrees from +z; camera on the shadow side
    wall = ((-3.0, 0.0, -6.6), (3.0, 2.0, -6.5))
    sun = sun_from(20.0, 0.0)
    for label, clip in (('outdoor', CLIP_OUTDOOR), ('skybox room', CLIP_ROOM)):
        sc = make([wall], [0.8, 1.6, -9.5], [0.0, 0.0, -6.7], sun, clip, top=2.0)
        pix = []
        for k in range(1, 600):
            p = up((0.0, 0.0, -6.6 - 0.001 * k))
            try:
                pix.append(sc.project(p))
            except AssertionError:
                pix.append(None)
        texel_cm = None
        for variant in ('base', 'tree', 'pcss'):
            gaps = {}
            for name, b in (('new', NEW_BIAS), ('old', old_bias(z))):
                img = light_map(sc, prog(variant), b)
                gap = float('inf')
                for k, xy in enumerate(pix):
                    if xy is not None and img[xy[1], xy[0]] < 0.5:
                        gap = 0.1 * (k + 1)
                        break
                gaps[name] = gap
            results[(z, label, variant)] = gaps
        sc.release()

def part2():
    print('\nPart 2: GL, the scene at each altitude (viewer shaders; PCF = PCSS off)')
    rows = []; contacts = {}
    for z in ALTITUDES:
        ALT[0] = z
        acne_floor(z, rows); acne_wall(z, rows); contact(z, contacts)
    print('\n  acne: % of lit pixels darker than fully lit (new bias | old bias)')
    for kind, z, elev, label, variant, new, old in rows:
        name = VNAME[variant]
        line = f'{kind:5s} {z:6.0f} m  sun {elev:4.0f} deg  {label:11s} {name}: new {new:.3f} %   (old {old:.3f} %)'
        if variant == 'base':
            print('  info ' + line)
        elif kind == 'foot':
            ok(new <= FOOT_BAND_MAX, line + f'   (the crease limit: at most {FOOT_BAND_MAX} %)')
        else:
            ok(new == 0.0, line)
    print('\n  contact: lit gap between a 10 cm wall and its shadow, cascade-0 distance (new bias | old bias)')
    for (z, label, variant), g in contacts.items():
        ground = contacts[(0.0, label, variant)]['new']
        ok(abs(g['new'] - ground) <= 1.0,
           f'{z:6.0f} m  {label:11s} {VNAME[variant]}: new {g["new"]:.1f} cm (ground {ground:.1f} cm)   old {g["old"]:.1f} cm')

def main():
    print('GL:', get_string(0x1F01).decode(), flush=True)
    print(f'bias: old -0.002 - 0.007 z/3000 (x{old_bias(3000.0) / old_bias(0.0):.2f} at 3000 m); new -0.002 at every altitude\n')
    part1()
    part2()
    print(f'\n{"all checks passed" if not fails else str(len(fails)) + " check(s) failed"}')
    return 1 if fails else 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        gl.wglMakeCurrent(None, None); gl.wglDeleteContext(context); user.ReleaseDC(window, hdc); user.DestroyWindow(window)

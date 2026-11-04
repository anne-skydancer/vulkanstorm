"""Check the PCSS kernel of shadowUtil.glsl: its Poisson disk, edge clamp and receiver-plane
gradient (stage 1), and the blocker search, penumbra and projector depth linearisation of the
contact hardening (stage 2), through Python mirrors of the GLSL functions that use the
shader's own constants. The GPU behaviour itself is measured by
scripts/perf/bench_shadow_pcss.py."""
from pathlib import Path
import importlib.util
import math
import random
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHADER = (ROOT / 'indra/newview/app_settings/shaders/class1/deferred/shadowUtil.glsl').read_text()


def pcss_block():
    begin = SHADER.index('#if defined(PCSS_SHADOW)')
    return SHADER[begin:SHADER.index('// </Vulkanstorm>', begin)]


def define(name):
    return float(re.search(r'^#define %s\s+([-0-9.eE]+)' % name, pcss_block(), re.M).group(1))


def shader_disk():
    body = re.search(r'pcss_poisson12\[12\] = vec2\[12\]\((.*?)\);', pcss_block(), re.S).group(1)
    return [(float(x), float(y)) for x, y in re.findall(r'vec2\(\s*([-0-9.]+),\s*([-0-9.]+)\)', body)]


def generator():
    spec = importlib.util.spec_from_file_location('gen_poisson_disk', ROOT / 'scripts/perf/gen_poisson_disk.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# --- mirrors of the GLSL ---------------------------------------------------------------
def edge_scale(uv, extent, texel):
    """pcssEdgeScale"""
    room = [min(uv[i], 1.0 - uv[i]) - 0.5 * texel[i] for i in range(2)]
    return min(max(min(room[0] / extent[0], room[1] / extent[1]), 0.0), 1.0)


def mat_vec(m, v):
    return [sum(m[i][k] * v[k] for k in range(4)) for i in range(4)]


def project(m, p):
    c = mat_vec(m, list(p) + [1.0])
    return [c[i] / c[3] for i in range(3)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def normalize(a):
    length = math.sqrt(sum(x * x for x in a))
    return [x / length for x in a]


def depth_gradient(m, pos, norm):
    """pcssDepthGradient (c = the projected position)"""
    step, grad_max = define('PCSS_PLANE_STEP'), define('PCSS_GRAD_MAX')
    if sum(x * x for x in norm) < 1.0e-8:
        return [0.0, 0.0]
    n = normalize(norm)
    t1 = normalize(cross(n, [1.0, 0.0, 0.0] if abs(n[0]) < 0.9 else [0.0, 1.0, 0.0]))
    t2 = cross(n, t1)
    c = project(m, pos)
    a = project(m, [pos[i] + t1[i] * step for i in range(3)])
    b = project(m, [pos[i] + t2[i] * step for i in range(3)])
    d1 = [a[i] - c[i] for i in range(3)]
    d2 = [b[i] - c[i] for i in range(3)]
    det = d1[0] * d2[1] - d1[1] * d2[0]
    if abs(det) < 1.0e-14:
        return [grad_max, grad_max]
    g = [(d1[2] * d2[1] - d1[1] * d2[2]) / det, (d1[0] * d2[2] - d1[2] * d2[0]) / det]
    return [min(max(x, -grad_max), grad_max) for x in g]


def light_matrix(direction, extent, depth, perspective=False):
    """view space -> shadow texture space for a light looking along -direction"""
    f = normalize([-x for x in direction])
    s = normalize(cross(f, [0.0, 1.0, 0.0] if abs(f[1]) < 0.99 else [1.0, 0.0, 0.0]))
    u = cross(s, f)
    eye = [-f[i] * depth * 0.5 for i in range(3)]
    view = [s + [-sum(s[i] * eye[i] for i in range(3))], u + [-sum(u[i] * eye[i] for i in range(3))],
            [-x for x in f] + [sum(f[i] * eye[i] for i in range(3))], [0.0, 0.0, 0.0, 1.0]]
    if perspective:
        n, fr, t = 0.5, depth * 2.0, 1.0 / math.tan(math.radians(30.0))
        proj = [[t, 0, 0, 0], [0, t, 0, 0], [0, 0, (fr + n) / (n - fr), 2 * fr * n / (n - fr)], [0, 0, -1, 0]]
    else:
        proj = [[1 / extent, 0, 0, 0], [0, 1 / extent, 0, 0], [0, 0, -2 / depth, -1], [0, 0, 0, 1]]
    tex = [[0.5, 0, 0, 0.5], [0, 0.5, 0, 0.5], [0, 0, 0.5, 0.5], [0, 0, 0, 1]]
    mul = lambda a, b: [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
    return mul(tex, mul(proj, view))


# --- stage 2 mirrors ---------------------------------------------------------------------
def clamp_extent(extent, texel, lo, hi):
    """pcssClampExtent"""
    return [min(max(extent[i], texel[i] * lo), texel[i] * hi) for i in range(2)]


def res_scale(res, screen):
    """pcssResScale"""
    return max(res[0] / screen[0], 1.0)


def blocker_depth(depth_at, c, z_bias, grad, extent, rot):
    """pcssBlockerDepth; depth_at(uv) reads the raw depth map"""
    total, count = 0.0, 0
    centre = depth_at((c[0], c[1]))
    if centre < c[2] + z_bias:
        total += centre
        count += 1
    for px, py in shader_disk():
        off = ((rot[0] * px - rot[1] * py) * extent[0], (rot[1] * px + rot[0] * py) * extent[1])
        d = depth_at((c[0] + off[0], c[1] + off[1]))
        if d < c[2] + z_bias - min(grad[0] * abs(off[0]) + grad[1] * abs(off[1]), define('PCSS_RPDB_MAX')):
            total += d
            count += 1
    return total / count if count else -1.0


def sun_extents(s_uv, s_z, z_receiver, z_blocker, light_size, d_max, texel, scale=1.0):
    """search and PCF half axes (uv) of pcssSunContactShadow; PCF None for the early out"""
    r_max = define('PCSS_PCF_RADIUS_MAX') * scale
    search = clamp_extent([s * light_size * d_max for s in s_uv], texel, define('PCSS_SEARCH_RADIUS_MIN'), r_max)
    if z_blocker < 0.0:
        return search, None
    w_world = max(z_receiver - z_blocker, 0.0) / max(s_z, 1.0e-8) * light_size
    return search, clamp_extent([0.5 * w_world * s for s in s_uv], texel, define('PCSS_PCF_RADIUS_MIN'), r_max)


def det3(q):
    return (q[0][0] * (q[1][1] * q[2][2] - q[1][2] * q[2][1]) - q[0][1] * (q[1][0] * q[2][2] - q[1][2] * q[2][0])
            + q[0][2] * (q[1][0] * q[2][1] - q[1][1] * q[2][0]))


def projector_eye(m):
    """pcssProjectorEye: the point where rows 0, 1 and 3 of m vanish (x = y = w = 0)"""
    a = [m[0][:3], m[1][:3], m[3][:3]]
    b = [-m[0][3], -m[1][3], -m[3][3]]
    d = det3(a)
    return [det3([[b[r] if k == i else a[r][k] for k in range(3)] for r in range(3)]) / d for i in range(3)]


def spot_blocker_distance(m, stc, z_blocker):
    """the linearisation in pcssSpotContactShadow: stc undivided, z_blocker a raw depth"""
    eye = projector_eye(m)
    z_eye = sum(m[2][k] * eye[k] for k in range(3)) + m[2][3]
    d_r = stc[3]
    denom = z_blocker * d_r - stc[2] + z_eye
    d_b = z_eye * d_r / denom if abs(denom) > 1.0e-7 else d_r
    return min(max(d_b, 1.0e-3), d_r)


class PoissonDiskTest(unittest.TestCase):
    def test_shader_disk_is_the_generator_output(self):
        expected = generator().best_candidate(12)
        actual = shader_disk()
        self.assertEqual(len(actual), 12)
        self.assertEqual(int(define('PCSS_PCF_TAPS')), 12)
        for (ex, ey), (ax, ay) in zip(expected, actual):
            self.assertAlmostEqual(ex, ax, places=6)
            self.assertAlmostEqual(ey, ay, places=6)

    def test_disk_is_centred_in_the_unit_disc_and_spread(self):
        disk = shader_disk()
        self.assertAlmostEqual(max(math.hypot(x, y) for x, y in disk), 1.0, places=5)
        self.assertLess(abs(sum(x for x, _ in disk) / 12), 1e-5)
        self.assertLess(abs(sum(y for _, y in disk) / 12), 1e-5)
        spacing = min(math.dist(a, b) for i, a in enumerate(disk) for b in disk[i + 1:])
        self.assertGreater(spacing, 0.4)


class EdgeClampTest(unittest.TestCase):
    def test_taps_stay_inside_the_map(self):
        rng = random.Random(7)
        radius = define('PCSS_PCF_RADIUS')
        disk = shader_disk()
        for res in ((1936, 1088), (2576, 1456), (1920, 1920), (64, 32)):
            texel = (1.0 / res[0], 1.0 / res[1])
            extent = (radius * texel[0], radius * texel[1])
            for _ in range(2000):
                uv = (rng.uniform(-0.01, 1.01), rng.uniform(-0.01, 1.01))
                k = edge_scale(uv, extent, texel)
                inside = all(0.5 * texel[i] <= uv[i] <= 1.0 - 0.5 * texel[i] for i in range(2))
                if not inside:
                    self.assertEqual(k, 0.0)    # outside the map: one tap at the centre
                    continue
                a = rng.uniform(0.0, 2.0 * math.pi)
                for px, py in disk:
                    off = ((math.cos(a) * px - math.sin(a) * py) * extent[0] * k,
                           (math.sin(a) * px + math.cos(a) * py) * extent[1] * k)
                    for i in range(2):
                        self.assertGreaterEqual(uv[i] + off[i], 0.5 * texel[i] - 1e-12)
                        self.assertLessEqual(uv[i] + off[i], 1.0 - 0.5 * texel[i] + 1e-12)

    def test_full_kernel_away_from_the_edges(self):
        texel = (1.0 / 1936, 1.0 / 1088)
        extent = (1.5 * texel[0], 1.5 * texel[1])
        self.assertEqual(edge_scale((0.5, 0.5), extent, texel), 1.0)
        self.assertEqual(edge_scale((2.0 * texel[0], 0.5), extent, texel), 1.0)
        self.assertAlmostEqual(edge_scale((1.25 * texel[0], 0.5), extent, texel), 0.5)


class ReceiverPlaneTest(unittest.TestCase):
    def check_plane(self, m, tolerance):
        rng = random.Random(11)
        for _ in range(200):
            norm = normalize([rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)])
            pos = [rng.uniform(-5, 5), rng.uniform(-5, 5), rng.uniform(-30, -5)]
            g = depth_gradient(m, pos, norm)
            if max(abs(g[0]), abs(g[1])) >= define('PCSS_GRAD_MAX'):
                continue    # edge-on to the light: capped, checked below
            c = project(m, pos)
            # any small step within the surface's plane: depth change = g . uv change
            side = normalize(cross(norm, [rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)]))
            q = project(m, [pos[i] + side[i] * 0.02 for i in range(3)])
            predicted = g[0] * (q[0] - c[0]) + g[1] * (q[1] - c[1])
            self.assertAlmostEqual(predicted, q[2] - c[2], delta=tolerance)

    def test_orthographic_cascade(self):
        self.check_plane(light_matrix([-0.4, 1.0, 0.3], 20.0, 100.0), 1e-9)

    def test_perspective_projector(self):
        self.check_plane(light_matrix([0.2, 1.0, -0.1], 20.0, 30.0, perspective=True), 1e-6)

    def test_edge_on_surface_gets_the_capped_bias(self):
        m = light_matrix([0.0, 1.0, 0.0], 20.0, 100.0)
        # light along view space y (the matrix's own frame): a wall with a horizontal normal is edge-on
        g = depth_gradient(m, [0.0, 0.0, -10.0], [1.0, 0.0, 0.0])
        self.assertEqual(g, [define('PCSS_GRAD_MAX')] * 2)
        rpdb = min(g[0] * 1e-3 + g[1] * 1e-3, define('PCSS_RPDB_MAX'))
        self.assertEqual(rpdb, define('PCSS_RPDB_MAX'))

    def test_no_normal_means_no_plane_bias(self):
        m = light_matrix([0.0, 1.0, 0.0], 20.0, 100.0)
        self.assertEqual(depth_gradient(m, [0.0, 0.0, -10.0], [0.0, 0.0, 0.0]), [0.0, 0.0])

    def test_surface_facing_the_light_has_no_slope(self):
        m = light_matrix([0.3, 1.0, 0.2], 20.0, 100.0)
        g = depth_gradient(m, [1.0, 0.0, -12.0], [0.3, 1.0, 0.2])
        self.assertLess(max(abs(g[0]), abs(g[1])), 1e-9)


class ContactHardeningTest(unittest.TestCase):
    TEXEL = (1.0 / 1936, 1.0 / 1088)
    S_UV = (1.0 / 40.0, 1.0 / 22.5)     # an orthographic cascade 40 x 22.5 m across
    S_Z = 1.0 / 100.0                   # and 100 m deep

    def test_sun_penumbra_is_blocker_height_times_light_size(self):
        light = 0.05
        for h in (0.5, 1.0, 2.0):
            _, pcf = sun_extents(self.S_UV, self.S_Z, 0.6, 0.6 - h * self.S_Z, light, define('PCSS_BLOCKER_DIST0'), self.TEXEL)
            for i in range(2):   # the PCF disc's diameter is the full penumbra h L
                want = min(max(0.5 * h * light * self.S_UV[i], self.TEXEL[i] * define('PCSS_PCF_RADIUS_MIN')),
                           self.TEXEL[i] * define('PCSS_PCF_RADIUS_MAX'))
                self.assertAlmostEqual(pcf[i], want, places=12)
        # wider with the height (contact hardening), up to the cap
        widths = [sun_extents(self.S_UV, self.S_Z, 0.6, 0.6 - h * self.S_Z, light, 4.0, self.TEXEL)[1][0]
                  for h in (0.1, 0.5, 1.0, 2.0)]
        self.assertEqual(widths, sorted(widths))
        self.assertLess(widths[0], widths[-1])

    def test_kernel_limits_and_resolution_scale(self):
        _, contact = sun_extents(self.S_UV, self.S_Z, 0.6, 0.6 - 1e-6, 0.00925, 4.0, self.TEXEL)
        self.assertEqual(contact, [t * define('PCSS_PCF_RADIUS_MIN') for t in self.TEXEL])
        for scale in (1.0, 2.0):
            search, wide = sun_extents(self.S_UV, self.S_Z, 0.6, 0.0, 10.0, 8.0, self.TEXEL, scale)
            cap = [t * define('PCSS_PCF_RADIUS_MAX') * scale for t in self.TEXEL]
            self.assertEqual(wide, cap)
            self.assertEqual(search, cap)
        self.assertEqual(res_scale((1936, 1088), (1920, 1080)), 1936 / 1920)
        self.assertEqual(res_scale((3856, 2176), (1920, 1080)), 3856 / 1920)
        self.assertEqual(res_scale((1000, 500), (1920, 1080)), 1.0)

    def test_search_radius_follows_blocker_distance_cap(self):
        light = 0.02                    # 4 m and 8 m reach both between the limits
        self.assertEqual((define('PCSS_BLOCKER_DIST0'), define('PCSS_BLOCKER_DIST1')), (4.0, 8.0))
        near, _ = sun_extents(self.S_UV, 1.0, 0.5, -1.0, light, define('PCSS_BLOCKER_DIST0'), self.TEXEL)
        far, _ = sun_extents(self.S_UV, 1.0, 0.5, -1.0, light, define('PCSS_BLOCKER_DIST1'), self.TEXEL)
        self.assertAlmostEqual(far[0] / near[0], 2.0)    # both below the cap: twice the reach
        tiny, _ = sun_extents(self.S_UV, 1.0, 0.5, -1.0, 0.00925, 0.01, self.TEXEL)
        self.assertEqual(tiny, [t * define('PCSS_SEARCH_RADIUS_MIN') for t in self.TEXEL])

    def test_no_blocker_is_the_early_out(self):
        rot = (math.cos(0.7), math.sin(0.7))
        lit = blocker_depth(lambda uv: 0.8, (0.5, 0.5, 0.6), -0.004, (0.0, 0.0), (0.01, 0.01), rot)
        self.assertEqual(lit, -1.0)
        self.assertIsNone(sun_extents((0.02, 0.03), 0.01, 0.6, lit, 0.00925, 4.0, self.TEXEL)[1])
        # the receiver itself, within the bias, is no blocker either
        ground = blocker_depth(lambda uv: 0.598, (0.5, 0.5, 0.6), -0.004, (0.0, 0.0), (0.01, 0.01), rot)
        self.assertEqual(ground, -1.0)
        half = blocker_depth(lambda uv: 0.3 if uv[0] < 0.5 else 0.8, (0.5, 0.5, 0.6), -0.004, (0.0, 0.0), (0.01, 0.01), rot)
        self.assertEqual(half, 0.3)

    def test_thin_caster_under_the_receiver_is_found(self):
        # a 2x2 texel caster centred on the pixel, searched with an 8 texel radius: every
        # disk tap lands outside it, so only the centre read can find it
        rot = (math.cos(0.7), math.sin(0.7))
        texel = 1.0 / 1024.0
        caster = lambda uv: 0.3 if abs(uv[0] - 0.5) <= texel and abs(uv[1] - 0.5) <= texel else 0.8
        found = blocker_depth(caster, (0.5, 0.5, 0.6), -0.004, (0.0, 0.0), (8 * texel, 8 * texel), rot)
        self.assertEqual(found, 0.3)

    def test_spot_blocker_distance_from_raw_depth(self):
        m = light_matrix([0.2, 1.0, -0.1], 20.0, 30.0, perspective=True)
        eye = projector_eye(m)
        e = mat_vec(m, eye + [1.0])
        for i in (0, 1, 3):          # the projector's eye maps to x = y = w = 0
            self.assertAlmostEqual(e[i], 0.0, places=9)
        rng = random.Random(5)
        for _ in range(50):
            pos = [rng.uniform(-3, 3), rng.uniform(-3, 3), rng.uniform(-14, -6)]
            stc = mat_vec(m, pos + [1.0])
            s = rng.uniform(0.3, 0.95)   # a blocker on the same ray, nearer the projector
            qt = mat_vec(m, [eye[i] + s * (pos[i] - eye[i]) for i in range(3)] + [1.0])
            self.assertAlmostEqual(qt[3], s * stc[3], delta=1e-9)
            self.assertAlmostEqual(spot_blocker_distance(m, stc, qt[2] / qt[3]), qt[3], delta=1e-6 * stc[3])

    def test_disc_light_profile_constant(self):
        # bench_shadow_pcss.DISC_10_90: 10-90 % width of a disc light's edge over its full width
        def covered(t):  # area of a unit diameter disc left of x = t, as a fraction
            t = min(max(t, -0.5), 0.5)
            return (0.25 * math.acos(-t / 0.5) + t * math.sqrt(0.25 - t * t)) / (math.pi * 0.25)

        def solve(v):
            lo, hi = -0.5, 0.5
            for _ in range(60):
                mid = 0.5 * (lo + hi)
                lo, hi = (mid, hi) if covered(mid) < v else (lo, mid)
            return lo
        bench = (ROOT / 'scripts/perf/bench_shadow_pcss.py').read_text()
        constant = float(re.search(r'^DISC_10_90 = ([0-9.]+)', bench, re.M).group(1))
        self.assertAlmostEqual(solve(0.9) - solve(0.1), constant, places=6)


if __name__ == '__main__':
    unittest.main()

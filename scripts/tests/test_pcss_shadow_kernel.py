"""Check the PCSS stage 1 kernel of shadowUtil.glsl: its Poisson disk, edge clamp and
receiver-plane gradient, through Python mirrors of the GLSL functions that use the shader's
own constants. The GPU behaviour itself is measured by scripts/perf/bench_shadow_pcss.py."""
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


if __name__ == '__main__':
    unittest.main()

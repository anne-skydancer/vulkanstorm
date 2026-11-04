"""Generate the Poisson disks used by the PCSS shadow filter (shadowUtil.glsl).

Mitchell's best-candidate sampling in the unit disc: each new point is the one, out of
CANDIDATES uniformly drawn candidates, farthest from the points already chosen (and from the
disc's rim, at half weight, so the edge is not crowded). The set is then moved so its mean is
the centre (an unbiased kernel) and scaled so its farthest point lies on the unit circle. Deterministic (fixed seed); prints GLSL constant arrays.

Usage: python scripts/perf/gen_poisson_disk.py [count ...]   (default: 12 16)
"""
import math
import random
import sys

SEED = 20261103
CANDIDATES = 4000


def disc_point(rng):
    r = math.sqrt(rng.random())
    a = rng.random() * 2.0 * math.pi
    return (r * math.cos(a), r * math.sin(a))


def best_candidate(count, seed=SEED):
    rng = random.Random(seed + count)
    points = [disc_point(rng)]
    while len(points) < count:
        best, best_d = None, -1.0
        for _ in range(CANDIDATES):
            c = disc_point(rng)
            d = min(math.dist(c, p) for p in points)
            d = min(d, 2.0 * (1.0 - math.hypot(*c)))  # keep away from the rim
            if d > best_d:
                best, best_d = c, d
        points.append(best)
    mx = sum(p[0] for p in points) / count
    my = sum(p[1] for p in points) / count
    points = [(x - mx, y - my) for x, y in points]
    scale = 1.0 / max(math.hypot(*p) for p in points)
    return [(x * scale, y * scale) for x, y in points]


def glsl(name, points):
    rows = ',\n'.join(f'    vec2({x: .6f}, {y: .6f})' for x, y in points)
    return f'const vec2 {name}[{len(points)}] = vec2[{len(points)}](\n{rows});'


def main(argv):
    counts = [int(a) for a in argv[1:]] or [12, 16]
    for n in counts:
        pts = best_candidate(n)
        dmin = min(math.dist(a, b) for i, a in enumerate(pts) for b in pts[i + 1:])
        print(f'// {n} points, min spacing {dmin:.3f} (unit disc), seed {SEED}')
        print(glsl(f'pcss_poisson{n}', pts))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

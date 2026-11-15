# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""CPU side of the point-light Q0 faces bench (scripts/shadows/point_faces_bench.py): the six
face matrices, face masks, the per-group box test against the six face frusta, and the
affine tile map plus the four clip planes of method D' (doc/shadows/point-light-shadows-plan.md
section 3.3). No GL and no numpy, so scripts/tests/test_point_faces_model.py runs anywhere."""
import math

# cube face order: +X, -X, +Y, -Y, +Z, -Z. Each entry is (forward, up); right = forward x up.
FACES = (((1, 0, 0), (0, -1, 0)), ((-1, 0, 0), (0, -1, 0)),
         ((0, 1, 0), (0, 0, 1)), ((0, -1, 0), (0, 0, -1)),
         ((0, 0, 1), (0, -1, 0)), ((0, 0, -1), (0, -1, 0)))
ALL_FACES = 0b111111
# the face masks of the bench grid: how many faces of the cube are dirty (1, 2, 3 or 6)
MASKS = {1: 0b000001, 2: 0b000011, 3: 0b010101, 6: ALL_FACES}
ATLAS_COLS, ATLAS_ROWS = 3, 2


def _cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def _dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def mmul(a, b):
    return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def face_view(face, eye=(0.0, 0.0, 0.0)):
    """Row-major view matrix of a cube face for a light at eye (a right-handed look-at)."""
    f, u = FACES[face]
    r = _cross(f, u)
    return [[r[0], r[1], r[2], -_dot(r, eye)], [u[0], u[1], u[2], -_dot(u, eye)],
            [-f[0], -f[1], -f[2], _dot(f, eye)], [0.0, 0.0, 0.0, 1.0]]


def face_projection(near, far):
    """OpenGL 90 degree square perspective."""
    return [[1.0, 0, 0, 0], [0, 1.0, 0, 0],
            [0, 0, (far+near)/(near-far), 2*far*near/(near-far)], [0, 0, -1.0, 0]]


def face_matrices(near, far, eye=(0.0, 0.0, 0.0)):
    proj = face_projection(near, far)
    return [mmul(proj, face_view(f, eye)) for f in range(6)]


def mask_faces(mask):
    """Face indices in a mask, ascending (the order the faces are submitted in)."""
    return [f for f in range(6) if mask >> f & 1]


def popcount(mask):
    return bin(mask & ALL_FACES).count('1')


def frustum_planes(vp):
    """The six clip planes of a view-projection matrix (Gribb and Hartmann), inward normals:
    left, right, bottom, top, near, far. Each is (a, b, c, d) with a*x + b*y + c*z + d >= 0 inside."""
    r = vp
    def comb(sign, i):
        return tuple(r[3][k] + sign*r[i][k] for k in range(4))
    return (comb(1, 0), comb(-1, 0), comb(1, 1), comb(-1, 1), comb(1, 2), comb(-1, 2))


def box_outside_plane(bmin, bmax, plane):
    """True when the whole box lies on the outside of the plane (the positive vertex is outside)."""
    a, b, c, d = plane
    px = bmax[0] if a >= 0 else bmin[0]
    py = bmax[1] if b >= 0 else bmin[1]
    pz = bmax[2] if c >= 0 else bmin[2]
    return a*px + b*py + c*pz + d < 0


def box_touches_face(bmin, bmax, vp):
    """Conservative box against one face frustum: false only when the box is wholly outside a plane,
    so a triangle inside the box that reaches the face is never lost (a box can pass without a triangle
    touching the face, which only costs a draw)."""
    return not any(box_outside_plane(bmin, bmax, p) for p in frustum_planes(vp))


def group_mask(bmin, bmax, face_vps):
    """Bit f is set when the group box touches face f's frustum."""
    mask = 0
    for f, vp in enumerate(face_vps):
        if box_touches_face(bmin, bmax, vp):
            mask |= 1 << f
    return mask


def tile_origin(face, size):
    """Lower-left texel of the face's tile in the 3x2 atlas (3*size by 2*size)."""
    return (face % ATLAS_COLS)*size, (face // ATLAS_COLS)*size


def atlas_dims(size):
    return ATLAS_COLS*size, ATLAS_ROWS*size


def tile_map(face):
    """Method D' affine map of a face's clip x and y into the atlas clip space: x' = sx*x + ox*w and
    y' = sy*y + oy*w, so the interpolation stays perspective-correct. Returns (sx, ox, sy, oy)."""
    col, row = face % ATLAS_COLS, face // ATLAS_COLS
    sx, sy = 1.0/ATLAS_COLS, 1.0/ATLAS_ROWS
    return sx, (1.0 + 2*col)*sx - 1.0, sy, (1.0 + 2*row)*sy - 1.0


def apply_tile_map(face, x, y, w):
    sx, ox, sy, oy = tile_map(face)
    return sx*x + ox*w, sy*y + oy*w


def tile_clip_distances(x, y, w):
    """Method D''s four gl_ClipDistance values at the tile edges (in the face's own clip space);
    a vertex is inside the tile when all four are >= 0."""
    return (x + w, w - x, y + w, w - y)


def lcg(seed):
    """Small deterministic generator, so scenes do not depend on the Python version's random."""
    state = seed & 0xFFFFFFFF
    while True:
        state = (state*1664525 + 1013904223) & 0xFFFFFFFF
        yield (state >> 8)/float(1 << 24)


def median(values):
    s = sorted(values)
    n = len(s)
    return s[n//2] if n % 2 else 0.5*(s[n//2-1] + s[n//2])

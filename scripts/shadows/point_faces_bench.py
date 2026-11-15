# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Q0 offline bench of the point-light face rendering methods (doc/shadows/point-light-shadows-plan.md
section 3.3), standalone GL through scripts/perf/gl_test_context.py. No viewer, no viewer code.

Renders synthetic scenes into a 3x2 depth atlas of square face tiles with
  A    one cull, one submit per face in the mask (viewport and uniform per face)
  B    one cull per face (cull, sort and submit six times)
  C    geometry shader, gl_ViewportIndex, invocations = 6, one submit
  D    vertex shader gl_ViewportIndex with instancing (ARB_shader_viewport_layer_array)
  Dp   D': instancing, one viewport over the atlas, a per-face affine map into the tile, four
       gl_ClipDistance planes at the tile edges, the face from a per-instance vertex attribute
       (divisor 1) offset by glDrawElementsInstancedBaseInstance; #version 420, no gl_BaseInstance
  E    multi-draw indirect, one call per vertex buffer and face
  A_gm, Dp_gm   A and D' with per-group masks: each group box is tested against the six face frusta
       once per cull and the group is drawn only into the faces it touches

Scenes: synthetic rooms of 50k / 200k / 1M triangles in 50 / 300 / 1500 draws (grid patches, ten draws
per spatial group and per vertex buffer), "straddleK" rooms whose groups touch K faces, and a skinned
146k triangle avatar proxy with four bone matrices. Grid: faces 64 / 128 / 256 / 512, masks of
1 / 2 / 3 / 6 faces. Equal work is checked by reading the depth atlas back and comparing it with A.

Timing: CPU submit time with QPC (time.perf_counter_ns) and GPU time with GL_TIME_ELAPSED, per
configuration the median of 9 runs of 20 repetitions (a run's value is the mean of its 20), after
warm-up, the methods interleaved run by run. The CPU time includes Python and ctypes overhead per GL call,
so the number of GL calls and draw calls is reported next to it.

Environment: POINT_BENCH_RUNS (9), POINT_BENCH_REPS (20), POINT_BENCH_WARM (3), POINT_BENCH_SCENES,
POINT_BENCH_SIZES, POINT_BENCH_MASKS (comma lists), POINT_BENCH_OUT (JSON path).
"""
import json, os, sys, time, types
from pathlib import Path
import importlib.util
bootstrap = Path(__file__).resolve().parents[1].joinpath('perf', 'gl_test_context.py').read_text()
exec(compile(bootstrap, 'gl_test_bootstrap', 'exec'))
import numpy as np
spec = importlib.util.spec_from_file_location('point_faces_model', Path(__file__).with_name('point_faces_model.py'))
M = importlib.util.module_from_spec(spec); spec.loader.exec_module(M)

RUNS = int(os.environ.get('POINT_BENCH_RUNS', 9)); REPS = int(os.environ.get('POINT_BENCH_REPS', 20))
WARM = int(os.environ.get('POINT_BENCH_WARM', 3))
NEAR, FAR, RADIUS = 0.1, 40.0, 20.0
SIZES = [int(v) for v in os.environ.get('POINT_BENCH_SIZES', '64,128,256,512').split(',')]
MASK_N = [int(v) for v in os.environ.get('POINT_BENCH_MASKS', '1,2,3,6').split(',')]
METHODS = ['A', 'B', 'C', 'D', 'Dp', 'E', 'A_gm', 'Dp_gm']
ROOMS = [('room50k', 50000, 50), ('room200k', 200000, 300), ('room1M', 1000000, 1500)]
DEFAULT_SCENES = 'room50k,room200k,room1M,avatar,straddle1,straddle2,straddle3,straddle4,straddle5,straddle6'
SCENES = os.environ.get('POINT_BENCH_SCENES', DEFAULT_SCENES).split(',')
STRADDLE_METHODS = ['A', 'A_gm', 'Dp', 'Dp_gm']     # the straddle scenes run these, at 256 and mask 6
DRAWS_PER_GROUP = 10

# --- GL entry points -----------------------------------------------------------------------------
UB = C.c_ubyte
F = C.c_float
gentex = fn('glGenTextures', None, I, C.POINTER(U)); bindtex = fn('glBindTexture', None, U, U)
texstorage = fn('glTexStorage2D', None, U, I, U, I, I)
genfbo = fn('glGenFramebuffers', None, I, C.POINTER(U)); bindfbo = fn('glBindFramebuffer', None, U, U)
attachtex = fn('glFramebufferTexture2D', None, U, U, U, U, I); fbstatus = fn('glCheckFramebufferStatus', U, U)
drawbuffer = fn('glDrawBuffer', None, U); readbuffer = fn('glReadBuffer', None, U)
viewport = fn('glViewport', None, I, I, I, I)
enable = fn('glEnable', None, U); disable = fn('glDisable', None, U); depthfunc = fn('glDepthFunc', None, U)
depthmask = fn('glDepthMask', None, C.c_ubyte)
geterror = fn('glGetError', U); finish = fn('glFinish', None)
loc = fn('glGetUniformLocation', I, U, C.c_char_p)
u1i = fn('glUniform1i', None, I, I); u4f = fn('glUniform4f', None, I, F, F, F, F)
umat = fn('glUniformMatrix4fv', None, I, I, C.c_ubyte, C.POINTER(F))
genq = fn('glGenQueries', None, I, C.POINTER(U)); beginq = fn('glBeginQuery', None, U, U); endq = fn('glEndQuery', None, U)
qresult = fn('glGetQueryObjectui64v', None, U, U, C.POINTER(C.c_uint64))
TEX2D, FB, DEPTH_ATT, TIME_ELAPSED, QUERY_RESULT = 0x0DE1, 0x8D40, 0x8D00, 0x88BF, 0x8866
gen_buffers = fn('glGenBuffers', None, I, C.POINTER(U)); bind_buffer = fn('glBindBuffer', None, U, U)
buffer_data = fn('glBufferData', None, U, S, P, U)
gen_vaos = fn('glGenVertexArrays', None, I, C.POINTER(U)); bind_vao = fn('glBindVertexArray', None, U)
attrib_ptr = fn('glVertexAttribPointer', None, U, I, U, UB, I, P)
attrib_iptr = fn('glVertexAttribIPointer', None, U, I, U, I, P)
attrib_div = fn('glVertexAttribDivisor', None, U, U); attrib_on = fn('glEnableVertexAttribArray', None, U)
draw_elements = fn('glDrawElements', None, U, I, U, P)
draw_inst_base = fn('glDrawElementsInstancedBaseInstance', None, U, I, U, P, I, U)
multi_indirect = fn('glMultiDrawElementsIndirect', None, U, U, P, I, I)
viewport_array = fn('glViewportArrayv', None, U, I, C.POINTER(F))
tex_buffer = fn('glTexBuffer', None, U, U, U)
clear = fn('glClear', None, U); get_tex_image = fn('glGetTexImage', None, U, I, U, U, P)
get_stringi = fn('glGetStringi', C.c_char_p, U, U); get_integer = fn('glGetIntegerv', None, U, C.POINTER(I))
GL_ARRAY, GL_ELEMENT, GL_INDIRECT, GL_STATIC = 0x8892, 0x8893, 0x8F3F, 0x88E4
GL_TRIANGLES, GL_UINT, GL_FLOAT, GL_UBYTE = 4, 0x1405, 0x1406, 0x1401


def gen_buf(target, array):
    b = U(); gen_buffers(1, C.byref(b)); bind_buffer(target, b.value)
    buffer_data(target, array.nbytes, array.ctypes.data, GL_STATIC)
    return b.value
VS, FS, GS = 0x8B31, 0x8B30, 0x8DD9


def extensions():
    n = I(); get_integer(0x821D, C.byref(n))
    return {get_stringi(0x1F03, i).decode() for i in range(n.value)}


def counting_gl(counts):
    """A stand-in for the GL table that only counts calls: the same submit code runs against it once."""
    def make(name):
        def f(*a):
            counts[name] = counts.get(name, 0) + 1
        return f
    return types.SimpleNamespace(**{n: make(n) for n in GL_NAMES})


GL_NAMES = ['use', 'umat', 'u1i', 'u4f', 'viewport', 'viewport_array', 'bind_vao', 'bind_buffer', 'draw_elements',
            'draw_inst_base', 'multi_indirect']
REAL = types.SimpleNamespace(use=use, umat=umat, u1i=u1i, u4f=u4f, viewport=viewport, viewport_array=viewport_array,
                             bind_vao=bind_vao, bind_buffer=bind_buffer, draw_elements=draw_elements,
                             draw_inst_base=draw_inst_base, multi_indirect=multi_indirect)
DRAW_NAMES = ('draw_elements', 'draw_inst_base', 'multi_indirect')

# --- shaders -------------------------------------------------------------------------------------
def vs_src(kind, skin, tbo):
    s = '#version 420\n'
    if kind == 'D':
        s += '#extension GL_ARB_shader_viewport_layer_array : require\n'
    s += 'layout(location=0) in vec3 aPos;\n'
    if skin:
        s += 'layout(location=1) in vec4 aW;\nlayout(location=2) in uvec4 aJ;\n'
    s += 'layout(location=3) in uint aInst;\n'
    s += 'uniform mat4 uFaceVP[6]; uniform int uFace; uniform vec4 uTile[6]; uniform mat4 uModel; uniform mat4 uBones[4];\n'
    if tbo:
        s += 'uniform samplerBuffer uModels;\n'
    s += 'void main() {\n  vec4 wp;\n'
    if skin:
        s += ('  vec4 q = vec4(0.0);\n  for (int i = 0; i < 4; ++i) q += aW[i] * (uBones[aJ[i]] * vec4(aPos, 1.0));\n'
              '  wp = uModel * q;\n')
    elif tbo:
        s += ('  int id = int(aInst) * 4;\n  mat4 mdl = mat4(texelFetch(uModels, id), texelFetch(uModels, id + 1),\n'
              '                  texelFetch(uModels, id + 2), texelFetch(uModels, id + 3));\n  wp = mdl * vec4(aPos, 1.0);\n')
    else:
        s += '  wp = uModel * vec4(aPos, 1.0);\n'
    if kind in ('A', 'E'):
        s += '  gl_Position = uFaceVP[uFace] * wp;\n'
    elif kind == 'C':
        s += '  gl_Position = wp;\n'
    elif kind == 'D':
        s += '  gl_ViewportIndex = int(aInst);\n  gl_Position = uFaceVP[aInst] * wp;\n'
    elif kind == 'Dp':
        s += ('  vec4 c = uFaceVP[aInst] * wp;\n  gl_ClipDistance[0] = c.x + c.w;\n  gl_ClipDistance[1] = c.w - c.x;\n'
              '  gl_ClipDistance[2] = c.y + c.w;\n  gl_ClipDistance[3] = c.w - c.y;\n  vec4 t = uTile[aInst];\n'
              '  gl_Position = vec4(t.x * c.x + t.y * c.w, t.z * c.y + t.w * c.w, c.z, c.w);\n')
    return s + '}\n'


GS_SRC = '''#version 420
layout(triangles, invocations = 6) in;
layout(triangle_strip, max_vertices = 3) out;
uniform mat4 uFaceVP[6]; uniform int uMask;
void main() {
  int f = gl_InvocationID;
  if (((uMask >> f) & 1) == 0) return;
  vec4 c0 = uFaceVP[f] * gl_in[0].gl_Position, c1 = uFaceVP[f] * gl_in[1].gl_Position, c2 = uFaceVP[f] * gl_in[2].gl_Position;
  if ((c0.x < -c0.w && c1.x < -c1.w && c2.x < -c2.w) || (c0.x > c0.w && c1.x > c1.w && c2.x > c2.w) ||
      (c0.y < -c0.w && c1.y < -c1.w && c2.y < -c2.w) || (c0.y > c0.w && c1.y > c1.w && c2.y > c2.w) ||
      (c0.z < -c0.w && c1.z < -c1.w && c2.z < -c2.w) || (c0.z > c0.w && c1.z > c1.w && c2.z > c2.w)) return;
  gl_ViewportIndex = f;
  gl_Position = c0; EmitVertex(); gl_Position = c1; EmitVertex(); gl_Position = c2; EmitVertex(); EndPrimitive();
}
'''
FS_SRC = '#version 420\nvoid main() {}\n'
PROGRAMS = {}


def get_program(kind, skin):
    key = (kind, skin)
    if key not in PROGRAMS:
        stages = [(VS, vs_src(kind, skin, tbo=(kind == 'E' and not skin))), (FS, FS_SRC)]
        if kind == 'C':
            stages.insert(1, (GS, GS_SRC))
        prog = program(stages)
        names = ['uFaceVP', 'uFace', 'uModel', 'uBones', 'uMask', 'uModels'] + ['uTile[%d]' % i for i in range(6)]
        PROGRAMS[key] = (prog, {n: loc(prog, n.encode()) for n in names})
    return PROGRAMS[key]




# --- scene -----------------------------------------------------------------------------------------
FACE_VP = M.face_matrices(NEAR, FAR)
FACE_VP_ARR = (F*96)(*[x for m in FACE_VP for row in m for x in row])      # row major, uploaded transposed
PLANES = [np.array(M.frustum_planes(vp), dtype=np.float64) for vp in FACE_VP]
TILE_MAPS = [M.tile_map(f) for f in range(6)]
# the face list buffer: for every mask 0..63 the faces in it; baseinstance = FACE_OFF[mask]
FACE_OFF = []; _flat = []
for _m in range(64):
    FACE_OFF.append(len(_flat)); _flat.extend(M.mask_faces(_m))
FACE_LIST = gen_buf(GL_ARRAY, np.array(_flat, dtype=np.uint32))
IDENT = gen_buf(GL_ARRAY, np.arange(4096, dtype=np.uint32))                 # draw ids for method E


def box_touch(bmin, bmax, planes):
    """Vectorised M.box_touches_face: (G,3), (G,3), (6,4) -> (G,) bool."""
    pos = np.where(planes[None, :, :3] >= 0, bmax[:, None, :], bmin[:, None, :])
    return ((pos*planes[None, :, :3]).sum(-1) + planes[None, :, 3] >= 0).all(1)


class Draw:
    __slots__ = ('group', 'offset', 'count', 'model', 'gid')


class Group:
    pass


class Scene:
    pass


def translate(t):
    return (F*16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1)


def make_patch(rng, center, half, n):
    u = np.array([next(rng) - 0.5 for _ in range(3)]); u /= np.linalg.norm(u)
    t = np.array([next(rng) - 0.5 for _ in range(3)]); t -= u*np.dot(t, u); t /= np.linalg.norm(t)
    ax = np.linspace(-half, half, n + 1)
    gx, gy = np.meshgrid(ax, ax)
    verts = center + gx[..., None]*u + gy[..., None]*t
    return verts.reshape(-1, 3), grid_tris(n + 1, n + 1)


def grid_tris(rows, cols):
    idx = np.arange(rows*cols, dtype=np.uint32).reshape(rows, cols)
    a, b, c, d = idx[:-1, :-1], idx[:-1, 1:], idx[1:, :-1], idx[1:, 1:]
    return np.stack([np.stack([a, b, c], -1), np.stack([b, d, c], -1)], 2).reshape(-1, 3)


def upload_group(sc, parts, skin=False):
    """parts: list of (center, world verts (n,3), tris (m,3), skin data or None); one vertex buffer,
    one index buffer, one indirect buffer and two VAOs (face-list and draw-id instance attributes)."""
    g = Group(); g.draws = []
    vdt = np.dtype([('p', '<f4', 3), ('w', '<f4', 4), ('j', 'u1', 4)]) if skin else np.dtype([('p', '<f4', 3)])
    vparts, iparts, base, first = [], [], 0, 0; cmds = []; allw = []
    for center, v, t, sk in parts:
        rec = np.zeros(len(v), dtype=vdt); rec['p'] = (v - center); allw.append(v)
        if skin:
            rec['w'], rec['j'] = sk
        vparts.append(rec); iparts.append((t.reshape(-1) + base).astype(np.uint32)); base += len(v)
        d = Draw(); d.group = g; d.offset = first*4; d.count = t.size; d.model = translate(center)
        d.gid = len(sc.groups)*1000000 + len(g.draws); g.draws.append(d)
        cmds.append((t.size, 1, first, 0, sc.next_id)); sc.model_cols.extend(list(d.model)); sc.next_id += 1
        first += t.size
    allw = np.concatenate(allw); g.bmin, g.bmax = allw.min(0), allw.max(0)
    g.ndraws = len(parts)
    vbo = gen_buf(GL_ARRAY, np.ascontiguousarray(np.concatenate(vparts)))
    ibo = gen_buf(GL_ELEMENT, np.concatenate(iparts))
    g.indirect = gen_buf(GL_INDIRECT, np.array(cmds, dtype=np.uint32))
    stride = vdt.itemsize
    vaos = []
    for instbuf in (FACE_LIST, IDENT):
        v = U(); gen_vaos(1, C.byref(v)); bind_vao(v.value)
        bind_buffer(GL_ARRAY, vbo); attrib_ptr(0, 3, GL_FLOAT, 0, stride, None); attrib_on(0)
        if skin:
            attrib_ptr(1, 4, GL_FLOAT, 0, stride, 12); attrib_on(1)
            attrib_iptr(2, 4, GL_UBYTE, stride, 28); attrib_on(2)
        bind_buffer(GL_ELEMENT, ibo)
        bind_buffer(GL_ARRAY, instbuf); attrib_iptr(3, 1, GL_UINT, 0, None); attrib_div(3, 1); attrib_on(3)
        vaos.append(v.value)
    g.vao, g.vao_e = vaos
    bind_vao(0)
    sc.groups.append(g)
    sc.tris += sum(t.shape[0] for _, _, t, _ in parts)
    return g


def finish_scene(sc, seed):
    rng = M.lcg(seed + 99)
    order = sorted(range(len(sc.draws_all)), key=lambda _: next(rng))          # random submission order
    sc.draws = [sc.draws_all[i] for i in order]
    sc.draw_group = np.array([gi for gi, g in enumerate(sc.groups) for _ in g.draws], dtype=np.int64)[order]
    sc.bmin = np.array([g.bmin for g in sc.groups]); sc.bmax = np.array([g.bmax for g in sc.groups])
    sc.all63 = [63]*len(sc.draws)
    sc.gmask = np.zeros(len(sc.groups), dtype=np.int64)
    for f in range(6):
        sc.gmask |= box_touch(sc.bmin, sc.bmax, PLANES[f]).astype(np.int64) << f
    sc.group_hist = [int(sum(1 for m in sc.gmask if M.popcount(int(m)) == k)) for k in range(7)]
    tex = U(); gentex(1, C.byref(tex)); bindtex(0x8C2A, tex.value)
    cols = np.array(sc.model_cols, dtype=np.float32)
    sc.tbo_buf = gen_buf(0x8892, cols); tex_buffer(0x8C2A, 0x8814, sc.tbo_buf); sc.tbo = tex.value


def new_scene(name, skinned=False):
    sc = Scene(); sc.name = name; sc.groups = []; sc.model_cols = []; sc.next_id = 0; sc.tris = 0; sc.skinned = skinned
    sc.bones = None
    return sc


def finish_groups(sc, seed):
    sc.draws_all = [d for g in sc.groups for d in g.draws]
    finish_scene(sc, seed)
    return sc


def build_room(name, tris, draws, straddle=None, seed=1):
    sc = new_scene(name); rng = M.lcg(seed)
    n = max(1, round(((tris/draws)/2) ** 0.5))
    half = min(4.0, max(0.5, 0.5*(3072.0/draws) ** 0.5))     # the patches add up to about two room surfaces (16 m cube)
    achieved = []
    for gi in range(draws // DRAWS_PER_GROUP):
        best, best_gap = None, 99
        for attempt in range(80 if straddle else 1):
            c = np.array([(next(rng) - 0.5)*16 for _ in range(3)])
            h = np.array([(0.3 + next(rng)*5.7) if straddle else (0.5 + next(rng)*2.5) for _ in range(3)])
            patches = []
            for _ in range(DRAWS_PER_GROUP):
                pc = c + (np.array([next(rng) for _ in range(3)])*2 - 1)*h
                v, t = make_patch(rng, pc, half, n); patches.append((pc, v, t, None))
            allv = np.concatenate([p[1] for p in patches])
            mask = M.group_mask(allv.min(0), allv.max(0), FACE_VP)
            gap = abs(M.popcount(mask) - straddle) if straddle else 0
            if best is None or gap < best_gap:
                best, best_gap = patches, gap
            if gap == 0:
                break
        upload_group(sc, best)
    return finish_groups(sc, seed)


def build_avatar(seed=5):
    sc = new_scene('avatar', skinned=True)
    rows, cols = 87, 140                                      # 6 parts x 24,360 triangles = 146,160
    origin = np.array([2.2, -1.0, 0.6])
    parts = []
    spec = [(0.00, 1.2, 0.20, (0.0, 0.0)), (0.00, 1.2, 0.20, (0.3, 0.0)), (1.1, 0.6, 0.22, (0.0, 0.0)),
            (1.5, 0.35, 0.12, (0.0, 0.0)), (1.1, 0.7, 0.07, (-0.3, 0.0)), (1.1, 0.7, 0.07, (0.3, 0.0))]
    for y0, length, rad, (ox, oz) in spec:
        v = np.linspace(0, 1, rows + 1)[:, None]; th = np.linspace(0, 2*np.pi, cols + 1)[None, :]
        r = rad*(1 + 0.2*np.sin(3*np.pi*v))
        x = ox + r*np.cos(th) + 0*v; z = oz + r*np.sin(th) + 0*v; y = origin[1] + y0 + length*v + 0*th
        world = np.stack([x + origin[0], y, z + origin[2]], -1).reshape(-1, 3)
        yn = np.clip((world[:, 1] - origin[1])/2.0, 0, 0.9999)*3.0
        b = yn.astype(np.int64); fr = (yn - b).astype(np.float32)
        w = np.zeros((len(world), 4), np.float32); w[:, 0] = 1 - fr; w[:, 1] = fr
        j = np.zeros((len(world), 4), np.uint8); j[:, 0] = b; j[:, 1] = np.minimum(b + 1, 3)
        parts.append((origin, world, grid_tris(rows + 1, cols + 1), (w, j)))
    upload_group(sc, parts, skin=True)
    bones = []
    for i in range(4):
        a = 0.04*i; ca, sa = np.cos(a), np.sin(a)
        bones.extend([ca, 0, -sa, 0, 0, 1, 0, 0, sa, 0, ca, 0, 0.01*i, 0.02*i, 0, 1])
    sc.bones = (F*64)(*bones)
    sc.model = translate(origin)
    return finish_groups(sc, seed)


def build_scene(name):
    if name == 'avatar':
        return build_avatar()
    if name.startswith('straddle'):
        return build_room(name, 200000, 300, straddle=int(name[8:]), seed=20 + int(name[8:]))
    for rn, tris, draws in ROOMS:
        if rn == name:
            return build_room(rn, tris, draws, seed=3)
    raise SystemExit('unknown scene ' + name)


# --- the methods ---------------------------------------------------------------------------------
def cull(sc, face=None, masks=False):
    """One cull: the groups whose box meets the lamp's sphere box (and, with face, that face's frustum),
    the draws of those groups in submission order, sorted by group (state sort). Returns (draw, mask) pairs;
    masks=True also tests every group box against the six face frusta."""
    vg = ((sc.bmax >= -RADIUS) & (sc.bmin <= RADIUS)).all(1)
    gm = None
    if face is not None:
        vg &= box_touch(sc.bmin, sc.bmax, PLANES[face])
    if masks:
        gm = np.zeros(len(vg), dtype=np.int64)
        for f in range(6):
            gm |= box_touch(sc.bmin, sc.bmax, PLANES[f]).astype(np.int64) << f
        dmask = gm[sc.draw_group].tolist()
    else:
        dmask = sc.all63
    sel = vg[sc.draw_group].tolist()
    items = [(d, m) for d, s, m in zip(sc.draws, sel, dmask) if s]
    items.sort(key=lambda p: p[0].gid)
    return items


def begin(g, kind, sc, mask=0):
    prog, L = get_program(kind, sc.skinned)
    g.use(prog)
    g.umat(L['uFaceVP'], 6, 1, FACE_VP_ARR)
    if kind == 'Dp':
        for f in range(6):
            g.u4f(L['uTile[%d]' % f], *TILE_MAPS[f])
    if sc.skinned:
        g.umat(L['uBones'], 4, 0, sc.bones); g.umat(L['uModel'], 1, 0, sc.model)
    if kind == 'C':
        g.u1i(L['uMask'], mask)
    return L


def issue_plain(g, L, sc, items):
    cur = None
    for d, _ in items:
        if d.group is not cur:
            g.bind_vao(d.group.vao); cur = d.group
        if not sc.skinned:
            g.umat(L['uModel'], 1, 0, d.model)
        g.draw_elements(GL_TRIANGLES, d.count, GL_UINT, d.offset)


def tile_viewports(size):
    flat = []
    for f in range(6):
        x0, y0 = M.tile_origin(f, size); flat.extend([x0, y0, size, size])
    return (F*24)(*flat)


def submit(method, g, sc, mask, size):
    faces = M.mask_faces(mask); n = len(faces)
    if method in ('A', 'A_gm'):
        gm = method == 'A_gm'
        L = begin(g, 'A', sc)
        items = cull(sc, None, gm)
        for f in faces:
            x0, y0 = M.tile_origin(f, size); g.viewport(x0, y0, size, size); g.u1i(L['uFace'], f)
            cur = None
            for d, dm in items:
                if gm and not dm >> f & 1:
                    continue
                if d.group is not cur:
                    g.bind_vao(d.group.vao); cur = d.group
                if not sc.skinned:
                    g.umat(L['uModel'], 1, 0, d.model)
                g.draw_elements(GL_TRIANGLES, d.count, GL_UINT, d.offset)
    elif method == 'B':
        L = begin(g, 'A', sc)
        for f in faces:
            items = cull(sc, f)
            x0, y0 = M.tile_origin(f, size); g.viewport(x0, y0, size, size); g.u1i(L['uFace'], f)
            issue_plain(g, L, sc, items)
    elif method == 'C':
        L = begin(g, 'C', sc, mask)
        g.viewport_array(0, 6, tile_viewports(size))
        issue_plain(g, L, sc, cull(sc))
    elif method == 'D':
        L = begin(g, 'D', sc)
        g.viewport_array(0, 6, tile_viewports(size))
        cur = None
        for d, _ in cull(sc):
            if d.group is not cur:
                g.bind_vao(d.group.vao); cur = d.group
            if not sc.skinned:
                g.umat(L['uModel'], 1, 0, d.model)
            g.draw_inst_base(GL_TRIANGLES, d.count, GL_UINT, d.offset, n, FACE_OFF[mask])
    elif method in ('Dp', 'Dp_gm'):
        gm = method == 'Dp_gm'
        L = begin(g, 'Dp', sc)
        w, h = M.atlas_dims(size); g.viewport(0, 0, w, h)
        cur = None
        for d, dm in cull(sc, None, gm):
            em = dm & mask
            if not em:
                continue
            if d.group is not cur:
                g.bind_vao(d.group.vao); cur = d.group
            if not sc.skinned:
                g.umat(L['uModel'], 1, 0, d.model)
            g.draw_inst_base(GL_TRIANGLES, d.count, GL_UINT, d.offset, M.popcount(em), FACE_OFF[em])
    elif method == 'E':
        L = begin(g, 'E', sc)
        groups = list(dict.fromkeys(d.group for d, _ in cull(sc)))
        for f in faces:
            x0, y0 = M.tile_origin(f, size); g.viewport(x0, y0, size, size); g.u1i(L['uFace'], f)
            for grp in groups:
                g.bind_vao(grp.vao_e); g.bind_buffer(GL_INDIRECT, grp.indirect)
                g.multi_indirect(GL_TRIANGLES, GL_UINT, None, grp.ndraws, 0)
    else:
        raise ValueError(method)


# --- harness ---------------------------------------------------------------------------------------
class Atlas:
    def __init__(self, size):
        self.size = size; self.w, self.h = M.atlas_dims(size)
        t = U(); gentex(1, C.byref(t)); bindtex(TEX2D, t.value); texstorage(TEX2D, 1, 0x8CAC, self.w, self.h)
        f = U(); genfbo(1, C.byref(f)); bindfbo(FB, f.value); attachtex(FB, DEPTH_ATT, TEX2D, t.value, 0)
        drawbuffer(0); readbuffer(0)
        assert fbstatus(FB) == 0x8CD5, hex(fbstatus(FB))
        self.tex, self.fbo = t.value, f.value
        self.buf = np.empty((self.h, self.w), dtype=np.float32)

    def bind(self):
        bindfbo(FB, self.fbo); depthmask(1); enable(0x0B71); depthfunc(0x0201)

    def readback(self):
        finish(); bindtex(TEX2D, self.tex)
        get_tex_image(TEX2D, 0, 0x1902, GL_FLOAT, self.buf.ctypes.data)
        return self.buf.copy()


def set_method_state(method, on):
    if method.startswith('Dp'):
        for i in range(4):
            (enable if on else disable)(0x3000 + i)


def run_once(method, sc, mask, atlas, timed=False):
    atlas.bind(); clear(0x100)
    set_method_state(method, True)
    q = None
    if timed:
        q = U(); genq(1, C.byref(q)); beginq(TIME_ELAPSED, q.value)
    t0 = time.perf_counter_ns()
    submit(method, REAL, sc, mask, atlas.size)
    t1 = time.perf_counter_ns()
    gpu = None
    if timed:
        endq(TIME_ELAPSED)
        v = C.c_uint64(); qresult(q.value, QUERY_RESULT, C.byref(v)); gpu = v.value/1e6
        fn('glDeleteQueries', None, I, C.POINTER(U))(1, C.byref(q))
    else:
        finish()
    set_method_state(method, False)
    return (t1 - t0)/1e6, gpu


def linear_z(d):
    return NEAR*FAR/(FAR - d*(FAR - NEAR))


def compare(a, b, tol_m=1e-3):
    """Depth atlases a (reference, method A) and b. A texel differs when its linear depth differs by more
    than tol_m (1 mm) or only one of the two is covered; a method passes when at most 1e-4 of the covered
    texels differ (the last-ulp edge cases: a neighbouring triangle winning a pixel centre that lies on a shared edge)."""
    ca, cb = a < 1.0, b < 1.0
    both = ca & cb
    dz = np.abs(linear_z(a) - linear_z(b))
    cov_mismatch = int((ca != cb).sum()); covered = int(max(ca.sum(), 1))
    over = int(((dz > tol_m) & both).sum())
    res = {'covered_texels': covered, 'coverage_mismatch': cov_mismatch, 'depth_over_tol': over,
           'max_abs_diff_depth': float(np.abs(a - b)[both].max()) if both.any() else 0.0,
           'max_abs_diff_m': float(dz[both].max()) if both.any() else 0.0, 'exact': bool((a == b).all())}
    res['ok'] = (cov_mismatch + over)/covered <= 1e-4
    return res


def timing(methods, sc, mask, atlas):
    for m in methods:                                   # warm-up
        for _ in range(WARM):
            run_once(m, sc, mask, atlas, timed=True)
    cpu = {m: [] for m in methods}; gpu = {m: [] for m in methods}
    for _ in range(RUNS):
        for m in methods:
            c_, g_ = [], []
            for _ in range(REPS):
                c, g = run_once(m, sc, mask, atlas, timed=True); c_.append(c); g_.append(g)
            cpu[m].append(sum(c_)/REPS); gpu[m].append(sum(g_)/REPS)
    return cpu, gpu


def call_counts(method, sc, mask, size):
    counts = {}
    submit(method, counting_gl(counts), sc, mask, size)
    return sum(counts.values()), sum(counts.get(n, 0) for n in DRAW_NAMES)


def busy_processes():
    import subprocess
    names = ('cl.exe', 'link.exe', 'msbuild.exe', 'ninja.exe', 'vulkanstorm-bin.exe', 'SLPlugin.exe', 'tracy-capture.exe')
    out = subprocess.run(['tasklist', '/fo', 'csv', '/nh'], capture_output=True, text=True).stdout.lower()
    return [n for n in names if '"%s"' % n.lower() in out]


def wait_quiet(limit_s=7200):
    """Timing runs only while no build, viewer, plugin or Tracy capture process runs: poll every 60 s."""
    waited = 0
    while busy_processes() and waited < limit_s:
        print('busy', busy_processes(), 'waiting', flush=True)
        time.sleep(60); waited += 60
    if busy_processes():
        raise SystemExit('machine not quiet after %d s' % limit_s)
    time.sleep(5)
    return waited


def ctypes_call_cost():
    t0 = time.perf_counter_ns()
    for _ in range(100000):
        geterror()
    return (time.perf_counter_ns() - t0)/100000/1000.0


def main():
    exts = extensions()
    assert 'GL_ARB_shader_viewport_layer_array' in exts, 'method D needs ARB_shader_viewport_layer_array'
    busy = busy_processes()
    info = {'renderer': get_string(0x1F01).decode(), 'version': get_string(0x1F02).decode(),
            'runs': RUNS, 'reps': REPS, 'warm': WARM, 'busy_at_start': busy,
            'ctypes_call_us': ctypes_call_cost(), 'started': time.strftime('%Y-%m-%d %H:%M:%S')}
    print(json.dumps(info), flush=True)
    out = Path(os.environ.get('POINT_BENCH_OUT', 'point_faces_results.jsonl'))
    out.write_text(json.dumps({'info': info}) + '\n')
    for scene in SCENES:
        sc = build_scene(scene)
        straddle = scene.startswith('straddle')
        print('scene', scene, 'triangles', sc.tris, 'draws', len(sc.draws), 'groups', len(sc.groups),
              'group face histogram', sc.group_hist[1:], flush=True)
        for size in ([256] if straddle else SIZES):
            atlas = Atlas(size)
            for mn in ([6] if straddle else MASK_N):
                mask = M.MASKS[mn]
                methods = STRADDLE_METHODS if straddle else METHODS
                waited = wait_quiet()
                started = time.strftime('%H:%M:%S')
                ref = None; checks = {}
                for m in methods:
                    run_once(m, sc, mask, atlas); img = atlas.readback()
                    if ref is None:
                        ref = img
                    checks[m] = compare(ref, img) if m != 'A' else {'exact': True, 'ok': True, 'covered_texels': int((img < 1).sum())}
                cpu, gpu = timing(methods, sc, mask, atlas)
                row = {'scene': scene, 'triangles': sc.tris, 'draws': len(sc.draws), 'groups': len(sc.groups), 'size': size,
                       'mask_faces': mn, 'group_hist': sc.group_hist[1:], 'waited_quiet_s': waited, 'started': started,
                       'methods': {}}
                for m in methods:
                    calls, draws_ = call_counts(m, sc, mask, size)
                    row['methods'][m] = {'cpu_ms': M.median(cpu[m]), 'cpu_min': min(cpu[m]), 'cpu_max': max(cpu[m]),
                                         'gpu_ms': M.median(gpu[m]), 'gpu_min': min(gpu[m]), 'gpu_max': max(gpu[m]),
                                         'gl_calls': calls, 'draw_calls': draws_, 'check': checks[m]}
                row['busy_after'] = busy_processes(); row['finished'] = time.strftime('%H:%M:%S')
                with out.open('a') as fh:
                    fh.write(json.dumps(row) + '\n')
                print(scene, size, mn, ' '.join('%s cpu %.2f gpu %.3f %s' % (m, row['methods'][m]['cpu_ms'], row['methods'][m]['gpu_ms'],
                      'ok' if checks[m]['ok'] else 'DIFF') for m in methods), flush=True)
            fn('glDeleteFramebuffers', None, I, C.POINTER(U))(1, C.byref(U(atlas.fbo)))
            fn('glDeleteTextures', None, I, C.POINTER(U))(1, C.byref(U(atlas.tex)))
    print('done', flush=True)


main()

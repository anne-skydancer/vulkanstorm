# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Lamp shadows L3 (the light-through layer), no GL and no numpy: the GLSL lines are pinned to their C++ twins in
indra/newview/lllightthrough.h (which indra/newview/tests/lllightthrough_test.cpp checks), and the plumbing is checked in the
sources: the limits (solid 0.98, invisible 1/255, no caster 1) are the same numbers in the header, the caster shader, the depth
pass's constant and the glTF blend shader; the light through one face, the tap and its combine, the reference and the depth store are
the same expressions (the reference is the depth lookup's); the lookup's samplers are arguments; an opaque-only lamp keeps the depth's
value; every blend bind of the depth pass sets the flag before its draw and a rigged face keeps the depth rule; the caster pass is
depth-tested against the pool's static depth with its own face rule and scissor; the caster programs exist only with the projector pool
and have no rigged variant; the silhouette key carries a blended face's colour. The GPU gates are scripts/shadows/light_through_gate.py
(the programs as the viewer links them, the caster pass, the lookup and the spot program bit for bit against the base, the layer's
lookup against the CPU twin), the CPU twin's gates scripts/shadows/lamp_filter_twin.py --l3.

    python3 scripts/tests/test_light_through_twin.py"""
import re
import sys
import unittest
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_shadow_alpha_permutation import evaluate, significant  # noqa: E402

HEADER = ROOT / 'indra/newview/lllightthrough.h'
SHADERS = ROOT / 'indra/newview/app_settings/shaders'
CASTER = SHADERS / 'class1/deferred/shadowLightThroughF.glsl'
UTIL = SHADERS / 'class1/deferred/shadowStochasticUtil.glsl'
SPOT = SHADERS / 'class3/deferred/spotLightF.glsl'
MASK = SHADERS / 'class1/deferred/shadowAlphaMaskF.glsl'
BLEND = SHADERS / 'class1/deferred/pbrShadowAlphaBlendF.glsl'
PIPELINE = ROOT / 'indra/newview/pipeline.cpp'
MGR = ROOT / 'indra/newview/llviewershadermgr.cpp'
POOL = ROOT / 'indra/newview/llprojectorshadows.cpp'
LAYER = ROOT / 'indra/newview/lllightthroughgl.cpp'
POOL_H = ROOT / 'indra/newview/llprojectorshadowpool.h'
FEED = ROOT / 'indra/newview/llshadowdirtyfeed.cpp'


def read(path):
    return path.read_text(encoding='utf-8').replace('\r\n', '\n')


def func(text, sig):
    i = text.index(sig)
    return text[i:text.index('\n}\n', i) + 3]


def number(expr):
    """'0.98', '0.98f', '1.0f / 255.0f', '(1.0 / 255.0)' -> an exact fraction"""
    expr = expr.strip().strip('()').replace('f', '')
    parts = [p.strip() for p in expr.split('/')]
    value = Fraction(parts[0])
    for p in parts[1:]:
        value /= Fraction(p)
    return value


def norm(expr):
    """an expression without float suffixes and spaces, the C++ names mapped to the shader's"""
    expr = re.sub(r'(\d)f\b', r'\1', expr)
    return re.sub(r'\s+', '', expr)


class LightThroughTwin(unittest.TestCase):
    def setUp(self):
        self.header = read(HEADER)
        self.caster = read(CASTER)
        self.util = read(UTIL)

    def header_constant(self, name):
        m = re.search(r'constexpr float %s = ([^;]+);' % name, self.header)
        self.assertIsNotNone(m, name)
        return number(m.group(1))

    def shader_define(self, text, name):
        m = re.search(r'^#define %s (\S.*?)\s*(//.*)?$' % name, text, re.M)
        self.assertIsNotNone(m, name)
        return number(m.group(1))

    def test_limits_are_one_number_everywhere(self):
        solid = self.header_constant('kSolidAlpha')
        self.assertEqual(solid, Fraction('0.98'))   # the 2026-11-14 decision
        self.assertEqual(self.shader_define(self.caster, 'LIGHT_THROUGH_SOLID'), solid)
        self.assertEqual(self.header_constant('kInvisibleAlpha'), Fraction(1, 255))
        self.assertEqual(self.shader_define(self.caster, 'LIGHT_THROUGH_INVISIBLE'), Fraction(1, 255))
        # no caster: 1, the far plane, in the header, the layer's clear and the lookup's tap
        self.assertEqual(self.header_constant('kNoLayer'), 1)
        self.assertIn('const GLfloat kDepthClear = lllightthrough::kNoLayer;', read(LAYER))
        self.assertIn('vec4 has = vec4(lessThan(z, vec4(1.0)));', func(self.util, 'vec4 stThroughTap('))
        # the depth pass draws a blend texel from the same cut: the shared mask/blend shader's uniform and the glTF shader's literal
        m = re.search(r'SHADOW_BLEND_SOLID_ALPHA = ([0-9.]+f);', read(PIPELINE))
        self.assertEqual(number(m.group(1)), solid)
        self.assertIn('if (alpha < 0.98)', evaluate(read(BLEND), True))

    def test_rule_is_the_header_rule(self):
        cpp = re.search(r'inline float transmittance\(float a, float c\)\n\{(.*?)\n\}', self.header, re.S).group(1)
        cpp = re.search(r'return (.*?);', cpp).group(1)
        glsl = re.search(r'frag_data\[0\] = vec4\((.*?), 1\.0\);', self.caster).group(1)
        self.assertEqual(norm(glsl).replace('alpha', 'a'), norm(cpp))
        self.assertEqual(norm(cpp), '(1.0-a)*(1.0+a*c)')
        # the limits as the C++ tests them: invisible with NaN, solid from the cut
        self.assertIn('if (!(alpha > LIGHT_THROUGH_INVISIBLE) || alpha >= LIGHT_THROUGH_SOLID)', self.caster)
        self.assertIn('return !(a > kInvisibleAlpha);', self.header)
        self.assertIn('return a >= kSolidAlpha;', self.header)
        # the colour is clamped as the C++ clamps it
        self.assertIn('vec3 c = clamp(colour, vec3(0.0), vec3(1.0));', self.caster)
        # the depth store: the window depth as it is, into R32F (no conversion, so no step)
        self.assertIn('frag_data[1] = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);', self.caster)
        self.assertNotIn('DEPTH_STEP', self.caster)
        self.assertNotIn('storedDepth', self.header)
        self.assertIn('GL_R32F, size, size, GL_R32F, nullptr, GL_RED, GL_FLOAT', read(LAYER))

    def test_caster_reads_colour_as_the_alpha_passes(self):
        self.assertIn('vec3 colour = vertex_color.rgb * srgb_to_linear(tap.rgb);', self.caster)   # pbralphaF.glsl
        self.assertIn('vec3 colour = srgb_to_linear(tap.rgb * vertex_color.rgb);', self.caster)   # alphaF.glsl
        self.assertEqual(self.caster.count('float alpha = tap.a * vertex_color.a;'), 2)
        gltf = self.caster[self.caster.index('#if defined(LIGHT_THROUGH_GLTF)\n    //'):]
        self.assertLess(gltf.index('srgb_to_linear(tap.rgb);'), gltf.index('#else'))

    def test_tap_is_the_header_tap(self):
        tap = func(self.util, 'vec4 stThroughTap(')
        cpp = re.search(r'inline Tap throughTap\(.*?\n\}\n', self.header, re.S).group(0)
        # per texel: a caster (zt < kNoLayer), open (the depth atlas at or behind the reference: the depth filter's lit test), in front
        self.assertIn('vec4 has = vec4(lessThan(z, vec4(1.0)));', tap)
        self.assertIn('vec4 open = vec4(greaterThanEqual(s, ref));', tap)
        self.assertIn('const bool has = zt[i] < kNoLayer;', cpp)
        self.assertIn('const bool open = s[i] >= ref[i];', cpp)
        self.assertIn('float w_front = dot(open * has * vec4(lessThan(z, ref)), bw);', tap)
        self.assertIn('w_front += open && has && zt[i] < ref[i] ? bw[i] : 0.f;', cpp)
        self.assertIn('float r = w_all > 0.0 ? w_front / w_all : 0.0;', tap)
        self.assertIn('const float r = w_all > 0.f ? w_front / w_all : 0.f;', cpp)
        self.assertIn('return vec4(w_open + r * (tb - 1.0), w_open);', tap)
        self.assertIn('tap.lit = w_open + r * (tb - 1.0f);', cpp)
        # the combine
        flt = func(self.util, 'vec3 stThroughFilter(')
        self.assertIn('return open > 0.0 ? clamp(lit / open, vec3(0.0), vec3(1.0)) : vec3(1.0); // combine', flt)
        self.assertIn('return open > 0.f ? clamp01(lit / open) : 1.0f;', self.header)
        # the read stays inside the tile: clamped half a texel in, the gather's corner at most two texels from the far edge; the layer
        # and the depth atlas are gathered at the same corner
        self.assertIn('vec2 p = clamp(uv * n, vec2(0.5), vec2(n - 0.5));', tap)
        self.assertIn('vec2 b = min(floor(t), vec2(n - 2.0));', tap)
        self.assertIn('vec4 z = textureGather(zt, corner);', tap)
        self.assertIn('vec4 s = textureGather(depth, corner);', tap)

    def test_reference_is_the_depth_lookups(self):
        # texelRef: the receiver's depth less the bias, lowered (never raised) by the plane, at most rp_max, as stSpotQuadTile's ref
        quad = func(self.util, 'vec4 stSpotQuadTile(')
        self.assertIn('ref = c.z - z_bias + clamp(g.x * dx + g.y * dy, -rp_max, 0.0);', quad)
        ref = func(self.util, 'vec4 stThroughRef(')
        self.assertIn('return pl.r0 + clamp(dot(pl.gt, b + 0.5) - pl.gc + pl.k, -pl.rp_max, 0.0);', ref)
        setup = func(self.util, 'StThroughPlane stThroughPlane(')
        for line in ('pl.gt = g / tile.z;', 'pl.gc = dot(pl.gt, c.xy * tile.z);', 'pl.k = vec4(pl.gt.y, pl.gt.x + pl.gt.y, pl.gt.x, 0.0);',
                     'pl.r0 = c.z - z_bias;', 'pl.rp_max = rp_max;'):
            self.assertIn(line, setup)
        self.assertIn('return cz - z_bias + std::min(std::max(plane, -rp_max), 0.f);', self.header)
        # the plane's cap is the depth filter's
        cap = 'z_tex * max(SHADOW_PLANE_BIAS_MAX_TEXELS, ST_SPOT_PLANE_SLOPE * (max(rs.x, rs.y) + 1.0))'
        self.assertIn(cap, func(self.util, 'float stSpotFilterTile('))
        self.assertIn(cap, func(self.util, 'vec3 stThroughFilter('))
        # the search and every tap use it
        flt = func(self.util, 'vec3 stThroughFilter(')
        self.assertIn('lessThan(z, stThroughRef(pl, b))', flt)
        self.assertIn('vec4 ref = stThroughRef(pl, b);', func(self.util, 'vec4 stThroughTap('))

    def test_samplers_are_arguments(self):
        # the point-light faces call the same two functions with their own pool's layer: no global sampler inside them
        for sig in ('vec4 stThroughTap(', 'vec3 stThroughFilter('):
            body = func(self.util, sig)
            self.assertNotIn('proj_through_atlas', body, sig)
            self.assertNotIn('proj_through_depth', body, sig)
            self.assertNotIn('proj_shadow_atlas', body, sig)
            self.assertIn('sampler2D tau, sampler2D zt, sampler2D depth', body.splitlines()[0], sig)
        # both are inside the G1 block (compiled by the G1 scripts with their stubs)
        g1 = self.util[self.util.index('// <G1-BEGIN>'):self.util.index('// <G1-END>')]
        self.assertIn('vec3 stThroughFilter(', g1)
        self.assertIn('vec4 stThroughTap(', g1)
        self.assertIn('vec4 stThroughRef(', g1)

    def test_lookup_keeps_an_opaque_lamp(self):
        sample = func(self.util, 'vec3 sampleProjectorShadow(')
        self.assertIn('vec3 vis = vec3(shadow);', sample)
        self.assertIn('if (proj_through != 0 && shadow > 0.0)', sample)
        self.assertIn('vis *= stThroughFilter(proj_through_atlas, proj_through_depth, proj_shadow_atlas, tile, c_tile, grad, SHADOW_BIAS_TEXELS * z_tex, z_tex, pen,', sample)
        self.assertIn('return mix(vec3(1.0), vis, fade);', sample)
        self.assertNotIn('return 1.0;', sample)
        # the filter's numbers are the depth filter's: the search disc and radius, the floor, the cap, the weights
        flt = func(self.util, 'vec3 stThroughFilter(')
        self.assertIn('vec2 rs = clamp(pen * c.z, vec2(ST_SPOT_KERNEL_MIN), kmax);', flt)
        self.assertIn('vec2 rf = clamp(pen * (c.z - z_near), vec2(ST_SPOT_KERNEL_MIN), kmax);', flt)
        self.assertIn('float w = 1.0 - 0.9 * (float(k) + 0.5) / float(ST_THROUGH_TAPS);', flt)
        self.assertIn('if (z_near < 0.0)\n    {\n        return vec3(1.0);', flt)
        spot = read(SPOT)
        self.assertIn('vec3 sampleProjectorShadow(vec3 pos, vec3 norm, vec2 pos_screen);', spot)
        self.assertIn('vec3 shadow = vec3(1.0);', spot)
        # the Legacy branch of the spot program keeps its float
        legacy = spot[spot.index('#else\n    float shadow = 1.0;'):]
        self.assertIn('shadow = clamp(shadow, 0.0, 1.0);', legacy)

    def test_depth_pass_leaves_the_layer_its_texels(self):
        for path in (MASK, BLEND):
            text = read(path)
            self.assertNotIn('shadow_through', evaluate(text, False), path.name)   # Legacy: the stock text
            on = significant(evaluate(text, True).splitlines())
            at = on.index('if (shadow_through != 0)')
            dither = next(i for i, l in enumerate(on) if l.startswith('float ign = '))
            self.assertLess(at, dither, path.name + ': the layer flag after the dither')
            self.assertEqual(on[at + 1:at + 4], ['{', 'discard;', '}'], path.name)

    def test_pipeline_sets_the_flag_on_every_bind(self):
        pipe = read(PIPELINE)
        start = pipe.index('void LLPipeline::renderAlphaObjects(bool rigged, bool through)')
        body = pipe[start:pipe.index('\n}\n', start)]
        binds = list(re.finditer(r'gDeferredShadow(?:GLTFAlphaBlend|AlphaMask)Program\.bind\(rigged\);', body))
        self.assertEqual(len(binds), 4)
        for m in binds:
            tail = body[m.end():]
            nxt = re.search(r'\.bind\(', tail)
            tail = tail[:nxt.start()] if nxt else tail
            flag = re.search(r'uniform1i\(LLShaderMgr::SHADOW_THROUGH, ([^)]+)\);', tail)
            self.assertIsNotNone(flag, m.group(0))
            draw = re.search(r'push\w*Batch', tail)
            self.assertLess(flag.start(), draw.start(), m.group(0) + ': the flag after the draw')
        rig = body[body.index('        if (rigged)\n'):body.index('        else\n        {\n            if (pparams->mGLTFMaterial)')]
        self.assertEqual(rig.count('uniform1i(LLShaderMgr::SHADOW_THROUGH, 0);'), 2)   # a rigged face keeps the depth rule
        self.assertIn('renderAlphaObjects(rigged, light_through && !rigged);', pipe)
        shadow = pipe[pipe.index('void LLPipeline::renderShadow('):]
        shadow = shadow[:shadow.index('\n}\n')]
        self.assertIn('LLLocalShadowGL::throughTarget() : nullptr;', shadow)
        self.assertIn('sLocalShadowPass == LOCAL_SHADOW_STATIC ? LLLocalShadowGL::throughTarget()', shadow)
        # the layer is drawn after every depth caster (the last glTF mask batches), the colour mask going back to the pass's
        self.assertLess(shadow.index('mAlphaMaskPool->pushGLTFBatches(type);'), shadow.index('renderLightThroughObjects('))
        self.assertIn('renderLightThroughObjects(*through_target->layer, through_target->rect, RenderShadowDetail > 2);', shadow)
        # the layer's casters are the depth pass's blend faces that are not rigged
        through = pipe[pipe.index('void LLPipeline::renderLightThroughObjects('):]
        through = through[:through.index('\n}\n')]
        self.assertIn('if (pparams->mAvatar != nullptr)', through)
        self.assertIn('LLRenderPass::PASS_ALPHA', through)

    def test_caster_pass_state(self):
        layer = read(LAYER)
        # the pool's static depth is the framebuffer's depth attachment, read only: the casters behind the tile's opaque depth stay out
        self.assertIn('glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthAtlas, 0);', func(layer, 'bool LLLightThroughLayer::ensure('))
        scope = layer[layer.index('LLLightThroughLayer::DrawScope::DrawScope('):]
        scope = scope[:scope.index('\n}\n')]
        self.assertIn('mDepth(GL_TRUE, GL_FALSE, GL_LEQUAL)', scope)   # depth test, no writes
        self.assertIn('mCull(GL_CULL_FACE)', scope)                    # its own face rule: back faces culled, whatever the caller's
        self.assertIn('mScissor(GL_SCISSOR_TEST)', scope)
        self.assertIn('glScissor((GLint)rect.x, (GLint)rect.y, (GLsizei)rect.size, (GLsizei)rect.size);', scope)
        self.assertIn('gGLViewport[0] == (S32)rect.x', scope)          # the rect is the caller's viewport (asserted)
        # bind takes the pool's own uniforms
        self.assertIn('void LLLightThroughLayer::bind(LLGLSLShader& shader, S32 tau_uniform, S32 depth_uniform) const', layer)
        # a carry never copies between rects of two sizes
        self.assertIn('if (move.copy && move.src.size == move.dst.size && holds(move.key))', self.header)
        self.assertIn('if (move.src.size == move.dst.size && new_pair(move.src.size, held[i].tau, held[i].depth))', layer)
        # the programs' failures stop a development build (Release logs and goes on)
        mgr = read(MGR)
        for prog in ('gDeferredShadowLightThroughProgram', 'gDeferredShadowGLTFLightThroughProgram'):
            fail = mgr[mgr.index('if (!%s.createShader())' % prog):]
            fail = fail[:fail.index('}')]
            self.assertIn('llassert(false);', fail, prog)

    def test_silhouette_key_carries_a_blended_faces_colour(self):
        pool = read(POOL_H)
        self.assertIn('uint8_t rgb[3] = { 255, 255, 255 };', pool)
        self.assertIn('key = silhouetteKeyMix(mix(key, f.tint), f.rgb, sizeof(f.rgb));', pool)
        feed = read(FEED)
        self.assertIn('f.tint = LLPipeline::getPoolTypeFromTE(te, volume->getTEImage(i)) == LLDrawPool::POOL_ALPHA;', feed)
        # the vertex colour as LLFace writes it: the face's tint, a glTF material's base colour over it
        self.assertIn('LLColor4U rgb = te->getColor(); // the same conversions as LLFace\'s', feed)
        self.assertIn('rgb = render_gltf->mBaseColor;', feed)

    def test_programs_only_with_the_pool_and_never_rigged(self):
        mgr = read(MGR)
        for prog in ('gDeferredShadowLightThroughProgram', 'gDeferredShadowGLTFLightThroughProgram'):
            self.assertNotIn('make_rigged_variant(%s' % prog, mgr)
            first = mgr.index('%s.mName' % prog)
            guard = mgr.rindex('if (success && use_projector_shadow_pool())', 0, first)
            self.assertLess(first - guard, 200, prog)
            self.assertIn('%s.unload();' % prog, mgr)
        glt = mgr[mgr.index('gDeferredShadowGLTFLightThroughProgram.mName'):]
        self.assertIn('gDeferredShadowGLTFLightThroughProgram.addPermutation("LIGHT_THROUGH_GLTF", "1");', glt[:1500])

    def test_pool_keeps_every_drawn_rect(self):
        pool = read(POOL)
        tile = func(pool, 'void LLProjectorShadows::renderTile(')
        # cleared before the static render, named to it, read after it
        self.assertLess(tile.index('mThrough.clearRect(rec.rect);'), tile.index('gPipeline.renderShadow('))
        self.assertLess(tile.index('LLLocalShadowGL::setThroughTarget(&mThrough, rec.rect);'), tile.index('gPipeline.renderShadow('))
        self.assertLess(tile.index('gPipeline.renderShadow('), tile.index('rec.through = LLLocalShadowGL::endThroughTarget();'))
        plan = func(pool, 'void LLProjectorShadows::applyPlan(')
        self.assertIn('lllightthrough::planCarry(plan.moves', plan)
        free_atlases = func(pool, 'void LLProjectorShadows::freeAtlases(')
        self.assertLess(free_atlases.index('mThrough.free();'), free_atlases.index('deleteTexture(mAtlasS)'))   # the layer before its depth attachment
        self.assertIn('mThrough.setAtlas(size, mAtlasS);', func(pool, 'bool LLProjectorShadows::allocateAtlases('))
        self.assertIn('mThrough.freeStorage();', func(pool, 'void LLProjectorShadows::update('))   # an idle layer goes
        light = func(pool, 'void LLProjectorShadows::setupLight(')
        self.assertIn('shader.uniform1i(LLShaderMgr::PROJ_THROUGH, through && mThrough.allocated() ? 1 : 0);', light)
        self.assertIn('mThrough.bind(shader, LLShaderMgr::PROJ_THROUGH_ATLAS, LLShaderMgr::PROJ_THROUGH_DEPTH);', func(pool, 'void LLProjectorShadows::bindAtlas('))


if __name__ == '__main__':
    unittest.main()

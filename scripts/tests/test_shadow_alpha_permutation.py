# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The semi-transparent shadow casters keep Firestorm 7.2.5's stipple in their Legacy permutation and take the Aurora
Viewer's caster rule only under the Stochastic shadow method (define STOCHASTIC_SHADOW_ALPHA).

With the define absent each of the four shaders must equal the stock text stored in
scripts/shadows/legacy_reference/NAME.glsl.txt (comments and blank lines ignored). With it present, the stipple
is gone and the blended faces call the rule (shadowStochasticCasterF.glsl, with the triangle's seed from
shadowStochasticCasterV.glsl) with no cut; mask faces keep the creator's cutoff and the stock floor. Every program in
llviewershadermgr.cpp that links one of them adds the define inside `if (use_stochastic_shadows())`, and nowhere else.
The rule's lines are pinned to the C++ twin in indra/newview/tests/llstochastichistory_test.cpp (golden values,
properties), and the pipeline gives the rule its per-draw seed, the engine's table and the frame of its pattern.

    python3 scripts/tests/test_shadow_alpha_permutation.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_shadow_alpha_permutation.py   (another source tree, e.g. a
        `git archive` of another line, to see whether the test would hold there)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
SHADERS = ROOT / 'indra/newview/app_settings/shaders/class1/deferred'
REFERENCE = ROOT / 'scripts/shadows/legacy_reference'
MGR = ROOT / 'indra/newview/llviewershadermgr.cpp'
SHADERMGR = ROOT / 'indra/llrender/llshadermgr.cpp'
SHADERMGR_H = ROOT / 'indra/llrender/llshadermgr.h'
PIPELINE = ROOT / 'indra/newview/pipeline.cpp'
AVATAR_POOL = ROOT / 'indra/newview/lldrawpoolavatar.cpp'
LOCAL_SHADOWS = ROOT / 'indra/newview/lllocalshadows.cpp'
PROJECTORS = ROOT / 'indra/newview/llprojectorshadows.cpp'
STATE = ROOT / 'indra/newview/llstochasticstate.h'
FILES = ['shadowAlphaMaskF', 'pbrShadowAlphaBlendF', 'avatarAlphaShadowF', 'avatarAlphaMaskShadowF']
VERTEX = {'shadowAlphaMaskV': ['shadowAlphaMaskF'], 'pbrShadowAlphaMaskV': ['pbrShadowAlphaBlendF'],
          'avatarAlphaShadowV': ['avatarAlphaShadowF', 'avatarAlphaMaskShadowF']}
CASTER_F = 'shadowStochasticCasterF'
CASTER_V = 'shadowStochasticCasterV'
DEFINE = 'STOCHASTIC_SHADOW_ALPHA'
OURS = re.compile(r'#\s*if\s+(!?)\s*defined\(' + DEFINE + r'\)$')
RULE_CALL = 'shadow_caster_keeps(alpha, shadow_caster_tri)'


def significant(lines):
    out = []
    for line in lines:
        t = re.sub(r'\s+', ' ', line.strip())
        if t and not t.startswith('//'):
            out.append(t)
    return out


def evaluate(text, defined):
    """The text as the GLSL preprocessor sees it for the conditionals on DEFINE, with the define present or
    absent. Every other conditional is kept, and is dropped with a branch that is not taken."""
    out, stack = [], []   # entries: [kind, taken]; kind 'ours' (a conditional on DEFINE) or 'other'
    for line in text.splitlines():
        t = line.strip()
        m = OURS.match(t)
        if m:
            stack.append(['ours', defined != bool(m.group(1))])
            continue
        if t.startswith('#if'):
            stack.append(['other', None])
        elif t == '#else' and stack and stack[-1][0] == 'ours':
            stack[-1][1] = not stack[-1][1]
            continue
        elif t == '#endif':
            kind, _ = stack.pop()
            if kind == 'ours':
                continue
        if all(taken for kind, taken in stack if kind == 'ours'):
            out.append(line)
    assert not stack, 'unbalanced conditionals'
    return '\n'.join(out)


def code(text):
    """The text without its comments (line and block), for the checks of what a shader does."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return '\n'.join(line.split('//', 1)[0] for line in text.splitlines())


def subsets():
    """The 16 subsets of 4 bits by popcount: entries, first entry of each size, count (the rule's packed tables)."""
    entries, off, cnt = [], [], []
    for r in range(5):
        off.append(len(entries))
        cnt.append(0)
        for m in range(16):
            if bin(m).count('1') == r:
                entries.append(m)
                cnt[r] += 1
    return entries, off, cnt


def pack(values):
    return sum(v << (4 * i) for i, v in enumerate(values))


def function_body(text, name):
    start = text.index(name)
    end = text.index('\n}', start) + 2
    return text[start:end]


class ShadowAlphaPermutation(unittest.TestCase):
    def read(self, name):
        return (SHADERS / (name + '.glsl')).read_text(encoding='utf-8').replace('\r\n', '\n')

    def src(self, path):
        return path.read_text(encoding='utf-8').replace('\r\n', '\n')

    def test_evaluate_handles_nested_conditionals(self):
        text = '\n'.join(['a', '#if defined(%s)' % DEFINE, 'b', '#if X', 'c', '#endif', '#else', 'd', '#if Y',
                          'e', '#endif', '#endif', 'f'])
        self.assertEqual(evaluate(text, True).split('\n'), ['a', 'b', '#if X', 'c', '#endif', 'f'])
        self.assertEqual(evaluate(text, False).split('\n'), ['a', 'd', '#if Y', 'e', '#endif', 'f'])
        neg = '\n'.join(['#if !defined(%s)' % DEFINE, 'x', '#else', 'y', '#endif'])
        self.assertEqual(evaluate(neg, False), 'x')
        self.assertEqual(evaluate(neg, True), 'y')
        # an unrelated #else is not ours to flip
        other = '\n'.join(['#if X', 'p', '#else', 'q', '#endif'])
        self.assertEqual(evaluate(other, True), other)

    def test_legacy_permutation_is_stock_text(self):
        for name in FILES:
            stock = (REFERENCE / (name + '.glsl.txt')).read_text(encoding='utf-8')
            self.assertEqual(significant(evaluate(self.read(name), False).splitlines()),
                             significant(stock.splitlines()), name)

    def test_legacy_vertex_shaders_are_unchanged_without_the_define(self):
        # the triangle's seed is added under the define only: without it the vertex shaders have no trace of the rule
        for name in VERTEX:
            off = evaluate(self.read(name), False)
            self.assertNotIn('shadow_caster', off, name)
            self.assertNotIn('flat out uint', off, name)

    def test_stochastic_branch_has_the_rule_and_no_stipple_nor_cut(self):
        for name in FILES:
            on = code(evaluate(self.read(name), True))
            self.assertNotIn('floor(target_pos_x', on, name)   # the stipple's use
            self.assertNotIn('52.9829189', on, name)           # the former shared dither (one field for every layer: max, not product)
            self.assertNotIn('0.98', on, name)                 # D4: no solid cut
            self.assertNotIn('0.88', on, name)
            self.assertIn('flat in uint shadow_caster_tri;', on, name)
            self.assertIn('bool shadow_caster_keeps(float alpha, uint tri);', on, name)
            self.assertEqual(on.count(RULE_CALL), 1, name)
        # the blended-only shaders: the rule below full opacity, nothing else decides
        for name in ('pbrShadowAlphaBlendF', 'avatarAlphaShadowF', 'avatarAlphaMaskShadowF'):
            on = code(evaluate(self.read(name), True))
            self.assertIn('if (alpha < 1.0 && !' + RULE_CALL + ')', on, name)
            self.assertNotIn('0.05', on, name)                  # D4: no floor
            self.assertNotIn('minimum_alpha)', on, name)        # no threshold but full opacity
        # the avatar mask pass draws the same meshes as the blend pass: the same alpha (texture times mesh colour)
        for name in ('avatarAlphaShadowF', 'avatarAlphaMaskShadowF'):
            on = significant(code(evaluate(self.read(name), True)).splitlines())
            self.assertTrue(any('color.a' in l for l in on), name)
        mask_on = code(evaluate(self.read('avatarAlphaMaskShadowF'), True))
        self.assertIn('uniform vec4 color;', mask_on)
        self.assertIn('alpha *= color.a;', mask_on)

    def test_mask_faces_keep_their_cutoff_and_the_stock_floor(self):
        # shadowAlphaMaskF draws mask faces (shadow_solid_alpha 0: the creator's cutoff, the stock floor, solid) and blend
        # faces (shadow_solid_alpha 1, minimum_alpha 0: the rule below full opacity); the mask path is the one it was, so a map
        # without blended casters is unchanged
        on = significant(code(evaluate(self.read('shadowAlphaMaskF'), True)).splitlines())
        i_min = on.index('if (alpha < minimum_alpha)')
        i_vtx = on.index('alpha *= vertex_color.a;')
        i_mask = on.index('if (shadow_solid_alpha <= 0.0)')
        i_cut = on.index('if (alpha < 0.05)')
        i_rule = on.index('else if (alpha < shadow_solid_alpha && !' + RULE_CALL + ')')
        self.assertLess(i_min, i_vtx)
        self.assertLess(i_vtx, i_mask)
        self.assertLess(i_mask, i_cut)
        self.assertLess(i_cut, i_rule)
        self.assertEqual(on[i_cut + 1:i_cut + 4], ['{', 'discard;', '}'])
        self.assertIn('uniform float shadow_solid_alpha;', on)
        self.assertNotIn('shadow_solid_alpha', evaluate(self.read('shadowAlphaMaskF'), False))

    def test_the_rule_is_the_twin_s(self):
        # shadowStochasticCasterF.glsl, line for line what mirror::casterKeeps and mirror::casterLayer of the C++ test do
        text = code(self.read(CASTER_F))
        body = significant(function_body(text, 'bool shadow_caster_keeps(float alpha, uint tri)').splitlines())
        self.assertEqual(body, [
            'bool shadow_caster_keeps(float alpha, uint tri)',
            '{',
            'ivec2 cell = ivec2(floor(gl_FragCoord.xy - shadow_tile_origin));',
            'uvec2 block = uvec2(cell >> 1) & 0xffffu;',
            'uint layer = stoch_hash(stoch_hash(uint(st_seed) * 0x9e3779b9u + 0x7f4a7c15u) ^ '
            'stoch_hash(tri * 2u + (gl_FrontFacing ? 0u : 1u) + 0x632be5abu));',
            'uint seed = stoch_frame_seed(layer, shadow_caster_frame);',
            'float xi = float(stoch_xi24(block, layer, shadow_caster_frame, SHADOW_CASTER_TABLE_S)) / 16777216.0;',
            'float pick = float(stoch_hash(seed ^ stoch_hash(block.x | (block.y << 16u)) ^ 0x5bd1e995u) >> 8u) / 16777216.0;',
            'uint r = min(uint(floor(clamp(alpha, 0.0, 1.0) * 4.0 + xi)), 4u);',
            'uint cnt = (SHADOW_CASTER_CNT >> (4u * r)) & 0xfu;',
            'uint i = ((SHADOW_CASTER_OFF >> (4u * r)) & 0xfu) + min(uint(pick * float(cnt)), cnt - 1u);',
            'uint subset = ((i >= 8u ? SHADOW_CASTER_HI : SHADOW_CASTER_LO) >> (4u * (i & 7u))) & 0xfu;',
            'uint bit = uint(cell.x & 1) | (uint(cell.y & 1) << 1u);',
            'return (subset & (1u << bit)) != 0u;',
            '}'])
        # the packed subsets are the generated ones (the Aurora Viewer's SST_* constants)
        entries, off, cnt = subsets()
        consts = dict(re.findall(r'#define (SHADOW_CASTER_\w+) 0x([0-9A-Fa-f]+)u', text))
        self.assertEqual(int(consts['SHADOW_CASTER_LO'], 16), pack(entries[:8]))
        self.assertEqual(int(consts['SHADOW_CASTER_HI'], 16), pack(entries[8:]))
        self.assertEqual(int(consts['SHADOW_CASTER_OFF'], 16), pack(off))
        self.assertEqual(int(consts['SHADOW_CASTER_CNT'], 16), pack(cnt))
        # the tile sits after the S + 1 mask rows of the engine's table, S = TRANSPARENCY_SAMPLES
        s = re.search(r'#define SHADOW_CASTER_TABLE_S (\d+)u', text).group(1)
        self.assertRegex(self.src(STATE), r'constexpr uint32_t TRANSPARENCY_SAMPLES = %s;' % s)
        for uniform in ('uniform vec2 shadow_tile_origin;', 'uniform int st_seed;', 'uniform int shadow_caster_frame;'):
            self.assertIn(uniform, text)

    def test_the_triangle_seed_is_the_engine_hash_of_the_provoking_vertex(self):
        vtext = code(self.read(CASTER_V))
        engine = code(self.read('stochasticSampleF'))
        squash = lambda s: re.sub(r'\s+', '', s)
        mine = function_body(vtext, 'uint shadow_caster_vertex_hash(uint v)').replace('shadow_caster_vertex_hash', 'stoch_hash')
        self.assertEqual(squash(mine), squash(function_body(engine, 'uint stoch_hash(uint v)')))
        self.assertIn('return shadow_caster_vertex_hash(shadow_caster_vertex_hash(shadow_caster_vertex_hash(bits.x) ^ bits.y) ^ bits.z);', vtext)
        for name in VERTEX:
            on = code(evaluate(self.read(name), True))
            self.assertIn('flat out uint shadow_caster_tri;', on, name)
            self.assertEqual(on.count('shadow_caster_tri = shadow_caster_vertex_seed(position);'), 1, name)

    def test_the_rule_is_attached_and_built_with_the_method(self):
        header = self.src(SHADERMGR_H)
        self.assertIn('#define STOCHASTIC_CASTER_VERTEX_OBJECT "deferred/%s.glsl"' % CASTER_V, header)
        self.assertIn('#define STOCHASTIC_CASTER_FRAGMENT_OBJECT "deferred/%s.glsl"' % CASTER_F, header)
        attach = self.src(SHADERMGR)
        self.assertIn('const bool stochastic_caster = shader->mDefines.count("%s") > 0;' % DEFINE, attach)
        self.assertIn('if (stochastic_caster && !shader->attachVertexObject(STOCHASTIC_CASTER_VERTEX_OBJECT))', attach)
        self.assertIn('if (stochastic_caster && !shader->attachFragmentObject(STOCHASTIC_CASTER_FRAGMENT_OBJECT))', attach)
        self.assertIn('if ((alpha_stochastic || stochastic_caster || shader->mDefines.count("STOCH_SAMPLE")) &&', attach)
        mgr = self.src(MGR)
        self.assertRegex(mgr, r'if \(use_stochastic_shadows\(\)\)\s*\{\s*shaders\.push_back\( make_pair\( STOCHASTIC_CASTER_VERTEX_OBJECT,')
        self.assertIn('helper == &STOCH_SAMPLE_HELPER  ? stochastic_transparency_shaders() || use_stochastic_shadows()', mgr)
        block = mgr[mgr.index('mFragmentShaderObjects.erase(STOCHASTIC_CASTER_FRAGMENT_OBJECT);'):]
        block = block[:block.index('// </Vulkanstorm>')]
        self.assertIn('if (use_stochastic_shadows())', block)
        self.assertIn('loadShaderFile(STOCHASTIC_CASTER_FRAGMENT_OBJECT, level, GL_FRAGMENT_SHADER, &attribs, -1) == 0', block)
        self.assertIn('return STOCHASTIC_CASTER_FRAGMENT_OBJECT;', block)

    def test_every_program_gets_the_permutation_under_the_stochastic_test(self):
        src = self.src(MGR)
        found = set()
        for name in FILES:
            for m in re.finditer(r'(g\w+)\.mShaderFiles\.push_back\(make_pair\("deferred/' + name + r'\.glsl"', src):
                prog = m.group(1)
                block = src[m.end():src.index('createShader', m.end())]
                self.assertRegex(block, r'if \(use_stochastic_shadows\(\)\)\s*\{[^{}]*\b' + prog +
                                 r'\.addPermutation\("' + DEFINE + r'", "1"\);',
                                 '%s links %s' % (prog, name))
                # make_rigged_variant copies mDefines when it is called, so the define must come first
                rig = re.search(r'make_rigged_variant\(' + prog + r',', block)
                if rig:
                    self.assertLess(block.index('addPermutation("' + DEFINE), rig.start(),
                                    '%s: the define is added after make_rigged_variant' % prog)
                found.add(prog)
        self.assertEqual(found, {'gDeferredShadowFullbrightAlphaMaskProgram', 'gDeferredShadowAlphaMaskProgram',
                                 'gDeferredShadowGLTFAlphaBlendProgram', 'gDeferredAvatarAlphaShadowProgram',
                                 'gDeferredAvatarAlphaMaskShadowProgram'})

    def test_define_is_added_only_there(self):
        src = self.src(MGR)
        guarded = re.findall(r'if \(use_stochastic_shadows\(\)\)\s*\{[^{}]*\.addPermutation\("' + DEFINE + r'", "1"\);', src)
        self.assertEqual(len(re.findall(r'addPermutation\("' + DEFINE + '"', src)), len(guarded), 'the define is added outside the stochastic test')
        start = src.index('add_common_permutations(LLGLSLShader')
        common = src[start:src.index('\n}\n', start)]
        self.assertNotIn(DEFINE, common)

    def test_solid_threshold_is_one_for_blend_and_zero_for_mask(self):
        # Stochastic: the shared mask/blend shader runs the rule below shadow_solid_alpha, which the pipeline sets to 1 after
        # each blend bind (no solid cut, D4) and to 0 after each mask bind (the creator's cutoff, never the rule)
        pipe = self.src(PIPELINE)
        self.assertRegex(pipe, r'SHADOW_BLEND_SOLID_ALPHA = 1\.f;')
        self.assertRegex(pipe, r'SHADOW_MASK_SOLID_ALPHA = 0\.f;')
        call = r'sCurBoundShaderPtr->uniform1f\(LLShaderMgr::DEFERRED_SHADOW_SOLID_ALPHA, (\w+)\);'
        # every bind of the two shared programs is followed by the uniform, with the right value
        for m in re.finditer(r'(gDeferredShadow(?:Fullbright)?AlphaMaskProgram)\.bind\(rigged\);', pipe):
            tail = pipe[m.end():m.end() + 900]
            nxt = re.search(r'\.bind\(', tail)
            tail = tail[:nxt.start()] if nxt else tail
            got = re.search(call, tail)
            self.assertIsNotNone(got, m.group(0) + ' is not followed by the solid-alpha uniform')
            draw = re.search(r'pushBatch|renderMaskedObjects|renderFullbrightMaskedObjects', tail)
            self.assertIsNotNone(draw, m.group(0))
            self.assertLess(got.start(), draw.start(), m.group(0) + ': the uniform is set after the draw')
            want = 'SHADOW_BLEND_SOLID_ALPHA' if draw.group(0) == 'pushBatch' else 'SHADOW_MASK_SOLID_ALPHA'
            self.assertEqual(got.group(1), want, m.group(0))
        self.assertEqual(len(re.findall(call, pipe)), 4)
        self.assertEqual(len(re.findall(r'gDeferredShadow(?:Fullbright)?AlphaMaskProgram\.bind\(rigged\);', pipe)), 4)

    def test_classic_blend_faces_take_no_minimum_alpha_under_stochastic(self):
        # renderAlphaObjects: the classic blend faces' minimum_alpha is 0 under Stochastic (no cut, D4) and the stock
        # ALPHA_BLEND_CUTOFF (0.598) otherwise; the shader tests the texture alpha against it before the vertex alpha multiply
        pipe = self.src(PIPELINE)
        self.assertRegex(pipe, r'ALPHA_BLEND_CUTOFF = 0\.598f;')
        self.assertRegex(pipe, r'SHADOW_BLEND_MIN_ALPHA_STOCHASTIC = 0\.f;')
        start = pipe.index('void LLPipeline::renderAlphaObjects')
        body = pipe[start:pipe.index('\n}\n', start)]
        self.assertRegex(body, r'blend_minimum_alpha = RenderShadowMethod == LLShadowMethod::STOCHASTIC '
                               r'\? SHADOW_BLEND_MIN_ALPHA_STOCHASTIC : ALPHA_BLEND_CUTOFF;')
        # each classic (gDeferredShadowAlphaMaskProgram) bind sets it; the glTF blend binds keep the stock value
        binds = list(re.finditer(r'gDeferredShadowAlphaMaskProgram\.bind\(rigged\);', body))
        self.assertEqual(len(binds), 2)
        for m in binds:
            tail = body[m.end():m.end() + 900]
            self.assertIn('setMinimumAlpha(blend_minimum_alpha)', tail[:tail.index('pushBatch')])
        self.assertEqual(body.count('setMinimumAlpha(ALPHA_BLEND_CUTOFF)'), 2)   # the two glTF blend binds
        self.assertEqual(body.count('setMinimumAlpha(blend_minimum_alpha)'), 2)

    def test_every_blend_draw_gives_the_rule_its_seed_and_table(self):
        # the draw's identity (LLDrawInfo::mStochasticSeed) and the engine's table on the bound program's unit, before each
        # of the four draws of renderAlphaObjects, under Stochastic only
        pipe = self.src(PIPELINE)
        start = pipe.index('void LLPipeline::renderAlphaObjects')
        body = pipe[start:pipe.index('\n}\n', start)]
        self.assertIn('const bool stochastic_casters = RenderShadowMethod == LLShadowMethod::STOCHASTIC;', body)
        self.assertIn('shader->uniform1i(LLShaderMgr::ALPHA_ST_SEED, (S32)params.mStochasticSeed);', body)
        self.assertIn('LLStochasticEngine::bindMasks(*shader);', body)
        calls = [m.start() for m in re.finditer(r'stochastic_caster\(\*pparams\);', body)]
        self.assertEqual(len(calls), 4)
        for m in re.finditer(r'\.bind\(rigged\);', body):
            after = [c for c in calls if c > m.end()]
            self.assertTrue(after, 'a bind without the seed after it')
            draw = re.search(r'pushRiggedGLTFBatch|pushGLTFBatch|uploadMatrixPalette|pushBatch', body[m.end():])
            self.assertLess(after[0], m.end() + draw.start(), 'the seed is given after the draw')
        # the avatar alpha passes: the avatar's identity and the table, under Stochastic, for the two alpha passes only
        pool = self.src(AVATAR_POOL)
        start = pool.index('void LLDrawPoolAvatar::renderShadow')
        body = pool[start:pool.index('\n}\n', start)]
        self.assertIn('LLPipeline::RenderShadowMethod == LLShadowMethod::STOCHASTIC', body)
        self.assertIn('(pass == SHADOW_PASS_AVATAR_ALPHA_BLEND || pass == SHADOW_PASS_AVATAR_ALPHA_MASK)', body)
        self.assertIn('sVertexProgram->uniform1i(LLShaderMgr::ALPHA_ST_SEED, (S32)LLStochasticEngine::faceSeed(avatarp->getID().mData, 0, 0));', body)
        self.assertIn('LLStochasticEngine::bindMasks(*sVertexProgram);', body)

    def test_only_the_sun_cascades_rotate_the_pattern(self):
        # the frame of the pattern: the sun cascades take shadowCasterFrame(), every other setter (the reset after the cascades,
        # the projector tiles, the local shadows' scope) leaves it at 0, a fixed pattern
        pipe = self.src(PIPELINE)
        self.assertIn('const S32 caster_frame = shadowCasterFrame();', pipe)
        self.assertEqual(len(re.findall(r'LLLocalShadowGL::setTileOrigin\(origin\.x, origin\.y, caster_frame\);', pipe)), 1)
        for src in (pipe, self.src(PROJECTORS), self.src(LOCAL_SHADOWS)):
            for m in re.finditer(r'setTileOrigin\(([^;]*)\);', src):
                args = m.group(1)
                if 'caster_frame' in args or args.startswith('F32 x'):
                    continue
                self.assertEqual(args.count(','), 1, 'a fixed-pattern setter passes a frame: ' + m.group(0))
        start = pipe.index('S32 LLPipeline::shadowCasterFrame() const')
        body = pipe[start:pipe.index('\n}\n', start)]
        for cond in ('RenderShadowMethod != LLShadowMethod::STOCHASTIC', 'mRT != &mMainRT', 'gCubeSnapshot', 'gSnapshot',
                     '!mShadowSTHistory.isAllocated()'):
            self.assertIn(cond, body)
        self.assertIn('return (S32)(LLFrameTimer::getFrameCount() & 7) + 1;', body)
        local = self.src(LOCAL_SHADOWS)
        self.assertIn('static const LLStaticHashedString sCasterFrame("shadow_caster_frame");', local)
        self.assertIn('variant->uniform1i(sCasterFrame, frame);', local)

    def test_avatar_shadow_pass_never_sets_minimum_alpha(self):
        # The stock avatar alpha-shadow branch (the stipple) runs only for texels below minimum_alpha, which stays 0 in the
        # shadow pass, as in stock: in Legacy a texel from 0.05 up is solid. The Stochastic branch does not read minimum_alpha:
        # the rule casts in proportion below full opacity.
        src = self.src(AVATAR_POOL)
        start = src.index('void LLDrawPoolAvatar::beginShadowPass')
        end = src.index('void LLDrawPoolAvatar::endShadowPass')
        self.assertNotIn('setMinimumAlpha', src[start:end])
        start = src.index('void LLDrawPoolAvatar::renderShadow')
        end = src.index('\n}\n', start)
        self.assertNotIn('setMinimumAlpha', src[start:end])
        for name in ('avatarAlphaShadowF', 'avatarAlphaMaskShadowF'):
            on = code(evaluate(self.read(name), True))
            body = on[on.index('void main()'):]
            self.assertNotIn('minimum_alpha', body, name)


if __name__ == '__main__':
    unittest.main()

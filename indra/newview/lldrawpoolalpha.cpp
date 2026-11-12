/**
 * @file lldrawpoolalpha.cpp
 * @brief LLDrawPoolAlpha class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolalpha.h"

#include "llglheaders.h"
#include "llviewercontrol.h"
#include "llcriticaldamp.h"
#include "llfasttimer.h"
#include "llrender.h"

#include "llcubemap.h"
#include "llsky.h"
#include "lldrawable.h"
#include "llface.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"    // For debugging
#include "llviewerobjectlist.h" // For debugging
#include "llviewerwindow.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llviewerregion.h"
#include "lldrawpoolwater.h"
#include "llspatialpartition.h"
#include "llglcommonfunc.h"
#include "llvoavatar.h"
#include "gltfscenemanager.h"

#include "llenvironment.h"

bool LLDrawPoolAlpha::sShowDebugAlpha = false;
bool LLDrawPoolAlpha::sShowDebugAlphaRigged = false;

#define current_shader (LLGLSLShader::sCurBoundShaderPtr)

// <Vulkanstorm> stochastic transparency: whether a blended legacy material's alpha-only variant
// can glare, the condition materialF.glsl tests (env_intensity and the glossiness in
// specular_color.a, the uniforms renderAlpha gives it from the draw): its alpha is then
// max(diffuse alpha, glare) x vertex alpha and it shades in full, with the same inputs as the
// shaded pass, so that the coverage it gives agrees with the accumulated alpha
static bool alphaOnlyCanGlare(const LLDrawInfo& draw)
{
    return draw.mEnvIntensity > 0.f || draw.mSpecColor.mV[VALPHA] > 0.f;
}
// </Vulkanstorm>

LLVector4 LLDrawPoolAlpha::sWaterPlane;

// minimum alpha before discarding a fragment
static const F32 MINIMUM_ALPHA = 0.004f; // ~ 1/255

// minimum alpha before discarding a fragment when rendering impostors
static const F32 MINIMUM_IMPOSTOR_ALPHA = 0.1f;

LLDrawPoolAlpha::LLDrawPoolAlpha(U32 type) :
        LLRenderPass(type), target_shader(NULL),
        mColorSFactor(LLRender::BF_UNDEF), mColorDFactor(LLRender::BF_UNDEF),
        mAlphaSFactor(LLRender::BF_UNDEF), mAlphaDFactor(LLRender::BF_UNDEF)
{

}

LLDrawPoolAlpha::~LLDrawPoolAlpha()
{
}


void LLDrawPoolAlpha::prerender()
{
    mShaderLevel = LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

S32 LLDrawPoolAlpha::getNumPostDeferredPasses()
{
    return 1;
}

// set some common parameters on the given shader to prepare for alpha rendering
static void prepare_alpha_shader(LLGLSLShader* shader, bool deferredEnvironment, F32 water_sign)
{
    static LLCachedControl<F32> displayGamma(gSavedSettings, "RenderDeferredDisplayGamma");
    F32 gamma = displayGamma;

    static LLStaticHashedString waterSign("waterSign");

    // Does this deferred shader need environment uniforms set such as sun_dir, etc. ?
    // NOTE: We don't actually need a gbuffer since we are doing forward rendering (for transparency) post deferred rendering
    // TODO: bindDeferredShader() probably should have the updating of the environment uniforms factored out into updateShaderEnvironmentUniforms()
    // i.e. shaders\class1\deferred\alphaF.glsl
    if (deferredEnvironment)
    {
        shader->mCanBindFast = false;
    }

    shader->bind();
    shader->uniform1f(LLShaderMgr::DISPLAY_GAMMA, (gamma > 0.1f) ? 1.0f / gamma : (1.0f / 2.2f));

    if (LLPipeline::sRenderingHUDs)
    { // for HUD attachments, only the pre-water pass is executed and we never want to clip anything
        LLVector4 near_clip(0, 0, -1, 0);
        shader->uniform1f(waterSign, 1.f);
        shader->uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, near_clip.mV);
    }
    else
    {
        shader->uniform1f(waterSign, water_sign);
        shader->uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, LLDrawPoolAlpha::sWaterPlane.mV);
    }

    if (LLPipeline::sImpostorRender)
    {
        shader->setMinimumAlpha(MINIMUM_IMPOSTOR_ALPHA);
    }
    else
    {
        shader->setMinimumAlpha(MINIMUM_ALPHA);
    }

    //also prepare rigged variant
    if (shader->mRiggedVariant && shader->mRiggedVariant != shader)
    {
        prepare_alpha_shader(shader->mRiggedVariant, deferredEnvironment, water_sign);
    }

    // <Vulkanstorm> and the stochastic transparency's alpha-only variant
    if (shader->mStochasticVariant && shader->mStochasticVariant != shader)
    {
        prepare_alpha_shader(shader->mStochasticVariant, deferredEnvironment, water_sign);
    }
    // </Vulkanstorm>
}

extern bool gCubeSnapshot;

void LLDrawPoolAlpha::renderPostDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    if (LLPipeline::isWaterClip() && getType() == LLDrawPool::POOL_ALPHA_PRE_WATER)
    { // don't render alpha objects on the other side of the water plane if water is opaque
        return;
    }

    F32 water_sign = 1.f;

    if (getType() == LLDrawPool::POOL_ALPHA_PRE_WATER)
    {
        water_sign = -1.f;
    }

    if (LLPipeline::sUnderWaterRender)
    {
        water_sign *= -1.f;
    }

    // prepare shaders
    llassert(LLPipeline::sRenderDeferred);

    emissive_shader = &gDeferredEmissiveProgram;
    prepare_alpha_shader(emissive_shader, false, water_sign);

    pbr_emissive_shader = &gPBRGlowProgram;
    prepare_alpha_shader(pbr_emissive_shader, false, water_sign);


    fullbright_shader   =
        (LLPipeline::sImpostorRender) ? &gDeferredFullbrightAlphaMaskProgram :
        (LLPipeline::sRenderingHUDs) ? &gHUDFullbrightAlphaMaskAlphaProgram :
        &gDeferredFullbrightAlphaMaskAlphaProgram;
    prepare_alpha_shader(fullbright_shader, true, water_sign);

    simple_shader   =
        (LLPipeline::sImpostorRender) ? &gDeferredAlphaImpostorProgram :
        (LLPipeline::sRenderingHUDs) ? &gHUDAlphaProgram :
        &gDeferredAlphaProgram;

    prepare_alpha_shader(simple_shader, true, water_sign); //prime simple shader (loads shadow relevant uniforms)

    LLGLSLShader* materialShader = gDeferredMaterialProgram;
    for (int i = 0; i < LLMaterial::SHADER_COUNT*2; ++i)
    {
        prepare_alpha_shader(&materialShader[i], true, water_sign);
    }

    pbr_shader =
        (LLPipeline::sRenderingHUDs) ? &gHUDPBRAlphaProgram :
        &gDeferredPBRAlphaProgram;

    prepare_alpha_shader(pbr_shader, true, water_sign);

    // explicitly unbind here so render loop doesn't make assumptions about the last shader
    // already being setup for rendering
    LLGLSLShader::unbind();

    // <Vulkanstorm> order-independent transparency (stochastic transparency, RenderAlphaSortMethod 1)
    // is limited to the main post-water view. Mirrors/hero probes, cube snapshots, HUDs,
    // impostors and pre-water alpha retain the stock path.
    static LLCachedControl<U32> alpha_method(gSavedSettings, "RenderAlphaSortMethod", 1);
    const bool oit_view_eligible =
        getType() == LLDrawPool::POOL_ALPHA_POST_WATER &&
        gPipeline.mRT == &gPipeline.mMainRT &&
        !LLPipeline::sRenderingHUDs &&
        !LLPipeline::sImpostorRender &&
        !LLPipeline::sReflectionRender &&
        !gCubeSnapshot;

    // Sorted alpha runs where the device cannot run stochastic transparency (the capability gate,
    // sRenderAlphaStochasticSupported) and, as a run-time fallback that never touches the saved setting,
    // while the engine is not ready on a supported device (targets missing or of another size after a
    // failed allocation or a resize, or an alpha-only shader variant that did not build). Owner decision
    // 2026-11-12, an explicit exception to the no-fallback rule: the blended faces are drawn, not dropped,
    // and the shadows follow to Legacy (LLStochasticEngine::requestFallback: also on a stochastic shader failure).
    // The targets are made when the method is selected or the screen buffers are
    // (LLPipeline::allocateAlphaStochastic) and retried by LLPipeline::retryAlphaStochastic, never here;
    // a later success, or a shader reload, brings stochastic transparency back by itself.
    const bool stochastic_selected = alpha_method == 1 && oit_view_eligible && LLPipeline::sRenderAlphaStochasticSupported;
    const bool use_stochastic = stochastic_selected && !LLStochasticEngine::fallbackActive() && gPipeline.alphaStochasticReady();
    if (stochastic_selected && !use_stochastic)
    {
        LL_WARNS_ONCE("Pipeline") << "Stochastic transparency is not ready (" << (gAlphaStochasticShadersReady ? "its targets are missing or of another size" : "its shaders did not build")
                                  << "): sorted alpha is drawn until it is" << LL_ENDL;
    }
    bool stochastic_drawn = false;
    if (use_stochastic)
    {
        // the GLTF scene's blended assets are drawn inside the passes (forwardRender)
        stochastic_drawn = renderStochastic();
        if (stochastic_drawn)
        {
            renderOITResidual();
        }
    }
    else
    {
        if (!LLPipeline::sRenderingHUDs)
        {
            // first pass, render rigged objects only and render to depth buffer
            forwardRender(true);
        }

        // second pass, regular forward alpha rendering
        forwardRender();
    }
    // </Vulkanstorm>

    // final pass, render to depth for depth of field effects (not when stochastic transparency was
    // selected and drew nothing: blended surfaces that were not drawn must not blur the scene)
    if (!LLPipeline::sImpostorRender && LLPipeline::RenderDepthOfField && !gCubeSnapshot && !LLPipeline::sRenderingHUDs && getType() == LLDrawPool::POOL_ALPHA_POST_WATER &&
        (!use_stochastic || stochastic_drawn))
    {
        //update depth buffer sampler
        simple_shader = fullbright_shader = &gDeferredFullbrightAlphaMaskProgram;

        simple_shader->bind();
        simple_shader->setMinimumAlpha(0.33f);

        // mask off color buffer writes as we're only writing to depth buffer
        gGL.setColorMask(false, false);

        // If the face is more than 90% transparent, then don't update the Depth buffer for Dof
        // We don't want the nearly invisible objects to cause of DoF effects
        renderAlpha(getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX | LLVertexBuffer::MAP_TANGENT | LLVertexBuffer::MAP_TEXCOORD1 | LLVertexBuffer::MAP_TEXCOORD2,
            true); // <--- discard mostly transparent faces

        // <Vulkanstorm> the GLTF scene's blended assets contribute as the blended faces do: the same
        // cut, depth only (the faces' rigged variants are not in this pass, nor are the GLTF ones)
        if (use_stochastic)
        {
            drawGLTFSceneDepth(false, 0.33f);
        }
        // </Vulkanstorm>

        gGL.setColorMask(true, false);
    }
}

// <FS> void LLDrawPoolAlpha::forwardRender(bool rigged)
void LLDrawPoolAlpha::forwardRender(bool rigged, EAlphaOITPhase oit_phase)
// </FS>
{
    gPipeline.enableLightsDynamic();

    LLGLSPipelineAlpha gls_pipeline_alpha;

    //enable writing to alpha for emissive effects
    gGL.setColorMask(true, true);

    // <Vulkanstorm> write_depth = rigged || ... on the stock paths; the stochastic transparency
    // passes never write the main depth buffer, only their own in their depth pass
    bool write_depth = oit_phase == ALPHA_ST_DEPTH || ((oit_phase == ALPHA_OIT_NONE || oit_phase == ALPHA_OIT_RESIDUAL) &&
        (rigged ||
        LLDrawPoolWater::sSkipScreenCopy
        // we want depth written so that rendered alpha will
        // contribute to the alpha mask used for impostors
        || LLPipeline::sImpostorRenderAlphaDepthPass
        || getType() == LLDrawPoolAlpha::POOL_ALPHA_PRE_WATER)); // needed for accurate water fog
    // </FS>


    LLGLDepthTest depth(GL_TRUE, write_depth ? GL_TRUE : GL_FALSE);
    // </FS>

    mColorSFactor = LLRender::BF_SOURCE_ALPHA;           // } regular alpha blend
    mColorDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA; // }
    mAlphaSFactor = LLRender::BF_ZERO;                         // } glow suppression
    mAlphaDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA;       // }
    // <Vulkanstorm> stochastic transparency: the total alpha keeps prod(1 - alpha) (its target
    // has no alpha channel); the accumulation adds alpha x colour and alpha. Every captured draw
    // takes them (renderAlpha), whatever its own source-over factors.
    if (oit_phase == ALPHA_ST_ALPHA)
    {
        mColorSFactor = LLRender::BF_ZERO;
        mColorDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA;
        mAlphaSFactor = LLRender::BF_ZERO;
        mAlphaDFactor = LLRender::BF_ONE_MINUS_SOURCE_ALPHA;
    }
    else if (oit_phase == ALPHA_ST_ACCUM || oit_phase == ALPHA_ST_DEPTH)
    {
        mColorSFactor = LLRender::BF_SOURCE_ALPHA;
        mColorDFactor = LLRender::BF_ONE;
        mAlphaSFactor = LLRender::BF_ONE;
        mAlphaDFactor = LLRender::BF_ONE;
    }
    // </Vulkanstorm>
    gGL.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);

    // <FS> if (rigged && mType == LLDrawPool::POOL_ALPHA_POST_WATER)
    // Vulkanstorm: the stochastic passes draw the GLTF scene's blended assets with the faces (below)
    if (oit_phase == ALPHA_OIT_NONE && rigged && mType == LLDrawPool::POOL_ALPHA_POST_WATER)
    // </FS>
    { // draw GLTF scene to depth buffer before rigged alpha
        LLGLDepthTest gltf_depth(GL_TRUE, GL_TRUE);
        // </FS>
        LL::GLTFSceneManager::instance().render(false, false);
        LL::GLTFSceneManager::instance().render(false, true);
        LL::GLTFSceneManager::instance().render(false, false, true);
        LL::GLTFSceneManager::instance().render(false, true, true);
    }

    // <Vulkanstorm> the GLTF scene's blended assets join the stochastic passes with the faces (one
    // engine for all transparency of the main view; they write no scene depth, so the depth spread
    // of the opaque scene needs no ordering against them)
    if (!rigged && getType() == LLDrawPool::POOL_ALPHA_POST_WATER &&
        (oit_phase == ALPHA_ST_ALPHA || oit_phase == ALPHA_ST_DEPTH || oit_phase == ALPHA_ST_ACCUM))
    {
        drawGLTFScene(oit_phase);
    }
    // </Vulkanstorm>

    // If the face is more than 90% transparent, then don't update the Depth buffer for Dof
    // We don't want the nearly invisible objects to cause of DoF effects
    renderAlpha(getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX | LLVertexBuffer::MAP_TANGENT | LLVertexBuffer::MAP_TEXCOORD1 | LLVertexBuffer::MAP_TEXCOORD2, false, rigged, oit_phase);

    gGL.setColorMask(true, false);

    // <FS> if (!rigged && (LLPipeline::sRenderingHUDs || getType() == LLDrawPoolAlpha::POOL_ALPHA_POST_WATER))
    if (!rigged &&
        (oit_phase == ALPHA_OIT_NONE || oit_phase == ALPHA_OIT_RESIDUAL) &&
        (LLPipeline::sRenderingHUDs || getType() == LLDrawPoolAlpha::POOL_ALPHA_POST_WATER))
    // </FS>
    { //render "highlight alpha" on final non-rigged pass for non-HUDs (HUDs only run pre-water alpha pass)
        // NOTE -- hacky call here protected by !rigged instead of alongside "forwardRender"
        // so renderDebugAlpha is executed while gls_pipeline_alpha and depth GL state
        // variables above are still in scope
        renderDebugAlpha();
    }
}

// <Vulkanstorm> one pass of the stochastic engine over the GLTF scene's blended assets, in the state
// forwardRender set for the phase (blend factors, depth writes in the depth pass only)
void LLDrawPoolAlpha::drawGLTFScene(EAlphaOITPhase oit_phase)
{
    LL::GLTFSceneManager::StochasticPass pass = LL::GLTFSceneManager::ST_NONE;
    switch (oit_phase)
    {
    case ALPHA_ST_ALPHA: pass = LL::GLTFSceneManager::ST_ALPHA; break;
    case ALPHA_ST_DEPTH: pass = LL::GLTFSceneManager::ST_DEPTH; break;
    case ALPHA_ST_ACCUM: pass = LL::GLTFSceneManager::ST_ACCUM; break;
    default: return;
    }

    LL::GLTFSceneManager& scene = LL::GLTFSceneManager::instance();
    scene.render(false, false, false, pass, mStochasticMasks);
    scene.render(false, true, false, pass, mStochasticMasks);
    scene.render(false, false, true, pass, mStochasticMasks);
    scene.render(false, true, true, pass, mStochasticMasks);
}

// A depth-only draw of the GLTF scene's blended assets (colour writes already masked by the caller):
// the alpha-only variant in its mode 3 (every sample, fragments under min_alpha discarded). Only
// where the blended faces contribute depth too (the depth of field pass, the rigged depth restore).
void LLDrawPoolAlpha::drawGLTFSceneDepth(bool rigged, F32 min_alpha)
{
    static const LLStaticHashedString sMode("st_mode");
    static const LLStaticHashedString sMinAlpha("st_min_alpha");
    for (LLGLSLShader& base : gGLTFPBRMetallicRoughnessProgram.mGLTFVariants)
    {
        LLGLSLShader* program = base.mStochasticVariant;
        if (program && program->mProgramObject)
        {
            program->bind();
            program->uniform1i(sMode, 3);
            program->uniform1f(sMinAlpha, min_alpha);
        }
    }
    LLGLSLShader::unbind();

    LL::GLTFSceneManager& scene = LL::GLTFSceneManager::instance();
    scene.render(false, rigged, false, LL::GLTFSceneManager::ST_DEPTH_ONLY);
    scene.render(false, rigged, true, LL::GLTFSceneManager::ST_DEPTH_ONLY);
}
// </Vulkanstorm>

// <Vulkanstorm> the end of an order-independent method: the OIT passes cannot write the shared
// scene depth (it would reject transparent fragments before they are composited). Restore the
// legacy rigged depth contribution before the residual alpha and emissive draws: the legacy
// path establishes this depth while drawing rigged alpha; without it, avatar-linked glow is
// tested only against the opaque depth and can bloom through nearby geometry as camera-
// dependent bright sprites. Particles and unsupported blend equations then draw in the stock
// path.
void LLDrawPoolAlpha::renderOITResidual()
{
    {
        LLGLDepthTest rigged_depth(GL_TRUE, GL_TRUE);
        gGL.setColorMask(false, false);
        renderAlpha(getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX |
                        LLVertexBuffer::MAP_TANGENT | LLVertexBuffer::MAP_TEXCOORD1 |
                        LLVertexBuffer::MAP_TEXCOORD2,
                    true, true, ALPHA_OIT_NONE);
        // <Vulkanstorm> and the GLTF scene's rigged blended assets, as the stock path draws them with
        // the rigged faces (same minimum alpha as the faces' shaders)
        drawGLTFSceneDepth(true, MINIMUM_ALPHA);
        // </Vulkanstorm>
        gGL.setColorMask(true, false);
    }

    forwardRender(true, ALPHA_OIT_RESIDUAL);
    forwardRender(false, ALPHA_OIT_RESIDUAL);
}

// The per-pass uniforms of every alpha-only variant this pool binds (alphaStochasticF.glsl).
void LLDrawPoolAlpha::setStochasticUniforms(S32 mode, U32 iteration)
{
    static const LLStaticHashedString sMode("st_mode");
    static const LLStaticHashedString sSamplesHW("st_samples_hw");
    static const LLStaticHashedString sSamples("st_samples");
    static const LLStaticHashedString sIteration("st_iteration");
    static const LLStaticHashedString sFrame("st_frame");
    const S32 frame = (S32)gPipeline.getAlphaStochasticFrame();
    const S32 samples_hw = (S32)gPipeline.getAlphaStochasticPassSamples();
    const S32 samples = (S32)gPipeline.getAlphaStochasticSamples();

    auto apply = [&](LLGLSLShader* shader)
    {
        if (!shader)
        {
            return;
        }
        for (LLGLSLShader* program : { shader->mStochasticVariant,
                                       shader->mRiggedVariant ? shader->mRiggedVariant->mStochasticVariant : nullptr })
        {
            if (program && program->mProgramObject)
            {
                program->bind();
                program->uniform1i(sMode, mode);
                program->uniform1i(sSamplesHW, samples_hw);
                program->uniform1i(sSamples, samples);
                program->uniform1i(sIteration, (S32)iteration);
                program->uniform1i(sFrame, frame);
            }
        }
    };

    apply(simple_shader);
    apply(fullbright_shader);
    apply(pbr_shader);
    // the GLTF scene's blended variants (their base variants point at their alpha-only ones)
    for (LLGLSLShader& base : gGLTFPBRMetallicRoughnessProgram.mGLTFVariants)
    {
        LLGLSLShader* program = base.mStochasticVariant;
        if (program && program->mProgramObject)
        {
            program->bind();
            program->uniform1i(sMode, mode);
            program->uniform1i(sSamplesHW, samples_hw);
            program->uniform1i(sSamples, samples);
            program->uniform1i(sIteration, (S32)iteration);
            program->uniform1i(sFrame, frame);
        }
    }
    LLGLSLShader* material_shader = gDeferredMaterialProgram;
    for (S32 i = 0; i < LLMaterial::SHADER_COUNT * 2; ++i)
    {
        if ((i & 0x3) == LLMaterial::DIFFUSE_ALPHA_MODE_BLEND)
        {
            apply(&material_shader[i]);
        }
    }
    LLGLSLShader::unbind();
}

// The mask table on the unit the bound variant gives it; texture units are global state, so on
// every program transition.
void LLDrawPoolAlpha::bindStochasticMasks(LLGLSLShader* shader)
{
    if (shader && mStochasticMasks)
    {
        const S32 channel = shader->enableTexture(LLShaderMgr::STOCH_MASKS);
        if (channel > -1)
        {
            gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE, mStochasticMasks);
        }
    }
}

// <Vulkanstorm/> the stochastic passes' slope-scaled polygon offset (see renderStochastic)
static const F32 ALPHA_ST_OFFSET_FACTOR = -1.f / 256.f;

bool LLDrawPoolAlpha::renderStochastic()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    if (!gPipeline.beginAlphaStochastic())
    {
        return false;
    }
    mStochasticMasks = gPipeline.getAlphaStochasticMasks();
    mStochasticGlow = false;
    const U32 iterations = llmax(gPipeline.getAlphaStochasticIterations(), 1u);
    {
        // Multisampled rasterization for the passes' targets (the viewer keeps it off). The
        // depth spread gives each sample the opaque surface's depth at that sample, so a face
        // meets it sample for sample as sorted alpha meets it at the pixel centre. The faces are
        // pulled forward by a 256th of their depth slope and one depth unit, the rounding of the
        // spread and of the rasteriser, so that a face lying on an opaque surface (a decal) keeps
        // every sample; no more, since a whole slope unit let a face up to a pixel's depth step
        // behind an opaque surface show through it, where sorted alpha hides it. The accumulation takes one depth unit more than the stochastic depth,
        // and the glow (other vertex shaders) one more again, so that a face always passes at the
        // samples it won whatever the last bit of its depth.
        LLGLEnable multisample(GL_MULTISAMPLE);
        LLGLEnable offset(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(ALPHA_ST_OFFSET_FACTOR, -1.f);

        {
            LL_PROFILE_GPU_ZONE("alpha stochastic total alpha");
            setStochasticUniforms(1, 0);
            gPipeline.setAlphaStochasticBuffer(LLPipeline::ALPHA_ST_BUFFER_TRANS);
            forwardRender(true, ALPHA_ST_ALPHA);
            forwardRender(false, ALPHA_ST_ALPHA);
        }

        for (U32 iteration = 0; iteration < iterations; ++iteration)
        {
            if (iteration > 0)
            {
                LLGLDisable no_offset(GL_POLYGON_OFFSET_FILL);
                gPipeline.spreadAlphaStochasticDepth();
            }
            {
                LL_PROFILE_GPU_ZONE("alpha stochastic depth");
                glPolygonOffset(ALPHA_ST_OFFSET_FACTOR, -1.f);
                setStochasticUniforms(2, iteration);
                gPipeline.setAlphaStochasticBuffer(LLPipeline::ALPHA_ST_BUFFER_NONE);
                forwardRender(true, ALPHA_ST_DEPTH);
                forwardRender(false, ALPHA_ST_DEPTH);
            }
            {
                LL_PROFILE_GPU_ZONE("alpha stochastic accumulation");
                glPolygonOffset(ALPHA_ST_OFFSET_FACTOR, -2.f);
                gPipeline.setAlphaStochasticBuffer(LLPipeline::ALPHA_ST_BUFFER_ACCUM);
                forwardRender(true, ALPHA_ST_ACCUM);
                forwardRender(false, ALPHA_ST_ACCUM);
            }
        }
    }
    gPipeline.endAlphaStochastic();
    gPipeline.compositeAlphaStochastic(mStochasticGlow);
    mStochasticMasks = 0;
    return true;
}
// </Vulkanstorm>

void LLDrawPoolAlpha::renderDebugAlpha()
{
    if (sShowDebugAlpha && !gCubeSnapshot && !LLPipeline::sReflectionRender)
    {
        gHighlightProgram.bind();
        gGL.diffuseColor4f(1, 0, 0, 1);
        gGL.getTexUnit(0)->bindFast(LLViewerFetchedTexture::getSmokeImage());


        renderAlphaHighlight();

        pushUntexturedBatches(LLRenderPass::PASS_ALPHA_MASK);
        pushUntexturedBatches(LLRenderPass::PASS_ALPHA_INVISIBLE);

        // Material alpha mask
        gGL.diffuseColor4f(0, 0, 1, 1);
        pushUntexturedBatches(LLRenderPass::PASS_MATERIAL_ALPHA_MASK);
        pushUntexturedBatches(LLRenderPass::PASS_NORMMAP_MASK);
        pushUntexturedBatches(LLRenderPass::PASS_SPECMAP_MASK);
        pushUntexturedBatches(LLRenderPass::PASS_NORMSPEC_MASK);
        pushUntexturedBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK);
        pushUntexturedBatches(LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK);

        gGL.diffuseColor4f(0, 1, 0, 1);
        pushUntexturedBatches(LLRenderPass::PASS_INVISIBLE);
        // <FS:Beq> FIRE-32132 et al. Allow rigged mesh transparency highlights to be toggled
        if (sShowDebugAlphaRigged)
        {
        // </FS:Beq>
        gHighlightProgram.mRiggedVariant->bind();
        gGL.diffuseColor4f(0, 1, 0, 1);// <FS:Beq/> FIRE-32132 et al. (can plain PASS_ALPHA_MASK_RIGGED exist?) paint it green if so.
        pushRiggedBatches(LLRenderPass::PASS_ALPHA_MASK_RIGGED, false);
        pushRiggedBatches(LLRenderPass::PASS_ALPHA_INVISIBLE_RIGGED, false);

        // Material alpha mask
        gGL.diffuseColor4f(0, 1, 1, 1);// <FS:Beq/> FIRE-32132 et al. Allow rigged mesh transparency highlights to be toggled
        pushRiggedBatches(LLRenderPass::PASS_MATERIAL_ALPHA_MASK_RIGGED, false);
        pushRiggedBatches(LLRenderPass::PASS_NORMMAP_MASK_RIGGED, false);
        pushRiggedBatches(LLRenderPass::PASS_SPECMAP_MASK_RIGGED, false);
        pushRiggedBatches(LLRenderPass::PASS_NORMSPEC_MASK_RIGGED, false);
        pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK_RIGGED, false);
        pushRiggedBatches(LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK_RIGGED, false);

        gGL.diffuseColor4f(0, 1, 0, 1);
        pushRiggedBatches(LLRenderPass::PASS_INVISIBLE_RIGGED, false);
        // <FS:Beq> FIRE-32132 et al. Allow rigged mesh transparency highlights to be toggled
        }
        // </FS:Beq>
        LLGLSLShader::sCurBoundShaderPtr->unbind();
    }
}

void LLDrawPoolAlpha::renderAlphaHighlight()
{
    for (int pass = 0; pass < 2; ++pass)
    { //two passes, one rigged and one not
        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;

        LLCullResult::sg_iterator begin = pass == 0 ? gPipeline.beginAlphaGroups() : gPipeline.beginRiggedAlphaGroups();
        LLCullResult::sg_iterator end = pass == 0 ? gPipeline.endAlphaGroups() : gPipeline.endRiggedAlphaGroups();

        for (LLCullResult::sg_iterator i = begin; i != end; ++i)
        {
            LLSpatialGroup* group = *i;
            if (group->getSpatialPartition()->mRenderByGroup &&
                !group->isDead())
            {
                LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[LLRenderPass::PASS_ALPHA+pass]; // <-- hacky + pass to use PASS_ALPHA_RIGGED on second pass

                for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
                {
                    LLDrawInfo& params = **k;

                    bool rigged = (params.mAvatar != nullptr);
                    gHighlightProgram.bind(rigged);

                    if (rigged)
                    {
                        if (!uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                        { // failed to upload matrix palette, skip rendering
                            continue;
                        }
                    }

                    // <FS:Beq> FIRE-32132 et al. Allow rigged mesh transparency highlights to be toggled
                    if (rigged && !sShowDebugAlphaRigged)
                    {
                        // if we don't want to show rigged alpha highlights then skip
                        continue;
                    }
                    else if (rigged && sShowDebugAlphaRigged)
                    {
                        // if we do and this is rigged then use a different colour
                        gGL.diffuseColor4f(1, 0.5, 0, 1);
                    }
                    else // NB dangling else to drop through to "normal behaviour"
                    // </FS:Beq>
                    gGL.diffuseColor4f(1, 0, 0, 1);
                    LLRenderPass::applyModelMatrix(params);
                    params.mVertexBuffer->setBuffer();
                    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
                }
            }
        }
    }

    // make sure static version of highlight shader is bound before returning
    gHighlightProgram.bind();
}

inline bool IsFullbright(LLDrawInfo& params)
{
    return params.mFullbright;
}

inline bool IsMaterial(LLDrawInfo& params)
{
    return params.mMaterial != nullptr;
}

inline bool IsEmissive(LLDrawInfo& params)
{
    return params.mVertexBuffer->hasDataType(LLVertexBuffer::TYPE_EMISSIVE);
}

inline void Draw(LLDrawInfo* draw, U32 mask)
{
    draw->mVertexBuffer->setBuffer();
    LLRenderPass::applyModelMatrix(*draw);
    draw->mVertexBuffer->drawRange(LLRender::TRIANGLES, draw->mStart, draw->mEnd, draw->mCount, draw->mOffset);
}

bool LLDrawPoolAlpha::TexSetup(LLDrawInfo* draw, bool use_material)
{
    bool tex_setup = false;

    if (draw->mGLTFMaterial)
    {
        if (draw->mTextureMatrix)
        {
            tex_setup = true;
            gGL.getTexUnit(0)->activate();
            gGL.matrixMode(LLRender::MM_TEXTURE);
            gGL.loadMatrix((GLfloat*)draw->mTextureMatrix->mMatrix);
            gPipeline.mTextureMatrixOps++;
        }
    }
    else
    {
        // <Vulkanstorm> an alpha-only variant reads only the diffuse map unless the material can
        // glare (materialF.glsl): then it shades in full for its alpha, and its own normal map
        // (the glossiness in its alpha) and specular map (the environment intensity in its
        // alpha) must be bound, or it reads whatever another face left on those units
        const bool alpha_only_no_glare = mAlphaOnlyPass && !alphaOnlyCanGlare(*draw);
        if (!LLPipeline::sRenderingHUDs && use_material && current_shader && !alpha_only_no_glare)
        {
            if (draw->mNormalMap)
            {
                current_shader->bindTexture(LLShaderMgr::BUMP_MAP, draw->mNormalMap);
            }

            if (draw->mSpecularMap)
            {
                current_shader->bindTexture(LLShaderMgr::SPECULAR_MAP, draw->mSpecularMap);
            }
        }
        else if (current_shader == simple_shader || current_shader == simple_shader->mRiggedVariant)
        {
            current_shader->bindTexture(LLShaderMgr::BUMP_MAP, LLViewerFetchedTexture::sFlatNormalImagep);
            current_shader->bindTexture(LLShaderMgr::SPECULAR_MAP, LLViewerFetchedTexture::sWhiteImagep);
        }
        if (draw->mTextureList.size() > 1)
        {
            for (U32 i = 0; i < draw->mTextureList.size(); ++i)
            {
                if (draw->mTextureList[i].notNull())
                {
                    gGL.getTexUnit(i)->bindFast(draw->mTextureList[i]);
                }
            }
        }
        else
        { //not batching textures or batch has only 1 texture -- might need a texture matrix
            if (draw->mTexture.notNull())
            {
                if (use_material)
                {
                    current_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, draw->mTexture);
                }
                else
                {
                    gGL.getTexUnit(0)->bindFast(draw->mTexture);
                }

                if (draw->mTextureMatrix)
                {
                    tex_setup = true;
                    gGL.getTexUnit(0)->activate();
                    gGL.matrixMode(LLRender::MM_TEXTURE);
                    gGL.loadMatrix((GLfloat*)draw->mTextureMatrix->mMatrix);
                    gPipeline.mTextureMatrixOps++;
                }
            }
            else
            {
                gGL.getTexUnit(0)->unbindFast(LLTexUnit::TT_TEXTURE);
            }
        }
    }

    return tex_setup;
}

void LLDrawPoolAlpha::RestoreTexSetup(bool tex_setup)
{
    if (tex_setup)
    {
        gGL.getTexUnit(0)->activate();
        gGL.matrixMode(LLRender::MM_TEXTURE);
        gGL.loadIdentity();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
    }
}

void LLDrawPoolAlpha::drawEmissive(LLDrawInfo* draw)
{
    LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, 1.f);
    draw->mVertexBuffer->setBuffer();
    // OIT residual replay can queue this face without drawing its normal batch.
    // Establish its transform here instead of inheriting the previous batch's.
    LLRenderPass::applyModelMatrix(*draw);
    draw->mVertexBuffer->drawRange(LLRender::TRIANGLES, draw->mStart, draw->mEnd, draw->mCount, draw->mOffset);
}


void LLDrawPoolAlpha::renderEmissives(std::vector<LLDrawInfo*>& emissives)
{
    emissive_shader->bind();
    emissive_shader->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, 1.f);

    for (LLDrawInfo* draw : emissives)
    {
        bool tex_setup = TexSetup(draw, false);
        drawEmissive(draw);
        RestoreTexSetup(tex_setup);
    }
}

void LLDrawPoolAlpha::renderPbrEmissives(std::vector<LLDrawInfo*>& emissives)
{
    pbr_emissive_shader->bind();

    for (LLDrawInfo* draw : emissives)
    {
        llassert(draw->mGLTFMaterial);
        LLGLDisable cull_face(draw->mGLTFMaterial->mDoubleSided ? GL_CULL_FACE : 0);
        draw->mGLTFMaterial->bind(draw->mTexture);
        draw->mVertexBuffer->setBuffer();
        LLRenderPass::applyModelMatrix(*draw);
        draw->mVertexBuffer->drawRange(LLRender::TRIANGLES, draw->mStart, draw->mEnd, draw->mCount, draw->mOffset);
    }
}

void LLDrawPoolAlpha::renderRiggedEmissives(std::vector<LLDrawInfo*>& emissives)
{
    LLGLDepthTest depth(GL_TRUE, GL_FALSE); //disable depth writes since "emissive" is additive so sorting doesn't matter
    LLGLSLShader* shader = emissive_shader->mRiggedVariant;
    shader->bind();
    shader->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, 1.f);

    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    for (LLDrawInfo* draw : emissives)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("Emissives");

        if (uploadMatrixPalette(draw->mAvatar, draw->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
        {
            bool tex_setup = TexSetup(draw, false);
            drawEmissive(draw);
            RestoreTexSetup(tex_setup);
        }
    }
}

void LLDrawPoolAlpha::renderRiggedPbrEmissives(std::vector<LLDrawInfo*>& emissives)
{
    LLGLDepthTest depth(GL_TRUE, GL_FALSE); //disable depth writes since "emissive" is additive so sorting doesn't matter
    pbr_emissive_shader->bind(true);

    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    for (LLDrawInfo* draw : emissives)
    {
        if (!uploadMatrixPalette(draw->mAvatar, draw->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
        { // failed to upload matrix palette, skip rendering
            continue;
        }

        LLGLDisable cull_face(draw->mGLTFMaterial->mDoubleSided ? GL_CULL_FACE : 0);
        draw->mGLTFMaterial->bind(draw->mTexture);
        draw->mVertexBuffer->setBuffer();
        LLRenderPass::applyModelMatrix(*draw);
        draw->mVertexBuffer->drawRange(LLRender::TRIANGLES, draw->mStart, draw->mEnd, draw->mCount, draw->mOffset);
    }
}

// <FS> void LLDrawPoolAlpha::renderAlpha(U32 mask, bool depth_only, bool rigged)
void LLDrawPoolAlpha::renderAlpha(U32 mask, bool depth_only, bool rigged, EAlphaOITPhase oit_phase)
// </FS>
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    bool initialized_lighting = false;
    bool light_enabled = true;

    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    const LLGLSLShader* lastAvatarShader = nullptr;
    bool skipLastSkin = false;

    LLCullResult::sg_iterator begin = rigged ? gPipeline.beginRiggedAlphaGroups() : gPipeline.beginAlphaGroups();
    LLCullResult::sg_iterator end = rigged ? gPipeline.endRiggedAlphaGroups() : gPipeline.endAlphaGroups();

    LLEnvironment& env = LLEnvironment::instance();
    F32 water_height = env.getWaterHeight();

    bool above_water = getType() == LLDrawPool::POOL_ALPHA_POST_WATER;
    if (LLPipeline::sUnderWaterRender)
    {
        above_water = !above_water;
    }

    for (LLCullResult::sg_iterator i = begin; i != end; ++i)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("renderAlpha - group");
        LLSpatialGroup* group = *i;
        llassert(group);
        llassert(group->getSpatialPartition());

        if (group->getSpatialPartition()->mRenderByGroup &&
            !group->isDead())
        {

            LLSpatialBridge* bridge = group->getSpatialPartition()->asBridge();
            const LLVector4a* ext = bridge ? bridge->getSpatialExtents() : group->getExtents();

            if (!LLPipeline::sRenderingHUDs) // ignore above/below water for HUD render
            {
                if (above_water)
                { // reject any spatial groups that have no part above water
                    if (ext[1].getF32ptr()[2] < water_height)
                    {
                        continue;
                    }
                }
                else
                { // reject any spatial groups that he no part below water
                    if (ext[0].getF32ptr()[2] > water_height)
                    {
                        continue;
                    }
                }
            }

            static std::vector<LLDrawInfo*> emissives;
            static std::vector<LLDrawInfo*> rigged_emissives;
            static std::vector<LLDrawInfo*> pbr_emissives;
            static std::vector<LLDrawInfo*> pbr_rigged_emissives;

            emissives.resize(0);
            rigged_emissives.resize(0);
            pbr_emissives.resize(0);
            pbr_rigged_emissives.resize(0);

            bool is_particle_or_hud_particle = group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_PARTICLE
                                                      || group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_HUD_PARTICLE;

            // <FS> Vulkanstorm: particle partitions are always rendered by the stock alpha
            // path, never by the order-independent passes, even when their batches happen to
            // be fullbright.
            if ((oit_phase == ALPHA_ST_ALPHA || oit_phase == ALPHA_ST_DEPTH || oit_phase == ALPHA_ST_ACCUM) &&
                is_particle_or_hud_particle)
            {
                continue;
            }
            // </FS>

            // <FS:LO> Dont suspend partical processing while particles are hidden, just skip over drawing them
            if(!(gPipeline.sRenderParticles) && (
                                                 group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_PARTICLE ||
                                                 group->getSpatialPartition()->mPartitionType == LLViewerRegion::PARTITION_HUD_PARTICLE))
            {
                continue;
            }
            // </FS:LO>

            bool disable_cull = is_particle_or_hud_particle;
            LLGLDisable cull(disable_cull ? GL_CULL_FACE : 0);

            LLSpatialGroup::drawmap_elem_t& draw_info = rigged ? group->mDrawMap[LLRenderPass::PASS_ALPHA_RIGGED] : group->mDrawMap[LLRenderPass::PASS_ALPHA];

            for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
            {
                LLDrawInfo& params = **k;
                if ((bool)params.mAvatar != rigged)
                {
                    continue;
                }

                // <FS> Vulkanstorm: OIT phase filtering. The stochastic passes skip residual
                // (particle or custom-blend) draws; the residual pass skips the others.
                const bool custom_blend =
                    params.mBlendFuncSrc != LLRender::BF_SOURCE_ALPHA ||
                    params.mBlendFuncDst != LLRender::BF_ONE_MINUS_SOURCE_ALPHA;
                const bool residual_draw = is_particle_or_hud_particle || custom_blend;

                const bool stochastic_pass = oit_phase == ALPHA_ST_ALPHA || oit_phase == ALPHA_ST_DEPTH ||
                                             oit_phase == ALPHA_ST_ACCUM; // <Vulkanstorm/>
                const bool alpha_only_pass = oit_phase == ALPHA_ST_ALPHA || oit_phase == ALPHA_ST_DEPTH; // <Vulkanstorm/>
                if (stochastic_pass && residual_draw)
                {
                    continue;
                }


                if (oit_phase == ALPHA_OIT_RESIDUAL && !residual_draw)
                {
                    // drawn by the stochastic passes, their glow with them
                    continue;
                }
                // </FS>

                LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("ra - push batch");

                LLRenderPass::applyModelMatrix(params);

                LLMaterial* mat = NULL;
                LLGLTFMaterial *gltf_mat = params.mGLTFMaterial;

                LLGLDisable cull_face(gltf_mat && gltf_mat->mDoubleSided ? GL_CULL_FACE : 0);

                if (gltf_mat && gltf_mat->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_BLEND)
                {
                    target_shader = pbr_shader;
                    if (params.mAvatar != nullptr)
                    {
                        target_shader = target_shader->mRiggedVariant;
                    }
                    // <Vulkanstorm> stochastic transparency's alpha-only passes
                    if ((oit_phase == ALPHA_ST_ALPHA || oit_phase == ALPHA_ST_DEPTH) && target_shader->mStochasticVariant)
                    {
                        target_shader = target_shader->mStochasticVariant;
                    }
                    // </Vulkanstorm>

                    // shader must be bound before LLGLTFMaterial::bind
                    if (current_shader != target_shader)
                    {
                        // <Vulkanstorm> the alpha-only variants read no light function, shadow
                        // map or reflection probe: once fully bound this frame, a plain bind
                        if (alpha_only_pass && target_shader->mCanBindFast)
                        {
                            target_shader->bind();
                            mAlphaOnlyLightBound = false;
                        }
                        else
                        {
                            gPipeline.bindDeferredShaderFast(*target_shader);
                            mAlphaOnlyLightBound = true;
                        }
                        // </Vulkanstorm>
                        // <Vulkanstorm> texture units are global state; the mask table on
                        // every shader transition
                        if (oit_phase == ALPHA_ST_DEPTH)
                        {
                            bindStochasticMasks(target_shader);
                        }
                        // </Vulkanstorm>
                    }

                    params.mGLTFMaterial->bind(params.mTexture);
                }
                else
                {
                    mat = LLPipeline::sRenderingHUDs ? nullptr : params.mMaterial;

                    if (params.mFullbright)
                    {
                        // Turn off lighting if it hasn't already been so.
                        if (light_enabled || !initialized_lighting)
                        {
                            initialized_lighting = true;
                            target_shader = fullbright_shader;

                            light_enabled = false;
                        }
                    }
                    // Turn on lighting if it isn't already.
                    else if (!light_enabled || !initialized_lighting)
                    {
                        initialized_lighting = true;
                        target_shader = simple_shader;
                        light_enabled = true;
                    }

                    if (LLPipeline::sRenderingHUDs)
                    {
                        target_shader = fullbright_shader;
                    }
                    else if (mat)
                    {
                        U32 mask = params.mShaderMask;

                        llassert(mask < LLMaterial::SHADER_COUNT);
                        target_shader = &(gDeferredMaterialProgram[mask]);
                    }
                    else if (!params.mFullbright)
                    {
                        target_shader = simple_shader;
                    }
                    else
                    {
                        target_shader = fullbright_shader;
                    }

                    if (params.mAvatar != nullptr)
                    {
                        llassert(target_shader->mRiggedVariant != nullptr);
                        target_shader = target_shader->mRiggedVariant;
                    }
                    // <Vulkanstorm> stochastic transparency's alpha-only passes
                    if ((oit_phase == ALPHA_ST_ALPHA || oit_phase == ALPHA_ST_DEPTH) && target_shader->mStochasticVariant)
                    {
                        target_shader = target_shader->mStochasticVariant;
                    }
                    // </Vulkanstorm>

                    if (current_shader != target_shader)
                    {// If we need shaders, and we're not ALREADY using the proper shader, then bind it
                    // (this way we won't rebind shaders unnecessarily).
                        // <Vulkanstorm> the alpha-only variants read no light function, shadow
                        // map or reflection probe: once fully bound this frame, a plain bind
                        if (alpha_only_pass && target_shader->mCanBindFast)
                        {
                            target_shader->bind();
                            mAlphaOnlyLightBound = false;
                        }
                        else
                        {
                            gPipeline.bindDeferredShaderFast(*target_shader);
                            mAlphaOnlyLightBound = true;
                        }
                        // </Vulkanstorm>

                        // <Vulkanstorm> texture units are global state; the mask table on
                        // every shader transition
                        if (oit_phase == ALPHA_ST_DEPTH)
                        {
                            bindStochasticMasks(target_shader);
                        }
                        // </Vulkanstorm>

                        if (params.mFullbright)
                        { // make sure the bind the exposure map for fullbright shaders so they can cancel out exposure
                            S32 channel = target_shader->enableTexture(LLShaderMgr::EXPOSURE_MAP);
                            if (channel > -1)
                            {
                                gGL.getTexUnit(channel)->bind(&gPipeline.mExposureMap);
                            }
                        }
                    }

                    LLVector4 spec_color(1, 1, 1, 1);
                    F32 env_intensity = 0.0f;
                    F32 brightness = 1.0f;

                    // We have a material.  Supply the appropriate data here.
                    if (mat)
                    {
                        spec_color = params.mSpecColor;
                        env_intensity = params.mEnvIntensity;
                        brightness = params.mFullbright ? 1.f : 0.f;
                    }

                    // <Vulkanstorm> an alpha-only variant of a material that can glare shades in
                    // full for its alpha: it needs the light function, shadow maps and probes
                    // that its plain bind skipped
                    if (alpha_only_pass && mat && current_shader && !mAlphaOnlyLightBound && alphaOnlyCanGlare(params))
                    {
                        gPipeline.bindDeferredShaderFast(*current_shader);
                        mAlphaOnlyLightBound = true;
                        if (oit_phase == ALPHA_ST_DEPTH)
                        {
                            bindStochasticMasks(current_shader);
                        }
                    }
                    // </Vulkanstorm>

                    if (current_shader)
                    {
                        current_shader->uniform4f(LLShaderMgr::SPECULAR_COLOR, spec_color.mV[VRED], spec_color.mV[VGREEN], spec_color.mV[VBLUE], spec_color.mV[VALPHA]);
                        current_shader->uniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, env_intensity);
                        current_shader->uniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, brightness);
                    }
                }

                if (params.mAvatar && !uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, lastAvatarShader, skipLastSkin))
                {
                    continue;
                }

                mAlphaOnlyPass = alpha_only_pass; // <Vulkanstorm/>
                bool tex_setup = TexSetup(&params, (mat != nullptr));
                mAlphaOnlyPass = false;

                {
                    // <Vulkanstorm> stochastic transparency: the pass's own factors, and the face's seed
                    if (stochastic_pass)
                    {
                        gGL.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);
                        if (oit_phase == ALPHA_ST_DEPTH && current_shader)
                        {
                            current_shader->uniform1i(LLShaderMgr::ALPHA_ST_SEED, (S32)params.mStochasticSeed);
                        }
                    }
                    else
                    // </Vulkanstorm>
                    gGL.blendFunc((LLRender::eBlendFactor) params.mBlendFuncSrc, (LLRender::eBlendFactor) params.mBlendFuncDst, mAlphaSFactor, mAlphaDFactor);

                    bool reset_minimum_alpha = false;
                    if (!LLPipeline::sImpostorRender &&
                        params.mBlendFuncDst != LLRender::BF_SOURCE_ALPHA &&
                        params.mBlendFuncSrc != LLRender::BF_SOURCE_ALPHA)
                    { // this draw call has a custom blend function that may require rendering of "invisible" fragments
                        current_shader->setMinimumAlpha(0.f);
                        reset_minimum_alpha = true;
                    }

                    params.mVertexBuffer->setBuffer();
                    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
                    stop_glerror();

                    if (reset_minimum_alpha)
                    {
                        current_shader->setMinimumAlpha(MINIMUM_ALPHA);
                    }
                }

                // If this alpha mesh has glow, then draw it a second time to add the destination-alpha (=glow).  Interleaving these state-changing calls is expensive, but glow must be drawn Z-sorted with alpha.
                // <Vulkanstorm> the alpha-only passes emit no glow; the accumulation accumulates it
                if ((oit_phase == ALPHA_OIT_NONE || oit_phase == ALPHA_OIT_RESIDUAL || oit_phase == ALPHA_ST_ACCUM) &&
                    getType() != LLDrawPool::POOL_ALPHA_PRE_WATER &&
                    params.mVertexBuffer->hasDataType(LLVertexBuffer::TYPE_EMISSIVE))
                {
                    if (params.mAvatar != nullptr)
                    {
                        if (params.mGLTFMaterial.isNull())
                        {
                            rigged_emissives.push_back(&params);
                        }
                        else
                        {
                            pbr_rigged_emissives.push_back(&params);
                        }
                    }
                    else
                    {
                        if (params.mGLTFMaterial.isNull())
                        {
                            emissives.push_back(&params);
                        }
                        else
                        {
                            pbr_emissives.push_back(&params);
                        }
                    }
                }

                if (tex_setup)
                {
                    gGL.getTexUnit(0)->activate();
                    gGL.matrixMode(LLRender::MM_TEXTURE);
                    gGL.loadIdentity();
                    gGL.matrixMode(LLRender::MM_MODELVIEW);
                }
            }

            // render emissive faces into alpha channel for bloom effects
            // <Vulkanstorm> the alpha-only passes emit no glow; the accumulation accumulates it
            const bool st_glow = oit_phase == ALPHA_ST_ACCUM &&
                (!emissives.empty() || !pbr_emissives.empty() || !rigged_emissives.empty() || !pbr_rigged_emissives.empty()); // <Vulkanstorm/>
            if (!depth_only &&
                (oit_phase == ALPHA_OIT_NONE || oit_phase == ALPHA_OIT_RESIDUAL || st_glow))
            {
                gPipeline.enableLightsDynamic();

                // install glow-accumulating blend mode
                // don't touch color, add to alpha (glow)
                gGL.blendFunc(LLRender::BF_ZERO, LLRender::BF_ONE, LLRender::BF_ONE, LLRender::BF_ONE);
                // <Vulkanstorm> stochastic transparency: the glow into the green of the
                // transparency target (the glow shaders write it there too), at or in front of
                // the stochastic depth, like the faces' colour
                if (st_glow)
                {
                    gPipeline.setAlphaStochasticBuffer(LLPipeline::ALPHA_ST_BUFFER_TRANS);
                    gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_ONE, LLRender::BF_ONE, LLRender::BF_ONE);
                    glPolygonOffset(ALPHA_ST_OFFSET_FACTOR, -3.f);
                    mStochasticGlow = true;
                }
                // </Vulkanstorm>

                bool rebind = false;
                LLGLSLShader* lastShader = current_shader;
                if (!emissives.empty())
                {
                    light_enabled = true;
                    renderEmissives(emissives);
                    rebind = true;
                }

                if (!pbr_emissives.empty())
                {
                    light_enabled = true;
                    renderPbrEmissives(pbr_emissives);
                    rebind = true;
                }

                if (!rigged_emissives.empty())
                {
                    light_enabled = true;
                    renderRiggedEmissives(rigged_emissives);
                    rebind = true;
                }

                if (!pbr_rigged_emissives.empty())
                {
                    light_enabled = true;
                    renderRiggedPbrEmissives(pbr_rigged_emissives);
                    rebind = true;
                }

                // restore our alpha blend mode
                gGL.blendFunc(mColorSFactor, mColorDFactor, mAlphaSFactor, mAlphaDFactor);
                if (st_glow)
                {
                    gPipeline.setAlphaStochasticBuffer(LLPipeline::ALPHA_ST_BUFFER_ACCUM); // <Vulkanstorm/>
                    glPolygonOffset(ALPHA_ST_OFFSET_FACTOR, -2.f);
                }

                if (lastShader && rebind)
                {
                    lastShader->bind();
                }
            }
        }
    }

    gGL.setSceneBlendType(LLRender::BT_ALPHA);

    LLVertexBuffer::unbind();

    if (!light_enabled)
    {
        gPipeline.enableLightsDynamic();
    }
}

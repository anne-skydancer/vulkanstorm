/**
 * @file llatmosphereluts.cpp
 * @brief Stage AT2-1: the physical atmosphere's tables and the EEP filter's tables on the GPU
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#include "llviewerprecompiledheaders.h"

#include "llatmosphereluts.h"

#include "llagent.h"
#include "llatmospherebase.h"
#include "llenvironment.h"
#include "llframetimer.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llsettingssky.h"
#include "llskyfilter.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

#include <cmath>
#include <cstring>

extern bool gCubeSnapshot;

namespace AL = LLAtmosphericLight;

namespace
{
enum TablePass // gAtmosphereTableProgram, atmosphereTablesF.glsl's AT_PASS - 1
{
    PASS_T = 0,
    PASS_MS_SPLIT,
    PASS_MS,
    PASS_E_SPLIT,
    PASS_E,
    PASS_SKY_VIEW,
};

enum FilterPass // gSkyFilterProgram, skyFilterF.glsl's SF_PASS - 1
{
    PASS_AB = 0,
    PASS_K_ROWS,
    PASS_K,
};

// the passes' sampler units (their programs have no reserved samplers, so the units are theirs)
const S32 UNIT_T = 0;
const S32 UNIT_MS = 1;
const S32 UNIT_E = 2;
const S32 UNIT_PART0 = 3;
const S32 UNIT_PART1 = 4;
const S32 UNIT_COUNT = 5;

const LLStaticHashedString sTransLut("pa_trans_lut");
const LLStaticHashedString sMsLut("pa_ms_lut");
const LLStaticHashedString sELut("pa_e_lut");
const LLStaticHashedString sPart0("at_part0");
const LLStaticHashedString sPart1("at_part1");
const LLStaticHashedString sSteps("at_steps");
const LLStaticHashedString sSkyView("sf_sky_view");
const LLStaticHashedString sTableA("sf_table_a");
const LLStaticHashedString sTableB("sf_table_b");
const LLStaticHashedString sRows("sf_rows");

void bind_unit(S32 unit, U32 tex, bool bilinear)
{
    LLTexUnit* u = gGL.getTexUnit(unit);
    u->bindManual(LLTexUnit::TT_TEXTURE, tex);
    u->setTextureFilteringOption(bilinear ? LLTexUnit::TFO_BILINEAR : LLTexUnit::TFO_POINT);
    u->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
}

void unbind_units()
{
    for (S32 i = UNIT_COUNT - 1; i >= 0; --i)
    {
        gGL.getTexUnit(i)->unbind(LLTexUnit::TT_TEXTURE);
    }
}

void draw_into(LLRenderTarget& target)
{
    target.bindTarget();
    gPipeline.mScreenTriangleVB->setBuffer();
    gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    target.flush();
}

bool allocate_target(LLRenderTarget& rt, U32 w, U32 h, U32 format, U32 attachments = 1)
{
    if (!rt.allocate(w, h, format, false, LLTexUnit::TT_TEXTURE, LLTexUnit::TMG_NONE))
    {
        return false;
    }
    for (U32 i = 1; i < attachments; ++i)
    {
        if (!rt.addColorAttachment(format))
        {
            return false;
        }
    }
    return true;
}

bool programs_ready()
{
    for (LLGLSLShader& p : gAtmosphereTableProgram)
    {
        if (!p.isComplete())
        {
            return false;
        }
    }
    for (LLGLSLShader& p : gSkyFilterProgram)
    {
        if (!p.isComplete())
        {
            return false;
        }
    }
    return gPipeline.mScreenTriangleVB.notNull();
}

AL::Vec3 to_vec(const LLVector3& v)
{
    return { v.mV[0], v.mV[1], v.mV[2] };
}

// the camera's height above the sea, km, to the metre (the aerial perspective's observer, AT2-3)
double camera_height_km()
{
    const LLViewerRegion* region = gAgent.getRegion();
    const F64 water = region ? (F64)region->getWaterHeight() : 0.0;
    const F64 z = (F64)LLViewerCamera::getInstance()->getOrigin().mV[VZ];
    return std::round(llmax(z - water, 0.0)) * 1e-3;
}

// The base's lights from the sky: the sky's own sun (normalised as the deferred pass has it) and sunlight scale, AT1's moon gain, the
// discs of the sky's sun and moon scales. Returns their key.
uint64_t set_lights(AL::LLScatter& base)
{
    static LLCachedControl<bool> hdr(gSavedSettings, "RenderHDREnabled");
    static LLCachedControl<F32> sunlight_scale(gSavedSettings, "RenderSkySunlightScale", 1.5f);
    static LLCachedControl<F32> sunlight_hdr_scale(gSavedSettings, "RenderHDRSkySunlightScale", 1.5f);

    const LLEnvironment& env = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = env.getCurrentSky();
    const LLColor3 source = env.getDeferredSunlight();
    const F64 scale = hdr ? (F64)sunlight_hdr_scale : (F64)sunlight_scale;
    const F64 gain = env.getPrimaryLight().valid ? (F64)env.getPrimaryLight().moon_gain : 0.0;
    const AL::Vec3 sun = psky ? to_vec(psky->getSunDirection()) : AL::Vec3{ 0.0, 0.0, 1.0 };
    const AL::Vec3 moon = psky ? to_vec(psky->getMoonDirection()) : AL::Vec3{ 0.0, 0.0, -1.0 };
    const F64 sun_disc = AL::body_disc_sin(psky ? psky->getSunScale() : 1.0);
    const F64 moon_disc = AL::body_disc_sin(psky ? psky->getMoonScale() : 1.0);
    AL::set_clear_day_lights(base, sun, moon, { source.mV[0], source.mV[1], source.mV[2] }, scale, gain, sun_disc, moon_disc);

    LLSkyFilter::KeyHasher h;
    for (int i = 0; i < 2; ++i)
    {
        h.direction(base.lights.dir[i]);
        h.colour(base.lights.E[i]);
        h.scalar(base.lights.disc_sin[i]);
    }
    h.add(base.lights.count);
    return h.value();
}

uint64_t combine(uint64_t a, uint64_t b)
{
    LLSkyFilter::KeyHasher h;
    h.add((int64_t)a);
    h.add((int64_t)b);
    return h.value() | 1; // never 0 (0 is "not built")
}
} // namespace

struct LLAtmosphereLUTs::State
{
    AL::LLScatter base = AL::clear_day_scatter();
    AL::AtmosphereBlock block = {};
};

LLAtmosphereLUTs::LLAtmosphereLUTs()
    : mState(std::make_unique<State>())
{
}

LLAtmosphereLUTs::~LLAtmosphereLUTs() = default;

void LLAtmosphereLUTs::release()
{
    if (mUBO)
    {
        glDeleteBuffers(1, &mUBO);
        mUBO = 0;
    }
    mTrans.release();
    mMsPart.release();
    mMs.release();
    mEPart.release();
    mE.release();
    mSkyView.release();
    mFilter.release();
    mRows.release();
    mCastLight.release();
    mAllocated = false;
    mBlockValid = false;
    mBaseBuilt = false;
    mFailed = false;
    mSkyViewKey = 0;
    mFilterKey = 0;
    mCastLightKey = 0;
    mLightKey = 0;
    mUpdatedFrame = U64(-1);
}

bool LLAtmosphereLUTs::allocate()
{
    if (mAllocated)
    {
        return true;
    }
    // T, Psi, E and the sky view are RGBA16F (doc/light/atmosphere-study.md section 4.1, D5 = c); the cast-light sums are RGBA32F (sums
    // of many terms). The split passes' partials live only while the base is built (buildBase).
    const bool ok = allocate_target(mTrans, 256, 64, GL_RGBA16F)
                 && allocate_target(mMs, 32, 32, GL_RGBA16F)
                 && allocate_target(mE, 64, 16, GL_RGBA16F)
                 && allocate_target(mSkyView, LLSkyFilter::TABLE_W, LLSkyFilter::TABLE_H, GL_RGBA16F)
                 && allocate_target(mFilter, LLSkyFilter::TABLE_W, LLSkyFilter::TABLE_H, GL_RGBA16F, 2)
                 && allocate_target(mRows, 1, LLSkyFilter::TABLE_H / 2, GL_RGBA32F)
                 && allocate_target(mCastLight, 1, 1, GL_RGBA32F);
    if (!ok)
    {
        LL_WARNS("Atmosphere") << "The atmosphere's tables could not be allocated" << LL_ENDL;
        release();
        mFailed = true;
        return false;
    }
    glGenBuffers(1, &mUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, mUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(AL::AtmosphereBlock), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    mAllocated = true;
    return true;
}

// The block for the current lights and camera: uploaded (one glBufferSubData) only when it differs from the last upload.
bool LLAtmosphereLUTs::updateBlock(bool force)
{
    mLightKey = set_lights(mState->base);
    const AL::AtmosphereBlock block = AL::fill_atmosphere_block(mState->base, camera_height_km());
    if (!force && mBlockValid && std::memcmp(&block, &mState->block, sizeof(block)) == 0)
    {
        return false;
    }
    mState->block = block;
    glBindBuffer(GL_UNIFORM_BUFFER, mUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(mState->block), &mState->block);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    mBlockValid = true;
    return true;
}

// T, then Psi from T, then E from T and Psi: once (the base is fixed), and again after a release. The split passes' partials (3 MB of
// RGBA32F) exist only for this.
void LLAtmosphereLUTs::buildBase()
{
    LL_PROFILE_GPU_ZONE("atmosphere base tables");
    if (!allocate_target(mMsPart, 256, 256, GL_RGBA32F, 2) || !allocate_target(mEPart, 512, 128, GL_RGBA32F))
    {
        LL_WARNS("Atmosphere") << "The atmosphere's split passes could not be allocated: no tables" << LL_ENDL;
        mMsPart.release();
        mEPart.release();
        mFailed = true;
        return;
    }

    LLGLSLShader& t = gAtmosphereTableProgram[PASS_T];
    t.bind();
    draw_into(mTrans);
    t.unbind();

    LLGLSLShader& split = gAtmosphereTableProgram[PASS_MS_SPLIT];
    split.bind();
    split.uniform1i(sTransLut, UNIT_T);
    bind_unit(UNIT_T, mTrans.getTexture(), true);
    draw_into(mMsPart);
    split.unbind();

    LLGLSLShader& ms = gAtmosphereTableProgram[PASS_MS];
    ms.bind();
    ms.uniform1i(sPart0, UNIT_PART0);
    ms.uniform1i(sPart1, UNIT_PART1);
    bind_unit(UNIT_PART0, mMsPart.getTexture(0), false);
    bind_unit(UNIT_PART1, mMsPart.getTexture(1), false);
    draw_into(mMs);
    ms.unbind();

    LLGLSLShader& esplit = gAtmosphereTableProgram[PASS_E_SPLIT];
    esplit.bind();
    esplit.uniform1i(sTransLut, UNIT_T);
    esplit.uniform1i(sMsLut, UNIT_MS);
    bind_unit(UNIT_MS, mMs.getTexture(), true);
    draw_into(mEPart);
    esplit.unbind();

    LLGLSLShader& e = gAtmosphereTableProgram[PASS_E];
    e.bind();
    e.uniform1i(sPart0, UNIT_PART0);
    bind_unit(UNIT_PART0, mEPart.getTexture(), false);
    draw_into(mE);
    e.unbind();

    unbind_units();
    mMsPart.release();
    mEPart.release();
    mBaseBuilt = true;
}

void LLAtmosphereLUTs::buildSkyView()
{
    LL_PROFILE_GPU_ZONE("atmosphere sky view");
    static LLCachedControl<S32> steps(gSavedSettings, "RenderAtmosphereSkyViewSteps", 64);
    LLGLSLShader& p = gAtmosphereTableProgram[PASS_SKY_VIEW];
    p.bind();
    p.uniform1i(sTransLut, UNIT_T);
    p.uniform1i(sMsLut, UNIT_MS);
    p.uniform1i(sELut, UNIT_E);
    p.uniform1i(sSteps, llclamp((S32)steps, 4, 512));
    bind_unit(UNIT_T, mTrans.getTexture(), true);
    bind_unit(UNIT_MS, mMs.getTexture(), true);
    bind_unit(UNIT_E, mE.getTexture(), true);
    draw_into(mSkyView);
    p.unbind();
}

// the A and B tables: the program is in the sky's shader group, so binding it applies this frame's sky uniforms (the dome's)
void LLAtmosphereLUTs::buildFilter()
{
    LL_PROFILE_GPU_ZONE("sky filter tables");
    LLGLSLShader& p = gSkyFilterProgram[PASS_AB];
    p.bind();
    draw_into(mFilter);
    p.unbind();
}

void LLAtmosphereLUTs::buildCastLight()
{
    LL_PROFILE_GPU_ZONE("sky cast light");
    LLGLSLShader& rows = gSkyFilterProgram[PASS_K_ROWS];
    rows.bind();
    rows.uniform1i(sSkyView, 0);
    rows.uniform1i(sTableA, 1);
    rows.uniform1i(sTableB, 2);
    bind_unit(0, mSkyView.getTexture(), false);
    bind_unit(1, mFilter.getTexture(0), false);
    bind_unit(2, mFilter.getTexture(1), false);
    draw_into(mRows);
    rows.unbind();

    LLGLSLShader& k = gSkyFilterProgram[PASS_K];
    k.bind();
    k.uniform1i(sRows, 3);
    bind_unit(3, mRows.getTexture(), false);
    draw_into(mCastLight);
    k.unbind();
}

void LLAtmosphereLUTs::build(bool allow_filter)
{
    if (mFailed)
    {
        return;
    }
    if (!programs_ready())
    {
        LL_WARNS_ONCE("Atmosphere") << "The atmosphere's table programs are not loaded: no tables" << LL_ENDL;
        return;
    }
    if (!allocate())
    {
        return;
    }

    updateBlock(false);
    static LLCachedControl<S32> steps(gSavedSettings, "RenderAtmosphereSkyViewSteps", 64);
    const uint64_t sky_view_key = combine(mLightKey, (uint64_t)llclamp((S32)steps, 4, 512));

    uint64_t filter_key = mFilterKey;
    if (allow_filter)
    {
        LLSkyFilter::Inputs in;
        filter_key = LLSkyFilter::current_inputs(in) ? combine(LLSkyFilter::inputs_key(in), 0) : 0;
    }
    const bool base = !mBaseBuilt;
    const bool sky_view = base || sky_view_key != mSkyViewKey;
    const bool filter = allow_filter && filter_key != 0 && filter_key != mFilterKey;
    const uint64_t cast_key = combine(sky_view_key, filter_key);
    const bool cast = allow_filter && filter_key != 0 && cast_key != mCastLightKey;
    if (!(base || sky_view || filter || cast))
    {
        return; // the steady state: the key and the block compare only
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    gGL.flush();
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLDisable blend(GL_BLEND);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLDisable scissor(GL_SCISSOR_TEST);
    gGL.setColorMask(true, true);
    glBindBufferBase(GL_UNIFORM_BUFFER, LLGLSLShader::UB_ATMOSPHERE, mUBO);

    if (base)
    {
        buildBase();
        if (!mBaseBuilt)
        {
            unbind_units();
            return;
        }
    }
    if (sky_view)
    {
        buildSkyView();
        mSkyViewKey = sky_view_key;
    }
    if (filter)
    {
        buildFilter();
        mFilterKey = filter_key;
    }
    if (cast && mSkyViewKey != 0 && mFilterKey != 0)
    {
        buildCastLight();
        mCastLightKey = cast_key;
    }
    unbind_units();
}

void LLAtmosphereLUTs::update()
{
    if (gCubeSnapshot)
    {
        return;
    }
    const U64 frame = LLFrameTimer::getFrameCount();
    if (frame == mUpdatedFrame)
    {
        return;
    }
    mUpdatedFrame = frame;
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    build(true);
}

void LLAtmosphereLUTs::ensureTables()
{
    if (mFailed || (mBaseBuilt && mSkyViewKey != 0 && (gCubeSnapshot || mFilterKey != 0)))
    {
        return;
    }
    build(!gCubeSnapshot);
}

/**
 * @file llstochastichistory.cpp
 * @brief The stochastic engine: the mask table and counted textures, and one producer's temporal history
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

#include "llstochastichistory.h"

#include "llframetimer.h"
#include "llglslshader.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llstaticstringtable.h"

#include "glm/gtc/type_ptr.hpp"

namespace LLStochasticEngine
{

std::vector<U32> maskTable(U32 samples)
{
    const U32 s = llclamp(samples, 1u, 32u);
    U64 state = 0x9e3779b97f4a7c15ULL ^ (U64)s;
    auto next = [&state]()
    {
        state += 0x9e3779b97f4a7c15ULL;
        U64 z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    };
    std::vector<U32> table;
    table.reserve((size_t)(s + 1) * SEEDS);
    std::vector<U32> bits(s);
    for (U32 r = 0; r <= s; ++r)
    {
        for (U32 seed = 0; seed < SEEDS; ++seed)
        {
            for (U32 i = 0; i < s; ++i)
            {
                bits[i] = i;
            }
            U32 mask = 0;
            for (U32 i = 0; i < r; ++i)
            {
                const U32 j = i + (U32)(next() % (U64)(s - i));
                std::swap(bits[i], bits[j]);
                mask |= 1u << bits[i];
            }
            table.push_back(mask);
        }
    }
    return table;
}

namespace
{
    U32 sMasks = 0;
    U32 sMasksHolders = 0;

    // The texture is bound on gGL's unit 0, where the texture memory count reads it, and the unit
    // left unbound: gGL's cache of the unit stays what GL holds.
    void bind_for_allocation(LLTexUnit::eTextureType type, U32 name)
    {
        gGL.flush();
        gGL.getTexUnit(0)->bindManual(type, name);
    }
}

U32 newTexture2D(GLenum format, U32 width, U32 height, GLenum counted_format,
                 const void* data, GLenum data_format, GLenum data_type)
{
    U32 name = 0;
    glGenTextures(1, &name);
    if (!name)
    {
        return 0;
    }
    bind_for_allocation(LLTexUnit::TT_TEXTURE, name);
    glTexImage2D(GL_TEXTURE_2D, 0, format, (GLsizei)width, (GLsizei)height, 0, data_format, data_type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    LLImageGLMemory::alloc_tex_image(width, height, counted_format, 1);
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    return name;
}

U32 newTextureMultisample(GLenum format, U32 width, U32 height, U32 samples, GLenum counted_format)
{
    U32 name = 0;
    glGenTextures(1, &name);
    if (!name)
    {
        return 0;
    }
    bind_for_allocation(LLTexUnit::TT_MULTISAMPLE_TEXTURE, name);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, (GLsizei)samples, format, (GLsizei)width, (GLsizei)height, GL_TRUE);
    LLImageGLMemory::alloc_tex_image(width, height, counted_format, samples);
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_MULTISAMPLE_TEXTURE);
    return name;
}

void deleteTexture(U32& name)
{
    if (!name)
    {
        return;
    }
    // no unit may keep the deleted name as its current texture (bindManual's cache)
    for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
    {
        LLTexUnit* tex_unit = gGL.getTexUnit(unit);
        if (tex_unit && tex_unit->getCurrTexture() == name)
        {
            tex_unit->unbind(tex_unit->getCurrType());
        }
    }
    LLImageGLMemory::free_tex_image(name);
    glDeleteTextures(1, &name);
    name = 0;
}

bool acquireMasks()
{
    if (!sMasks)
    {
        const std::vector<U32> table = maskTable(SAMPLES);
        sMasks = newTexture2D(GL_R32UI, SEEDS, SAMPLES + 1, GL_R32UI, table.data(), GL_RED_INTEGER, GL_UNSIGNED_INT);
        if (!sMasks)
        {
            return false;
        }
    }
    ++sMasksHolders;
    return true;
}

void releaseMasks()
{
    if (sMasksHolders && --sMasksHolders == 0)
    {
        deleteTexture(sMasks);
    }
}

U32 getMasks()
{
    return sMasks;
}

} // namespace LLStochasticEngine

bool LLStochasticHistory::allocate(U32 width, U32 height)
{
    release();
    if (!width || !height)
    {
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    bool ok = true;
    for (U32 i = 0; i < 2; ++i)
    {
        mTexture[i] = LLStochasticEngine::newTexture2D(mFormat, width, height, mFormat);
        ok = ok && mTexture[i];
        glGenFramebuffers(1, &mFramebuffer[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTexture[i], 0);
        ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, LLRenderTarget::sCurFBO);
    if (!ok || glGetError() != GL_NO_ERROR)
    {
        release();
        return false;
    }
    mWidth = width;
    mHeight = height;
    mIndex = 0;
    mValid = false;
    return true;
}

void LLStochasticHistory::release()
{
    for (U32 i = 0; i < 2; ++i)
    {
        if (mFramebuffer[i])
        {
            glDeleteFramebuffers(1, &mFramebuffer[i]);
            mFramebuffer[i] = 0;
        }
        LLStochasticEngine::deleteTexture(mTexture[i]);
    }
    mWidth = mHeight = 0;
    mValid = false;
    mTemporal = false;
    mUsed = false;
}

bool LLStochasticHistory::beginFrame(bool enabled)
{
    mTemporal = enabled && isAllocated();
    mUsed = false;
    if (mTemporal)
    {
        if (++mFrame == 0)
        {
            mFrame = 1;
        }
        mUsed = mValid && mStamp + 1 == LLFrameTimer::getFrameCount();
    }
    return mTemporal;
}

void LLStochasticHistory::setResolveUniforms(LLGLSLShader& shader)
{
    static const LLStaticHashedString sReproj("stoch_reproj");
    static const LLStaticHashedString sWeight("stoch_weight");
    static const LLStaticHashedString sValid("stoch_history_valid");

    mModelView = get_current_modelview();
    mProjection = get_current_projection();
    // this frame's clip space to the last frame's: the world position of the surface, through the
    // last frame's camera
    const glm::mat4 reproj = mLastProjection * mLastModelView * glm::inverse(mProjection * mModelView);
    shader.uniformMatrix4fv(sReproj, 1, GL_FALSE, glm::value_ptr(reproj));
    shader.uniform1f(sWeight, 1.f / 8.f);
    shader.uniform1i(sValid, mUsed ? 1 : 0);
}

void LLStochasticHistory::endFrame()
{
    mIndex = 1 - mIndex;
    mValid = true;
    mStamp = LLFrameTimer::getFrameCount();
    mLastModelView = mModelView;
    mLastProjection = mProjection;
}

U64 LLStochasticHistory::bytes() const
{
    return isAllocated() ? 2 * (U64)LLImageGL::dataFormatBytes(mFormat, mWidth, mHeight) : 0;
}

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

namespace
{
    U32 sMasks = 0;
    U32 sMasksHolders = 0;

    // The texture is bound on gGL's unit 0, where the texture memory count reads it, and the unit
    // left unbound: gGL's cache of the unit stays what GL holds. A new name can equal the one a
    // deleted texture left in the unit's cache (bindManual would then skip the GL bind and the
    // allocation would land on the wrong texture): the unit is unbound first in that case.
    void bind_for_allocation(LLTexUnit::eTextureType type, U32 name)
    {
        gGL.flush();
        LLTexUnit* unit = gGL.getTexUnit(0);
        if (unit->getCurrTexture() == name)
        {
            unit->unbind(unit->getCurrType());
        }
        unit->bindManual(type, name);
    }

    // The error of the allocation just made, read before anything else can drain it (stop_glerror
    // does under gDebugGL): out of memory is a failed allocation.
    bool allocation_failed(const char* what, U32 width, U32 height)
    {
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            LL_WARNS("Pipeline") << "Stochastic engine: " << what << " " << width << "x" << height
                                 << " could not be allocated (GL error " << error << ")" << LL_ENDL;
            while (glGetError() != GL_NO_ERROR) {}
            return true;
        }
        return false;
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
    while (glGetError() != GL_NO_ERROR) {}
    glTexImage2D(GL_TEXTURE_2D, 0, format, (GLsizei)width, (GLsizei)height, 0, data_format, data_type, data);
    if (allocation_failed("texture", width, height))
    {
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        glDeleteTextures(1, &name);
        return 0;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    LLImageGLMemory::alloc_tex_image_exact(width, height, counted_format, 1);
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
    while (glGetError() != GL_NO_ERROR) {}
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, (GLsizei)samples, format, (GLsizei)width, (GLsizei)height, GL_TRUE);
    if (allocation_failed("multisample texture", width, height))
    {
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_MULTISAMPLE_TEXTURE);
        glDeleteTextures(1, &name);
        return 0;
    }
    LLImageGLMemory::alloc_tex_image_exact(width, height, counted_format, samples);
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

bool deviceSupported()
{
    // 3.99 / 4.29 as llgl.cpp: mGLVersion is a float
    const bool ok = gGLManager.mGLVersion >= 3.99f && gGLManager.mGLSLVersionMajor >= 4;
    if (!ok)
    {
        LL_INFOS_ONCE("Pipeline") << "Stochastic transparency needs OpenGL 4.0; this device reports GL "
                                  << gGLManager.mGLVersion << ", GLSL " << gGLManager.mGLSLVersionMajor << "."
                                  << gGLManager.mGLSLVersionMinor << ": sorted alpha is used" << LL_ENDL;
    }
    return ok;
}

bool computeResolve()
{
    const bool glsl = gGLManager.mGLSLVersionMajor > 4 || (gGLManager.mGLSLVersionMajor == 4 && gGLManager.mGLSLVersionMinor >= 30);
    const bool capable = gGLManager.mGLVersion >= 4.29f && glsl;
    // debug only: force the fragment path on a GL 4.3 device (tests), read once
    static const bool force_fragment = []() {
        const char* v = getenv("VKSTORM_STOCH_FRAGMENT_RESOLVE");
        return v && v[0] == '1';
    }();
    const bool compute = capable && !force_fragment;
    static bool logged = false;   // main render thread, the pipeline's allocation
    if (!logged && gGLManager.mGLVersion >= 3.99f)
    {
        logged = true;
        LL_INFOS("Pipeline") << "Stochastic transparency: " << (compute ? "compute resolve (GL 4.3 and later, 144 B/px)"
                                                                          : "fragment resolve with the pre-pass target (GL 4.0 to 4.2, 160 B/px)")
                             << (force_fragment ? ", forced by VKSTORM_STOCH_FRAGMENT_RESOLVE" : "") << LL_ENDL;
    }
    return compute;
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

bool LLStochasticHistory::allocate(U32 width, U32 height, bool framebuffers)
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
        if (framebuffers)
        {
            glGenFramebuffers(1, &mFramebuffer[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTexture[i], 0);
            ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
    }
    if (framebuffers)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, LLRenderTarget::sCurFBO);
    }
    if (!ok || glGetError() != GL_NO_ERROR)
    {
        release();
        return false;
    }
    mWidth = width;
    mHeight = height;
    mState.reset(width, height);
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
    mState.clear();
}

bool LLStochasticHistory::beginFrame(bool enabled)
{
    mCameraSet = false;
    return mState.begin(enabled && isAllocated(), LLFrameTimer::getFrameCount());
}

LLStochasticHistory::~LLStochasticHistory()
{
    if (isAllocated())
    {
        LL_WARNS("Pipeline") << "Stochastic history destroyed while allocated (release() it with the GL context current)" << LL_ENDL;
    }
}

void LLStochasticHistory::bindWriteImage(U32 unit) const
{
    // alphaStochasticResolveC.glsl declares layout(rgba16f)
    llassert(mFormat == GL_RGBA16F);
    glBindImageTexture(unit, mTexture[1 - mState.index()], 0, GL_FALSE, 0, GL_WRITE_ONLY, mFormat);
}

void LLStochasticHistory::endComputeWrite(U32 unit) const
{
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glBindImageTexture(unit, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, mFormat);
}

void LLStochasticHistory::setCamera(const glm::mat4& modelview, const glm::mat4& projection)
{
    mModelView = modelview;
    mProjection = projection;
    mCameraSet = true;
}

// this frame's clip space to the last frame's: the world position of the surface, through the
// last frame's camera
glm::mat4 LLStochasticHistory::reprojection() const
{
    return mLastProjection * mLastModelView * glm::inverse(mProjection * mModelView);
}

void LLStochasticHistory::setResolveUniforms(LLGLSLShader& shader)
{
    static const LLStaticHashedString sReproj("stoch_reproj");
    static const LLStaticHashedString sWeight("stoch_weight");
    static const LLStaticHashedString sValid("stoch_history_valid");

    setCamera(get_current_modelview(), get_current_projection());
    const glm::mat4 reproj = reprojection();
    shader.uniformMatrix4fv(sReproj, 1, GL_FALSE, glm::value_ptr(reproj));
    shader.uniform1f(sWeight, mWeight);
    shader.uniform1i(sValid, mState.used() ? 1 : 0);
}

void LLStochasticHistory::endFrame()
{
    if (!mCameraSet)
    {
        // the camera of this frame is what the next frame reprojects from
        setCamera(get_current_modelview(), get_current_projection());
    }
    mCameraSet = false;
    mState.end(LLFrameTimer::getFrameCount());
    mLastModelView = mModelView;
    mLastProjection = mProjection;
}

U64 LLStochasticHistory::bytes() const
{
    return isAllocated() ? 2 * (U64)LLImageGL::dataFormatBytes(mFormat, mWidth, mHeight) : 0;
}

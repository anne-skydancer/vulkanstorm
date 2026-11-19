/**
 * @file lllightthroughgl.cpp
 * @brief Lamp shadows L3: a lamp pool's light-through layer (the GL half of lllightthrough.h)
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

#include "lllightthroughgl.h"

#include "llglslshader.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstochastichistory.h"
#include "llthread.h"

namespace
{
    // "no caster": tau 1 in every channel, the depth at the far plane (lllightthrough::kNoLayer)
    const GLubyte kTauClear[4] = { 255, 255, 255, 255 };
    const GLushort kDepthClear = 0xFFFF;

    // the two textures of one rect size: the layer's or a transient of the carry. NEAREST, no mips (newTexture2D); counted in the
    // viewer's texture memory at their real size (R16 is counted as R16F, the same two bytes: LLImageGL does not know R16)
    bool new_pair(U32 size, U32& tau, U32& depth)
    {
        tau = LLStochasticEngine::newTexture2D(GL_RGBA8, size, size, GL_RGBA8, nullptr, GL_RGBA, GL_UNSIGNED_BYTE);
        depth = tau ? LLStochasticEngine::newTexture2D(GL_R16, size, size, GL_R16F, nullptr, GL_RED, GL_UNSIGNED_SHORT) : 0;
        if (!depth)
        {
            LLStochasticEngine::deleteTexture(tau);
            return false;
        }
        return true;
    }

    void copy_rect(U32 from, const llprojectorpool::Rect& src, U32 to, GLint x, GLint y)
    {
        glCopyImageSubData(from, GL_TEXTURE_2D, 0, (GLint)src.x, (GLint)src.y, 0, to, GL_TEXTURE_2D, 0, x, y, 0, (GLsizei)src.size,
                           (GLsizei)src.size, 1);
    }
}

LLLightThroughLayer::~LLLightThroughLayer()
{
    // no GL here: the owner frees the layer with its atlases, with the context alive
    llassert(mTau == 0 && mDepth == 0 && mFbo == 0);
}

void LLLightThroughLayer::setEdge(U32 edge)
{
    llassert(!allocated() || edge == mEdge);
    mEdge = edge;
    mFailed = false;
}

bool LLLightThroughLayer::ensure()
{
    if (allocated())
    {
        return true;
    }
    // glClearTexImage and glClearTexSubImage are OpenGL 4.4, glCopyImageSubData 4.3 (llgl.cpp loads them from those versions)
    if (mFailed || mEdge == 0 || !on_main_thread() || gGLManager.mGLVersion < 4.39f)
    {
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    if (!new_pair(mEdge, mTau, mDepth))
    {
        LL_WARNS("ProjectorShadows") << "light-through layer " << mEdge << "x" << mEdge << " could not be allocated: see-through casters "
                                     << "shadow lamps as before (dithered into the depth) until the shadow atlases are made again" << LL_ENDL;
        mFailed = true;
        return false;
    }
    // tau is read bilinearly (the lookup's tap), the depth gathered; both clamped (the lookup keeps every read inside its tile anyway)
    gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, mTau);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, mDepth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    const U32 saved_fbo = LLRenderTarget::sCurFBO;
    glGenFramebuffers(1, &mFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTau, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mDepth, 0);
    const GLenum targets[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, targets);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, saved_fbo);
    const GLenum error = glGetError();
    if (!complete || error != GL_NO_ERROR)
    {
        LL_WARNS("ProjectorShadows") << "light-through layer " << mEdge << "x" << mEdge << " could not be set up (framebuffer "
                                     << (complete ? "complete" : "incomplete") << ", GL error " << error << "): see-through casters "
                                     << "shadow lamps as before" << LL_ENDL;
        while (glGetError() != GL_NO_ERROR) {}
        const U32 edge = mEdge;
        free();
        mEdge = edge;
        mFailed = true;
        return false;
    }
    glClearTexImage(mTau, 0, GL_RGBA, GL_UNSIGNED_BYTE, kTauClear);
    glClearTexImage(mDepth, 0, GL_RED, GL_UNSIGNED_SHORT, &kDepthClear);
    LL_INFOS("ProjectorShadows") << "light-through layer: " << mEdge << "x" << mEdge << " (" << (lllightthrough::layerBytes(mEdge) >> 20)
                                 << " MiB), made for the first tile that met a see-through caster" << LL_ENDL;
    return true;
}

void LLLightThroughLayer::free()
{
    if (mFbo)
    {
        // a framebuffer that is the current one must not stay so (LLRenderTarget::release does the same)
        if (LLRenderTarget::sCurFBO == mFbo)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            LLRenderTarget::sCurFBO = 0;
        }
        glDeleteFramebuffers(1, &mFbo);
        mFbo = 0;
    }
    // deleteTexture unbinds the name from every texture unit first and uncounts it
    LLStochasticEngine::deleteTexture(mTau);
    LLStochasticEngine::deleteTexture(mDepth);
    mEdge = 0;
    mFailed = false;
}

void LLLightThroughLayer::clearRect(const llprojectorpool::Rect& rect)
{
    if (!allocated() || rect.size == 0)
    {
        return;
    }
    llassert(rect.x + rect.size <= mEdge && rect.y + rect.size <= mEdge);
    glClearTexSubImage(mTau, 0, (GLint)rect.x, (GLint)rect.y, 0, (GLsizei)rect.size, (GLsizei)rect.size, 1, GL_RGBA, GL_UNSIGNED_BYTE, kTauClear);
    glClearTexSubImage(mDepth, 0, (GLint)rect.x, (GLint)rect.y, 0, (GLsizei)rect.size, (GLsizei)rect.size, 1, GL_RED, GL_UNSIGNED_SHORT, &kDepthClear);
}

std::vector<U32> LLLightThroughLayer::carry(const lllightthrough::CarryPlan& plan)
{
    std::vector<U32> lost;
    if (!allocated())
    {
        return lost;
    }
    struct Held
    {
        U32 tau = 0;
        U32 depth = 0;
    };
    std::vector<Held> held(plan.copies.size());
    // every source first: a move's new rect may be another move's old one
    for (size_t i = 0; i < plan.copies.size(); ++i)
    {
        const llprojectorpool::Move& move = plan.copies[i];
        llassert(move.src.size == move.dst.size);
        if (new_pair(move.src.size, held[i].tau, held[i].depth))
        {
            copy_rect(mTau, move.src, held[i].tau, 0, 0);
            copy_rect(mDepth, move.src, held[i].depth, 0, 0);
        }
    }
    for (const llprojectorpool::Rect& rect : plan.clears)
    {
        clearRect(rect);
    }
    for (size_t i = 0; i < plan.copies.size(); ++i)
    {
        const llprojectorpool::Move& move = plan.copies[i];
        if (!held[i].tau)
        {
            clearRect(move.dst);
            lost.push_back(move.key);
            continue;
        }
        llprojectorpool::Rect whole;
        whole.size = move.src.size;
        copy_rect(held[i].tau, whole, mTau, (GLint)move.dst.x, (GLint)move.dst.y);
        copy_rect(held[i].depth, whole, mDepth, (GLint)move.dst.x, (GLint)move.dst.y);
        LLStochasticEngine::deleteTexture(held[i].tau);
        LLStochasticEngine::deleteTexture(held[i].depth);
    }
    if (!lost.empty())
    {
        LL_WARNS("ProjectorShadows") << lost.size() << " light-through tile(s) could not be carried through a repack: drawn again" << LL_ENDL;
    }
    return lost;
}

void LLLightThroughLayer::bind(LLGLSLShader& shader) const
{
    if (!allocated())
    {
        return;
    }
    const S32 tau = shader.enableTexture(LLShaderMgr::PROJ_THROUGH_ATLAS, LLTexUnit::TT_TEXTURE);
    if (tau > -1)
    {
        gGL.getTexUnit(tau)->bindManual(LLTexUnit::TT_TEXTURE, mTau);
    }
    const S32 depth = shader.enableTexture(LLShaderMgr::PROJ_THROUGH_DEPTH, LLTexUnit::TT_TEXTURE);
    if (depth > -1)
    {
        gGL.getTexUnit(depth)->bindManual(LLTexUnit::TT_TEXTURE, mDepth);
    }
}

void LLLightThroughLayer::unbindUnits() const
{
    if (!allocated())
    {
        return;
    }
    for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
    {
        LLTexUnit* tex_unit = gGL.getTexUnit(unit);
        if (tex_unit && (tex_unit->getCurrTexture() == mTau || tex_unit->getCurrTexture() == mDepth))
        {
            tex_unit->unbind(tex_unit->getCurrType());
        }
    }
}

LLLightThroughLayer::DrawScope::DrawScope(const LLLightThroughLayer& layer, bool color_writes_after)
    : mBlend(GL_BLEND), mDepth(GL_FALSE, GL_FALSE), mSavedFbo(LLRenderTarget::sCurFBO), mColorAfter(color_writes_after)
{
    llassert(layer.allocated());
    gGL.flush();
    glBindFramebuffer(GL_FRAMEBUFFER, layer.mFbo);
    LLRenderTarget::sCurFBO = layer.mFbo;
    gGL.setColorMask(true, true);
    gGL.blendFunc(LLRender::BF_ZERO, LLRender::BF_SOURCE_COLOR); // tau: the product (both targets get it; GL_MIN ignores it)
    glBlendEquationi(1, GL_MIN);                                 // the depth: the nearest
}

LLLightThroughLayer::DrawScope::~DrawScope()
{
    gGL.flush();
    glBlendEquationi(1, GL_FUNC_ADD);
    gGL.setSceneBlendType(LLRender::BT_ALPHA);
    gGL.setColorMask(mColorAfter, mColorAfter);
    glBindFramebuffer(GL_FRAMEBUFFER, mSavedFbo);
    LLRenderTarget::sCurFBO = mSavedFbo;
}

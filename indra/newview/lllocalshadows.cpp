/**
 * @file lllocalshadows.cpp
 * @brief What the local shadow caches share: one save and restore of the GL state their tile renders change
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

#include "lllocalshadows.h"

#include "llapp.h"
#include "llglslshader.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llthread.h"
#include "llviewercamera.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

namespace
{
    // the viewer's GL state the tile renders change, put back by the last scope of the frame
    struct SavedState
    {
        U32 fbo = 0;
        U32 resX = 0, resY = 0;
        S32 viewport[4] = {};
        glm::mat4 view, proj, lastView, lastProj;
        LLViewerCamera::eCameraID cameraId = LLViewerCamera::CAMERA_WORLD;
    };

    // main-thread state: the scopes only open and close on the render thread (a scope off it is inactive and does nothing)
    llprojectorpool::ScopeCounter sScopes;
    SavedState sSaved;
}

LLLocalShadowGL::Scope::Scope() : mActive(on_main_thread() && !LLApp::isExiting())
{
    if (!mActive || !sScopes.enter())
    {
        return;
    }
    sSaved.fbo = LLRenderTarget::sCurFBO;
    sSaved.resX = LLRenderTarget::sCurResX;
    sSaved.resY = LLRenderTarget::sCurResY;
    for (int i = 0; i < 4; ++i)
    {
        sSaved.viewport[i] = gGLViewport[i];
    }
    sSaved.view = get_current_modelview();
    sSaved.proj = get_current_projection();
    sSaved.lastView = get_last_modelview();
    sSaved.lastProj = get_last_projection();
    sSaved.cameraId = LLViewerCamera::sCurCameraID;
}

LLLocalShadowGL::Scope::~Scope()
{
    if (!mActive || !sScopes.leave())
    {
        return;
    }
    // put back what the tile renders changed
    setTileOrigin(0.f, 0.f);
    gDeferredShadowCubeProgram.bind(); // the program renderShadow leaves bound
    glBindFramebuffer(GL_FRAMEBUFFER, sSaved.fbo);
    LLRenderTarget::sCurFBO = sSaved.fbo;
    for (int i = 0; i < 4; ++i)
    {
        gGLViewport[i] = sSaved.viewport[i];
    }
    glViewport(sSaved.viewport[0], sSaved.viewport[1], sSaved.viewport[2], sSaved.viewport[3]);
    glScissor(sSaved.viewport[0], sSaved.viewport[1], sSaved.viewport[2], sSaved.viewport[3]); // not the last tile's box
    LLRenderTarget::sCurResX = sSaved.resX;
    LLRenderTarget::sCurResY = sSaved.resY;
    set_current_modelview(sSaved.view);
    set_current_projection(sSaved.proj);
    set_last_modelview(sSaved.lastView);
    set_last_projection(sSaved.lastProj);
    LLViewerCamera::sCurCameraID = sSaved.cameraId;
}

// static
void LLLocalShadowGL::setTileOrigin(F32 x, F32 y, S32 frame)
{
    // the programs of the semi-transparent casters' rule: the texel origin of its blocks, the tile's (not the atlas') or the
    // sun cascade's wrapped world-grid index (generateSunCascadesStable), and the frame of its pattern
    static const LLStaticHashedString sCasterFrame("shadow_caster_frame");
    LLGLSLShader* programs[] = {
        &gDeferredShadowAlphaMaskProgram, &gDeferredShadowFullbrightAlphaMaskProgram, &gDeferredShadowGLTFAlphaBlendProgram,
        &gDeferredAvatarAlphaShadowProgram, &gDeferredAvatarAlphaMaskShadowProgram
    };
    for (LLGLSLShader* program : programs)
    {
        for (LLGLSLShader* variant : { program, program->mRiggedVariant })
        {
            if (variant && variant->mProgramObject)
            {
                variant->bind();
                variant->uniform2f(LLShaderMgr::SHADOW_TILE_ORIGIN, x, y);
                variant->uniform1i(sCasterFrame, frame);
            }
        }
    }
}

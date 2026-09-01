/**
 * @file llvkui2d.cpp
 * @brief Implementation of the independent Vulkan 2D/UI emission sink
 *        (see llvkui2d.h).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llvkui2d.h"

#include "llerror.h"
#include "llvkcontext.h"

#include <cstring>

namespace
{
    LLVKUI2D s_ui;

    LLVKContext::Blend2D toContextBlend(LLVKBlend b)
    {
        switch (b)
        {
        case LLVKBlend::Alpha:         return LLVKContext::Blend2D::Alpha;
        case LLVKBlend::Replace:       return LLVKContext::Blend2D::Replace;
        case LLVKBlend::AddWithAlpha:  return LLVKContext::Blend2D::AddWithAlpha;
        case LLVKBlend::Add:           return LLVKContext::Blend2D::Add;
        }
        return LLVKContext::Blend2D::Alpha;
    }
}

void LLVKUI2D::begin(LLVKContext* ctx, VkCommandBuffer cmd)
{
    mCtx = ctx;
    mCmd = cmd;
    mVerts.clear();
    mTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // Reset pending state to the frame defaults (matches GL's UI-pass init:
    // BT_ALPHA, untextured=white, no scissor, identity transform).
    mBlend = LLVKBlend::Alpha;
    mTexture = VK_NULL_HANDLE;
    mScissorOn = false;
    mOffX = 0.f; mOffY = 0.f; mScaleX = 1.f; mScaleY = 1.f;
}

void LLVKUI2D::end()
{
    flushRun();
    mCmd = VK_NULL_HANDLE;
    mCtx = nullptr;
}

void LLVKUI2D::setBlend(LLVKBlend blend)
{
    if (blend == mBlend) return;
    flushRun();            // flush-before-mutate
    mBlend = blend;
}

void LLVKUI2D::setTexture(VkDescriptorSet descriptor)
{
    if (descriptor == mTexture) return;
    flushRun();            // flush-before-mutate
    mTexture = descriptor;
}

void LLVKUI2D::setScissor(int x, int y, int w, int h)
{
    if (mScissorOn && x == mSx && y == mSy && w == mSw && h == mSh) return;
    flushRun();
    mScissorOn = true;
    mSx = x; mSy = y; mSw = w; mSh = h;
}

void LLVKUI2D::clearScissor()
{
    if (!mScissorOn) return;
    flushRun();
    mScissorOn = false;
}

void LLVKUI2D::setTransform(float off_x, float off_y, float scale_x, float scale_y)
{
    mOffX = off_x; mOffY = off_y; mScaleX = scale_x; mScaleY = scale_y;
}

void LLVKUI2D::rect(float left, float top, float right, float bottom,
                    float r, float g, float b, float a)
{
    if (!isActive()) return;
    if (mTopo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) { flushRun(); mTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; }

    // Bake the UI transform at emit time (matches LLRender::vertex3f).
    const float x0 = (left + mOffX) * mScaleX,  y0 = (top + mOffY) * mScaleY;
    const float x1 = (right + mOffX) * mScaleX, y1 = (bottom + mOffY) * mScaleY;

    // Two triangles, counterclockwise, matching gl_rect_2d's winding.
    mVerts.push_back({ x0, y0, 0, 0, r, g, b, a });
    mVerts.push_back({ x0, y1, 0, 1, r, g, b, a });
    mVerts.push_back({ x1, y1, 1, 1, r, g, b, a });
    mVerts.push_back({ x0, y0, 0, 0, r, g, b, a });
    mVerts.push_back({ x1, y1, 1, 1, r, g, b, a });
    mVerts.push_back({ x1, y0, 1, 0, r, g, b, a });
}

void LLVKUI2D::texturedQuad(float left, float top, float right, float bottom,
                            float u0, float v0, float u1, float v1,
                            float r, float g, float b, float a)
{
    if (!isActive()) return;
    if (mTopo != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) { flushRun(); mTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; }

    const float x0 = (left + mOffX) * mScaleX,  y0 = (top + mOffY) * mScaleY;
    const float x1 = (right + mOffX) * mScaleX, y1 = (bottom + mOffY) * mScaleY;

    mVerts.push_back({ x0, y0, u0, v0, r, g, b, a });
    mVerts.push_back({ x0, y1, u0, v1, r, g, b, a });
    mVerts.push_back({ x1, y1, u1, v1, r, g, b, a });
    mVerts.push_back({ x0, y0, u0, v0, r, g, b, a });
    mVerts.push_back({ x1, y1, u1, v1, r, g, b, a });
    mVerts.push_back({ x1, y0, u1, v0, r, g, b, a });
}

void LLVKUI2D::lineStrip(const float* xy, int count, float r, float g, float b, float a)
{
    if (!isActive() || count < 2) return;
    if (mTopo != VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) { flushRun(); mTopo = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; }
    for (int i = 0; i < count; ++i)
    {
        const float x = (xy[i * 2] + mOffX) * mScaleX;
        const float y = (xy[i * 2 + 1] + mOffY) * mScaleY;
        mVerts.push_back({ x, y, 0, 0, r, g, b, a });
    }
}

void LLVKUI2D::flushRun()
{
    if (!isActive() || mVerts.empty() || !mCtx)
    {
        return;
    }

    const VkDeviceSize bytes = (VkDeviceSize)(mVerts.size() * sizeof(Vert));

    if (bytes > mVBufCapacity)
    {
        if (mVBuf != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(mCtx->device());
            vmaDestroyBuffer(mCtx->allocator(), mVBuf, mVBufAlloc);
            mVBuf = VK_NULL_HANDLE;
            mVBufAlloc = VK_NULL_HANDLE;
        }
        VkDeviceSize cap = bytes * 2;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = cap;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vinfo{};
        if (vmaCreateBuffer(mCtx->allocator(), &bi, &ai, &mVBuf, &mVBufAlloc, &vinfo) != VK_SUCCESS)
        {
            LL_WARNS("Vulkan") << "UI2D: vertex buffer alloc failed" << LL_ENDL;
            mVerts.clear();
            return;
        }
        mVBufCapacity = cap;
    }

    VmaAllocationInfo vinfo{};
    vmaGetAllocationInfo(mCtx->allocator(), mVBufAlloc, &vinfo);
    memcpy(vinfo.pMappedData, mVerts.data(), (size_t)bytes);

    // Ortho projection (top-left origin via the negative-height viewport).
    const float W = (float)mCtx->swapchainExtent().width;
    const float H = (float)mCtx->swapchainExtent().height;
    float ortho[16] = {
        2.f / W, 0.f,      0.f, 0.f,
        0.f,     2.f / H,  0.f, 0.f,
        0.f,     0.f,     -1.f, 0.f,
       -1.f,    -1.f,      0.f, 1.f
    };
    vkCmdPushConstants(mCmd, mCtx->pipelineLayout2D(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ortho), ortho);

    // Scissor (read at flush).
    VkRect2D scissor{ { 0, 0 }, mCtx->swapchainExtent() };
    if (mScissorOn)
    {
        scissor.offset = { mSx, mSy };
        scissor.extent = { (uint32_t)mSw, (uint32_t)mSh };
    }
    vkCmdSetScissor(mCmd, 0, 1, &scissor);

    // Bind the pipeline for the current blend mode, and the texture (white when
    // untextured).
    vkCmdBindPipeline(mCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCtx->pipeline2D(toContextBlend(mBlend)));
    VkDescriptorSet tex = (mTexture != VK_NULL_HANDLE) ? mTexture : mCtx->whiteTextureDescriptor();
    mCtx->bindTexture2D(mCmd, tex);

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(mCmd, 0, 1, &mVBuf, &off);
    vkCmdDraw(mCmd, (uint32_t)mVerts.size(), 1, 0, 0);

    mVerts.clear();
}

// --- Global sink ------------------------------------------------------------

namespace LLVKUI2DSink
{
    LLVKUI2D& get() { return s_ui; }
}

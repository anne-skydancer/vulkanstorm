/**
 * @file llvksession.cpp
 * @brief Implementation of the Vulkan session (see llvksession.h).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llvksession.h"

#include "llerror.h"

#if LL_WINDOWS

#include "llvkcontext.h"
#include "llwindow.h"

#include <cstring>

namespace
{
    LLVKContext* s_context = nullptr;
    VkSurfaceKHR s_surface = VK_NULL_HANDLE;
    uint32_t     s_width = 0;
    uint32_t     s_height = 0;

    // Boot-path clear color. Matches the Phase-1 self-test teal so a Vulkan-
    // owned frame is unmistakable during bring-up.
    constexpr float kClearR = 0.0f;
    constexpr float kClearG = 0.5f;
    constexpr float kClearB = 0.5f;
    constexpr float kClearA = 1.0f;

    // Persistent vertex buffer for the 2D test rect (bring-up; a real batching
    // buffer lands in 3a-2).
    VkBuffer      s_test_vbuf = VK_NULL_HANDLE;
    VmaAllocation s_test_vbuf_alloc = VK_NULL_HANDLE;

    void queryClientSize(LLWindow* window, uint32_t& width, uint32_t& height)
    {
        width = 0;
        height = 0;
        if (window)
        {
            LLCoordWindow size;
            if (window->getSize(&size))
            {
                width = (uint32_t)llmax(1, size.mX);
                height = (uint32_t)llmax(1, size.mY);
            }
        }
    }

    // Build the persistent vertex buffer holding the known test rect. Rect is
    // in pixel coords with the viewer's top-left origin, solid red.
    bool createTestRectBuffer()
    {
        const float x0 = 200.f, y0 = 150.f, x1 = 520.f, y1 = 390.f;
        const float r = 1.f, g = 0.f, b = 0.f, a = 1.f;
        struct Vtx { float x, y, u, v, cr, cg, cb, ca; };
        Vtx verts[6] = {
            { x0, y0, 0, 0, r, g, b, a },
            { x1, y0, 1, 0, r, g, b, a },
            { x1, y1, 1, 1, r, g, b, a },
            { x0, y0, 0, 0, r, g, b, a },
            { x1, y1, 1, 1, r, g, b, a },
            { x0, y1, 0, 1, r, g, b, a },
        };

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = sizeof(verts);
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vinfo{};
        if (vmaCreateBuffer(s_context->allocator(), &bi, &ai, &s_test_vbuf, &s_test_vbuf_alloc, &vinfo) != VK_SUCCESS)
        {
            LL_WARNS("Vulkan") << "Test rect: vertex buffer alloc failed" << LL_ENDL;
            return false;
        }
        memcpy(vinfo.pMappedData, verts, sizeof(verts));
        return true;
    }

    void destroyTestRectBuffer()
    {
        if (s_test_vbuf != VK_NULL_HANDLE && s_context)
        {
            vmaDestroyBuffer(s_context->allocator(), s_test_vbuf, s_test_vbuf_alloc);
        }
        s_test_vbuf = VK_NULL_HANDLE;
        s_test_vbuf_alloc = VK_NULL_HANDLE;
    }

    // Draw the known test rect through the 2D pipeline.
    void drawTestRect(VkCommandBuffer cmd)
    {
        if (s_test_vbuf == VK_NULL_HANDLE) return;
        LLVKContext* ctx = s_context;
        const float W = (float)ctx->swapchainExtent().width;
        const float H = (float)ctx->swapchainExtent().height;

        // Ortho projection: pixel coords (top-left origin, y down) -> NDC.
        // Column-major mat4, matching GLSL's default layout.
        float ortho[16] = {
            2.f / W, 0.f,      0.f, 0.f,
            0.f,    -2.f / H,  0.f, 0.f,
            0.f,     0.f,     -1.f, 0.f,
           -1.f,     1.f,      0.f, 1.f
        };
        vkCmdPushConstants(cmd, ctx->pipelineLayout2D(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ortho), ortho);

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &s_test_vbuf, &off);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
}

bool LLVKSession::start(LLWindow* window, bool enable_validation)
{
    if (s_context)
    {
        LL_WARNS("Vulkan") << "Session: start() called while already running" << LL_ENDL;
        return true;
    }
    if (!window)
    {
        LL_WARNS("Vulkan") << "Session: no window to bind to" << LL_ENDL;
        return false;
    }

    void* native_window = window->getNativeHandle();
    void* native_instance = window->getNativeInstance();
    if (!native_window || !native_instance)
    {
        LL_WARNS("Vulkan") << "Session: window exposes no native handles" << LL_ENDL;
        return false;
    }

    std::string error;
    LLVKContext* ctx = new LLVKContext();

    if (!ctx->createInstance(enable_validation, error))
    {
        LL_WARNS("Vulkan") << "Session: createInstance failed: " << error << LL_ENDL;
        delete ctx;
        return false;
    }

    // The window was created with no GL context (LLWindow::setSkipGLContext),
    // so it is free to be owned by Vulkan.
    s_surface = ctx->createSurface(native_window, native_instance);
    if (s_surface == VK_NULL_HANDLE)
    {
        LL_WARNS("Vulkan") << "Session: surface creation failed" << LL_ENDL;
        delete ctx;
        return false;
    }

    if (!ctx->pickPhysicalDevice(s_surface, error) ||
        !ctx->createDevice(s_surface, error))
    {
        LL_WARNS("Vulkan") << "Session: device setup failed: " << error << LL_ENDL;
        vkDestroySurfaceKHR(ctx->instance(), s_surface, nullptr);
        s_surface = VK_NULL_HANDLE;
        delete ctx;
        return false;
    }

    queryClientSize(window, s_width, s_height);
    if (!ctx->createSwapchain(s_surface, s_width, s_height, error))
    {
        LL_WARNS("Vulkan") << "Session: swapchain creation failed: " << error << LL_ENDL;
        // createSwapchain() adopts the surface on entry; the context destroys
        // it on teardown.
        delete ctx;
        s_surface = VK_NULL_HANDLE;
        return false;
    }

    s_context = ctx;

    // Phase 3: stand up the 2D UI pipeline now that the swapchain format is known.
    if (!ctx->create2DPipeline(error))
    {
        LL_WARNS("Vulkan") << "Session: 2D pipeline creation failed: " << error
                           << " (falling back to clear-only frames)" << LL_ENDL;
    }
    else if (!createTestRectBuffer())
    {
        LL_WARNS("Vulkan") << "Session: test-rect buffer alloc failed (clear-only frames)" << LL_ENDL;
    }
    LL_INFOS("Vulkan") << "Session: Vulkan owns the viewer window ("
                       << s_width << "x" << s_height << ", device: "
                       << ctx->deviceName() << ")" << LL_ENDL;
    return true;
}

bool LLVKSession::isRunning()
{
    return s_context != nullptr;
}

bool LLVKSession::captureRGBA(std::vector<uint8_t>& out_rgba, uint32_t& out_w, uint32_t& out_h)
{
    if (!s_context)
    {
        return false;
    }
    // Present one full frame first so the captured image holds the current
    // frame's content (not a stale or not-yet-drawn swapchain image), then read.
    renderFrame();
    return s_context->readbackSwapchain(out_rgba, out_w, out_h);
}

void LLVKSession::renderFrame()
{
    if (!s_context)
    {
        return;
    }

    // Phase 3: drive a 2D frame when the pipeline is up. Clears to the
    // bring-up teal, then draws a known test rect through the real 2D pipeline
    // (vertex buffer + ortho push-constant matrix). Falls back to the plain
    // clear if the pipeline isn't available.
    if (s_context->pipeline2D() != VK_NULL_HANDLE)
    {
        VkCommandBuffer cmd = s_context->begin2DFrame(kClearR, kClearG, kClearB, kClearA);
        if (cmd != VK_NULL_HANDLE)
        {
            drawTestRect(cmd);
            s_context->end2DFrame();
        }
        return;
    }

    if (!s_context->renderClearFrame(kClearR, kClearG, kClearB, kClearA))
    {
        LL_WARNS("Vulkan") << "Session: renderClearFrame failed" << LL_ENDL;
    }
}

void LLVKSession::resizeIfNeeded(LLWindow* window)
{
    if (!s_context)
    {
        return;
    }

    uint32_t width, height;
    queryClientSize(window, width, height);
    if (width == 0 || height == 0 || (width == s_width && height == s_height))
    {
        return;
    }

    std::string error;
    if (!s_context->createSwapchain(s_surface, width, height, error))
    {
        LL_WARNS("Vulkan") << "Session: swapchain recreate failed ("
                           << width << "x" << height << "): " << error << LL_ENDL;
        return;
    }
    s_width = width;
    s_height = height;
    LL_INFOS("Vulkan") << "Session: swapchain resized to " << s_width << "x" << s_height << LL_ENDL;
}

void LLVKSession::stop()
{
    if (!s_context)
    {
        return;
    }
    // Free session-owned VMA allocations (the test-rect vertex buffer) BEFORE
    // the context tears down the allocator, so vkDestroyDevice sees no leaks.
    destroyTestRectBuffer();
    // destroy() idles the device and releases the swapchain + surface.
    s_context->destroy();
    delete s_context;
    s_context = nullptr;
    s_surface = VK_NULL_HANDLE;
    s_width = s_height = 0;
    LL_INFOS("Vulkan") << "Session: stopped" << LL_ENDL;
}

std::string LLVKSession::deviceName()
{
    return s_context ? s_context->deviceName() : std::string();
}

#else // !LL_WINDOWS

bool LLVKSession::start(LLWindow* window, bool enable_validation)
{
    LL_WARNS("Vulkan") << "Session: Vulkan boot is only implemented on Windows" << LL_ENDL;
    return false;
}

bool LLVKSession::isRunning() { return false; }
void LLVKSession::renderFrame() {}
void LLVKSession::resizeIfNeeded(LLWindow* window) {}
void LLVKSession::stop() {}
bool LLVKSession::captureRGBA(std::vector<uint8_t>& out_rgba, uint32_t& out_w, uint32_t& out_h) { return false; }
std::string LLVKSession::deviceName() { return std::string(); }

#endif // LL_WINDOWS

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

#include <cstdlib>
#include <cstring>
#include <vector>

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

    // --- Primitive battery (Phase 3a-2) -----------------------------------
    // One interleaved quad vertex: pixel pos (top-left origin), uv, RGBA.
    struct QuadVert { float x, y, u, v, r, g, b, a; };

    // Scene selection for the GL<->Vulkan diff harness (VULKANSTORM_SCENE=n).
    int sceneIndex()
    {
        const char* s = getenv("VULKANSTORM_SCENE");
        return s ? atoi(s) : 0;
    }

    void pushRect(std::vector<QuadVert>& out, float x0, float y0, float x1, float y1,
                  float r, float g, float b, float a)
    {
        out.push_back({ x0, y0, 0, 0, r, g, b, a });
        out.push_back({ x1, y0, 1, 0, r, g, b, a });
        out.push_back({ x1, y1, 1, 1, r, g, b, a });
        out.push_back({ x0, y0, 0, 0, r, g, b, a });
        out.push_back({ x1, y1, 1, 1, r, g, b, a });
        out.push_back({ x0, y1, 0, 1, r, g, b, a });
    }

    // Build the vertex list for the selected scene. Must match the GL
    // reference scene in llviewerdisplay.cpp exactly.
    std::vector<QuadVert> buildScene(int scene)
    {
        std::vector<QuadVert> v;
        switch (scene)
        {
        case 1: // several opaque rects, distinct colors (batching + ordering)
            pushRect(v, 200, 150, 520, 390, 1, 0, 0, 1);   // red
            pushRect(v, 600, 150, 920, 390, 0, 1, 0, 1);   // green
            pushRect(v, 1000, 150, 1320, 390, 0, 0, 1, 1); // blue
            pushRect(v, 400, 500, 720, 740, 1, 1, 0, 1);   // yellow
            break;
        case 2: // overlapping alpha-blended rects (blending math)
            pushRect(v, 300, 250, 900, 750, 1, 0, 0, 0.5f);   // red 50%
            pushRect(v, 500, 350, 1100, 850, 0, 0, 1, 0.5f);  // blue 50% over red
            pushRect(v, 400, 550, 1000, 950, 0, 1, 0, 0.25f); // green 25% over both
            break;
        case 3: // one textured quad (checkerboard) — texture upload + UV path
            pushRect(v, 400, 300, 912, 556, 1, 1, 1, 1); // white tint, uv 0..1
            break;
        case 0: // single solid red rect (regression baseline)
        default:
            pushRect(v, 200, 150, 520, 390, 1, 0, 0, 1);
            break;
        }
        return v;
    }

    // Persistent vertex buffer holding the current scene's quads, plus its
    // vertex count for the draw call.
    VkBuffer      s_test_vbuf = VK_NULL_HANDLE;
    VmaAllocation s_test_vbuf_alloc = VK_NULL_HANDLE;
    uint32_t      s_test_vert_count = 0;

    // The scene's texture (scene 3: a checkerboard). Null for solid scenes.
    LLVKContext::Texture2D s_scene_tex;

    // 16x16 black/white checkerboard, 1 texel per cell — catches UV, flip, and
    // filtering bugs precisely.
    std::vector<uint8_t> makeCheckerboard()
    {
        const int dim = 16;
        std::vector<uint8_t> px(dim * dim * 4);
        for (int y = 0; y < dim; ++y)
            for (int x = 0; x < dim; ++x)
            {
                bool on = ((x + y) & 1) != 0;
                uint8_t v = on ? 255 : 0;
                size_t o = (size_t)(y * dim + x) * 4;
                px[o] = v; px[o + 1] = v; px[o + 2] = v; px[o + 3] = 255;
            }
        return px;
    }

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

    // Build the persistent vertex buffer for the selected scene's quads.
    bool createTestRectBuffer()
    {
        std::vector<QuadVert> verts = buildScene(sceneIndex());
        if (verts.empty())
        {
            LL_WARNS("Vulkan") << "Test scene: no vertices" << LL_ENDL;
            return false;
        }
        s_test_vert_count = (uint32_t)verts.size();

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = verts.size() * sizeof(QuadVert);
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vinfo{};
        if (vmaCreateBuffer(s_context->allocator(), &bi, &ai, &s_test_vbuf, &s_test_vbuf_alloc, &vinfo) != VK_SUCCESS)
        {
            LL_WARNS("Vulkan") << "Test scene: vertex buffer alloc failed" << LL_ENDL;
            return false;
        }
        memcpy(vinfo.pMappedData, verts.data(), bi.size);
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

    // Draw the current scene's quads through the 2D pipeline.
    void drawTestRect(VkCommandBuffer cmd)
    {
        if (s_test_vbuf == VK_NULL_HANDLE || s_test_vert_count == 0) return;
        LLVKContext* ctx = s_context;
        const float W = (float)ctx->swapchainExtent().width;
        const float H = (float)ctx->swapchainExtent().height;

        // Ortho projection: pixel coords (top-left origin, y down) -> NDC.
        // The negative-height viewport already flips NDC y, so this matches the
        // viewer's gl_rect_2d / GL ortho output exactly. Column-major mat4.
        float ortho[16] = {
            2.f / W, 0.f,      0.f, 0.f,
            0.f,     2.f / H,  0.f, 0.f,
            0.f,     0.f,     -1.f, 0.f,
           -1.f,    -1.f,      0.f, 1.f
        };
        vkCmdPushConstants(cmd, ctx->pipelineLayout2D(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ortho), ortho);

        // The fragment shader always samples set 0 / binding 0. Solid quads bind
        // the 1x1 white texture; the textured scene binds the checkerboard.
        ctx->bindTexture2D(cmd, (s_scene_tex.descriptor != VK_NULL_HANDLE) ? s_scene_tex.descriptor
                                                                           : ctx->whiteTextureDescriptor());

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &s_test_vbuf, &off);
        vkCmdDraw(cmd, s_test_vert_count, 1, 0, 0);
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
    else
    {
        // Scene 3 needs the checkerboard texture uploaded before the first frame.
        if (sceneIndex() == 3)
        {
            std::vector<uint8_t> cb = makeCheckerboard();
            if (!ctx->createTexture2D(cb.data(), 16, 16, s_scene_tex, error))
            {
                LL_WARNS("Vulkan") << "Session: checkerboard texture upload failed: " << error << LL_ENDL;
            }
        }
        if (!createTestRectBuffer())
        {
            LL_WARNS("Vulkan") << "Session: test-rect buffer alloc failed (clear-only frames)" << LL_ENDL;
        }
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
    // Free session-owned GPU resources (scene texture + test vertex buffer)
    // BEFORE the context tears down the allocator, so vkDestroyDevice is clean.
    if (s_scene_tex.image != VK_NULL_HANDLE)
    {
        s_context->destroyTexture2D(s_scene_tex);
    }
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

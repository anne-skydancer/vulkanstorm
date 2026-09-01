/**
 * @file llviewerdisplay.cpp
 * @brief LLViewerDisplay class implementation
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

#include "llviewerdisplay.h"

#include "llvkprobe.h"
#include "llvkselftest.h"
#include "llvksession.h"
#include "llvkui2d.h"
#include "llvkuicache.h"
#include "llrender2dutils.h"
#include "llviewertexture.h"   // <VulkanStorm> LLViewerTexture::getRawImage (image funnel)
#include "llimage.h"           // <VulkanStorm> LLImageRaw (image funnel)
#include "llvector4a.h"        // <VulkanStorm> LLVector4a (vertex batch funnel)
#include "v2math.h"            // <VulkanStorm> LLVector2 (vertex batch funnel)
#include "fsyspath.h"
#include "hexdump.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llappviewer.h"
#include "llcoord.h"
#include "llcriticaldamp.h"
#include "llcubemap.h"
#include "lldir.h"
#include "lldrawpoolalpha.h"
#include "lldrawpoolbump.h"
#include "lldrawpoolwater.h"
#include "lldynamictexture.h"
#include "llenvironment.h"
#include "llfasttimer.h"
#include "llfeaturemanager.h"
#include "llfloatertools.h"
#include "llfocusmgr.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llgltfmateriallist.h"
#include "llhudmanager.h"
#include "llimagepng.h"
#include "llmachineid.h"
#include "llmemory.h"
#include "llparcel.h"
#include "llperfstats.h"
#include "llpostprocess.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llscenemonitor.h"
#include "llsdjson.h"
#include "llselectmgr.h"
#include "llsky.h"
#include "llspatialpartition.h"
#include "llstartup.h"
#include "llstartup.h"
#include "lltooldraganddrop.h"
#include "lltoolfocus.h"
#include "lltoolmgr.h"
#include "lltoolpie.h"
#include "lltracker.h"
#include "lltrans.h"
#include "llui.h"
#include "lluuid.h"
#include "llversioninfo.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewernetwork.h"
#include "llviewerobjectlist.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewertexturelist.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"
#include "llvograss.h"
#include "llworld.h"
#include "pipeline.h"

#include <boost/json.hpp>
// [RLVa:KB] - Checked: 2011-05-22 (RLVa-1.3.1a)
#include "llvisualeffect.h"
#include "rlvactions.h"
#include "rlvlocks.h"
// [/RLVa:KB]
#include "llpresetsmanager.h"
#include "fsdata.h"

// <FS:PP> Render chat range spheres in 3D world
#include "lfsimfeaturehandler.h"
#include "llrendersphere.h"
#include "lluicolortable.h"
// </FS:PP>

#include <filesystem>
#include <iomanip>
#include <sstream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

extern LLPointer<LLViewerTexture> gStartTexture;
extern bool gShiftFrame;

LLPointer<LLViewerTexture> gDisconnectedImagep = nullptr;

// used to toggle renderer back on after teleport
bool         gTeleportDisplay = false;
LLFrameTimer gTeleportDisplayTimer;
LLFrameTimer gTeleportArrivalTimer;
constexpr F32 RESTORE_GL_TIME = 5.f;  // Wait this long while reloading textures before we raise the curtain
// <FS:Ansariel> Draw Distance stepping; originally based on SpeedRez by Henri Beauchamp, licensed under LGPL
F32         gSavedDrawDistance = 0.0f;
F32         gLastDrawDistanceStep = 0.0f;
// <FS:Ansariel> FIRE-12004: Attachments getting lost on TP
LLFrameTimer gPostTeleportFinishKillObjectDelayTimer;

bool gForceRenderLandFence = false;
bool gDisplaySwapBuffers = false;
bool gDepthDirty = false;
bool gResizeScreenTexture = false;
bool gResizeShadowTexture = false;
bool gWindowResized = false;
bool gSnapshot = false;
bool gCubeSnapshot = false;
bool gSnapshotNoPost = false;
bool gShaderProfileFrame = false;

// This is how long the sim will try to teleport you before giving up.
constexpr F32 TELEPORT_EXPIRY = 15.0f;
// Additional time (in seconds) to wait per attachment
constexpr F32 TELEPORT_EXPIRY_PER_ATTACHMENT = 3.f;

U32 gRecentFrameCount = 0; // number of 'recent' frames
LLFrameTimer gRecentFPSTime;
LLFrameTimer gRecentMemoryTime;
LLFrameTimer gAssetStorageLogTime;

// Rendering stuff
void render_ui(F32 zoom_factor = 1.f, int subfield = 0);
void swap();
void render_hud_attachments();
void render_ui_3d();
void render_ui_2d();
void render_disconnected_background();

void getProfileStatsContext(boost::json::object& stats);
std::string getProfileStatsFilename();

#if LL_WINDOWS
// <VulkanStorm> Funnel-dispatch hook implementation (Phase 3b v2 / 3c M1) ----
// Routes the 2D funnel primitives (gl_rect_2d & friends) into the LLVKUI2D
// sink while the Vulkan backend owns the frame. Installed around the UI draw
// by vulkan_ui_frame(); cleared afterward.
//
// Transform contract: LLRender::vertex3f bakes (in + mUIOffset) * mUIScale at
// emit. The sink's rect() applies the same (left + offX) * scaleX, so the hook
// passes the CURRENT UI stacks RAW (getUITranslation/getUIScale). Do NOT fold
// in sUIGLScaleFactor here: the funnel call sites that need it (e.g.
// gl_rect_2d_offset_local) have already applied it to the coords they pass.
static void vk_push_ui_transform()
{
    const LLVector3 off = gGL.getUITranslation();
    const LLVector3 sc = gGL.getUIScale();
    LLVKUI2DSink::get().setTransform(off.mV[VX], off.mV[VY], sc.mV[VX], sc.mV[VY]);
}
static void vk_funnel_rect(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color)
{
    vk_push_ui_transform();
    LLVKUI2DSink::get().rect((F32)left, (F32)top, (F32)right, (F32)bottom,
              color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE], color.mV[VALPHA]);
}
static void vk_funnel_line_rect(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& color)
{
    vk_push_ui_transform();
    // Match gl_rect_2d's outline winding: inset top/right by one pixel, closed strip.
    const F32 l = (F32)left, t = (F32)(top - 1), r = (F32)(right - 1), b = (F32)bottom;
    const float xy[10] = { l, t,  l, b,  r, b,  r, t,  l, t };
    LLVKUI2DSink::get().lineStrip(xy, 5,
              color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE], color.mV[VALPHA]);
}
static void vk_funnel_line2d(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4& color)
{
    vk_push_ui_transform();
    const float xy[4] = { (F32)x1, (F32)y1, (F32)x2, (F32)y2 };
    LLVKUI2DSink::get().lineStrip(xy, 2,
              color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE], color.mV[VALPHA]);
}
static void vk_funnel_set_color(const LLColor4& color)
{
    // Color is tracked in LLRender::sVulkanUICurrentColor (via color4ub); the
    // funnels read it directly. Nothing to push to the sink here.
    (void)color;
}
static void vk_funnel_set_blend(int blend_type)
{
    // Map LLRender::eBlendType (int) onto the sink's LLVKBlend subset.
    switch ((LLRender::eBlendType)blend_type)
    {
    case LLRender::BT_ALPHA:          LLVKUI2DSink::get().setBlend(LLVKBlend::Alpha); break;
    case LLRender::BT_REPLACE:        LLVKUI2DSink::get().setBlend(LLVKBlend::Replace); break;
    case LLRender::BT_ADD_WITH_ALPHA: LLVKUI2DSink::get().setBlend(LLVKBlend::AddWithAlpha); break;
    case LLRender::BT_ADD:            LLVKUI2DSink::get().setBlend(LLVKBlend::Add); break;
    default:
        LL_WARNS("Vulkan") << "UI funnel: unsupported blend type " << blend_type << " (using Alpha)" << LL_ENDL;
        LLVKUI2DSink::get().setBlend(LLVKBlend::Alpha);
        break;
    }
}
static void vk_funnel_set_scissor(S32 x, S32 y, S32 w, S32 h)
{
    // Incoming rect is in GL bottom-left-origin device pixels (as the clip
    // rects compute it). Convert to the sink's top-left-origin device pixels.
    if (w <= 0 || h <= 0)
    {
        LLVKUI2DSink::get().clearScissor();
        return;
    }
    LLCoordWindow ws;
    if (!gViewerWindow || !gViewerWindow->getWindow() || !gViewerWindow->getWindow()->getSize(&ws) || ws.mY <= 0)
    {
        LLVKUI2DSink::get().clearScissor();
        return;
    }
    const S32 device_h = ws.mY;
    const S32 vk_y = device_h - (y + h);
    LLVKUI2DSink::get().setScissor(x, vk_y, w, h);
}

// Drop-shadow gradient: reproduce gl_drop_shadow's geometry with per-vertex
// alpha. Same four quads (right edge, bottom edge, two corners), start_color at
// the panel edge fading to alpha 0 at the outer edge.
static void vk_funnel_drop_shadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4& start_color, S32 lines)
{
    vk_push_ui_transform();
    right--;
    bottom++;
    lines++;

    const float sr = start_color.mV[VRED], sg = start_color.mV[VGREEN],
                sb = start_color.mV[VBLUE], sa = start_color.mV[VALPHA];
    const float sc[4] = { sr, sg, sb, sa };   // start (opaque) color
    const float ec[4] = { sr, sg, sb, 0.f };  // end (transparent) color

    // Interleaved per-vertex: xy pairs + rgba quads, matching gl_drop_shadow order.
    float xy[64];
    float rgba[128];
    int nv = 0;
    auto tri = [&](float x0, float y0, const float* c0,
                   float x1, float y1, const float* c1,
                   float x2, float y2, const float* c2)
    {
        const float xs[3] = { x0, x1, x2 };
        const float ys[3] = { y0, y1, y2 };
        const float* cs[3] = { c0, c1, c2 };
        for (int k = 0; k < 3; ++k)
        {
            xy[nv * 2] = xs[k]; xy[nv * 2 + 1] = ys[k];
            rgba[nv * 4] = cs[k][0]; rgba[nv * 4 + 1] = cs[k][1];
            rgba[nv * 4 + 2] = cs[k][2]; rgba[nv * 4 + 3] = cs[k][3];
            ++nv;
        }
    };

    const float L = (F32)left, T = (F32)top, R = (F32)right, B = (F32)bottom, n = (F32)lines;
    // Right edge
    tri(R, T - n, sc,  R, B, sc,  R + n, B, ec);
    tri(R, T - n, sc,  R + n, B, ec,  R + n, T - n, ec);
    // Bottom edge
    tri(R, B, sc,  L + n, B, sc,  L + n, B - n, ec);
    tri(R, B, sc,  L + n, B - n, ec,  R, B - n, ec);
    // Bottom-left corner
    tri(L + n, B, sc,  L, B, ec,  L + 1, B - n + 1, ec);
    tri(L + n, B, sc,  L + 1, B - n + 1, ec,  L + n, B - n, ec);
    // Bottom-right corner
    tri(R, B, sc,  R, B - n, ec,  R + n - 1, B - n + 1, ec);
    tri(R, B, sc,  R + n - 1, B - n + 1, ec,  R + n, B - n, ec);

    LLVKUI2DSink::get().rawTris(xy, rgba, nv);
}

// <VulkanStorm> M2 image funnel ------------------------------------------------
// Place a UI raw image's content into a buffer sized to the texture's FULL
// (power-of-2 padded) dims — the texel space the GL texture uses and the frame
// the UV clip region was normalized against. Content goes to bottom-left
// (LLImageRaw row 0 = bottom = v0), padding zero-filled top/right, exactly
// replicating LLImageRaw::scale(..., false) from preCreateTexture.
static bool vk_blit_ui_texture(const LLImageRaw* raw, uint32_t full_w, uint32_t full_h,
                               std::vector<uint8_t>& out)
{
    if (!raw || full_w == 0 || full_h == 0) return false;
    const U8* data = raw->getData();
    const uint32_t raw_w = raw->getWidth();
    const uint32_t raw_h = raw->getHeight();
    const S32 comp = raw->getComponents();
    if (!data || raw_w == 0 || raw_h == 0 || comp < 1 || comp > 4) return false;
    if (raw_w > full_w || raw_h > full_h) return false;

    out.assign((size_t)full_w * full_h * 4, 0); // zero-fill the padding
    for (uint32_t y = 0; y < raw_h; ++y)         // content rows (bottom-up)
    {
        for (uint32_t x = 0; x < raw_w; ++x)
        {
            const size_t s = ((size_t)y * raw_w + x) * comp;
            const size_t d = ((size_t)y * full_w + x) * 4;
            uint8_t r, g, b, a;
            switch (comp)
            {
            case 1: r = g = b = data[s]; a = 255; break;
            case 2: r = g = b = data[s + 0]; a = data[s + 1]; break;
            case 3: r = data[s + 0]; g = data[s + 1]; b = data[s + 2]; a = 255; break;
            default: r = data[s + 0]; g = data[s + 1]; b = data[s + 2]; a = data[s + 3]; break;
            }
            out[d + 0] = r; out[d + 1] = g; out[d + 2] = b; out[d + 3] = a;
        }
    }
    return true;
}

static VkDescriptorSet vk_resolve_ui_texture(LLTexture* image)
{
    if (!image) return VK_NULL_HANDLE;
    // UI images are LLViewerFetchedTexture (pinned, decoded); getRawImage lives
    // on that subclass, not the LLViewerTexture base.
    LLViewerFetchedTexture* vt = dynamic_cast<LLViewerFetchedTexture*>(image);
    if (!vt) return VK_NULL_HANDLE;
    // Cached? Resolve without re-reading pixels.
    if (LLVKUITexture::get().contains(image))
    {
        return LLVKUITexture::get().resolve(LLVKSession::getContext(), image, nullptr, 0, 0);
    }
    const LLImageRaw* raw = vt->getRawImage();
    if (!raw) return VK_NULL_HANDLE; // not decoded yet; retry next frame

    // The GL texture is the PADDED (power-of-2) image; the UV clip region is
    // normalized against those full dims. Upload at full dims with content at
    // bottom-left so the uv_rect samples identically to GL. Only cache once the
    // raw matches the full dims (a smaller raw is a pre-padding/partial frame —
    // retry rather than freeze a wrong-size texture).
    const uint32_t full_w = (uint32_t)vt->getFullWidth();
    const uint32_t full_h = (uint32_t)vt->getFullHeight();
    if (full_w == 0 || full_h == 0) return VK_NULL_HANDLE;
    if ((uint32_t)raw->getWidth() != full_w || (uint32_t)raw->getHeight() != full_h)
    {
        return VK_NULL_HANDLE; // raw not yet at the padded dims; retry next frame
    }

    std::vector<uint8_t> rgba;
    if (!vk_blit_ui_texture(raw, full_w, full_h, rgba)) return VK_NULL_HANDLE;
    return LLVKUITexture::get().resolve(LLVKSession::getContext(), image, rgba.data(), full_w, full_h);
}

// L2 texture bind: resolve the LLTexture via the cache and select it on the
// sink (white/solid when null). Returns true (handled).
static bool vk_funnel_bind_texture(LLTexture* image)
{
    VkDescriptorSet tex = (image != nullptr) ? vk_resolve_ui_texture(image) : VK_NULL_HANDLE;
    LLVKUI2DSink::get().setTexture(tex); // VK_NULL_HANDLE -> white (solid) texture
    return true;
}

// Pre-transformed textured batch (images + 9-slice converge here). The verts
// are LLVector4a (screen-space pos) + LLVector2 (uv); color = the tracked
// current UI color (set by the widget's color4fv just before). Emits to the
// sink with NO additional transform (positions are already screen-space).
static bool vk_funnel_vertex_batch_tex(const void* pos_p, const void* uv_p, int vert_count)
{
    if (!pos_p || !uv_p || vert_count < 3) return true;
    const LLVector4a* pos = (const LLVector4a*)pos_p;
    const LLVector2* uv = (const LLVector2*)uv_p;
    const LLColor4U& cc = LLRender::sVulkanUICurrentColor;
    const float cr = cc.mV[VRED] / 255.f, cg = cc.mV[VGREEN] / 255.f,
                cb = cc.mV[VBLUE] / 255.f, ca = cc.mV[VALPHA] / 255.f;

    std::vector<float> xy((size_t)vert_count * 2), uvv((size_t)vert_count * 2), rgba((size_t)vert_count * 4);
    for (int i = 0; i < vert_count; ++i)
    {
        xy[i*2+0] = pos[i][VX]; xy[i*2+1] = pos[i][VY];
        uvv[i*2+0] = uv[i].mV[VX]; uvv[i*2+1] = uv[i].mV[VY];
        rgba[i*4+0]=cr; rgba[i*4+1]=cg; rgba[i*4+2]=cb; rgba[i*4+3]=ca;
    }
    LLVKUI2DSink::get().texturedBatchPreTransformed(xy.data(), uvv.data(), rgba.data(), vert_count);
    return true;
}
// </VulkanStorm>

static LLUIFunnelHook s_vk_funnel_hook = {
    &vk_funnel_rect,
    &vk_funnel_set_color,
    &vk_funnel_set_blend,
    &vk_funnel_set_scissor,
    &vk_funnel_line_rect,
    &vk_funnel_line2d,
    &vk_funnel_drop_shadow,
    &vk_funnel_bind_texture,
    &vk_funnel_vertex_batch_tex
};

// Run one Vulkan UI frame: begin the 2D pass + sink, run the real widget tree
// (mRootView->draw) through the funnel hook, flush + present. Returns false
// when the frame was skipped. Text/images/media are force-gated (M1) via
// VULKANSTORM_UI_GATE until their funnels land (M2/M3/M4).
static bool vulkan_ui_frame()
{
    if (!LLVKSession::beginUIFrame())
    {
        return false;
    }
    // <VulkanStorm> M1 diagnostic: confirm the UI frame runs each frame.
    static int s_frame = 0;
    if (getenv("VULKANSTORM_UI_DEBUG") && (s_frame++ % 120) == 0)
    {
        LL_INFOS("Vulkan") << "vulkan_ui_frame running (frame " << s_frame << ")" << LL_ENDL;
    }
    // </VulkanStorm>
    g_ui_funnel_hook = &s_vk_funnel_hook;
    LLRender::setVulkanUIActive(true);
    if (gViewerWindow)
    {
        // Draws the live 2D widget tree; rect/line/scissor funnels route to the
        // sink, gated primitives no-op, and the raw-GL sections are gated off.
        gViewerWindow->draw();
    }
    LLRender::setVulkanUIActive(false);
    g_ui_funnel_hook = nullptr;
    // <VulkanStorm> M1 diagnostic: emit a known red rect DIRECTLY to the sink
    // (bypassing the widget tree) to isolate whether the sink renders at all in
    // the UI-frame path. Set VULKANSTORM_UI_DEBUG=direct.
    static bool s_direct = getenv("VULKANSTORM_UI_DEBUG") && std::string(getenv("VULKANSTORM_UI_DEBUG")) == "direct";
    if (s_direct)
    {
        LLVKUI2DSink::get().rect(200.f, 150.f, 520.f, 390.f, 1.f, 0.f, 0.f, 1.f);
    }
    // </VulkanStorm>
    LLVKSession::endUIFrame();
    return true;
}
// </VulkanStorm>

// <VulkanStorm> One-shot Vulkan frame capture for the GL<->Vulkan diff
// harness. After a settle delay, dump the presented frame to a raw RGBA8 file
// (8-byte header: width, height LE). Call once per frame while the session runs.
static void vulkan_capture_tick()
{
    // Bring-up toggle via env var (avoids settings-registration friction for a
    // one-shot diagnostic). Set VULKANSTORM_CAPTURE=1 to capture one frame.
    static const bool s_want_capture = (getenv("VULKANSTORM_CAPTURE") != nullptr);
    if (!s_want_capture) return;
    static S32 s_capture_countdown = -1;
    if (s_capture_countdown < 0) s_capture_countdown = 90; // ~1.5s settle
    if (--s_capture_countdown != 0) return;
    s_capture_countdown = S32_MAX; // one-shot: never fire again
    std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
    if (!LLVKSession::captureRGBA(rgba, w, h)) return;
    std::string path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "vulkan_capture.rgba");
    LLFILE* f = LLFile::fopen(path, "wb");
    if (!f) return;
    uint32_t hdr[2] = { w, h };
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(rgba.data(), 1, rgba.size(), f);
    LLFile::close(f);
    LL_INFOS("Vulkan") << "Captured Vulkan frame: " << w << "x" << h << " -> " << path << LL_ENDL;
}
#endif // LL_WINDOWS (funnel hook + vulkan_capture_tick block)

// GL reference capture for the diff harness: render the SAME known primitive
// (teal clear + a solid red rect at (200,150)-(520,390)) into an OFFSCREEN
// framebuffer (never the visible window), read it back with glReadPixels, and
// dump it in the same format as the Vulkan capture for per-pixel diffing.
static void gl_reference_capture_tick()
{
#if LL_WINDOWS
    // Scene-screenshot mode only. When no VULKANSTORM_SCENE is set the
    // real-tree capture (gl_real_tree_capture_tick) owns gl_reference_capture.rgba.
    static const bool s_want = (getenv("VULKANSTORM_CAPTURE_GL") != nullptr) &&
                               (getenv("VULKANSTORM_SCENE") != nullptr);
    if (!s_want) return;
    static S32 s_countdown = -1;
    if (s_countdown < 0) s_countdown = 90; // ~1.5s settle (GL context + window sized)
    if (--s_countdown != 0) return;
    s_countdown = S32_MAX; // one-shot

    LLCoordWindow ws;
    gViewerWindow->getWindow()->getSize(&ws);
    const S32 w = ws.mX, h = ws.mY;
    if (w <= 0 || h <= 0) return;

    // Offscreen render target so the visible UI is never touched.
    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &tex);
        glDeleteFramebuffers(1, &fbo);
        LL_WARNS("Vulkan") << "GL reference FBO incomplete" << LL_ENDL;
        return;
    }

    // Draw the known primitive through the viewer's OWN 2D path (gGL /
    // gl_rect_2d), which works on the core profile. Teal clear + the same red
    // rect at (200,150)-(520,390) in the top-left-origin UI space. Save the
    // default-framebuffer viewport + clear color first so the live UI frame is
    // byte-identical to a run without this hook.
    GLint saved_viewport[4];
    GLfloat saved_clear[4];
    glGetIntegerv(GL_VIEWPORT, saved_viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_clear);
    {
        // Scene selection matches the Vulkan side (llvksession.cpp buildScene).
        const char* s = getenv("VULKANSTORM_SCENE");
        const int scene = s ? atoi(s) : 0;

        LLGLSDefault gls_default;
        LLGLSUIDefault gls_ui;
        gViewerWindow->setup2DRender();           // sets the top-left ortho projection
        glViewport(0, 0, w, h);
        gGL.setColorMask(true, true);
        gGL.setSceneBlendType(LLRender::BT_REPLACE);
        gUIProgram.bind();                         // gl_rect_2d needs a bound UI shader
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        glClearColor(0.f, 0.5f, 0.5f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        switch (scene)
        {
        case 1: // several opaque rects, distinct colors
            gl_rect_2d(200, 150, 520, 390, LLColor4(1, 0, 0, 1));
            gl_rect_2d(600, 150, 920, 390, LLColor4(0, 1, 0, 1));
            gl_rect_2d(1000, 150, 1320, 390, LLColor4(0, 0, 1, 1));
            gl_rect_2d(400, 500, 720, 740, LLColor4(1, 1, 0, 1));
            break;
        case 2: // overlapping alpha-blended rects over the teal clear
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            gl_rect_2d(300, 250, 900, 750, LLColor4(1, 0, 0, 0.5f));
            gl_rect_2d(500, 350, 1100, 850, LLColor4(0, 0, 1, 0.5f));
            gl_rect_2d(400, 550, 1000, 950, LLColor4(0, 1, 0, 0.25f));
            break;
        case 3: // one textured quad (16x16 checkerboard) — texture + UV path
        {
            // Build the same checkerboard as the Vulkan scene and upload it.
            const int dim = 16;
            std::vector<uint8_t> cb(dim * dim * 4);
            for (int yy = 0; yy < dim; ++yy)
                for (int xx = 0; xx < dim; ++xx)
                {
                    uint8_t v = ((xx + yy) & 1) ? 255 : 0;
                    size_t o = (size_t)(yy * dim + xx) * 4;
                    cb[o] = v; cb[o + 1] = v; cb[o + 2] = v; cb[o + 3] = 255;
                }
            GLuint ctex = 0;
            glGenTextures(1, &ctex);
            gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, ctex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dim, dim, 0, GL_RGBA, GL_UNSIGNED_BYTE, cb.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            // gl_rect_2d_simple_tex draws a textured quad from the origin to
            // (w,h) in the current projection; translate to the scene rect
            // (400,300)-(912,556), size 512x256, full uv range. White tint so
            // the texture shows through unmodified.
            gGL.color4f(1.f, 1.f, 1.f, 1.f);
            gGL.pushMatrix();
            gGL.translatef(400.f, 300.f, 0.f);
            gl_rect_2d_simple_tex(512, 256);
            gGL.popMatrix();
            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            glDeleteTextures(1, &ctex);
            break;
        }
        case 4: // alpha-gradient textured quad (the text mechanism), blended
        {
            // Same vertical alpha ramp as the Vulkan scene: white RGB, alpha
            // 0 (top) -> 255 (bottom), 16x16, one texel per row.
            const int dim = 16;
            std::vector<uint8_t> ramp(dim * dim * 4);
            for (int yy = 0; yy < dim; ++yy)
                for (int xx = 0; xx < dim; ++xx)
                {
                    uint8_t a = (uint8_t)((yy * 255) / (dim - 1));
                    size_t o = (size_t)(yy * dim + xx) * 4;
                    ramp[o] = 255; ramp[o + 1] = 255; ramp[o + 2] = 255; ramp[o + 3] = a;
                }
            GLuint rtex = 0;
            glGenTextures(1, &rtex);
            gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, rtex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dim, dim, 0, GL_RGBA, GL_UNSIGNED_BYTE, ramp.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            // Alpha-blend over the teal clear; red tint * white ramp -> red text-like.
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
            gGL.color4f(1.f, 0.f, 0.f, 1.f);
            gGL.pushMatrix();
            gGL.translatef(400.f, 300.f, 0.f);
            gl_rect_2d_simple_tex(512, 256);
            gGL.popMatrix();
            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            glDeleteTextures(1, &rtex);
            break;
        }
        case 0: // single solid red rect (regression baseline)
        default:
            gl_rect_2d(200, 150, 520, 390, LLColor4(1, 0, 0, 1));
            break;
        }
        gGL.flush();
        gUIProgram.unbind();
    }

    std::vector<uint8_t> rgba((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    // Restore the default framebuffer + all state we touched so the live UI is
    // unaffected (no teal band, no viewport/projection drift).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_TEXTURE_2D);
    glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
    glClearColor(saved_clear[0], saved_clear[1], saved_clear[2], saved_clear[3]);
    glDeleteTextures(1, &tex);
    glDeleteFramebuffers(1, &fbo);

    // glReadPixels is bottom-up; flip to top-down to match the Vulkan capture.
    std::vector<uint8_t> flipped((size_t)w * h * 4);
    const size_t row = (size_t)w * 4;
    for (S32 y = 0; y < h; ++y)
    {
        memcpy(flipped.data() + (size_t)(h - 1 - y) * row, rgba.data() + (size_t)y * row, row);
    }

    std::string path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "gl_reference_capture.rgba");
    LLFILE* f = LLFile::fopen(path, "wb");
    if (!f) return;
    uint32_t hdr[2] = { (uint32_t)w, (uint32_t)h };
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(flipped.data(), 1, flipped.size(), f);
    LLFile::close(f);
    LL_INFOS("Vulkan") << "Captured GL reference frame: " << w << "x" << h << " -> " << path << LL_ENDL;
#endif
}
// </VulkanStorm>

#if LL_WINDOWS
// <VulkanStorm> Real-tree GL reference capture (Phase 3c/M1 diff) ------------
// When VULKANSTORM_CAPTURE_GL is set AND no VULKANSTORM_SCENE is selected, the
// GL run captures the ACTUAL gated UI tree (same VULKANSTORM_UI_GATE as the
// Vulkan run) by reading the back buffer right after the login draw(). The
// readback is bottom-up; it is written BOTTOM-ORIGIN to match the Vulkan
// capture's convention, so the two files diff directly. One-shot after settle.
static void gl_real_tree_capture_tick()
{
    static const bool s_want = (getenv("VULKANSTORM_CAPTURE_GL") != nullptr) &&
                               (getenv("VULKANSTORM_SCENE") == nullptr);
    if (!s_want) return;
    static S32 s_countdown = -1;
    if (s_countdown < 0) s_countdown = 90; // ~1.5s settle, matches Vulkan capture
    if (--s_countdown != 0) return;
    s_countdown = S32_MAX; // one-shot

    LLCoordWindow ws;
    if (!gViewerWindow || !gViewerWindow->getWindow() || !gViewerWindow->getWindow()->getSize(&ws)) return;
    const S32 w = ws.mX, h = ws.mY;
    if (w <= 0 || h <= 0) return;

    std::vector<uint8_t> rgba((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // back buffer
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data()); // bottom-origin

    // glReadPixels is bottom-up; flip to top-down to match the Vulkan capture's
    // convention (the Vulkan swapchain readback writes top-down, as does the
    // scene-reference capture above), so the files diff directly.
    std::vector<uint8_t> flipped((size_t)w * h * 4);
    const size_t row = (size_t)w * 4;
    for (S32 y = 0; y < h; ++y)
    {
        memcpy(flipped.data() + (size_t)(h - 1 - y) * row, rgba.data() + (size_t)y * row, row);
    }

    std::string path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "gl_reference_capture.rgba");
    LLFILE* f = LLFile::fopen(path, "wb");
    if (!f) return;
    uint32_t hdr[2] = { (uint32_t)w, (uint32_t)h };
    fwrite(hdr, 1, sizeof(hdr), f);
    fwrite(flipped.data(), 1, flipped.size(), f); // top-down, matching the Vulkan capture
    LLFile::close(f);
    LL_INFOS("Vulkan") << "Captured GL real-tree reference frame: " << w << "x" << h << " -> " << path << LL_ENDL;
}
// </VulkanStorm>
#endif

void display_startup()
{
    if (   !gViewerWindow
        || !gViewerWindow->getActive()
        || !gViewerWindow->getWindow()->getVisible()
        || gViewerWindow->getWindow()->getMinimized()
        || gNonInteractive)
    {
        return;
    }

#if LL_WINDOWS
    // <VulkanStorm> The Vulkan backend owns the frame end-to-end while the
    // 2D/3D pipelines are being ported: clear + present, no GL calls.
    if (LLVKSession::isRunning())
    {
        LLVKSession::resizeIfNeeded(gViewerWindow->getWindow());
        if (getenv("VULKANSTORM_SCENE") != nullptr)
        {
            LLVKSession::renderFrame();
        }
        else
        {
            vulkan_ui_frame();
        }
        vulkan_capture_tick();
        return;
    }
    // </VulkanStorm>
#endif

#if LL_WINDOWS
    // <VulkanStorm> GL reference capture (diff harness): draws the same known
    // primitive via immediate GL and dumps it for comparison against Vulkan.
    gl_reference_capture_tick();
    // </VulkanStorm>
#endif

    gPipeline.updateGL();

    // Written as branch to appease GCC which doesn't like different
    // pointer types across ternary ops
    //
    if (!LLViewerFetchedTexture::sWhiteImagep.isNull())
    {
    LLTexUnit::sWhiteTexture = LLViewerFetchedTexture::sWhiteImagep->getTexName();
    }

    LLGLSDefault gls_default;

    // Required for HTML update in login screen
    static S32 frame_count = 0;

    LLGLState::checkStates();

    if (frame_count++ > 1) // make sure we have rendered a frame first
    {
        LLViewerDynamicTexture::updateAllInstances();
    }
    else
    {
        LL_DEBUGS("Window") << "First display_startup frame" << LL_ENDL;
    }

    LLGLState::checkStates();

    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT); // | GL_STENCIL_BUFFER_BIT);
    LLGLSUIDefault gls_ui;
    gPipeline.disableLights();

    if (gViewerWindow)
    gViewerWindow->setup2DRender();
    if (gViewerWindow)
    gViewerWindow->draw();
    gGL.flush();

#if LL_WINDOWS
    // <VulkanStorm> Phase 3c/M1: capture the real gated UI tree (back buffer)
    // for the byte-exact diff against the Vulkan login frame. No-op unless
    // VULKANSTORM_CAPTURE_GL is set with no VULKANSTORM_SCENE.
    gl_real_tree_capture_tick();
    // </VulkanStorm>
#endif

    LLVertexBuffer::unbind();

    LLGLState::checkStates();

#if LL_WINDOWS
    // <VulkanStorm> Phase-1 bring-up: isolated Vulkan self-test. Runs on the
    // login/startup frame path (which swaps here, not via swap()). When the
    // RenderVulkanSelfTest debug setting is on and a Vulkan device is present,
    // drive the Vulkan surface/context/swapchain and clear a test frame for a
    // few seconds, then return to the normal GL path. Diagnostic only.
    static LLCachedControl<bool> sVulkanSelfTest(gSavedSettings, "RenderVulkanSelfTest", false);
    static LLCachedControl<bool> sVulkanDebug(gSavedSettings, "RenderVulkanDebug", false);
    static bool sSelfTestStarted = false;
    if (sVulkanSelfTest && !LLVKSelfTest::finished())
    {
        if (!sSelfTestStarted)
        {
            sSelfTestStarted = true;
            if (LLVKProbe::hasVulkanDevice())
            {
                LLVKSelfTest::begin(sVulkanDebug);
            }
        }
        LLVKSelfTest::renderFrame();
    }
    // </VulkanStorm>
#endif

    if (gViewerWindow && gViewerWindow->getWindow())
    gViewerWindow->getWindow()->swapBuffers();

    glClear(GL_DEPTH_BUFFER_BIT);
}

void display_update_camera()
{
    LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Update Camera");
    // TODO: cut draw distance down if customizing avatar?
    // TODO: cut draw distance on per-parcel basis?

    // Cut draw distance in half when customizing avatar,
    // but on the viewer only.
    F32 final_far = gAgentCamera.mDrawDistance;
    // <FS:TJ> [FIRE-35748] Only enable the LL Draw Distance VRAM optimization when the setting is enabled
    static LLCachedControl<bool> use_vram_optimization(gSavedSettings, "FSDrawDistanceVRAMOptimization", false);
    // </FS:TJ>
    if (gCubeSnapshot)
    {
        static LLCachedControl<F32> reflection_probe_draw_distance(gSavedSettings, "RenderReflectionProbeDrawDistance", 64.f);
        final_far = reflection_probe_draw_distance();
    }
    else if (CAMERA_MODE_CUSTOMIZE_AVATAR == gAgentCamera.getCameraMode())
    {
        final_far *= 0.5f;
    }
    // When system memory is critically low or recovering, shrink draw distance.
    // <FS:TJ> [FIRE-35748] Only enable the LL Draw Distance VRAM optimization when the setting is enabled
    //else if (const F32 mem_factor = LLMemory::getSystemMemoryBudgetFactor(); mem_factor > 1.f)
    else if (const F32 mem_factor = LLMemory::getSystemMemoryBudgetFactor(); use_vram_optimization && mem_factor > 1.f)
    {
        final_far = llmax(32.f, final_far / mem_factor);
    }
// <FS:CR> Aurora sim
    if (LLWorld::getInstance()->getLockedDrawDistance())
    {
        //Reset the draw distance and do not update with the new val
        final_far = LLViewerCamera::getInstance()->getFar();
    }
// </FS:CR> Aurora sim
    LLViewerCamera::getInstance()->setFar(final_far);
    LLVOAvatar::sRenderDistance = llclamp(final_far, 16.f, 256.f);
    gViewerWindow->setup3DRender();

    if (!gCubeSnapshot)
    {
        // Update land visibility too
        LLWorld::getInstance()->setLandFarClip(final_far);
    }
}

// Write some stats to LL_INFOS()
void display_stats()
{
    LL_PROFILE_ZONE_SCOPED;
    constexpr F32 FPS_LOG_FREQUENCY = 10.f;
    if (gRecentFPSTime.getElapsedTimeF32() >= FPS_LOG_FREQUENCY)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("DS - FPS");
        LLTrace::Recording& recording = LLTrace::get_frame_recording().getLastRecording();
        F64 normalized_session_jitter = recording.getLastValue(LLStatViewer::NOTRMALIZED_FRAMETIME_JITTER_SESSION);
        F64 normalized_period_jitter = recording.getLastValue(LLStatViewer::NORMALIZED_FRAMTIME_JITTER_PERIOD);
        F32 fps = gRecentFrameCount / FPS_LOG_FREQUENCY;
        LL_INFOS() << llformat("FPS: %.02f SESSION JITTER: %.4f PERIOD JITTER: %.4f", fps, normalized_session_jitter, normalized_period_jitter) << LL_ENDL;
        gRecentFrameCount = 0;
        gRecentFPSTime.reset();
    }
    static LLCachedControl<F32> mem_log_freq(gSavedSettings, "MemoryLogFrequency", 600.f);
    if (mem_log_freq > 0.f && gRecentMemoryTime.getElapsedTimeF32() >= mem_log_freq)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("DS - Memory");
        gMemoryAllocated = U64Bytes(LLMemory::getCurrentRSS());
        U32Megabytes memory = gMemoryAllocated;
        LL_INFOS() << "MEMORY: " << memory << LL_ENDL;
        LLMemory::logMemoryInfo(true) ;
        gRecentMemoryTime.reset();
    }
    constexpr F32 ASSET_STORAGE_LOG_FREQUENCY = 60.f;
    if (gAssetStorageLogTime.getElapsedTimeF32() >= ASSET_STORAGE_LOG_FREQUENCY)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("DS - Asset Storage");
        gAssetStorageLogTime.reset();
        gAssetStorage->logAssetStorageInfo();
    }
}

static void update_tp_display(bool minimized)
{
    static LLCachedControl<F32> teleport_arrival_delay(gSavedSettings, "TeleportArrivalDelay");
    static LLCachedControl<F32> teleport_local_delay(gSavedSettings, "TeleportLocalDelay");

    // <FS:PP> Speed optimisation
    static LLCachedControl<bool> disable_teleport_screens(gSavedSettings, "FSDisableTeleportScreens");
    static LLCachedControl<bool> reset_camera_on_tp(gSavedSettings, "FSResetCameraOnTP");
    static LLCachedControl<LLVector3> nacl_ml_fov_values(gSavedSettings, "_NACL_MLFovValues");
    // </FS:PP>

    S32 attach_count = 0;
    if (isAgentAvatarValid())
    {
        attach_count = gAgentAvatarp->getAttachmentCount();
    }
    F32 teleport_save_time = TELEPORT_EXPIRY + TELEPORT_EXPIRY_PER_ATTACHMENT * attach_count;
    F32 teleport_elapsed = gTeleportDisplayTimer.getElapsedTimeF32();
    F32 teleport_percent = teleport_elapsed * (100.f / teleport_save_time);
    if (gAgent.getTeleportState() != LLAgent::TELEPORT_START && teleport_percent > 100.f)
    {
        // Give up.  Don't keep the UI locked forever.
        LL_WARNS("Teleport") << "Giving up on teleport. elapsed time " << teleport_elapsed << " exceeds max time " << teleport_save_time << LL_ENDL;
        gAgent.setTeleportState(LLAgent::TELEPORT_NONE);
        gAgent.setTeleportMessage(std::string());
    }

    // Make sure the TP progress panel gets hidden in case the viewer window
    // is minimized *during* a TP. HB
    if (minimized)
    {
        gViewerWindow->setShowProgress(false, false);
    }

    const std::string& message = gAgent.getTeleportMessage();
    switch (gAgent.getTeleportState())
    {
        case LLAgent::TELEPORT_PENDING:
        {
            gTeleportDisplayTimer.reset();
            const std::string& msg = LLAgent::sTeleportProgressMessages["pending"];
            if (!minimized)
            {
                // <FS:PP> Speed optimisation
                // gViewerWindow->setShowProgress(true, !gSavedSettings.getBOOL("FSDisableTeleportScreens"));
                gViewerWindow->setShowProgress(true, !disable_teleport_screens());
                // </FS:PP>
                gViewerWindow->setProgressPercent(llmin(teleport_percent, 0.0f));
                gViewerWindow->setProgressString(msg);
            }
            gAgent.setTeleportMessage(msg);
            break;
        }

        case LLAgent::TELEPORT_START:
        {
            // Transition to REQUESTED.  Viewer has sent some kind
            // of TeleportRequest to the source simulator

            // Reset view angle if in mouselook. Fixes camera angle getting stuck on teleport. -Zi
            if (gAgentCamera.cameraMouselook())
            {
                // If someone knows how to call "View.ZoomDefault" by hand, we should do that instead of
                // replicating the behavior here. -Zi
                LLViewerCamera::instance().setDefaultFOV(DEFAULT_FIELD_OF_VIEW);
                // <FS:PP> Speed optimisation
                // if (gSavedSettings.getBOOL("FSResetCameraOnTP"))
                if (reset_camera_on_tp())
                // </FS:PP>
                {
                    gSavedSettings.setF32("CameraAngle", LLViewerCamera::instance().getView()); // FS:LO Dont reset rightclick zoom when we teleport however. Fixes FIRE-6246.
                }
                // also, reset the marker for "currently zooming" in the mouselook zoom settings. -Zi
                // <FS:PP> Speed optimisation
                // LLVector3 vTemp = gSavedSettings.getVector3("_NACL_MLFovValues");
                LLVector3 vTemp = nacl_ml_fov_values();
                // </FS:PP>
                vTemp.mV[VZ] = 0.0f;
                gSavedSettings.setVector3("_NACL_MLFovValues", vTemp);
            }

            gTeleportDisplayTimer.reset();
            const std::string& msg = LLAgent::sTeleportProgressMessages["requesting"];
            LL_INFOS("Teleport") << "A teleport request has been sent, setting state to TELEPORT_REQUESTED" << LL_ENDL;
            gAgent.setTeleportState(LLAgent::TELEPORT_REQUESTED);
            gAgent.setTeleportMessage(msg);
            FSData::instance().selectNextMOTD();
            if (!minimized)
            {
                // <FS:PP> Speed optimisation
                // gViewerWindow->setShowProgress(true, !gSavedSettings.getBOOL("FSDisableTeleportScreens"));
                gViewerWindow->setShowProgress(true, !disable_teleport_screens());
                // </FS:PP>
                gViewerWindow->setProgressPercent(llmin(teleport_percent, 0.0f));
                gViewerWindow->setProgressString(msg);
                gViewerWindow->setProgressMessage(gAgent.mMOTD);
            }
            break;
        }

        case LLAgent::TELEPORT_REQUESTED:
            // Waiting for source simulator to respond
            if (!minimized)
            {
                gViewerWindow->setProgressPercent(llmin(teleport_percent, 37.5f));
                gViewerWindow->setProgressString(message);
            }
            break;

        case LLAgent::TELEPORT_MOVING:
            // Viewer has received destination location from source simulator
            if (!minimized)
            {
                gViewerWindow->setProgressPercent(llmin(teleport_percent, 75.f));
                gViewerWindow->setProgressString(message);
            }
            break;

        case LLAgent::TELEPORT_START_ARRIVAL:
            // Transition to ARRIVING.  Viewer has received avatar update, etc.,
            // from destination simulator
            gTeleportArrivalTimer.reset();
            LL_INFOS("Teleport") << "Changing state to TELEPORT_ARRIVING" << LL_ENDL;
            gAgent.setTeleportState(LLAgent::TELEPORT_ARRIVING);
            gAgent.setTeleportMessage(LLAgent::sTeleportProgressMessages["arriving"]);
            gAgent.sheduleTeleportIM();
            gTextureList.mForceResetTextureStats = true;
            gAgentCamera.resetView(true, true);
            if (!minimized)
            {
                gViewerWindow->setProgressCancelButtonVisible(false, LLTrans::getString("Cancel"));
                gViewerWindow->setProgressPercent(75.f);
            }

            // <FS:Ansariel> FIRE-12004: Attachments getting lost on TP
            gPostTeleportFinishKillObjectDelayTimer.reset();
            break;

        case LLAgent::TELEPORT_ARRIVING:
        // Make the user wait while content "pre-caches"
        {
            F32 arrival_fraction = (gTeleportArrivalTimer.getElapsedTimeF32() / teleport_arrival_delay());
            if (arrival_fraction > 1.f)
            {
                arrival_fraction = 1.f;
                //LLFirstUse::useTeleport();
                LL_INFOS("Teleport") << "arrival_fraction is " << arrival_fraction << " changing state to TELEPORT_NONE" << LL_ENDL;
                gAgent.setTeleportState(LLAgent::TELEPORT_NONE);
            }
            if (!minimized)
            {
                gViewerWindow->setProgressCancelButtonVisible(false, LLTrans::getString("Cancel"));
                gViewerWindow->setProgressPercent(arrival_fraction * 25.f + 75.f);
                gViewerWindow->setProgressString(message);
            }
            break;
        }

        case LLAgent::TELEPORT_LOCAL:
        // Short delay when teleporting in the same sim (progress screen active but not shown - did not
        // fall-through from TELEPORT_START)
        {
            // <FS:CR> FIRE-8721 - Remove local teleport delay
            //if (gTeleportDisplayTimer.getElapsedTimeF32() > teleport_local_delay())
            // </FS:CR>
            {
                //LLFirstUse::useTeleport();
                LL_INFOS("Teleport") << "State is local and gTeleportDisplayTimer " << gTeleportDisplayTimer.getElapsedTimeF32()
                                     << " exceeds teleport_local_delete " << teleport_local_delay
                                     << "; setting state to TELEPORT_NONE"
                                     << LL_ENDL;
                gAgent.setTeleportState(LLAgent::TELEPORT_NONE);
            }
            break;
        }

        case LLAgent::TELEPORT_NONE:
            // No teleport in progress
            gViewerWindow->setShowProgress(false, false);
            gTeleportDisplay = false;
    }
}

// Paint the display!
void display(bool rebuild, F32 zoom_factor, int subfield, bool for_snapshot)
{
#if LL_WINDOWS
    // <VulkanStorm> The Vulkan backend owns the frame end-to-end while the
    // 2D/3D pipelines are being ported: clear + present, no GL calls.
    if (LLVKSession::isRunning())
    {
        LLVKSession::resizeIfNeeded(gViewerWindow->getWindow());
        if (getenv("VULKANSTORM_SCENE") != nullptr)
        {
            LLVKSession::renderFrame();
        }
        else
        {
            vulkan_ui_frame();
        }
        vulkan_capture_tick();
        return;
    }
    // </VulkanStorm>
#endif

    LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Render");
    LL_PROFILE_GPU_ZONE("Render");

    LLPerfStats::RecordSceneTime T (LLPerfStats::StatType_t::RENDER_DISPLAY); // render time capture - This is the main stat for overall rendering.

    LLViewerCamera& camera = LLViewerCamera::instance(); // <FS:Ansariel> Factor out calls to getInstance

    if (gWindowResized)
    { //skip render on frames where window has been resized
        LL_DEBUGS("Window") << "Resizing window" << LL_ENDL;
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Resize Window");
        gGL.flush();
        glClear(GL_COLOR_BUFFER_BIT);
        gViewerWindow->getWindow()->swapBuffers();
        LLPipeline::refreshCachedSettings();
        gPipeline.resizeScreenTexture();
        gResizeScreenTexture = false;
        gWindowResized = false;
        return;
    }

    if (gResizeShadowTexture)
    { //skip render on frames where window has been resized
        gPipeline.resizeShadowTexture();
        // gResizeShadowTexture = false; // <FS:Beq/> This prevents the deferred resize from working properly.
    }

    gSnapshot = for_snapshot;

    if (LLPipeline::sRenderDeferred)
    { //hack to make sky show up in deferred snapshots
        for_snapshot = false;
    }

    LLGLSDefault gls_default;
    LLGLDepthTest gls_depth(GL_TRUE, GL_TRUE, GL_LEQUAL);

    LLVertexBuffer::unbind();

    LLGLState::checkStates();

    gPipeline.disableLights();

    // Don't draw if the window is hidden or minimized.
    // In fact, must explicitly check the minimized state before drawing.
    // Attempting to draw into a minimized window causes a GL error. JC
    if (   !gViewerWindow->getActive()
        || !gViewerWindow->getWindow()->getVisible()
        || gViewerWindow->getWindow()->getMinimized()
        || gNonInteractive)
    {
        // Clean up memory the pools may have allocated
        if (rebuild)
        {
            stop_glerror();
            gPipeline.rebuildPools();
            stop_glerror();
        }

        // <FS:ND> FIRE-15789; Make sure there's not backlog for thousands and thousands of beam objects
        LLHUDObject::renderAllForTimer();
        // </FS:ND>

        stop_glerror();
        gViewerWindow->returnEmptyPicks();
        stop_glerror();

        // We still need to update the teleport progress (to get changes done
        // in TP states, else the sim does not get the messages signaling the
        // agent's arrival). This fixes BUG-230616. HB
        if (gTeleportDisplay)
        {
            // true = minimized, do not show/update the TP screen. HB
            update_tp_display(true);
        }

        // Run texture subsystem to discard memory while backgrounded
        if (!gNonInteractive)
        {
            LL_PROFILE_ZONE_NAMED("Update Images");

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Class");
                LLViewerTexture::updateClass();
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Image Update Bump");
                gBumpImageList.updateImages();  // must be called before gTextureList version so that it's textures are thrown out first.
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("List");
                F32 max_image_decode_time = 0.050f * gFrameIntervalSeconds.value();          // 50 ms/second decode time
                max_image_decode_time     = llclamp(max_image_decode_time, 0.002f, 0.005f);  // min 2ms/frame, max 5ms/frame)
                gTextureList.updateImages(max_image_decode_time);
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("GLTF Materials Cleanup");
                // remove dead gltf materials
                gGLTFMaterialList.flushMaterials();
            }
        }
        return;
    }

    gViewerWindow->checkSettings();

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Picking");
        gViewerWindow->performPick();
    }

    LLAppViewer::instance()->pingMainloopTimeout("Display:CheckStates");
    LLGLState::checkStates();

    //////////////////////////////////////////////////////////
    //
    // Logic for forcing window updates if we're in drone mode.
    //

    // *TODO: Investigate running display() during gHeadlessClient.  See if this early exit is needed DK 2011-02-18
    if (gHeadlessClient)
    {
#if LL_WINDOWS
        static F32 last_update_time = 0.f;
        if ((gFrameTimeSeconds - last_update_time) > 1.f)
        {
            InvalidateRect((HWND)gViewerWindow->getPlatformWindow(), NULL, false);
            last_update_time = gFrameTimeSeconds;
        }
#elif LL_DARWIN
        // MBW -- Do something clever here.
#endif
        // Not actually rendering, don't bother.
        return;
    }


    //
    // Bail out if we're in the startup state and don't want to try to
    // render the world.
    //
    // <FS:Ansariel> Revert to original state to prevent flickering if login progress screen is disabled
    //if (LLStartUp::getStartupState() < STATE_PRECACHE)
    if (LLStartUp::getStartupState() < STATE_STARTED)
    // </FS:Ansariel>
    {
        LLAppViewer::instance()->pingMainloopTimeout("Display:Startup");
        display_startup();
        return;
    }


    if (gShaderProfileFrame)
    {
        LLGLSLShader::initProfile();
    }

    //LLGLState::verify(false);

    /////////////////////////////////////////////////
    //
    // Update GL Texture statistics (used for discard logic?)
    //

    LLAppViewer::instance()->pingMainloopTimeout("Display:TextureStats");
    stop_glerror();

    LLImageGL::updateStats(gFrameTimeSeconds);

    static LLCachedControl<S32> avatar_name_tag_mode(gSavedSettings, "AvatarNameTagMode", 1);
    static LLCachedControl<S32> name_tag_show_group_titles(gSavedSettings, "GroupTitlesTagMode", 2 /*all group tags*/);
// <FS:CR> Aurora sim
    //LLVOAvatar::sRenderName = avatar_name_tag_mode;
    //LLVOAvatar::sRenderGroupTitles = avatar_name_tag_mode > 0 ? name_tag_show_group_titles : 0;;
    auto& world_instance = LLWorld::instance();
    LLVOAvatar::sRenderName = avatar_name_tag_mode > world_instance.getAllowRenderName() ? world_instance.getAllowRenderName() : avatar_name_tag_mode;
    LLVOAvatar::sRenderGroupTitles = LLVOAvatar::sRenderName > 0 ? name_tag_show_group_titles : 0;
// <FS:CR> Aurora sim

    gPipeline.mBackfaceCull = true;
    gFrameCount++;
    gRecentFrameCount++;
    if (gFocusMgr.getAppHasFocus())
    {
        gForegroundFrameCount++;
    }

    //////////////////////////////////////////////////////////
    //
    // Display start screen if we're teleporting, and skip render
    //

    if (gTeleportDisplay)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Teleport Display");
        LLAppViewer::instance()->pingMainloopTimeout("Display:Teleport");
        // Note: false = not minimized, do update the TP screen. HB
        update_tp_display(false);
    }
    else if(LLAppViewer::instance()->logoutRequestSent())
    {
        LLAppViewer::instance()->pingMainloopTimeout("Display:Logout");
        F32 percent_done = gLogoutTimer.getElapsedTimeF32() * 100.f / gLogoutMaxTime;
        if (percent_done > 100.f)
        {
            percent_done = 100.f;
        }

        if( LLApp::isExiting() )
        {
            percent_done = 100.f;
        }

        gViewerWindow->setProgressPercent( percent_done );
        gViewerWindow->setProgressMessage(std::string());
    }
    else
    if (gRestoreGL)
    {
        LLAppViewer::instance()->pingMainloopTimeout("Display:RestoreGL");
        F32 percent_done = gRestoreGLTimer.getElapsedTimeF32() * 100.f / RESTORE_GL_TIME;
        if( percent_done > 100.f )
        {
            gViewerWindow->setShowProgress(false,false);
            gRestoreGL = false;
        }
        else
        {

            if( LLApp::isExiting() )
            {
                percent_done = 100.f;
            }

            gViewerWindow->setProgressPercent( percent_done );
        }
        gViewerWindow->setProgressMessage(std::string());
    }

    // <FS::Ansariel> Draw Distance stepping; originally based on SpeedRez by Henri Beauchamp, licensed under LGPL
    // Progressively increase draw distance after TP when required.
    static LLCachedControl<F32> renderFarClip(gSavedSettings, "RenderFarClip");
    if (gSavedDrawDistance > 0.0f && gAgent.getTeleportState() == LLAgent::TELEPORT_NONE)
    {
        if (gLastDrawDistanceStep != renderFarClip())
        {
            LLPresetsManager::instance().setIsDrawDistanceSteppingActive(false);
            gSavedDrawDistance = 0.0f;
            gLastDrawDistanceStep = 0.0f;
            gSavedSettings.setF32("FSSavedRenderFarClip", 0.0f);
        }

        if (gTeleportArrivalTimer.getElapsedTimeF32() >=
            (F32)gSavedSettings.getU32("FSRenderFarClipSteppingInterval"))
        {
            gTeleportArrivalTimer.reset();
            F32 current = gSavedSettings.getF32("RenderFarClip");
            if (gSavedDrawDistance > current)
            {
                current *= 2.0f;
                if (current > gSavedDrawDistance)
                {
                    current = gSavedDrawDistance;
                }
                gSavedSettings.setF32("RenderFarClip", current);
                gLastDrawDistanceStep = current;
            }
            if (current >= gSavedDrawDistance)
            {
                LLPresetsManager::instance().setIsDrawDistanceSteppingActive(false);
                gSavedDrawDistance = 0.0f;
                gLastDrawDistanceStep = 0.0f;
                gSavedSettings.setF32("FSSavedRenderFarClip", 0.0f);
            }
        }
    }
    // </FS::Ansariel>

    //////////////////////////
    //
    // Prepare for the next frame
    //

    /////////////////////////////
    //
    // Update the camera
    //
    //

    LLAppViewer::instance()->pingMainloopTimeout("Display:Camera");
    if (LLViewerCamera::instanceExists())
    {
        LLViewerCamera::getInstance()->setZoomParameters(zoom_factor, subfield);
        LLViewerCamera::getInstance()->setNear(MIN_NEAR_PLANE);
    }

    //////////////////////////
    //
    // clear the next buffer
    // (must follow dynamic texture writing since that uses the frame buffer)
    //

    if (gDisconnected)
    {
        LLAppViewer::instance()->pingMainloopTimeout("Display:Disconnected");
        render_ui();
        swap();
    }

    //////////////////////////
    //
    // Set rendering options
    //
    //
    LLAppViewer::instance()->pingMainloopTimeout("Display:RenderSetup");
    stop_glerror();

    ///////////////////////////////////////
    //
    // Slam lighting parameters back to our defaults.
    // Note that these are not the same as GL defaults...

    stop_glerror();
    gGL.setAmbientLightColor(LLColor4::white);
    stop_glerror();

    /////////////////////////////////////
    //
    // Render
    //
    // Actually push all of our triangles to the screen.
    //

    // do render-to-texture stuff here
    if (gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_DYNAMIC_TEXTURES))
    {
        LLAppViewer::instance()->pingMainloopTimeout("Display:DynamicTextures");
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Update Dynamic Textures");
        if (LLViewerDynamicTexture::updateAllInstances())
        {
            gGL.setColorMask(true, true);
            glClear(GL_DEPTH_BUFFER_BIT);
        }
    }

    gViewerWindow->setup3DViewport();

    gPipeline.resetFrameStats();    // Reset per-frame statistics.

    if (!gDisconnected && !LLApp::isExiting())
    {
        // Render mirrors and associated hero probes before we render the rest of the scene.
        // This ensures the scene state in the hero probes are exactly the same as the rest of the scene before we render it.
        if (gPipeline.RenderMirrors && !gSnapshot)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Update hero probes");
            LL_PROFILE_GPU_ZONE("hero manager")
            gPipeline.mHeroProbeManager.update();
            gPipeline.mHeroProbeManager.renderProbes();
        }

        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("display - 1");
        LLAppViewer::instance()->pingMainloopTimeout("Display:Update");
        if (gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_HUD))
        { //don't draw hud objects in this frame
            gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD);
        }

        if (gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_HUD_PARTICLES))
        { //don't draw hud particles in this frame
            gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD_PARTICLES);
        }

        stop_glerror();
        display_update_camera();
        stop_glerror();

        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Env Update");
            // update all the sky/atmospheric/water settings
            LLEnvironment::instance().update(&camera); // <FS:Ansariel> Factor out calls to getInstance
        }

        // *TODO: merge these two methods
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("HUD Update");
            LLHUDManager::getInstance()->updateEffects();
            LLHUDObject::updateAll();
            stop_glerror();
        }

        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Update Geom");
            const F32 max_geom_update_time = 0.005f*10.f*gFrameIntervalSeconds.value(); // 50 ms/second update time
            gPipeline.createObjects(max_geom_update_time);
            gPipeline.processPartitionQ();
            gPipeline.updateGeom(max_geom_update_time);
            stop_glerror();
        }

        gPipeline.updateGL();

        stop_glerror();

        LLAppViewer::instance()->pingMainloopTimeout("Display:Cull");

        //Increment drawable frame counter
        LLDrawable::incrementVisible();

        LLSpatialGroup::sNoDelete = true;
        LLTexUnit::sWhiteTexture = LLViewerFetchedTexture::sWhiteImagep->getTexName();

        S32 occlusion = LLPipeline::sUseOcclusion;
        if (gDepthDirty)
        { //depth buffer is invalid, don't overwrite occlusion state
            LLPipeline::sUseOcclusion = llmin(occlusion, 1);
        }
        gDepthDirty = false;

        LLGLState::checkStates();

        static LLCullResult result;
        LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;
        LLPipeline::sUnderWaterRender = LLViewerCamera::getInstance()->cameraUnderWater();
        gPipeline.updateCull(*LLViewerCamera::getInstance(), result);
        stop_glerror();

        LLGLState::checkStates();

        LLAppViewer::instance()->pingMainloopTimeout("Display:Swap");

        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("display - 2")
            if (gResizeScreenTexture)
            {
                gPipeline.resizeScreenTexture();
                gResizeScreenTexture = false;
            }

            gGL.setColorMask(true, true);
            glClearColor(0.f, 0.f, 0.f, 0.f);

            LLGLState::checkStates();

            if (!for_snapshot)
            {
                if (gFrameCount > 1 && !for_snapshot)
                { //for some reason, ATI 4800 series will error out if you
                  //try to generate a shadow before the first frame is through
                    gPipeline.generateSunShadow(*LLViewerCamera::getInstance());
                }

                LLVertexBuffer::unbind();

                LLGLState::checkStates();

                glm::mat4 proj = get_current_projection();
                glm::mat4 mod = get_current_modelview();
                glViewport(0,0,512,512);

                LLVOAvatar::updateImpostors();

                set_current_projection(proj);
                set_current_modelview(mod);
                gGL.matrixMode(LLRender::MM_PROJECTION);
                gGL.loadMatrix(glm::value_ptr(proj));
                gGL.matrixMode(LLRender::MM_MODELVIEW);
                gGL.loadMatrix(glm::value_ptr(mod));
                gViewerWindow->setup3DViewport();

                LLGLState::checkStates();
            }
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        //////////////////////////////////////
        //
        // Update images, using the image stats generated during object update/culling
        //
        // Can put objects onto the retextured list.
        //
        // Doing this here gives hardware occlusion queries extra time to complete
        LLAppViewer::instance()->pingMainloopTimeout("Display:UpdateImages");

        {
            LL_PROFILE_ZONE_NAMED("Update Images");

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Class");
                LLViewerTexture::updateClass();
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Image Update Bump");
                gBumpImageList.updateImages();  // must be called before gTextureList version so that it's textures are thrown out first.
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("List");
                F32 max_image_decode_time = 0.050f*gFrameIntervalSeconds.value(); // 50 ms/second decode time
                max_image_decode_time = llclamp(max_image_decode_time, 0.002f, 0.005f ); // min 2ms/frame, max 5ms/frame)
                gTextureList.updateImages(max_image_decode_time);
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("GLTF Materials Cleanup");
                //remove dead gltf materials
                gGLTFMaterialList.flushMaterials();
            }
        }

        LLGLState::checkStates();

        ///////////////////////////////////
        //
        // StateSort
        //
        // Responsible for taking visible objects, and adding them to the appropriate draw orders.
        // In the case of alpha objects, z-sorts them first.
        // Also creates special lists for outlines and selected face rendering.
        //
        LLAppViewer::instance()->pingMainloopTimeout("Display:StateSort");
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("display - 4")
            LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;
            gPipeline.stateSort(camera, result); // <FS:Ansariel> Factor out calls to getInstance
            stop_glerror();

            if (rebuild)
            {
                //////////////////////////////////////
                //
                // rebuildPools
                //
                //
                gPipeline.rebuildPools();
                stop_glerror();
            }
        }

        LLSceneMonitor::getInstance()->fetchQueryResult();

        LLGLState::checkStates();

        LLPipeline::sUseOcclusion = occlusion;

        {
            LLAppViewer::instance()->pingMainloopTimeout("Display:Sky");
            LL_PROFILE_ZONE_NAMED_CATEGORY_ENVIRONMENT("update sky"); //LL_RECORD_BLOCK_TIME(FTM_UPDATE_SKY);
            gSky.updateSky();
        }

        if(gUseWireframe)
        {
            glClearColor(0.5f, 0.5f, 0.5f, 0.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        LLAppViewer::instance()->pingMainloopTimeout("Display:RenderStart");

        //// render frontmost floater opaque for occlusion culling purposes
        //LLFloater* frontmost_floaterp = gFloaterView->getFrontmost();
        //// assumes frontmost floater with focus is opaque
        //if (frontmost_floaterp && gFocusMgr.childHasKeyboardFocus(frontmost_floaterp))
        //{
        //  gGL.matrixMode(LLRender::MM_MODELVIEW);
        //  gGL.pushMatrix();
        //  {
        //      gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

        //      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        //      gGL.loadIdentity();

        //      LLRect floater_rect = frontmost_floaterp->calcScreenRect();
        //      // deflate by one pixel so rounding errors don't occlude outside of floater extents
        //      floater_rect.stretch(-1);
        //      LLRectf floater_3d_rect((F32)floater_rect.mLeft / (F32)gViewerWindow->getWindowWidthScaled(),
        //                              (F32)floater_rect.mTop / (F32)gViewerWindow->getWindowHeightScaled(),
        //                              (F32)floater_rect.mRight / (F32)gViewerWindow->getWindowWidthScaled(),
        //                              (F32)floater_rect.mBottom / (F32)gViewerWindow->getWindowHeightScaled());
        //      floater_3d_rect.translate(-0.5f, -0.5f);
        //      gGL.translatef(0.f, 0.f, -LLViewerCamera::getInstance()->getNear());
        //      gGL.scalef(LLViewerCamera::getInstance()->getNear() * LLViewerCamera::getInstance()->getAspect() / sinf(LLViewerCamera::getInstance()->getView()), LLViewerCamera::getInstance()->getNear() / sinf(LLViewerCamera::getInstance()->getView()), 1.f);
        //      gGL.color4fv(LLColor4::white.mV);
        //      gGL.begin(LLVertexBuffer::QUADS);
        //      {
        //          gGL.vertex3f(floater_3d_rect.mLeft, floater_3d_rect.mBottom, 0.f);
        //          gGL.vertex3f(floater_3d_rect.mLeft, floater_3d_rect.mTop, 0.f);
        //          gGL.vertex3f(floater_3d_rect.mRight, floater_3d_rect.mTop, 0.f);
        //          gGL.vertex3f(floater_3d_rect.mRight, floater_3d_rect.mBottom, 0.f);
        //      }
        //      gGL.end();
        //      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        //  }
        //  gGL.popMatrix();
        //}

        LLPipeline::sUnderWaterRender = LLViewerCamera::getInstance()->cameraUnderWater();

// <FS:CR> Aurora Sim
        if (!LLWorld::getInstance()->getAllowRenderWater())
        {
            LLPipeline::sUnderWaterRender = false;
        }
// </FS:CR> Aurora Sim
        LLGLState::checkStates();

        stop_glerror();

        gGL.setColorMask(true, true);

        gPipeline.mRT->deferredScreen.bindTarget();
        if (gUseWireframe)
        {
            constexpr F32 g = 0.5f;
            glClearColor(g, g, g, 1.f);
        }
        else
        {
            glClearColor(1, 0, 1, 1);
        }
        gPipeline.mRT->deferredScreen.clear();

        gGL.setColorMask(true, false);

        LLAppViewer::instance()->pingMainloopTimeout("Display:RenderGeom");

        if (!(LLAppViewer::instance()->logoutRequestSent() && LLAppViewer::instance()->hasSavedFinalSnapshot())
                && !gRestoreGL)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("display - 5")
            LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;

            static LLCachedControl<bool> render_depth_pre_pass(gSavedSettings, "RenderDepthPrePass", false);
            if (render_depth_pre_pass)
            {
                gGL.setColorMask(false, false);

                constexpr U32 types[] = {
                    LLRenderPass::PASS_SIMPLE,
                    LLRenderPass::PASS_FULLBRIGHT,
                    LLRenderPass::PASS_SHINY
                };

                U32 num_types = LL_ARRAY_SIZE(types);
                gOcclusionProgram.bind();
                for (U32 i = 0; i < num_types; i++)
                {
                    gPipeline.renderObjects(types[i], LLVertexBuffer::MAP_VERTEX, false);
                }

                gOcclusionProgram.unbind();

            }

            gGL.setColorMask(true, true);
            gPipeline.renderGeomDeferred(*LLViewerCamera::getInstance(), true);
        }

        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Texture Unbind");
            for (S32 i = 0; i < gGLManager.mNumTextureImageUnits; i++)
            { //dummy cleanup of any currently bound textures
                if (gGL.getTexUnit(i)->getCurrType() != LLTexUnit::TT_NONE)
                {
                    gGL.getTexUnit(i)->unbind(gGL.getTexUnit(i)->getCurrType());
                    gGL.getTexUnit(i)->disable();
                }
            }
        }

        LLAppViewer::instance()->pingMainloopTimeout("Display:RenderFlush");

        LLRenderTarget &rt = (gPipeline.sRenderDeferred ? gPipeline.mRT->deferredScreen : gPipeline.mRT->screen);
        rt.flush();

        if (LLPipeline::sRenderDeferred)
        {
            gPipeline.renderDeferredLighting();
        }

        LLPipeline::sUnderWaterRender = false;

        {
            //capture the frame buffer.
            LLSceneMonitor::getInstance()->capture();
        }

        LLAppViewer::instance()->pingMainloopTimeout("Display:RenderUI");
        if (!for_snapshot)
        {
            render_ui();
            swap();
        }


        LLSpatialGroup::sNoDelete = false;

        gPipeline.clearReferences();
    }

    LLAppViewer::instance()->pingMainloopTimeout("Display:FrameStats");

    stop_glerror();

    display_stats();

    LLAppViewer::instance()->pingMainloopTimeout("Display:Done");

    gShiftFrame = false;

    if (gShaderProfileFrame)
    {
        gShaderProfileFrame = false;
        boost::json::value stats{ boost::json::object_kind };
        getProfileStatsContext(stats.as_object());
        LLGLSLShader::finishProfile(stats);

        auto report_name = getProfileStatsFilename();
        std::ofstream outf(report_name);
        if (! outf)
        {
            LL_WARNS() << "Couldn't write to " << std::quoted(report_name) << LL_ENDL;
        }
        else
        {
            outf << stats;
            LL_INFOS() << "(also dumped to " << std::quoted(report_name) << ")" << LL_ENDL;
        }
    }
}

void getProfileStatsContext(boost::json::object& stats)
{
    // populate the context with info from LLFloaterAbout
    auto contextit = stats.emplace("context",
                                   LlsdToJson(LLAppViewer::instance()->getViewerInfo())).first;
    auto& context = contextit->value().as_object();

    // then add a few more things
    unsigned char unique_id[MAC_ADDRESS_BYTES]{};
    LLMachineID::getUniqueID(unique_id, sizeof(unique_id));
    context.emplace("machine", stringize(LL::hexdump(unique_id, sizeof(unique_id))));
    context.emplace("grid", LLGridManager::instance().getGrid());
    LLViewerRegion* region = gAgent.getRegion();
    if (region)
    {
        context.emplace("regionid", stringize(region->getRegionID()));
    }
    LLParcel* parcel = LLViewerParcelMgr::instance().getAgentParcel();
    if (parcel)
    {
        context.emplace("parcel", parcel->getName());
        context.emplace("parcelid", parcel->getLocalID());
    }
    context.emplace("time", LLDate::now().toHTTPDateString("%Y-%m-%dT%H:%M:%S"));
}

std::string getProfileStatsFilename()
{
    std::ostringstream basebuff;
    // viewer build
    basebuff << "profile.v" << LLVersionInfo::instance().getBuild();
    // machine ID: zero-initialize unique_id in case LLMachineID fails
    unsigned char unique_id[MAC_ADDRESS_BYTES]{};
    LLMachineID::getUniqueID(unique_id, sizeof(unique_id));
    basebuff << ".m" << LL::hexdump(unique_id, sizeof(unique_id));
    // region ID
    LLViewerRegion *region = gAgent.getRegion();
    basebuff << ".r" << (region? region->getRegionID() : LLUUID());
    // local parcel ID
    LLParcel* parcel = LLViewerParcelMgr::instance().getAgentParcel();
    basebuff << ".p" << (parcel? parcel->getLocalID() : 0);
    // date/time -- omit seconds for now
    auto now = LLDate::now();
    basebuff << ".t" << LLDate::now().toHTTPDateString("%Y-%m-%dT%H-%M-");
    // put this candidate file in our logs directory
    auto base = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, basebuff.str());
    S32 sec;
    now.split(nullptr, nullptr, nullptr, nullptr, nullptr, &sec);
    // Loop over finished filename, incrementing sec until we find one that
    // doesn't yet exist. Should rarely loop (only if successive calls within
    // same second), may produce (e.g.) sec==61, but avoids collisions and
    // preserves chronological filename sort order.
    std::string name;
    std::error_code ec;
    do
    {
        // base + missing 2-digit seconds, append ".json"
        // post-increment sec in case we have to try again
        name = stringize(base, std::setw(2), std::setfill('0'), sec++, ".json");
    } while (std::filesystem::exists(fsyspath(name), ec));
    // Ignoring ec means we might potentially return a name that does already
    // exist -- but if we can't check its existence, what more can we do?
    return name;
}

// WIP simplified copy of display() that does minimal work
void display_cube_face()
{
    LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Render Cube Face");
    LL_PROFILE_GPU_ZONE("display cube face");

    llassert(!gSnapshot);
    llassert(!gTeleportDisplay);
    llassert(LLStartUp::getStartupState() >= STATE_PRECACHE);
    llassert(!LLAppViewer::instance()->logoutRequestSent());
    llassert(!gRestoreGL);

    bool rebuild = false;

    LLGLSDefault gls_default;
    LLGLDepthTest gls_depth(GL_TRUE, GL_TRUE, GL_LEQUAL);

    LLVertexBuffer::unbind();

    gPipeline.disableLights();

    gPipeline.mBackfaceCull = true;

    gViewerWindow->setup3DViewport();

    if (gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_HUD))
    { //don't draw hud objects in this frame
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD);
    }

    if (gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_HUD_PARTICLES))
    { //don't draw hud particles in this frame
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD_PARTICLES);
    }

    display_update_camera();

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Env Update");
        // update all the sky/atmospheric/water settings
        LLEnvironment::instance().update(LLViewerCamera::getInstance());
    }

    LLSpatialGroup::sNoDelete = true;

    S32 occlusion = LLPipeline::sUseOcclusion;
    LLPipeline::sUseOcclusion = 0; // occlusion data is from main camera point of view, don't read or write it during cube snapshots
    //gDepthDirty = true; //let "real" render pipe know it can't trust the depth buffer for occlusion data

    static LLCullResult result;
    LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;
    LLPipeline::sUnderWaterRender = LLViewerCamera::getInstance()->cameraUnderWater();
    gPipeline.updateCull(*LLViewerCamera::getInstance(), result);

    gGL.setColorMask(true, true);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    gPipeline.generateSunShadow(*LLViewerCamera::getInstance());

    glClear(GL_DEPTH_BUFFER_BIT); // | GL_STENCIL_BUFFER_BIT);

    {
        LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;
        gPipeline.stateSort(*LLViewerCamera::getInstance(), result);

        if (rebuild)
        {
            //////////////////////////////////////
            //
            // rebuildPools
            //
            //
            gPipeline.rebuildPools();
            stop_glerror();
        }
    }

    LLPipeline::sUseOcclusion = occlusion;

    LLAppViewer::instance()->pingMainloopTimeout("Display:RenderStart");

    LLPipeline::sUnderWaterRender = LLViewerCamera::getInstance()->cameraUnderWater();

    gGL.setColorMask(true, true);

    gPipeline.mRT->deferredScreen.bindTarget();
    if (gUseWireframe)
    {
        glClearColor(0.5f, 0.5f, 0.5f, 1.f);
    }
    else
    {
        glClearColor(1.f, 0.f, 1.f, 1.f);
    }
    gPipeline.mRT->deferredScreen.clear();

    LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;

    gPipeline.renderGeomDeferred(*LLViewerCamera::getInstance());

    gPipeline.mRT->deferredScreen.flush();

    gPipeline.renderDeferredLighting();

    LLPipeline::sUnderWaterRender = false;

    // Finalize scene
    //gPipeline.renderFinalize();

    LLSpatialGroup::sNoDelete = false;
    gPipeline.clearReferences();
}

void render_hud_attachments()
{
    LLPerfStats::RecordSceneTime T ( LLPerfStats::StatType_t::RENDER_HUDS); // render time capture - Primary contributor to HUDs (though these end up in render batches)
    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.pushMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();

    glm::mat4 current_proj = get_current_projection();
    glm::mat4 current_mod = get_current_modelview();

    // clamp target zoom level to reasonable values
//  gAgentCamera.mHUDTargetZoom = llclamp(gAgentCamera.mHUDTargetZoom, 0.1f, 1.f);
// [RLVa:KB] - Checked: 2010-08-22 (RLVa-1.2.1a) | Modified: RLVa-1.0.0c
    gAgentCamera.mHUDTargetZoom = llclamp(gAgentCamera.mHUDTargetZoom, (!gRlvAttachmentLocks.hasLockedHUD()) ? 0.1f : 0.85f, 1.f);
// [/RLVa:KB]

    // smoothly interpolate current zoom level
    gAgentCamera.mHUDCurZoom = lerp(gAgentCamera.mHUDCurZoom, gAgentCamera.getAgentHUDTargetZoom(), LLSmoothInterpolation::getInterpolant(0.03f));

    if (LLPipeline::sShowHUDAttachments && !gDisconnected && setup_hud_matrices())
    {
        LLPipeline::sRenderingHUDs = true;
        LLCamera hud_cam = *LLViewerCamera::getInstance();
        hud_cam.setOrigin(-1.f, 0.f, 0.f);
        hud_cam.setAxes(LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 1.f, 0.f), LLVector3(0.f, 0.f, 1.f));
        LLViewerCamera::updateFrustumPlanes(hud_cam, true);

        static LLCachedControl<bool> render_hud_particles(gSavedSettings, "RenderHUDParticles", false);
        bool render_particles = gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_PARTICLES) && render_hud_particles;

        //only render hud objects
        gPipeline.pushRenderTypeMask();

        // turn off everything
        gPipeline.andRenderTypeMask(LLPipeline::END_RENDER_TYPES);
        // turn on HUD
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD);
        // turn on HUD particles
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD_PARTICLES);

        // if particles are off, turn off hud-particles as well
        if (!render_particles)
        {
            // turn back off HUD particles
            gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD_PARTICLES);
        }

        bool has_ui = gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI);
        if (has_ui)
        {
            gPipeline.toggleRenderDebugFeature(LLPipeline::RENDER_DEBUG_FEATURE_UI);
        }

        S32 use_occlusion = LLPipeline::sUseOcclusion;
        LLPipeline::sUseOcclusion = 0;

        //cull, sort, and render hud objects
        static LLCullResult result;
        LLSpatialGroup::sNoDelete = true;

        LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;
        gPipeline.updateCull(hud_cam, result, true);

        // Toggle render types
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_BUMP);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_SIMPLE);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_VOLUME);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_ALPHA);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_ALPHA_PRE_WATER);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_ALPHA_MASK);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_FULLBRIGHT_ALPHA_MASK);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_FULLBRIGHT);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_GLTF_PBR);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_GLTF_PBR_ALPHA_MASK);

        // Toggle render passes
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_ALPHA);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_ALPHA_MASK);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_BUMP);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_MATERIAL);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_SHINY);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_SHINY);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_INVISIBLE);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_INVISI_SHINY);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_GLTF_PBR);
        gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK);

        gPipeline.stateSort(hud_cam, result);

        gPipeline.renderGeomPostDeferred(hud_cam);

        LLSpatialGroup::sNoDelete = false;
        //gPipeline.clearReferences();

        render_hud_elements();

        //restore type mask
        gPipeline.popRenderTypeMask();

        if (has_ui)
        {
            gPipeline.toggleRenderDebugFeature(LLPipeline::RENDER_DEBUG_FEATURE_UI);
        }
        LLPipeline::sUseOcclusion = use_occlusion;
        LLPipeline::sRenderingHUDs = false;
    }
    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.popMatrix();

    set_current_projection(current_proj);
    set_current_modelview(current_mod);
}

LLRect get_whole_screen_region()
{
    // <FS:Ansariel> Factor out calls to getInstance
    LLViewerCamera& camera = LLViewerCamera::instance();

    LLRect whole_screen = gViewerWindow->getWorldViewRectScaled();

    // apply camera zoom transform (for high res screenshots)
    F32 zoom_factor = camera.getZoomFactor(); // <FS:Ansariel> Factor out calls to getInstance
    S16 sub_region = camera.getZoomSubRegion(); // <FS:Ansariel> Factor out calls to getInstance
    if (zoom_factor > 1.f)
    {
        S32 num_horizontal_tiles = llceil(zoom_factor);
        S32 tile_width = ll_round((F32)gViewerWindow->getWorldViewWidthScaled() / zoom_factor);
        S32 tile_height = ll_round((F32)gViewerWindow->getWorldViewHeightScaled() / zoom_factor);
        int tile_y = sub_region / num_horizontal_tiles;
        int tile_x = sub_region - (tile_y * num_horizontal_tiles);

        whole_screen.setLeftTopAndSize(tile_x * tile_width, gViewerWindow->getWorldViewHeightScaled() - (tile_y * tile_height), tile_width, tile_height);
    }
    return whole_screen;
}

bool get_hud_matrices(const LLRect& screen_region, glm::mat4 &proj, glm::mat4&model)
{
    if (isAgentAvatarValid() && gAgentAvatarp->hasHUDAttachment())
    {
        F32 zoom_level = gAgentCamera.mHUDCurZoom;
        LLBBox hud_bbox = gAgentAvatarp->getHUDBBox();

        F32 hud_depth = llmax(1.f, hud_bbox.getExtentLocal().mV[VX] * 1.1f);

        // <FS:Ansariel> Factor out calls to getInstance
        //proj = glm::ortho(-0.5f * LLViewerCamera::getInstance()->getAspect(), 0.5f * LLViewerCamera::getInstance()->getAspect(), -0.5f, 0.5f, 0.f, hud_depth);
        //proj[2][2] = -0.01f;

        F32 aspect_ratio = LLViewerCamera::getInstance()->getAspect();
        proj = glm::ortho(-0.5f * aspect_ratio, 0.5f * aspect_ratio, -0.5f, 0.5f, 0.f, hud_depth);
        proj[2][2] = -0.01f;
        // <//FS:Ansariel> Factor out calls to getInstance

        F32 scale_x = (F32)gViewerWindow->getWorldViewWidthScaled() / (F32)screen_region.getWidth();
        F32 scale_y = (F32)gViewerWindow->getWorldViewHeightScaled() / (F32)screen_region.getHeight();

        glm::mat4 mat = glm::identity<glm::mat4>();
        mat = glm::translate(mat,
            glm::vec3(clamp_rescale((F32)(screen_region.getCenterX() - screen_region.mLeft), 0.f, (F32)gViewerWindow->getWorldViewWidthScaled(), 0.5f * scale_x * aspect_ratio, -0.5f * scale_x * aspect_ratio),
                clamp_rescale((F32)(screen_region.getCenterY() - screen_region.mBottom), 0.f, (F32)gViewerWindow->getWorldViewHeightScaled(), 0.5f * scale_y, -0.5f * scale_y),
                0.f));
        mat = glm::scale(mat, glm::vec3(scale_x, scale_y, 1.f));
        proj *= mat;

        glm::mat4 tmp_model = glm::make_mat4(OGL_TO_CFR_ROTATION);
        mat = glm::identity<glm::mat4>();
        mat = glm::translate(mat, glm::vec3(-hud_bbox.getCenterLocal().mV[VX] + (hud_depth * 0.5f), 0.f, 0.f));
        mat = glm::scale(mat, glm::vec3(zoom_level));
        tmp_model *= mat;
        model = tmp_model;

        return true;
    }
    else
    {
        return false;
    }
}

bool get_hud_matrices(glm::mat4 &proj, glm::mat4&model)
{
    LLRect whole_screen = get_whole_screen_region();
    return get_hud_matrices(whole_screen, proj, model);
}

bool setup_hud_matrices()
{
    LLRect whole_screen = get_whole_screen_region();
    return setup_hud_matrices(whole_screen);
}

bool setup_hud_matrices(const LLRect& screen_region)
{
    glm::mat4 proj, model;
    bool result = get_hud_matrices(screen_region, proj, model);
    if (!result) return result;

    // set up transform to keep HUD objects in front of camera
    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.loadMatrix(glm::value_ptr(proj));
    set_current_projection(proj);

    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.loadMatrix(glm::value_ptr(model));
    set_current_modelview(model);
    return true;
}

void render_ui(F32 zoom_factor, int subfield)
{
    LLPerfStats::RecordSceneTime T ( LLPerfStats::StatType_t::RENDER_UI ); // render time capture - Primary UI stat can have HUD time overlap (TODO)
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI; //LL_RECORD_BLOCK_TIME(FTM_RENDER_UI);
    LL_PROFILE_GPU_ZONE("ui");
    LLGLState::checkStates();

    glm::mat4 saved_view = get_current_modelview();

    if (!gSnapshot)
    {
        gGL.pushMatrix();
        gGL.loadMatrix(gGLLastModelView);
        set_current_modelview(glm::make_mat4(gGLLastModelView));
    }

    if(LLSceneMonitor::getInstance()->needsUpdate())
    {
        gGL.pushMatrix();
        gViewerWindow->setup2DRender();
        LLSceneMonitor::getInstance()->compare();
        gViewerWindow->setup3DRender();
        gGL.popMatrix();
    }

    // apply gamma correction and post effects
    gPipeline.renderFinalize();

    {
        LLGLState::checkStates();


        LL_PROFILE_ZONE_NAMED_CATEGORY_UI("HUD");
    render_hud_elements();
// [RLVa:KB] - Checked: RLVa-2.2 (@setoverlay)
        if (RlvActions::hasBehaviour(RLV_BHVR_SETOVERLAY))
        {
            LLVfxManager::instance().runEffect(EVisualEffect::RlvOverlay);
        }
// [/RLVa:KB]
        LLGLState::checkStates();
        render_hud_attachments();

        LLGLState::checkStates();

        LLGLSDefault gls_default;
        LLGLSUIDefault gls_ui;
        {
            gPipeline.disableLights();
        }

        bool render_ui = gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI);
        if (render_ui)
        {
            if (!gDisconnected)
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_UI("UI 3D"); //LL_RECORD_BLOCK_TIME(FTM_RENDER_UI_3D);
                LLGLState::checkStates();
                render_ui_3d();
                LLGLState::checkStates();
            }
            else
            {
                render_disconnected_background();
            }
        }
        else
        {
            // Make sure particle effects disappear
            LLHUDObject::renderAllForTimer();
        }

        if (render_ui)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_UI("UI 2D"); //LL_RECORD_BLOCK_TIME(FTM_RENDER_UI_2D);
            LLHUDObject::renderAll();
            render_ui_2d();
        }
        // <FS:Beq> FIRE-33239 - particles do not sie when UI is disabled
        if (!render_ui)
        {
            // Make sure particle effects disappear
            LLHUDObject::renderAllForTimer();
        }
        // </FS:Beq>

        gViewerWindow->setup2DRender();
        gViewerWindow->updateDebugText();
        gViewerWindow->drawDebugText();
    }

    if (!gSnapshot)
    {
        set_current_modelview(saved_view);
        gGL.popMatrix();
    }
}

void swap()
{
    LLPerfStats::RecordSceneTime T ( LLPerfStats::StatType_t::RENDER_SWAP ); // render time capture - Swap buffer time - can signify excessive data transfer to/from GPU
    LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("Swap");
    LL_PROFILE_GPU_ZONE("swap");
    if (gDisplaySwapBuffers)
    {
        gViewerWindow->getWindow()->swapBuffers();
    }
    gDisplaySwapBuffers = true;
}

void renderCoordinateAxes()
{
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gGL.begin(LLRender::LINES);
        gGL.color3f(1.0f, 0.0f, 0.0f);   // i direction = X-Axis = red
        gGL.vertex3f(0.0f, 0.0f, 0.0f);
        gGL.vertex3f(2.0f, 0.0f, 0.0f);
        gGL.vertex3f(3.0f, 0.0f, 0.0f);
        gGL.vertex3f(5.0f, 0.0f, 0.0f);
        gGL.vertex3f(6.0f, 0.0f, 0.0f);
        gGL.vertex3f(8.0f, 0.0f, 0.0f);
        // Make an X
        gGL.vertex3f(11.0f, 1.0f, 1.0f);
        gGL.vertex3f(11.0f, -1.0f, -1.0f);
        gGL.vertex3f(11.0f, 1.0f, -1.0f);
        gGL.vertex3f(11.0f, -1.0f, 1.0f);

        gGL.color3f(0.0f, 1.0f, 0.0f);   // j direction = Y-Axis = green
        gGL.vertex3f(0.0f, 0.0f, 0.0f);
        gGL.vertex3f(0.0f, 2.0f, 0.0f);
        gGL.vertex3f(0.0f, 3.0f, 0.0f);
        gGL.vertex3f(0.0f, 5.0f, 0.0f);
        gGL.vertex3f(0.0f, 6.0f, 0.0f);
        gGL.vertex3f(0.0f, 8.0f, 0.0f);
        // Make a Y
        gGL.vertex3f(1.0f, 11.0f, 1.0f);
        gGL.vertex3f(0.0f, 11.0f, 0.0f);
        gGL.vertex3f(-1.0f, 11.0f, 1.0f);
        gGL.vertex3f(0.0f, 11.0f, 0.0f);
        gGL.vertex3f(0.0f, 11.0f, 0.0f);
        gGL.vertex3f(0.0f, 11.0f, -1.0f);

        gGL.color3f(0.0f, 0.0f, 1.0f);   // Z-Axis = blue
        gGL.vertex3f(0.0f, 0.0f, 0.0f);
        gGL.vertex3f(0.0f, 0.0f, 2.0f);
        gGL.vertex3f(0.0f, 0.0f, 3.0f);
        gGL.vertex3f(0.0f, 0.0f, 5.0f);
        gGL.vertex3f(0.0f, 0.0f, 6.0f);
        gGL.vertex3f(0.0f, 0.0f, 8.0f);
        // Make a Z
        gGL.vertex3f(-1.0f, 1.0f, 11.0f);
        gGL.vertex3f(1.0f, 1.0f, 11.0f);
        gGL.vertex3f(1.0f, 1.0f, 11.0f);
        gGL.vertex3f(-1.0f, -1.0f, 11.0f);
        gGL.vertex3f(-1.0f, -1.0f, 11.0f);
        gGL.vertex3f(1.0f, -1.0f, 11.0f);
    gGL.end();
}


void draw_axes()
{
    LLGLSUIDefault gls_ui;
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    // A vertical white line at origin
    LLVector3 v = gAgent.getPositionAgent();
    gGL.begin(LLRender::LINES);
        gGL.color3f(1.0f, 1.0f, 1.0f);
        gGL.vertex3f(0.0f, 0.0f, 0.0f);
        gGL.vertex3f(0.0f, 0.0f, 40.0f);
    gGL.end();
    // Some coordinate axes
    gGL.pushMatrix();
        gGL.translatef( v.mV[VX], v.mV[VY], v.mV[VZ] );
        renderCoordinateAxes();
    gGL.popMatrix();
}

// <FS:PP> Render chat range spheres in 3D world
static void renderChatRangeSphere(const LLVector3& center, F32 radius, const LLColor4& color)
{
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();
    {
        gGL.translatef(center.mV[VX], center.mV[VY], center.mV[VZ]);
        gDebugProgram.bind();
        LLGLEnable blend(GL_BLEND);
        LLGLDepthTest depth(GL_TRUE, GL_TRUE);
        gGL.color4fv(color.mV);
        gGL.diffuseColor4fv(color.mV);
        gGL.pushMatrix();
        {
            gGL.scalef(radius, radius, radius);
            glCullFace(GL_FRONT);
            gSphere.render();
            glCullFace(GL_BACK);
            gSphere.render();
        }
        gGL.popMatrix();
        gUIProgram.bind();
    }
    gGL.popMatrix();
}

static void drawChatRangeSpheres()
{
    if (!isAgentAvatarValid())
    {
        return;
    }

    LFSimFeatureHandler& simfeatures = LFSimFeatureHandler::instance();
    F32 whisper_range = (F32)simfeatures.whisperRange();
    F32 say_range = (F32)simfeatures.sayRange();
    F32 shout_range = (F32)simfeatures.shoutRange();

    LLVector3 avatar_pos = gAgent.getPositionAgent();
    static LLUIColor whisper_color = LLUIColorTable::instance().getColor("MapWhisperRingColor", LLColor4::blue);
    static LLUIColor say_color = LLUIColorTable::instance().getColor("MapChatRingColor", LLColor4::yellow);
    static LLUIColor shout_color = LLUIColorTable::instance().getColor("MapShoutRingColor", LLColor4::red);
    renderChatRangeSphere(avatar_pos, whisper_range, whisper_color);
    renderChatRangeSphere(avatar_pos, say_range, say_color);
    renderChatRangeSphere(avatar_pos, shout_range, shout_color);
}
// </FS:PP>

void render_ui_3d()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    LLGLSPipeline gls_pipeline;

    //////////////////////////////////////
    //
    // Render 3D UI elements
    // NOTE: zbuffer is cleared before we get here by LLDrawPoolHUD,
    //       so 3d elements requiring Z buffer are moved to LLDrawPoolHUD
    //

    /////////////////////////////////////////////////////////////
    //
    // Render 2.5D elements (2D elements in the world)
    // Stuff without z writes
    //

    // Debugging stuff goes before the UI.

    stop_glerror();

    gUIProgram.bind();
    gGL.color4f(1.f, 1.f, 1.f, 1.f);

    // Coordinate axes
    static LLCachedControl<bool> show_axes(gSavedSettings, "ShowAxes");
    if (show_axes())
    {
        draw_axes();
    }

    // <FS:PP> Render chat range spheres in 3D world
    static LLCachedControl<bool> show_spheres(gSavedSettings, "FSShowChatRangeSpheres", false);
    if (show_spheres())
    {
        drawChatRangeSpheres();
    }
    // </FS:PP>

    gViewerWindow->renderSelections(false, false, true); // Non HUD call in render_hud_elements

    if (gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
    {
        // Render debugging beacons.
        gObjectList.renderObjectBeacons();
        gObjectList.resetObjectBeacons();
        gSky.addSunMoonBeacons();
    }
    else
    {
        // Make sure particle effects disappear
        LLHUDObject::renderAllForTimer();
    }

    stop_glerror();
}

void render_ui_2d()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    LLGLSUIDefault gls_ui;

    /////////////////////////////////////////////////////////////
    //
    // Render 2D UI elements that overlay the world (no z compare)

    //  Disable wireframe mode below here, as this is HUD/menus
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    //  Menu overlays, HUD, etc
    gViewerWindow->setup2DRender();

    // <FS:Ansariel> Factor out instance() call
    //F32 zoom_factor = LLViewerCamera::getInstance()->getZoomFactor();
    //S16 sub_region = LLViewerCamera::getInstance()->getZoomSubRegion();
    LLViewerCamera& camera = LLViewerCamera::instance();
    F32 zoom_factor = camera.getZoomFactor();
    S16 sub_region = camera.getZoomSubRegion();
    LLVector2& ui_scale_factor = LLUI::getScaleFactor();

    if (zoom_factor > 1.f)
    {
        //decompose subregion number to x and y values
        int pos_y = sub_region / llceil(zoom_factor);
        int pos_x = sub_region - (pos_y*llceil(zoom_factor));
        // offset for this tile
        LLFontGL::sCurOrigin.mX -= ll_round((F32)gViewerWindow->getWindowWidthScaled() * (F32)pos_x / zoom_factor);
        LLFontGL::sCurOrigin.mY -= ll_round((F32)gViewerWindow->getWindowHeightScaled() * (F32)pos_y / zoom_factor);
    }

    stop_glerror();

    // render outline for HUD
    if (isAgentAvatarValid() && gAgentCamera.mHUDCurZoom < 0.98f)
    {
        gUIProgram.bind();
        gGL.pushMatrix();
        S32 half_width = (gViewerWindow->getWorldViewWidthScaled() / 2);
        S32 half_height = (gViewerWindow->getWorldViewHeightScaled() / 2);
        gGL.scalef(LLUI::getScaleFactor().mV[VX], LLUI::getScaleFactor().mV[VY], 1.f);
        gGL.translatef((F32)half_width, (F32)half_height, 0.f);
        F32 zoom = gAgentCamera.mHUDCurZoom;
        gGL.scalef(zoom,zoom,1.f);
        gGL.color4fv(LLColor4::white.mV);
        gl_rect_2d(-half_width, half_height, half_width, -half_height, false);
        gGL.popMatrix();
        gUIProgram.unbind();
        stop_glerror();
    }


    if (LLPipeline::RenderUIBuffer)
    {
        if (LLView::sIsRectDirty)
        {
            LLView::sIsRectDirty = false;
            LLRect t_rect;

            gPipeline.mUIScreen.bindTarget();
            gGL.setColorMask(true, true);
            {
                constexpr S32 pad = 8;

                LLView::sDirtyRect.mLeft -= pad;
                LLView::sDirtyRect.mRight += pad;
                LLView::sDirtyRect.mBottom -= pad;
                LLView::sDirtyRect.mTop += pad;

                LLGLEnable scissor(GL_SCISSOR_TEST);
                static LLRect last_rect = LLView::sDirtyRect;

                //union with last rect to avoid mouse poop
                last_rect.unionWith(LLView::sDirtyRect);

                t_rect = LLView::sDirtyRect;
                LLView::sDirtyRect = last_rect;
                last_rect = t_rect;

                // <FS:Ansariel> Factor out instance() call
                //last_rect.mLeft = LLRect::tCoordType(last_rect.mLeft / LLUI::getScaleFactor().mV[0]);
                //last_rect.mRight = LLRect::tCoordType(last_rect.mRight / LLUI::getScaleFactor().mV[0]);
                //last_rect.mTop = LLRect::tCoordType(last_rect.mTop / LLUI::getScaleFactor().mV[1]);
                //last_rect.mBottom = LLRect::tCoordType(last_rect.mBottom / LLUI::getScaleFactor().mV[1]);
                last_rect.mLeft = LLRect::tCoordType(last_rect.mLeft / ui_scale_factor.mV[0]);
                last_rect.mRight = LLRect::tCoordType(last_rect.mRight / ui_scale_factor.mV[0]);
                last_rect.mTop = LLRect::tCoordType(last_rect.mTop / ui_scale_factor.mV[1]);
                last_rect.mBottom = LLRect::tCoordType(last_rect.mBottom / ui_scale_factor.mV[1]);

                LLRect clip_rect(last_rect);

                glClear(GL_COLOR_BUFFER_BIT);

                gViewerWindow->draw();
            }

            gPipeline.mUIScreen.flush();
            gGL.setColorMask(true, false);

            LLView::sDirtyRect = t_rect;
        }

        LLGLDisable cull(GL_CULL_FACE);
        LLGLDisable blend(GL_BLEND);
        S32 width = gViewerWindow->getWindowWidthScaled();
        S32 height = gViewerWindow->getWindowHeightScaled();
        gGL.getTexUnit(0)->bind(&gPipeline.mUIScreen);
        gGL.begin(LLRender::TRIANGLE_STRIP);
        gGL.color4f(1.f,1.f,1.f,1.f);
        gGL.texCoord2f(0.f, 0.f);                 gGL.vertex2i(0, 0);
        gGL.texCoord2f((F32)width, 0.f);          gGL.vertex2i(width, 0);
        gGL.texCoord2f(0.f, (F32)height);         gGL.vertex2i(0, height);
        gGL.texCoord2f((F32)width, (F32)height);  gGL.vertex2i(width, height);
        gGL.end();
    }
    else
    {
        gViewerWindow->draw();
    }

    // reset current origin for font rendering, in case of tiling render
    LLFontGL::sCurOrigin.set(0, 0);
}

void render_disconnected_background()
{
    gUIProgram.bind();

    gGL.color4f(1.f, 1.f, 1.f, 1.f);
    if (!gDisconnectedImagep && gDisconnected)
    {
        LL_INFOS() << "Loading last bitmap..." << LL_ENDL;

        std::string temp_str;
        temp_str = gDirUtilp->getLindenUserDir() + gDirUtilp->getDirDelimiter() + LLStartUp::getScreenLastFilename();

        LLPointer<LLImagePNG> image_png = new LLImagePNG;
        if( !image_png->load(temp_str) )
        {
            //LL_INFOS() << "Bitmap load failed" << LL_ENDL;
            return;
        }

        LLPointer<LLImageRaw> raw = new LLImageRaw;
        if (!image_png->decode(raw, 0.0f))
        {
            LL_INFOS() << "Bitmap decode failed" << LL_ENDL;
            gDisconnectedImagep = NULL;
            return;
        }

        U8 *rawp = raw->getData();
        S32 npixels = (S32)image_png->getWidth()*(S32)image_png->getHeight();
        for (S32 i = 0; i < npixels; i++)
        {
            S32 sum = 0;
            sum = *rawp + *(rawp+1) + *(rawp+2);
            sum /= 3;
            *rawp = ((S32)sum*6 + *rawp)/7;
            rawp++;
            *rawp = ((S32)sum*6 + *rawp)/7;
            rawp++;
            *rawp = ((S32)sum*6 + *rawp)/7;
            rawp++;
        }


        raw->expandToPowerOfTwo();
        gDisconnectedImagep = LLViewerTextureManager::getLocalTexture(raw.get(), false);
        gStartTexture = gDisconnectedImagep;
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    }

    // Make sure the progress view always fills the entire window.
    S32 width = gViewerWindow->getWindowWidthScaled();
    S32 height = gViewerWindow->getWindowHeightScaled();

    if (gDisconnectedImagep)
    {
        LLGLSUIDefault gls_ui;
        gViewerWindow->setup2DRender();
        gGL.pushMatrix();
        {
            // scale ui to reflect UIScaleFactor
            // this can't be done in setup2DRender because it requires a
            // pushMatrix/popMatrix pair
            const LLVector2& display_scale = gViewerWindow->getDisplayScale();
            gGL.scalef(display_scale.mV[VX], display_scale.mV[VY], 1.f);

            gGL.getTexUnit(0)->bind(gDisconnectedImagep);
            gGL.color4f(1.f, 1.f, 1.f, 1.f);
            gl_rect_2d_simple_tex(width, height);
            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        }
        gGL.popMatrix();
    }
    gGL.flush();

    gUIProgram.unbind();
}

void display_cleanup()
{
    gDisconnectedImagep = nullptr;
}

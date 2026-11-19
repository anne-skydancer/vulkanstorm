// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Viewer side of the OpenGL submission instrumentation. Compiled to nothing unless VKS_GL_STATS is set.

#include "llviewerprecompiledheaders.h"
#include "llglsubmitstatsapi.h"

#if VKS_GL_STATS

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <unordered_set>

#include "lleventapi.h"
#include "llfile.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llsd.h"
#include "llsdjson.h"
#include "llsdutil.h"
#include "llviewercontrol.h"
#include "llappviewer.h"
#include "lldir.h"
#include "lldate.h"
#include "lldrawable.h"
#include "lldrawpool.h"
#include "llface.h"
#include "llmaterial.h"
#include "llselectmgr.h"
#include "llgltfmaterial.h"
#include "llspatialpartition.h"
#include "lltextureentry.h"
#include "llvertexbuffer.h"
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llvovolume.h"
#include "llworld.h"
#include "pipeline.h"

extern bool gShaderProfileFrame; // llviewerdisplay.cpp
extern bool gCubeSnapshot;       // llviewerdisplay.cpp: a reflection probe face is being rendered

namespace
{
    // ---- the compare check -------------------------------------------------------------------------------

    S32 sArmed = LLGLSubmitCompare::PASS_OFF;   // the pass(es) to compare this frame
    S32 sArmFramesLeft = 0;                     // frames the arming stays open until its passes have run (compareArm "frames")
    S32 sPendingFrames = 1;                     // the "frames" of the next arming
    bool sCompareOwned = false;                 // an open frame owns the arming (ends it, or passes it on to the next frame)
    bool sInRun = false;                        // a compared pass is being rendered (no nesting)
    bool sReplay = false;                       // ... for the second time
    S32 sInject = 0;
    U64 sFramesSeen = 0;                        // display() and display_startup() frames, collected or not
    U64 sArmSerial = 0;                         // counts the armings, so a client can tell a fresh result from a stale one
    bool sPassDone[LLGLSubmitCompare::PASS_COUNT] = {}; // a pass is compared once per arming
    std::vector<F32> sFadeLog;                  // the light fades the first render used, in the order it asked
    size_t sFadeCursor = 0;

    struct Capture
    {
        std::string name;
        U32 texture = 0;
        S32 width = 0;
        S32 height = 0;
        U32 channels = 4;
        bool depth = false;
        bool f32 = false;
        std::vector<uint8_t> data;
        uint32_t distinct = 0;      // distinct pixel values in the first readback (counted up to kDistinctCap)
        uint64_t hash = 0;          // FNV-1a of the first readback
    };

    constexpr uint32_t kDistinctCap = 1024;

    struct AttachmentResult
    {
        std::string name;
        LLGLSubmitImage::Diff diff;
        bool injected = false;
        uint32_t distinct = 0;
        uint64_t hash = 0;
    };

    struct PassResult
    {
        S32 pass = 0;
        bool ran = false;
        std::string note;
        std::vector<AttachmentResult> attachments;
        bool identical() const
        {
            for (const auto& a : attachments)
            {
                if (!a.diff.identical())
                {
                    return false;
                }
            }
            return ran;
        }
    };

    std::vector<PassResult> sResults;
    U64 sResultsFrame = 0;

    const char* passLabel(S32 pass)
    {
        static const char* const labels[LLGLSubmitCompare::PASS_COUNT] = {
            "off", "G-buffer", "sun cascade 0", "sun cascade 1", "sun cascade 2", "sun cascade 3",
            "spot shadow 0", "spot shadow 1", "post-deferred", "UI (reserved)", "stochastic targets (reserved)",
            "projector tiles, static (reserved)", "projector tiles, avatars (reserved)", "probe face", "HUD", "impostor"};
        return pass >= 0 && pass < LLGLSubmitCompare::PASS_COUNT ? labels[pass] : "?";
    }

    // Each pass once per arming, never nested. The probe face compare is the only one a cube snapshot reaches: every
    // other pass is the main view's (a probe face reaches their sites with other targets).
    bool armedFor(S32 pass)
    {
        return sArmed != LLGLSubmitCompare::PASS_OFF && !sInRun && LLGLSubmitCompare::implemented(pass) && !sPassDone[pass] &&
               (sArmed == LLGLSubmitCompare::PASS_ALL || sArmed == pass) && (pass == LLGLSubmitCompare::PASS_PROBE_FACE) == gCubeSnapshot;
    }

    // Every pass the arming asked for has been compared (or reported as not run).
    bool armingDone()
    {
        if (sArmed != LLGLSubmitCompare::PASS_ALL)
        {
            return sArmed <= 0 || sArmed >= LLGLSubmitCompare::PASS_COUNT || sPassDone[sArmed];
        }
        for (S32 p = 1; p < LLGLSubmitCompare::PASS_COUNT; ++p)
        {
            if (LLGLSubmitCompare::implemented(p) && !sPassDone[p])
            {
                return false;
            }
        }
        return true;
    }

    // Errors that were pending before the check touched GL: reported once, not drained silently.
    void reportPendingGLErrors(const char* where)
    {
        static bool reported = false;
        GLenum e = glGetError();
        if (e == GL_NO_ERROR)
        {
            return;
        }
        if (!reported)
        {
            reported = true;
            LL_WARNS("GLSubmitCompare") << "GL error 0x" << std::hex << e << std::dec << " was pending before " << where
                                        << " (reported once; later ones are cleared silently)" << LL_ENDL;
        }
        while (glGetError() != GL_NO_ERROR) {}
    }

    void summarize(Capture& c)
    {
        const size_t px = (size_t)c.channels * (c.f32 ? 4 : 1);
        std::unordered_set<uint64_t> seen;
        uint64_t h = 1469598103934665603ull;
        for (uint8_t b : c.data)
        {
            h = (h ^ b) * 1099511628211ull;
        }
        if (px)
        {
            for (size_t off = 0; off + px <= c.data.size() && seen.size() < kDistinctCap; off += px)
            {
                uint64_t k = 1469598103934665603ull;
                for (size_t i = 0; i < px; ++i)
                {
                    k = (k ^ c.data[off + i]) * 1099511628211ull;
                }
                seen.insert(k);
            }
        }
        c.hash = h;
        c.distinct = (uint32_t)seen.size();
    }

    struct PixelStore
    {
        GLint pack = 4, unpack = 4;
        PixelStore()
        {
            glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        }
        ~PixelStore()
        {
            glPixelStorei(GL_PACK_ALIGNMENT, pack);
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
        }
    };

    // Texture unit 0 as the harness found it: the readback and the restore bind other textures there, and the
    // second render must start from the state the first one left.
    struct TexUnit0State
    {
        U32 unit = 0;
        U32 texture = 0;
        LLTexUnit::eTextureType type = LLTexUnit::TT_TEXTURE;
        bool mips = false;
        TexUnit0State()
        {
            unit = gGL.getCurrentTexUnitIndex();
            LLTexUnit* u = gGL.getTexUnit(0);
            texture = u->getCurrTexture();
            type = u->getCurrType();
            mips = u->getHasMipMaps();
        }
        void restore() const
        {
            LLTexUnit* u = gGL.getTexUnit(0);
            if (texture != 0)
            {
                u->bindManual(type, texture, mips);
            }
            else
            {
                u->unbind(LLTexUnit::TT_TEXTURE);
            }
            gGL.getTexUnit(unit)->activate();
        }
    };

    // Reads one 2D texture (level 0) as 8 bit RGBA, float RGBA or float depth: lossless for the formats the
    // renderer uses, so equal bits mean equal stored values.
    bool readTexture(Capture& c)
    {
        gGL.flush(); // anything batched must be drawn before the texture is read
        reportPendingGLErrors("a readback");
        const TexUnit0State saved;
        gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, c.texture);
        GLint w = 0, h = 0, rs = 0, gs = 0, bs = 0, as = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        if (!c.depth)
        {
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &rs);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &gs);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_BLUE_SIZE, &bs);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &as);
        }
        c.width = w;
        c.height = h;
        bool ok = w > 0 && h > 0 && glGetError() == GL_NO_ERROR;
        if (ok)
        {
            c.f32 = c.depth || rs > 8 || gs > 8 || bs > 8 || as > 8;
            c.channels = c.depth ? 1 : 4;
            c.data.assign((size_t)w * h * c.channels * (c.f32 ? 4 : 1), 0);
            glGetTexImage(GL_TEXTURE_2D, 0, c.depth ? GL_DEPTH_COMPONENT : GL_RGBA, c.f32 ? GL_FLOAT : GL_UNSIGNED_BYTE, c.data.data());
            ok = glGetError() == GL_NO_ERROR;
        }
        saved.restore();
        if (!ok)
        {
            c.data.clear();
        }
        else
        {
            summarize(c);
        }
        return ok;
    }

    bool writeTexture(const Capture& c)
    {
        gGL.flush();
        reportPendingGLErrors("a restore");
        const TexUnit0State saved;
        gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, c.texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, c.width, c.height, c.depth ? GL_DEPTH_COMPONENT : GL_RGBA,
                        c.f32 ? GL_FLOAT : GL_UNSIGNED_BYTE, c.data.data());
        const bool ok = glGetError() == GL_NO_ERROR;
        saved.restore();
        return ok;
    }

    struct Attach
    {
        std::string name;
        U32 texture;
        bool depth;
    };

    std::vector<Attach> attachmentsOf(LLRenderTarget& rt)
    {
        std::vector<Attach> out;
        for (U32 i = 0; i < rt.getNumTextures(); ++i)
        {
            out.push_back({"color" + std::to_string(i), rt.getTexture(i), false});
        }
        if (rt.getDepth())
        {
            out.push_back({"depth", rt.getDepth(), true});
        }
        return out;
    }

    Capture captureOf(const Attach& a, bool& ok)
    {
        Capture c;
        c.name = a.name;
        c.texture = a.texture;
        c.depth = a.depth;
        ok = readTexture(c);
        if (!ok)
        {
            LL_WARNS("GLSubmitCompare") << "attachment " << a.name << " could not be read back" << LL_ENDL;
        }
        return c;
    }

    // Every attachment of a target (for the first readback and the inputs to restore).
    std::vector<Capture> captureTarget(LLRenderTarget& rt)
    {
        std::vector<Capture> out;
        PixelStore store;
        for (const Attach& a : attachmentsOf(rt))
        {
            bool ok = false;
            Capture c = captureOf(a, ok);
            if (ok)
            {
                out.push_back(std::move(c));
            }
        }
        return out;
    }

    // The back buffer's contents as captures: colour (8 bit RGBA as bytes, wider formats as float RGBA) and depth (float),
    // read with glReadPixels from the default framebuffer, which must be bound for reading with GL_BACK as its read buffer.
    std::vector<Capture> captureBackBuffer(S32 width, S32 height, bool float_color, bool depth)
    {
        gGL.flush();
        reportPendingGLErrors("a back buffer readback");
        PixelStore store;
        std::vector<Capture> out;
        Capture color;
        color.name = "color";
        color.width = width;
        color.height = height;
        color.channels = 4;
        color.f32 = float_color;
        color.data.assign((size_t)width * height * 4 * (float_color ? 4 : 1), 0);
        glReadPixels(0, 0, width, height, GL_RGBA, float_color ? GL_FLOAT : GL_UNSIGNED_BYTE, color.data.data());
        if (glGetError() != GL_NO_ERROR)
        {
            return {};
        }
        summarize(color);
        out.push_back(std::move(color));
        if (depth)
        {
            Capture d;
            d.name = "depth";
            d.width = width;
            d.height = height;
            d.channels = 1;
            d.depth = true;
            d.f32 = true;
            d.data.assign((size_t)width * height * 4, 0);
            glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, d.data.data());
            if (glGetError() != GL_NO_ERROR)
            {
                return {};
            }
            summarize(d);
            out.push_back(std::move(d));
        }
        return out;
    }

    // A copy of the back buffer in textures of the same formats, made and put back by blits, so that the second render of
    // a pass drawn over the back buffer (the HUD) starts from what the first one found. Blits are exact between equal
    // formats; the scissor test, the only fragment operation that applies to them, is off around them.
    struct BackBufferCopy
    {
        GLuint fbo = 0;
        GLuint color = 0;
        GLuint depth = 0;
        S32 width = 0;
        S32 height = 0;
        GLbitfield mask = 0;
        bool floatColor = false;
        bool hasDepth = false;

        BackBufferCopy() = default;
        BackBufferCopy(const BackBufferCopy&) = delete;
        BackBufferCopy& operator=(const BackBufferCopy&) = delete;
        ~BackBufferCopy() { release(); }

        void release()
        {
            if (fbo)
            {
                glDeleteFramebuffers(1, &fbo);
            }
            GLuint tex[2] = {color, depth};
            GLsizei n = 0;
            for (GLuint name : tex)
            {
                if (name)
                {
                    tex[n++] = name;
                }
            }
            if (n)
            {
                glDeleteTextures(n, tex);
            }
            fbo = color = depth = 0;
        }

        static GLint attachmentParameter(GLenum attachment, GLenum pname)
        {
            GLint v = 0;
            glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, attachment, pname, &v);
            return v;
        }

        // Reads the back buffer's formats; with 'copy', makes the textures to hold its inputs. nullptr, or why not.
        const char* make(bool copy)
        {
            if (LLRenderTarget::sCurFBO != 0)
            {
                return "not run: a render target is bound, not the back buffer";
            }
            GLint read_fbo = 0, draw_fbo = 0, read_buffer = 0, draw_buffer = 0, pack_buffer = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
            glGetIntegerv(GL_READ_BUFFER, &read_buffer);
            glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
            if (read_fbo != 0 || draw_fbo != 0 || pack_buffer != 0 || (read_buffer != GL_BACK && read_buffer != GL_BACK_LEFT) ||
                (draw_buffer != GL_BACK && draw_buffer != GL_BACK_LEFT))
            {
                return "not run: the default framebuffer's back buffer is not the bound read and draw buffer";
            }
            width = gViewerWindow ? gViewerWindow->getWindowWidthRaw() : 0;
            height = gViewerWindow ? gViewerWindow->getWindowHeightRaw() : 0;
            if (width <= 0 || height <= 0)
            {
                return "not run: no window size";
            }
            const GLint r = attachmentParameter(GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE);
            const GLint g = attachmentParameter(GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE);
            const GLint b = attachmentParameter(GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE);
            const GLint a = attachmentParameter(GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE);
            const GLint type = attachmentParameter(GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE);
            const GLint encoding = attachmentParameter(GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING);
            GLint depth_bits = 0, depth_type = GL_UNSIGNED_NORMALIZED, stencil_bits = 0;
            if (attachmentParameter(GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) == GL_FRAMEBUFFER_DEFAULT)
            {
                depth_bits = attachmentParameter(GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE);
                depth_type = attachmentParameter(GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE);
            }
            if (attachmentParameter(GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) == GL_FRAMEBUFFER_DEFAULT)
            {
                stencil_bits = attachmentParameter(GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE);
            }
            if (glGetError() != GL_NO_ERROR)
            {
                return "not run: the back buffer's formats could not be read";
            }
            GLenum color_format = 0;
            if (r == 8 && g == 8 && b == 8 && (a == 0 || a == 8) && type == GL_UNSIGNED_NORMALIZED)
            {
                color_format = encoding == GL_SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            }
            else if (r == 10 && g == 10 && b == 10 && (a == 0 || a == 2) && type == GL_UNSIGNED_NORMALIZED)
            {
                color_format = GL_RGB10_A2;
                floatColor = true;
            }
            else if (r == 16 && g == 16 && b == 16 && type == GL_FLOAT)
            {
                color_format = GL_RGBA16F;
                floatColor = true;
            }
            GLenum depth_format = 0;
            if (depth_bits == 0 && stencil_bits == 0)
            {
                depth_format = GL_NONE;
            }
            else if (depth_bits == 24 && depth_type == GL_UNSIGNED_NORMALIZED)
            {
                depth_format = stencil_bits == 8 ? GL_DEPTH24_STENCIL8 : stencil_bits == 0 ? GL_DEPTH_COMPONENT24 : 0;
            }
            else if (depth_bits == 32 && depth_type == GL_FLOAT)
            {
                depth_format = stencil_bits == 8 ? GL_DEPTH32F_STENCIL8 : stencil_bits == 0 ? GL_DEPTH_COMPONENT32F : 0;
            }
            else if (depth_bits == 16 && stencil_bits == 0)
            {
                depth_format = GL_DEPTH_COMPONENT16;
            }
            if (!color_format || (depth_format == 0 && (depth_bits || stencil_bits)))
            {
                return "not run: the back buffer's format is not one the check copies";
            }
            hasDepth = depth_bits > 0;
            mask = GL_COLOR_BUFFER_BIT | (depth_bits ? GL_DEPTH_BUFFER_BIT : 0) | (stencil_bits ? GL_STENCIL_BUFFER_BIT : 0);
            if (!copy)
            {
                return nullptr;
            }
            glCreateTextures(GL_TEXTURE_2D, 1, &color);
            glTextureStorage2D(color, 1, color_format, width, height);
            glCreateFramebuffers(1, &fbo);
            glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color, 0);
            if (depth_format != GL_NONE)
            {
                glCreateTextures(GL_TEXTURE_2D, 1, &depth);
                glTextureStorage2D(depth, 1, depth_format, width, height);
                const GLenum attachment = stencil_bits ? (depth_bits ? GL_DEPTH_STENCIL_ATTACHMENT : GL_STENCIL_ATTACHMENT) : GL_DEPTH_ATTACHMENT;
                glNamedFramebufferTexture(fbo, attachment, depth, 0);
            }
            glNamedFramebufferDrawBuffer(fbo, GL_COLOR_ATTACHMENT0);
            glNamedFramebufferReadBuffer(fbo, GL_COLOR_ATTACHMENT0);
            if (glGetError() != GL_NO_ERROR || glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                release();
                return "not run: the copy of the back buffer could not be made";
            }
            return nullptr;
        }

        bool blit(GLuint from, GLuint to)
        {
            gGL.flush();
            reportPendingGLErrors("a back buffer copy");
            const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
            if (scissor)
            {
                glDisable(GL_SCISSOR_TEST);
            }
            glBlitNamedFramebuffer(from, to, 0, 0, width, height, 0, 0, width, height, mask, GL_NEAREST);
            const bool ok = glGetError() == GL_NO_ERROR;
            if (scissor)
            {
                glEnable(GL_SCISSOR_TEST);
            }
            return ok;
        }

        bool save() { return fbo && blit(0, fbo); }
        bool restore() { return fbo && blit(fbo, 0); }
    };

    void flipOneBit(Capture& c)
    {
        // the first channel of the centre pixel, lowest bit: one unit in the last place
        if (c.data.empty())
        {
            return;
        }
        const size_t pixel = ((size_t)(c.height / 2) * c.width + (c.width / 2)) * c.channels;
        const size_t off = pixel * (c.f32 ? 4 : 1);
        c.data[off] ^= 1;
    }

    const char* kTag = "GLSubmitCompare";
}

struct LLGLSubmitCompare::Run::Impl
{
    S32 pass = 0;
    LLRenderTarget* target = nullptr;   // null: the back buffer
    bool restore = false;
    bool restoreFailed = false; // the second render did not start from the first one's inputs: no valid comparison
    std::vector<Capture> inputs;
    std::vector<Capture> first;
    BackBufferCopy back;        // the back buffer: its formats, and its inputs with 'restore'
};

namespace
{
    void reportNotRun(S32 pass, const std::string& note)
    {
        PassResult skipped;
        skipped.pass = pass;
        skipped.note = note;
        LL_INFOS(kTag) << passLabel(pass) << ": " << skipped.note << LL_ENDL;
        sResults.push_back(std::move(skipped));
    }
}

bool LLGLSubmitCompare::Run::arm(S32 pass, const char* not_run)
{
    if (!armedFor(pass))
    {
        return false;
    }
    sPassDone[pass] = true;
    if (not_run)
    {
        reportNotRun(pass, not_run);
        return false;
    }
    return true;
}

LLGLSubmitCompare::Run::Run(S32 pass, LLRenderTarget* target, bool restore_inputs, const char* not_run)
{
    if (!arm(pass, not_run))
    {
        return;
    }
    if (!target || target->getUsage() != LLTexUnit::TT_TEXTURE)
    {
        reportNotRun(pass, "not run: no plain 2D target this frame (missing, multisampled, or stochastic transparency in use)");
        return;
    }
    mImpl = std::make_unique<Impl>();
    mImpl->pass = pass;
    mImpl->target = target;
    mImpl->restore = restore_inputs;
    sInRun = true;
    sFadeLog.clear();
    sFadeCursor = 0;
}

LLGLSubmitCompare::Run::Run(S32 pass, BackBuffer, bool restore_inputs, const char* not_run)
{
    if (!arm(pass, not_run))
    {
        return;
    }
    auto impl = std::make_unique<Impl>();
    if (const char* why = impl->back.make(restore_inputs))
    {
        reportNotRun(pass, why);
        return;
    }
    if (restore_inputs && !impl->back.save())
    {
        // rendered once, as normally: a second render over the first would not be the same render
        reportNotRun(pass, "not run: the back buffer could not be copied (its depth or stencil format refuses the blit)");
        return;
    }
    mImpl = std::move(impl);
    mImpl->pass = pass;
    mImpl->restore = restore_inputs;
    sInRun = true;
    sFadeLog.clear();
    sFadeCursor = 0;
}

LLGLSubmitCompare::Run::~Run()
{
    if (mImpl)
    {
        sInRun = false;
        sReplay = false;
        sFadeLog.clear();
    }
}

bool LLGLSubmitCompare::Run::next()
{
    if (!mImpl)
    {
        return mStep++ == 0;
    }
    Impl& m = *mImpl;
    switch (mStep++)
    {
    case 0:
        if (m.restore && m.target)
        {
            m.inputs = captureTarget(*m.target);
        }
        return true;
    case 1:
        m.first = m.target ? captureTarget(*m.target) : captureBackBuffer(m.back.width, m.back.height, m.back.floatColor, m.back.hasDepth);
        sReplay = true;
        sFadeCursor = 0;
        if (m.restore && m.target)
        {
            for (const Capture& c : m.inputs)
            {
                if (!writeTexture(c))
                {
                    LL_WARNS(kTag) << "could not restore " << c.name << " for the second render" << LL_ENDL;
                    m.restoreFailed = true;
                }
            }
            std::vector<Capture>().swap(m.inputs);
        }
        else if (m.restore && !m.back.restore())
        {
            LL_WARNS(kTag) << "could not restore the back buffer for the second render" << LL_ENDL;
            m.restoreFailed = true;
        }
        return true;
    default:
        break;
    }

    // After the second render. The second readback of a target is compared one attachment at a time and dropped, so the
    // peak is the first readback of the whole target plus one attachment.
    sReplay = false;
    PassResult res;
    res.pass = m.pass;
    res.ran = true;
    PixelStore store;
    const std::vector<Attach> list = m.target ? attachmentsOf(*m.target) : std::vector<Attach>();
    std::vector<Capture> back_second;
    if (!m.target && !m.restoreFailed && !m.first.empty())
    {
        back_second = captureBackBuffer(m.back.width, m.back.height, m.back.floatColor, m.back.hasDepth);
    }
    const size_t count = m.target ? list.size() : back_second.size();
    m.back.release();
    if (m.restoreFailed)
    {
        res.ran = false;
        res.note = "the inputs could not be restored for the second render";
    }
    else if (count != m.first.size() || m.first.empty())
    {
        res.ran = false;
        res.note = "attachments could not be read back";
    }
    else
    {
        for (size_t i = 0; i < count; ++i)
        {
            bool ok = true;
            Capture second = m.target ? captureOf(list[i], ok) : std::move(back_second[i]);
            const Capture& first = m.first[i];
            if (!ok || second.width != first.width || second.height != first.height || second.channels != first.channels)
            {
                res.ran = false;
                res.note = "an attachment could not be read back for the comparison";
                res.attachments.clear();
                break;
            }
            AttachmentResult ar;
            ar.name = second.name;
            ar.distinct = first.distinct;
            ar.hash = first.hash;
            if (sInject && i == 0)
            {
                flipOneBit(second);
                ar.injected = true;
            }
            ar.diff = LLGLSubmitImage::compare(first.data.data(), second.data.data(), (uint32_t)second.width,
                                               (uint32_t)second.height, second.channels, second.f32);
            res.attachments.push_back(std::move(ar));
        }
    }
    LL_INFOS(kTag) << passLabel(m.pass) << ": " << (res.ran ? (res.identical() ? "IDENTICAL" : "DIFFERENT") : res.note) << LL_ENDL;
    for (const auto& a : res.attachments)
    {
        LL_INFOS(kTag) << "  " << a.name << ": " << a.diff.mismatchedPixels << " of " << a.diff.pixels << " pixels differ"
                       << (a.diff.identical() ? "" : " (max " + std::to_string(a.diff.maxDiff[0]) + "/" + std::to_string(a.diff.maxDiff[1]) +
                                                   "/" + std::to_string(a.diff.maxDiff[2]) + "/" + std::to_string(a.diff.maxDiff[3]) +
                                                   ", first at " + std::to_string(a.diff.firstX) + "," + std::to_string(a.diff.firstY) + ")")
                       << " (content: " << (a.distinct >= kDistinctCap ? ">=" : "") << a.distinct << " distinct values)"
                       << (a.injected ? " [one bit injected into the second readback]" : "") << LL_ENDL;
    }
    sResults.push_back(std::move(res));
    sInRun = false;
    sFadeLog.clear();
    mImpl.reset();
    return false;
}

bool LLGLSubmitCompare::implemented(S32 pass)
{
    return (pass >= PASS_GBUFFER && pass <= PASS_POST_DEFERRED) || (pass >= PASS_PROBE_FACE && pass <= PASS_IMPOSTOR);
}

const char* LLGLSubmitCompare::tailNotRepeatable()
{
    if (!gPipeline.mHighlightFaces.empty())
    {
        return "not run: highlight faces pending (the pass's tail consumes that list)";
    }
    // the tail draws the selected faces (LLPipeline::mSelectedFaces), gathered from the selection in face mode
    if (LLSelectMgr::getInstance()->getTEMode() && !LLSelectMgr::getInstance()->getSelection()->isEmpty())
    {
        return "not run: faces are selected (the pass's tail rebuilds and redraws them)";
    }
    if (gPipeline.hasRenderDebugMask(~0ULL))
    {
        return "not run: a debug display is on (the pass's tail draws it)";
    }
    if (gShowObjectUpdates)
    {
        return "not run: object update blips are on (they age in the pass's tail)";
    }
    return nullptr;
}

F32 LLGLSubmitCompare::lightFade(F32 fade, bool& advance)
{
    advance = true;
    if (!sInRun)
    {
        return fade;
    }
    if (!sReplay)
    {
        sFadeLog.push_back(fade);
        return fade;
    }
    advance = false;
    return sFadeCursor < sFadeLog.size() ? sFadeLog[sFadeCursor++] : fade;
}

bool LLGLSubmitCompare::replaying()
{
    return sReplay;
}

S32 LLGLSubmitCompare::activePath()
{
    if (sInRun && !sReplay)
    {
        return 0;
    }
    return gSavedSettings.getS32("RenderGLSubmitPath");
}

// ---- the frame glue, the Frame Profile section and the LEAP API ---------------------------------------------

namespace
{
    boost::json::array resultsToJson()
    {
        boost::json::array arr;
        for (const PassResult& r : sResults)
        {
            boost::json::object o;
            o.emplace("pass", (int64_t)r.pass);
            o.emplace("label", passLabel(r.pass));
            o.emplace("ran", r.ran);
            o.emplace("identical", r.identical());
            if (!r.note.empty())
            {
                o.emplace("note", r.note);
            }
            boost::json::array atts;
            for (const AttachmentResult& a : r.attachments)
            {
                boost::json::object ao;
                ao.emplace("name", a.name);
                ao.emplace("pixels", (uint64_t)a.diff.pixels);
                ao.emplace("mismatched_pixels", (uint64_t)a.diff.mismatchedPixels);
                ao.emplace("distinct_values", (uint64_t)a.distinct);   // capped at 1024; 1 = the pass left the target empty
                ao.emplace("content_hash", std::to_string(a.hash));
                if (!a.diff.identical())
                {
                    ao.emplace("max_diff", boost::json::array{a.diff.maxDiff[0], a.diff.maxDiff[1], a.diff.maxDiff[2], a.diff.maxDiff[3]});
                    ao.emplace("first_x", a.diff.firstX);
                    ao.emplace("first_y", a.diff.firstY);
                }
                if (a.injected)
                {
                    ao.emplace("injected", true);
                }
                atts.emplace_back(std::move(ao));
            }
            o.emplace("attachments", std::move(atts));
            arr.emplace_back(std::move(o));
        }
        return arr;
    }

    // only the headline numbers: the LEAP reply stays small (see scripts/inworld/README.md, "Big replies")
    LLSD headline(const boost::json::object& summary)
    {
        LLSD out = LLSD::emptyMap();
        static const char* const keys[] = {"draws", "drawTriangles", "vbChanges", "attribRespecs", "iboBinds", "shaderBinds",
                                           "texBindFast", "texBindFastRedundant", "texGLBind", "uniformGL", "uniformBytes",
                                           "paletteUploads", "paletteDistinct", "mdvSyncs", "bufSubData", "flushBuffers",
                                           "stateToggles", "occBlocking", "runs", "mergeableDraws", "cpu_ms", "gpu_ms"};
        if (const boost::json::value* totals = summary.if_contains("totals"))
        {
            const boost::json::object& t = totals->as_object();
            LLSD tl = LLSD::emptyMap();
            for (const char* k : keys)
            {
                if (const boost::json::value* v = t.if_contains(k))
                {
                    tl[k] = v->is_int64() ? LLSD((S32)v->as_int64()) : LLSD(v->to_number<double>());
                }
            }
            out["totals"] = tl;
        }
        if (const boost::json::value* views = summary.if_contains("views"))
        {
            LLSD vl = LLSD::emptyArray();
            for (const auto& v : views->as_array())
            {
                const boost::json::object& vo = v.as_object();
                LLSD e = LLSD::emptyMap();
                e["view"] = std::string(vo.at("view").as_string());
                for (const char* k : {"draws", "cpu_ms", "gpu_ms"})
                {
                    if (const boost::json::value* x = vo.if_contains(k))
                    {
                        e[k] = x->is_int64() ? LLSD((S32)x->as_int64()) : LLSD(x->to_number<double>());
                    }
                }
                vl.append(e);
            }
            out["views"] = vl;
        }
        return out;
    }

    // A LEAP client may only write .json files under the viewer's log folder or a ".captures" folder (where the scripts put
    // their run folders): no relative paths, no "..", no UNC paths, so it cannot truncate arbitrary files.
    bool dumpPathAllowed(const std::string& path, std::string& why)
    {
        std::string p = path;
        for (char& ch : p)
        {
            if (ch == '\\')
            {
                ch = '/';
            }
        }
        if (p.rfind("//", 0) == 0)
        {
            why = "UNC and device paths are not allowed";
            return false;
        }
        const bool absolute = (p.size() > 2 && p[1] == ':' && p[2] == '/') || (!p.empty() && p[0] == '/');
        if (!absolute)
        {
            why = "the path must be absolute";
            return false;
        }
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= p.size())
        {
            size_t end = p.find('/', start);
            if (end == std::string::npos)
            {
                end = p.size();
            }
            parts.push_back(p.substr(start, end - start));
            start = end + 1;
        }
        bool captures = false;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i] == "..")
            {
                why = "\"..\" is not allowed";
                return false;
            }
            captures = captures || (parts[i] == ".captures" && i + 1 < parts.size());
        }
        std::string lower = p;
        for (char& ch : lower)
        {
            ch = (char)std::tolower((unsigned char)ch);
        }
        if (lower.size() < 6 || lower.compare(lower.size() - 5, 5, ".json") != 0)
        {
            why = "only .json files are written";
            return false;
        }
        std::string logs = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "");
        for (char& ch : logs)
        {
            ch = ch == '\\' ? '/' : (char)std::tolower((unsigned char)ch);
        }
        if (!logs.empty() && logs.back() != '/')
        {
            logs += '/';
        }
        if (captures || (!logs.empty() && lower.compare(0, logs.size(), logs) == 0))
        {
            return true;
        }
        why = "only the viewer log folder or a .captures folder";
        return false;
    }

    std::string defaultDumpPath(const std::string& kind)
    {
        std::string stamp = LLDate::now().toHTTPDateString("%Y%m%d-%H%M%S");
        return gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "gl_submit_" + kind + "_" + stamp + ".json");
    }

    void api_status(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        r["compiled"] = true;
        r["collecting_setting"] = gSavedSettings.getBOOL("RenderGLSubmitStats");
        r["window_target"] = (S32)LLGLSubmitStats::windowTarget();
        r["window_collected"] = (S32)LLGLSubmitStats::windowCollected();
        r["window_gpu_frames"] = (S32)LLGLSubmitStats::windowGpuResolved();
        r["window_active"] = LLGLSubmitStats::windowActive();
        r["window_census"] = LLGLSubmitStats::windowCensus();
        r["compare_armed"] = (S32)sArmed;
        r["compare_results"] = (S32)sResults.size();
        r["arm_serial"] = (F64)sArmSerial;
        r["frame_count"] = (F64)sFramesSeen;
        if (auto f = LLGLSubmitStats::lastFrame())
        {
            r["last_frame"] = (F64)f->number;
            r["last_frame_gpu_resolved"] = f->gpuResolved;
        }
    }

    void api_window_begin(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const S32 frames = req.has("frames") ? req["frames"].asInteger() : 60;
        if (frames < 1 || frames > 100000)
        {
            return r.error("frames must be 1..100000");
        }
        const bool census = req.has("census") && req["census"].asBoolean();
        LLGLSubmitStats::windowBegin((uint32_t)frames, req.has("gpu") ? req["gpu"].asBoolean() : true, census);
        r["frames"] = frames;
        r["census"] = census;
    }

    void api_window_stop(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        LLGLSubmitStats::windowStop();
        r["collected"] = (S32)LLGLSubmitStats::windowCollected();
    }

    void api_summary(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const bool window = !req.has("kind") || req["kind"].asString() == "window";
        if (window)
        {
            if (!LLGLSubmitStats::windowCollected())
            {
                return r.error("no window collected: call windowBegin and wait for window_active to turn false");
            }
            r["summary"] = headline(LLGLSubmitStats::summaryOfWindow());
            r["frames"] = (S32)LLGLSubmitStats::windowCollected();
            r["gpu_frames"] = (S32)LLGLSubmitStats::windowGpuResolved();
        }
        else
        {
            auto f = LLGLSubmitStats::lastFrame();
            if (!f)
            {
                return r.error("no frame collected yet: set RenderGLSubmitStats or call windowBegin");
            }
            r["summary"] = headline(LLGLSubmitStats::summaryOfFrame(*f));
        }
    }

    void api_dump(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const bool window = !req.has("kind") || req["kind"].asString() == "window";
        boost::json::object doc;
        if (window)
        {
            if (!LLGLSubmitStats::windowCollected())
            {
                return r.error("no window collected");
            }
            doc = LLGLSubmitStats::windowToJson();
        }
        else
        {
            auto f = LLGLSubmitStats::lastFrame();
            if (!f)
            {
                return r.error("no frame collected yet");
            }
            doc = LLGLSubmitStats::frameToJson(*f);
        }
        doc.emplace("compare", resultsToJson());
        const std::string path = req.has("path") ? req["path"].asString() : defaultDumpPath(window ? "window" : "frame");
        std::string why;
        if (!dumpPathAllowed(path, why))
        {
            return r.error("refused to write " + path + ": " + why);
        }
        llofstream out(path, std::ios::binary);
        if (!out)
        {
            return r.error("cannot write " + path);
        }
        out << doc;
        out.close();
        r["path"] = path;
    }

    void api_compare_arm(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        const S32 pass = req.has("pass") ? req["pass"].asInteger() : LLGLSubmitCompare::PASS_ALL;
        if (pass != LLGLSubmitCompare::PASS_ALL && !LLGLSubmitCompare::implemented(pass))
        {
            return r.error("pass must be -1 (all), 1..8 or 13..15");
        }
        const S32 frames = req.has("frames") ? req["frames"].asInteger() : 1;
        if (frames < 1 || frames > 600)
        {
            return r.error("frames must be 1..600");
        }
        if (req.has("inject"))
        {
            gSavedSettings.setS32("RenderGLSubmitCompareInject", req["inject"].asInteger());
        }
        sPendingFrames = frames;
        gSavedSettings.setS32("RenderGLSubmitCompare", pass);
        r["armed"] = pass;
        r["frames"] = frames;
    }

    void api_compare_results(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        r["armed"] = (S32)sArmed;
        r["pending"] = gSavedSettings.getS32("RenderGLSubmitCompare");
        r["frame"] = (F64)sResultsFrame;
        r["arm_serial"] = (F64)sArmSerial;
        r["results"] = LlsdFromJson(resultsToJson());
    }

    // The cost of the hooks in this very binary, measured in place: the mix of hooks one draw runs (about 20 macro
    // sites), with the statistics not collecting (the cost every dev build pays: a pointer test per site), collecting,
    // and the cost of one pass scope with and without GPU timers.
    void api_self_bench(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        if (LLGLSubmitStats::windowActive() || LLGLSubmitStats::collecting() || gSavedSettings.getBOOL("RenderGLSubmitStats"))
        {
            return r.error("stop the collection first (the benchmark uses the frame machinery)");
        }
        using Clock = std::chrono::steady_clock;
        const S32 n = llclamp(req.has("iterations") ? req["iterations"].asInteger() : 1000000, 1000, 100000000);
        constexpr int kHooksPerDraw = 20;
        // The barrier after each hook makes the compiler reload the collection pointer at every site, as it must in the
        // renderer where GL calls sit between the hooks (without it a loop of hooks collapses to one test).
        auto draw_mix = [](S32 count)
        {
#define VKS_BENCH_HOOK(hook) do { hook; std::atomic_signal_fence(std::memory_order_seq_cst); } while (0)
            for (S32 i = 0; i < count; ++i)
            {
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(vbSetBuffer));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(vbChanges));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(attribRespecs));
                VKS_BENCH_HOOK(VKS_GLSTAT(attribPointers, 5));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(iboBinds));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(modelMatrixCalls));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(modelMatrixChanges));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(texBindFast));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(texGLBind));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(texGLActive));
                VKS_BENCH_HOOK(VKS_GLSTAT_STATE_CHANGE());
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(matrixSyncs));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(mdvSyncs));
                VKS_BENCH_HOOK(VKS_GLSTAT_UNIFORM(64, 1, true));
                VKS_BENCH_HOOK(VKS_GLSTAT_UNIFORM(36, 1, true));
                VKS_BENCH_HOOK(VKS_GLSTAT_UNIFORM(64, 1, true));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(uniformRequests));
                VKS_BENCH_HOOK(VKS_GLSTAT_INC(uniformRequests));
                VKS_BENCH_HOOK(VKS_GLSTAT_DRAW(300));
                VKS_BENCH_HOOK(VKS_GLSTAT(drawTriangles, 100));
            }
#undef VKS_BENCH_HOOK
        };
        auto time_ns = [&](auto&& body)
        {
            const auto t0 = Clock::now();
            body();
            return std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
        };

        draw_mix(n / 10); // warm up
        const double off_ns = time_ns([&] { draw_mix(n); });
        const auto saved_last = LLGLSubmitStats::lastFrame(); // the benchmark's frames must not replace the last real one
        LLGLSubmitStats::beginFrame(true, false);
        const double on_ns = time_ns([&] { draw_mix(n); });
        LLGLSubmitStats::endFrame();

        const S32 scopes = 2000;
        LLGLSubmitStats::beginFrame(true, false);
        const double scope_cpu_ns = time_ns([&] { for (S32 i = 0; i < scopes; ++i) { VKS_GLSTAT_PASS("bench pass", i % 8); } });
        LLGLSubmitStats::endFrame();
        LLGLSubmitStats::beginFrame(true, true);
        const double scope_gpu_ns = time_ns([&] { for (S32 i = 0; i < scopes; ++i) { VKS_GLSTAT_PASS("bench pass", i % 8); } });
        LLGLSubmitStats::endFrame();
        LLGLSubmitStats::resolveGpu(true);
        LLGLSubmitStats::restoreLast(saved_last);

        r["iterations"] = n;
        r["hooks_per_draw"] = kHooksPerDraw;
        r["ns_per_draw_not_collecting"] = off_ns / n;
        r["ns_per_draw_collecting"] = on_ns / n;
        r["ns_per_hook_not_collecting"] = off_ns / n / kHooksPerDraw;
        r["ns_per_hook_collecting"] = on_ns / n / kHooksPerDraw;
        r["ns_per_scope_cpu_only"] = scope_cpu_ns / scopes;
        r["ns_per_scope_with_gpu_timers"] = scope_gpu_ns / scopes;
        LL_INFOS(kTag) << "hook cost: " << off_ns / n << " ns per draw's hooks not collecting, " << on_ns / n << " collecting; scope "
                       << scope_cpu_ns / scopes << " ns, with GPU timers " << scope_gpu_ns / scopes << " ns" << LL_ENDL;
    }

    // ---- the resident copy census: every face of every draw item the scene holds now ----

    struct ResidentFace
    {
        uint64_t key = 0;
        uint32_t vertices = 0;
        uint32_t bytes = 0;
        uint32_t bakeNs = 0;
        uint8_t exclusion = LLGLSubmitMath::CENSUS_ELIGIBLE;
        bool blended = false;
    };

    // Collects each face once: a face is in a draw item per pass it is drawn in, but its vertices are in one buffer. It
    // can become a copy only if every pass it is in allows it, so the first reason seen in any of them is kept.
    class CensusCollector : public OctreeTraveler
    {
    public:
        std::unordered_map<uint64_t, ResidentFace> mFaces;
        uint64_t mItems = 0;
        uint64_t mItemsWithoutRecord = 0;

        void visit(const OctreeNode* node) override
        {
            LLSpatialGroup* group = (LLSpatialGroup*)node->getListener(0);
            if (!group)
            {
                return;
            }
            for (const auto& type : group->mDrawMap)
            {
                for (const LLPointer<LLDrawInfo>& info : type.second)
                {
                    if (info.isNull())
                    {
                        continue;
                    }
                    ++mItems;
                    if (info->mGLSubmitCensus.empty())
                    {
                        ++mItemsWithoutRecord;
                    }
                    for (const LLGLSubmitMath::CensusFace& f : info->mGLSubmitCensus)
                    {
                        auto it = mFaces.find(f.face);
                        if (it == mFaces.end())
                        {
                            ResidentFace& r = mFaces[f.face];
                            r.key = f.key;
                            r.vertices = f.vertices;
                            r.bytes = f.bytes;
                            r.bakeNs = f.bakeNs;
                            r.exclusion = f.exclusion;
                            r.blended = f.blended;
                        }
                        else
                        {
                            if (it->second.exclusion == LLGLSubmitMath::CENSUS_ELIGIBLE)
                            {
                                it->second.exclusion = f.exclusion;
                            }
                            it->second.blended = it->second.blended || f.blended;
                        }
                    }
                }
            }
            for (LLSpatialBridge* bridge : group->mBridgeList)
            {
                traverse(bridge->mOctree);
            }
        }
    };

    boost::json::object residentCensus(const CensusCollector& c)
    {
        using namespace LLGLSubmitMath;
        constexpr int kSizes = kCensusSizeClasses;
        static const char* const size_names[kSizes] = {"under_64", "64_255", "256_575", "576_up"};
        struct Key { uint32_t copies = 0; uint32_t vertices = 0; uint32_t bytes = 0; uint64_t bakeNs = 0; };
        std::unordered_map<uint64_t, Key> keys, rigged_keys;
        uint64_t faces = 0, vertices = 0, bytes = 0, bake_ns = 0;
        uint64_t reason_faces[CENSUS_EXCLUSIONS] = {}, reason_vertices[CENSUS_EXCLUSIONS] = {}, reason_bytes[CENSUS_EXCLUSIONS] = {};
        uint64_t rigged_faces = 0, rigged_bytes = 0;
        for (const auto& entry : c.mFaces)
        {
            const ResidentFace& f = entry.second;
            const uint8_t reason = f.exclusion < CENSUS_EXCLUSIONS ? f.exclusion : (uint8_t)CENSUS_NO_KEY;
            ++faces;
            vertices += f.vertices;
            bytes += f.bytes;
            bake_ns += f.bakeNs;
            reason_faces[reason]++;
            reason_vertices[reason] += f.vertices;
            reason_bytes[reason] += f.bytes;
            if (reason == CENSUS_ELIGIBLE)
            {
                Key& k = keys[f.key];
                k.copies++;
                k.vertices = f.vertices;
                k.bytes = f.bytes;
                k.bakeNs += f.bakeNs;
            }
            else if (reason == CENSUS_RIGGED && !f.blended && f.key)
            {
                // rigged buffer sharing (G5b-1): a rigged, non-blended face's bake depends on the volume face, the skin
                // and the texture entry only, so equal keys could share one buffer between avatars
                ++rigged_faces;
                rigged_bytes += f.bytes;
                Key& k = rigged_keys[f.key];
                k.copies++;
                k.bytes = f.bytes;
            }
        }

        boost::json::object o;
        o.emplace("schema", 1);
        o.emplace("kind", "resident_census");
        o.emplace("items", c.mItems);
        o.emplace("items_without_record", c.mItemsWithoutRecord);
        o.emplace("faces", faces);
        o.emplace("vertices", vertices);
        o.emplace("bytes", bytes);
        o.emplace("bake_ms", (double)bake_ns / 1.0e6);
        boost::json::object reasons;
        for (int r = 0; r < CENSUS_EXCLUSIONS; ++r)
        {
            if (reason_faces[r])
            {
                boost::json::object e;
                e.emplace("faces", reason_faces[r]);
                e.emplace("vertices", reason_vertices[r]);
                e.emplace("bytes", reason_bytes[r]);
                reasons.emplace(censusExclusionName(r), std::move(e));
            }
        }
        o.emplace("by_reason", std::move(reasons));

        // eligible faces by size class: baked bytes (one bake per copy) against unique bytes (one per key), and the bake
        // time of the copies past the first of each key (what a cache hit skips)
        uint64_t s_faces[kSizes] = {}, s_vertices[kSizes] = {}, s_keys[kSizes] = {}, s_baked[kSizes] = {}, s_unique[kSizes] = {};
        uint64_t s_bake_ns[kSizes] = {}, s_repeat_bake_ns[kSizes] = {}, s_copies_hist[kSizes][kCensusRunBuckets] = {};
        for (const auto& entry : keys)
        {
            const Key& k = entry.second;
            const int s = censusSizeClass(k.vertices);
            s_faces[s] += k.copies;
            s_vertices[s] += (uint64_t)k.vertices * k.copies;
            s_keys[s]++;
            s_baked[s] += (uint64_t)k.bytes * k.copies;
            s_unique[s] += k.bytes;
            s_bake_ns[s] += k.bakeNs;
            s_repeat_bake_ns[s] += k.copies > 1 ? k.bakeNs - k.bakeNs / k.copies : 0;
            s_copies_hist[s][censusRunBucket(k.copies)]++;
        }
        boost::json::object sizes;
        uint64_t inst_baked = 0, inst_unique = 0;
        for (int s = 0; s < kSizes; ++s)
        {
            boost::json::object e;
            e.emplace("faces", s_faces[s]);
            e.emplace("vertices", s_vertices[s]);
            e.emplace("keys", s_keys[s]);
            e.emplace("baked_bytes", s_baked[s]);
            e.emplace("unique_bytes", s_unique[s]);
            e.emplace("bake_ms", (double)s_bake_ns[s] / 1.0e6);
            e.emplace("repeat_bake_ms", (double)s_repeat_bake_ns[s] / 1.0e6);
            boost::json::array hist;
            for (uint64_t v : s_copies_hist[s])
            {
                hist.emplace_back(v);
            }
            e.emplace("keys_by_copies", std::move(hist)); // 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65+
            sizes.emplace(size_names[s], std::move(e));
            if (s >= censusSizeClass(kCensusMinVertices))
            {
                inst_baked += s_baked[s];
                inst_unique += s_unique[s];
            }
        }
        o.emplace("eligible_by_size", std::move(sizes));
        boost::json::object inst;
        inst.emplace("min_vertices", kCensusMinVertices);
        inst.emplace("baked_bytes", inst_baked);
        inst.emplace("unique_bytes", inst_unique);
        inst.emplace("ratio", inst_unique ? (double)inst_baked / (double)inst_unique : 0.0);
        o.emplace("instancing", std::move(inst));
        uint64_t rigged_unique = 0;
        for (const auto& entry : rigged_keys)
        {
            rigged_unique += entry.second.bytes;
        }
        boost::json::object rig;
        rig.emplace("faces", rigged_faces);
        rig.emplace("keys", (uint64_t)rigged_keys.size());
        rig.emplace("bytes", rigged_bytes);
        rig.emplace("unique_bytes", rigged_unique);
        o.emplace("rigged_sharing", std::move(rig));
        return o;
    }

    void api_census(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        CensusCollector collector;
        const U32 partitions[] = {LLViewerRegion::PARTITION_VOLUME, LLViewerRegion::PARTITION_BRIDGE, LLViewerRegion::PARTITION_AVATAR,
                                  LLViewerRegion::PARTITION_CONTROL_AV};
        for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
        {
            for (U32 type : partitions)
            {
                LLSpatialPartition* part = region->getSpatialPartition(type);
                if (part && part->mOctree)
                {
                    collector.traverse(part->mOctree);
                }
            }
        }
        boost::json::object doc = residentCensus(collector);
        const std::string path = req.has("path") ? req["path"].asString() : defaultDumpPath("census");
        std::string why;
        if (!dumpPathAllowed(path, why))
        {
            return r.error("refused to write " + path + ": " + why);
        }
        llofstream out(path, std::ios::binary);
        if (!out)
        {
            return r.error("cannot write " + path);
        }
        out << doc;
        out.close();
        r["path"] = path;
        r["faces"] = (F64)doc.at("faces").to_number<double>();
        r["items_without_record"] = (F64)collector.mItemsWithoutRecord;
        const boost::json::object& inst = doc.at("instancing").as_object();
        r["instancing_baked_bytes"] = inst.at("baked_bytes").to_number<double>();
        r["instancing_unique_bytes"] = inst.at("unique_bytes").to_number<double>();
        r["instancing_ratio"] = inst.at("ratio").to_number<double>();
    }

    void api_profile_frame(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        gShaderProfileFrame = true; // the next frame writes the Frame Profile, with the "submit" section
        r["logs_dir"] = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "");
    }

    class GLSubmitStatsListener : public LLEventAPI
    {
    public:
        GLSubmitStatsListener()
            : LLEventAPI("GLSubmitStats", "OpenGL submission instrumentation (development builds): counters per view and pass, GPU timers, the identical-image check")
        {
            add("status", "State of the collection and of the compare check, on [\"reply\"].", &api_status, llsd::map("reply", LLSD()));
            add("windowBegin",
                "Collect [\"frames\"] frames (default 60), GPU timers on unless [\"gpu\"] is false; the window is averaged per frame. "
                "[\"census\"] true also counts the copy census and the draw-state keys (dearer: use a window of its own).",
                &api_window_begin, llsd::map("reply", LLSD()));
            add("windowStop", "Stop collecting; what was collected stays readable.", &api_window_stop, llsd::map("reply", LLSD()));
            add("summary",
                "Headline numbers of the window ([\"kind\"] \"window\", default) or of the last frame (\"frame\"), on [\"reply\"]. Small.",
                &api_summary, llsd::map("reply", LLSD()));
            add("dump",
                "Write the full JSON of the window or last frame ([\"kind\"]) to [\"path\"] (default: the viewer log folder); the path is in the reply.",
                &api_dump, llsd::map("reply", LLSD()));
            add("compareArm",
                "Arm the identical-image check for the next frame: [\"pass\"] -1 all, 1 G-buffer, 2..5 sun cascades, 6..7 spot shadows, 8 post-deferred, "
                "13 probe face, 14 HUD, 15 impostor. [\"frames\"] (default 1, up to 600) keeps it armed until the passes have run. "
                "[\"inject\"] 1 flips one bit of the second readback to prove the check catches it.",
                &api_compare_arm, llsd::map("reply", LLSD()));
            add("census",
                "Write the resident copy census (every face of every draw item the scene holds: why it stays baked, by size class "
                "baked against unique bytes and bake time, rigged sharing) to [\"path\"] (default: the viewer log folder); headline in the reply.",
                &api_census, llsd::map("reply", LLSD()));
            add("compareResults", "Results of the last compared frame, on [\"reply\"].", &api_compare_results, llsd::map("reply", LLSD()));
            add("selfBench",
                "Measure the cost of the instrumentation hooks in this binary: ns per draw's worth of hooks not collecting and collecting, "
                "ns per pass scope. [\"iterations\"] default 1000000. Refused while collecting.",
                &api_self_bench, llsd::map("reply", LLSD()));
            add("profileFrame",
                "Write a Frame Profile (per-shader GPU time, binds, samples, triangles, plus the \"submit\" section) for the next frame; "
                "the file is profile.v*.json in [\"logs_dir\"].",
                &api_profile_frame, llsd::map("reply", LLSD()));
        }
    };

    GLSubmitStatsListener sListener;
}

namespace
{
    // The main loop's frame (MainLoopFrame): open, and the ends it holds for the frame display() began.
    bool sMainLoopOpen = false;
    bool sMainLoopHolds = false;
    bool sMainLoopHeldStats = false;
    bool sMainLoopHeldCompare = false;

    void finishFrame(bool owns_stats, bool owns_compare)
    {
        if (owns_stats)
        {
            LLGLSubmitStats::endFrame();
        }
        if (owns_compare)
        {
            sCompareOwned = false;
            sInRun = false;
            sReplay = false;
            if (--sArmFramesLeft > 0 && !armingDone())
            {
                return; // the next frame takes the arming over
            }
            if (sResults.empty())
            {
                LL_INFOS(kTag) << "the armed pass did not run in the armed frames" << LL_ENDL;
            }
            sArmed = LLGLSubmitCompare::PASS_OFF;
            sArmFramesLeft = 0;
        }
    }
}

LLGLSubmitStatsApi::MainLoopFrame::MainLoopFrame()
{
    if (!sMainLoopOpen)
    {
        sMainLoopOpen = true;
        sMainLoopHolds = false;
        mOpen = true;
    }
}

void LLGLSubmitStatsApi::MainLoopFrame::end()
{
    if (!mOpen)
    {
        return;
    }
    mOpen = false;
    sMainLoopOpen = false;
    if (sMainLoopHolds)
    {
        sMainLoopHolds = false;
        finishFrame(sMainLoopHeldStats, sMainLoopHeldCompare);
    }
}

LLGLSubmitStatsApi::DisplayFrame::DisplayFrame(bool profile_frame)
{
    static LLCachedControl<bool> stats_on(gSavedSettings, "RenderGLSubmitStats", false);
    static LLCachedControl<bool> stats_gpu(gSavedSettings, "RenderGLSubmitStatsGPU", true);
    const bool window = LLGLSubmitStats::windowActive();
    const bool collect = stats_on || window || profile_frame;
    const bool gpu = profile_frame ? true : (window ? LLGLSubmitStats::windowWantsGpu() : (bool)stats_gpu);
    ++sFramesSeen;
    mOwnsStats = LLGLSubmitStats::beginFrame(collect, gpu);

    sInject = gSavedSettings.getS32("RenderGLSubmitCompareInject");
    const S32 want = gSavedSettings.getS32("RenderGLSubmitCompare");
    if (want != LLGLSubmitCompare::PASS_OFF && sArmed == LLGLSubmitCompare::PASS_OFF)
    {
        mOwnsCompare = true;
        sCompareOwned = true;
        // armed for sPendingFrames frames at most (compareArm "frames", 1 from the setting), then the setting returns to 0
        sArmed = want;
        sArmFramesLeft = std::max<S32>(1, sPendingFrames);
        sPendingFrames = 1;
        sResults.clear();
        sResultsFrame = gFrameCount;
        ++sArmSerial;
        std::fill(std::begin(sPassDone), std::end(sPassDone), false);
        gSavedSettings.setS32("RenderGLSubmitCompare", 0);
        LL_INFOS(kTag) << "armed for " << (want == LLGLSubmitCompare::PASS_ALL ? "all passes" : passLabel(want))
                       << " in frame " << gFrameCount << " for up to " << sArmFramesLeft << " frames" << LL_ENDL;
    }
    else if (sArmed != LLGLSubmitCompare::PASS_OFF && !sCompareOwned)
    {
        // an arming of several frames whose passes have not all run: this frame carries it on
        mOwnsCompare = true;
        sCompareOwned = true;
    }
}

LLGLSubmitStatsApi::DisplayFrame::~DisplayFrame()
{
    if (sMainLoopOpen && !sMainLoopHolds && (mOwnsStats || mOwnsCompare))
    {
        // display() inside the main loop's frame: the frame ends after the reflection probe update
        sMainLoopHolds = true;
        sMainLoopHeldStats = mOwnsStats;
        sMainLoopHeldCompare = mOwnsCompare;
        return;
    }
    finishFrame(mOwnsStats, mOwnsCompare);
}

namespace
{
    // The face last baked and how long it took (main thread): registerFace runs right after the bake of the same face.
    const LLFace* sBakeFace = nullptr;
    U32 sBakeNs = 0;

    U64 nowNs()
    {
        return (U64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

LLGLSubmitStatsApi::CensusBake::CensusBake(const LLFace* face)
    : mFace(face), mStart(LLGLSubmitStats::onMain() ? nowNs() : 0)
{
}

LLGLSubmitStatsApi::CensusBake::~CensusBake()
{
    if (mStart)
    {
        sBakeFace = mFace;
        sBakeNs = (U32)std::min<U64>(nowNs() - mStart, 0xffffffffull);
    }
}

void LLGLSubmitStatsApi::censusFace(LLDrawInfo& info, LLFace* facep, U32 pass_type)
{
    using namespace LLGLSubmitMath;
    if (!facep || !LLGLSubmitStats::onMain())
    {
        return;
    }
    LLDrawable* drawable = facep->getDrawable();
    LLVOVolume* vobj = drawable ? drawable->getVOVolume() : nullptr;
    LLViewerObject* obj = facep->getViewerObject();
    const LLVolume* volume = vobj ? vobj->getVolume() : nullptr;
    const LLTextureEntry* te = facep->getTextureEntry();
    LLVertexBuffer* buffer = facep->getVertexBuffer();

    CensusFacts f;
    f.hasKey = volume && te && buffer;
    f.hud = obj && obj->isHUDAttachment();
    f.flexi = vobj && vobj->isFlexible();
    f.gltfAsset = obj && obj->mGLTFAsset;
    f.rigged = facep->isState(LLFace::RIGGED);
    f.blended = pass_type == LLRenderPass::PASS_ALPHA;
    f.invisible = pass_type == LLRenderPass::PASS_INVISIBLE || pass_type == LLRenderPass::PASS_INVISI_SHINY ||
                  pass_type == LLRenderPass::PASS_ALPHA_INVISIBLE;
    f.textureAnim = facep->isState(LLFace::TEXTURE_ANIM);
    f.planar = te && te->getTexGen() == LLTextureEntry::TEX_GEN_PLANAR;
    f.media = facep->hasMedia();
    const LLGLTFMaterial* gltf = te ? te->getGLTFRenderMaterial() : nullptr;
    const LLMaterial* mat = te ? te->getMaterialParams().get() : nullptr;
    f.legacyBump = te && !gltf && te->getBumpmap() && te->getBumpmap() < 18 && (!mat || mat->getNormalID().isNull());
    f.selected = obj && obj->isSelected();
    if (vobj && !f.rigged)
    {
        // the instanced transform's inputs: the bake's position and normal matrices (the vertices are not checked)
        const LLMatrix4& m = vobj->getRelativeXform();
        const LLMatrix3& n = vobj->getRelativeXformInvTrans();
        for (U32 i = 0; i < 4; ++i)
        {
            for (U32 j = 0; j < 4; ++j)
            {
                f.numbersSafe = f.numbersSafe && censusNumberSafe(m.mMatrix[i][j]);
            }
        }
        for (U32 i = 0; i < 3; ++i)
        {
            for (U32 j = 0; j < 3; ++j)
            {
                f.numbersSafe = f.numbersSafe && censusNumberSafe(n.mMatrix[i][j]);
            }
        }
    }

    CensusFace face;
    if (f.hasKey)
    {
        CensusKeyInputs k;
        k.volume = (uint64_t)(uintptr_t)volume;
        k.face = (uint32_t)facep->getTEOffset();
        k.vertices = facep->getGeomCount();
        k.indices = facep->getIndicesCount();
        k.formatMask = buffer->getTypeMask();
        te->getScale(&k.scaleS, &k.scaleT);
        te->getOffset(&k.offsetS, &k.offsetT);
        k.rotation = te->getRotation();
        const LLColor4& color = te->getColor();
        for (U32 c = 0; c < 4; ++c)
        {
            k.color[c] = color.mV[c];
        }
        k.glow = te->getGlow();
        // the material's hash (glTF or legacy), as registerFace keyed the item on it: it carries the normal and specular
        // maps' and the glTF textures' transforms that the bake writes into the extra texture coordinates
        std::memcpy(k.material, info.mMaterialID.mData, sizeof(k.material));
        k.rigged = f.rigged;
        if (f.rigged)
        {
            k.skinHash = facep->getSkinHash();
            k.textureIndex = facep->getTextureIndex();
        }
        face.key = censusKey(k);
    }
    face.face = (uint64_t)(uintptr_t)facep;
    face.vertices = facep->getGeomCount();
    face.bytes = buffer ? (uint32_t)(LLVertexBuffer::calcVertexSize(buffer->getTypeMask()) * facep->getGeomCount() +
                                     facep->getIndicesCount() * sizeof(U16))
                        : 0;
    face.bakeNs = sBakeFace == facep ? sBakeNs : 0;
    face.exclusion = censusExclusion(f);
    face.blended = f.blended;
    info.mGLSubmitCensus.push_back(face);
}

void LLGLSubmitStatsApi::addToProfile(boost::json::object& stats)
{
    LLGLSubmitStats::endFrame();
    LLGLSubmitStats::resolveGpu(true);
    if (auto f = LLGLSubmitStats::lastFrame())
    {
        boost::json::object submit = LLGLSubmitStats::frameToJson(*f);
        submit.emplace("compare", resultsToJson());
        stats.emplace("submit", std::move(submit));
    }
}

#endif // VKS_GL_STATS

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
extern bool gSnapshot;           // llviewerdisplay.cpp: display() renders a snapshot

namespace
{
    // ---- the compare check -------------------------------------------------------------------------------

    static_assert(LLGLSubmitCompare::PASS_COUNT == LLGLSubmitMath::kCompareIds, "the compare ids of the pure arming");
    // The arming (the pass or passes to compare, for how many frames, which are done) and the frames that own it: the main
    // loop's frame, display()'s, and the ends the main loop holds (pure, llglsubmitstats.h; unit-tested).
    LLGLSubmitMath::CompareFrames sFrames;
    bool sInRun = false;                        // a compared pass is being rendered (no nesting)
    bool sReplay = false;                       // ... for the second time
    S32 sInject = 0;
    U64 sFramesSeen = 0;                        // display() and display_startup() frames, collected or not
    U64 sArmSerial = 0;                         // counts the armings, so a client can tell a fresh result from a stale one
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
        std::string view;       // the view the pass was compared in, when it can be more than one (13: probe or hero face)
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

    // Each pass once per arming, only while a frame owns the arming (the main loop's), never nested. The probe face compare
    // is the only one a cube snapshot reaches: every other pass is the main view's (a probe face reaches their sites with
    // other targets).
    bool armedFor(S32 pass)
    {
        return sFrames.wants(pass) && !sInRun && (pass == LLGLSubmitCompare::PASS_PROBE_FACE) == gCubeSnapshot;
    }

    // The view a compare result belongs to, when a pass can be compared in more than one: 13 compares the first probe face
    // rendered while armed, a mirror's (hero face, inside display()) or a reflection probe's (probe face, the probe update).
    std::string compareView(S32 pass)
    {
        if (pass == LLGLSubmitCompare::PASS_PROBE_FACE)
        {
            return gPipeline.mHeroProbeManager.isMirrorPass() ? "hero face" : "probe face";
        }
        return std::string();
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
    std::string view;                   // compareView(pass), taken when the run was armed
    LLRenderTarget* target = nullptr;   // null: the back buffer
    bool restore = false;
    std::vector<Capture> inputs;
    std::vector<Capture> first;
    BackBufferCopy back;        // the back buffer: its formats, and its inputs with 'restore'
};

namespace
{
    void reportNotRun(S32 pass, const std::string& view, const std::string& note)
    {
        PassResult skipped;
        skipped.pass = pass;
        skipped.view = view;
        skipped.note = note;
        LL_INFOS(kTag) << passLabel(pass) << (view.empty() ? "" : " (" + view + ")") << ": " << skipped.note << LL_ENDL;
        sResults.push_back(std::move(skipped));
    }

    // Puts the first render's output back into the attachments a failed restore already overwrote with the inputs
    // (matched by texture), so that the frame keeps the first render when the second one is skipped.
    void putBackFirst(const std::vector<Capture>& first, const std::vector<const Capture*>& overwritten)
    {
        for (const Capture* o : overwritten)
        {
            const auto it = std::find_if(first.begin(), first.end(), [&](const Capture& f) { return f.texture == o->texture; });
            if (it == first.end() || !writeTexture(*it))
            {
                LL_WARNS(kTag) << "could not put the first render's " << o->name << " back: this frame shows the pass's inputs there" << LL_ENDL;
            }
        }
    }
}

bool LLGLSubmitCompare::Run::arm(S32 pass, NotRun not_run)
{
    if (!armedFor(pass))
    {
        return false;
    }
    sFrames.done[pass] = true;
    if (const char* why = not_run ? not_run() : nullptr)
    {
        reportNotRun(pass, compareView(pass), why);
        return false;
    }
    return true;
}

LLGLSubmitCompare::Run::Run(S32 pass, LLRenderTarget* target, bool restore_inputs, NotRun not_run)
{
    if (!arm(pass, not_run))
    {
        return;
    }
    if (!target || target->getUsage() != LLTexUnit::TT_TEXTURE)
    {
        reportNotRun(pass, compareView(pass), "not run: no plain 2D target this frame (missing, multisampled, or stochastic transparency in use)");
        return;
    }
    mImpl = std::make_unique<Impl>();
    mImpl->pass = pass;
    mImpl->view = compareView(pass);
    mImpl->target = target;
    mImpl->restore = restore_inputs;
    sInRun = true;
    sFadeLog.clear();
    sFadeCursor = 0;
}

LLGLSubmitCompare::Run::Run(S32 pass, BackBuffer, bool restore_inputs, NotRun not_run)
{
    if (!arm(pass, not_run))
    {
        return;
    }
    auto impl = std::make_unique<Impl>();
    if (const char* why = impl->back.make(restore_inputs))
    {
        reportNotRun(pass, compareView(pass), why);
        return;
    }
    if (restore_inputs && !impl->back.save())
    {
        // rendered once, as normally: a second render over the first would not be the same render
        reportNotRun(pass, compareView(pass), "not run: the back buffer could not be copied (its depth or stencil format refuses the blit)");
        return;
    }
    mImpl = std::move(impl);
    mImpl->pass = pass;
    mImpl->view = compareView(pass);
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
    {
        m.first = m.target ? captureTarget(*m.target) : captureBackBuffer(m.back.width, m.back.height, m.back.floatColor, m.back.hasDepth);
        // Without the whole first readback there is nothing to compare; without all the inputs put back the second render
        // would draw over the first (the HUD twice into the presented frame). Either way the pass is not rendered again.
        const size_t attachments = m.target ? attachmentsOf(*m.target).size() : m.first.size();
        const char* stop = m.first.empty() || m.first.size() != attachments ? "not run: the first render could not be read back" : nullptr;
        if (!stop && m.restore && m.target && m.inputs.size() != attachments)
        {
            stop = "not run: the inputs could not be read back for a second render";
        }
        else if (!stop && m.restore && m.target)
        {
            std::vector<const Capture*> overwritten;
            for (const Capture& c : m.inputs)
            {
                if (!writeTexture(c))
                {
                    LL_WARNS(kTag) << "could not restore " << c.name << " for the second render" << LL_ENDL;
                    putBackFirst(m.first, overwritten);
                    stop = "not run: the inputs could not be put back for a second render";
                    break;
                }
                overwritten.push_back(&c);
            }
            std::vector<Capture>().swap(m.inputs);
        }
        else if (!stop && m.restore && !m.back.restore())
        {
            // a blit that fails changes nothing: the back buffer still holds the first render
            LL_WARNS(kTag) << "could not restore the back buffer for the second render" << LL_ENDL;
            stop = "not run: the inputs could not be put back for a second render";
        }
        if (stop)
        {
            m.back.release();
            reportNotRun(m.pass, m.view, stop);
            sInRun = false;
            sFadeLog.clear();
            mImpl.reset();
            return false;
        }
        sReplay = true;
        sFadeCursor = 0;
        return true;
    }
    default:
        break;
    }

    // After the second render. The second readback of a target is compared one attachment at a time and dropped, so the
    // peak is the first readback of the whole target plus one attachment.
    sReplay = false;
    PassResult res;
    res.pass = m.pass;
    res.view = m.view;
    res.ran = true;
    PixelStore store;
    const std::vector<Attach> list = m.target ? attachmentsOf(*m.target) : std::vector<Attach>();
    std::vector<Capture> back_second;
    if (!m.target)
    {
        back_second = captureBackBuffer(m.back.width, m.back.height, m.back.floatColor, m.back.hasDepth);
    }
    const size_t count = m.target ? list.size() : back_second.size();
    m.back.release();
    if (count != m.first.size() || m.first.empty())
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
    LL_INFOS(kTag) << passLabel(m.pass) << (m.view.empty() ? "" : " (" + m.view + ")") << ": "
                   << (res.ran ? (res.identical() ? "IDENTICAL" : "DIFFERENT") : res.note) << LL_ENDL;
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
    return LLGLSubmitMath::compareImplemented(pass);
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
    // blips keep ageing (and moving) in every non-HUD view's tail for 2 s after they are turned off
    if (gShowObjectUpdates || gPipeline.hasDebugBlips())
    {
        return "not run: object update blips are on or still fading (they age in the pass's tail)";
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
            if (!r.view.empty())
            {
                o.emplace("view", r.view); // 13: "probe face" (a reflection probe's) or "hero face" (a mirror's)
            }
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
                                           "stateToggles", "occBlocking", "runs", "mergeableDraws", "runsNoUniform",
                                           "mergeableDrawsNoUniform", "censusFaces", "cpu_ms", "gpu_ms"};
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
        r["census_recording"] = LLGLSubmitStatsApi::censusRecording();
        r["compare_armed"] = (S32)sFrames.armed;
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
        // a census window credits only the draw items that have census records: censusRecord makes them
        r["census_recording"] = LLGLSubmitStatsApi::censusRecording();
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
        sFrames.pendingFrames = frames;
        gSavedSettings.setS32("RenderGLSubmitCompare", pass);
        r["armed"] = pass;
        r["frames"] = frames;
    }

    void api_compare_results(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        r["armed"] = (S32)sFrames.armed;
        r["pending"] = gSavedSettings.getS32("RenderGLSubmitCompare");
        r["frame"] = (F64)sResultsFrame;
        r["arm_serial"] = (F64)sArmSerial;
        r["results"] = LlsdFromJson(resultsToJson());
    }

    // Up to 'limit' faces of the scene's draw items (volume partitions), for the census record's benchmark.
    class FaceSampler : public OctreeTraveler
    {
    public:
        explicit FaceSampler(size_t limit) : mLimit(limit) {}
        std::vector<LLFace*> mFaces;

        void visit(const OctreeNode* node) override
        {
            LLSpatialGroup* group = (LLSpatialGroup*)node->getListener(0);
            if (!group)
            {
                return;
            }
            for (LLSpatialGroup::element_iter it = group->getDataBegin(); it != group->getDataEnd() && mFaces.size() < mLimit; ++it)
            {
                LLDrawable* drawable = (LLDrawable*)(*it)->getDrawable();
                if (!drawable || drawable->isDead() || !drawable->getVOVolume())
                {
                    continue;
                }
                for (S32 i = 0; i < drawable->getNumFaces() && mFaces.size() < mLimit; ++i)
                {
                    if (LLFace* face = drawable->getFace(i))
                    {
                        mFaces.push_back(face);
                    }
                }
            }
            for (LLSpatialBridge* bridge : group->mBridgeList)
            {
                if (mFaces.size() < mLimit)
                {
                    traverse(bridge->mOctree);
                }
            }
        }

    private:
        size_t mLimit;
    };

    // The cost of the hooks in this very binary, measured in place: the mix of hooks one draw runs (24 macro sites),
    // with the statistics not collecting (the cost every dev build pays: a pointer test per site), collecting, the cost
    // of one pass scope with and without GPU timers, and the copy census's cost per face it records (the record on real
    // faces of the scene, and the timer around the face's bake), which a census frame pays censusFaces times.
    void api_self_bench(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        if (LLGLSubmitStats::windowActive() || LLGLSubmitStats::collecting() || gSavedSettings.getBOOL("RenderGLSubmitStats"))
        {
            return r.error("stop the collection first (the benchmark uses the frame machinery)");
        }
        using Clock = std::chrono::steady_clock;
        const S32 n = llclamp(req.has("iterations") ? req["iterations"].asInteger() : 1000000, 1000, 100000000);
        constexpr int kHooksPerDraw = 24;
        // an item with census records for the census draw hook (credited in census windows only: here its test)
        struct BenchItem { std::vector<LLGLSubmitMath::CensusFace> mGLSubmitCensus = std::vector<LLGLSubmitMath::CensusFace>(2); };
        const BenchItem bench_item;
        // The barrier after each hook makes the compiler reload the collection pointer at every site, as it must in the
        // renderer where GL calls sit between the hooks (without it a loop of hooks collapses to one test).
        auto draw_mix = [&bench_item](S32 count)
        {
#define VKS_BENCH_HOOK(hook) do { hook; std::atomic_signal_fence(std::memory_order_seq_cst); } while (0)
            for (S32 i = 0; i < count; ++i)
            {
                // phase 2b's step 1: a texture unbind, a cap toggle, setBuffer's vertex format check, the census draw's test
                VKS_BENCH_HOOK(VKS_GLSTAT_TEX_UNBIND(true));
                VKS_BENCH_HOOK(VKS_GLSTAT_CAP_TOGGLE(GL_BLEND));
                VKS_BENCH_HOOK(VKS_GLSTAT_VBO(1, LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_NORMAL | LLVertexBuffer::MAP_TEXCOORD0));
                VKS_BENCH_HOOK(VKS_GLSTAT_CENSUS_DRAW(bench_item));
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

        // The copy census per recorded face: the record (key, exclusion rules, exactness guard) on up to 1024 real faces,
        // and the timer around a face's bake (two clock reads), as a census frame pays them for each face it rebuilds.
        FaceSampler sampler(1024);
        for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
        {
            for (U32 type : {LLViewerRegion::PARTITION_VOLUME, LLViewerRegion::PARTITION_BRIDGE})
            {
                LLSpatialPartition* part = region->getSpatialPartition(type);
                if (part && part->mOctree && sampler.mFaces.size() < 1024)
                {
                    sampler.traverse(part->mOctree);
                }
            }
        }
        double record_ns = 0.0, bake_timer_ns = 0.0;
        S32 records = 0;
        if (!sampler.mFaces.empty())
        {
            const S32 rounds = std::max<S32>(1, 50000 / (S32)sampler.mFaces.size());
            records = rounds * (S32)sampler.mFaces.size();
            U64 sink = 0;
            record_ns = time_ns([&]
            {
                for (S32 k = 0; k < rounds; ++k)
                {
                    for (LLFace* face : sampler.mFaces)
                    {
                        sink ^= LLGLSubmitStatsApi::makeCensusFace(face, LLUUID::null, LLRenderPass::PASS_SIMPLE).key;
                    }
                }
            }) / records;
            const bool recording = LLGLSubmitStatsApi::sCensusRecording;
            LLGLSubmitStatsApi::sCensusRecording = true; // the timer runs only while the census records
            bake_timer_ns = time_ns([&]
            {
                for (S32 k = 0; k < records; ++k)
                {
                    LLGLSubmitStatsApi::CensusBake bake(sampler.mFaces[k % sampler.mFaces.size()]);
                    std::atomic_signal_fence(std::memory_order_seq_cst);
                }
            }) / records;
            LLGLSubmitStatsApi::sCensusRecording = recording;
            LLGLSubmitStatsApi::forgetCensusBake(); // no record may take a benchmark's bake time
            r["census_bench_sink"] = (F64)(sink & 0xffff); // keeps the records from being optimised away
        }

        r["iterations"] = n;
        r["hooks_per_draw"] = kHooksPerDraw;
        r["ns_per_draw_not_collecting"] = off_ns / n;
        r["ns_per_draw_collecting"] = on_ns / n;
        r["ns_per_hook_not_collecting"] = off_ns / n / kHooksPerDraw;
        r["ns_per_hook_collecting"] = on_ns / n / kHooksPerDraw;
        r["ns_per_scope_cpu_only"] = scope_cpu_ns / scopes;
        r["ns_per_scope_with_gpu_timers"] = scope_gpu_ns / scopes;
        r["census_faces_sampled"] = (S32)sampler.mFaces.size();
        r["ns_per_census_record"] = record_ns;
        r["ns_per_census_bake_timer"] = bake_timer_ns;
        LL_INFOS(kTag) << "hook cost: " << off_ns / n << " ns per draw's hooks not collecting, " << on_ns / n << " collecting; scope "
                       << scope_cpu_ns / scopes << " ns, with GPU timers " << scope_gpu_ns / scopes << " ns; census record "
                       << record_ns << " ns and bake timer " << bake_timer_ns << " ns per face (" << sampler.mFaces.size()
                       << " faces sampled)" << LL_ENDL;
    }

    // ---- the resident copy census: every face of every draw item the scene holds now ----

    // The partitions whose draw items LLVolumeGeometryManager builds (registerFace records their faces): volumes, their
    // bridges, avatars' and animesh attachments, and HUD attachments.
    const U32 kCensusPartitions[] = {LLViewerRegion::PARTITION_VOLUME, LLViewerRegion::PARTITION_BRIDGE, LLViewerRegion::PARTITION_AVATAR,
                                     LLViewerRegion::PARTITION_CONTROL_AV, LLViewerRegion::PARTITION_HUD};

    struct ResidentFace
    {
        uint64_t key = 0;
        uint32_t vertices = 0;
        uint32_t vertexBytes = 0;
        uint32_t indexBytes = 0;
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
                            r.vertexBytes = f.vertexBytes;
                            r.indexBytes = f.indexBytes;
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
        // the faces of one key have one vertex count and one buffer format, so one copy's bytes are every copy's
        struct Key { uint32_t copies = 0; uint32_t vertices = 0; uint32_t vertexBytes = 0; uint32_t indexBytes = 0; uint64_t bakeNs = 0; };
        std::unordered_map<uint64_t, Key> keys, rigged_keys;
        uint64_t faces = 0, vertices = 0, vertex_bytes = 0, index_bytes = 0, bake_ns = 0;
        uint64_t reason_faces[CENSUS_EXCLUSIONS] = {}, reason_vertices[CENSUS_EXCLUSIONS] = {}, reason_bytes[CENSUS_EXCLUSIONS] = {};
        uint64_t reason_vertex_bytes[CENSUS_EXCLUSIONS] = {};
        uint64_t rigged_faces = 0, rigged_bytes = 0;
        for (const auto& entry : c.mFaces)
        {
            const ResidentFace& f = entry.second;
            const uint8_t reason = f.exclusion < CENSUS_EXCLUSIONS ? f.exclusion : (uint8_t)CENSUS_NO_KEY;
            const uint64_t bytes = (uint64_t)f.vertexBytes + f.indexBytes;
            ++faces;
            vertices += f.vertices;
            vertex_bytes += f.vertexBytes;
            index_bytes += f.indexBytes;
            bake_ns += f.bakeNs;
            reason_faces[reason]++;
            reason_vertices[reason] += f.vertices;
            reason_bytes[reason] += bytes;
            reason_vertex_bytes[reason] += f.vertexBytes;
            if (reason == CENSUS_ELIGIBLE)
            {
                Key& k = keys[f.key];
                k.copies++;
                k.vertices = f.vertices;
                k.vertexBytes = f.vertexBytes;
                k.indexBytes = f.indexBytes;
                k.bakeNs += f.bakeNs;
            }
            else if (reason == CENSUS_RIGGED && !f.blended && f.key)
            {
                // rigged buffer sharing (G5b-1): a rigged, non-blended face's bake depends on the volume face, the skin
                // and the texture entry only, so equal keys could share one buffer between avatars
                ++rigged_faces;
                rigged_bytes += bytes;
                Key& k = rigged_keys[f.key];
                k.copies++;
                k.vertexBytes = f.vertexBytes;
                k.indexBytes = f.indexBytes;
            }
        }

        boost::json::object o;
        o.emplace("schema", 2);
        o.emplace("kind", "resident_census");
        o.emplace("recording", LLGLSubmitStatsApi::censusRecording());
        o.emplace("items", c.mItems);
        o.emplace("items_without_record", c.mItemsWithoutRecord);
        o.emplace("faces", faces);
        o.emplace("vertices", vertices);
        o.emplace("bytes", vertex_bytes + index_bytes);
        o.emplace("vertex_bytes", vertex_bytes);
        o.emplace("index_bytes", index_bytes);
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
                e.emplace("vertex_bytes", reason_vertex_bytes[r]);
                reasons.emplace(censusExclusionName(r), std::move(e));
            }
        }
        o.emplace("by_reason", std::move(reasons));

        // Eligible faces by size class: baked bytes (one bake per copy) against unique bytes (one per key), vertex and
        // index bytes together and the vertex bytes alone; the vertex bytes of repeated keys (2 copies or more, every
        // copy: what X1-P2's trigger reads for faces under 64 vertices); the bake time of the copies past the first of
        // each key (what a cache hit skips).
        uint64_t s_faces[kSizes] = {}, s_vertices[kSizes] = {}, s_keys[kSizes] = {}, s_baked[kSizes] = {}, s_unique[kSizes] = {};
        uint64_t s_baked_vb[kSizes] = {}, s_unique_vb[kSizes] = {}, s_repeated_vb[kSizes] = {}, s_repeated_faces[kSizes] = {};
        uint64_t s_bake_ns[kSizes] = {}, s_repeat_bake_ns[kSizes] = {}, s_copies_hist[kSizes][kCensusRunBuckets] = {};
        for (const auto& entry : keys)
        {
            const Key& k = entry.second;
            const int s = censusSizeClass(k.vertices);
            const uint64_t bytes = (uint64_t)k.vertexBytes + k.indexBytes;
            s_faces[s] += k.copies;
            s_vertices[s] += (uint64_t)k.vertices * k.copies;
            s_keys[s]++;
            s_baked[s] += bytes * k.copies;
            s_unique[s] += bytes;
            s_baked_vb[s] += (uint64_t)k.vertexBytes * k.copies;
            s_unique_vb[s] += k.vertexBytes;
            if (k.copies > 1)
            {
                s_repeated_vb[s] += (uint64_t)k.vertexBytes * k.copies;
                s_repeated_faces[s] += k.copies;
            }
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
            e.emplace("baked_vertex_bytes", s_baked_vb[s]);
            e.emplace("unique_vertex_bytes", s_unique_vb[s]);
            e.emplace("repeated_faces", s_repeated_faces[s]);
            e.emplace("repeated_vertex_bytes", s_repeated_vb[s]);
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
            rigged_unique += (uint64_t)entry.second.vertexBytes + entry.second.indexBytes;
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
        for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
        {
            for (U32 type : kCensusPartitions)
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
        r["recording"] = LLGLSubmitStatsApi::censusRecording();
        r["faces"] = (F64)doc.at("faces").to_number<double>();
        r["items"] = (F64)collector.mItems;
        r["items_without_record"] = (F64)collector.mItemsWithoutRecord;
        const boost::json::object& inst = doc.at("instancing").as_object();
        r["instancing_baked_bytes"] = inst.at("baked_bytes").to_number<double>();
        r["instancing_unique_bytes"] = inst.at("unique_bytes").to_number<double>();
        r["instancing_ratio"] = inst.at("ratio").to_number<double>();
    }

    // Marks every draw item of the census partitions for a rebuild (LLPipeline::rebuildDrawInfo's traversal over all of
    // them): the next frame's rebuildPriorityGroups rebuilds the whole queue, visible or not, so each item gets its record.
    class CensusRebuild : public OctreeTraveler
    {
    public:
        S32 mGroups = 0;

        void visit(const OctreeNode* node) override
        {
            LLSpatialGroup* group = (LLSpatialGroup*)node->getListener(0);
            if (!group)
            {
                return;
            }
            if (!group->isDead() && group->getSpatialPartition() && group->getSpatialPartition()->mRenderByGroup)
            {
                group->setState(LLSpatialGroup::GEOM_DIRTY);
                gPipeline.markRebuild(group);
                ++mGroups;
            }
            for (LLSpatialBridge* bridge : group->mBridgeList)
            {
                traverse(bridge->mOctree);
            }
        }
    };

    void api_census_record(const LLSD& req)
    {
        LLEventAPI::Response r(LLSD(), req);
        if (!req.has("on"))
        {
            return r.error("[\"on\"] is required: true records the census (and rebuilds every draw item), false stops");
        }
        const bool on = req["on"].asBoolean();
        S32 groups = 0;
        if (on && !LLGLSubmitStatsApi::sCensusRecording)
        {
            LLGLSubmitStatsApi::sCensusRecording = true;
            CensusRebuild rebuild;
            for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
            {
                for (U32 type : kCensusPartitions)
                {
                    LLSpatialPartition* part = region->getSpatialPartition(type);
                    if (part && part->mOctree)
                    {
                        rebuild.traverse(part->mOctree);
                    }
                }
            }
            groups = rebuild.mGroups;
            LL_INFOS(kTag) << "copy census recording on: " << groups << " groups marked for rebuild" << LL_ENDL;
        }
        else if (!on && LLGLSubmitStatsApi::sCensusRecording)
        {
            // the records stay with their draw items until those are rebuilt; census windows only credit them
            LLGLSubmitStatsApi::sCensusRecording = false;
            LLGLSubmitStatsApi::forgetCensusBake();
            LL_INFOS(kTag) << "copy census recording off" << LL_ENDL;
        }
        r["recording"] = LLGLSubmitStatsApi::sCensusRecording;
        r["groups_rebuilt"] = groups;
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
                "[\"census\"] true also counts the copy census and the draw-state keys (dearer: use a window of its own, after "
                "censusRecord has given every draw item its record).",
                &api_window_begin, llsd::map("reply", LLSD()));
            add("windowStop", "Stop collecting; what was collected stays readable.", &api_window_stop, llsd::map("reply", LLSD()));
            add("summary",
                "Headline numbers of the window ([\"kind\"] \"window\", default) or of the last frame (\"frame\"), on [\"reply\"]. Small.",
                &api_summary, llsd::map("reply", LLSD()));
            add("dump",
                "Write the full JSON of the window or last frame ([\"kind\"]) to [\"path\"] (default: the viewer log folder); the path is in the reply.",
                &api_dump, llsd::map("reply", LLSD()));
            add("compareArm",
                "Arm the identical-image check for the next main-loop frame: [\"pass\"] -1 all, 1 G-buffer, 2..5 sun cascades, 6..7 spot "
                "shadows, 8 post-deferred, 13 probe face, 14 HUD, 15 impostor. [\"frames\"] (default 1, up to 600) keeps it armed for up "
                "to that many main-loop frames until the passes have run; a pass that did not run in them gets a result saying so. "
                "[\"inject\"] 1 flips one bit of the second readback to prove the check catches it.",
                &api_compare_arm, llsd::map("reply", LLSD()));
            add("censusRecord",
                "[\"on\"] true: record the copy census at every draw item's build, and rebuild every draw item now (the next frame) "
                "so that each has its record; false: stop recording (off, registerFace and the bake pay one test each).",
                &api_census_record, llsd::map("reply", LLSD()));
            add("census",
                "Write the resident copy census (every face of every draw item the scene holds: why it stays baked, by size class "
                "baked against unique bytes and bake time, rigged sharing) to [\"path\"] (default: the viewer log folder); headline in the reply.",
                &api_census, llsd::map("reply", LLSD()));
            add("compareResults", "Results of the last compared frame, on [\"reply\"].", &api_compare_results, llsd::map("reply", LLSD()));
            add("selfBench",
                "Measure the cost of the instrumentation hooks in this binary: ns per draw's worth of hooks not collecting and collecting, "
                "ns per pass scope, ns per copy census record and bake timer (on faces of the scene). [\"iterations\"] default 1000000. "
                "Refused while collecting.",
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
    // The end of a frame: its statistics and, when it owned the arming, the arming's frame. An arming that is over gives
    // every pass it asked for and that did not run a result saying so, so that no pass is missing from the results.
    void finishFrame(bool owns_stats, bool owns_compare)
    {
        if (owns_stats)
        {
            LLGLSubmitStats::endFrame();
        }
        if (owns_compare)
        {
            sInRun = false;
            sReplay = false;
            if (!sFrames.compareEnd())
            {
                return; // the next main-loop frame carries the arming on
            }
            for (S32 p = 1; p < LLGLSubmitCompare::PASS_COUNT; ++p)
            {
                if (sFrames.asked(p) && !sFrames.done[p])
                {
                    reportNotRun(p, std::string(), "not run: did not run in the " + std::to_string(sFrames.frames) +
                                                   (sFrames.frames == 1 ? " armed frame" : " armed frames"));
                }
            }
            sFrames.disarm();
        }
    }
}

LLGLSubmitStatsApi::MainLoopFrame::MainLoopFrame()
{
    mOpen = sFrames.mainLoopBegin();
}

void LLGLSubmitStatsApi::MainLoopFrame::end()
{
    if (!mOpen)
    {
        return;
    }
    mOpen = false;
    bool stats = false, compare = false;
    if (sFrames.mainLoopEnd(stats, compare))
    {
        finishFrame(stats, compare);
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
    // only the main loop's display() begins or carries an arming (not a snapshot's, not display_startup() from the startup
    // code); the setting stays set until such a frame takes it
    switch (sFrames.displayBegin(want, gSnapshot))
    {
    case LLGLSubmitMath::CompareFrames::BEGIN_NEW:
        mOwnsCompare = true;
        // armed for up to sFrames.frames main-loop frames (compareArm "frames", 1 from the setting); the setting returns to 0
        sResults.clear();
        sResultsFrame = gFrameCount;
        ++sArmSerial;
        gSavedSettings.setS32("RenderGLSubmitCompare", 0);
        LL_INFOS(kTag) << "armed for " << (want == LLGLSubmitCompare::PASS_ALL ? "all passes" : passLabel(want))
                       << " in frame " << gFrameCount << " for up to " << sFrames.frames << " frames" << LL_ENDL;
        break;
    case LLGLSubmitMath::CompareFrames::BEGIN_CARRIED:
        mOwnsCompare = true; // an arming of several frames whose passes have not all run
        break;
    default:
        break;
    }
}

LLGLSubmitStatsApi::DisplayFrame::~DisplayFrame()
{
    // display() inside the main loop's frame: its ends come after the reflection probe update (MainLoopFrame::end)
    if (!sFrames.displayEnd(mOwnsStats, mOwnsCompare))
    {
        finishFrame(mOwnsStats, mOwnsCompare);
    }
}

bool LLGLSubmitStatsApi::sCensusRecording = false;

namespace
{
    // The face last baked and how long it took (main thread): registerFace runs right after the bake of the same face.
    const LLFace* sBakeFace = nullptr;
    U32 sBakeNs = 0;

    U64 nowNs()
    {
        return (U64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // A volume's identity in the census key: its parameters and detail level, by which LLVolumeMgr shares one volume
    // between objects (its address could be reused by another volume while a draw item not rebuilt yet still holds a
    // record of the first).
    uint64_t volumeIdentity(const LLVolume& volume)
    {
        using LLGLSubmitMath::mix64;
        using LLGLSubmitMath::mixFloat;
        const LLVolumeParams& params = volume.getParams();
        const LLProfileParams& profile = params.getProfileParams();
        const LLPathParams& path = params.getPathParams();
        uint64_t h = mix64(0x766f6c756d65ull, ((uint64_t)profile.getCurveType() << 8) | path.getCurveType());
        for (F32 v : {profile.getBegin(), profile.getEnd(), profile.getHollow(), path.getBegin(), path.getEnd(), path.getScaleX(),
                      path.getScaleY(), path.getShearX(), path.getShearY(), path.getTwistBegin(), path.getTwistEnd(),
                      path.getRadiusOffset(), path.getTaperX(), path.getTaperY(), path.getRevolutions(), path.getSkew()})
        {
            h = mixFloat(h, v);
        }
        uint64_t id[2];
        std::memcpy(id, params.getSculptID().mData, sizeof(id));
        h = mix64(mix64(mix64(h, id[0]), id[1]), params.getSculptType());
        return mixFloat(h, volume.getDetail());
    }

    // A face's identity in the resident census: its object and face (an address could be reused by another face while a
    // draw item not rebuilt yet still holds the record of the first).
    uint64_t faceIdentity(const LLFace* facep, const LLViewerObject* obj)
    {
        if (!obj)
        {
            return LLGLSubmitMath::mix64(0x66616365ull, (uint64_t)(uintptr_t)facep);
        }
        uint64_t id[2];
        std::memcpy(id, obj->getID().mData, sizeof(id));
        return LLGLSubmitMath::mix64(LLGLSubmitMath::mix64(LLGLSubmitMath::mix64(0x66616365ull, id[0]), id[1]), (uint32_t)facep->getTEOffset());
    }
}

U64 LLGLSubmitStatsApi::CensusBake::start()
{
    return LLGLSubmitStats::onMain() ? nowNs() : 0;
}

void LLGLSubmitStatsApi::CensusBake::finish()
{
    sBakeFace = mFace;
    sBakeNs = (U32)std::min<U64>(nowNs() - mStart, 0xffffffffull);
}

void LLGLSubmitStatsApi::forgetCensusBake()
{
    sBakeFace = nullptr;
    sBakeNs = 0;
}

void LLGLSubmitStatsApi::censusFace(LLDrawInfo& info, LLFace* facep, U32 pass_type)
{
    if (!facep || !LLGLSubmitStats::onMain())
    {
        return;
    }
    VKS_GLSTAT_INC(censusFaces);
    info.mGLSubmitCensus.push_back(makeCensusFace(facep, info.mMaterialID, pass_type));
}

LLGLSubmitMath::CensusFace LLGLSubmitStatsApi::makeCensusFace(LLFace* facep, const LLUUID& material_id, U32 pass_type)
{
    using namespace LLGLSubmitMath;
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
    // genDrawInfo's use_legacy_bump (llvovolume.cpp): bump codes 1-17 are the legacy bump maps, without a normal map
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
        k.volume = volumeIdentity(*volume);
        k.face = (uint32_t)facep->getTEOffset();
        k.vertices = facep->getGeomCount();
        k.indices = facep->getIndicesCount();
        k.formatMask = buffer->getTypeMask();
        // the texture entry's transform reaches the texture coordinates of a face without a glTF material only
        k.gltf = gltf != nullptr;
        if (!k.gltf)
        {
            te->getScale(&k.scaleS, &k.scaleT);
            te->getOffset(&k.offsetS, &k.offsetT);
            k.rotation = te->getRotation();
        }
        // the colour and glow bytes the bake writes (getGeometryVolume), when the buffer has those streams
        if (buffer->hasDataType(LLVertexBuffer::TYPE_COLOR))
        {
            // LLColor4's conversion to bytes, as the bake converts (getGeometryVolume assigns the LLColor4 to an LLColor4U)
            LLColor4U te_color, base_color;
            te_color = te->getColor();
            if (gltf)
            {
                base_color = gltf->mBaseColor;
            }
            k.color = censusBakedColor(te_color.mV, gltf ? base_color.mV : nullptr, facep->isInAlphaPool(),
                                       mat && mat->getSpecularID().notNull(), te->getShiny());
        }
        if (buffer->hasDataType(LLVertexBuffer::TYPE_EMISSIVE))
        {
            k.glow = censusGlowByte(te->getGlow());
        }
        // the material's hash (glTF or legacy), as registerFace keyed the item on it: it carries the normal and specular
        // maps' and the glTF textures' transforms that the bake writes into the extra texture coordinates
        std::memcpy(k.material, material_id.mData, sizeof(k.material));
        k.rigged = f.rigged;
        if (f.rigged)
        {
            k.skinHash = facep->getSkinHash();
            k.textureIndex = facep->getTextureIndex();
        }
        face.key = censusKey(k);
    }
    face.face = faceIdentity(facep, obj);
    face.vertices = facep->getGeomCount();
    if (buffer)
    {
        face.vertexBytes = (uint32_t)(LLVertexBuffer::calcVertexSize(buffer->getTypeMask()) * facep->getGeomCount());
        face.indexBytes = (uint32_t)(facep->getIndicesCount() * sizeof(U16));
    }
    face.bakeNs = sBakeFace == facep ? sBakeNs : 0;
    face.exclusion = censusExclusion(f);
    face.blended = f.blended;
    return face;
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

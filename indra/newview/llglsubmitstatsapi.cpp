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

extern bool gShaderProfileFrame; // llviewerdisplay.cpp
extern bool gCubeSnapshot;       // llviewerdisplay.cpp: a reflection probe face is being rendered

namespace
{
    // ---- the compare check -------------------------------------------------------------------------------

    S32 sArmed = LLGLSubmitCompare::PASS_OFF;   // the pass(es) to compare this frame
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
            "spot shadow 0", "spot shadow 1", "post-deferred"};
        return pass >= 0 && pass < LLGLSubmitCompare::PASS_COUNT ? labels[pass] : "?";
    }

    // The main view only (a probe face reaches the same sites with other targets and would duplicate the results
    // under the main view's labels), each pass once per arming, never nested.
    bool armedFor(S32 pass)
    {
        return sArmed != LLGLSubmitCompare::PASS_OFF && !sInRun && !gCubeSnapshot && pass > 0 && pass < LLGLSubmitCompare::PASS_COUNT &&
               !sPassDone[pass] && (sArmed == LLGLSubmitCompare::PASS_ALL || sArmed == pass);
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
    LLRenderTarget* target = nullptr;
    bool restore = false;
    bool restoreFailed = false; // the second render did not start from the first one's inputs: no valid comparison
    std::vector<Capture> inputs;
    std::vector<Capture> first;
};

LLGLSubmitCompare::Run::Run(S32 pass, LLRenderTarget* target, bool restore_inputs)
{
    if (!armedFor(pass))
    {
        return;
    }
    sPassDone[pass] = true;
    if (!target || target->getUsage() != LLTexUnit::TT_TEXTURE)
    {
        PassResult skipped;
        skipped.pass = pass;
        skipped.note = "not run: no plain 2D target this frame (missing, multisampled, stochastic transparency in use, or highlight faces pending)";
        LL_INFOS(kTag) << passLabel(pass) << ": " << skipped.note << LL_ENDL;
        sResults.push_back(std::move(skipped));
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
        if (m.restore)
        {
            m.inputs = captureTarget(*m.target);
        }
        return true;
    case 1:
        m.first = captureTarget(*m.target);
        sReplay = true;
        sFadeCursor = 0;
        if (m.restore)
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
        return true;
    default:
        break;
    }

    // After the second render. The second readback is compared one attachment at a time and dropped, so the peak is
    // the first readback of the whole target plus one attachment.
    sReplay = false;
    PassResult res;
    res.pass = m.pass;
    res.ran = true;
    PixelStore store;
    const std::vector<Attach> list = attachmentsOf(*m.target);
    if (m.restoreFailed)
    {
        res.ran = false;
        res.note = "the inputs could not be restored for the second render";
    }
    else if (list.size() != m.first.size() || m.first.empty())
    {
        res.ran = false;
        res.note = "attachments could not be read back";
    }
    else
    {
        for (size_t i = 0; i < list.size(); ++i)
        {
            bool ok = false;
            Capture second = captureOf(list[i], ok);
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
        LLGLSubmitStats::windowBegin((uint32_t)frames, req.has("gpu") ? req["gpu"].asBoolean() : true);
        r["frames"] = frames;
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
        if (pass == LLGLSubmitCompare::PASS_OFF || pass < LLGLSubmitCompare::PASS_ALL || pass >= LLGLSubmitCompare::PASS_COUNT)
        {
            return r.error("pass must be -1 (all) or 1..8");
        }
        if (req.has("inject"))
        {
            gSavedSettings.setS32("RenderGLSubmitCompareInject", req["inject"].asInteger());
        }
        gSavedSettings.setS32("RenderGLSubmitCompare", pass);
        r["armed"] = pass;
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
                "Collect [\"frames\"] frames (default 60), GPU timers on unless [\"gpu\"] is false; the window is averaged per frame.",
                &api_window_begin, llsd::map("reply", LLSD()));
            add("windowStop", "Stop collecting; what was collected stays readable.", &api_window_stop, llsd::map("reply", LLSD()));
            add("summary",
                "Headline numbers of the window ([\"kind\"] \"window\", default) or of the last frame (\"frame\"), on [\"reply\"]. Small.",
                &api_summary, llsd::map("reply", LLSD()));
            add("dump",
                "Write the full JSON of the window or last frame ([\"kind\"]) to [\"path\"] (default: the viewer log folder); the path is in the reply.",
                &api_dump, llsd::map("reply", LLSD()));
            add("compareArm",
                "Arm the identical-image check for the next frame: [\"pass\"] -1 all, 1 G-buffer, 2..5 sun cascades, 6..7 spot shadows, 8 post-deferred. "
                "[\"inject\"] 1 flips one bit of the second readback to prove the check catches it.",
                &api_compare_arm, llsd::map("reply", LLSD()));
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
        // one frame, then the setting returns to 0
        sArmed = want;
        sResults.clear();
        sResultsFrame = gFrameCount;
        ++sArmSerial;
        std::fill(std::begin(sPassDone), std::end(sPassDone), false);
        gSavedSettings.setS32("RenderGLSubmitCompare", 0);
        LL_INFOS(kTag) << "armed for " << (want == LLGLSubmitCompare::PASS_ALL ? "all passes" : passLabel(want))
                       << " in frame " << gFrameCount << LL_ENDL;
    }
}

LLGLSubmitStatsApi::DisplayFrame::~DisplayFrame()
{
    if (mOwnsStats)
    {
        LLGLSubmitStats::endFrame();
    }
    if (mOwnsCompare)
    {
        if (sResults.empty())
        {
            LL_INFOS(kTag) << "the armed pass did not run in this frame" << LL_ENDL;
        }
        sArmed = LLGLSubmitCompare::PASS_OFF;
        sInRun = false;
        sReplay = false;
    }
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

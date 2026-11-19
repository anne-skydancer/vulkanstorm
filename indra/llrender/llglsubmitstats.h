// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// OpenGL submission instrumentation (development builds only, -DVKS_GL_STATS=ON).
//
// Counts, per view and pass and per frame, what the renderer asks of the GL driver: draws, buffer and
// attribute rebinding, texture binds (and how many were redundant), uniform uploads (skin palettes
// included), buffer uploads, matrix syncs, state toggles, occlusion queries, and how many consecutive draws
// differ only in their vertex buffer (the "mergeable run" length, also without the uniforms per-draw records
// would carry). GPU time per pass comes from timestamp query pairs in a four-frame ring, read without waiting.
// Census windows add the copy census (what instancing and vertex pulling could take over from the bake) and the
// draw-state keys. See doc/gl/instrumentation.md.
//
// Off (the default, and always on release): every macro below expands to nothing. On but not collecting:
// one pointer test per hook site, and one flag test where the copy census records a face and times its bake (it
// records only when switched on, LLGLSubmitStatsApi::censusRecording). Counters are exclusive (attributed to the
// innermost scope); CPU and GPU times are inclusive of nested scopes.
#pragma once

#include <cstdint>
#include <cstring>

#ifndef VKS_GL_STATS
#define VKS_GL_STATS 0
#endif

// ---------------------------------------------------------------------------------------------------
// Pure image comparison (no GL). Also compiled when the instrumentation is off so a test can use it.
// ---------------------------------------------------------------------------------------------------
namespace LLGLSubmitImage
{
    struct Diff
    {
        uint64_t pixels = 0;            // pixels compared
        uint64_t mismatchedPixels = 0;  // pixels with at least one differing channel
        uint32_t maxDiff[4] = {0, 0, 0, 0}; // per channel: |a-b| for 8 bit, ulp distance for float32
        int32_t  firstX = -1;
        int32_t  firstY = -1;
        bool identical() const { return mismatchedPixels == 0; }
    };

    // Maps a float32 bit pattern to an integer that increases with the float value, so the difference of
    // two mapped values is the distance in representable floats (+0 and -0 are one apart).
    inline uint32_t orderedFloatBits(uint32_t bits)
    {
        return (bits & 0x80000000u) ? ~bits : (bits | 0x80000000u);
    }

    // Bitwise compare of two images of 'channels' (1..4) channels each, stored interleaved row after row.
    // float32: 32 bit channels compared as bit patterns (NaNs with different payloads differ); otherwise 8 bit.
    inline Diff compare(const void* a, const void* b, uint32_t width, uint32_t height, uint32_t channels, bool float32)
    {
        Diff d;
        d.pixels = (uint64_t)width * height;
        if (channels == 0 || channels > 4)
        {
            d.mismatchedPixels = d.pixels;
            return d;
        }
        const size_t stride = float32 ? 4 : 1;
        const uint8_t* pa = static_cast<const uint8_t*>(a);
        const uint8_t* pb = static_cast<const uint8_t*>(b);
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const size_t off = ((size_t)y * width + x) * channels * stride;
                bool differs = false;
                for (uint32_t c = 0; c < channels; ++c)
                {
                    uint32_t da = 0;
                    if (float32)
                    {
                        uint32_t ba, bb;
                        std::memcpy(&ba, pa + off + c * 4, 4);
                        std::memcpy(&bb, pb + off + c * 4, 4);
                        if (ba == bb)
                        {
                            continue;
                        }
                        const uint32_t oa = orderedFloatBits(ba), ob = orderedFloatBits(bb);
                        da = oa > ob ? oa - ob : ob - oa;
                    }
                    else
                    {
                        const uint8_t ba = pa[off + c], bb = pb[off + c];
                        if (ba == bb)
                        {
                            continue;
                        }
                        da = ba > bb ? ba - bb : bb - ba;
                    }
                    differs = true;
                    if (da > d.maxDiff[c])
                    {
                        d.maxDiff[c] = da;
                    }
                }
                if (differs)
                {
                    if (d.mismatchedPixels == 0)
                    {
                        d.firstX = (int32_t)x;
                        d.firstY = (int32_t)y;
                    }
                    ++d.mismatchedPixels;
                }
            }
        }
        return d;
    }
}

// Pure helpers of the statistics (no GL), also compiled when the instrumentation is off so a test can use them.
namespace LLGLSubmitMath
{
    // Bucket of a mergeable run of 'len' draws: 1, 2, 3-4, 5-8, 9-16, 17+.
    inline int runBucket(uint32_t len)
    {
        return len <= 1 ? 0 : len == 2 ? 1 : len <= 4 ? 2 : len <= 8 ? 3 : len <= 16 ? 4 : 5;
    }

    // Mean per frame of a sum over a window.
    inline double perFrameMean(double sum, double frames)
    {
        return frames > 0.0 ? sum / frames : 0.0;
    }

    // Mergeable runs: a draw continues the run of the previous draw when it is in the same record (view/pass scope)
    // and the state epoch (bumped by every program, texture, uniform or state change that reached GL) is unchanged.
    struct RunTracker
    {
        struct Closed { const void* rec; uint32_t len; };
        const void* rec = nullptr;
        uint32_t len = 0;
        uint32_t epoch = 0;

        // Note a draw; returns the run this draw closed (len 0: none).
        Closed draw(const void* r, uint32_t e)
        {
            Closed closed{nullptr, 0};
            if (r == rec && len && e == epoch)
            {
                ++len;
            }
            else
            {
                closed = close();
                rec = r;
                len = 1;
            }
            epoch = e;
            return closed;
        }

        Closed close()
        {
            Closed closed{rec, len};
            rec = nullptr;
            len = 0;
            return closed;
        }
    };

    // The two epochs of the run trackers: every change that reached GL between two draws, and the same without the
    // changes per-draw records would carry (uniforms, skin palettes, model matrices; G6r-0's runsNoUniform).
    struct RunEpochs
    {
        uint32_t all = 0;
        uint32_t noUniform = 0;
        void stateChange() { ++all; ++noUniform; }  // a program, texture, cap or vertex format change
        void uniform() { ++all; }                   // a uniform reached GL (palettes and model matrices are uniforms)
    };

    // ---- The copy census (G5b-0, X1-0): what instancing and vertex pulling could take over from the bake ----

    // Size class of a face by its vertex count: under 64, 64-255, 256-575, 576 and more. 64 is the instancing floor
    // (one floor in every pass, research size-floor.md); 576 is where one template's long runs stop costing (N-SF1).
    constexpr int kCensusSizeClasses = 4;
    constexpr uint32_t kCensusMinVertices = 64;
    inline int censusSizeClass(uint32_t vertices)
    {
        return vertices < 64 ? 0 : vertices < 256 ? 1 : vertices < 576 ? 2 : 3;
    }

    // Bucket of a run or a set of n copies: 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65+ (runs over 16 decide N-SF1's replicas).
    constexpr int kCensusRunBuckets = 8;
    inline int censusRunBucket(uint32_t n)
    {
        return n <= 1 ? 0 : n == 2 ? 1 : n <= 4 ? 2 : n <= 8 ? 3 : n <= 16 ? 4 : n <= 32 ? 5 : n <= 64 ? 6 : 7;
    }

    // Why a face stays baked: the first rule it fails, in this order (b-11 §3.3). Size is not a rule here: eligible faces
    // are reported by size class, so the instancing floor (64) and the smaller copies vertex pulling would take both read it.
    enum CensusExclusion : uint8_t
    {
        CENSUS_ELIGIBLE = 0,
        CENSUS_NO_KEY,          // no volume or texture entry to key on
        CENSUS_HUD,             // a HUD attachment
        CENSUS_FLEXI,           // a flexible object: its vertices are computed at every update
        CENSUS_GLTF_ASSET,      // part of a glTF asset object
        CENSUS_RIGGED,          // rigged: shares buffers between avatars (G5b-1) instead; counted apart
        CENSUS_BLENDED,         // in the blended pass (its batches seed the stochastic grain)
        CENSUS_INVISIBLE,       // an invisiprim pass
        CENSUS_TEXTURE_ANIM,    // an animated texture (its texture matrix is per face)
        CENSUS_PLANAR,          // planar texture mapping (texture coordinates from positions)
        CENSUS_MEDIA,           // a media face
        CENSUS_LEGACY_BUMP,     // a legacy bump map without a normal-map material
        CENSUS_SELECTED,        // a selected object (the selection paths draw its baked faces)
        CENSUS_NUMERIC,         // a transform entry outside the exactness guard
        CENSUS_EXCLUSIONS
    };

    inline const char* censusExclusionName(int e)
    {
        static const char* const names[CENSUS_EXCLUSIONS] = {"eligible", "no_key", "hud", "flexi", "gltf_asset", "rigged",
                                                             "blended", "invisible", "texture_anim", "planar", "media",
                                                             "legacy_bump", "selected", "numeric"};
        return e >= 0 && e < CENSUS_EXCLUSIONS ? names[e] : "?";
    }

    struct CensusFacts
    {
        bool hasKey = false;
        bool hud = false;
        bool flexi = false;
        bool gltfAsset = false;
        bool rigged = false;
        bool blended = false;
        bool invisible = false;
        bool textureAnim = false;
        bool planar = false;
        bool media = false;
        bool legacyBump = false;
        bool selected = false;
        bool numbersSafe = true;
    };

    inline CensusExclusion censusExclusion(const CensusFacts& f)
    {
        return !f.hasKey ? CENSUS_NO_KEY : f.hud ? CENSUS_HUD : f.flexi ? CENSUS_FLEXI : f.gltfAsset ? CENSUS_GLTF_ASSET
             : f.rigged ? CENSUS_RIGGED : f.blended ? CENSUS_BLENDED : f.invisible ? CENSUS_INVISIBLE
             : f.textureAnim ? CENSUS_TEXTURE_ANIM : f.planar ? CENSUS_PLANAR : f.media ? CENSUS_MEDIA
             : f.legacyBump ? CENSUS_LEGACY_BUMP : f.selected ? CENSUS_SELECTED : !f.numbersSafe ? CENSUS_NUMERIC
             : CENSUS_ELIGIBLE;
    }

    // The exactness guard of the instanced transform (b-11 §3.3): finite, and zero or at least 2^-50 in magnitude, so
    // that no product or sum of the transform can be subnormal and flush-to-zero cannot matter on any vendor.
    inline bool censusNumberSafe(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        const uint32_t exponent = (bits >> 23) & 0xffu;
        const uint32_t magnitude = bits & 0x7fffffffu;
        return exponent != 0xffu && (magnitude == 0 || exponent >= 127u - 50u);
    }

    // 64-bit mixing (the splitmix64 finaliser over h + v): keys and state signatures, not cryptographic.
    inline uint64_t mix64(uint64_t h, uint64_t v)
    {
        uint64_t z = h + 0x9e3779b97f4a7c15ull + v;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    inline uint64_t mixFloat(uint64_t h, float f)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return mix64(h, bits);
    }

    // The colour bytes the bake writes into a face's colour stream (LLFace::getGeometryVolume): the texture entry's colour,
    // or a glTF material's base colour when the face has one (both already converted to bytes, as the bake converts them);
    // outside the alpha pool, without a glTF material and without a specular map, the alpha byte carries the legacy
    // shininess instead (the bake's SHININESS_TO_ALPHA). Packed as r | g << 8 | b << 16 | a << 24.
    inline uint32_t censusBakedColor(const uint8_t te[4], const uint8_t* gltfBase, bool alphaPool, bool specularMap, uint8_t shiny)
    {
        static const float kShinyToAlpha[4] = {0.0000f, 0.25f, 0.5f, 0.75f};
        uint8_t c[4];
        std::memcpy(c, gltfBase ? gltfBase : te, 4);
        if (!alphaPool && !gltfBase && !specularMap)
        {
            c[3] = (uint8_t)(kShinyToAlpha[shiny & 3] * 255);
        }
        return (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
    }

    // The glow byte the bake writes into a face's emissive stream.
    inline uint8_t censusGlowByte(float glow)
    {
        const int32_t g = (int32_t)(glow * 255);
        return (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
    }

    // What decides a face's object-space streams (b-11's LLGeomCacheKey): the shared volume (its parameters and detail
    // level, not its address) and face, the counts the bake uses, the streams of its buffer, and what else the bake writes
    // into them: the texture entry's transform (not for a glTF face: its texture coordinates are untransformed, the
    // material's transforms apply in the shader), the colour and glow bytes (b-11 §3.2's streamHash: 0 when the buffer has
    // no such stream), and the transforms of the material, legacy or glTF, through its hash. A rigged face adds its skin
    // and its texture index (the bake puts it in position.w); a static face's texture index goes in its per-copy record.
    struct CensusKeyInputs
    {
        uint64_t volume = 0;
        uint32_t face = 0;
        uint32_t vertices = 0;
        uint32_t indices = 0;
        uint32_t formatMask = 0;
        bool gltf = false;                  // a glTF face: the texture entry's transform is not in its streams
        float scaleS = 0.f, scaleT = 0.f, offsetS = 0.f, offsetT = 0.f, rotation = 0.f;
        uint32_t color = 0;                 // censusBakedColor
        uint8_t glow = 0;                   // censusGlowByte
        uint64_t material[2] = {0, 0};      // the material's 128-bit hash (glTF or legacy; 0 for none)
        bool rigged = false;
        uint64_t skinHash = 0;
        uint32_t textureIndex = 0;
    };

    // Equal inputs give equal keys; 0 is never returned (it marks "no key").
    inline uint64_t censusKey(const CensusKeyInputs& k)
    {
        uint64_t h = mix64(0x6c6c67656f6d6b79ull, k.volume);
        h = mix64(h, ((uint64_t)k.face << 32) | k.formatMask);
        h = mix64(h, ((uint64_t)k.vertices << 32) | k.indices);
        if (k.gltf)
        {
            h = mix64(h, 0x676c7466ull);
        }
        else
        {
            h = mixFloat(h, k.scaleS);
            h = mixFloat(h, k.scaleT);
            h = mixFloat(h, k.offsetS);
            h = mixFloat(h, k.offsetT);
            h = mixFloat(h, k.rotation);
        }
        h = mix64(h, ((uint64_t)k.glow << 32) | k.color);
        h = mix64(mix64(h, k.material[0]), k.material[1]);
        if (k.rigged)
        {
            h = mix64(mix64(h, 0x72696767ull), k.skinHash);
            h = mix64(h, k.textureIndex);
        }
        return h ? h : 1;
    }

    // One face of a draw item, as the census records it when the face joins the item (registerFace), in draw order.
    struct CensusFace
    {
        uint64_t key = 0;           // the geometry cache key; 0 when the face has none
        uint64_t face = 0;          // the face's identity (its object and face), to count resident bytes once over its passes
        uint32_t vertices = 0;
        uint32_t vertexBytes = 0;   // the vertices at the buffer's vertex size
        uint32_t indexBytes = 0;
        uint32_t bakeNs = 0;        // its last bake (getGeometryVolume) in ns; 0 when it was not timed
        uint8_t  exclusion = CENSUS_ELIGIBLE;
        bool     blended = false;   // registered in the blended pass (rigged blended faces share no buffer, b-11 §3.6)
        bool instanced() const { return exclusion == CENSUS_ELIGIBLE && vertices >= kCensusMinVertices; }
    };

    // One draw item under b-11's two policies: a face is instanced when it is eligible and has at least 64 vertices.
    struct CensusItem
    {
        uint32_t segments = 0;          // maximal runs of baked or of instanced faces: P0 issues one call per segment
        uint32_t sets = 0;              // maximal runs of one key among the instanced faces: one command each
        bool mixed = false;             // baked and instanced faces in one item: P1 keeps the whole item baked
        uint64_t instancedVertices = 0; // the vertices P0 instances (P1 loses them when the item is mixed)
    };

    inline CensusItem censusItem(const CensusFace* faces, size_t n)
    {
        CensusItem it;
        bool haveBaked = false, haveInstanced = false;
        for (size_t i = 0; i < n; ++i)
        {
            const bool inst = faces[i].instanced();
            if (i == 0 || inst != faces[i - 1].instanced())
            {
                ++it.segments;
            }
            if (inst)
            {
                it.instancedVertices += faces[i].vertices;
                if (i == 0 || !faces[i - 1].instanced() || faces[i - 1].key != faces[i].key)
                {
                    ++it.sets;
                }
            }
            haveBaked = haveBaked || !inst;
            haveInstanced = haveInstanced || inst;
        }
        it.mixed = haveBaked && haveInstanced;
        return it;
    }

    // Runs of one template in list order inside one scope: consecutive eligible faces with the same key (any size; the
    // closed run carries its vertices per copy for the size class). An ineligible face or a scope change ends the run.
    struct CensusRunTracker
    {
        struct Closed { const void* rec; uint32_t len; uint32_t vertices; };
        const void* rec = nullptr;
        uint64_t key = 0;
        uint32_t len = 0;
        uint32_t vertices = 0;

        // Note a face; returns the run it closed (len 0: none).
        Closed face(const void* r, const CensusFace& f)
        {
            if (f.exclusion != CENSUS_ELIGIBLE)
            {
                return close();
            }
            if (len && r == rec && f.key == key)
            {
                ++len;
                return Closed{nullptr, 0, 0};
            }
            const Closed closed = close();
            rec = r;
            key = f.key;
            len = 1;
            vertices = f.vertices;
            return closed;
        }

        Closed close()
        {
            const Closed closed{rec, len, vertices};
            rec = nullptr;
            key = 0;
            len = 0;
            vertices = 0;
            return closed;
        }
    };

    // ---- The identical-image check's arming and the frames that own it (LLGLSubmitStatsApi's frame glue drives it) ----

    // Compare ids (LLGLSubmitCompare::PASS_*): 1-8 and 13-15 have a site; 9-12 are reserved for G0b.
    constexpr int kCompareIds = 16;
    inline bool compareImplemented(int pass)
    {
        return (pass >= 1 && pass <= 8) || (pass >= 13 && pass <= 15);
    }

    // An arming begins in the main loop's display() frame and stays open for up to N such frames (compareArm "frames")
    // until every pass it asked for has run; a pass is compared only while a frame owns the arming, each pass once. The
    // main loop's frame holds the end of display()'s frame until after the reflection probe update, so compare 13 reaches
    // a reflection face. A display() outside it (the snapshot floaters' after its end, display_startup() from the startup
    // code, a 360 capture's) gets a statistics frame of its own but neither begins, carries nor counts toward an arming,
    // and nothing is compared outside an owning frame (a 360 capture's cube faces).
    struct CompareFrames
    {
        int armed = 0;              // 0 off, -1 every pass, else one compare id
        int frames = 0;             // the open arming's frames
        int framesLeft = 0;
        int pendingFrames = 1;      // the frames of the next arming (compareArm "frames"; the debug setting arms for 1)
        bool owned = false;         // a frame that owns the arming is open
        bool done[kCompareIds] = {};
        bool mainLoopOpen = false;  // the main loop's frame is open ...
        bool mainLoopHolds = false; // ... and holds the ends of the frame its display() began
        bool heldStats = false;
        bool heldCompare = false;

        enum Begin { BEGIN_NONE, BEGIN_NEW, BEGIN_CARRIED };

        // The open arming asked for this pass.
        bool asked(int pass) const
        {
            return armed != 0 && compareImplemented(pass) && (armed == -1 || armed == pass);
        }

        // The pass may be compared now: asked for, not compared yet in this arming, and a frame owns the arming.
        bool wants(int pass) const { return owned && asked(pass) && !done[pass]; }

        bool allDone() const
        {
            for (int p = 1; p < kCompareIds; ++p)
            {
                if (asked(p) && !done[p])
                {
                    return false;
                }
            }
            return true;
        }

        // MainLoopFrame's constructor: true when it opened the main loop's frame (false: nested, it leaves it alone).
        bool mainLoopBegin()
        {
            if (mainLoopOpen)
            {
                return false;
            }
            mainLoopOpen = true;
            mainLoopHolds = false;
            return true;
        }

        // DisplayFrame's constructor. want: the RenderGLSubmitCompare setting (0: none); snapshot: gSnapshot. BEGIN_NEW: this
        // frame began an arming (the caller clears the results and the setting); BEGIN_CARRIED: it carries an open arming
        // on. Either way the frame owns the arming until compareEnd().
        Begin displayBegin(int want, bool snapshot)
        {
            if (!mainLoopOpen || mainLoopHolds || snapshot)
            {
                return BEGIN_NONE;
            }
            if (want != 0 && armed == 0)
            {
                armed = want;
                frames = framesLeft = pendingFrames > 1 ? pendingFrames : 1;
                pendingFrames = 1;
                owned = true;
                for (bool& d : done)
                {
                    d = false;
                }
                return BEGIN_NEW;
            }
            if (armed != 0 && !owned)
            {
                owned = true;
                return BEGIN_CARRIED;
            }
            return BEGIN_NONE;
        }

        // DisplayFrame's destructor: true when the main loop's frame takes over the ends this frame owns (mainLoopEnd hands
        // them back after the reflection probe update); false: the caller ends them now.
        bool displayEnd(bool ownsStats, bool ownsCompare)
        {
            if (!mainLoopOpen || mainLoopHolds || !(ownsStats || ownsCompare))
            {
                return false;
            }
            mainLoopHolds = true;
            heldStats = ownsStats;
            heldCompare = ownsCompare;
            return true;
        }

        // MainLoopFrame::end of the frame mainLoopBegin opened: the ends it held, to end now (none: false).
        bool mainLoopEnd(bool& ownsStats, bool& ownsCompare)
        {
            ownsStats = ownsCompare = false;
            mainLoopOpen = false;
            if (!mainLoopHolds)
            {
                return false;
            }
            mainLoopHolds = false;
            ownsStats = heldStats;
            ownsCompare = heldCompare;
            heldStats = heldCompare = false;
            return true;
        }

        // The end of a frame that owned the arming: true when the arming is over (every pass it asked for has run, or its
        // frames are used up). The caller then reports the passes still missing (asked, not done) and calls disarm().
        bool compareEnd()
        {
            owned = false;
            return --framesLeft <= 0 || allDone();
        }

        void disarm()
        {
            armed = 0;
            framesLeft = 0;
            owned = false;
        }
    };
}

#if VKS_GL_STATS

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <boost/json.hpp>

// One entry per counter: the struct field, the JSON key, and the sum over the records all use this list.
#define VKS_GLSTAT_COUNTER_LIST(X) \
    X(draws)              /* glDraw* calls (drawRange, drawRangeFast, drawArrays) */ \
    X(drawIndices)        /* indices (or array vertices) submitted */ \
    X(drawTriangles)      /* triangles submitted (GL_TRIANGLES draws only) */ \
    X(drawArraysCalls)    /* of draws: glDrawArrays */ \
    X(vbSetBuffer)        /* LLVertexBuffer::setBuffer calls */ \
    X(vbChanges)          /* ... of which rebound the array buffer */ \
    X(attribRespecs)      /* setupVertexBuffer calls (a full attribute respecification) */ \
    X(attribPointers)     /* glVertexAttrib(I)Pointer calls */ \
    X(iboBinds)           /* element array buffer binds in setBuffer */ \
    X(shaderBinds)        /* LLGLSLShader::bind that changed the program */ \
    X(attribToggles)      /* glEnable/DisableVertexAttribArray */ \
    X(texBindFast)        /* LLTexUnit::bindFast (unconditional glBindTexture) */ \
    X(texBindFastRedundant) /* ... of which the name was already on the unit */ \
    X(texBindOther)       /* bind/bindManual calls that reached glBindTexture */ \
    X(texBindOtherFiltered) /* bind/bindManual calls skipped by the unit cache */ \
    X(texGLBind)          /* glBindTexture calls issued by LLTexUnit */ \
    X(texGLActive)        /* glActiveTexture calls issued by LLTexUnit */ \
    X(uniformRequests)    /* LLGLSLShader::uniform* calls */ \
    X(uniformGL)          /* glUniform* calls that reached GL */ \
    X(uniformArrayGL)     /* ... with count > 1 */ \
    X(uniformMatrixGL)    /* ... matrices (incl. palettes) */ \
    X(uniformBytes)       /* bytes uploaded by glUniform* */ \
    X(matrixSyncs)        /* syncMatrices calls with a shader bound */ \
    X(mdvSyncs)           /* modelview branch taken (normal, MVP, inverse recomputed or uploaded) */ \
    X(projSyncs)          /* projection branch taken */ \
    X(inverseMatrices)    /* glm::inverse evaluations in syncMatrices */ \
    X(modelMatrixCalls)   /* LLRenderPass::applyModelMatrix calls */ \
    X(modelMatrixChanges) /* ... that changed the matrix pointer */ \
    X(paletteUploads)     /* skin palette uploads (glUniformMatrix3x4fv) */ \
    X(paletteBytes)       /* bytes in those uploads */ \
    X(paletteDistinct)    /* distinct (avatar, skin) pairs uploaded (derived) */ \
    X(vbosDistinct)       /* distinct array buffers set (derived) */ \
    X(bufAllocHit)        /* VBO pool allocation served from the pool */ \
    X(bufAllocMiss)       /* ... missed: glBufferData */ \
    X(bufSubData)         /* glBufferSubData calls */ \
    X(bufSubDataBytes)    /* bytes in those calls */ \
    X(bufAllocMissBytes)  /* bytes allocated by those glBufferData calls */ \
    X(flushBuffers)       /* LLVertexBuffer::flushBuffers calls */ \
    X(stateToggles)       /* glEnable/glDisable that reached GL through LLGLState */ \
    X(occIssued)          /* occlusion queries issued */ \
    X(occPolled)          /* occlusion results polled */ \
    X(occBlocking)        /* occlusion results read with a blocking GL_QUERY_RESULT */ \
    X(gltfBinds)          /* LLFetchedGLTFMaterial::bind calls */ \
    X(gltfSameAsLast)     /* ... for the same material as the previous call */ \
    X(vbFormatChanges)    /* setBuffer of a buffer with other vertex streams than the previous one (a run breaker) */ \
    X(texUnbinds)         /* LLTexUnit::unbind / unbindFast calls that changed the unit's binding (a run breaker) */ \
    X(texMatrixLoads)     /* texture matrices loaded (an animated texture's matrix) */ \
    X(censusFaces)        /* copy census records made (registerFace while the census records) */ \
    X(runs)               /* mergeable runs: draws separated by no shader, texture, uniform or state change */ \
    X(mergeableDraws)     /* draws - runs: draws that could join the previous draw's run */ \
    X(runsNoUniform)      /* runs if uniforms, palettes and model matrices came from per-draw records (G6r-0) */ \
    X(mergeableDrawsNoUniform) /* draws - runsNoUniform */ \
    X(stateKeysDistinct)  /* census windows: distinct draw-state keys, the buckets a by-state sort would form (derived) */

struct LLGLSubmitCounters
{
#define VKS_GLSTAT_FIELD(name) uint64_t name = 0;
    VKS_GLSTAT_COUNTER_LIST(VKS_GLSTAT_FIELD)
#undef VKS_GLSTAT_FIELD
    uint64_t runHist[6] = {0, 0, 0, 0, 0, 0}; // runs of length 1, 2, 3-4, 5-8, 9-16, 17+
    uint64_t runHistNoUniform[6] = {0, 0, 0, 0, 0, 0}; // the same for runsNoUniform
    void add(const LLGLSubmitCounters& o);
};

// The copy census of a scope (census windows only): the faces its draws drew, what instancing (64 vertices and more) and
// vertex pulling (smaller copies) could take over, and how the two eligibility policies of b-11 §3.3 would issue them.
// A face drawn by several draws counts at each.
struct LLGLSubmitCensus
{
    static constexpr int kSizes = LLGLSubmitMath::kCensusSizeClasses;
    static constexpr int kRuns = LLGLSubmitMath::kCensusRunBuckets;
    static constexpr int kReasons = LLGLSubmitMath::CENSUS_EXCLUSIONS;
    uint64_t faces = 0;
    uint64_t vertices = 0;
    uint64_t reasonFaces[kReasons] = {};        // by exclusion reason; 0 is the eligible faces
    uint64_t reasonVertices[kReasons] = {};
    uint64_t sizeFaces[kSizes] = {};            // eligible faces by size class (under 64, 64-255, 256-575, 576+)
    uint64_t sizeVertices[kSizes] = {};
    uint64_t sizeBytes[kSizes] = {};            // the baked bytes they hold
    uint64_t sizeBakeNs[kSizes] = {};           // and their last bake time
    uint64_t items = 0;                         // draw items with census records
    uint64_t instancedItems = 0;                // ... with an instanced face (eligible, 64 vertices or more)
    uint64_t mixedItems = 0;                    // ... and a baked one
    uint64_t sets = 0;                          // instanced sets: runs of one key inside items (one command each)
    uint64_t p0ExtraCalls = 0;                  // policy P0: calls over today's one per item (segments - 1)
    uint64_t p0Switches = 0;                    // P0: baked/instanced changes in list order (program and record switches)
    uint64_t p1LostVertices = 0;                // policy P1: instanced vertices of mixed items, which stay baked
    uint64_t runHist[kSizes][kRuns] = {};       // strict order: runs of one key in list order, by size class and length
    uint64_t runVertices[kSizes][kRuns] = {};   // ... the vertices those runs draw (copies times vertices per copy)
    uint64_t relaxedHist[kSizes][kRuns] = {};   // relaxed order (depth-only views): copies of one key per scope and frame
    void add(const LLGLSubmitCensus& o);
};

class LLGLSubmitStats
{
public:
    struct Record
    {
        std::string view;               // "frame", "main", "sun cascade 2", ...
        std::string pass;               // "" for the view itself, else the path of pass scopes inside the view
        LLGLSubmitCounters c;
        uint64_t calls = 0;             // times the scope was entered
        double cpuMs = 0.0;             // inclusive
        double gpuMs = 0.0;             // inclusive; valid when the frame's GPU results were read
        uint64_t samples = 0;           // GL_SAMPLES_PASSED of the alpha passes (inclusive); valid with gpuMs
        LLGLSubmitCensus census;
        std::unordered_set<uint64_t> palettes;
        std::unordered_set<uint32_t> vbos;
        std::unordered_set<uint64_t> stateKeys;                             // census windows
        std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> censusKeys; // census windows: key -> (copies, vertices)
    };

    struct Frame
    {
        uint64_t number = 0;
        double cpuMs = 0.0;
        bool gpuTimed = false;
        bool gpuResolved = false;
        bool gpuTruncated = false;
        bool inWindow = false;      // counted into the current window
        bool census = false;        // ... a census window: the copy census and the draw-state keys are counted
        std::vector<std::unique_ptr<Record>> records;
        std::unordered_map<std::string, Record*> byKey;
        struct Inst { Record* rec; uint32_t begin; uint32_t end; };
        std::vector<Inst> insts;
        struct SampleInst { Record* rec; uint32_t query; };
        std::vector<SampleInst> sampleInsts;    // GL_SAMPLES_PASSED queries of the alpha passes
        std::unordered_set<uint64_t> palettesAll;
    };

    // The record that hooks add to; null when not collecting. Relaxed atomics: hooks may run on any thread (they only
    // count on the main one) while the main thread starts and ends scopes.
    static Record* cur() { return sCurRec.load(std::memory_order_relaxed); }
    static void setCur(Record* r) { sCurRec.store(r, std::memory_order_relaxed); }
    // Set once, on the thread that starts frames (the main thread): a thread-local flag is a plain load, where comparing
    // std::this_thread::get_id() is a call into the runtime.
    static inline thread_local bool sIsMain = false;
    static bool onMain() { return sIsMain; }
    // A hook on the main thread of a collecting frame.
    static bool active() { return cur() != nullptr && onMain(); }

    // A frame: collect == false makes every hook a no-op for this frame. Returns true when this call started the frame
    // (and so owns its end); false when a frame is already open (a nested display call), which it leaves alone.
    static bool beginFrame(bool collect, bool gpu);
    static void endFrame();   // idempotent
    static bool collecting() { return cur() != nullptr; }

    // Scopes. A view starts a new group of passes; a pass is a path element inside the current view.
    static void enterView(const char* name, int index);
    static void enterPass(const char* name, int index);
    static void leave();

    // Hooks with logic beyond a counter bump.
    static void noteDraw(uint32_t indices);                 // after the state of the draw was set
    // A change that ends a run with and without per-draw records: program, texture, cap, vertex format.
    static void noteStateChange() { if (active()) { sEpochs.stateChange(); } }
    static void noteCapToggle(uint32_t cap);                // an LLGLState toggle: a state change, and the census's cap signature
    static void notePalette(const void* avatar, uint64_t skinHash, uint32_t bytes);
    static void noteVBO(uint32_t name, uint32_t format);    // format: the buffer's vertex streams (type mask)
    static void noteUniform(uint32_t bytes, uint32_t count, bool matrix);
    static LLGLSubmitMath::RunEpochs sEpochs;

    // The copy census: draws of items with census records, credited in census windows only.
    static bool censusActive() { return sWindowCensus && windowActive() && active(); }
    static void noteCensusDraw(const LLGLSubmitMath::CensusFace* faces, size_t count);

    // Windows of N frames, averaged: what the LEAP client reads. A census window also counts the copy census and the
    // draw-state keys, which cost about a hash and a set insertion per draw and per face.
    static void windowBegin(uint32_t frames, bool gpu, bool census = false);
    static void windowStop();
    static bool windowActive() { return sWindowTarget != 0 && sWindowCollected < sWindowTarget; }
    static bool windowWantsGpu() { return sWindowGpu; }
    static bool windowCensus() { return sWindowCensus; }
    static uint32_t windowTarget() { return sWindowTarget; }
    static uint32_t windowCollected() { return sWindowCollected; }
    static uint32_t windowGpuResolved() { return sWindowGpuFrames; }

    // Results. 'blockForGpu' waits for the GPU timers of the frames in flight (profile frames, tests).
    static void resolveGpu(bool blockForGpu);
    // The GL context is going away or was made again: the query objects are gone. Deletes them while 'contextValid'.
    static void resetGL(bool contextValid);
    static void restoreLast(const std::shared_ptr<Frame>& f) { sLast = f; }
    static std::shared_ptr<Frame> lastFrame() { return sLast; }
    static boost::json::object frameToJson(const Frame& f);
    static boost::json::object windowToJson();      // per-frame means over the window
    static boost::json::object summaryOfFrame(const Frame& f);
    static boost::json::object summaryOfWindow();   // small: totals and views only

    // RAII for the hooks.
    struct FrameScope
    {
        FrameScope(bool collect, bool gpu) : mOwns(beginFrame(collect, gpu)) {}
        ~FrameScope() { if (mOwns) { endFrame(); } }
        bool mOwns;
    };
    struct ViewScope
    {
        explicit ViewScope(const char* name, int index = -1) { if (active()) { enterView(name, index); mOn = true; } }
        ~ViewScope() { if (mOn) { leave(); } }
        bool mOn = false;
    };
    struct PassScope
    {
        explicit PassScope(const char* name, int index = -1) { if (active()) { enterPass(name, index); mOn = true; } }
        ~PassScope() { if (mOn) { leave(); } }
        bool mOn = false;
    };

private:
    static std::atomic<Record*> sCurRec;
    static std::shared_ptr<Frame> sLast;
    static uint32_t sWindowTarget, sWindowCollected, sWindowGpuFrames;
    static bool sWindowGpu;
    static bool sWindowCensus;
    static void closeRun();
    static void dropOldest();
};

#define VKS_GLSTAT_CAT2(a, b) a##b
#define VKS_GLSTAT_CAT(a, b) VKS_GLSTAT_CAT2(a, b)
// Bump a counter of the current record.
#define VKS_GLSTAT(field, n) do { LLGLSubmitStats::Record* vks_glstat_rec_ = LLGLSubmitStats::cur(); if (vks_glstat_rec_ && LLGLSubmitStats::onMain()) { vks_glstat_rec_->c.field += (n); } } while (0)
#define VKS_GLSTAT_INC(field) VKS_GLSTAT(field, 1)
// A change of GL state between draws: ends a mergeable run.
#define VKS_GLSTAT_STATE_CHANGE() LLGLSubmitStats::noteStateChange()
#define VKS_GLSTAT_VIEW(...) LLGLSubmitStats::ViewScope VKS_GLSTAT_CAT(vks_glstat_view_, __LINE__)(__VA_ARGS__)
#define VKS_GLSTAT_PASS(...) LLGLSubmitStats::PassScope VKS_GLSTAT_CAT(vks_glstat_pass_, __LINE__)(__VA_ARGS__)
#define VKS_GLSTAT_DRAW(indices) do { if (LLGLSubmitStats::active()) { LLGLSubmitStats::noteDraw(indices); } } while (0)
#define VKS_GLSTAT_PALETTE(avatar, skinhash, bytes) do { if (LLGLSubmitStats::active()) { LLGLSubmitStats::notePalette(avatar, skinhash, bytes); } } while (0)
#define VKS_GLSTAT_VBO(name, format) do { if (LLGLSubmitStats::active()) { LLGLSubmitStats::noteVBO((name), (format)); } } while (0)
#define VKS_GLSTAT_UNIFORM(bytes, count, matrix) do { if (LLGLSubmitStats::active()) { LLGLSubmitStats::noteUniform((uint32_t)(bytes), (uint32_t)(count), (matrix)); } } while (0)
// An LLGLState toggle that reached GL: ends a run, and enters the census's cap signature.
#define VKS_GLSTAT_CAP_TOGGLE(cap) do { if (LLGLSubmitStats::active()) { LLGLSubmitStats::noteCapToggle((uint32_t)(cap)); } } while (0)
// A texture unit rebound to nothing (unbind, unbindFast) while a texture was on it: ends a run, as a bind does. An unbind
// of a unit that holds nothing changes nothing and is not counted, as a redundant bind is not.
#define VKS_GLSTAT_TEX_UNBIND(was_bound) do { if ((was_bound) && LLGLSubmitStats::active()) { VKS_GLSTAT_INC(texUnbinds); LLGLSubmitStats::noteStateChange(); } } while (0)
// A draw of an item with census records (an LLDrawInfo's mGLSubmitCensus): credited in census windows only.
#define VKS_GLSTAT_CENSUS_DRAW(info) do { if (LLGLSubmitStats::censusActive()) { LLGLSubmitStats::noteCensusDraw((info).mGLSubmitCensus.data(), (info).mGLSubmitCensus.size()); } } while (0)
#define VKS_GLSTAT_ACTIVE() (LLGLSubmitStats::active())
#define VKS_GLSTAT_RESET_GL(valid) LLGLSubmitStats::resetGL(valid)

#else // !VKS_GL_STATS

#define VKS_GLSTAT(field, n) do { } while (0)
#define VKS_GLSTAT_INC(field) do { } while (0)
#define VKS_GLSTAT_STATE_CHANGE() do { } while (0)
#define VKS_GLSTAT_VIEW(...) do { } while (0)
#define VKS_GLSTAT_PASS(...) do { } while (0)
#define VKS_GLSTAT_DRAW(indices) do { } while (0)
#define VKS_GLSTAT_PALETTE(avatar, skinhash, bytes) do { } while (0)
#define VKS_GLSTAT_VBO(name, format) do { } while (0)
#define VKS_GLSTAT_UNIFORM(bytes, count, matrix) do { } while (0)
#define VKS_GLSTAT_CAP_TOGGLE(cap) do { } while (0)
#define VKS_GLSTAT_TEX_UNBIND(was_bound) do { } while (0)
#define VKS_GLSTAT_CENSUS_DRAW(info) do { } while (0)
#define VKS_GLSTAT_ACTIVE() (false)
#define VKS_GLSTAT_RESET_GL(valid) do { } while (0)

#endif // VKS_GL_STATS

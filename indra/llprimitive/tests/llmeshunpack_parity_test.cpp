/**
 * @file llmeshunpack_parity_test.cpp
 * @brief Bit-level parity of mesh asset unpacking against a recorded corpus.
 *
 * The corpus (tests/fixtures/llmeshunpack_corpus.vksa) holds mesh assets
 * packed by the viewer's own upload packer (LLModel::writeModel from LLModel
 * instances of prim shapes at every LOD, with skin weights, skin info,
 * convex decompositions and physics shapes) and, for each, what the C++
 * unpackers produced before the port to Rust (vkstorm-rust): every
 * LLVolumeFace buffer of every LOD and of the physics mesh after
 * LLVolume::unpackVolumeFaces (positions, normals, texture coordinates,
 * indices, weights, tangents, extents), the LLMeshSkinInfo and the
 * LLModel::Decomposition of the asset. The test rebuilds all of them and
 * compares bit for bit. Each LOD is recorded twice: as the unpacker leaves
 * it (the crate's own corpus test compares this) and after the vertex
 * cache optimisation and tangent generation that follow in C++.
 *
 * VKS_MESH_RECORD=<file>   writes the corpus instead of comparing
 * VKS_MESH_BENCH=<n>       times the unpacking of the largest LODs
 * VKS_MESH_BENCH_OUT=<csv> appends the timings to a CSV file
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "../llmodel.h"
#include "llvolume.h"
#include "llvolumemgr.h"
#include "llsdserialize.h"
#include "llmatrix4a.h"
#include "llvector4a.h"

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef VKS_MESH_FIXTURE_DIR
#error VKS_MESH_FIXTURE_DIR must name the directory of the recorded corpus
#endif
#define VKS_MESH_FIXTURE VKS_MESH_FIXTURE_DIR "/llmeshunpack_corpus.vksa"

// llmodel.cpp reaches for two viewer symbols (a model-name helper and the
// settings) that nothing here exercises.
#include "llcontrol.h"
LLControlGroup gSavedSettings("Global");
std::string stripSuffix(std::string name) { return name; }

namespace
{
    using tut::ensure;
    using tut::ensure_equals;

    // ---- byte sinks and sources ------------------------------------------------

    struct Writer
    {
        std::vector<U8> b;
        void u8(U8 v) { b.push_back(v); }
        void u32(U32 v) { const U8* p = (const U8*)&v; b.insert(b.end(), p, p + 4); }
        void i32(S32 v) { u32((U32)v); }
        void u64(U64 v) { const U8* p = (const U8*)&v; b.insert(b.end(), p, p + 8); }
        void f32(F32 v) { const U8* p = (const U8*)&v; b.insert(b.end(), p, p + 4); }
        void bytes(const void* p, size_t n) { u32((U32)n); b.insert(b.end(), (const U8*)p, (const U8*)p + n); }
        void str(const std::string& s) { bytes(s.data(), s.size()); }
    };

    struct Reader
    {
        const std::vector<U8>& b;
        size_t pos;
        bool ok;
        explicit Reader(const std::vector<U8>& data) : b(data), pos(0), ok(true) {}
        bool need(size_t n) { if (pos + n > b.size()) { ok = false; return false; } return true; }
        U8 u8() { if (!need(1)) return 0; return b[pos++]; }
        U32 u32() { if (!need(4)) return 0; U32 v; memcpy(&v, &b[pos], 4); pos += 4; return v; }
        S32 i32() { return (S32)u32(); }
        U64 u64() { if (!need(8)) return 0; U64 v; memcpy(&v, &b[pos], 8); pos += 8; return v; }
        F32 f32() { if (!need(4)) return 0; F32 v; memcpy(&v, &b[pos], 4); pos += 4; return v; }
        std::vector<U8> bytes() { U32 n = u32(); std::vector<U8> v; if (!need(n)) return v; v.assign(b.begin() + pos, b.begin() + pos + n); pos += n; return v; }
        std::string str() { std::vector<U8> v = bytes(); return std::string(v.begin(), v.end()); }
    };

    U64 fnv1a(const void* data, size_t n)
    {
        const U8* p = (const U8*)data;
        U64 h = 1469598103934665603ULL;
        for (size_t i = 0; i < n; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    // ---- the recorded shape of one unpacked face ------------------------------

    const S32 FULL_BUFFER_LIMIT = 300; // faces up to this many vertices carry their buffers in full

    struct FaceRecord
    {
        S32 num_vertices = 0;
        S32 num_indices = 0;
        bool has_weights = false;
        bool has_tangents = false;
        U64 hash_positions = 0, hash_normals = 0, hash_texcoords = 0, hash_indices = 0, hash_weights = 0, hash_tangents = 0;
        F32 extents[8] = {};
        F32 tc_extents[4] = {};
        F32 normalized_scale[3] = {};
        std::vector<U8> positions, normals, texcoords, indices, weights, tangents; // full when small

        bool operator==(const FaceRecord& o) const
        {
            return num_vertices == o.num_vertices && num_indices == o.num_indices && has_weights == o.has_weights
                && has_tangents == o.has_tangents && hash_positions == o.hash_positions && hash_normals == o.hash_normals
                && hash_texcoords == o.hash_texcoords && hash_indices == o.hash_indices && hash_weights == o.hash_weights
                && hash_tangents == o.hash_tangents && memcmp(extents, o.extents, sizeof(extents)) == 0
                && memcmp(tc_extents, o.tc_extents, sizeof(tc_extents)) == 0
                && memcmp(normalized_scale, o.normalized_scale, sizeof(normalized_scale)) == 0
                && positions == o.positions && normals == o.normals && texcoords == o.texcoords && indices == o.indices
                && weights == o.weights && tangents == o.tangents;
        }
    };

    FaceRecord record_face(const LLVolumeFace& f)
    {
        FaceRecord r;
        r.num_vertices = f.mNumVertices;
        r.num_indices = f.mNumIndices;
        r.has_weights = f.mWeights != NULL;
        r.has_tangents = f.mTangents != NULL;
        size_t vbytes = (size_t)f.mNumVertices * 16;
        size_t tbytes = (size_t)f.mNumVertices * 8;
        size_t ibytes = (size_t)f.mNumIndices * 2;
        if (f.mPositions && f.mNumVertices > 0)
        {
            r.hash_positions = fnv1a(f.mPositions, vbytes);
            r.hash_normals = fnv1a(f.mNormals, vbytes);
            r.hash_texcoords = fnv1a(f.mTexCoords, tbytes);
        }
        if (f.mIndices && f.mNumIndices > 0)
        {
            r.hash_indices = fnv1a(f.mIndices, ibytes);
        }
        if (f.mWeights && f.mNumVertices > 0)
        {
            r.hash_weights = fnv1a(f.mWeights, vbytes);
        }
        if (f.mTangents && f.mNumVertices > 0)
        {
            r.hash_tangents = fnv1a(f.mTangents, vbytes);
        }
        memcpy(r.extents, f.mExtents, sizeof(r.extents));
        memcpy(r.tc_extents, f.mTexCoordExtents, sizeof(r.tc_extents));
        memcpy(r.normalized_scale, f.mNormalizedScale.mV, sizeof(r.normalized_scale));
        if (f.mNumVertices <= FULL_BUFFER_LIMIT)
        {
            if (f.mPositions && f.mNumVertices > 0)
            {
                r.positions.assign((const U8*)f.mPositions, (const U8*)f.mPositions + vbytes);
                r.normals.assign((const U8*)f.mNormals, (const U8*)f.mNormals + vbytes);
                r.texcoords.assign((const U8*)f.mTexCoords, (const U8*)f.mTexCoords + tbytes);
            }
            if (f.mIndices && f.mNumIndices > 0)
            {
                r.indices.assign((const U8*)f.mIndices, (const U8*)f.mIndices + ibytes);
            }
            if (f.mWeights && f.mNumVertices > 0)
            {
                r.weights.assign((const U8*)f.mWeights, (const U8*)f.mWeights + vbytes);
            }
            if (f.mTangents && f.mNumVertices > 0)
            {
                r.tangents.assign((const U8*)f.mTangents, (const U8*)f.mTangents + vbytes);
            }
        }
        return r;
    }

    void write_face(Writer& w, const FaceRecord& r)
    {
        w.i32(r.num_vertices);
        w.i32(r.num_indices);
        w.u8(r.has_weights);
        w.u8(r.has_tangents);
        w.u64(r.hash_positions); w.u64(r.hash_normals); w.u64(r.hash_texcoords);
        w.u64(r.hash_indices); w.u64(r.hash_weights); w.u64(r.hash_tangents);
        for (F32 v : r.extents) w.f32(v);
        for (F32 v : r.tc_extents) w.f32(v);
        for (F32 v : r.normalized_scale) w.f32(v);
        w.bytes(r.positions.data(), r.positions.size());
        w.bytes(r.normals.data(), r.normals.size());
        w.bytes(r.texcoords.data(), r.texcoords.size());
        w.bytes(r.indices.data(), r.indices.size());
        w.bytes(r.weights.data(), r.weights.size());
        w.bytes(r.tangents.data(), r.tangents.size());
    }

    FaceRecord read_face(Reader& rd)
    {
        FaceRecord r;
        r.num_vertices = rd.i32();
        r.num_indices = rd.i32();
        r.has_weights = rd.u8() != 0;
        r.has_tangents = rd.u8() != 0;
        r.hash_positions = rd.u64(); r.hash_normals = rd.u64(); r.hash_texcoords = rd.u64();
        r.hash_indices = rd.u64(); r.hash_weights = rd.u64(); r.hash_tangents = rd.u64();
        for (F32& v : r.extents) v = rd.f32();
        for (F32& v : r.tc_extents) v = rd.f32();
        for (F32& v : r.normalized_scale) v = rd.f32();
        r.positions = rd.bytes();
        r.normals = rd.bytes();
        r.texcoords = rd.bytes();
        r.indices = rd.bytes();
        r.weights = rd.bytes();
        r.tangents = rd.bytes();
        return r;
    }

    // ---- the recorded skin info and decomposition ------------------------------

    struct SkinRecord
    {
        bool present = false;
        bool ok = false;
        std::vector<std::string> joint_names;
        std::vector<S32> joint_nums;
        std::vector<U8> inv_bind, alt_bind, bind_shape, bind_pose;
        F32 pelvis_offset = 0.f;
        bool lock_scale = false;
        U64 hash = 0;

        bool operator==(const SkinRecord& o) const
        {
            return present == o.present && ok == o.ok && joint_names == o.joint_names && joint_nums == o.joint_nums
                && inv_bind == o.inv_bind && alt_bind == o.alt_bind && bind_shape == o.bind_shape && bind_pose == o.bind_pose
                && memcmp(&pelvis_offset, &o.pelvis_offset, 4) == 0 && lock_scale == o.lock_scale && hash == o.hash;
        }
    };

    void mats(std::vector<U8>& out, const std::vector<LLMatrix4a>& m)
    {
        for (const LLMatrix4a& x : m)
        {
            const U8* p = (const U8*)x.mMatrix;
            out.insert(out.end(), p, p + 64);
        }
    }

    SkinRecord record_skin(const LLMeshSkinInfo& s, bool present, bool ok)
    {
        SkinRecord r;
        r.present = present;
        r.ok = ok;
        r.joint_names = s.mJointNames;
        r.joint_nums = s.mJointNums;
        mats(r.inv_bind, s.mInvBindMatrix);
        mats(r.alt_bind, s.mAlternateBindMatrix);
        mats(r.bind_pose, s.mBindPoseMatrix);
        const U8* p = (const U8*)s.mBindShapeMatrix.mMatrix;
        r.bind_shape.assign(p, p + 64);
        r.pelvis_offset = s.mPelvisOffset;
        r.lock_scale = s.mLockScaleIfJointPosition;
        r.hash = s.mHash;
        return r;
    }

    void write_skin(Writer& w, const SkinRecord& r)
    {
        w.u8(r.present);
        w.u8(r.ok);
        w.u32((U32)r.joint_names.size());
        for (const std::string& n : r.joint_names) w.str(n);
        w.u32((U32)r.joint_nums.size());
        for (S32 n : r.joint_nums) w.i32(n);
        w.bytes(r.inv_bind.data(), r.inv_bind.size());
        w.bytes(r.alt_bind.data(), r.alt_bind.size());
        w.bytes(r.bind_shape.data(), r.bind_shape.size());
        w.bytes(r.bind_pose.data(), r.bind_pose.size());
        w.f32(r.pelvis_offset);
        w.u8(r.lock_scale);
        w.u64(r.hash);
    }

    SkinRecord read_skin(Reader& rd)
    {
        SkinRecord r;
        r.present = rd.u8() != 0;
        r.ok = rd.u8() != 0;
        U32 n = rd.u32();
        for (U32 i = 0; i < n && rd.ok; ++i) r.joint_names.push_back(rd.str());
        n = rd.u32();
        for (U32 i = 0; i < n && rd.ok; ++i) r.joint_nums.push_back(rd.i32());
        r.inv_bind = rd.bytes();
        r.alt_bind = rd.bytes();
        r.bind_shape = rd.bytes();
        r.bind_pose = rd.bytes();
        r.pelvis_offset = rd.f32();
        r.lock_scale = rd.u8() != 0;
        r.hash = rd.u64();
        return r;
    }

    struct DecompRecord
    {
        bool present = false;
        bool ok = false;
        std::vector<std::vector<U8> > hulls; // each: the LLVector3 bytes
        std::vector<U8> base_hull;
        bool base_mesh_cleared = false;

        bool operator==(const DecompRecord& o) const
        {
            return present == o.present && ok == o.ok && hulls == o.hulls && base_hull == o.base_hull && base_mesh_cleared == o.base_mesh_cleared;
        }
    };

    std::vector<U8> vec3s(const std::vector<LLVector3>& v)
    {
        std::vector<U8> out;
        for (const LLVector3& p : v)
        {
            const U8* b = (const U8*)p.mV;
            out.insert(out.end(), b, b + 12);
        }
        return out;
    }

    DecompRecord record_decomp(const LLModel::Decomposition& d, bool present, bool ok)
    {
        DecompRecord r;
        r.present = present;
        r.ok = ok;
        for (const auto& h : d.mHull) r.hulls.push_back(vec3s(h));
        r.base_hull = vec3s(d.mBaseHull);
        r.base_mesh_cleared = d.mBaseHullMesh.mPositions.empty();
        return r;
    }

    void write_decomp(Writer& w, const DecompRecord& r)
    {
        w.u8(r.present);
        w.u8(r.ok);
        w.u32((U32)r.hulls.size());
        for (const auto& h : r.hulls) w.bytes(h.data(), h.size());
        w.bytes(r.base_hull.data(), r.base_hull.size());
        w.u8(r.base_mesh_cleared);
    }

    DecompRecord read_decomp(Reader& rd)
    {
        DecompRecord r;
        r.present = rd.u8() != 0;
        r.ok = rd.u8() != 0;
        U32 n = rd.u32();
        for (U32 i = 0; i < n && rd.ok; ++i) r.hulls.push_back(rd.bytes());
        r.base_hull = rd.bytes();
        r.base_mesh_cleared = rd.u8() != 0;
        return r;
    }

    // ---- one corpus case --------------------------------------------------------

    const S32 NUM_PARTS = 5; // the four render LODs and the physics mesh
    const char* PART_NAMES[NUM_PARTS] = { "lowest_lod", "low_lod", "medium_lod", "high_lod", "physics_mesh" };

    struct PartRecord
    {
        bool present = false;
        bool ok = false;
        std::vector<FaceRecord> faces;     // after cacheOptimize (what the renderer gets)
        std::vector<FaceRecord> raw_faces; // as unpacked, before cacheOptimize
        // Not recorded: the raw faces put through cacheOptimize in this
        // process, the oracle the optimised faces are compared with.
        bool oracle_ok = false;
        std::vector<FaceRecord> oracle_faces;
        bool operator==(const PartRecord& o) const { return present == o.present && ok == o.ok && faces == o.faces && raw_faces == o.raw_faces; }
    };

    struct Case
    {
        std::string name;
        U8 sculpt_type = 0;        // the flags the unpacking volume carries
        std::vector<U8> asset;     // the packed mesh asset
        PartRecord parts[NUM_PARTS];
        SkinRecord skin;
        DecompRecord decomp;
    };

    const U32 FIXTURE_MAGIC = 0x48534d56; // "VMSH"
    const U32 FIXTURE_VERSION = 2;

    void write_corpus(const std::string& path, const std::vector<Case>& cases)
    {
        Writer w;
        w.u32(FIXTURE_MAGIC);
        w.u32(FIXTURE_VERSION);
        w.u32((U32)cases.size());
        for (const Case& c : cases)
        {
            w.str(c.name);
            w.u8(c.sculpt_type);
            w.bytes(c.asset.data(), c.asset.size());
            for (int p = 0; p < NUM_PARTS; ++p)
            {
                w.u8(c.parts[p].present);
                w.u8(c.parts[p].ok);
                w.u32((U32)c.parts[p].faces.size());
                for (const FaceRecord& f : c.parts[p].faces) write_face(w, f);
                w.u32((U32)c.parts[p].raw_faces.size());
                for (const FaceRecord& f : c.parts[p].raw_faces) write_face(w, f);
            }
            write_skin(w, c.skin);
            write_decomp(w, c.decomp);
        }
        std::ofstream o(path.c_str(), std::ios::binary);
        o.write((const char*)w.b.data(), w.b.size());
    }

    bool read_corpus(const std::string& path, std::vector<Case>& cases)
    {
        std::ifstream i(path.c_str(), std::ios::binary);
        if (!i) return false;
        std::vector<U8> data((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
        Reader rd(data);
        if (rd.u32() != FIXTURE_MAGIC) return false;
        if (rd.u32() != FIXTURE_VERSION) return false;
        U32 n = rd.u32();
        for (U32 k = 0; k < n && rd.ok; ++k)
        {
            Case c;
            c.name = rd.str();
            c.sculpt_type = rd.u8();
            c.asset = rd.bytes();
            for (int p = 0; p < NUM_PARTS; ++p)
            {
                c.parts[p].present = rd.u8() != 0;
                c.parts[p].ok = rd.u8() != 0;
                U32 faces = rd.u32();
                for (U32 f = 0; f < faces && rd.ok; ++f) c.parts[p].faces.push_back(read_face(rd));
                U32 raw = rd.u32();
                for (U32 f = 0; f < raw && rd.ok; ++f) c.parts[p].raw_faces.push_back(read_face(rd));
            }
            c.skin = read_skin(rd);
            c.decomp = read_decomp(rd);
            cases.push_back(c);
        }
        return rd.ok;
    }

    // ---- building assets with the viewer's packer ------------------------------

    struct Rng
    {
        U64 s;
        explicit Rng(U64 seed) : s(seed) {}
        U64 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
        U32 below(U32 n) { return (U32)(next() % n); }
        F32 unit() { return (F32)(next() % 10000) / 10000.f; }
    };

    const F32 LOD_DETAIL[4] = { 1.f, 1.5f, 2.5f, 4.f };

    void fill_weights(LLModel* model, Rng& rng, S32 joint_count)
    {
        for (S32 f = 0; f < model->getNumVolumeFaces(); ++f)
        {
            const LLVolumeFace& face = model->getVolumeFace(f);
            for (S32 v = 0; v < face.mNumVertices; ++v)
            {
                LLVector3 pos(face.mPositions[v].getF32ptr());
                LLModel::weight_list weights;
                S32 n = 1 + (S32)rng.below(4);
                for (S32 k = 0; k < n; ++k)
                {
                    weights.push_back(LLModel::JointWeight((S32)rng.below(joint_count), 0.05f + 0.9f * rng.unit()));
                }
                model->mSkinWeights[pos] = weights;
            }
        }
    }

    LLMatrix4a random_matrix(Rng& rng)
    {
        LLMatrix4 m;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m.mMatrix[i][j] = (i == j ? 1.f : 0.f) + (rng.unit() - 0.5f) * 0.5f;
        return LLMatrix4a(m);
    }

    void fill_skin_info(LLMeshSkinInfo& info, Rng& rng, S32 joint_count, bool alternates)
    {
        static const char* names[] = { "mPelvis", "mSpine", "mTorso", "mChest", "mNeck", "mHead", "mCollarLeft", "mShoulderLeft", "mElbowLeft", "mWristLeft", "mHipLeft", "mKneeLeft" };
        for (S32 i = 0; i < joint_count; ++i)
        {
            info.mJointNames.push_back(names[i % 12]);
            info.mJointNums.push_back(-1);
            info.mInvBindMatrix.push_back(random_matrix(rng));
            if (alternates)
            {
                info.mAlternateBindMatrix.push_back(random_matrix(rng));
            }
        }
        info.mBindShapeMatrix = random_matrix(rng);
        info.mPelvisOffset = (rng.unit() - 0.5f) * 0.2f;
    }

    void fill_decomp(LLModel::Decomposition& d, Rng& rng, LLModel* high, bool hulls, bool base)
    {
        const LLVolumeFace& face = high->getVolumeFace(0);
        if (hulls)
        {
            S32 nh = 1 + (S32)rng.below(4);
            for (S32 h = 0; h < nh; ++h)
            {
                std::vector<LLVector3> hull;
                S32 np = 4 + (S32)rng.below(12);
                for (S32 p = 0; p < np; ++p)
                {
                    const LLVector4a& v = face.mPositions[rng.below((U32)face.mNumVertices)];
                    hull.push_back(LLVector3(v.getF32ptr()) * (0.5f + rng.unit()));
                }
                d.mHull.push_back(hull);
            }
        }
        if (base)
        {
            S32 np = 4 + (S32)rng.below(20);
            for (S32 p = 0; p < np; ++p)
            {
                const LLVector4a& v = face.mPositions[rng.below((U32)face.mNumVertices)];
                d.mBaseHull.push_back(LLVector3(v.getF32ptr()));
            }
        }
    }

    struct Shape
    {
        const char* name;
        U8 profile;
        U8 path;
        F32 hollow;
        F32 twist;
        F32 taper_x, taper_y;
        F32 begin_s, end_s;
        F32 shear_x;
        F32 ratio_x, ratio_y;
    };

    const Shape SHAPES[] = {
        { "box", LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f },
        { "box_hollow_twist", LL_PCODE_PROFILE_SQUARE | LL_PCODE_HOLE_CIRCLE, LL_PCODE_PATH_LINE, 0.3f, 0.25f, 0.2f, -0.2f, 0.f, 1.f, 0.1f, 1.f, 1.f },
        { "cylinder", LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_LINE, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f },
        { "cylinder_cut", LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_LINE, 0.f, 0.f, 0.f, 0.f, 0.2f, 0.8f, 0.f, 1.f, 1.f },
        { "prism_taper", LL_PCODE_PROFILE_EQUALTRI, LL_PCODE_PATH_LINE, 0.f, 0.f, 0.5f, 0.5f, 0.f, 1.f, 0.f, 1.f, 1.f },
        { "sphere", LL_PCODE_PROFILE_CIRCLE_HALF, LL_PCODE_PATH_CIRCLE, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f },
        { "sphere_hollow", LL_PCODE_PROFILE_CIRCLE_HALF | LL_PCODE_HOLE_SQUARE, LL_PCODE_PATH_CIRCLE, 0.4f, 0.1f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f },
        { "torus", LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_CIRCLE, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f },
        { "torus_twist", LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_CIRCLE, 0.2f, 0.5f, 0.1f, 0.1f, 0.f, 1.f, 0.f, 0.25f, 0.25f },
        { "tube", LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_CIRCLE, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f },
        { "ring", LL_PCODE_PROFILE_EQUALTRI, LL_PCODE_PATH_CIRCLE, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f },
        { "ring_cut_hollow", LL_PCODE_PROFILE_EQUALTRI | LL_PCODE_HOLE_TRIANGLE, LL_PCODE_PATH_CIRCLE, 0.5f, 0.f, 0.f, 0.f, 0.1f, 0.9f, 0.f, 0.5f, 0.5f },
    };

    LLVolumeParams shape_params(const Shape& s)
    {
        LLVolumeParams p;
        p.setType(s.profile, s.path);
        p.setHollow(s.hollow);
        p.setTwist(s.twist);
        p.setTaper(s.taper_x, s.taper_y);
        p.setBeginAndEndS(s.begin_s, s.end_s);
        p.setShear(s.shear_x, 0.f);
        p.setRatio(s.ratio_x, s.ratio_y);
        return p;
    }

    struct Variant
    {
        const char* suffix;
        bool skin;
        bool alternates;
        bool hulls;
        bool base_hull;
        U8 flags;
        bool no_geometry_face;
        int submodel;
    };

    const Variant VARIANTS[] = {
        { "plain", false, false, false, false, 0, false, 0 },
        { "skin_hulls", true, true, true, true, 0, false, 0 },
        { "skin_basehull_mirror", true, false, false, true, LL_SCULPT_FLAG_MIRROR, false, 0 },
        { "hulls_invert_nogeom", false, false, true, false, LL_SCULPT_FLAG_INVERT, true, 0 },
        { "skin_both_flags", true, true, true, true, LL_SCULPT_FLAG_MIRROR | LL_SCULPT_FLAG_INVERT, false, 0 },
        { "submodel", false, false, false, false, 0, false, 2 },
    };

    // A high LOD in the 10,000-triangle class for the timing: the shape's
    // first face repeated up to the model face limit.
    const int BIG_FACES = 8;

    LLVolumeFace degenerate_face()
    {
        LLVolumeFace f;
        f.resizeVertices(2);
        f.resizeIndices(0);
        return f;
    }

    // Builds one case's asset bytes with LLModel::writeModel.
    void build_asset(Case& c, const Shape& s, const Variant& v, Rng& rng, bool big = false)
    {
        LLVolumeParams params = shape_params(s);
        LLPointer<LLModel> lods[4];
        for (int i = 0; i < 4; ++i)
        {
            lods[i] = new LLModel(params, LOD_DETAIL[i]);
        }
        if (big)
        {
            LLVolumeFace first = lods[3]->getVolumeFace(0);
            while (lods[3]->getNumVolumeFaces() < BIG_FACES)
            {
                lods[3]->addFace(first);
            }
        }
        LLPointer<LLModel> physics = new LLModel(params, LOD_DETAIL[0]);
        if (v.no_geometry_face)
        {
            lods[3]->addFace(degenerate_face());
            lods[1]->addFace(degenerate_face());
        }
        const S32 joints = 6 + (S32)rng.below(6);
        if (v.skin)
        {
            for (int i = 0; i < 4; ++i)
            {
                fill_weights(lods[i], rng, joints);
            }
            fill_skin_info(lods[3]->mSkinInfo, rng, joints, v.alternates);
        }
        LLModel::Decomposition decomp;
        if (v.hulls || v.base_hull)
        {
            fill_decomp(decomp, rng, lods[3], v.hulls, v.base_hull);
        }
        std::stringstream out;
        LLModel::writeModel(out, physics, lods[3], lods[2], lods[1], lods[0], decomp,
                            v.skin, v.alternates, v.alternates, LLModel::WRITE_BINARY, false, v.submodel);
        std::string bytes = out.str();
        c.asset.assign(bytes.begin(), bytes.end());
        c.sculpt_type = LL_SCULPT_TYPE_MESH | v.flags;
    }

    // ---- unpacking a case with the viewer's unpackers -------------------------

    struct Blocks
    {
        bool ok = false;
        S32 header_size = 0;
        S32 part_offset[NUM_PARTS], part_size[NUM_PARTS];
        S32 skin_offset = -1, skin_size = 0;
        S32 convex_offset = -1, convex_size = 0;
    };

    Blocks parse_header(const std::vector<U8>& asset)
    {
        Blocks b;
        boost::iostreams::stream<boost::iostreams::array_source> stream((const char*)asset.data(), asset.size());
        LLSD header;
        if (!LLSDSerialize::fromBinary(header, stream, (S32)asset.size()))
        {
            return b;
        }
        b.ok = true;
        b.header_size = (S32)stream.tellg();
        for (int p = 0; p < NUM_PARTS; ++p)
        {
            b.part_offset[p] = header.has(PART_NAMES[p]) ? header[PART_NAMES[p]]["offset"].asInteger() : -1;
            b.part_size[p] = header.has(PART_NAMES[p]) ? header[PART_NAMES[p]]["size"].asInteger() : 0;
        }
        if (header.has("skin"))
        {
            b.skin_offset = header["skin"]["offset"].asInteger();
            b.skin_size = header["skin"]["size"].asInteger();
        }
        if (header.has("physics_convex"))
        {
            b.convex_offset = header["physics_convex"]["offset"].asInteger();
            b.convex_size = header["physics_convex"]["size"].asInteger();
        }
        return b;
    }

    void unpack_case(Case& c, bool with_timing, double* unpack_ms)
    {
        Blocks b = parse_header(c.asset);
        ensure("asset header parses: " + c.name, b.ok);
        LLUUID mesh_id;
        mesh_id.generate();
        LLVolumeParams params;
        params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
        params.setSculptID(mesh_id, c.sculpt_type);
        if (unpack_ms) *unpack_ms = 0.0;
        for (int p = 0; p < NUM_PARTS; ++p)
        {
            PartRecord& part = c.parts[p];
            part = PartRecord();
            if (b.part_offset[p] < 0 || b.part_size[p] <= 0)
            {
                continue;
            }
            part.present = true;
            S32 offset = b.header_size + b.part_offset[p];
            ensure("block within asset: " + c.name, offset >= 0 && offset + b.part_size[p] <= (S32)c.asset.size());
            LLPointer<LLVolume> volume;
            if (p == 4)
            {
                // The physics shape: as physicsShapeReceived builds it.
                LLVolumeParams physics_params;
                physics_params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
                physics_params.setSculptID(mesh_id, LL_SCULPT_TYPE_MESH);
                volume = new LLVolume(physics_params, 0);
            }
            else
            {
                volume = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(p));
            }
            std::vector<U8> block(c.asset.begin() + offset, c.asset.begin() + offset + b.part_size[p]);
            // As unpacked, before the vertex cache optimisation: what the
            // unpacker itself produces (and what is timed).
            {
                LLPointer<LLVolume> raw_volume = new LLVolume(volume->getParams(), p == 4 ? 0.f : LLVolumeLODGroup::getVolumeScaleFromDetail(p));
                auto t0 = std::chrono::steady_clock::now();
                bool raw_ok = raw_volume->unpackVolumeFaces(block.data(), (S32)block.size(), false);
                if (with_timing && unpack_ms)
                {
                    *unpack_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
                if (raw_ok)
                {
                    for (S32 f = 0; f < raw_volume->getNumVolumeFaces(); ++f)
                    {
                        part.raw_faces.push_back(record_face(raw_volume->getVolumeFace(f)));
                    }
                    // The oracle: the same C++ vertex cache optimisation and
                    // tangent generation, built by the same compiler, run on
                    // these raw faces (which the comparison holds bit for bit
                    // to the corpus).
                    part.oracle_ok = raw_volume->cacheOptimize(true);
                    if (part.oracle_ok)
                    {
                        for (S32 f = 0; f < raw_volume->getNumVolumeFaces(); ++f)
                        {
                            part.oracle_faces.push_back(record_face(raw_volume->getVolumeFace(f)));
                        }
                    }
                }
            }
            part.ok = volume->unpackVolumeFaces(block.data(), (S32)block.size());
            if (part.ok)
            {
                for (S32 f = 0; f < volume->getNumVolumeFaces(); ++f)
                {
                    part.faces.push_back(record_face(volume->getVolumeFace(f)));
                }
            }
        }
        c.skin = SkinRecord();
        if (b.skin_offset >= 0 && b.skin_size > 0)
        {
            S32 offset = b.header_size + b.skin_offset;
            LLMeshSkinInfo info;
            info.mMeshID = mesh_id;
            bool ok = info.fromZippedData(c.asset.data() + offset, b.skin_size);
            c.skin = record_skin(info, true, ok);
        }
        c.decomp = DecompRecord();
        if (b.convex_offset >= 0 && b.convex_size > 0)
        {
            S32 offset = b.header_size + b.convex_offset;
            LLModel::Decomposition d;
            d.mMeshID = mesh_id;
            // A marker in the base hull mesh: fromLLSD clears it when the block
            // carries no bounding vertices, so the record tells which branch ran.
            d.mBaseHullMesh.mPositions.push_back(LLVector3(1.f, 2.f, 3.f));
            bool ok = d.fromZippedData(c.asset.data() + offset, b.convex_size);
            c.decomp = record_decomp(d, true, ok);
        }
    }

    void compare_case(const Case& got, const Case& want, std::vector<std::string>& problems)
    {
        for (int p = 0; p < NUM_PARTS; ++p)
        {
            const PartRecord& g = got.parts[p];
            const PartRecord& w = want.parts[p];
            if (g.present != w.present || g.ok != w.ok || g.faces.size() != w.faces.size() || g.raw_faces.size() != w.raw_faces.size())
            {
                std::ostringstream s;
                s << want.name << "/" << PART_NAMES[p] << ": present " << g.present << "/" << w.present << " ok " << g.ok << "/" << w.ok
                  << " faces " << g.faces.size() << "/" << w.faces.size() << " raw " << g.raw_faces.size() << "/" << w.raw_faces.size();
                problems.push_back(s.str());
                continue;
            }
            // The raw faces against the corpus; the optimised faces against
            // the oracle of this process. The corpus' optimised faces are not
            // compared: cacheOptimize and the MikkTSpace tangents are C++ the
            // port hands its faces to, and their float results depend on how
            // the compiler builds them (MSVC 19.44 and GCC 15 reproduce the
            // record, GCC 14 differs in the normals and tangents).
            if (g.ok && (!g.oracle_ok || g.oracle_faces.size() != g.faces.size()))
            {
                std::ostringstream s;
                s << want.name << "/" << PART_NAMES[p] << ": the oracle optimisation failed or has "
                  << g.oracle_faces.size() << " faces for " << g.faces.size();
                problems.push_back(s.str());
                continue;
            }
            for (size_t f = 0; f < w.faces.size() + w.raw_faces.size(); ++f)
            {
                const bool raw = f >= w.faces.size();
                if (!raw && !g.ok)
                {
                    continue;
                }
                const FaceRecord& a = raw ? g.raw_faces[f - w.faces.size()] : g.faces[f];
                const FaceRecord& e = raw ? w.raw_faces[f - w.faces.size()] : g.oracle_faces[f];
                if (!(a == e))
                {
                    std::ostringstream s;
                    s << want.name << "/" << PART_NAMES[p] << (raw ? " raw face " : " optimised face (against the oracle) ") << (raw ? f - w.faces.size() : f) << ": ";
                    if (a.num_vertices != e.num_vertices || a.num_indices != e.num_indices) s << "counts ";
                    if (a.hash_positions != e.hash_positions) s << "positions ";
                    if (a.hash_normals != e.hash_normals) s << "normals ";
                    if (a.hash_texcoords != e.hash_texcoords) s << "texcoords ";
                    if (a.hash_indices != e.hash_indices) s << "indices ";
                    if (a.hash_weights != e.hash_weights || a.has_weights != e.has_weights) s << "weights ";
                    if (a.hash_tangents != e.hash_tangents || a.has_tangents != e.has_tangents) s << "tangents ";
                    if (memcmp(a.extents, e.extents, sizeof(a.extents)) != 0) s << "extents ";
                    if (memcmp(a.tc_extents, e.tc_extents, sizeof(a.tc_extents)) != 0) s << "tc_extents ";
                    if (memcmp(a.normalized_scale, e.normalized_scale, sizeof(a.normalized_scale)) != 0) s << "normalized_scale ";
                    problems.push_back(s.str());
                }
            }
        }
        if (!(got.skin == want.skin))
        {
            problems.push_back(want.name + ": skin info differs");
        }
        if (!(got.decomp == want.decomp))
        {
            problems.push_back(want.name + ": decomposition differs");
        }
    }
}

namespace tut
{
    struct meshunpack_data {};
    typedef test_group<meshunpack_data> meshunpack_group;
    typedef meshunpack_group::object meshunpack_object;
    tut::meshunpack_group meshunpack_instance("llmeshunpack_parity");

    template<> template<>
    void meshunpack_object::test<1>()
    {
        set_test_name("mesh assets unpack bit-identically to the recorded corpus");
        const char* record = getenv("VKS_MESH_RECORD");
        if (record && *record)
        {
            std::vector<Case> cases;
            Rng rng(0x243F6A8885A308D3ULL);
            for (const Shape& s : SHAPES)
            {
                for (const Variant& v : VARIANTS)
                {
                    Case c;
                    c.name = std::string(s.name) + "/" + v.suffix;
                    build_asset(c, s, v, rng);
                    unpack_case(c, false, NULL);
                    cases.push_back(c);
                }
            }
            {
                Case c;
                c.name = "torus_big/skin_hulls";
                build_asset(c, SHAPES[7], VARIANTS[1], rng, true);
                unpack_case(c, false, NULL);
                cases.push_back(c);
            }
            write_corpus(record, cases);
            size_t total = 0, faces = 0;
            for (const Case& c : cases)
            {
                total += c.asset.size();
                for (int p = 0; p < NUM_PARTS; ++p) faces += c.parts[p].faces.size() + c.parts[p].raw_faces.size();
            }
            std::cout << "recorded " << cases.size() << " cases, " << total << " asset bytes, " << faces << " faces to " << record << std::endl;
            // Unpack again from the stored bytes: the record must reproduce.
            std::vector<Case> again;
            ensure("corpus reads back", read_corpus(record, again));
            std::vector<std::string> problems;
            for (size_t i = 0; i < again.size(); ++i)
            {
                Case c = again[i];
                unpack_case(c, false, NULL);
                compare_case(c, again[i], problems);
            }
            for (const std::string& p : problems) std::cout << "PARITY: " << p << std::endl;
            ensure("the corpus reproduces itself", problems.empty());
            return;
        }

        std::vector<Case> cases;
        ensure("corpus present: " VKS_MESH_FIXTURE, read_corpus(VKS_MESH_FIXTURE, cases));
        ensure("corpus has cases", !cases.empty());
        std::vector<std::string> problems;
        size_t raw_faces = 0, optimised_faces = 0;
        for (size_t i = 0; i < cases.size(); ++i)
        {
            Case c = cases[i];
            unpack_case(c, false, NULL);
            compare_case(c, cases[i], problems);
            for (int p = 0; p < NUM_PARTS; ++p)
            {
                raw_faces += cases[i].parts[p].raw_faces.size();
                optimised_faces += c.parts[p].faces.size();
            }
        }
        for (const std::string& p : problems) std::cout << "PARITY: " << p << std::endl;
        std::cout << "compared " << cases.size() << " cases: " << raw_faces << " raw faces against the corpus, "
                  << optimised_faces << " optimised faces against the in-process oracle, skin and decompositions against the corpus; "
                  << problems.size() << " differences" << std::endl;
        ensure("mesh unpacking bit-identical", problems.empty());
    }

    template<> template<>
    void meshunpack_object::test<2>()
    {
        set_test_name("unpacking timing");
        const char* bench = getenv("VKS_MESH_BENCH");
        if (!bench || !*bench)
        {
            return;
        }
        int iterations = atoi(bench);
        std::vector<Case> cases;
        ensure("corpus present: " VKS_MESH_FIXTURE, read_corpus(VKS_MESH_FIXTURE, cases));
        // The largest high LOD by triangle count, and a 10,000-triangle
        // class representative: the sphere/torus high LODs.
        double best_total = 1e18;
        size_t best_tris = 0;
        std::string best_name;
        for (const Case& c : cases)
        {
            size_t tris = 0;
            for (const FaceRecord& f : c.parts[3].faces) tris += f.num_indices / 3;
            if (tris > best_tris)
            {
                best_tris = tris;
                best_name = c.name;
            }
        }
        for (const Case& c : cases)
        {
            if (c.name != best_name) continue;
            for (int it = 0; it < iterations; ++it)
            {
                Case copy = c;
                double ms = 0;
                unpack_case(copy, true, &ms);
                best_total = std::min(best_total, ms);
            }
        }
        std::cout << "VKS_MESH_BENCH case=" << best_name << " high_lod_triangles=" << best_tris << " all_parts_min_ms=" << best_total << std::endl;
        const char* out = getenv("VKS_MESH_BENCH_OUT");
        if (out && *out)
        {
            std::ofstream o(out, std::ios::app);
            o << "mesh,unpack_all_parts," << best_name << "," << best_tris << "," << best_total << "\n";
        }
    }
}

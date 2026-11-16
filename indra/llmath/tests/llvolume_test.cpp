// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file llvolume_test.cpp
 * @brief Characterisation tests for LLVolume::unpackVolumeFaces on malformed mesh blobs.
 */

#include "linden_common.h"
#include "../test/lltut.h"
#include "../llvolume.h"
#include "llsdserialize.h"

namespace tut
{
    struct llvolume_test
    {
        static LLSD::Binary bytes(size_t n)
        {
            LLSD::Binary b(n);
            for (size_t i = 0; i < n; ++i)
            {
                b[i] = static_cast<U8>((i * 37 + 11) & 0xff);
            }
            return b;
        }

        // Builds a one-face mesh LOD. Index values are U16 little endian.
        static LLSD face(size_t pos_bytes, size_t norm_bytes, size_t tc_bytes, const std::vector<U16>& indices)
        {
            LLSD f;
            f["PositionDomain"]["Min"] = LLSD::emptyArray();
            f["PositionDomain"]["Max"] = LLSD::emptyArray();
            for (int i = 0; i < 3; ++i)
            {
                f["PositionDomain"]["Min"].append(-0.5);
                f["PositionDomain"]["Max"].append(0.5);
            }
            f["TexCoord0Domain"]["Min"] = LLSD::emptyArray();
            f["TexCoord0Domain"]["Max"] = LLSD::emptyArray();
            for (int i = 0; i < 2; ++i)
            {
                f["TexCoord0Domain"]["Min"].append(0.0);
                f["TexCoord0Domain"]["Max"].append(1.0);
            }
            f["Position"] = bytes(pos_bytes);
            if (norm_bytes) { f["Normal"] = bytes(norm_bytes); }
            if (tc_bytes) { f["TexCoord0"] = bytes(tc_bytes); }
            LLSD::Binary idx;
            for (U16 v : indices)
            {
                idx.push_back(static_cast<U8>(v & 0xff));
                idx.push_back(static_cast<U8>(v >> 8));
            }
            f["TriangleList"] = idx;
            return f;
        }

        // Returns the number of indices left in face 0 (0 when the face was rejected), or -1 when unpack failed.
        static S32 unpack(const LLSD& one_face)
        {
            LLSD mdl = LLSD::emptyArray();
            mdl.append(one_face);
            std::string z = zip_llsd(mdl);
            LLVolumeParams params;
            LLPointer<LLVolume> volume = new LLVolume(params, 1.f);
            if (!volume->unpackVolumeFaces(reinterpret_cast<U8*>(z.data()), static_cast<S32>(z.size())))
            {
                return -1;
            }
            return volume->getNumVolumeFaces() > 0 ? volume->getVolumeFace(0).mNumIndices : -1;
        }
    };

    typedef test_group<llvolume_test> llvolume_test_t;
    typedef llvolume_test_t::object llvolume_test_object_t;
    tut::llvolume_test_t tut_llvolume_test("LLVolumeUnpack");

    template<> template<>
    void llvolume_test_object_t::test<1>()
    {
        set_test_name("well-formed face is kept");
        ensure_equals("indices kept", unpack(face(3 * 6, 3 * 6, 3 * 4, {0, 1, 2})), 3);
    }

    template<> template<>
    void llvolume_test_object_t::test<2>()
    {
        set_test_name("normal blob shorter than the position blob claims is rejected");
        ensure_equals("one-vertex normal blob", unpack(face(1000 * 6, 6, 1000 * 4, {0, 1, 2})), 0);
        ensure_equals("normals one byte short", unpack(face(1000 * 6, 1000 * 6 - 1, 1000 * 4, {0, 1, 2})), 0);
    }

    template<> template<>
    void llvolume_test_object_t::test<3>()
    {
        set_test_name("texcoord blob shorter than the position blob claims is rejected");
        ensure_equals("one-vertex texcoord blob", unpack(face(1000 * 6, 1000 * 6, 4, {0, 1, 2})), 0);
        ensure_equals("texcoords one byte short", unpack(face(1001 * 6, 1001 * 6, 1001 * 4 - 1, {0, 1, 2})), 0);
        ensure_equals("odd count, exact size is kept", unpack(face(1001 * 6, 1001 * 6, 1001 * 4, {0, 1, 2})), 3);
    }

    template<> template<>
    void llvolume_test_object_t::test<4>()
    {
        set_test_name("more than 65535 vertices is rejected");
        ensure_equals("65536 vertices", unpack(face(65536 * 6, 0, 0, {0, 1, 2})), 0);
        ensure_equals("65535 vertices", unpack(face(65535 * 6, 0, 0, {0, 1, 2})), 3);
    }

    template<> template<>
    void llvolume_test_object_t::test<5>()
    {
        set_test_name("triangle index beyond the vertex count is rejected");
        ensure_equals("index == num_verts", unpack(face(3 * 6, 3 * 6, 3 * 4, {0, 1, 3})), 0);
        ensure_equals("index 65535", unpack(face(3 * 6, 3 * 6, 3 * 4, {0, 1, 65535})), 0);
        ensure_equals("last valid index", unpack(face(3 * 6, 3 * 6, 3 * 4, {2, 1, 0})), 3);
    }

    template<> template<>
    void llvolume_test_object_t::test<6>()
    {
        set_test_name("a truncated Weights blob does not read past its end");
        // two vertices; vertex 0: joint 1, influence 0x8000, end marker; vertex 1: joint 2, then the blob ends
        // in the middle of an influence (1 byte) and, in the second case, right after an influence.
        const U8 truncated_influence[] = { 1, 0x00, 0x80, 0xff, 2, 0x00 };
        const U8 no_next_joint[] = { 1, 0x00, 0x80, 0xff, 2, 0x00, 0x80 };
        for (const auto& blob : { LLSD::Binary(truncated_influence, truncated_influence + sizeof(truncated_influence)),
                                  LLSD::Binary(no_next_joint, no_next_joint + sizeof(no_next_joint)) })
        {
            LLSD f = face(2 * 6, 2 * 6, 2 * 4, {0, 1, 0});
            f["Weights"] = blob;
            // Heap-exact copy so that a read past the end is visible to the debug heap / ASan.
            ensure_equals("unpack survives", unpack(f), 3);
        }
    }
}

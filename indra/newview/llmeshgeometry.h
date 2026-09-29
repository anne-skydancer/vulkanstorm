// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "stdtypes.h"
#include "llmatrix4a.h"
#include <vector>
class LLVolume;
class LLVertexBuffer;
class LLMatrix4a;
class LLDrawInfo;

namespace LLMeshGeometry
{
bool initGL();
// Publish mesh position/normal/tangent attributes without a CPU transform or
// render-time readback. Other attributes and ordered draw ownership stay with
// the face. Matrices include the bind shape for rigged meshes.
bool generate(LLVertexBuffer& destination, const LLVolume& volume, S32 face,
              U32 first, U32 count, U32 padded_count, U32 texture_index,
              const LLMatrix4a& position, const LLMatrix4a& normal,
              bool normals, bool tangents);
void flushRequired();
void destroyGL();
void fill(LLVertexBuffer& destination, U32 attribute, U32 first, U32 count, U32 value);
bool weights(LLVertexBuffer& destination, const LLVolume& volume, S32 face, U32 first, U32 count);
bool indices(LLVertexBuffer& destination, const LLVolume& volume, S32 face,
             U32 first, U32 count, U32 base_vertex);
struct Texcoords
{
    LLMatrix4a texture, normal, rotation;
    F32 scale[4] = {1,1,1,0};
    F32 transform[4] = {1,0,1,1}; // cosine, sine, S scale, T scale
    F32 offset[4] = {};
    F32 bump_s[4] = {}, bump_t[4] = {}, binormal[4] = {};
    // planar, texture matrix, TE transform, emboss bump, active-object rotation
    U32 flags = 0;
    Texcoords() { texture.setIdentity(); normal.setIdentity(); rotation.setIdentity(); }
};
bool texcoords(LLVertexBuffer& destination, const LLVolume& volume, S32 face,
               U32 attribute, U32 first, U32 count, const Texcoords& parameters);
void copyResidentRange(LLVertexBuffer& destination, LLVertexBuffer& source,
                       U32 source_vertex, U32 source_index, U32 vertices,
                       U32 indices, U32 target_vertex, U32 target_index);
// Adjacent compatible ranges retain their original order. State changes still
// require host GL bindings; command expansion is performed and cached on GPU.
bool compatibleState(const LLDrawInfo& first, const LLDrawInfo& next);
bool compatibleBatch(const LLDrawInfo& first, const LLDrawInfo& next);
bool drawBatch(const std::vector<LLDrawInfo*>& batch);
}

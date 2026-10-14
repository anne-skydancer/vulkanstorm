// Shared CPU terrain strip/LOD topology from the reference viewer. LGPL-2.1.
#pragma once
#include "v3math.h"
#include "v2math.h"
#include "llstrider.h"
#include <functional>
#include <vector>
struct VSTerrainMesh
{
    std::vector<LLVector3> vertices, normals;
    std::vector<LLVector2> uv, detail;
    std::vector<U16>       indices;
};
class VSTerrainGeometry
{
public:
    using Evaluate = std::function<void(U32, U32, U32, LLVector3*, LLVector3*, LLVector2*, LLVector2*)>;
    VSTerrainGeometry(U32 size, U32 stride, U32 north, U32 east, Evaluate evaluate);
    VSTerrainMesh mesh();
    void          updateMainGeometry(LLStrider<LLVector3>& verticesp,
                                     LLStrider<LLVector3>& normalsp,
                                     LLStrider<LLVector2>& texCoords0p,
                                     LLStrider<LLVector2>& texCoords1p,
                                     LLStrider<U16>&       indicesp,
                                     U32&                  index_offset);
    void          updateNorthGeometry(LLStrider<LLVector3>& verticesp,
                                      LLStrider<LLVector3>& normalsp,
                                      LLStrider<LLVector2>& texCoords0p,
                                      LLStrider<LLVector2>& texCoords1p,
                                      LLStrider<U16>&       indicesp,
                                      U32&                  index_offset);
    void          updateEastGeometry(LLStrider<LLVector3>& verticesp,
                                     LLStrider<LLVector3>& normalsp,
                                     LLStrider<LLVector2>& texCoords0p,
                                     LLStrider<LLVector2>& texCoords1p,
                                     LLStrider<U16>&       indicesp,
                                     U32&                  index_offset);
    void          getGeomSizesMain(const S32 stride, S32& num_vertices, S32& num_indices);
    void          getGeomSizesNorth(const S32 stride, const S32 north_stride, S32& num_vertices, S32& num_indices);
    void          getGeomSizesEast(const S32 stride, const S32 east_stride, S32& num_vertices, S32& num_indices);

private:
    U32      mPatchSize, mLastStride, mLastNorthStride, mLastEastStride;
    Evaluate mEval;
};

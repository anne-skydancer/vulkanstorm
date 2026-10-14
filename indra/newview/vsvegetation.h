// Shared CPU vegetation producers, extracted from the reference viewer. LGPL-2.1.
#pragma once
#include "llvotree.h"
#include "llstrider.h"
#include "m4math.h"
#include "v2math.h"
#include "v4coloru.h"
#include <vector>
#include <functional>
struct VSVegetationMesh
{
    std::vector<LLVector3> vertices, normals;
    std::vector<LLVector2> uv;
    std::vector<LLColor4U> colors;
    std::vector<U16>       indices;
};
class VSTreeGeometry : private LLVOTree::TreeSpeciesData
{
public:
    explicit VSTreeGeometry(const LLVOTree::TreeSpeciesData&);
    VSVegetationMesh mesh(const LLVector3&, const LLQuaternion&, const LLVector3&, S32 lod, const LLVector3& bend = {});

private:
    void                 reference();
    VSVegetationMesh     mReference;
    S32                  mTrunkLOD = 0;
    LLVector3            mTrunkBend;
    static constexpr S32 sMAX_NUM_TREE_LOD_LEVELS = 4;
    S32                  sLODVertexOffset[4]{}, sLODVertexCount[4]{}, sLODIndexOffset[4]{}, sLODIndexCount[4]{};
    static constexpr S32 sLODSlices[4] = { 10, 5, 4, 3 };
    void calcNumVerts(U32& vert_count, U32& index_count, S32 trunk_LOD, S32 stop_level, U16 depth, U16 trunk_depth, F32 branches);

    void appendMesh(LLStrider<LLVector3>& vertices,
                    LLStrider<LLVector3>& normals,
                    LLStrider<LLVector2>& tex_coords,
                    LLStrider<LLColor4U>& colors,
                    LLStrider<U16>&       indices,
                    U16&                  idx_offset,
                    LLMatrix4&            matrix,
                    LLMatrix4&            norm_mat,
                    S32                   vertex_offset,
                    S32                   vertex_count,
                    S32                   index_count,
                    S32                   index_offset);

    void genBranchPipeline(LLStrider<LLVector3>& vertices,
                           LLStrider<LLVector3>& normals,
                           LLStrider<LLVector2>& tex_coords,
                           LLStrider<LLColor4U>& colors,
                           LLStrider<U16>&       indices,
                           U16&                  index_offset,
                           LLMatrix4&            matrix,
                           S32                   trunk_LOD,
                           S32                   stop_level,
                           U16                   depth,
                           U16                   trunk_depth,
                           F32                   scale,
                           F32                   twist,
                           F32                   droop,
                           F32                   branches,
                           F32                   alpha);
};

struct VSGrassBlade
{
    F32 x, y, rx, ry, dx, dy, wind;
};
using VSVegetationGround = std::function<F32(const LLVector3&)>;
VSVegetationMesh vs_grass_mesh(const LLVector3&, const LLVector3&, F32 width, F32 height, const VSGrassBlade*, unsigned count,
                               const VSVegetationGround&);

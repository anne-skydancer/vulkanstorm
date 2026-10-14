// Device-free checks of the same CPU geometry producers linked by the viewer.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsvegetation.h"
#include "vsflexiblepath.h"
#include "vsterraingeometry.h"
#include "llxmltree.h"
#include "llvolumemgr.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void validate(const VSVegetationMesh& mesh)
{
    require(!mesh.vertices.empty() && mesh.vertices.size() == mesh.normals.size() && mesh.vertices.size() == mesh.uv.size() &&
                mesh.indices.size() % 3 == 0,
            "Incomplete vegetation geometry");
    for (const auto& point : mesh.vertices)
        require(point.isFinite(), "Nonfinite vegetation position");
    for (const auto& normal : mesh.normals)
        require(normal.isFinite(), "Nonfinite vegetation normal");
    for (const auto index : mesh.indices)
        require(index < mesh.vertices.size(), "Invalid vegetation index");
}
LLVOTree::TreeSpeciesData species(LLXmlTreeNode& node)
{
    LLVOTree::TreeSpeciesData data{};
    auto                      real = [&](const char* name, F32& value)
    {
        require(node.getAttributeF32(name, value), "Missing tree field");
    };
    real("branch_length", data.mBranchLength);
    real("droop", data.mDroop);
    real("twist", data.mTwist);
    real("branches", data.mBranches);
    real("scale_step", data.mScaleStep);
    real("leaf_scale", data.mLeafScale);
    real("trunk_length", data.mTrunkLength);
    real("billboard_scale", data.mBillboardScale);
    real("billboard_ratio", data.mBillboardRatio);
    real("trunk_aspect", data.mTrunkAspect);
    real("branch_aspect", data.mBranchAspect);
    real("leaf_rotate", data.mRandomLeafRotate);
    real("noise_scale", data.mNoiseScale);
    real("noise_mag", data.mNoiseMag);
    real("taper", data.mTaper);
    real("repeat_z", data.mRepeatTrunkZ);
    require(node.getAttributeU8("depth", data.mDepth) && node.getAttributeU8("trunk_depth", data.mTrunkDepth), "Missing tree depth");
    require(node.getAttributeString("name", data.mName), "Missing tree name");
    return data;
}
void trees(const std::string& filename)
{
    LLXmlTree tree;
    require(tree.parseFile(filename, false), "Could not read staged tree species");
    unsigned count = 0;
    for (auto* node = tree.getRoot()->getFirstChild(); node; node = tree.getRoot()->getNextChild())
    {
        const auto     data = species(*node);
        VSTreeGeometry producer(data);
        for (S32 lod = 0; lod < 4; ++lod)
        {
            const auto mesh = producer.mesh(LLVector3(2, 3, 4), LLQuaternion::DEFAULT, LLVector3(4, 5, 6), lod);
            validate(mesh);
            const auto repeat = producer.mesh(LLVector3(2, 3, 4), LLQuaternion::DEFAULT, LLVector3(4, 5, 6), lod);
            require(mesh.vertices == repeat.vertices && mesh.indices == repeat.indices, "Unchanged tree generation is nondeterministic");
            const auto moved = producer.mesh(LLVector3(12, 3, 4), LLQuaternion::DEFAULT, LLVector3(4, 5, 6), lod);
            require(moved.vertices.size() == mesh.vertices.size(), "Tree translation changed topology");
            for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex)
                require(std::abs(moved.vertices[vertex].mV[0] - mesh.vertices[vertex].mV[0] - 10) < .001f, "Tree transform drift");
        }
        ++count;
    }
    require(count >= 20, "Incomplete staged tree species coverage");
    std::cout << "PASS: " << count << " tree species, all four LODs, repeat and transform\n";
}
void grass()
{
    std::array<VSGrassBlade, 32> blades;
    for (unsigned index = 0; index < blades.size(); ++index)
        blades[index] = { F32(index % 8) * .1f, F32(index / 8) * .1f, .5f, .75f, .125f, .25f, 1.f };
    for (const unsigned count : { 1, 2, 4, 8, 16, 32 })
    {
        auto mesh = vs_grass_mesh(LLVector3(2, 3, 4), LLVector3(4, 5, 6), 1, 2, blades.data(), count,
                                  [](const LLVector3& point) { return point.mV[0] + point.mV[1]; });
        validate(mesh);
        require(mesh.vertices.size() == count * 8 && mesh.indices.size() == count * 12, "Grass LOD topology differs from reference");
        for (unsigned blade = 0; blade < count; ++blade)
            require(mesh.normals[blade * 8].mV[2] > 0 && mesh.normals[blade * 8 + 1].mV[2] > 0, "Grass back-face normal changed");
    }
    std::cout << "PASS: grass LODs, terrain, paired faces\n";
}
void flexible()
{
    LLVolumeParams params;
    require(params.setType(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_FLEXIBLE), "Could not create flexible volume");
    LLFlexibleObjectData attributes;
    attributes.setSimulateLOD(2);
    LLVector3 force(.2f, .1f, 0);
    attributes.setGravity(-1);
    attributes.setWindSensitivity(1);
    attributes.setUserForce(force);
    for (S32 simulation = 0; simulation <= FLEXIBLE_OBJECT_MAX_SECTIONS; ++simulation)
        for (S32 render = simulation; render <= FLEXIBLE_OBJECT_MAX_SECTIONS; ++render)
        {
            std::array<VSFlexibleSection, (1 << FLEXIBLE_OBJECT_MAX_SECTIONS) + 1> sections;
            S32                                                                    initialized = -1;
            LLQuaternion                                                           last;
            LLPointer<LLVolume>                                                    volume = new LLVolume(params, 2.f);
            const LLVector3                                                        position(1, 2, 3), scale(2, 3, 4);
            vs_flexible_parameters(sections.data(), initialized, simulation, params.getPathParams(), scale, position,
                                   LLQuaternion::DEFAULT);
            for (unsigned frame = 0; frame < 4; ++frame)
                vs_flexible_step(
                    sections.data(), simulation, render, *volume, attributes, position, LLQuaternion::DEFAULT, scale, 1.f / 60, 10,
                    [](const LLVector3&) { return LLVector3(1, 2, 0); }, false, last);
            require(volume->getPath().getPathLength() == (1 << render) + 1, "Flexible render resolution changed");
            for (S32 section = 0; section < volume->getPath().getPathLength(); ++section)
                require(volume->getPath().mPath[section].mPos.isFinite3(), "Nonfinite flexible path");
        }
    std::cout << "PASS: flexible simulation/render resolutions, forces and wind\n";
}
void volume(const LLVolume& value)
{
    require(value.getNumVolumeFaces() > 0, "Missing volume faces");
    for (S32 index = 0; index < value.getNumVolumeFaces(); ++index)
    {
        const auto& face = value.getVolumeFace(index);
        require(face.mNumVertices > 0 && face.mNumIndices > 0 && face.mNumIndices % 3 == 0, "Incomplete volume topology");
        for (S32 vertex = 0; vertex < face.mNumVertices; ++vertex)
        {
            require(face.mPositions[vertex].isFinite3(), "Nonfinite volume position");
            LLVector3 normal(face.mNormals[vertex].getF32ptr());
            normal.normalize();
            require(normal.isFinite(), "Nonfinite normalized volume normal");
        }
        for (S32 corner = 0; corner < face.mNumIndices; ++corner)
            require(face.mIndices[corner] < face.mNumVertices, "Invalid volume index");
    }
}
void primitives()
{
    unsigned cases = 0;
    for (U8 profile = LL_PCODE_PROFILE_MIN; profile <= LL_PCODE_PROFILE_MAX; ++profile)
        for (const U8 path : { LL_PCODE_PATH_LINE, LL_PCODE_PATH_CIRCLE, LL_PCODE_PATH_CIRCLE2 })
            for (S32 lod = 0; lod < 4; ++lod)
            {
                LLVolumeParams params;
                require(params.setType(profile, path), "Unsupported reference primitive profile/path");
                LLPointer<LLVolume> geometry = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(lod));
                volume(*geometry);
                ++cases;
            }
    std::array<U8, 32 * 32 * 3> map;
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 32; ++x)
        {
            const std::size_t offset = (y * 32 + x) * 3;
            map[offset]              = U8(x * 255 / 31);
            map[offset + 1]          = U8(y * 255 / 31);
            map[offset + 2]          = U8((x + y) * 255 / 62);
        }
    const LLUUID identity("b0000000-0000-0000-0000-000000000001");
    for (const U8 type : { LL_SCULPT_TYPE_SPHERE, LL_SCULPT_TYPE_TORUS, LL_SCULPT_TYPE_PLANE, LL_SCULPT_TYPE_CYLINDER })
        for (const U8 flags : { U8(0), LL_SCULPT_FLAG_MIRROR, LL_SCULPT_FLAG_INVERT, U8(LL_SCULPT_FLAG_MIRROR | LL_SCULPT_FLAG_INVERT) })
            for (S32 lod = 0; lod < 4; ++lod)
            {
                LLVolumeParams params;
                require(params.setSculptID(identity, type | flags), "Invalid sculpt fixture");
                LLPointer<LLVolume> geometry = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(lod));
                geometry->sculpt(32, 32, 3, map.data(), 0, false);
                volume(*geometry);
                ++cases;
            }
    std::cout << "PASS: " << cases << " primitive/sculpt topology, LOD and mirror/invert cases\n";
}
void terrain()
{
    for (const U32 size : { 16, 32 })
        for (U32 stride = 1; stride <= size; stride *= 2)
            for (const U32 north : { std::max(stride / 2, 1u), stride, std::min(stride * 2, size) })
                for (const U32 east : { std::max(stride / 2, 1u), stride, std::min(stride * 2, size) })
                {
                    VSTerrainGeometry producer(size, stride, north, east,
                                               [](U32 x, U32 y, U32, LLVector3* point, LLVector3* normal, LLVector2* uv, LLVector2* detail)
                                               {
                                                   *point  = LLVector3(F32(x), F32(y), 0);
                                                   *normal = LLVector3::z_axis;
                                                   *uv     = LLVector2(F32(x), F32(y));
                                                   *detail = *uv;
                                               });
                    const auto        mesh = producer.mesh();
                    F64               area = 0;
                    require(mesh.indices.size() % 3 == 0, "Invalid terrain topology");
                    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
                    {
                        for (unsigned corner = 0; corner < 3; ++corner)
                            require(mesh.indices[triangle + corner] < mesh.vertices.size(), "Invalid terrain index");
                        const auto a = mesh.vertices[mesh.indices[triangle]], b = mesh.vertices[mesh.indices[triangle + 1]],
                                   c         = mesh.vertices[mesh.indices[triangle + 2]];
                        const F32  twiceArea = ((b - a) % (c - a)).mV[2];
                        require(twiceArea >= 0, "Reversed terrain triangle");
                        area += twiceArea * .5;
                    }
                    require(std::abs(area - size * size) < .001, "Terrain joins leave uncovered area");
                }
    std::cout << "PASS: terrain patch sizes, all LODs and neighboring strides\n";
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: viewer_geometry_test staged/app_settings/trees.xml");
        primitives();
        terrain();
        trees(argv[1]);
        grass();
        flexible();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}

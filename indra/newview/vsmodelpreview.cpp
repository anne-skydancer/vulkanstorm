// The original import scene, physics and rig controls on native preview targets.
// LGPL-2.1, like llmodelpreview.cpp.
#include "llviewerprecompiledheaders.h"
#include "llmodelpreview.h"
#include "vsscenepreview.h"
#include "vsworldavatar.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "llavatarappearance.h"
#include "llavatarjoint.h"
#include "lljoint.h"
#include "llskinningutil.h"
#include "llviewercontrol.h"
#include "llviewercamera.h"
#include "lltabcontainer.h"
#include "pipeline.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <numeric>
#include <utility>

struct LLModelPreview::NativeModel
{
    // Slots belong to this preview, not a global pointer-key cache. A retired
    // model never aliases a replacement allocation at the same address.
    std::vector<VSWorldRenderer::Mesh> meshes;
    std::vector<LLColor4U>             hullColors;
};
namespace
{
using Vertex   = VSWorldRenderer::Vertex;
using Packet   = VSWorldRenderer::Packet;
using Mesh     = VSWorldRenderer::Mesh;
using Topology = VSWorldRenderer::Topology;
struct PreviewPaint
{
    VSScenePreview&     preview;
    std::vector<Mesh>&  cache;
    std::vector<Packet> packets;
    std::size_t         next     = 0;
    bool                complete = true, clear = false;
    glm::mat4           camera{ 1 };
    void                submit(std::vector<Vertex> vertices, std::vector<unsigned> indices, Packet state)
    {
        if (vertices.empty() || indices.empty())
            return;
        if (next == cache.size())
            cache.emplace_back();
        auto&      mesh = cache[next++];
        const bool same =
            mesh && VSWorldRenderer::geometryVertices(mesh).size() == vertices.size() &&
            VSWorldRenderer::geometryIndices(mesh) == indices &&
            std::memcmp(VSWorldRenderer::geometryVertices(mesh).data(), vertices.data(), vertices.size() * sizeof(Vertex)) == 0;
        if (!same && !preview.renderer().publishMesh(mesh, vertices, indices))
        {
            complete = false;
            return;
        }
        state.mesh             = mesh;
        state.fullbright       = true;
        state.clearDepthBefore = std::exchange(clear, false);
        packets.push_back(std::move(state));
    }
    void line(const LLVector3& a, const LLVector3& b, F32 width, Packet state)
    {
        std::vector<Vertex> vertices;
        for (const auto& uv : std::array<std::array<float, 2>, 4>{ { { 0, -1 }, { 1, -1 }, { 1, 1 }, { 0, 1 } } })
            vertices.push_back({ a[0], a[1], a[2], b[0], b[1], b[2], uv[0], uv[1] });
        state.screenLinePixels = width;
        state.cull             = false;
        submit(std::move(vertices), { 0, 1, 2, 0, 2, 3 }, state);
    }
    void point(const Vertex& point, F32 pixels, Packet state)
    {
        std::vector<Vertex> vertices(4, point);
        constexpr float     corners[4][2]{ { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (unsigned i = 0; i < 4; ++i)
        {
            vertices[i].u = corners[i][0];
            vertices[i].v = corners[i][1];
        }
        state.screenPointPixels = pixels;
        state.cull              = false;
        submit(std::move(vertices), { 0, 1, 2, 0, 2, 3 }, state);
    }
    void edges(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, F32 width, Packet state)
    {
        std::vector<Vertex>   quads;
        std::vector<unsigned> triangles;
        quads.reserve(indices.size() * 4);
        triangles.reserve(indices.size() * 6);
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            for (unsigned edge = 0; edge < 3; ++edge)
            {
                const auto& a     = vertices[indices[i + edge]];
                const auto& b     = vertices[indices[i + (edge + 1) % 3]];
                const U32   first = U32(quads.size());
                for (const auto& uv : std::array<std::array<float, 2>, 4>{ { { 0, -1 }, { 1, -1 }, { 1, 1 }, { 0, 1 } } })
                {
                    Vertex      vertex{ a.x, a.y, a.z, b.x, b.y, b.z, uv[0], uv[1] };
                    const auto& endpoint = uv[0] == 0 ? a : b;
                    vertex.r             = endpoint.r;
                    vertex.g             = endpoint.g;
                    vertex.b             = endpoint.b;
                    vertex.a             = endpoint.a;
                    quads.push_back(vertex);
                }
                triangles.insert(triangles.end(), { first, first + 1, first + 2, first, first + 2, first + 3 });
            }
        state.screenLinePixels = width;
        state.cull             = false;
        submit(std::move(quads), std::move(triangles), state);
    }
    LLColor4 light(const glm::vec3& normal) const
    {
        // previewV.glsl and enableLightsPreview: three directional lights in
        // eye coordinates plus the exact existing preview ambient setting.
        LLColor4                 result = LLPipeline::PreviewAmbientColor;
        std::array<LLVector3, 3> directions{ LLPipeline::PreviewDirection0, LLPipeline::PreviewDirection1, LLPipeline::PreviewDirection2 };
        const std::array<LLColor4, 3> colors{ LLPipeline::PreviewDiffuse0, LLPipeline::PreviewDiffuse1, LLPipeline::PreviewDiffuse2 };
        auto                          norm = glm::mat3(camera) * normal;
        if (glm::dot(norm, norm) > 0)
            norm = glm::normalize(norm);
        for (unsigned i = 0; i < 3; ++i)
        {
            directions[i].normalize();
            const F32 amount = std::max(0.f, norm.x * directions[i][0] + norm.y * directions[i][1] + norm.z * directions[i][2]);
            for (unsigned c = 0; c < 3; ++c)
                result[c] += colors[i][c] * amount;
        }
        return result;
    }
    std::vector<Vertex> vertices(const VSPreviewBuffer& buffer, const LLMatrix4& transform, const std::vector<LLMatrix4a>& palette = {})
    {
        const glm::mat4     matrix       = glm::make_mat4(&transform.mMatrix[0][0]);
        const auto          normalMatrix = glm::transpose(glm::inverse(glm::mat3(matrix)));
        std::vector<Vertex> output;
        output.reserve(buffer.getNumVerts());
        for (U32 i = 0; i < buffer.getNumVerts(); ++i)
        {
            const auto* p  = buffer.attribute(LLVertexBuffer::TYPE_VERTEX, i);
            const auto* n  = buffer.attribute(LLVertexBuffer::TYPE_NORMAL, i);
            const auto* uv = buffer.attribute(LLVertexBuffer::TYPE_TEXCOORD0, i);
            if (!p)
                return {};
            LLVector4a position, normal;
            position.loadua(p);
            if (n)
                normal.loadua(n);
            else
                normal.set(0, 0, 1, 0);
            if (!palette.empty())
            {
                const auto* weights = buffer.attribute(LLVertexBuffer::TYPE_WEIGHT4, i);
                if (!weights)
                    return {};
                LLMatrix4a skin;
                LLSkinningUtil::getPerVertexSkinMatrix(const_cast<F32*>(weights), palette.data(), false, skin, U32(palette.size()));
                skin.affineTransform(position, position);
                skin.rotate(normal, normal);
            }
            const auto* xyz       = position.getF32ptr();
            const auto* direction = normal.getF32ptr();
            const auto  point     = matrix * glm::vec4(xyz[0], xyz[1], xyz[2], 1);
            auto        norm      = normalMatrix * glm::vec3(direction[0], direction[1], direction[2]);
            if (glm::dot(norm, norm) > 0)
                norm = glm::normalize(norm);
            Vertex     vertex{ point.x, point.y, point.z, norm.x, norm.y, norm.z, uv ? uv[0] : 0, uv ? 1 - uv[1] : 0 };
            const auto color = light(norm);
            vertex.r         = color[0];
            vertex.g         = color[1];
            vertex.b         = color[2];
            vertex.a         = color[3];
            output.push_back(vertex);
        }
        return output;
    }
    void face(const VSPreviewBuffer& buffer, const LLMatrix4& transform, Packet state, bool outline, F32 width, const LLColor4& edgeColor,
              const std::vector<LLMatrix4a>& palette = {})
    {
        auto positions = vertices(buffer, transform, palette);
        if (positions.size() != buffer.getNumVerts())
        {
            complete = false;
            return;
        }
        std::vector<unsigned> indices(buffer.indices(), buffer.indices() + buffer.getNumIndices());
        submit(positions, indices, state);
        if (outline)
        {
            state.image = {};
            state.color = { edgeColor[0], edgeColor[1], edgeColor[2], edgeColor[3] };
            edges(positions, indices, width, state);
        }
    }
    void sphereAndLine(const LLMatrix4& transform, const LLVector3& end, F32 scale, const LLVector3& occluded, const LLVector3& visible)
    {
        const glm::mat4 matrix         = glm::make_mat4(&transform.mMatrix[0][0]);
        const auto      transformPoint = [&](const LLVector3& value)
        {
            const auto p = matrix * glm::vec4(value[0], value[1], value[2], 1);
            return LLVector3(p.x, p.y, p.z);
        };
        std::vector<Vertex>   vertices;
        std::vector<unsigned> indices;
        // Same latitude/longitude construction and winding as gSphere.renderGGL.
        for (unsigned lat = 0; lat <= 20; ++lat)
            for (unsigned lon = 0; lon <= 30; ++lon)
            {
                const F32  a = F_TWO_PI * F32(lat) / 20, b = F_TWO_PI * F32(lon) / 30;
                const auto p = transformPoint(LLVector3(std::sin(a) * std::cos(b), std::sin(a) * std::sin(b), std::cos(a)) * scale);
                vertices.push_back({ p[0], p[1], p[2], 0, 0, 1, 0, 0 });
            }
        for (unsigned lat = 0; lat < 20; ++lat)
            for (unsigned lon = 0; lon < 30; ++lon)
            {
                const unsigned a = lat * 31 + lon, b = a + 1, c = a + 31, d = c + 1;
                indices.insert(indices.end(), { a, b, c, c, b, d });
            }
        for (unsigned pass = 0; pass < 2; ++pass)
        {
            Packet     state;
            const auto color = pass ? occluded : visible;
            state.color      = { color[0], color[1], color[2], 1 };
            state.depthWrite = pass == 0;
            if (pass)
                state.depthCompare = VSWorldRenderer::DepthCompare::Greater;
            line(transformPoint(LLVector3::zero), transformPoint(end), 1, state);
            submit(vertices, indices, state);
        }
    }
};
Packet colorPacket(const LLColor4& color, bool blend = false)
{
    Packet packet;
    packet.color      = { color[0], color[1], color[2], color[3] };
    packet.alpha      = blend;
    packet.depthWrite = true;
    packet.cull       = true;
    return packet;
}
} // namespace

bool LLModelPreview::drawNative(const LLRect& rectangle)
{
    if (!mNativePreview)
        return false;
    if (!mCurrent() || !mNativePreview->current())
        return true;
    if (needsRender())
        render();
    auto* context = gViewerWindow ? gViewerWindow->nativeContext() : nullptr;
    if (context && mNativePreview->image())
        context->resources().imageLocalRectangle(
            mNativePreview->image(),
            LLRectf(F32(rectangle.mLeft + 1), F32(rectangle.mTop - 1), F32(rectangle.mRight - 1), F32(rectangle.mBottom + 1)),
            LLColor4::white);
    else if (context)
        context->resources().deferDraw();
    return true;
}

bool LLModelPreview::renderNative(bool textures, bool edges, bool physics, bool skin, bool jointOverrides, bool jointPositions,
                                  bool uvGuide, F32 explode)
{
    if (!mNativePreview || !mNativePreview->current())
        return false;
    if (!mNativeModel)
        mNativeModel = std::make_shared<NativeModel>();
    mNativePreview->avatar().advancePreview();
    auto&      avatar        = mNativePreview->avatar().appearanceOwner();
    auto*      fmp           = LLFloaterModelPreview::sInstance;
    const auto numberedJoint = [&](S32 number) -> LLJoint*
    {
        // The real UI skeleton contains bones, collision volumes and attachment
        // joints. Match LLVOAvatar's numbered lookup without an LLVO surrogate.
        std::function<LLJoint*(LLJoint*)> find = [&](LLJoint* joint) -> LLJoint*
        {
            if (joint->getJointNum() == number)
                return joint;
            for (auto* child : joint->mChildren)
                if (auto* found = find(child))
                    return found;
            return nullptr;
        };
        return number < 0 ? nullptr : find(avatar.mRoot);
    };
    const LLVector3 offset   = mCameraOffset;
    LLVector3       target   = mPreviewTarget + offset;
    F32             farClip  = mCameraDistance * 10 + mPreviewScale.magVec() + offset.magVec();
    F32             distance = mCameraDistance;
    if (skin)
    {
        target   = avatar.getCharacterPosition() + offset;
        farClip  = 1024;
        distance = SKIN_WEIGHT_CAMERA_DISTANCE;
        refresh();
    }
    const LLQuaternion rotation = LLQuaternion(mCameraPitch, LLVector3::y_axis) * LLQuaternion(mCameraYaw, LLVector3::z_axis);
    LLVector3          eye      = target + (LLVector3(distance, 0, 0) + offset) * rotation;
    PreviewPaint       paint{ *mNativePreview, mNativeModel->meshes };
    const auto         glvec = [](const LLVector3& v)
    {
        return glm::vec3(v[0], v[1], v[2]);
    };
    paint.camera            = glm::lookAtRH(glvec(eye), glvec(target), glvec(LLVector3::z_axis));
    const auto settingColor = [](const char* name)
    {
        return gSavedSettings.getColor4(name);
    };
    const auto base = settingColor("MeshPreviewBaseColor"), edge = settingColor("MeshPreviewEdgeColor");
    const F32  edgeWidth = gSavedSettings.getF32("MeshPreviewEdgeWidth");
    if (!mBaseModel.empty() && mVertexBuffer[LLModel::NUM_LODS].empty())
        genBuffers(-1, skin);
    if (!mModel[mPreviewLOD].empty())
    {
        mFMP->childEnable("reset_btn");
        const auto& buffers    = mVertexBuffer[mPreviewLOD];
        const bool  regenerate = buffers.empty() || buffers.begin()->second.empty() ||
                                 buffers.begin()->second.front()->hasDataType(LLVertexBuffer::TYPE_WEIGHT4) != skin;
        if (regenerate)
            genBuffers(mPreviewLOD, skin);
        if (physics && mVertexBuffer[LLModel::LOD_PHYSICS].empty())
            genBuffers(LLModel::LOD_PHYSICS, false);
        const auto material = [&](const LLModelInstance& instance, std::size_t face)
        {
            Packet                  state = colorPacket(base);
            LLViewerFetchedTexture* image = nullptr;
            if (textures && face < instance.mModel->mMaterialList.size())
            {
                const auto found = instance.mMaterial.find(instance.mModel->mMaterialList[face]);
                if (found != instance.mMaterial.end())
                {
                    state = colorPacket(found->second.mDiffuseColor);
                    if (found->second.mOpaqueData)
                        image = (*reinterpret_cast<LLPointer<LLViewerFetchedTexture>*>(found->second.mOpaqueData)).get();
                }
            }
            else if (uvGuide)
                image = mUVGuideTexture;
            if (image)
            {
                state.image = vs_ui_image_snapshot(image->getID());
                if (state.image)
                    mTextureSet.insert(image);
            }
            return state;
        };
        if (!skin)
        {
            for (const auto& instance : mUploadData)
            {
                LLModel* model = instance.mLOD[mPreviewLOD];
                if (!model)
                    continue;
                const auto& faces = mVertexBuffer[mPreviewLOD][model];
                for (std::size_t i = 0; i < faces.size(); ++i)
                    paint.face(*faces[i], instance.mTransform, material(instance, i), edges, edgeWidth, edge);
            }
            if (physics)
            {
                paint.clear = true; // Original glClear(GL_DEPTH_BUFFER_BIT), after visible model.
                for (unsigned pass = 0; pass < 2; ++pass)
                {
                    for (const auto& instance : mUploadData)
                    {
                        LLModel* model = instance.mLOD[LLModel::LOD_PHYSICS];
                        if (!model)
                            continue;
                        bool renderMesh = true;
                        if (auto* decomp = gMeshRepo.mDecompThread)
                        {
                            LLMutexLock lock(decomp->mMutex);
                            auto&       decomposition = model->mPhysics;
                            if (!decomposition.mHull.empty())
                            {
                                renderMesh = false;
                                if (decomposition.mMesh.empty())
                                    gMeshRepo.buildPhysicsMesh(decomposition);
                                for (U32 hull = 0; hull < decomposition.mMesh.size(); ++hull)
                                {
                                    if (hull + 1 >= mNativeModel->hullColors.size())
                                        mNativeModel->hullColors.push_back(
                                            LLColor4U(rand() % 128 + 127, rand() % 128 + 127, rand() % 128 + 127, 128));
                                    auto state = colorPacket(LLColor4(mNativeModel->hullColors[hull]), pass != 0);
                                    if (!pass)
                                    {
                                        VSWorldRenderer::Blend blend;
                                        blend.writeMask = 0;
                                        state.blend     = blend;
                                    }
                                    const auto& source    = decomposition.mMesh[hull].mPositions;
                                    glm::mat4   transform = glm::make_mat4(&instance.mTransform.mMatrix[0][0]);
                                    if (explode > 0)
                                    {
                                        const auto displacement = (model->mHullCenter[hull] - model->mCenterOfHullCenters) * explode;
                                        transform               = glm::translate(transform, glvec(displacement));
                                    }
                                    std::vector<Vertex>   vertices;
                                    std::vector<unsigned> indices;
                                    for (const auto& point : source)
                                    {
                                        const auto p = transform * glm::vec4(point[0], point[1], point[2], 1);
                                        vertices.push_back({ p.x, p.y, p.z, 0, 0, 1, 0, 0 });
                                        indices.push_back(U32(indices.size()));
                                    }
                                    paint.submit(std::move(vertices), std::move(indices), state);
                                }
                            }
                        }
                        if (renderMesh && pass)
                        {
                            auto state = colorPacket(settingColor("MeshPreviewPhysicsFillColor"), true);
                            for (const auto& face : mVertexBuffer[LLModel::LOD_PHYSICS][model])
                                paint.face(*face, instance.mTransform, state, true, gSavedSettings.getF32("MeshPreviewPhysicsEdgeWidth"),
                                           settingColor("MeshPreviewPhysicsEdgeColor"));
                        }
                    }
                    if (mHasDegenerate)
                    {
                        for (const auto& instance : mUploadData)
                        {
                            LLModel* model = instance.mLOD[LLModel::LOD_PHYSICS];
                            if (!model)
                                continue;
                            auto* decomp = gMeshRepo.mDecompThread;
                            if (!decomp)
                                continue;
                            LLMutexLock lock(decomp->mMutex);
                            if (!model->mPhysics.mHull.empty())
                                continue;
                            for (const auto& face : mVertexBuffer[LLModel::LOD_PHYSICS][model])
                            {
                                const auto  positions = paint.vertices(*face, instance.mTransform);
                                const auto* indices   = face->indices();
                                for (U32 i = 0; i + 2 < face->getNumIndices(); i += 3)
                                {
                                    LLVector4a a, b, c, scale(.5f);
                                    a.loadua(face->attribute(LLVertexBuffer::TYPE_VERTEX, indices[i]));
                                    a.mul(scale);
                                    b.loadua(face->attribute(LLVertexBuffer::TYPE_VERTEX, indices[i + 1]));
                                    b.mul(scale);
                                    c.loadua(face->attribute(LLVertexBuffer::TYPE_VERTEX, indices[i + 2]));
                                    c.mul(scale);
                                    if (!ll_is_degenerate(a, b, c))
                                        continue;
                                    auto state         = colorPacket(settingColor("MeshPreviewDegenerateFillColor"), pass != 0);
                                    state.depthCompare = VSWorldRenderer::DepthCompare::Always;
                                    state.doubleSided  = true;
                                    state.cull         = false;
                                    if (!pass)
                                    {
                                        VSWorldRenderer::Blend blend;
                                        blend.writeMask = 0;
                                        state.blend     = blend;
                                    }
                                    paint.submit(positions, { indices[i], indices[i + 1], indices[i + 2] }, state);
                                    state.color      = {};
                                    const auto color = settingColor("MeshPreviewDegenerateEdgeColor");
                                    state.color      = { color[0], color[1], color[2], 1 };
                                    paint.edges(positions, { indices[i], indices[i + 1], indices[i + 2] },
                                                gSavedSettings.getF32("MeshPreviewDegenerateEdgeWidth"), state);
                                    for (unsigned j = 0; j < 3; ++j)
                                        paint.point(positions[indices[i + j]], gSavedSettings.getF32("MeshPreviewDegeneratePointSize"),
                                                    state);
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // Original preview clearAttachmentOverrides, including pelvis fixup.
            for (S32 i = 0; i < LL_CHARACTER_MAX_ANIMATED_JOINTS; ++i)
                if (auto* joint = numberedJoint(i))
                {
                    joint->clearAttachmentPosOverrides();
                    joint->clearAttachmentScaleOverrides();
                }
            F32    previousFixup;
            LLUUID fixupId;
            while (avatar.hasPelvisFixup(previousFixup, fixupId))
                avatar.removePelvisFixup(fixupId);
            if (auto* pelvis = avatar.getJoint("mPelvis"))
                pelvis->setPosition(LLVector3::zero);
            avatar.mRoot->updateWorldMatrixChildren();
            avatar.computeBodySize();
            LLUUID localMesh;
            localMesh.generate();
            avatar.addPelvisFixup(mPelvisZOffset, localMesh);
            target            = avatar.getCharacterPosition();
            eye               = target + (LLVector3(distance, 0, 0) + offset) * rotation;
            paint.camera      = glm::lookAtRH(glvec(eye), glvec(target), glvec(LLVector3::z_axis));
            bool pelvisRecalc = false;
            for (auto& [transform, instances] : mScene[mPreviewLOD])
                for (auto& instance : instances)
                {
                    LLModel* model = instance.mModel;
                    if (model->mSkinWeights.empty())
                        continue;
                    auto&     info  = model->mSkinInfo;
                    const U32 count = LLSkinningUtil::getMeshJointCount(&info);
                    if (!info.mJointNumsInitialized)
                    {
                        for (U32 j = 0; j < info.mJointNames.size(); ++j)
                        {
                            auto* joint =
                                info.mJointNums[j] == -1 ? avatar.getJoint(info.mJointNames[j]) : numberedJoint(info.mJointNums[j]);
                            info.mJointNums[j] = joint ? joint->getJointNum() : 0;
                        }
                        info.mJointNumsInitialized = true;
                    }
                    if (jointOverrides && count && count == info.mAlternateBindMatrix.size())
                    {
                        LLUUID overrideId;
                        overrideId.generate();
                        for (U32 j = 0; j < count; ++j)
                            if (auto* joint = numberedJoint(info.mJointNums[j]))
                            {
                                const LLVector3 position(info.mAlternateBindMatrix[j].getTranslation());
                                if (joint->aboveJointPosThreshold(position))
                                {
                                    bool changed = false;
                                    joint->addAttachmentPosOverride(position, overrideId, "model", changed);
                                    if (changed && joint->getName() == "mPelvis")
                                        pelvisRecalc = true;
                                    if (info.mLockScaleIfJointPosition)
                                        joint->addAttachmentScaleOverride(joint->getDefaultScale(), overrideId, "model");
                                }
                            }
                    }
                    if (count > info.mInvBindMatrix.size())
                    {
                        paint.complete = false;
                        continue;
                    }
                    bool paletteValid=count!=0;
                    std::vector<LLMatrix4a> palette(count);
                    for (U32 j = 0; j < count; ++j)
                    {
                        auto* joint = numberedJoint(info.mJointNums[j]);
                        if (!joint)
                        {
                            paletteValid=false;
                            break;
                        }
                        matMulUnsafe(info.mInvBindMatrix[j], joint->getWorldMatrix4a(), palette[j]);
                    }
                    if(!paletteValid){paint.complete=false;continue;}
                    LLMatrix4   identity;
                    const auto& faces = mVertexBuffer[mPreviewLOD][model];
                    for (std::size_t i = 0; i < faces.size(); ++i)
                        paint.face(*faces[i], identity, material(instance, i), edges, edgeWidth, edge, palette);
                }
            if (jointPositions)
            {
                // Actual collision-volume and skeleton transforms, original
                // selected/overridden/ordinary colors and occluded depth pass.
                for (S32 i = 0; i < avatar.mNumCollisionVolumes; ++i)
                {
                    auto& volume = avatar.mCollisionVolumes[i];
                    volume.updateWorldMatrix();
                    paint.sphereAndLine(volume.getXform()->getWorldMatrix(), volume.getEnd(), 1, LLVector3(0, 0, 1),
                                        LLVector3(.5f, .5f, 1));
                    paint.sphereAndLine(volume.getXform()->getWorldMatrix(), volume.getEnd(), .05f, LLVector3(1, 1, 1), LLVector3(1, 1, 1));
                }
                const bool selectedTab = fmp && fmp->mTabContainer->getCurrentPanelIndex() == fmp->mAvatarTabIndex;
                for (auto* joint : avatar.getSkeleton())
                    if (joint)
                    {
                        LLVector3 overridden;
                        LLUUID    id;
                        F32       scale = .001f;
                        LLVector3 occluded(0, 1, 0), visible(.5f, .5f, .5f);
                        if (selectedTab && joint->getName() == fmp->mSelectedJointName)
                        {
                            scale *= 16;
                            occluded = LLVector3(1, 1, 0);
                        }
                        else if (joint->hasAttachmentPosOverride(overridden, id))
                            occluded = LLVector3(1, 0, 0);
                        joint->updateWorldMatrix();
                        paint.sphereAndLine(joint->getXform()->getWorldMatrix(), joint->getEnd(), scale, occluded, visible);
                    }
                Packet state = colorPacket(LLColor4(1, 0, 1, 1));
                for (unsigned i = 0; i < 4; ++i)
                    paint.line(mGroundPlane[i], mGroundPlane[(i + 1) % 4], 1, state);
            }
            if (pelvisRecalc)
            {
                avatar.mRoot->updateWorldMatrixChildren();
                avatar.computeBodySize();
            }
        }
    }
    paint.cache.resize(paint.next);
    if (!paint.complete)
    {
        refresh();
        if (auto* context = gViewerWindow->nativeContext())
            context->resources().deferDraw();
        return false;
    }
    const auto background = settingColor("MeshPreviewCanvasColor");
    const F32  nearClip   = llclamp(farClip * .001f, .001f, .1f);
    const bool rendered   = mNativePreview->render(getWidth(), getHeight(), eye, target, LLVector3::z_axis,
                                                   LLViewerCamera::getInstance()->getDefaultFOV() / mCameraZoom, nearClip, farClip,
                                                   paint.packets, { background[0], background[1], background[2], background[3] });
    if (!rendered)
        refresh();
    return rendered;
}

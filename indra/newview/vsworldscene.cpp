// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsbakedtexturepolicy.h"
#include "lltoolmgr.h"
#include "fsdiscordconnect.h"
#include "vsobjecteditparams.h"
#include "llavatarappearancedefines.h"
#include "vsregionlimits.h"
#include "vsworldscene.h"
#include "vsspatialindex.h"
#include "vsvolumevisibility.h"
#include "vsfacealphapolicy.h"
#include "vsbuiltintextures.h"
#include "vsimagealphamask.h"
#include "lldrawpoolalpha.h"
extern bool gCubeSnapshot;
#include "llcamera.h"
#include "vsobjectbeaconpolicy.h"
#include "llaudioengine.h"
#include "llagentcamera.h"
#include "llviewercamera.h"
#include "llviewerwindow.h"
#include "vsregioncontext.h"
#include "llviewerobjectlist.h"
#include "fsassetblacklist.h"
#include "vscamerafocusobject.h"
#include "vssurfacecoordinates.h"
#include "vspickpolicy.h"
#include "llmaterial.h"
#include "llclickaction.h"
#include "raytrace.h"
#include "vsobjectindex.h"
#include "vsparcelgeometry.h"
#include "llcamera.h"
#include "llenvironment.h"
#include "vsskygeometry.h"
#include "vsmaterialassets.h"
#include "vsobjectmedia.h"
#include "llgltfmateriallist.h"
#include "llviewertextureanim.h"
#include "vsterrainpacket.h"
#include "vsworldavatar.h"
#include "lldrawable.h"
#include "vsavatarrigginginfo.h"
#include "vsavatarnametag.h"
#include "llmanipscale.h"
#include "llmodel.h"
#include "llchat.h"
#include "vsworldassets.h"
#include "vsworldgltf.h"
#include "vsnormalbasis.h"
#include "vsflexiblepath.h"
#include "vswindfield.h"
#include "vsvegetation.h"
#include "vsterraingeometry.h"
#include "vsterrainmaterial.h"
#include "vsterrainassets.h"
#include "vsrendercomplexity.h"
#include "vsvolumelod.h"
#include "vsvegetationlod.h"
#include "llmeshrepository.h"
#include "llpartdata.h"
#include "lltextureanim.h"
#include "llgenericstreamingmessage.h"
#include "llsdserialize.h"
#include "llhudobject.h"
#include "llhudmanager.h"
#include "llhudicon.h"
#include "vsuiimageprovider.h"
#include "llagent.h"
#include <utility>
#include "llhudeffectlookat.h"
#include "llhudeffectpointat.h"
#include "llcharacter.h"
#include "lljoint.h"
#include "llvograss.h"
#include "pipeline.h"
#include "vsworldmotion.h"
#include "vsnativesession.h"
#include "llregionhandle.h"
#include "patch_dct.h"
#include <limits>
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "llimage.h"
#include "llprimitive.h"
#include "llvolumemessage.h"
#include "llvolumemgr.h"
#include "llviewercontrol.h"
#include "llvolume.h"
#include "lltextureentry.h"
#include "llui.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llframetimer.h"
#include "llviewerregion.h"
#include "lldatapacker.h"
#include "message.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
namespace
{
constexpr std::size_t maxObjects=16384;
S32 meshDetail(const LLVector3& position, const LLVector3& scale, const LLVector3& eye, bool hud, const LLVector3* scaleBias = nullptr,
               bool selected = false)
{
    if (hud)
        return 3;
    const S32 selectedLOD = gSavedSettings.getS32("DebugSelectionLODs");
    if (selected && selectedLOD >= 0)
        return llmin(selectedLOD, 3);
    const F32 factor      = llclamp(gSavedSettings.getF32("RenderVolumeLODFactor"), 0.f, 16.f);
    LLVector3 radiusScale = scale;
    if (scaleBias)
        radiusScale.scaleVec(*scaleBias);
    const F32 radius   = ll_round(radiusScale.length(), .01f);
    const F32 distance = std::max(ll_round(vs_volume_lod_distance((position - eye).length(), factor, 1.f - factor * .1f), .01f), .01f);
    // Native presentation currently uses the reference default PI/3 FOV.
    return vs_volume_lod_detail(distance, radius, factor, gSavedSettings.getBOOL("RenderDynamicLOD"));
}
bool finiteVector(const LLVector3& v) { return std::isfinite(v.mV[0]) && std::isfinite(v.mV[1]) && std::isfinite(v.mV[2]); }
U32 littleU32(const U8* p) { return U32(p[0]) | U32(p[1])<<8 | U32(p[2])<<16 | U32(p[3])<<24; }
F32 littleFloat(const U8* p) { const U32 n=littleU32(p);F32 v;std::memcpy(&v,&n,4);return v; }
LLVector3 vectorAt(const U8* p) { return {littleFloat(p),littleFloat(p+4),littleFloat(p+8)}; }
LLUUID    attachmentItem(const std::string& values)
{
    if (values.size() > 4096)
        return LLUUID::null;
    std::istringstream input(values);
    std::string        line;
    while (std::getline(input, line))
    {
        std::istringstream value(line);
        std::string        name, type, access, scope, uuid;
        if (value >> name >> type >> access >> scope >> uuid && name == "AttachItemID" && type == "STRING")
        {
            LLUUID item;
            if (LLUUID::parseUUID(uuid, &item))
                return item;
        }
    }
    return LLUUID::null;
}
bool baseVolume(const std::vector<U8>& extra)
{
    if (extra.empty()) return true;
    std::size_t p=1;
    for (unsigned i=0;i<extra[0];++i)
    {
        if (extra.size()-p<6) return false;
        const U16 type=U16(extra[p]) | U16(extra[p+1])<<8;
        const U32 length=littleU32(extra.data()+p+2);p+=6;
        // Do not misrepresent mesh/sculpt/flexible assets as undeformed prims.
        if (length>extra.size()-p || type==0x10 || type==0x30 || type==LLNetworkData::PARAMS_MESH) return false;
        p+=length;
    }
    return p==extra.size();
}
bool flexibleData(const U8* bytes, U32 length, LLFlexibleObjectData& attributes)
{
    if (length != 4 && length != 16)
        return false;
    U8 buffer[16];
    std::copy_n(bytes, length, buffer);
    LLDataPackerBinaryBuffer data(buffer, S32(length));
    return attributes.unpack(data) && finiteVector(attributes.getUserForce()) && attributes.getUserForce().lengthSquared() <= 1.e12f;
}
bool volumeExtras(const std::vector<U8>& extra, LLVolumeParams& params, std::optional<LLFlexibleObjectData>& flexible)
{
    if (baseVolume(extra)) return true;
    if (extra.empty()) return true;
    std::size_t p=1;
    for (unsigned i=0;i<extra[0];++i)
    {
        if (extra.size()-p<6) return false;
        const U16 type=U16(extra[p]) | U16(extra[p+1])<<8;
        const U32 length=littleU32(extra.data()+p+2);p+=6;
        if (length > extra.size() - p)
            return false;
        if (type == 0x10)
        {
            LLFlexibleObjectData attributes;
            if (!flexibleData(extra.data() + p, length, attributes))
                return false;
            flexible = attributes;
            if (!params.setType(params.getProfileParams().getCurveType(), LL_PCODE_PATH_FLEXIBLE))
                return false;
        }
        if (type==0x30 || type==LLNetworkData::PARAMS_MESH)
        {
            if (length!=17) return false;
            LLUUID asset;std::memcpy(asset.mData,extra.data()+p,UUID_BYTES);
            if (asset.isNull() || !params.setSculptID(asset,extra[p+16])) return false;
        }
        p+=length;
    }
    return p == extra.size() && (!flexible || params.getSculptID().isNull());
}
bool renderMaterialData(const U8* data,U32 size,LLRenderMaterialParams& materials)
{
    if(!size || data[0]>14 || size!=1+17*U32(data[0])) return false;
    std::set<U8> faces;
    for(unsigned i=0;i<data[0];++i)
        if(data[1+17*i]>=45 || !faces.insert(data[1+17*i]).second) return false;
    LLDataPackerBinaryBuffer packed(const_cast<U8*>(data),S32(size));
    return materials.unpack(packed);
}
std::shared_ptr<LLViewerTextureAnim> textureAnimationData(const U8* data,U32 size)
{
    if(size!=16) return {};
    std::array<U8,20> bytes{};bytes[0]=16;std::copy_n(data,16,bytes.begin()+4);
    LLDataPackerBinaryBuffer packed(bytes.data(),20);
    auto animation=std::make_shared<LLViewerTextureAnim>(nullptr);
    animation->unpackTAMessage(packed);
    if(animation->mFace < -1 || animation->mFace>=45 || !std::isfinite(animation->mStart) ||
        !std::isfinite(animation->mLength) || animation->mLength<0 || !std::isfinite(animation->mRate)) return {};
    return animation;
}
bool ownerExtras(const std::vector<U8>& extra,U32& meshFlags,bool* reflectionProbe=nullptr)
{
    meshFlags=0;if (extra.empty()) return true;
    std::size_t position=1;
    for (unsigned i=0;i<extra[0];++i)
    {
        if (extra.size()-position<6) return false;
        const U16 type=U16(extra[position])|U16(extra[position+1])<<8;
        const U32 length=littleU32(extra.data()+position+2);position+=6;
        if (length>extra.size()-position) return false;
        if (type==LLNetworkData::PARAMS_EXTENDED_MESH)
        { if (length!=4) return false;meshFlags=littleU32(extra.data()+position); }
        if(type==LLNetworkData::PARAMS_REFLECTION_PROBE)
        {if(length!=9)return false;if(reflectionProbe)*reflectionProbe=true;}
        position+=length;
    }
    return position==extra.size();
}
}
struct VSWorldScene::Budgets
{
    LLUUID account;U64 accountGeneration=0;
    boost::signals2::signal<void(const ObjectHandle&)> objectSounds;
    boost::signals2::signal<void(const ObjectHandle&, S32)> materialUpdates;
    U64 particleRevision=1;
    std::map<LLHost, std::weak_ptr<Impl>> regions;
    VSResourceBudget objects{maxObjects},avatars{256},objectRequests{4096},primitiveBytes{128*1024*1024},terrainBytes{64*1024*1024};
};
struct VSWorldScene::Impl
{
    struct PickSurface
    {
        // Keep the immutable source generation already owned by CPU geometry;
        // no duplicate per-vertex arrays. Its residency survives refused replacement.
        LLPointer<LLVolume> volume;
        std::shared_ptr<const VSWorldAssets::Geometry> asset;
        std::shared_ptr<const VSResourceBudget::Reservation> volumeResidency;
        S32 face=0;
        LLVector4a center;
        LLVector3 position,scale;
        LLQuaternion rotation;
        bool volumeGlobal=false,normalGlobal=false,rigged=false;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    struct Face
    {
        VSWorldRenderer::Mesh                     mesh;
        LLTextureEntry                            material;
        LLPointer<LLFetchedGLTFMaterial>            pbrAsset;
        std::shared_ptr<const VSMaterial::State> surface;
        std::array<VSUIRenderer::Image,VSMaterial::ChannelCount-1> publishedChannels;
        U64                                       poseGeneration = 0;
        LLTextureEntry                            publishedMaterial;
        VSUIRenderer::Image                       publishedImage;
        bool                                      published = false;
        bool                                      publishedAlphaOnly=false;
        std::shared_ptr<const PickSurface> pickSurface;
        std::optional<LLMatrix4> pickTextureMatrix;
        std::optional<VSWorldContracts::Material> sceneMaterial;
    };
    static std::optional<Pick> surfacePick(const Face& face,const VSWorldRenderer::SurfaceHit& hit,const VSUIRenderer::Image& image)
    {
        Pick pick;
            const auto raw=face.pickSurface;
            if(raw && raw->volume && raw->face>=0 && raw->face<raw->volume->getNumVolumeFaces())
            {
                const auto& geometry=raw->volume->getVolumeFace(raw->face);
                if(!geometry.mTexCoords || !geometry.mNormals || !std::all_of(hit.triangle.begin(),hit.triangle.end(),
                    [&](unsigned index){return index<unsigned(geometry.mNumVertices);}))return {};
                LLVector2 st;LLVector4a normal,tangent;normal.clear();tangent.clear();
                for(unsigned vertex=0;vertex<3;++vertex)
                {
                    const auto index=hit.triangle[vertex];const F32 weight=hit.barycentric[vertex];
                    st.mV[0]+=geometry.mTexCoords[index].mV[0]*weight;st.mV[1]+=geometry.mTexCoords[index].mV[1]*weight;
                    if(!raw->normalGlobal)
                    {
                        LLVector4a n=geometry.mNormals[index],t;t.clear();if(geometry.mTangents)t=geometry.mTangents[index];
                        n.mul(weight);t.mul(weight);normal.add(n);tangent.add(t);
                    }
                }
                if(raw->normalGlobal)
                {
                    normal.load3(hit.rawNormal.data());tangent.set(hit.tangent[0],hit.tangent[1],hit.tangent[2],hit.tangent[3]);
                }
                else
                {
                    const auto transform=[&](const LLVector4a& direction)
                    {
                        LLVector3 value(direction.getF32ptr());value.scaleVec(LLVector3(1/raw->scale.mV[0],1/raw->scale.mV[1],1/raw->scale.mV[2]));
                        return value*raw->rotation;
                    };
                    const auto n=transform(normal),t=transform(tangent);const F32 handedness=tangent.getF32ptr()[3];
                    normal.load3(n.mV);tangent.set(t.mV[0],t.mV[1],t.mV[2],handedness);
                }
                normal.normalize3fast();tangent.normalize3fast();
                struct SurfaceFace
                {
                    const Impl::Face& face;const Impl::PickSurface& raw;
                    const LLTextureEntry* textureEntry()const{return &face.publishedMaterial;}
                    const LLVector4a& center()const{return raw.center;}
                    bool volumeGlobal()const{return raw.volumeGlobal;}
                    LLVector3 scale()const{return raw.scale;}
                    LLVector3 positionToVolume(const LLVector3& point)const
                    {
                        auto value=(point-raw.position)*~raw.rotation;
                        if(!raw.volumeGlobal)
                            value.scaleVec(LLVector3(1/raw.scale.mV[0],1/raw.scale.mV[1],1/raw.scale.mV[2]));
                        return value;
                    }
                    LLVector3 directionToVolume(const LLVector3& direction)const
                    {
                        auto value=direction*~raw.rotation;
                        if(!raw.volumeGlobal)
                            value.scaleVec(raw.scale);
                        return value;
                    }
                    const LLMatrix4* textureMatrix()const{return face.pickTextureMatrix?&*face.pickTextureMatrix:nullptr;}
                };
                LLVector4a point;point.load3(hit.point.data());
                const auto uv=VSSurfaceCoordinates::surfaceToTexture(SurfaceFace{face,*raw},st,point,normal);
                pick.st={st.mV[0],st.mV[1]};pick.uv={uv.mV[0],uv.mV[1]};pick.normal.set(normal.getF32ptr());
                std::copy_n(tangent.getF32ptr(),4,pick.tangent.begin());pick.surfaceKnown=true;
                if(image && uv.mV[0]>=0 && uv.mV[1]>=0)
                {
                    const auto extent=VSUIRenderer::imageExtent(image);
                    pick.xy={ll_round(uv.mV[0]*extent[0]),ll_round((1-uv.mV[1])*extent[1])};
                }
            }
        return pick.surfaceKnown?std::optional<Pick>(pick):std::nullopt;
    }
    struct Object
    {
        std::vector<std::pair<std::shared_ptr<const VSWorldAssets::Geometry>, std::shared_ptr<const VSAvatarRiggingInfo>>> metricsRigging;
        LLUUID id;U32 parent=0;U8 pcode=LL_PCODE_VOLUME,attachment=0;LLVector3 position,scale,velocity,acceleration,angularVelocity;LLQuaternion rotation;
        LLUUID inventoryItem,reportedOwner;
        LLUUID soundAsset; F32 soundGain = 0, soundRadius = 0; U8 soundFlags = 0;
        LLPointer<LLHUDIcon> icon;
        std::shared_ptr<const VSWorldRenderer::AvatarTiming> avatarTiming;
        std::optional<U32> avatarTimingAttempt;
        std::shared_ptr<const VSWorldRenderer::AvatarTiming> attachmentTiming;
        std::optional<U32> attachmentTimingAttempt;
        std::optional<F32> attachmentGpuMilliseconds;
        std::optional<U32> attachmentComplexity;

        S32 nameRiggingLOD=-1;
        U64 nameRiggingGeneration=0;
        std::shared_ptr<const VSAvatarRiggingInfo> nameRigging;
        std::shared_ptr<const std::function<bool()>> nameTagGuard;

        bool reportedOwnerKnown=false;
        U32 updateFlags=0;
        U8 material=0,clickAction=0;bool reflectionProbe=false,soundSource=false,selectable=true;
        std::string nameValues;
        bool nameValuesPresent=false;
        std::shared_ptr<const VSResourceBudget::Reservation> metadataResidency;
        std::string mediaVersion;
        std::shared_ptr<VSObjectMedia> media;
        LLRenderMaterialParams renderMaterials;
        struct LegacyMaterialPreview
        {
            uuid_vec_t ids;
            std::vector<LLPointer<LLGLTFMaterial>> bases,overrides;
            std::shared_ptr<const VSResourceBudget::Reservation> residency;
        };
        std::shared_ptr<LegacyMaterialPreview> legacyPreview;
        std::shared_ptr<LLViewerTextureAnim> animation;
        S32 textureAnimMode=0;
        U64 generation=0,revision=0;VSWorldMotion motion;
        U64 parentGeneration=0;bool parentRetired=false;
        std::optional<LLPartSysData>                         particles;
        U64 particleRevision=0;bool particleFullMessage=false;
        bool                                                 light = false, textureAnimation = false;
        U8                                                   species           = 0;
        S32                                                  vegetationLOD     = -1;
        U64                                                  vegetationTerrain = 0;
        U32 extendedMeshFlags=0;
        std::map<LLUUID,S32> controlAnimations;
        std::shared_ptr<const VSResourceBudget::Reservation> controlAnimationResidency;
        std::shared_ptr<VSWorldAvatar>                       controlAvatar;
        std::optional<LLFlexibleObjectData>                  flexible;
        struct Flex
        {
            std::array<VSFlexibleSection, (1 << FLEXIBLE_OBJECT_MAX_SECTIONS) + 1> sections;
            S32                                                                    initialized = -1, simulation = -1, render = -1;
            double                                                                 time = 0;
            LLQuaternion                                                           lastRotation;
            std::array<float, ((1 << FLEXIBLE_OBJECT_MAX_SECTIONS) + 1) * 15 + 1>  pathSignature{};
            std::size_t                                                            pathSignatureSize = 0;
            std::shared_ptr<const VSResourceBudget::Reservation>                   residency;
        };
        std::shared_ptr<Flex>                                flex;
        std::shared_ptr<const VSResourceBudget::Reservation> slot,primitiveResidency;
        U16 taskSerial=0;bool taskKnown=false;double taskDeadline=0;std::string taskFile;std::optional<ObjectHandle> taskRequest;
        LLPointer<LLVolume> volume;LLVolumeParams params;
        std::shared_ptr<VSObjectEditParameters> edit;
        std::shared_ptr<const VSWorldAssets::Geometry> asset;
        std::shared_ptr<VSWorldGLTF>                   gltf;
        std::shared_ptr<VSWorldGLTF>                   pendingGLTF;
        S32 assetLOD=-1;
        U64                                            sculptGeneration = 0;
        std::vector<Face>                              faces;
        std::vector<Face>                              admittedFaces;
        bool                                           dirty = true, renderable = true;
        LLVector3                                      geometryPosition;
        LLQuaternion                                   geometryRotation;
        bool                                           geometryPoseKnown = false;
        U64 spatialEpoch=0,spatialRevision=0;
        const LLVolume* spatialVolume=nullptr;
        LLVector3 spatialScale,spatialPosition;
        LLQuaternion spatialRotation;
        bool spatialFlexible=false;
        void pruneMetricsRigging()
        {
            for (std::size_t index = 0; index < metricsRigging.size(); ++index)
            {
                auto& slot = metricsRigging[index];
                if (!slot.first) continue;
                const auto found = std::find_if(admittedFaces.begin(), admittedFaces.end(),
                    [&](const auto& face)
                    {
                        const auto raw = face.pickSurface;
                        return face.published && face.mesh && raw && raw->rigged &&
                            raw->face == S32(index) && raw->asset == slot.first;
                    });
                if (found == admittedFaces.end()) slot = {};
            }
            while (!metricsRigging.empty() && !metricsRigging.back().first) metricsRigging.pop_back();
        }
        void releaseRenderingResources()
        {
            for (auto& face : faces)
            {
                { face.mesh.reset(); face.pickSurface.reset(); face.pickTextureMatrix.reset(); }
                face.publishedImage.reset();
                face.publishedChannels = {};
                face.pbrAsset=nullptr;
            }
            admittedFaces.clear();
            metricsRigging.clear();
            if (gltf) gltf->releaseRenderingResources();
            if (pendingGLTF) pendingGLTF->releaseRenderingResources();
            dirty = true;
        }
    };
    VSWorldRenderer& renderer;VSUIResources& resources;
    std::shared_ptr<VSWorldAssets> assets;
    std::unique_ptr<VSMaterialAssets> materialAssets;
    std::unique_ptr<VSTerrainAssets> terrainAssets;
    std::map<U32,LLSD> pendingMaterialOverrides;
    std::shared_ptr<Budgets> budgets;
    LLHost host;U64 handle=0,incarnation=0,nextObjectGeneration=1,nextRevision=1;
    U32 width=256,regionHeight=256;
    bool retired=false;
    static VSWorldContracts::OwnerKind kind(const Object& object)
    {
        return object.pcode==LL_PCODE_LEGACY_AVATAR?VSWorldContracts::OwnerKind::Avatar:
            (object.extendedMeshFlags&LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)?VSWorldContracts::OwnerKind::ControlAvatar:
            object.attachment?VSWorldContracts::OwnerKind::Attachment:VSWorldContracts::OwnerKind::Object;
    }
    bool linksCurrent(const Object& object) const
    {
        const Object* child=&object;std::set<U32> visited;
        while(child->parent)
        {
            if(child->parentRetired || !visited.insert(child->parent).second || visited.size()>256)return false;
            const auto parent=objects.find(child->parent);
            if(parent==objects.end() || (child->parentGeneration && child->parentGeneration!=parent->second.generation))return false;
            child=&parent->second;
        }
        return true;
    }
    struct Request { double deadline=0; unsigned attempts=0;std::shared_ptr<const VSResourceBudget::Reservation> slot; };
    void packetOwner(VSWorldRenderer::Packet& packet,const Object& object,U32 local)const
    {
        std::copy_n(object.id.mData,UUID_BYTES,packet.objectIdentity.begin());
        packet.objectGeneration=object.generation;packet.faceGeneration=object.revision;
        packet.objectSource={host.getAddress(),host.getPort(),local,incarnation,budgets->accountGeneration};
        std::copy_n(budgets->account.mData,UUID_BYTES,packet.objectAccount.begin());
    }
    std::map<U32,Object> objects;
    std::set<U32> particleObjects;
    VSObjectIndex<LLUUID> objectIndex;std::map<U32,Request> pending;
    VSWorldAvatar*                                     skeletonOwner(const Object& object) const
    {
        const Object* owner = &object;
        std::set<U32> visited;
        for (unsigned depth = 0; owner && depth < 256; ++depth)
        {
            if (owner->controlAvatar)
                return owner->controlAvatar.get();
            const auto avatar = avatars.find(owner->id);
            if (avatar != avatars.end())
                return avatar->second.get();
            if (!owner->parent || !visited.insert(owner->parent).second)
                break;
            const auto parent = objects.find(owner->parent);
            owner             = parent == objects.end() ? nullptr : &parent->second;
        }
        return nullptr;
    }
    bool hudObject(const Object& object) const
    {
        const Object* child = &object;
        std::set<U32> visited;
        while (child->parent && visited.insert(child->parent).second && visited.size() <= 256)
        {
            const auto parent = objects.find(child->parent);
            if (parent == objects.end())
                break;
            if (parent->second.pcode == LL_PCODE_LEGACY_AVATAR)
            {
                const auto     avatar = avatars.find(parent->second.id);
                const unsigned state  = child->attachment;
                return avatar != avatars.end() && avatar->second->hudAttachment(((state & 15) << 4) | (state >> 4));
            }
            child = &parent->second;
        }
        return false;
    }
    struct AnimationState
    {
        std::map<LLUUID, S32>                                animations;
        double                                               deadline = 0;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::map<LLUUID, AnimationState> pendingAnimations;
    struct Visible
    {
        VSWorldRenderer::Packet                        packet;
        std::weak_ptr<const VSWorldRenderer::Geometry> mesh;
        std::weak_ptr<const VSUIRenderer::Texture>     image;
    };
    std::vector<Visible> visiblePackets;
    bool                 publishVolume(Object& object, LLPointer<LLVolume> volume, S32 lod, U64 sculptGeneration = 0)
    {
        if (!volume || volume->getNumVolumeFaces() <= 0 || volume->getNumVolumeFaces() > 45)
            return false;
        std::size_t bytes = sizeof(LLVolume);
        for (S32 index = 0; index < volume->getNumVolumeFaces(); ++index)
        {
            const auto& face = volume->getVolumeFace(index);
            if (face.mNumVertices < 0 || face.mNumIndices < 0)
                return false;
            bytes +=
                std::size_t(face.mNumVertices) * (sizeof(LLVector4a) * 4 + sizeof(LLVector2)) + std::size_t(face.mNumIndices) * sizeof(U16);
        }
        auto residency = budgets->primitiveBytes.reserve(bytes);
        if (!residency)
            return false;
        // Admit the entire replacement before retiring its CPU generation.
        // Matching GPU faces remain live until their own publication succeeds.
        object.volume             = std::move(volume);
        object.primitiveResidency = std::move(residency);
        object.assetLOD           = lod;
        object.sculptGeneration   = sculptGeneration;
        object.dirty              = true;
        object.revision           = nextRevision++;
        object.faces.resize(object.volume->getNumVolumeFaces());
        return true;
    }
    LLUUID agent;
    float timeDilation=1;
    std::map<LLUUID,std::unique_ptr<VSWorldAvatar>> avatars;
    struct TextureSource
    {
        std::shared_ptr<const Impl> owner;
        const Impl& scene;const Object& source;
        std::function<bool()> current;
        U64 sourceRevision=0,rootRevision=0;
        bool live()const{return current && current() && source.revision==sourceRevision && scene.linksCurrent(source);}
        struct Root
        {
            const Object* object=nullptr;
            bool isAnimatedObject()const{return object && object->pcode==LL_PCODE_VOLUME &&
                (object->extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG);}
        } root;
        bool rootKnown=false;
        TextureSource(std::shared_ptr<const Impl> state,const Object& object,std::function<bool()> guard)
            :owner(std::move(state)),scene(*owner),source(object),current(std::move(guard)),sourceRevision(object.revision){}
        Root* getRootEdit()
        {
            if(!rootKnown || rootRevision!=scene.nextRevision)
            {
                rootKnown=true;rootRevision=scene.nextRevision;root.object=&source;
                for(unsigned depth=0;root.object && root.object->parent && depth<256;++depth)
                {
                    const auto parent=scene.objects.find(root.object->parent);
                    if(parent==scene.objects.end()){root.object=nullptr;break;}
                    if(parent->second.pcode==LL_PCODE_LEGACY_AVATAR)break;
                    root.object=&parent->second;
                }
            }
            return root.object?&root:nullptr;
        }
        VSWorldAvatar* getAvatar()
        {
            if(!getRootEdit() || !root.object->parent)return nullptr;
            const auto parent=scene.objects.find(root.object->parent);
            if(parent==scene.objects.end() || parent->second.pcode!=LL_PCODE_LEGACY_AVATAR)return nullptr;
            const auto found=scene.avatars.find(parent->second.id);
            return found==scene.avatars.end()?nullptr:found->second.get();
        }
        bool isHUDAttachment()
        {
            const auto avatar=getAvatar();if(!avatar)return false;
            const auto state=root.object->attachment;
            return avatar->hudAttachment(((state&15)<<4)|(state>>4));
        }
        struct Service
        {
            using Image=ResolvedTexture;
            const TextureSource& source;
            bool requestImages=true,includePixels=true;
            Image fetch(const LLUUID& id)const
            {
                Image value;if(id.isNull() || !source.live())return value;
                value.present=true;value.asset=id;if(requestImages)vs_request_world_image(id,64);
                if(!source.live())return {};
                value.image=vs_ui_image_snapshot(id);if(includePixels)value.raw=vs_ui_image_raw(id);value.missing=vs_ui_image_failed(id);
                value.generation=source.scene.resources.generation("remote:"+vs_native_image_id(id).asString());return value;
            }
            Image bake(VSWorldAvatar* avatar,LLAvatarAppearanceDefines::EBakedTextureIndex index)const
            {
                const auto region=vs_region_context(source.scene.host);if(!region || !region->current())return {};
                const auto baked=avatar->bakedTexture(unsigned(index),region->getRegionMaxBakes(),includePixels);
                if(!source.live())return {};
                if(baked.present && baked.asset.notNull())return fetch(baked.asset);
                return {baked.present,baked.missing,baked.asset,baked.image,baked.raw,baked.generation};
            }
            bool missing(const Image& image)const{return image.missing;}
        };
        ResolvedTexture resolve(const LLUUID& id,bool magic=true,bool requestImages=true,bool includePixels=true)
        {
            if(!live())return {};
            const Service service{*this,requestImages,includePixels};
            if(magic && LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(id))
                return vs_baked_texture_for_magic_id(*this,id,service);
            return service.fetch(id);
        }
    };

    bool groundAt(const LLVector3& point,LLVector3& position,LLVector3& normal) const
    {
        if (!finiteVector(point))
            return false;
        if (point.mV[0] < 0 || point.mV[1] < 0 || point.mV[0] >= width || point.mV[1] >= regionHeight)
        {
            const LLVector3d global = from_region_handle(handle) + LLVector3d(point);
            for (const auto& [regionHost, weak] : budgets->regions)
                if (const auto region = weak.lock(); region && region.get() != this && !region->retired)
                {
                    const LLVector3 local(global - from_region_handle(region->handle));
                    if (local.mV[0] >= 0 && local.mV[1] >= 0 && local.mV[0] < region->width && local.mV[1] < region->regionHeight &&
                        region->groundAt(local, position, normal))
                    {
                        position = LLVector3(from_region_handle(region->handle) + LLVector3d(position) - from_region_handle(handle));
                        return true;
                    }
                }
            return false;
        }
        const unsigned x=unsigned(point.mV[0]),y=unsigned(point.mV[1]);const float z=height(x,y,std::numeric_limits<float>::quiet_NaN());
        bool found     = std::isfinite(z);
        position       = point;
        position.mV[2] = found ? z : point.mV[2] - 1;
        normal         = found ? LLVector3(height(x ? x - 1 : x, y, z) - height(std::min(x + 1, width), y, z),
                                           height(x, y ? y - 1 : y, z) - height(x, std::min(y + 1, regionHeight), z), 2)
                               : LLVector3(0, 0, 1);
        normal.normalize();
        // Motion constraints follow the exact admitted terrain triangles,
        // including the current LOD and edge strips rather than integer texels.
        for (const auto& [key, patch] : ground)
        {
            if (!patch.mesh || point.mV[0] < U32(key >> 32) || point.mV[0] > U32(key >> 32) + patch.size || point.mV[1] < U32(key) ||
                point.mV[1] > U32(key) + patch.size)
                continue;
            const auto                 bounds = VSWorldRenderer::bounds(patch.mesh);
            const std::array<float, 3> top{ point.mV[0], point.mV[1], bounds[1][2] + 1 },
                bottom{ point.mV[0], point.mV[1], bounds[0][2] - 1 };
            VSWorldRenderer::SurfaceHit hit;
            if (VSWorldRenderer::intersect(patch.mesh, top, bottom, hit))
            {
                position = LLVector3(hit.point.data());
                normal   = LLVector3(hit.normal.data());
                found    = true;
                break;
            }
        }
        const std::array<float, 3> start{ point.mV[0], point.mV[1], point.mV[2] + 1 }, end{ point.mV[0], point.mV[1], point.mV[2] - 1 };
        for (const auto& [local, object] : objects)
        {
            if (object.pcode != LL_PCODE_VOLUME || object.attachment || !linksCurrent(object))
                continue;
            bool attached = false;
            U32  parent   = object.parent;
            for (unsigned depth = 0; parent && depth < 256; ++depth)
            {
                const auto owner = objects.find(parent);
                if (owner == objects.end())
                    break;
                if (owner->second.attachment || owner->second.pcode == LL_PCODE_LEGACY_AVATAR)
                {
                    attached = true;
                    break;
                }
                parent = owner->second.parent;
            }
            if (attached)
                continue;
            const auto& faces = object.admittedFaces.empty() ? object.faces : object.admittedFaces;
            for (const auto& face : faces)
            {
                if (!face.mesh)
                    continue;
                const auto bounds = VSWorldRenderer::bounds(face.mesh);
                if (point.mV[0] < bounds[0][0] || point.mV[0] > bounds[1][0] || point.mV[1] < bounds[0][1] || point.mV[1] > bounds[1][1])
                    continue;
                VSWorldRenderer::SurfaceHit hit;
                if (VSWorldRenderer::intersect(face.mesh, start, end, hit) && (!found || hit.point[2] > position.mV[2]))
                {
                    position = LLVector3(hit.point.data());
                    normal   = LLVector3(hit.normal.data());
                    found    = true;
                }
            }
        }
        return found;
    }
    VSWorldAvatar* avatar(const LLUUID& id,U64 handle)
    {
        const auto found=avatars.find(id);if (found!=avatars.end()) return found->second.get();
        if (id.isNull() || avatars.size()>=256) return nullptr;
        auto reservation=budgets->avatars.reserve(1);if (!reservation) return nullptr;
        VSWorldAvatar::initializeClass();
        auto owner=std::make_unique<VSWorldAvatar>(id,id==agent,handle,renderer,resources,
            [this](const LLVector3& p,LLVector3& out,LLVector3& n){return groundAt(p,out,n);},std::move(reservation));
        owner->nameTagBudget(budgets->terrainBytes);
        auto* result=owner.get();avatars[id]=std::move(owner);return result;
    }
    double nextRequest=0;
    struct Ground
    {
        unsigned                                             size = 0;
        VSWorldRenderer::Mesh                                mesh;
        bool                                                 dirty = true;
        std::array<float, 1024>                              heights{};
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        unsigned                                             stride = 1, north = 1, east = 1;
    };
    std::map<U64,Ground> ground;
    struct GroundPublication
    {
        VSWorldRenderer::Mesh mesh;
        unsigned              stride = 1, north = 1, east = 1;
    };
    std::shared_ptr<const VSResourceBudget::Reservation> groundCandidateResidency;
    std::optional<std::map<U64, GroundPublication>>      groundCandidate;
    U64 terrainRevision=0;
    unsigned patchSize=0;
    std::array<LLUUID,4> terrainTextures;
    std::array<float,4> terrainStarts{},terrainRanges{};
    VSWorldRenderer::Mesh water,sky;
    unsigned skyDetail=0;float skyRadius=0;
    std::array<float, 256>                               windX{}, windY{};
    bool                                                 windKnown = false;
    float waterHeight=20;bool waterKnown=false;
    bool                      wind(const std::vector<U8>& bytes, U8 type)
    {
        std::vector<VSTerrain::Patch> patches;
        if (!VSTerrain::decode(bytes, type, patches, width, regionHeight) || patches.size() != 2)
            return false;
        std::array<std::array<float, 256>, 2> decoded;
        for (unsigned component = 0; component < 2; ++component)
        {
            auto&         p = patches[component];
            LLGroupHeader group;
            group.stride     = 16;
            group.patch_size = 16;
            group.layer_type = type;
            LLPatchHeader header;
            header.dc_offset   = p.offset;
            header.range       = U16(p.range);
            header.quant_wbits = U8(p.quant);
            header.patchids    = 0;
            init_patch_decompressor(16);
            set_group_of_patch_header(&group);
            decompress_patch(decoded[component].data(), p.coefficients.data(), &header);
            for (float value : decoded[component])
                if (!std::isfinite(value) || std::abs(value) > 65536)
                    return false;
        }
        windX     = decoded[0];
        windY     = decoded[1];
        windKnown = true;
        return true;
    }
    LLVector3 windAt(const LLVector3& position) const
    { return vs_wind_velocity(windX, windY, position, float(width), float(regionHeight)); }
    float heightLocal(S32 x, S32 y, float fallback) const
    {
        if (!patchSize || x < 0 || y < 0 || x >= S32(width) || y >= S32(regionHeight))
            return fallback;
        const unsigned x0 = unsigned(x) / patchSize * patchSize, y0 = unsigned(y) / patchSize * patchSize;
        const auto patch=ground.find((U64(x0)<<32)|y0);
        return patch == ground.end() ? fallback : patch->second.heights[(unsigned(y) - y0) * patchSize + unsigned(x) - x0];
    }
    float height(S32 x, S32 y, float fallback) const
    {
        if (x >= 0 && y >= 0 && x < S32(width) && y < S32(regionHeight))
            return heightLocal(x, y, fallback);
        const LLVector3d global = from_region_handle(handle) + LLVector3d(x, y, 0);
        for (const auto& [host, weak] : budgets->regions)
        {
            const auto region = weak.lock();
            if (!region || region.get() == this || region->retired)
                continue;
            const auto point = global - from_region_handle(region->handle);
            if (point.mdV[0] >= 0 && point.mdV[1] >= 0 && point.mdV[0] < region->width && point.mdV[1] < region->regionHeight)
                return region->heightLocal(S32(point.mdV[0]), S32(point.mdV[1]), fallback);
        }
        // The outermost supported region edge uses the reference clamped
        // sample until an adjacent simulator heightfield is known.
        return heightLocal(llclamp(x, 0, S32(width) - 1), llclamp(y, 0, S32(regionHeight) - 1), fallback);
    }
    LLVector3 terrainNormal(S32 x, S32 y, F32 fallback) const
    {
        const LLVector3 p00(-1, -1, height(x - 1, y - 1, fallback)), p01(-1, 1, height(x - 1, y + 1, fallback));
        const LLVector3 p10(1, -1, height(x + 1, y - 1, fallback)), p11(1, 1, height(x + 1, y + 1, fallback));
        LLVector3       normal = (p11 - p00) % (p01 - p10);
        normal.normalize();
        return normal;
    }
    bool terrain(const std::vector<U8>& data,U8 type)
    {
        std::vector<VSTerrain::Patch> patches;
        if (!VSTerrain::decode(data,type,patches,width,regionHeight)) return false;
        // Reuse the viewer's established CPU inverse DCT, after bounded decoding.
        // No legacy surface, drawable, texture or OpenGL owner is constructed.
        struct Decoded {unsigned x,y,size;std::array<float,1024> h;};
        std::vector<Decoded> decoded;
        for (auto& p:patches)
        {
            if (patchSize && p.size!=patchSize) return false;
            LLGroupHeader group;group.stride=p.size;group.patch_size=U8(p.size);group.layer_type=type;
            LLPatchHeader header;header.dc_offset=p.offset;header.range=U16(p.range);header.quant_wbits=U8(p.quant);header.patchids=0;
            init_patch_decompressor(p.size);set_group_of_patch_header(&group);
            Decoded d{p.x*p.size,p.y*p.size,p.size,{}};decompress_patch(d.h.data(),p.coefficients.data(),&header);
            for (unsigned i=0;i<p.size*p.size;++i) if (!std::isfinite(d.h[i]) || std::abs(d.h[i])>65536) return false;
            decoded.push_back(std::move(d));
        }
        std::set<U64> additional;
        for (const auto& d:decoded) if (!ground.count((U64(d.x)<<32)|d.y)) additional.insert((U64(d.x)<<32)|d.y);
        if (ground.size()+additional.size()>4096) return false;
        std::map<U64,std::shared_ptr<const VSResourceBudget::Reservation>> reservations;
        for (const auto key:additional)
        { auto reservation=budgets->terrainBytes.reserve(sizeof(Ground));if (!reservation) return false;reservations.emplace(key,std::move(reservation)); }
        for (const auto& d:decoded)
        {
            const U64 key=(U64(d.x)<<32)|d.y;
            auto reservation=ground.count(key)?ground.at(key).residency:reservations.at(key);
            // Keep the last admitted GPU patch while new heights await upload.
            auto previousMesh = ground.count(key) ? std::move(ground.at(key).mesh) : VSWorldRenderer::Mesh{};
            patchSize=d.size;ground[key]=Ground{d.size,std::move(previousMesh),true,d.h,std::move(reservation)};
        }
        // Neighbor samples affect edge stitching and normals. Rebuild only the
        // bounded set of received patches on the next rendering frame.
        for (auto& [key,g]:ground) g.dirty=true;
        for (auto& [local, object] : objects)
            if (object.pcode == LL_PCODE_LEGACY_GRASS)
                object.dirty = true;
        // An adjacent region's edge vertices sample these heights as well.
        // Weak registry entries never prolong a retired simulator's lifetime.
        for (const auto& [regionHost, weak] : budgets->regions)
            if (const auto region = weak.lock(); region && region.get() != this && !region->retired)
            {
                for (auto& [key, patch] : region->ground)
                    patch.dirty = true;
                for (auto& [local, object] : region->objects)
                    if (object.pcode == LL_PCODE_LEGACY_GRASS)
                        object.dirty = true;
            }
        if (!decoded.empty()) ++terrainRevision;
        return true;
    }
    void dirtyTerrainNeighbours()
    {
        for (const auto& [regionHost, weak] : budgets->regions)
            if (const auto region = weak.lock(); region && region.get() != this && !region->retired)
            {
                for (auto& [key, patch] : region->ground)
                    patch.dirty = true;
                for (auto& [local, object] : region->objects)
                    if (object.pcode == LL_PCODE_LEGACY_GRASS)
                        object.dirty = true;
            }
    }
    bool prepareGround(const LLVector3& eye)
    {
        groundCandidate.reset();
        groundCandidateResidency.reset();
        if (ground.empty())
            return true;
        auto lodWorking = budgets->terrainBytes.reserve(ground.size() * 64);
        if (!lodWorking)
            return false;
        // Match the reference distance/radius policy, then balance adjacent
        // patch strides so its half-step edge strips always join exactly.
        std::map<U64, unsigned> desired;
        const F32               factor = std::max(gSavedSettings.getF32("RenderTerrainLODFactor"), .1f);
        for (const auto& [key, patch] : ground)
        {
            // Reference patch bounds include the shared north/east samples.
            F32 minimum = patch.heights[0], maximum = minimum;
            for (unsigned y = 0; y <= patch.size; ++y)
                for (unsigned x = 0; x <= patch.size; ++x)
                {
                    const F32 fallback = patch.heights[std::min(y, patch.size - 1) * patch.size + std::min(x, patch.size - 1)];
                    const F32 value    = height(S32(U32(key >> 32) + x), S32(U32(key) + y), fallback);
                    minimum            = std::min(minimum, value);
                    maximum            = std::max(maximum, value);
                }
            const LLVector3 center(float(U32(key >> 32)) + patch.size * .5f, float(U32(key)) + patch.size * .5f, (minimum + maximum) * .5f);
            const F32       radius = LLVector3(F32(patch.size), F32(patch.size), maximum - minimum).length() * .5f;
            const unsigned  limit =
                gSavedSettings.getBOOL("RenderDynamicLOD")
                    ? unsigned(llclamp(lltrunc(std::max((center - eye).length() - radius, 0.f) / factor * .15f), 1, S32(patch.size)))
                    : 1;
            unsigned stride = 1;
            while (stride * 2 <= limit)
                stride *= 2;
            // Boundary samples are shared with another region, including a
            // simulator whose visibility/height data has not arrived yet.
            if (!U32(key >> 32) || !U32(key) || U32(key >> 32) + patch.size == width || U32(key) + patch.size == regionHeight)
                stride = 1;
            desired[key] = stride;
        }
        for (unsigned pass = 0; pass < 6; ++pass)
        {
            bool changed = false;
            for (auto& [key, stride] : desired)
            {
                const S32 x = S32(key >> 32), y = S32(U32(key)), size = S32(ground.at(key).size);
                for (const auto& offset : { std::pair<S32, S32>{ size, 0 }, { -size, 0 }, { 0, size }, { 0, -size } })
                {
                    if (x + offset.first < 0 || y + offset.second < 0)
                        continue;
                    const auto neighbour = desired.find((U64(U32(x + offset.first)) << 32) | U32(y + offset.second));
                    if (neighbour != desired.end() && stride > neighbour->second * 2)
                    {
                        stride  = neighbour->second * 2;
                        changed = true;
                    }
                }
            }
            if (!changed)
                break;
        }
        bool changed = false;
        for (const auto& [key, patch] : ground)
        {
            const unsigned stride = desired.at(key), x = U32(key >> 32), y = U32(key);
            auto           neighbour = [&](U32 nx, U32 ny)
            {
                const auto found = desired.find((U64(nx) << 32) | ny);
                return found == desired.end() ? stride : found->second;
            };
            changed |= patch.dirty || patch.stride != stride || patch.north != neighbour(x, y + patch.size) ||
                       patch.east != neighbour(x + patch.size, y);
        }
        if (!changed)
            return true;
        groundCandidateResidency = budgets->terrainBytes.reserve(ground.size() * (sizeof(GroundPublication) + 64));
        if (!groundCandidateResidency)
            return false;
        groundCandidate.emplace();
        for (const auto& [key, g] : ground)
            groundCandidate->emplace(key, GroundPublication{ g.mesh, g.stride, g.north, g.east });
        for (auto& [key, publication] : *groundCandidate)
        {
            const auto&    g = ground.at(key);
            const unsigned x0=unsigned(key>>32),y0=U32(key),n=g.size;
            const unsigned stride    = desired.at(key);
            auto           neighbour = [&](unsigned x, unsigned y)
            {
                const auto value = desired.find((U64(x) << 32) | y);
                return value == desired.end() ? stride : value->second;
            };
            const unsigned north = neighbour(x0, y0 + n), east = neighbour(x0 + n, y0);
            if (g.dirty || g.stride != stride || g.north != north || g.east != east)
            {
                auto geometryWorking = budgets->terrainBytes.reserve(256 * 1024);
                if (!geometryWorking)
                    return false;
                const auto origin = from_region_handle(handle);
                VSTerrainGeometry producer(n, stride, north, east,
                                           [&](U32 x, U32 y, U32, LLVector3* point, LLVector3* normal, LLVector2* uv, LLVector2* detail)
                                           {
                                               const unsigned gx = x0 + x, gy = y0 + y;
                                               const F32      fallback = g.heights[std::min(y, n - 1) * n + std::min(x, n - 1)];
                                               const F32      z        = height(S32(gx), S32(gy), fallback);
                                               *point                  = LLVector3(F32(gx), F32(gy), z);
                                               *normal                 = terrainNormal(S32(gx), S32(gy), z);
                                               *uv = LLVector2(F32(gx),1-F32(gy));
                                               const auto blend=VSTerrainMaterial::coordinates(origin.mdV[0],origin.mdV[1],F32(gx),F32(gy),z,F32(width),terrainStarts,terrainRanges);
                                               *detail=LLVector2(blend[0],blend[1]);
                                           });
                const auto        geometry = producer.mesh();
                std::vector<VSWorldRenderer::Vertex> vertices;
                vertices.reserve(geometry.vertices.size());
                for (std::size_t index = 0; index < geometry.vertices.size(); ++index)
                {
                    const auto& point  = geometry.vertices[index];
                    const auto& normal = geometry.normals[index];
                    const auto& uv     = geometry.uv[index];
                    vertices.push_back(
                        { point.mV[0], point.mV[1], point.mV[2], normal.mV[0], normal.mV[1], normal.mV[2],uv.mV[0],uv.mV[1],geometry.detail[index].mV[0],geometry.detail[index].mV[1] });
                }
                std::vector<U32> indices(geometry.indices.begin(), geometry.indices.end());
                if (!renderer.publishMesh(publication.mesh, vertices, indices,VSWorldRenderer::Topology::Triangles,
                                          VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0|VSWorldContracts::UV1))
                    return false;
                publication.stride = stride;
                publication.north  = north;
                publication.east   = east;
            }
        }
        return true;
    }
    VSUIRenderer::Image terrainRamp()
    {
        const std::string key="terrain:alpha-ramp";
        if(auto image=resources.image(key)) return image;
        const auto path=gDirUtilp->findSkinnedFilename(LLDir::TEXTURES,"alpha_gradient_2d.j2c");
        LLPointer<LLImageFormatted> encoded=LLImageFormatted::createFromExtension("alpha_gradient_2d.j2c");
        LLPointer<LLImageRaw> source=new LLImageRaw;
        if(path.empty() || !encoded || !encoded->load(path) || !encoded->decode(source,0)) return {};
        LLPointer<LLImageRaw> raw=new LLImageRaw(source->getWidth(),source->getHeight(),4);
        const unsigned components=source->getComponents();
        for(unsigned i=0;i<unsigned(source->getWidth()*source->getHeight());++i)
        {
            auto* pixel=raw->getData()+i*4;
            pixel[0]=pixel[1]=pixel[2]=255;
            pixel[3]=source->getData()[i*components+(components==4?3:0)];
        }
        resources.publish(key,*raw);
        return resources.image(key);
    }
    std::shared_ptr<const VSWorldRenderer::Terrain> terrainMaterial(unsigned demand)
    {
        if(!host.isOk() || !VSNativeSession::active()) return {};
        if(!terrainAssets) terrainAssets=std::make_unique<VSTerrainAssets>(host);
        return terrainAssets->snapshot(terrainTextures,demand,width,resources,terrainRamp());
    }
    void surfaces(std::vector<VSWorldRenderer::Packet>& packets,const LLVector3& eye,unsigned viewportHeight=768)
    {
        const auto activeSky=LLEnvironment::instanceExists()?LLEnvironment::instance().getCurrentSky():nullptr;
        const unsigned detail=activeSky?llclamp(gSavedSettings.getU32("WLSkyDetail"),8u,180u):0;
        const float radius=activeSky?activeSky->getDomeRadius():0;
        // Startup's clip-space quad and the reference sky dome have different
        // geometry domains. Never shade the quad as a dome on first refusal.
        if(activeSky && !skyDetail)sky.reset();
        if(activeSky && (!sky || skyDetail!=detail || skyRadius!=radius)) {
            const U32 slices=detail*2,stacks=detail;
            const std::size_t vertexCount=std::size_t(stacks+1)*slices;
            const std::size_t stripCount=1+std::size_t(stacks)*(2+2*slices);
            const std::size_t bytes=vertexCount*(sizeof(LLVector3)+sizeof(LLVector2)+sizeof(VSWorldRenderer::Vertex))+
                (stripCount+(stripCount-2)*3)*sizeof(unsigned);
            // Charge all CPU staging vectors before allocation. GPU/retained
            // mesh data is charged by publishMesh; refusal keeps the old mesh.
            const auto skyWorking=budgets->terrainBytes.reserve(bytes);
            if(skyWorking) {
            std::vector<LLVector3> positions(vertexCount);
            std::vector<LLVector2> coordinates(positions.size());
            std::vector<unsigned> strips(1+stacks*(2+2*slices));
            auto* position=positions.data();auto* coordinate=coordinates.data();auto* index=strips.data();
            vs_sky_strips(0,stacks,position,coordinate,index,radius,slices,stacks);
            std::vector<VSWorldRenderer::Vertex> vertices;vertices.reserve(positions.size());
            for(unsigned i=0;i<positions.size();++i)vertices.push_back({positions[i].mV[0],positions[i].mV[1],positions[i].mV[2],
                0,1,0,coordinates[i].mV[0],coordinates[i].mV[1]});
            std::vector<unsigned> triangles;triangles.reserve((strips.size()-2)*3);
            for(unsigned i=2;i<strips.size();++i) {
                unsigned a=strips[i-2],b=strips[i-1],c=strips[i];if(i&1)std::swap(a,b);
                if(a!=b && b!=c && a!=c)triangles.insert(triangles.end(),{a,b,c});
            }
            if(renderer.publishMesh(sky,vertices,triangles)){skyDetail=detail;skyRadius=radius;}
            }
        }
        if(!activeSky && skyDetail){sky.reset();skyDetail=0;skyRadius=0;}
        if (!sky && !activeSky)
            sky = renderer.upload(
                { { -1, -1, 0, 0, 0, 1, 0, 0 }, { 1, -1, 0, 0, 0, 1, 1, 0 }, { 1, 1, 0, 0, 0, 1, 1, 1 }, { -1, 1, 0, 0, 0, 1, 0, 1 } },
                { 0, 1, 2, 0, 2, 3 });
        if (sky && gPipeline.hasRenderType(activeSky?LLPipeline::RENDER_TYPE_WL_SKY:LLPipeline::RENDER_TYPE_SKY))
        {
            VSWorldRenderer::Packet background;
            background.mesh       = sky;
            background.background = true;
            background.fullbright = true;
            if(activeSky) {
                auto opticalImage=[&](const LLUUID& id) {
                    if(id.isNull())return VSUIRenderer::Image{};
                    vs_request_world_image(id,512);
                    return resources.image("remote:"+vs_native_image_id(id).asString());
                };
                background.image=opticalImage(activeSky->getRainbowTextureId());
                background.channels[0]=opticalImage(activeSky->getHaloTextureId());
            }
            packets.push_back(background);
        }
        auto surface=std::make_shared<VSMaterial::State>();
        const F32 scale=1.f/std::max(gSavedSettings.getF32("RenderTerrainScale"),.001f);
        const auto origin=from_region_handle(handle);
        surface->uv[0].matrix={scale,0,0,scale};
        surface->uv[0].offset={F32(std::fmod(origin.mdV[0],1.0/scale)*scale),F32(std::fmod(origin.mdV[1],1.0/scale)*scale)};
        surface->samplers[VSMaterial::Occlusion].u=surface->samplers[VSMaterial::Occlusion].v=VSWorldContracts::AddressMode::Clamp;
        for(unsigned channel=0;channel<4;++channel) {surface->samplers[channel].mipmaps=true;surface->samplers[channel].anisotropy=gSavedSettings.getBOOL("RenderAnisotropic")?16u:1u;}
        unsigned demand=1;
        bool received=false;
        for(const auto& [key,g]:ground) if(g.mesh)
        {
            received=true;
            const unsigned x=unsigned(key>>32),y=U32(key),n=g.size;
            const LLVector3 center(F32(x)+F32(n)*.5f,F32(y)+F32(n)*.5f,height(x+n/2,y+n/2,0));
            demand=std::max(demand,unsigned(std::clamp(F32(n)*std::max(viewportHeight,1u)/std::max((center-eye).length(),1.f),1.f,4096.f)));
        }
        const auto fallback=received?resources.defaultWorldImage():VSUIRenderer::Image{};
        std::array<VSUIRenderer::Image,4> layers;
        if(received) for(unsigned layer=0;layer<4;++layer)
        {
            const auto& texture=terrainTextures[layer];
            if(texture.notNull()) vs_request_world_image(texture,demand);
            layers[layer]=texture.notNull()?resources.image("remote:"+vs_native_image_id(texture).asString()):VSUIRenderer::Image{};
            if(!layers[layer]) layers[layer]=fallback;
        }
        const auto ramp=received?terrainRamp():VSUIRenderer::Image{};
        const auto pbr=received?terrainMaterial(demand):nullptr;
        for (auto& [key, g] : ground)
        {
            if (!g.mesh || !gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_TERRAIN)) continue;
            VSWorldRenderer::Packet p;
            p.mesh=g.mesh;p.terrain=true;p.terrainKey=key;p.objectGeneration=incarnation;
            p.image=layers[0];p.channels={layers[1],layers[2],layers[3],ramp};
            p.terrainMaterial=pbr;
            if(!p.image && !fallback) p.color={127.f/255,127.f/255,127.f/255,1};
            p.surface=surface;
            packets.push_back(std::move(p));
        }
        if (waterKnown && vs_region_limits().getAllowRenderWater() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_WATER))
        {
            if (!water) water=renderer.upload({{0,0,waterHeight,0,0,1,0,0},{float(width),0,waterHeight,0,0,1,1,0},
                {float(width),float(regionHeight),waterHeight,0,0,1,1,1},{0,float(regionHeight),waterHeight,0,0,1,0,1}},{0,1,2,0,2,3});
            if (water)
            {
                VSWorldRenderer::Packet p;p.mesh=water;p.color={.08f,.28f,.38f,.65f};p.alpha=true;
                p.distanceSquared=(LLVector3(width*.5f,regionHeight*.5f,waterHeight)-eye).lengthSquared();packets.push_back(std::move(p));
            }
        }
    }
    U64 accepted=0,rejected=0,unsupported=0;
    Impl(VSWorldRenderer& r, VSUIResources& u, std::shared_ptr<VSWorldAssets> cache, std::shared_ptr<Budgets> limits) :
        renderer(r),
        resources(u),
        assets(std::move(cache)),
        budgets(std::move(limits))
    {
        windX.fill(.5f);
        windY.fill(.5f);
    }
    bool highlightedOrBeacon(const Object& object) const
    {
        if(!LLPipeline::getRenderBeacons() && !LLPipeline::getRenderHighlights())return false;
        const bool avatar=object.pcode==LL_PCODE_LEGACY_AVATAR;
        const bool scripted=!avatar && !object.parent && (object.updateFlags&FLAGS_SCRIPTED);
        const bool physical=!avatar && (object.updateFlags&FLAGS_USE_PHYSICS);
        return (object.particles && LLPipeline::getRenderParticleBeacons()) ||
            (object.soundSource && LLPipeline::getRenderSoundBeacons()) ||
            (!object.mediaVersion.empty() && LLPipeline::getRenderMOAPBeacons()) ||
            (scripted && LLPipeline::getRenderScriptedBeacons()) ||
            (scripted && (object.updateFlags&FLAGS_HANDLE_TOUCH) && LLPipeline::getRenderScriptedTouchBeacons()) ||
            (physical && LLPipeline::getRenderPhysicalBeacons());
    }
    void request(U32 id)
    {
        if(!id || pending.count(id) || pending.size()>=1024)return;
        auto slot=budgets->objectRequests.reserve(1);if(!slot)return;
        Request request;request.slot=std::move(slot);pending.emplace(id,std::move(request));
    }
    bool applyMaterialOverrides(Object& object,const LLSD& data)
    {
        const auto& sides=data["te"]; const auto& overrides=data["od"];
        if(!sides.isArray() || !overrides.isArray() || sides.size()!=overrides.size() || sides.size()>45) return false;
        std::vector<LLPointer<LLGLTFMaterial>> candidate(object.faces.size());
        std::set<S32> seen;
        for(S32 i=0;i<sides.size();++i)
        {
            if(!sides[i].isInteger() || sides[i].asInteger()<0 || U32(sides[i].asInteger())>=object.faces.size() ||
                !seen.insert(sides[i].asInteger()).second || !overrides[i].isMap()) return false;
            LLPointer<LLGLTFMaterial> value=new LLGLTFMaterial();value->applyOverrideLLSD(overrides[i]);
            candidate[std::size_t(sides[i].asInteger())]=value;
        }
        for (std::size_t i = 0; i < object.faces.size(); ++i)
        {
            const auto id = object.renderMaterials.getMaterial(U8(i));
            object.faces[i].material.setGLTFMaterial(id.notNull() ? gGLTFMaterialList.getMaterial(id) : nullptr);
            object.faces[i].material.setGLTFMaterialOverride(id.notNull() ? candidate[i] : nullptr);
        }
        object.revision=nextRevision++;
        return true;
    }
    void eraseObject(U32 id)
    {
        if(selectionIndex && selectionIndex->valid())selectionIndex->erase(id);
        for (auto& [local,object]:objects) if (object.parent==id) object.parentRetired=true;
        dirtyChildren(id);
        if (const auto object=objects.find(id);object!=objects.end() && object->second.pcode==LL_PCODE_LEGACY_AVATAR)
            avatars.erase(object->second.id);
        if (const auto object = objects.find(id); object != objects.end())
            pendingAnimations.erase(object->second.id);
        if(const auto found=objects.find(id);found!=objects.end())objectIndex.erase(id,found->second);
        if(const auto found=objects.find(id);found!=objects.end() && found->second.icon)found->second.icon->markDead();
        std::optional<ObjectHandle> retired;
        if (const auto found = objects.find(id); found != objects.end())
        {
            retired = ObjectHandle{host, found->second.id, id, incarnation, found->second.generation};
            retired->kind = kind(found->second); retired->account = budgets->account; retired->accountGeneration = budgets->accountGeneration;
        }
        if(particleObjects.erase(id))++budgets->particleRevision;
        objects.erase(id);pending.erase(id);pendingMaterialOverrides.erase(id);
        if (retired) budgets->objectSounds(*retired);
    }
    void eraseAnimated()
    {
        std::set<U32> removed;
        for (const auto& [local, object] : objects)
            if (object.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)
                removed.insert(local);
        for (unsigned depth = 0; depth < 256; ++depth)
        {
            bool changed = false;
            for (const auto& [local, object] : objects)
                if (linksCurrent(object) && removed.count(object.parent) && removed.insert(local).second)
                    changed = true;
            if (!changed)
                break;
        }
        for (const auto local : removed)
            eraseObject(local);
    }
    bool replace(U32 id,Object&& object)
    {
        // UUID identity can survive a local-ID reassignment; the old host/local
        // binding must expire before the new binding becomes observable.
        for(auto old=objects.begin();old!=objects.end();++old)
            if(old->first!=id && old->second.id==object.id)
            {
                const U32 local=old->first;
                auto appearance=avatars.extract(object.id);
                eraseObject(local);
                if(object.pcode==LL_PCODE_LEGACY_AVATAR && !appearance.empty())avatars.insert(std::move(appearance));
                break;
            }
        const auto previous=objects.find(id);
        const bool sameOwner=previous!=objects.end() && previous->second.id==object.id &&
            previous->second.pcode==object.pcode && kind(previous->second)==kind(object);
        if(sameOwner && !object.nameValuesPresent)
        {object.nameValues=previous->second.nameValues;object.inventoryItem=previous->second.inventoryItem;}
        object.metadataResidency=budgets->primitiveBytes.reserve(sizeof(U32)+sizeof(LLUUID)+sizeof(U8)*2+object.nameValues.size()+1+(object.edit?sizeof(VSObjectEditParameters):0)+(object.particles?sizeof(std::set<U32>::value_type)+sizeof(void*)*4:0));
        if(!object.metadataResidency){request(id);return false;}
        object.slot=previous!=objects.end()?previous->second.slot:budgets->objects.reserve(1);
        if (!object.slot) {request(id);return false;}
        if (object.volume && object.params.getSculptID().isNull() &&
            (previous==objects.end() || object.volume.get()!=previous->second.volume.get()))
        {
            size_t bytes=sizeof(LLVolume);
            for (S32 index=0;index<object.volume->getNumVolumeFaces();++index)
            {
                const auto& face=object.volume->getVolumeFace(index);
                bytes+=size_t(face.mNumVertices)*(sizeof(LLVector4a)*4+sizeof(LLVector2))+size_t(face.mNumIndices)*sizeof(U16);
            }
            object.primitiveResidency=budgets->primitiveBytes.reserve(bytes);
            if (!object.primitiveResidency) {request(id);return false;}
        }
        if (gObjectList.isDerendered(object.id) || FSAssetBlacklist::instance().isBlacklisted(object.id,
                object.pcode == LL_PCODE_LEGACY_AVATAR ? LLAssetType::AT_PERSON : LLAssetType::AT_OBJECT))
        {
            if (previous != objects.end()) eraseObject(id);
            return false;
        }
        object.generation=sameOwner?previous->second.generation:nextObjectGeneration++;
        object.revision=nextRevision++;
        if (sameOwner)
        {
            if(object.edit && previous->second.edit)
            {
                object.edit->physics=previous->second.edit->physics;
                object.edit->costs=previous->second.edit->costs;
                if(object.params!=previous->second.params || object.scale!=previous->second.scale)object.edit->costs.stale=true;
            }
            object.media=std::move(previous->second.media);
            if(object.faces.size()==previous->second.faces.size())
                object.legacyPreview=std::move(previous->second.legacyPreview);
            object.avatarTiming=std::move(previous->second.avatarTiming);
            object.avatarTimingAttempt=previous->second.avatarTimingAttempt;
            if(object.attachment==previous->second.attachment && object.parent==previous->second.parent &&
               object.inventoryItem==previous->second.inventoryItem)
            {
                object.attachmentTiming=std::move(previous->second.attachmentTiming);
                object.attachmentTimingAttempt=previous->second.attachmentTimingAttempt;
                object.attachmentGpuMilliseconds=previous->second.attachmentGpuMilliseconds;
            }

            object.nameTagGuard=std::move(previous->second.nameTagGuard);
            if(object.params.getSculptID()==previous->second.params.getSculptID())
            {
                object.nameRiggingLOD=previous->second.nameRiggingLOD;
                object.nameRiggingGeneration=previous->second.nameRiggingGeneration;
                object.nameRigging=std::move(previous->second.nameRigging);
            }

            // Identity guards/revision still advance immediately; the admitted
            // GPU generation survives until its replacement can be published.
            if (object.renderable && previous->second.renderable &&
                object.params.getSculptID() == previous->second.params.getSculptID())
            {
                object.admittedFaces = std::move(previous->second.admittedFaces);
                if (object.admittedFaces.empty())
                    object.admittedFaces = previous->second.faces;
                for (std::size_t face = 0; face < std::min(object.faces.size(), previous->second.faces.size()); ++face)
                {
                    auto& replacement             = object.faces[face];
                    auto& old                     = previous->second.faces[face];
                    replacement.mesh              = std::move(old.mesh);
                    replacement.publishedMaterial = old.publishedMaterial;
                    if(object.renderMaterials.getMaterial(U8(face))==previous->second.renderMaterials.getMaterial(U8(face)))
                    {
                        const auto id = object.renderMaterials.getMaterial(U8(face));
                        replacement.material.setGLTFMaterial(id.notNull() ? gGLTFMaterialList.getMaterial(id) : nullptr);
                        replacement.material.setGLTFMaterialOverride(id.notNull() ? old.material.getGLTFMaterialOverride() : nullptr);
                    }
                    replacement.surface = old.surface;
                    replacement.publishedChannels = old.publishedChannels;
                    replacement.publishedImage    = std::move(old.publishedImage);
                    replacement.published         = old.published;
                    replacement.poseGeneration    = old.poseGeneration;
                }
                if(object.animation && previous->second.animation && object.animation->equals(*previous->second.animation))
                {object.animation=previous->second.animation;object.textureAnimMode=previous->second.textureAnimMode;}
                if (object.params.getSculptType() == previous->second.params.getSculptType())
                {
                    object.gltf        = std::move(previous->second.gltf);
                    object.pendingGLTF = std::move(previous->second.pendingGLTF);
                }
            }
            object.motion=previous->second.motion;
            object.taskSerial=previous->second.taskSerial;object.taskKnown=previous->second.taskKnown;
            object.taskFile=previous->second.taskFile;object.taskRequest=previous->second.taskRequest;object.taskDeadline=previous->second.taskDeadline;
            object.controlAnimations=previous->second.controlAnimations;
            object.controlAnimationResidency = previous->second.controlAnimationResidency;
            object.controlAvatar             = std::move(previous->second.controlAvatar);
            if (object.flexible && previous->second.flexible && object.flexible->operator==(*previous->second.flexible) &&
                object.params == previous->second.params && object.scale == previous->second.scale)
            {
                object.flex = std::move(previous->second.flex);
                if (object.flex)
                    object.flex->pathSignatureSize = 0;
            }
        }
        if(const auto override=pendingMaterialOverrides.find(id);override!=pendingMaterialOverrides.end())
        {applyMaterialOverrides(object,override->second);pendingMaterialOverrides.erase(override);}
        object.motion.accept(object.position,object.rotation,object.velocity,object.acceleration,object.angularVelocity,LLFrameTimer::getTotalSeconds());
        if (!sameOwner && previous!=objects.end())
            for (auto& [local,child]:objects) if (child.parent==id) child.parentRetired=true;
        if (const auto old=objects.find(id);old!=objects.end() && old->second.pcode==LL_PCODE_LEGACY_AVATAR &&
            (old->second.id!=object.id || object.pcode!=LL_PCODE_LEGACY_AVATAR)) avatars.erase(old->second.id);
        if(kind(object)==VSWorldContracts::OwnerKind::ControlAvatar)avatars.erase(object.id);
        if (kind(object) == VSWorldContracts::OwnerKind::ControlAvatar)
            if (const auto animations = pendingAnimations.find(object.id); animations != pendingAnimations.end())
            {
                if (animations->second.deadline >= LLFrameTimer::getTotalSeconds())
                {
                    object.controlAnimations         = std::move(animations->second.animations);
                    object.controlAnimationResidency = std::move(animations->second.residency);
                }
                pendingAnimations.erase(animations);
            }
        // Received emitter parameters remain authoritative; no full-scene
        // scan, duplicated protocol parser, or pose publication is required.
        if(object.particles)
        {
            particleObjects.insert(id);
            object.particleRevision=++budgets->particleRevision;
        }
        else if(particleObjects.erase(id))++budgets->particleRevision;
        objectIndex.replace(id,previous!=objects.end()?&previous->second:nullptr,object);
        objects[id]=std::move(object);pending.erase(id);dirtyChildren(id);
        const auto& current = objects.at(id);
        ObjectHandle token{host, current.id, id, incarnation, current.generation};
        token.kind = kind(current); token.account = budgets->account; token.accountGeneration = budgets->accountGeneration;
        budgets->objectSounds(token);
        return true;
    }
    bool compressed(std::vector<U8>& bytes,U32 updateFlags)
    {
        LLDataPackerBinaryBuffer dp(bytes.data(),S32(bytes.size()));
        Object object;U32 id,ignored,flags;U8 pcode,state;LLVector3 rotation,unused;LLUUID owner;
        if (!dp.unpackUUID(object.id,"ID") || !dp.unpackU32(id,"LocalID") || !dp.unpackU8(pcode,"PCode") ||
            !dp.unpackU8(state,"State")) return false;
        object.pcode=pcode;
        if (!dp.unpackU32(ignored,"CRC") || !dp.unpackU8(object.material,"Material") || !dp.unpackU8(object.clickAction,"ClickAction") ||
            !dp.unpackVector3(object.scale,"Scale") || !dp.unpackVector3(object.position,"Pos") ||
            !dp.unpackVector3(rotation,"Rot") || !dp.unpackU32(flags,"SpecialCode") || !dp.unpackUUID(owner,"Owner")) return false;
        object.rotation.unpackFromVector3(rotation);object.attachment=state;
        object.updateFlags=updateFlags;object.reportedOwner=owner;object.reportedOwnerKnown=true;
        if (flags&0x80) if (!dp.unpackVector3(object.angularVelocity,"Omega") || !finiteVector(object.angularVelocity)) return false;
        if (flags&0x20) if (!dp.unpackU32(object.parent,"ParentID")) return false;
        U8 scratch[4096];S32 length;std::string text;
        // The legacy packer calls strlen before checking the remaining bytes.
        // Validate termination before invoking it on simulator-owned strings.
        auto string=[&](const char* label)
        {
            const S32 remaining=dp.getBufferSize()-dp.getCurrentSize();
            return remaining>0 && std::memchr(dp.getBuffer()+dp.getCurrentSize(),0,std::size_t(remaining)) && dp.unpackString(text,label);
        };
        if (flags & 2)
        {
            if (!dp.unpackU8(object.species, "TreeData"))
                return false;
        }
        else if (flags & 1)
        {
            if (!dp.unpackU32(ignored, "ScratchPadSize") || !dp.unpackBinaryData(scratch, length, "PartData"))
                return false;
            if (length > 0)
                object.species = scratch[0];
        }
        if (pcode == LL_PCODE_LEGACY_GRASS)
            object.species = state;
        if (flags&4) if (!string("Text") || !dp.unpackBinaryDataFixed(scratch,4,"Color")) return false;
        if (flags&0x200)
        {
            if (!string("MediaURL")) return false;
            object.mediaVersion=text;
        }
        if (flags & 8)
        {
            if (!dp.unpackBinaryDataFixed(scratch, 86, "LegacyParticles"))
                return false;
            LLDataPackerBinaryBuffer particleData(scratch, 86);
            LLPartSysData            parameters;
            if (!parameters.unpackLegacy(particleData))
                return false;
            if (parameters.mCRC)
                object.particles = parameters;
        }
        U8 parameters;
        if (!dp.unpackU8(parameters,"num_params")) return false;
        bool supported=true;LLUUID mesh;U8 sculptType=LL_SCULPT_TYPE_NONE;
        object.edit=std::make_shared<VSObjectEditParameters>();
        for (unsigned n=0;n<parameters;++n)
        {
            U16 type;
            if (!dp.unpackU16(type,"param_type") || !dp.unpackBinaryData(scratch,length,"param_data")) return false;
            if(length<0 || !object.edit->unpack(type,scratch,U32(length)))return false;
            if (type == 0x10)
            {
                LLFlexibleObjectData attributes;
                if (!flexibleData(scratch, U32(length), attributes))
                    return false;
                object.flexible = attributes;
            }
            if (type==0x30 || type==LLNetworkData::PARAMS_MESH)
            {
                if (length!=17) return false;
                std::memcpy(mesh.mData,scratch,UUID_BYTES);sculptType=scratch[16];
                if (mesh.isNull())
                    supported = false;
            }
            if(type==LLNetworkData::PARAMS_RENDER_MATERIAL && !renderMaterialData(scratch,U32(length),object.renderMaterials)) return false;
            if (type==LLNetworkData::PARAMS_EXTENDED_MESH)
            { if (length!=4) return false;object.extendedMeshFlags=littleU32(scratch); }
            if(type==LLNetworkData::PARAMS_REFLECTION_PROBE)
            {if(length!=9)return false;object.reflectionProbe=true;}
            if (type == LLNetworkData::PARAMS_LIGHT && length == 16)
                object.light = scratch[3] != 0;
        }
        if(flags&0x10)
        {
            if(!dp.unpackUUID(object.soundAsset,"SoundUUID") || !dp.unpackF32(object.soundGain,"SoundGain") ||
                !dp.unpackU8(object.soundFlags,"SoundFlags") || !dp.unpackF32(object.soundRadius,"SoundRadius"))return false;
            object.soundSource=object.soundAsset.notNull();
        }
        if (flags & 0x100)
        {
            if (!string("NV"))
                return false;
            object.inventoryItem = attachmentItem(text);object.nameValues=text;object.nameValuesPresent=true;
        }
        if (!id || object.id.isNull() || object.parent==id || !finiteVector(object.position) || !finiteVector(object.scale) ||
            !object.rotation.isFinite() || object.scale.mV[0]<=0 || object.scale.mV[1]<=0 || object.scale.mV[2]<=0 ||
            object.scale.lengthSquared()>1.e8f || (objects.size()>=maxObjects && !objects.count(id))) return false;
        if(pcode!=LL_PCODE_VOLUME)
        {
            object.renderable = pcode == LL_PCODE_LEGACY_TREE || pcode == LL_PCODE_TREE_NEW || pcode == LL_PCODE_LEGACY_GRASS;
            if (object.renderable)
            {
                object.faces.resize(1);
                object.attachment = 0;
            }
            else
                ++unsupported;
            if (!replace(id, std::move(object)))
                return false;
            ++accepted;
            return true;
        }
        LLVolumeParams params;if (!LLVolumeMessage::unpackVolumeParams(&params,dp)) return false;
        if (mesh.notNull() && !params.setSculptID(mesh,sculptType)) return false;
        if (object.flexible &&
            (params.getSculptID().notNull() || !params.setType(params.getProfileParams().getCurveType(), LL_PCODE_PATH_FLEXIBLE)))
            return false;
        object.params=params;
        object.renderable=supported;
        if (mesh.isNull() && supported) object.volume=new LLVolume(params,2.f);
        if(!supported)++unsupported;
        const S32 faces=object.volume ? object.volume->getNumVolumeFaces() : 8;
        if (faces<=0 || faces>45) return false;
        LLPrimitive material;material.setNumTEs(U8(faces));
        if (material.unpackTEMessage(dp)==TEM_INVALID) return false;
        for (S32 f=0;f<faces;++f) object.faces.push_back({{},*material.getTE(U8(f))});
        if (flags & 0x40)
        {
            if (!dp.unpackBinaryData(scratch, length, "TextureAnim") || length < 1)
                return false;
            object.animation=textureAnimationData(scratch,U32(length));
            if(!object.animation) return false;
            object.textureAnimation = (object.animation->mMode & LLTextureAnim::ON) != 0;
        }
        if (flags & 0x400)
        {
            if (!dp.unpackBinaryData(scratch, length, "PSBlock") || length <= 0)
                return false;
            if (length < 94 || length > 98 || littleU32(scratch) != 68 || littleU32(scratch + 72) != U32(length - 76))
                return false;
            LLDataPackerBinaryBuffer particleData(scratch, length);
            LLPartSysData            parameters;
            if (!parameters.unpack(particleData))
                return false;
            if (parameters.mCRC)
                object.particles = parameters;
        }
        if (!replace(id,std::move(object))) return false;
        ++accepted;return true;
    }
    bool transform(U32 id,LLVector3& position,LLQuaternion& rotation,std::set<U32>& visited,bool predicted=true)
    {
        if (!visited.insert(id).second || visited.size()>256) return false;
        const auto it=objects.find(id);if (it==objects.end()) {request(id);return false;}
        position=predicted?it->second.motion.position():it->second.position;
        rotation=predicted?it->second.motion.rotation():it->second.rotation;
        if (it->second.parent)
        {
            const auto parentOwner=objects.find(it->second.parent);
            if (it->second.parentRetired || (parentOwner!=objects.end() && it->second.parentGeneration &&
                it->second.parentGeneration!=parentOwner->second.generation)) {request(id);return false;}
            if (parentOwner!=objects.end() && !it->second.parentGeneration) it->second.parentGeneration=parentOwner->second.generation;
            LLVector3 parentPosition;LLQuaternion parentRotation;
            if (!transform(it->second.parent,parentPosition,parentRotation,visited,predicted)) return false;
            if (it->second.attachment)
            {
                const auto parent=objects.find(it->second.parent);
                if (parent!=objects.end() && parent->second.pcode==LL_PCODE_LEGACY_AVATAR)
                {
                    const auto avatar=avatars.find(parent->second.id);const unsigned state=it->second.attachment;
                    if (avatar==avatars.end() || !avatar->second->attachmentTransform(((state&15)<<4)|(state>>4),parentPosition,parentRotation)) return false;
                }
            }
            position=position*parentRotation+parentPosition;rotation=rotation*parentRotation;
        }
        return finiteVector(position);
    }
    static VSWorldContracts::AlphaMode classifyFaceAlpha(const LLTextureEntry& entry,
        const VSWorldContracts::Image* image,VSWorldContracts::AlphaMode alpha,bool explicitAlpha,bool hud,bool rigged)
    {
        if(explicitAlpha || !image || (alpha!=VSWorldContracts::AlphaMode::Blend && image->alpha==VSWorldContracts::AlphaMode::Opaque))return alpha;
        struct FaceAlpha
        {
            const LLTextureEntry& entry;const VSWorldContracts::Image& image;bool isHud,isRigged;
            bool present()const{return true;}
            bool pbr()const{return false;}
            bool noAlpha()const{return LLPipeline::sNoAlpha;}
            bool rigged()const{return isRigged;}
            bool legacyBlend()const{return false;}
            F32 alpha()const{return entry.getColor().mV[3];}
            F32 glow()const{return entry.getGlow();}
            bool hud()const{return isHud;}
            bool imageMask()const{return image.alphaMask;}
            bool fullbright()const{return entry.getFullbright();}
            bool autoMaskNonDeferred()const
            {static LLCachedControl<bool> value(gSavedSettings,"RenderAutoMaskAlphaNonDeferred");return value;}
            bool autoMaskDeferred()const
            {static LLCachedControl<bool> value(gSavedSettings,"RenderAutoMaskAlphaDeferred");return value;}
        };
        return vs_face_can_render_as_mask(FaceAlpha{entry,*image,hud,rigged})?
            VSWorldContracts::AlphaMode::Mask:VSWorldContracts::AlphaMode::Blend;
    }
    struct CostTexture
    {
        ResolvedTexture value;
        mutable std::optional<VSWorldContracts::Image> metadata;
        const VSWorldContracts::Image* description() const
        {
            if(metadata)return &*metadata;
            if(value.image){metadata=VSUIRenderer::imageDescription(value.image);return &*metadata;}
            const auto& raw=value.raw;if(!raw)return nullptr;
            VSWorldContracts::Image result;
            result.width=unsigned(raw->getWidth());result.height=unsigned(raw->getHeight());result.components=unsigned(raw->getComponents());
            result.alpha=VSWorldContracts::AlphaMode::Opaque;
            if(result.components==2 || result.components==4)
            {
                bool transparent=false,partial=false;
                for(std::size_t i=result.components-1;i<std::size_t(result.width)*result.height*result.components;i+=result.components)
                {const auto alpha=raw->getData()[i];transparent|=alpha!=255;partial|=alpha!=0 && alpha!=255;}
                if(transparent)result.alpha=partial?VSWorldContracts::AlphaMode::Blend:VSWorldContracts::AlphaMode::Mask;
                result.alphaMask=vs_image_alpha_mask(raw->getData(),result.width,result.height,result.components,result.components-1);
            }
            metadata=result;return &*metadata;
        }
        // A local layer-set composite has no asset UUID. Its actual retained
        // immutable image/raw owner supplies identity, never a fabricated UUID.
        bool operator<(const CostTexture& other)const
        {
            if(value.asset!=other.value.asset)return value.asset<other.value.asset;
            if(value.asset.notNull())return false;
            const void* left=value.image?static_cast<const void*>(value.image.get()):static_cast<const void*>(value.raw.get());
            const void* right=other.value.image?static_cast<const void*>(other.value.image.get()):static_cast<const void*>(other.value.raw.get());
            return std::less<const void*>{}(left,right);
        }
    };
    using CostTextures=std::set<CostTexture>;
    U32 renderCost(std::shared_ptr<const Impl> state,const Object& object, bool animated, bool root, CostTextures& textures) const
    {
        U32 triangles = 4;
        if (object.asset)
        {
            LLSD        header;
            const char* names[] = { "lowest_lod", "low_lod", "medium_lod", "high_lod" };
            for (unsigned index = 0; index < 4; ++index)
                header[names[index]]["size"] = object.asset->lodBytes[index];
            LLMeshCostData costs;
            costs.init(LLMeshHeader(header));
            const bool rigged = object.asset->skin.isMap();
            triangles         = U32(animated && rigged ? (ANIMATED_OBJECT_COST_PER_KTRI * .001f * costs.getEstTrisForStreamingCost()) / .06f
                                                       : costs.getRadiusWeightedTris(object.scale.length() * .5f));
            if (!triangles)
                triangles = 4;
        }
        U32 alpha = 0, invisible = 0, glow = 0, bump = 0, shiny = 0, planar = 0, media = 0;
        std::optional<TextureSource> source;
        std::optional<bool> alphaRigged,alphaHud;
        const auto textureSource=[&]() -> TextureSource&
        {
            if(!source)
            {
                const auto range=objectIndex.identities(object.id);
                U32 local=0;
                for(auto item=range.first;item!=range.second;++item)
                    if(const auto found=objects.find(item->second);found!=objects.end() && &found->second==&object)
                    {local=item->second;break;}
                const auto id=object.id;const auto generation=object.generation;
                const auto epoch=budgets->accountGeneration;const auto account=budgets->account;
                source.emplace(state,object,[state,local,id,generation,epoch,account]
                {
                    if(!local || state->retired || state->budgets->account!=account || state->budgets->accountGeneration!=epoch)return false;
                    const auto found=state->objects.find(local);
                    return found!=state->objects.end() && found->second.id==id && found->second.generation==generation;
                });
            }
            return *source;
        };
        for (U32 side=0;side<object.faces.size();++side)
        {
            const auto& face=object.faces[side];
            const auto& entry = face.material;
            ResolvedTexture texture;
            if(object.media && entry.hasMedia())
            {
                const auto id=object.media->texture(side);
                if(const auto image=resources.image("media:"+id.asString()))texture={true,false,id,image,{},0};
            }
            if(!texture.present)
            {
                const auto id=entry.getID();
                if(LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(id))
                    texture=textureSource().resolve(id,true,false,false);
                else if(id.notNull())texture={true,false,id,resources.image("remote:"+vs_native_image_id(id).asString()),{},0};
            }
            const auto textureCost=texture.present?textures.insert(CostTexture{texture,{}}).first:textures.end();
            const auto& id=texture.asset;
            auto legacy=entry.getMaterialParams();
            if(legacy.isNull() && materialAssets)
            {
                // Cost is a lazy CPU query: borrow only the real region cache;
                // ordinary rendering/inventory acquisition owns GET requests.
                const auto& values=materialAssets->materials();
                const auto found=values.find(entry.getMaterialID());if(found!=values.end())legacy=found->second;
            }
            auto material=VSMaterial::legacy(entry,legacy.get());bool explicitAlpha=bool(legacy);
            const auto materialID=object.legacyPreview?LLUUID::null:object.renderMaterials.getMaterial(U8(side));
            if(materialID.notNull())
            {
                const auto* base=gGLTFMaterialList.findMaterial(materialID);
                if(base && base->isLoaded()){material=VSMaterial::pbr(*base,entry.getGLTFMaterialOverride());explicitAlpha=true;}
            }
            auto poolAlpha=material.alpha;
            if(!explicitAlpha && textureCost!=textures.end())
                if(const auto* description=textureCost->description())
                {
                    if(!alphaRigged)alphaRigged=(object.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)?bool(object.controlAvatar):
                        (object.asset && object.asset->skin.isMap() && skeletonOwner(object));
                    if(!alphaHud)alphaHud=hudObject(object);
                    poolAlpha=classifyFaceAlpha(entry,description,poolAlpha,false,*alphaHud,*alphaRigged);
                }
            if(poolAlpha==VSWorldContracts::AlphaMode::Blend)alpha=1;
            if (id == IMG_INVISIBLE)
                invisible = 1;
            if (entry.getGlow() > 0)
                glow = 1;
            if (entry.getBumpmap())
                bump = 1;
            if (entry.getShiny())
                shiny = 1;
            if (entry.getTexGen())
                planar = 1;
            if (entry.hasMedia())
                ++media;
        }
        if (object.params.getSculptID().notNull() && !object.params.isMeshSculpt())
            textures.insert(CostTexture{{true,false,object.params.getSculptID(),resources.image("remote:"+vs_native_image_id(object.params.getSculptID()).asString()),{},0},{}});
        return U32(vs_volume_render_cost(triangles, planar, object.textureAnimation, alpha, invisible, glow, bump, shiny,
                                         object.asset && object.asset->skin.isMap(), bool(object.flexible), bool(object.particles),
                                         object.light, media, object.particles ? &*object.particles : nullptr, animated && root));
    }
    F32 textureCost(const CostTexture& texture) const
    {
        const auto& value=texture.value;
        auto extent=value.image?VSUIRenderer::imageExtent(value.image):std::array<unsigned,2>{};
        // Local composites can exist before GPU publication. Borrow their real
        // CPU extent; ordinary unresolved assets retain the reference base cost.
        if(!value.image && value.raw)extent={unsigned(value.raw->getWidth()),unsigned(value.raw->getHeight())};
        return F32(256 + S32(16 * (extent[0] / 128.f + extent[1] / 128.f)));
    }
    std::unique_ptr<VSSpatialIndex> selectionIndex;
    U64 selectionIndexEpoch=0,selectionIndexRevision=0,selectionRefusedRevision=0;
    std::size_t selectionRefusedBytes=0;
    bool selectionIndexAttempted=false;
    VSSpatialIndex::Bounds selectionBounds(const Object& object,const LLVector3& position,const LLQuaternion& rotation)const
    {
        LLMatrix4 matrix;matrix.initScale(object.flexible?LLVector3(1,1,1):object.scale);
        matrix.rotate(rotation);matrix.translate(position);
        VSSpatialIndex::Bounds bounds{};bool known=false;
        for(S32 index=0;index<object.volume->getNumVolumeFaces();++index)
        {
            const auto& face=object.volume->getVolumeFace(index);
            if(!face.mExtents || face.mNumVertices<=0)continue;
            for(unsigned corner=0;corner<8;++corner)
            {
                LLVector3 point;
                for(unsigned axis=0;axis<3;++axis)point.mV[axis]=face.mExtents[(corner>>axis)&1].getF32ptr()[axis];
                point=point*matrix;
                for(unsigned axis=0;axis<3;++axis)
                {
                    if(!known)bounds[0][axis]=bounds[1][axis]=point.mV[axis];
                    else {bounds[0][axis]=std::min(bounds[0][axis],point.mV[axis]);bounds[1][axis]=std::max(bounds[1][axis],point.mV[axis]);}
                }
                known=true;
            }
        }
        if(!known)bounds[0][0]=std::numeric_limits<float>::quiet_NaN();
        return bounds;
    }
    void updateSelectionIndex(U32 local,Object& object,const LLVector3& position,const LLQuaternion& rotation)
    {
        if(!selectionIndex || !selectionIndex->valid())return;
        const bool same=object.spatialEpoch==selectionIndexEpoch && object.spatialRevision==object.revision &&
            object.spatialPosition==position && object.spatialRotation==rotation && object.spatialScale==object.scale &&
            object.spatialVolume==object.volume.get() && object.spatialFlexible==bool(object.flexible);
        if(same)return;
        bool accepted;
        if(object.pcode!=LL_PCODE_VOLUME || !object.renderable || !object.volume || !linksCurrent(object))
            accepted=selectionIndex->erase(local);
        else accepted=selectionIndex->upsert(local,selectionBounds(object,position,rotation));
        if(!accepted)
        {selectionRefusedBytes=budgets->primitiveBytes.used();selectionRefusedRevision=nextRevision;return;}
        object.spatialEpoch=selectionIndexEpoch;object.spatialRevision=object.revision;
        object.spatialPosition=position;object.spatialRotation=rotation;object.spatialScale=object.scale;
        object.spatialVolume=object.volume.get();object.spatialFlexible=bool(object.flexible);
    }

    void dirtyChildren(U32 parent)
    {
        std::set<U32> dirty{parent};
        for (std::size_t pass=0;pass<256;++pass)
        {
            bool changed=false;
            for (auto& [id,o]:objects)
                if (dirty.count(o.parent) && dirty.insert(id).second) { o.dirty=true;changed=true; }
            if (!changed) break;
        }
    }
};
VSWorldScene::VSWorldScene(VSWorldRenderer& r,VSUIResources& u):
    VSWorldScene(r,u,std::make_shared<VSWorldAssets>(),std::make_shared<Budgets>()) { mRoot=true; }
VSWorldScene::VSWorldScene(VSWorldRenderer& r,VSUIResources& u,std::shared_ptr<VSWorldAssets> cache,std::shared_ptr<Budgets> budgets):
    mImpl(std::make_shared<Impl>(r,u,std::move(cache),std::move(budgets))),mRoot(false) {}
VSWorldScene::~VSWorldScene()
{
    mNeighbours.clear();
    for(auto& [local,object]:mImpl->objects)if(object.icon)object.icon->markDead();
    mImpl->retired = true;
    LLHUDIcon::cleanupDeadIcons();LLHUDObject::cleanupUnreferenced();
    mImpl->dirtyTerrainNeighbours();
    mImpl.reset();
    if (mRoot)
        VSWorldAvatar::cleanupClass();
}
void VSWorldScene::reset(bool preserveAssets)
{
    mNeighbours.clear();if (!preserveAssets) mImpl->assets->reset();
    mImpl->avatars.clear();
    ++mImpl->terrainRevision;
    mImpl->ground.clear();
    mImpl->dirtyTerrainNeighbours();
    mImpl->patchSize                      = 0;
    mImpl->water.reset();mImpl->waterKnown=false;mImpl->terrainTextures={};
    mImpl->materialAssets.reset();mImpl->pendingMaterialOverrides.clear();
    mImpl->terrainAssets.reset();
    if(mRoot && !preserveAssets) mImpl->resources.erase("terrain:local-paint");
    for(auto& [local,object]:mImpl->objects)if(object.icon)object.icon->markDead();
    mImpl->objects.clear();mImpl->particleObjects.clear();++mImpl->budgets->particleRevision;mImpl->pending.clear();mImpl->nextRequest=0;
    LLHUDIcon::cleanupDeadIcons();LLHUDObject::cleanupUnreferenced();
    mImpl->objectIndex.clear();
    mImpl->pendingAnimations.clear();
    mImpl->visiblePackets.clear();
    mImpl->accepted=mImpl->rejected=mImpl->unsupported=0;
    if(mRoot && !preserveAssets) {mImpl->budgets->account.setNull();mImpl->budgets->accountGeneration=0;}
}
boost::signals2::connection VSWorldScene::observeTerrainSettings(const LLHost& host,const boost::signals2::signal<void()>::slot_type& slot)
{
    if(host!=mImpl->host)
    {
        const auto found=mNeighbours.find(host);
        return found==mNeighbours.end()?boost::signals2::connection():found->second->observeTerrainSettings(host,slot);
    }
    if(mImpl->retired)return {};
    if(!mImpl->terrainAssets)mImpl->terrainAssets=std::make_unique<VSTerrainAssets>(host);
    return mImpl->terrainAssets->observeSettings(slot);
}
std::optional<VSRegionTerrain> VSWorldScene::terrainSettings(const LLHost& host)
{
    if(host!=mImpl->host)
    {
        const auto found=mNeighbours.find(host);
        return found==mNeighbours.end()?std::nullopt:found->second->terrainSettings(host);
    }
    if(mImpl->retired)return {};
    if(!mImpl->terrainAssets)mImpl->terrainAssets=std::make_unique<VSTerrainAssets>(host);
    VSRegionTerrain result;result.assets=mImpl->terrainTextures;result.starts=mImpl->terrainStarts;result.ranges=mImpl->terrainRanges;
    const auto ready=mImpl->terrainAssets->settings(result.assets,result.overrides);
    result.texturesReady=ready.first;result.materialsReady=ready.second;
    const auto weak=std::weak_ptr<Impl>(mImpl);const auto incarnation=mImpl->incarnation;
    const auto assets=result.assets;const auto starts=result.starts,ranges=result.ranges;
    result.current=[weak,incarnation,assets,starts,ranges]
    {
        const auto state=weak.lock();
        return state && !state->retired && state->incarnation==incarnation && state->terrainTextures==assets &&
            state->terrainStarts==starts && state->terrainRanges==ranges;
    };
    return result;
}
std::optional<U64> VSWorldScene::terrainRevision(const LLHost& host) const
{
    if (host!=mImpl->host)
    {
        const auto found=mNeighbours.find(host);
        return found==mNeighbours.end()?std::nullopt:found->second->terrainRevision(host);
    }
    return mImpl->retired?std::nullopt:std::optional<U64>(mImpl->terrainRevision);
}
std::optional<VSWorldScene::TerrainField> VSWorldScene::terrainField(const LLHost& host) const
{
    if (host!=mImpl->host)
    {
        const auto found=mNeighbours.find(host);
        return found==mNeighbours.end()?std::nullopt:found->second->terrainField(host);
    }
    if (mImpl->retired) return {};
    TerrainField result;
    result.revision=mImpl->terrainRevision;result.incarnation=mImpl->incarnation;
    result.width=mImpl->width;result.height=mImpl->regionHeight;
    const auto weak=std::weak_ptr<Impl>(mImpl);
    const auto revision=result.revision,incarnation=result.incarnation;
    result.current=[weak,revision,incarnation]
    {
        const auto owner=weak.lock();
        return owner && !owner->retired && owner->incarnation==incarnation && owner->terrainRevision==revision;
    };
    const auto sampler=[weak,revision,incarnation](bool initialGrid)
    {
        return [weak,revision,incarnation,initialGrid](float x,float y)->std::optional<float>
        {
            const auto owner=weak.lock();
            if (!owner || owner->retired || owner->incarnation!=incarnation || owner->terrainRevision!=revision ||
                !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=owner->width || y>=owner->regionHeight) return {};
            bool received=true;
            const float value=VSParcelGeometry::terrainHeight(x,y,1.f,S32(owner->width),S32(owner->regionHeight),
                [&received,&owner,initialGrid](S32 column,S32 row)
                {
                    const float height=owner->heightLocal(column,row,std::numeric_limits<float>::quiet_NaN());
                    received=received && std::isfinite(height);
                    return initialGrid && !std::isfinite(height)?0.f:height;
                });
            return (initialGrid || received) && std::isfinite(value)?std::optional<float>(value):std::nullopt;
        };
    };
    result.sample=sampler(false);
    result.referenceHeight=sampler(true);
    return result;
}

bool VSWorldScene::openRegion(const LLHost& host,U64 handle,U32 width,U32 height,U64 incarnation)
{
    if (!host.isOk() || width<16 || height<16 || width>65536 || height>65536 || !incarnation) return false;
    if (host==mImpl->host)
    {
        if (mImpl->handle!=handle || mImpl->incarnation!=incarnation)
        {
            auto previous     = mImpl;
            previous->retired = true;
            previous->dirtyTerrainNeighbours();
            mImpl=std::make_shared<Impl>(previous->renderer,previous->resources,previous->assets,previous->budgets);
        }
        mImpl->host=host;mImpl->handle=handle;mImpl->incarnation=incarnation;
        if (mImpl->width!=width || mImpl->regionHeight!=height)
        {
            mImpl->water.reset();
            for (auto patch=mImpl->ground.begin();patch!=mImpl->ground.end();)
                if (U32(patch->first>>32)+patch->second.size>width || U32(patch->first)+patch->second.size>height) patch=mImpl->ground.erase(patch);
                else {patch->second.dirty=true;++patch;}
        }
        mImpl->width                  = width;
        mImpl->regionHeight           = height;
        mImpl->budgets->regions[host] = mImpl;
        return true;
    }
    auto found=mNeighbours.find(host);
    if (found==mNeighbours.end())
    {
        if (mNeighbours.size()>=64) return false;
        auto region=std::unique_ptr<VSWorldScene>(new VSWorldScene(mImpl->renderer,mImpl->resources,mImpl->assets,mImpl->budgets));
        region->mImpl->host=host;
        found=mNeighbours.emplace(host,std::move(region)).first;
    }
    return found->second->openRegion(host,handle,width,height,incarnation);
}
bool VSWorldScene::activateRegion(const LLHost& host,U64 handle,U32 width,U32 height,U64 incarnation)
{
    if (!mImpl->host.isOk()) mImpl->host=host;
    const auto agent=mImpl->agent;
    auto self=mImpl->avatars.extract(agent);
    if (!openRegion(host,handle,width,height,incarnation))
    {if(!self.empty())mImpl->avatars.insert(std::move(self));return false;}
    if (host!=mImpl->host)
    {
        auto destination=mNeighbours.extract(host);
        const auto previous=mImpl->host;
        mImpl.swap(destination.mapped()->mImpl);
        destination.key()=previous;mNeighbours.insert(std::move(destination));
    }
    mImpl->agent=agent;
    if(!self.empty())
    {
        const std::weak_ptr<Impl> weak=mImpl;
        self.mapped()->rebase(handle,[weak](const LLVector3& point,LLVector3& position,LLVector3& normal)
        {auto region=weak.lock();return region && !region->retired && region->groundAt(point,position,normal);});
        mImpl->avatars.erase(agent);mImpl->avatars.insert(std::move(self));
    }
    return true;
}
void VSWorldScene::retireRegion(const LLHost& host)
{
    if (host==mImpl->host) reset();else mNeighbours.erase(host);
}
bool VSWorldScene::configureRegion(const LLHost& host,const LLSD& handshake)
{
    if (host==mImpl->host) { configureRegion(handshake);return true; }
    const auto found=mNeighbours.find(host);
    return found!=mNeighbours.end() && found->second->configureRegion(host,handshake);
}
std::optional<VSWorldScene::ObjectHandle> VSWorldScene::object(const LLHost& host,U32 local) const
{
    if (host!=mImpl->host)
    {
        const auto found=mNeighbours.find(host);
        return found==mNeighbours.end()?std::nullopt:found->second->object(host,local);
    }
    const auto found=mImpl->objects.find(local);
    if (found==mImpl->objects.end()) return std::nullopt;
    auto result=ObjectHandle{host,found->second.id,local,mImpl->incarnation,found->second.generation};
    result.kind=Impl::kind(found->second);
    result.account=mImpl->budgets->account;result.accountGeneration=mImpl->budgets->accountGeneration;
    return result;
}
bool VSWorldScene::current(const ObjectHandle& token) const
{
    const auto owned=object(token.host,token.local);
    return owned && owned->id==token.id && owned->region==token.region && owned->generation==token.generation && owned->kind==token.kind &&
        owned->account==token.account && owned->accountGeneration==token.accountGeneration;
}
std::optional<VSWorldScene::ObjectHandle> VSWorldScene::object(const LLUUID& identity) const
{
    const auto [begin,end]=mImpl->objectIndex.identities(identity);
    for(auto it=begin;it!=end;++it)
    {
        const auto found=mImpl->objects.find(it->second);
        if(found!=mImpl->objects.end() && mImpl->linksCurrent(found->second))
            return object(mImpl->host,it->second);
    }
    for (const auto& [host, region] : mNeighbours)
        if (auto value = region->object(identity))
            return value;
    return std::nullopt;
}
std::optional<std::vector<VSWorldScene::ObjectHandle>> VSWorldScene::linkedFamily(const ObjectHandle& token,bool rootEdit,bool includeChildren,FamilyTraversal traversal) const
{
    if(mImpl->retired || !current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->linkedFamily(token,rootEdit,includeChildren,traversal);
    U32 root=token.local;
    std::set<U32> visited;
    if(rootEdit)
    {
        // Reference selection ascends only non-avatar links; an avatar itself
        // remains a singleton, never a temporary selection of its attachments.
        while(mImpl->objects.at(root).pcode!=LL_PCODE_LEGACY_AVATAR)
        {
            const auto& child=mImpl->objects.at(root);
            if(!child.parent)break;
            if(child.parentRetired || !visited.insert(root).second)return {};
            const auto parent=mImpl->objects.find(child.parent);
            if(parent==mImpl->objects.end() || (child.parentGeneration && child.parentGeneration!=parent->second.generation))return {};
            if(parent->second.pcode==LL_PCODE_LEGACY_AVATAR)break;
            root=parent->first;
        }
    }
    visited.clear();
    std::vector<ObjectHandle> result;
    using Iterator=decltype(mImpl->objectIndex.children(root).first);
    std::vector<std::pair<Iterator,Iterator>> pending;
    auto append=[&](U32 local)->bool
    {
        if(!visited.insert(local).second)return false;
        const auto handle=object(mImpl->host,local);
        if(!handle)return false;
        result.push_back(*handle);
        if(includeChildren && (traversal==FamilyTraversal::AllChildren || mImpl->objects.at(local).pcode!=LL_PCODE_LEGACY_AVATAR))
            pending.push_back(mImpl->objectIndex.children(local));
        return true;
    };
    if(!append(root))return {};
    while(!pending.empty())
    {
        auto& range=pending.back();
        if(range.first==range.second){pending.pop_back();continue;}
        const auto edge=range.first++;
        const U32 parentLocal=edge->first.first,local=edge->second;
        const auto child=mImpl->objects.find(local);
        if(child==mImpl->objects.end() || child->second.pcode==LL_PCODE_LEGACY_AVATAR)continue;
        // Parent generation belongs to this circuit, never a reused local-ID
        // incarnation. Index rows alone do not confer current ancestry.
        const auto parent=mImpl->objects.find(child->second.parent);
        if(child->second.parent!=parentLocal || child->second.parentRetired || parent==mImpl->objects.end() ||
            (child->second.parentGeneration && child->second.parentGeneration!=parent->second.generation))continue;
        if(!append(local))return {};
    }
    return result;
}

bool VSWorldScene::loadGLTF(const ObjectHandle& token, const std::string& filename)
{
    if (!current(token) || filename.empty())
        return false;
    if (token.host != mImpl->host)
        return mNeighbours.at(token.host)->loadGLTF(token, filename);
    auto candidate =
        std::make_shared<VSWorldGLTF>(filename, mImpl->renderer, mImpl->resources, mImpl->budgets->primitiveBytes, guard(token));
    if (!candidate->ready())
    {
        LL_WARNS("NativeGLTF") << "Local scene import rejected: " << candidate->evidence()["error"].asString() << LL_ENDL;
        return false;
    }
    auto& object       = mImpl->objects.at(token.local);
    object.pendingGLTF = std::move(candidate);
    object.renderable  = true;
    object.revision    = mImpl->nextRevision++;
    object.dirty       = true;
    return true;
}
std::optional<VSWorldScene::FaceHandle> VSWorldScene::face(const ObjectHandle& token,U32 index) const
{
    if (!current(token)) return std::nullopt;
    if (token.host!=mImpl->host) return mNeighbours.at(token.host)->face(token,index);
    const auto& object=mImpl->objects.at(token.local);
    if (index >= std::max(object.faces.size(), object.admittedFaces.size()))
        return std::nullopt;
    return FaceHandle{token,index,object.revision};
}
std::shared_ptr<const VSResourceBudget::Reservation> VSWorldScene::reserveSpatialBytes(std::size_t bytes)
{
    if(mImpl->retired)return {};
    return mImpl->budgets->terrainBytes.reserve(bytes);
}
// Flags/type queries deliberately do not evaluate transforms or copy strings.
std::optional<LLUUID> VSWorldScene::objectInventoryItem(const ObjectHandle& token) const
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->objectInventoryItem(token);
    return mImpl->objects.at(token.local).inventoryItem;
}

std::optional<VSWorldScene::LocalTransform> VSWorldScene::objectLocalTransform(const ObjectHandle& token) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectLocalTransform(token);
    const auto& source=mImpl->objects.at(token.local);
    return LocalTransform{source.motion.position(),source.scale,source.motion.rotation()};
}
bool VSWorldScene::previewLocalTransform(const ObjectHandle& token,const LocalTransform& value,U32 components)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->previewLocalTransform(token,value,components);
    if(((components&1) && !value.position.isFinite()) || ((components&2) && !value.rotation.isFinite()) || ((components&4) && !value.scale.isFinite()))return false;
    if(!(components&7))return true;
    auto& source=mImpl->objects.at(token.local);
    if(components&1){source.position=value.position;source.motion.previewPosition(value.position);}
    if(components&2){source.rotation=value.rotation;source.motion.previewRotation(value.rotation);}
    if(components&4)source.scale=value.scale;
    source.dirty=true;mImpl->dirtyChildren(token.local);
    source.revision=mImpl->nextRevision++;
    return true;
}

std::optional<bool> VSWorldScene::objectIsRootEdit(const ObjectHandle& token) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectIsRootEdit(token);
    const auto& source=mImpl->objects.at(token.local);
    if(!source.parent)return true;
    if(source.parentRetired)return {};
    const auto parent=mImpl->objects.find(source.parent);
    if(parent==mImpl->objects.end() || (source.parentGeneration && source.parentGeneration!=parent->second.generation))return {};
    return source.pcode==LL_PCODE_VOLUME && parent->second.pcode==LL_PCODE_LEGACY_AVATAR;
}
std::optional<VSWorldScene::LocalTransform> VSWorldScene::objectEditTransform(const ObjectHandle& token) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectEditTransform(token);
    // Exact LLViewerObject::getPositionEdit recursion and getRotationEdit's
    // immediate-parent multiplication, using accepted source-local transforms.
    std::array<const Impl::Object*,256> chain{};std::array<U32,256> locals{};
    std::size_t count=0;U32 local=token.local;
    for(;;)
    {
        if(count==chain.size() || std::find(locals.begin(),locals.begin()+count,local)!=locals.begin()+count)return {};
        const auto& source=mImpl->objects.at(local);locals[count]=local;chain[count++]=&source;
        if(!source.parent)break;
        if(source.parentRetired)return {};
        const auto parent=mImpl->objects.find(source.parent);
        if(parent==mImpl->objects.end() || (source.parentGeneration && source.parentGeneration!=parent->second.generation))return {};
        if(source.pcode==LL_PCODE_VOLUME && parent->second.pcode==LL_PCODE_LEGACY_AVATAR)break;
        local=source.parent;
    }
    const auto& root=*chain[count-1];LocalTransform result{root.motion.position(),chain[0]->scale,root.motion.rotation()};
    for(std::size_t index=count-1;index>0;--index)
    {
        const auto& child=*chain[index-1];
        result.position=result.position+(child.motion.position()*result.rotation);
        result.rotation=child.motion.rotation()*chain[index]->motion.rotation();
    }
    return result;
}

bool VSWorldScene::previewObjectFlag(const ObjectHandle& token,U32 mask,bool enabled)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->previewObjectFlag(token,mask,enabled);
    auto& source=mImpl->objects.at(token.local);
    source.updateFlags=enabled?(source.updateFlags|mask):(source.updateFlags&~mask);
    return true;
}
bool VSWorldScene::previewClickAction(const ObjectHandle& token,U8 action)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->previewClickAction(token,action);
    mImpl->objects.at(token.local).clickAction=action;
    return true;
}
LLPointer<LLVolume> VSWorldScene::objectVolume(const ObjectHandle& token) const
{
    if(!current(token))return nullptr;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectVolume(token);
    return mImpl->objects.at(token.local).volume;
}
std::optional<LLVolumeParams> VSWorldScene::objectVolumeParams(const ObjectHandle& token)const
{
    if(!current(token))return std::nullopt;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectVolumeParams(token);
    return mImpl->objects.at(token.local).params;
}
bool VSWorldScene::refreshBakeTexture(const ObjectHandle& token)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->refreshBakeTexture(token);
    // Recompose the actual attachment material/texture packets from the current
    // avatar bake owner. Do not invalidate or rebake local glyphs/wearables.
    auto& object=mImpl->objects.at(token.local);object.dirty=true;
    return true;
}
bool VSWorldScene::previewVolume(const ObjectHandle& token,const LLVolumeParams& params)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->previewVolume(token,params);
    const auto& source=mImpl->objects.at(token.local);
    if(source.params==params)return false;
    auto next=source;next.params=params;next.dirty=true;
    next.volume=params.getSculptID().isNull()?new LLVolume(params,2.f):nullptr;
    next.primitiveResidency.reset();
    if(source.params.getSculptID()!=params.getSculptID() || source.params.getSculptType()!=params.getSculptType())
    {next.asset.reset();next.assetLOD=-1;next.gltf.reset();next.pendingGLTF.reset();}
    if(next.volume)
    {
        const S32 count=next.volume->getNumVolumeFaces();
        if(count<=0 || count>45)return false;
        const LLTextureEntry entry=next.faces.empty()?LLTextureEntry():next.faces.back().material;
        while(next.faces.size()<std::size_t(count))next.faces.push_back({{},entry});
        next.faces.resize(std::size_t(count));
    }
    return mImpl->replace(token.local,std::move(next));
}
bool VSWorldScene::previewMaterial(const ObjectHandle& token,U8 material)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->previewMaterial(token,material);
    mImpl->objects.at(token.local).material=material;return true;
}
std::shared_ptr<const VSObjectEditParameters> VSWorldScene::editParameters(const ObjectHandle& token) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->editParameters(token);
    return mImpl->objects.at(token.local).edit;
}
bool VSWorldScene::setObjectCostValues(const ObjectHandle& token,const VSObjectCostValues& costs)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->setObjectCostValues(token,costs);
    auto& source=mImpl->objects.at(token.local);
    if(!source.edit)return false;
    source.edit->costs=costs;return true;
}
bool VSWorldScene::setPhysicsProperties(const ObjectHandle& token,const VSObjectPhysicsProperties& properties)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->setPhysicsProperties(token,properties);
    auto& source=mImpl->objects.at(token.local);
    if(!source.edit)return false;
    source.edit->physics=properties;
    return true;
}
bool VSWorldScene::setExtraParameter(const ObjectHandle& token,U16 type,const LLNetworkData& value,bool inUse,bool send)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->setExtraParameter(token,type,value,inUse,send);
    const auto state=mImpl;
    const auto currentSource=guard(token);
    const auto account=VSNativeSession::active();
    if(!account || account->phase()!=VSNativeSession::Phase::Connected)return false;
    const auto currentAccount=account->accountGuard();
    const auto& source=state->objects.at(token.local);
    if(!source.edit || !source.edit->get(type))return false;
    const auto* old=source.edit->get(type);
    if(source.edit->inUse(type)==inUse && (!inUse || *old==value))return false;
    U8 packedBytes[1024];LLDataPackerBinaryBuffer packed(packedBytes,1024);
    if(send && !value.pack(packed))return false;
    // Publish the CPU edit through the same admitted object replacement used
    // by simulator updates. A refused geometry allocation leaves its complete
    // previous owner and preview intact, and sends no uncommitted mutation.
    auto next=source;
    next.edit=std::make_shared<VSObjectEditParameters>(*source.edit);
    next.edit->get(type)->copy(value);next.edit->setInUse(type,inUse);
    next.light=next.edit->inUse(LLNetworkData::PARAMS_LIGHT) && next.edit->light.getLinearColor().mV[3]!=0;
    next.reflectionProbe=next.edit->inUse(LLNetworkData::PARAMS_REFLECTION_PROBE);
    next.extendedMeshFlags=next.edit->inUse(LLNetworkData::PARAMS_EXTENDED_MESH)?next.edit->mesh.getFlags():0;
    next.flexible=next.edit->inUse(LLNetworkData::PARAMS_FLEXIBLE)?std::optional<LLFlexibleObjectData>(next.edit->flexible):std::nullopt;
    if(type==LLNetworkData::PARAMS_SCULPT || type==LLNetworkData::PARAMS_MESH)
    {
        next.params.setSculptID(inUse?next.edit->sculpt.getSculptTexture():LLUUID::null,
                               inUse?next.edit->sculpt.getSculptType():LL_SCULPT_TYPE_NONE);
        next.asset.reset();next.assetLOD=-1;next.gltf.reset();next.pendingGLTF.reset();
    }
    if(type==LLNetworkData::PARAMS_FLEXIBLE)
        next.params.setType(next.params.getProfileParams().getCurveType(),inUse?LL_PCODE_PATH_FLEXIBLE:LL_PCODE_PATH_LINE);
    if(type==LLNetworkData::PARAMS_FLEXIBLE || type==LLNetworkData::PARAMS_SCULPT || type==LLNetworkData::PARAMS_MESH)
    {
        next.volume=next.params.getSculptID().isNull()?new LLVolume(next.params,2.f):nullptr;
        next.primitiveResidency.reset();next.dirty=true;
        if(next.volume)
        {
            const auto count=next.volume->getNumVolumeFaces();
            if(count<=0 || count>45)return false;
            const LLTextureEntry entry=next.faces.empty()?LLTextureEntry():next.faces.back().material;
            while(next.faces.size()<std::size_t(count))next.faces.push_back({{},entry});
            next.faces.resize(std::size_t(count));
        }
    }
    if(!state->replace(token.local,std::move(next)))return false;
    if(send && currentAccount() && currentSource() && gMessageSystem)
    {
        gMessageSystem->newMessageFast(_PREHASH_ObjectExtraParams);
        gMessageSystem->nextBlockFast(_PREHASH_AgentData);
        gMessageSystem->addUUIDFast(_PREHASH_AgentID,gAgent.getID());
        gMessageSystem->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID());
        gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
        gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID,token.local);
        gMessageSystem->addU16Fast(_PREHASH_ParamType,type);
        gMessageSystem->addBOOLFast(_PREHASH_ParamInUse,inUse);
        gMessageSystem->addU32Fast(_PREHASH_ParamSize,U32(packed.getCurrentSize()));
        gMessageSystem->addBinaryDataFast(_PREHASH_ParamData,packedBytes,packed.getCurrentSize());
        gMessageSystem->sendReliable(token.host);
    }
    return true;
}
std::optional<VSWorldScene::ObjectAdmission> VSWorldScene::objectAdmission(const ObjectHandle& token) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectAdmission(token);
    const auto& source=mImpl->objects.at(token.local);
    ObjectAdmission result;result.updateFlags=source.updateFlags;result.rootUpdateFlags=source.updateFlags;result.pcode=source.pcode;result.clickAction=source.clickAction;
    const Impl::Object* child=&source;
    std::array<U32,256> visited{};unsigned count=1;visited[0]=token.local;
    while(child->parent)
    {
        if(child->parentRetired || count==visited.size() || std::find(visited.begin(),visited.begin()+count,child->parent)!=visited.begin()+count)
        {result.orphaned=true;break;}
        visited[count++]=child->parent;
        const auto parent=mImpl->objects.find(child->parent);
        if(parent==mImpl->objects.end() || (child->parentGeneration && child->parentGeneration!=parent->second.generation))
        {result.orphaned=true;break;}
        if(parent->second.pcode==LL_PCODE_LEGACY_AVATAR)
        {
            result.attachment=true;
            const auto avatar=mImpl->avatars.find(parent->second.id);
            if(avatar==mImpl->avatars.end())return {}; // actual attachment-point metadata not available yet
            const unsigned state=child->attachment;
            result.hud=avatar->second->hudAttachment(((state&15)<<4)|(state>>4));
            break;
        }
        child=&parent->second;result.rootUpdateFlags=child->updateFlags;
    }
    return result;
}


U64 VSWorldScene::particleRevision() const {return mImpl->budgets->particleRevision;}
std::size_t VSWorldScene::particleSystemCount() const
{
    std::size_t count=mImpl->particleObjects.size();
    for(const auto& entry:mNeighbours)count+=entry.second->particleSystemCount();
    return count;
}
std::vector<VSWorldScene::ParticleSystem> VSWorldScene::particleSystems() const
{
    std::vector<ParticleSystem> result;result.reserve(particleSystemCount());
    for(const auto local:mImpl->particleObjects)
    {
        const auto found=mImpl->objects.find(local);
        if(found==mImpl->objects.end() || !found->second.particles)continue;
        const auto token=object(mImpl->host,local);if(!token)continue;
        const auto& value=found->second;
        result.push_back({*token,*value.particles,value.reportedOwner,value.particleRevision,value.particleFullMessage});
    }
    for(const auto& entry:mNeighbours)
    {
        auto adjacent=entry.second->particleSystems();
        result.insert(result.end(),std::make_move_iterator(adjacent.begin()),std::make_move_iterator(adjacent.end()));
    }
    return result;
}
std::optional<LLVector3> VSWorldScene::particlePosition(const ObjectHandle& token,bool leftWrist) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->particlePosition(token,leftWrist);
    if(leftWrist && token.kind==VSWorldContracts::OwnerKind::Avatar)
    {
        const auto found=mImpl->avatars.find(token.id);if(found==mImpl->avatars.end())return {};
        auto* joint=found->second->character().getJoint("mWristLeft");if(!joint)return {};
        return joint->getWorldPosition()+LLVector3(from_region_handle(mImpl->handle)-gAgent.getPosGlobalFromAgent(LLVector3::zero));
    }
    const auto value=focusObject(token,gAgent.getPosGlobalFromAgent(LLVector3::zero));
    return value?std::optional<LLVector3>(value->renderPosition):std::nullopt;
}

boost::signals2::connection VSWorldScene::observeObjectSounds(const boost::signals2::signal<void(const ObjectHandle&)>::slot_type& slot)
{
    return mImpl->budgets->objectSounds.connect(slot);
}
bool VSWorldScene::derender(const ObjectHandle& token)
{
    if (!current(token)) return false;
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->derender(token);
    std::vector<U32> family{token.local};
    std::set<U32> visited{token.local};
    for (std::size_t i = 0; i < family.size(); ++i)
    {
        const auto [begin, end] = mImpl->objectIndex.children(family[i]);
        for (auto edge = begin; edge != end; ++edge)
        {
            const auto child = mImpl->objects.find(edge->second);
            if (child == mImpl->objects.end() || child->second.parentRetired) continue;
            const auto parent = mImpl->objects.find(child->second.parent);
            if (parent == mImpl->objects.end() || (child->second.parentGeneration && child->second.parentGeneration != parent->second.generation)) continue;
            if (!visited.insert(child->first).second) continue;
            family.push_back(child->first);
        }
    }
    for (auto i = family.rbegin(); i != family.rend(); ++i) mImpl->eraseObject(*i);
    return true;
}
std::optional<VSWorldScene::ObjectSound> VSWorldScene::objectSound(const ObjectHandle& token) const
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->objectSound(token);
    const auto snapshot = objectSnapshot(token);
    if (!snapshot) return {};
    const auto& source = mImpl->objects.at(token.local);
    ObjectSound result;
    result.object = token; result.asset = source.soundAsset; result.owner = source.reportedOwner;
    result.gain = source.soundGain; result.flags = source.soundFlags;
    auto& state = result.state;
    state.id = source.id; state.cutoff = source.soundRadius;
    state.position = snapshot->position; state.attachment = snapshot->attachment; state.hud = snapshot->hud;
    state.velocity = source.velocity;
    if (source.parent)
        if (const auto parent = mImpl->objects.find(source.parent); parent != mImpl->objects.end()) state.velocity = parent->second.velocity;
    if (const auto root = rootObject(token, false)) state.root = root->id;
    if (snapshot->attachment)
        if (const auto avatar = object(snapshot->rootID))
            if (const auto transform = objectTransform(*avatar))
            {
                state.avatarRoot = avatar->id; state.avatarRootPosition = transform->position;
            }
    for (const auto& [host, weak] : mImpl->budgets->regions)
    {
        const auto owner = weak.lock();
        if (!owner || owner->retired) continue;
        bool foundAgent = false;
        for (const auto& [local, agent] : owner->objects)
        {
            if (agent.id != mImpl->budgets->account || !owner->linksCurrent(agent)) continue;
            foundAgent = true; state.agentRiding = agent.parent != 0;
            const Impl::Object* root = &agent;
            std::set<U32> visited;
            while (root->parent && visited.insert(root->parent).second)
            {
                const auto parent = owner->objects.find(root->parent);
                if (parent == owner->objects.end()) break;
                root = &parent->second;
            }
            state.agentRoot = root->id;
            break;
        }
        if (foundAgent) break;
    }
    return result;
}

std::optional<VSWorldScene::ObjectSnapshot> VSWorldScene::objectSnapshot(const ObjectHandle& token) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectSnapshot(token);
    const auto& source=mImpl->objects.at(token.local);
    LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
    if(!mImpl->transform(token.local,position,rotation,visited))return {};
    ObjectSnapshot row;
    row.object=token;row.rootEditID=source.id;row.rootID=source.id;row.hud=mImpl->hudObject(source);row.owner=source.reportedOwner;row.ownerKnown=source.reportedOwnerKnown;
    row.nameValues=source.nameValues;row.mediaVersion=source.mediaVersion;row.updateFlags=source.updateFlags;row.rootUpdateFlags=source.updateFlags;
    row.material=source.material;row.clickAction=source.clickAction;row.rootClickAction=source.clickAction;
    row.pcode=source.pcode;row.position=from_region_handle(mImpl->handle)+LLVector3d(position);row.scale=source.scale;
    const Impl::Object* ancestor=&source;
    std::set<U32> ancestors;
    while(ancestor->parent && ancestors.insert(ancestor->parent).second && ancestors.size()<=256)
    {
        const auto parent=mImpl->objects.find(ancestor->parent);
        if(parent==mImpl->objects.end())break;
        row.rootID=parent->second.id;
        if(parent->second.pcode==LL_PCODE_LEGACY_AVATAR){row.attachment=true;break;}
        ancestor=&parent->second;row.rootEditID=ancestor->id;row.rootUpdateFlags=ancestor->updateFlags;row.rootClickAction=ancestor->clickAction;
    }
    return row;
}
std::vector<VSWorldScene::ObjectSnapshot> VSWorldScene::objectSnapshots() const
{
    std::vector<ObjectSnapshot> result;
    if(mImpl->retired)return result;
    for(const auto& [local,source]:mImpl->objects)
    {
        if(!mImpl->linksCurrent(source))continue;
        auto handle=object(mImpl->host,local);if(!handle)continue;
        if(auto row=objectSnapshot(*handle))result.push_back(std::move(*row));
    }
    for(const auto& [host,region]:mNeighbours)
    {
        auto nearby=region->objectSnapshots();
        result.insert(result.end(),std::make_move_iterator(nearby.begin()),std::make_move_iterator(nearby.end()));
    }
    return result;
}

std::vector<VSWorldScene::MapObjectSnapshot> VSWorldScene::mapObjectSnapshots() const
{
    std::vector<MapObjectSnapshot> result;
    if(mImpl->retired)return result;
    result.reserve(mImpl->objects.size());
    for(const auto& [local,source]:mImpl->objects)
    {
        if(!mImpl->linksCurrent(source) || source.pcode==LL_PCODE_LEGACY_AVATAR)continue;
        auto token=object(mImpl->host,local);if(!token)continue;
        LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
        if(!mImpl->transform(local,position,rotation,visited))continue;
        MapObjectSnapshot row;
        row.object=*token;row.updateFlags=source.updateFlags;row.rootUpdateFlags=source.updateFlags;
        row.pcode=source.pcode;row.position=from_region_handle(mImpl->handle)+LLVector3d(position);row.scale=source.scale;
        const Impl::Object* ancestor=&source;
        std::set<U32> ancestors;
        while(ancestor->parent && ancestors.insert(ancestor->parent).second && ancestors.size()<=256)
        {
            const auto parent=mImpl->objects.find(ancestor->parent);
            if(parent==mImpl->objects.end())break;
            if(parent->second.pcode==LL_PCODE_LEGACY_AVATAR){row.attachment=true;break;}
            ancestor=&parent->second;row.rootUpdateFlags=ancestor->updateFlags;
        }
        if(!row.attachment)result.push_back(std::move(row));
    }
    for(const auto& [host,region]:mNeighbours)
    {
        auto nearby=region->mapObjectSnapshots();
        result.insert(result.end(),std::make_move_iterator(nearby.begin()),std::make_move_iterator(nearby.end()));
    }
    return result;
}

std::vector<VSWorldScene::AvatarLocation> VSWorldScene::avatarLocations() const
{
    std::vector<AvatarLocation> result;
    if (mImpl->retired) return result;
    for (const auto& [local, object] : mImpl->objects)
    {
        if (object.pcode != LL_PCODE_LEGACY_AVATAR || !mImpl->linksCurrent(object)) continue;
        LLVector3 position; LLQuaternion rotation; std::set<U32> visited;
        if (!mImpl->transform(local, position, rotation, visited)) continue;
        result.push_back({object.id, from_region_handle(mImpl->handle) + LLVector3d(position)});
    }
    for (const auto& [host, region] : mNeighbours)
    {
        auto nearby = region->avatarLocations();
        result.insert(result.end(), nearby.begin(), nearby.end());
    }
    return result;
}
std::vector<VSWorldScene::Attachment> VSWorldScene::attachments() const
{
    std::vector<Attachment> result;
    for (const auto& [local, object] : mImpl->objects)
    {
        if (!object.attachment || !mImpl->linksCurrent(object))
            continue;
        const Impl::Object* child = &object;
        std::set<U32>       visited;
        while (child->parent && visited.insert(child->parent).second && visited.size() <= 256)
        {
            const auto parent = mImpl->objects.find(child->parent);
            if (parent == mImpl->objects.end())
                break;
            if (parent->second.pcode == LL_PCODE_LEGACY_AVATAR)
            {
                if (parent->second.id == mImpl->budgets->account)
                    if (auto token = this->object(mImpl->host, local))
                    {
                        const unsigned state = object.attachment;
                        result.push_back({ *token, object.inventoryItem, U8(((state & 15) << 4) | (state >> 4)) });
                    }
                break;
            }
            child = &parent->second;
        }
    }
    for (const auto& [host, region] : mNeighbours)
    {
        auto attachments = region->attachments();
        result.insert(result.end(), attachments.begin(), attachments.end());
    }
    return result;
}
bool VSWorldScene::current(const FaceHandle& token) const
{
    const auto owned=face(token.object,token.face);
    return owned && owned->generation==token.generation;
}
LLUUID VSWorldScene::renderMaterialID(const ObjectHandle& token, U32 side) const
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->renderMaterialID(token, side);
    const auto& object = mImpl->objects.at(token.local);
    if (object.pcode != LL_PCODE_VOLUME || side >= object.faces.size()) return {};
    return object.legacyPreview?LLUUID::null:object.renderMaterials.getMaterial(U8(side));
}

LLTextureEntry* VSWorldScene::materialEntry(const ObjectHandle& token, U32 side)
{
    if (!current(token)) return nullptr;
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->materialEntry(token, side);
    auto& object = mImpl->objects.at(token.local);
    if (object.pcode != LL_PCODE_VOLUME || side >= object.faces.size()) return nullptr;
    auto& result = object.faces[side].material;
    if(result.getMaterialParams().isNull() && !result.getMaterialID().isNull())
    {
        if(!mImpl->materialAssets)mImpl->materialAssets=std::make_unique<VSMaterialAssets>(mImpl->host);
        if(auto material=mImpl->materialAssets->get(result.getMaterialID()))result.setMaterialParams(material);
    }
    // The real base/override remains available to the editor during BP preview,
    // exactly as the legacy object's temporary material cache preserves it.
    if(object.legacyPreview)return &result;
    const LLUUID id = object.renderMaterials.getMaterial(U8(side));
    LLPointer<LLFetchedGLTFMaterial> base = id.notNull() ? gGLTFMaterialList.getMaterial(id) : nullptr;
    result.setGLTFMaterial(base);
    if (base.notNull())
    {
        // Same CPU material composition as LLViewerObject::initRenderMaterial.
        // Rebuild on read so an asynchronously populated base is not frozen at
        // its pre-download values. No GL texture acquisition is involved.
        const auto* override_material = result.getGLTFMaterialOverride();
        bool need_render_material = override_material != nullptr;
        if (!need_render_material)
            for (const auto& texture : base->mTextureId)
                if (LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(texture))
                { need_render_material = true; break; }
        LLPointer<LLFetchedGLTFMaterial> render;
        if (need_render_material)
        {
            render = new LLFetchedGLTFMaterial(*base);
            if (override_material) render->applyOverride(*override_material);
            render->clearFetchedTextures();
        }
        result.setGLTFRenderMaterial(render);
    }
    return &result;
}

bool VSWorldScene::copyMaterialEntry(const ObjectHandle& token, U32 side, LLTextureEntry& result) const
{
    auto* entry = const_cast<VSWorldScene*>(this)->materialEntry(token, side);
    if (!entry) return false;
    result = *entry;
    return true;
}

bool VSWorldScene::previewMaterialID(const ObjectHandle& token, U32 side, const LLUUID& id)
{
    if (!current(token)) return false;
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->previewMaterialID(token, side, id);
    auto& object = mImpl->objects.at(token.local);
    if (object.pcode != LL_PCODE_VOLUME || side >= object.faces.size()) return false;
    object.renderMaterials.setMaterial(U8(side), id);
    if(!object.legacyPreview)
    object.faces[side].material.setGLTFMaterial(id.notNull() ? gGLTFMaterialList.getMaterial(id) : nullptr);
    object.revision = mImpl->nextRevision++;
    return true;
}

bool VSWorldScene::copyTextureEntry(const FaceHandle& token, LLTextureEntry& result) const
{
    if (!current(token)) return false;
    if (token.object.host != mImpl->host) return mNeighbours.at(token.object.host)->copyTextureEntry(token, result);
    const auto& object = mImpl->objects.at(token.object.local);
    if (object.pcode != LL_PCODE_VOLUME) return false;
    const auto& faces = object.admittedFaces.empty() ? object.faces : object.admittedFaces;
    if (token.face >= faces.size()) return false;
    const auto& face = faces.at(token.face);
    result = face.published ? face.publishedMaterial : face.material;
    return true;
}

std::optional<VSWorldContracts::Material> VSWorldScene::material(const FaceHandle& token) const
{
    if(!current(token))return std::nullopt;
    if(token.object.host!=mImpl->host)return mNeighbours.at(token.object.host)->material(token);
    const auto& object = mImpl->objects.at(token.object.local);
    const auto& faces  = object.admittedFaces.empty() ? object.faces : object.admittedFaces;
    if (token.face >= faces.size())
        return std::nullopt;
    const auto& face     = faces.at(token.face);
    const auto& material = face.published ? face.publishedMaterial : face.material;
    if (face.sceneMaterial)
    {
        auto result       = *face.sceneMaterial;
        result.generation = token.generation;
        return result;
    }
    VSWorldContracts::Material result;
    if(face.surface)
    {
        result.diffuse=face.surface->base;result.alpha=face.surface->alpha;
        result.fullbright=face.surface->fullbright;result.doubleSided=face.surface->doubleSided;
          result.sampler=face.surface->samplers[0];
          result.diffuseEncoding=face.surface->pbr?VSWorldContracts::ColorEncoding::Linear:VSWorldContracts::ColorEncoding::EncodedSRGB;
        result.diffuseImageKnown=bool(face.publishedImage) || face.surface->textures[0]==std::array<std::uint8_t,16>{};
        result.generation=token.generation;return result;
    }
    std::copy(material.getColor().mV, material.getColor().mV + 4, result.diffuse.begin());
    result.fullbright        = material.getFullbright();
    result.generation        = token.generation;
    const auto image         = mImpl->resources.image("remote:" + vs_native_image_id(material.getID()).asString());
    result.diffuseImageKnown = material.getID().isNull() || bool(image);
    result.alpha=result.diffuse[3]<.999f?VSWorldContracts::AlphaMode::Blend:
        image?VSUIRenderer::imageDescription(image).alpha:VSWorldContracts::AlphaMode::Opaque;
    return result;
}
std::optional<VSWorldScene::ObjectTransform> VSWorldScene::objectTransform(const ObjectHandle& token) const
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->objectTransform(token);
    LLVector3 position; LLQuaternion rotation; std::set<U32> visited;
    if (!mImpl->transform(token.local, position, rotation, visited)) return {};
    return ObjectTransform{from_region_handle(mImpl->handle) + LLVector3d(position), rotation, mImpl->objects.at(token.local).scale,position};
}
std::optional<LLVector3> VSWorldScene::windVelocity(const LLHost& host,const LLVector3& position) const
{
    if(host!=mImpl->host)
    {
        const auto owner=mNeighbours.find(host);
        return owner!=mNeighbours.end()?owner->second->windVelocity(host,position):std::nullopt;
    }
    if(!mImpl->handle || !position.isFinite())return {};
    return mImpl->windAt(position);
}

std::shared_ptr<const VSWorldRenderer::AvatarTiming> VSWorldScene::avatarTiming(const ObjectHandle& token)
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->avatarTiming(token);
    auto found=mImpl->objects.find(token.local);if(found==mImpl->objects.end() || !mImpl->linksCurrent(found->second))return {};
    U32 local=token.local;std::set<U32> visited;
    VSWorldAvatar* avatar=nullptr;bool control=false;
    while(found!=mImpl->objects.end())
    {
        auto& object=found->second;
        if(object.controlAvatar){avatar=object.controlAvatar.get();control=true;}
        else if(const auto owner=mImpl->avatars.find(object.id);owner!=mImpl->avatars.end())avatar=owner->second.get();
        if(avatar)break;
        if(!object.parent || !visited.insert(local).second || object.parentRetired)return {};
        local=object.parent;const auto parent=mImpl->objects.find(local);
        if(parent==mImpl->objects.end() || (object.parentGeneration && object.parentGeneration!=parent->second.generation))return {};
        found=parent;
    }
    if(!avatar || found==mImpl->objects.end())return {};
    auto& object=found->second;
    if(object.avatarTiming && object.avatarTiming->current())return object.avatarTiming;
    const U32 frame=LLDrawable::getCurrentFrame();
    if(object.avatarTimingAttempt && *object.avatarTimingAttempt==frame)return {};
    object.avatarTimingAttempt=frame; // Refused owner publication cannot yield a partial attachment-only sample.
    auto residency=reserveSpatialBytes(sizeof(VSWorldRenderer::AvatarTiming)+sizeof(ObjectHandle)+sizeof(std::weak_ptr<VSWorldAvatar>));
    if(!residency)return {};
    const auto ownerToken=this->object(mImpl->host,local);if(!ownerToken)return {};
    const auto live=guard(*ownerToken);const std::weak_ptr<Impl> weak=mImpl;
    const U64 animationInstance=avatar->character().getAnimationInstanceId();
    const auto resolve=[weak,live,local,control,animationInstance]()->VSWorldAvatar*
    {
        if(!live())return nullptr;
        const auto state=weak.lock();if(!state)return nullptr;
        const auto object=state->objects.find(local);if(object==state->objects.end())return nullptr;
        VSWorldAvatar* avatar=nullptr;
        if(control)avatar=object->second.controlAvatar.get();
        else if(const auto found=state->avatars.find(object->second.id);found!=state->avatars.end())avatar=found->second.get();
        return avatar && avatar->character().getAnimationInstanceId()==animationInstance?avatar:nullptr;
    };
    auto value=std::make_shared<VSWorldRenderer::AvatarTiming>();value->residency=std::move(residency);
    std::copy(ownerToken->id.mData,ownerToken->id.mData+UUID_BYTES,value->identity.begin());
    value->current=[resolve]{return resolve()!=nullptr;};
    value->publish=[resolve](float milliseconds){if(auto* avatar=resolve())avatar->gpuRenderTime(milliseconds);};
    object.avatarTiming=std::move(value);return object.avatarTiming;
}

std::optional<VSWorldScene::ManipulationObject> VSWorldScene::manipulationObject(const ObjectHandle& token,const LLVector3d& agentOrigin) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->manipulationObject(token,agentOrigin);
    const auto admitted=objectAdmission(token);if(!admitted || admitted->orphaned)return {};
    const auto root=rootObject(token,false),edit=rootObject(token,true);if(!root || !edit)return {};
    const auto& source=mImpl->objects.at(token.local);
    const auto& rootSource=mImpl->objects.at(root->local);
    const auto& editSource=mImpl->objects.at(edit->local);
    ManipulationObject value;value.object=token;value.root=*root;value.rootEdit=*edit;
    value.flags=source.updateFlags;value.rootEditFlags=editSource.updateFlags;value.pcode=source.pcode;
    value.attachment=admitted->attachment;value.hud=admitted->hud;
    value.rotation=source.rotation;value.rootRotation=rootSource.rotation;
    LLVector3 official,rootPosition;LLQuaternion ignored;std::set<U32> visited;
    if(!mImpl->transform(token.local,official,ignored,visited,false))return {};
    visited.clear();if(!mImpl->transform(root->local,rootPosition,ignored,visited,false))return {};
    visited.clear();if(!mImpl->transform(token.local,value.renderPosition,value.renderRotation,visited,true))return {};
    const LLVector3d origin=from_region_handle(mImpl->handle);
    value.position=origin+LLVector3d(official);value.rootPosition=origin+LLVector3d(rootPosition);
    if(!value.hud)value.renderPosition+=LLVector3(origin-agentOrigin);
    return value;
}

std::optional<VSCameraFocusObject> VSWorldScene::focusObject(const ObjectHandle& token,const LLVector3d& agentOrigin) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->focusObject(token,agentOrigin);
    const auto& object=mImpl->objects.at(token.local);
    if(!mImpl->linksCurrent(object))return {};
    LLVector3 renderPosition,officialPosition;LLQuaternion renderRotation,officialRotation;std::set<U32> visited;
    if(!mImpl->transform(token.local,renderPosition,renderRotation,visited,true))return {};
    visited.clear();if(!mImpl->transform(token.local,officialPosition,officialRotation,visited,false))return {};
    const LLVector3 regionOffset=mImpl->hudObject(object)?LLVector3::zero:LLVector3(from_region_handle(mImpl->handle)-agentOrigin);
    VSCameraFocusObject result;result.renderPosition=renderPosition+regionOffset;result.renderRotation=renderRotation;
    result.isMesh=(object.params.getSculptType() & LL_SCULPT_TYPE_MASK)==LL_SCULPT_TYPE_MESH;
    result.rawScale=object.scale;
    // LLDrawable::updateXform explicitly keeps unit scale in drawable world
    // transforms. Geometry applies object scale through its separate relative
    // transform; shared focus/planar/node consumers need this same rigid frame.
    result.renderMatrix.initAll(LLVector3(1.f,1.f,1.f),renderRotation,result.renderPosition);
    // getGLTFAssetToAgentTransform deliberately carries object scale, unlike
    // getRenderMatrix. Preserve its original scale/rotate/translate sequence.
    result.assetToAgentMatrix.initScale(object.scale);
    result.assetToAgentMatrix.rotate(renderRotation);
    result.assetToAgentMatrix.translate(result.renderPosition);
    result.officialBounds=LLBBox(officialPosition+regionOffset,officialRotation,object.scale*-.5f,object.scale*.5f);
    result.isAvatar=object.pcode==LL_PCODE_LEGACY_AVATAR;result.isRoot=object.parent==0;
    const Impl::Object* root=&object;visited.clear();
    while(root->parent)
    {
        if(!visited.insert(root->parent).second)return {};
        const auto parent=mImpl->objects.find(root->parent);if(parent==mImpl->objects.end())return {};
        if(parent->second.pcode==LL_PCODE_LEGACY_AVATAR)break;
        root=&parent->second;
    }
    result.isAnimated=root->pcode==LL_PCODE_VOLUME && (root->extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG);
    result.hasControlAvatar=bool(root->controlAvatar);
    if(result.isAvatar)
    {
        const auto avatar=mImpl->avatars.find(object.id);if(avatar==mImpl->avatars.end())return {};
        result.characterAgentPosition=avatar->second->character().getCharacterPosition()+regionOffset;
    }
    const std::weak_ptr<Impl> weak=mImpl;const auto live=guard(token);
    result.valid=[weak,live,token]
    {
        const auto owner=weak.lock();if(!owner || !live())return false;
        const auto found=owner->objects.find(token.local);return found!=owner->objects.end() && owner->linksCurrent(found->second);
    };
    return result;
}
S32 VSWorldScene::textureEntryCount(const ObjectHandle& token) const
{
    if(mImpl->retired || !current(token))return 0;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->textureEntryCount(token);
    return S32(mImpl->objects.at(token.local).faces.size());
}
LLUUID VSWorldScene::mediaTexture(const ObjectHandle& token,U32 face) const
{
    if(mImpl->retired || !current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->mediaTexture(token,face);
    const auto& object=mImpl->objects.at(token.local);
    return object.media && face<object.faces.size()?object.media->texture(face):LLUUID::null;
}
const LLMediaEntry* VSWorldScene::mediaEntry(const ObjectHandle& token,U32 face) const
{
    if(mImpl->retired || !current(token))return nullptr;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->mediaEntry(token,face);
    const auto& object=mImpl->objects.at(token.local);
    return object.media && face<object.faces.size()?object.media->entry(face):nullptr;
}
bool VSWorldScene::mediaDataReady(const ObjectHandle& token) const
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->mediaDataReady(token);
    const auto& object=mImpl->objects.at(token.local);
    const bool hasMedia=std::any_of(object.faces.begin(),object.faces.end(),[](const auto& face){return face.material.hasMedia();});
    return !hasMedia || (object.media && object.media->known(object.mediaVersion));
}
bool VSWorldScene::prepareMediaData(const ObjectHandle& token)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->prepareMediaData(token);
    auto& object=mImpl->objects.at(token.local);
    if(object.pcode!=LL_PCODE_VOLUME || object.faces.empty() || !mediaDataReady(token))return false;
    if(object.media && object.media->known(object.mediaVersion))return true;
    object.media=std::make_shared<VSObjectMedia>(mImpl->host,object.id,guard(token));
    const auto owner=object.media;const auto version=object.mediaVersion;
    return owner->sync(0,unsigned(object.faces.size()),LLSD(),false,true,false,version) && current(token);
}
bool VSWorldScene::syncMediaData(const ObjectHandle& token,U32 side,const LLSD& data,bool merge,bool ignoreAgent,bool updatePlayer)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->syncMediaData(token,side,data,merge,ignoreAgent,updatePlayer);
    auto& object=mImpl->objects.at(token.local);
    if(object.pcode!=LL_PCODE_VOLUME || side>=object.faces.size())return false;
    // A whole-object UPDATE must never replace another face's pending metadata.
    if(!mediaDataReady(token))return false;
    if(!object.media)object.media=std::make_shared<VSObjectMedia>(mImpl->host,object.id,guard(token));
    const auto owner=object.media;
    const auto admission=objectAdmission(token);
    return owner->sync(side,unsigned(object.faces.size()),data,merge,ignoreAgent,admission && admission->hud,object.mediaVersion,updatePlayer) && current(token);
}
bool VSWorldScene::sendMediaDataUpdate(const ObjectHandle& token)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->sendMediaDataUpdate(token);
    const auto owner=mImpl->objects.at(token.local).media;
    return owner && owner->sendUpdate() && current(token);
}
std::optional<std::array<LLVector3,8>> VSWorldScene::mediaFaceBounds(const ObjectHandle& token,U32 faceIndex,const LLVector3d& agentOrigin) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->mediaFaceBounds(token,faceIndex,agentOrigin);
    const auto& object=mImpl->objects.at(token.local);
    if(!mImpl->linksCurrent(object) || faceIndex>=object.admittedFaces.size())return {};
    const auto raw=object.admittedFaces[faceIndex].pickSurface;
    if(!raw || !raw->volume || raw->face<0 || raw->face>=raw->volume->getNumVolumeFaces())return {};
    const auto& geometry=raw->volume->getVolumeFace(raw->face);
    const LLVector3 offset=mImpl->hudObject(object)?LLVector3::zero:LLVector3(from_region_handle(mImpl->handle)-agentOrigin);
    const auto result=VSSurfaceCoordinates::volumeBounds(LLVector3(geometry.mExtents[0].getF32ptr()),
        LLVector3(geometry.mExtents[1].getF32ptr()),[&](LLVector3 point)
        {
            if(!raw->volumeGlobal)point.scaleVec(raw->scale);
            return point*raw->rotation+raw->position+offset;
        });
    return current(token)?std::optional<std::array<LLVector3,8>>(result):std::nullopt;
}
std::optional<std::array<std::array<float, 3>, 2>> VSWorldScene::selectionBounds(const ObjectHandle& token) const
{
    if (!current(token))
        return {};
    if (token.host != mImpl->host)
        return mNeighbours.at(token.host)->selectionBounds(token);
    std::optional<std::array<std::array<float, 3>, 2>> result;
    const auto&                                        object = mImpl->objects.at(token.local);
    const auto&                                        faces  = object.admittedFaces.empty() ? object.faces : object.admittedFaces;
    for (const auto& face : faces)
        if (face.mesh)
        {
            const auto bounds = VSWorldRenderer::bounds(face.mesh);
            if (!result)
                result = bounds;
            else
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    (*result)[0][axis] = std::min((*result)[0][axis], bounds[0][axis]);
                    (*result)[1][axis] = std::max((*result)[1][axis], bounds[1][axis]);
                }
        }
    return result;
}
std::function<bool()> VSWorldScene::guard(const ObjectHandle& token) const
{
    if (!current(token)) return []{return false;};
    const std::weak_ptr<Impl> weak=token.host==mImpl->host?mImpl:mNeighbours.at(token.host)->mImpl;
    return [weak,token]()
    {
        const auto owner=weak.lock();if (!owner || owner->retired || owner->host!=token.host || owner->incarnation!=token.region ||
            owner->budgets->account!=token.account || owner->budgets->accountGeneration!=token.accountGeneration) return false;
        const auto object=owner->objects.find(token.local);
        return object!=owner->objects.end() && object->second.id==token.id && object->second.generation==token.generation && Impl::kind(object->second)==token.kind;
    };
}
std::function<bool()> VSWorldScene::guard(const FaceHandle& token) const
{
    if (!current(token)) return []{return false;};
    const auto objectGuard=guard(token.object);
    const std::weak_ptr<Impl> weak=token.object.host==mImpl->host?mImpl:mNeighbours.at(token.object.host)->mImpl;
    return [weak,token,objectGuard]()
    {
        if (!objectGuard())
        {
            return false;
        }
        const auto owner = weak.lock();
        if (!owner)
        {
            return false;
        }
        const auto object=owner->objects.find(token.object.local);
        return object!=owner->objects.end() && owner->linksCurrent(object->second) &&
            object->second.revision==token.generation && token.face<object->second.faces.size();
    };
}
bool VSWorldScene::initializeSelf(const LLUUID& agent,U64 accountGeneration)
{
    if (agent.isNull())
    {
        return false;
    }
    mImpl->agent = agent;
    mImpl->budgets->account=agent;mImpl->budgets->accountGeneration=accountGeneration;
    return mImpl->avatar(agent,mImpl->handle)!=nullptr;
}
bool VSWorldScene::requestTaskInventory(LLMessageSystem& message,const ObjectHandle& token,const LLUUID& agent,const LLUUID& session)
{
    if (!current(token) || agent.isNull() || session.isNull()) return false;
    if (token.host!=mImpl->host) return mNeighbours.at(token.host)->requestTaskInventory(message,token,agent,session);
    message.newMessage("RequestTaskInventory");message.nextBlock("AgentData");
    message.addUUID("AgentID",agent);message.addUUID("SessionID",session);
    message.nextBlock("InventoryData");message.addU32("LocalID",token.local);
    if (message.sendReliable(token.host)<=0) return false;
    auto& object=mImpl->objects.at(token.local);object.taskRequest=token;
    object.taskDeadline=LLFrameTimer::getTotalSeconds()+30;return true;
}
LLSD VSWorldScene::taskInventory(const ObjectHandle& token) const
{
    if (!current(token)) return LLSD();
    if (token.host!=mImpl->host) return mNeighbours.at(token.host)->taskInventory(token);
    const auto& object=mImpl->objects.at(token.local);LLSD result;
    result["id"]=token.id;result["owner_generation"]=ll_sd_from_U64(token.generation);
    result["serial"]=S32(object.taskSerial);result["known"]=object.taskKnown;
    result["pending"]=bool(object.taskRequest);result["filename"]=object.taskFile;return result;
}
bool VSWorldScene::freezeObject(const ObjectHandle& token,bool selected)
{
    if(mImpl->retired || !current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->freezeObject(token,selected);
    mImpl->objects.at(token.local).motion.freeze(selected);return true;
}
bool VSWorldScene::setSelectionState(const ObjectHandle& token,bool selected)
{
    if(mImpl->retired || !current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->setSelectionState(token,selected);
    if(!selected && !setLegacyMaterialPreview(token,false))return false;
    mImpl->objects.at(token.local).motion.setSelectionState(selected);return true;
}
bool VSWorldScene::initializeSelectionMotion(const ObjectHandle& token)
{
    if(mImpl->retired || !current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->initializeSelectionMotion(token);
    auto& object=mImpl->objects.at(token.local);
    object.velocity=LLVector3::zero;object.acceleration=LLVector3::zero;
    object.motion.initializeSelection();return true;
}
bool VSWorldScene::initializeDeselectionMotion(const ObjectHandle& token)
{
    if(mImpl->retired || !current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->initializeDeselectionMotion(token);
    auto& object=mImpl->objects.at(token.local);
    object.angularVelocity=LLVector3::zero;object.velocity=LLVector3::zero;
    object.motion.initializeDeselection();return true;
}

bool VSWorldScene::freeze(const ObjectHandle& token,bool selected)
{
    if (!current(token)) return false;
    if (token.host!=mImpl->host) return mNeighbours.at(token.host)->freeze(token,selected);
    U32 root=token.local;std::set<U32> ancestors;
    for(;;)
    {
        if(!ancestors.insert(root).second || ancestors.size()>256)return false;
        const auto& object=mImpl->objects.at(root);
        if(object.parentRetired)return false;
        const auto parent=mImpl->objects.find(object.parent);
        if(parent!=mImpl->objects.end() && object.parentGeneration && object.parentGeneration!=parent->second.generation)return false;
        if(parent==mImpl->objects.end() || parent->second.pcode==LL_PCODE_LEGACY_AVATAR)break;
        root=parent->first;
    }
    std::set<U32> family{root};
    for(std::size_t pass=0;pass<256;++pass)
    {
        bool changed=false;
        for(auto& [local,object]:mImpl->objects)
            if(mImpl->linksCurrent(object) && family.count(object.parent) && family.insert(local).second)changed=true;
        if(!changed)break;
    }
    for(const auto local:family)mImpl->objects.at(local).motion.freeze(selected);
    return true;
}
void VSWorldScene::deleteTerrainPaintMap()
{
    gSavedSettings.setBOOL("LocalTerrainPaintEnabled",false);
    mImpl->resources.erase("terrain:local-paint");
}
bool VSWorldScene::createTerrainPaintMap()
{
    // Orthographic CPU transposition of the reference paint bake. Interpolate
    // the admitted terrain triangles' composition UVs, sample the same ramp,
    // and apply pbrTerrainBakeF's requested quantization. No GL owners are made.
    if(mImpl->ground.empty() || !mImpl->terrainRamp()) return false;
    const auto ramp=mImpl->resources.rawImage("terrain:alpha-ramp");
    if(!ramp || ramp->getComponents()!=4) return false;
    unsigned dimension=llclamp(gSavedSettings.getU32("TerrainPaintResolution"),16u,
        llclamp(gSavedSettings.getU32("RenderMaxTextureResolution"),512u,4096u));
    unsigned rounded=16;while(rounded<dimension) rounded*=2;dimension=rounded;
    auto working=mImpl->budgets->terrainBytes.reserve(std::size_t(dimension)*dimension*4);
    if(!working) return false;
    LLPointer<LLImageRaw> pixels=new LLImageRaw(dimension,dimension,4);pixels->clear(0,0,0,255);
    const float precision=float((1u<<llclamp(gSavedSettings.getU32("TerrainPaintBitDepth"),1u,8u))-1);
    auto alpha=[&](float u,float v)
    {
        const float x=std::clamp(u,0.f,1.f)*ramp->getWidth()-.5f;
        const float y=std::clamp(v,0.f,1.f)*ramp->getHeight()-.5f;
        const S32 x0=llfloor(x),y0=llfloor(y);
        auto sample=[&](S32 xx,S32 yy) {
            return float(ramp->getData()[(std::size_t(llclamp(yy,0,ramp->getHeight()-1))*ramp->getWidth()+
                llclamp(xx,0,ramp->getWidth()-1))*4+3])/255.f;
        };
        return VSTerrainMaterial::bilinear(sample(x0,y0),sample(x0,y0+1),sample(x0+1,y0),sample(x0+1,y0+1),x-x0,y-y0);
    };
    const float scaleX=float(dimension)/mImpl->width,scaleY=float(dimension)/mImpl->regionHeight;
    for(const auto& [key,patch]:mImpl->ground)
    {
        if(!patch.mesh) continue;
        const auto& vertices=VSWorldRenderer::geometryVertices(patch.mesh);
        const auto& indices=VSWorldRenderer::geometryIndices(patch.mesh);
        for(std::size_t index=0;index+2<indices.size();index+=3)
        {
            const auto& a=vertices[indices[index]];
            const auto& b=vertices[indices[index+1]];
            const auto& c=vertices[indices[index+2]];
            const float area=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
            if(std::abs(area)<1.e-12f) continue;
            const S32 left=llclamp(llfloor(std::min({a.x,b.x,c.x})*scaleX),0,S32(dimension)-1);
            const S32 right=llclamp(S32(std::ceil(std::max({a.x,b.x,c.x})*scaleX)),0,S32(dimension));
            const S32 bottom=llclamp(llfloor(std::min({a.y,b.y,c.y})*scaleY),0,S32(dimension)-1);
            const S32 top=llclamp(S32(std::ceil(std::max({a.y,b.y,c.y})*scaleY)),0,S32(dimension));
            for(S32 y=bottom;y<top;++y) for(S32 x=left;x<right;++x)
            {
                const float px=(x+.5f)/scaleX,py=(y+.5f)/scaleY;
                const float wa=((b.y-c.y)*(px-c.x)+(c.x-b.x)*(py-c.y))/area;
                const float wb=((c.y-a.y)*(px-c.x)+(a.x-c.x)*(py-c.y))/area;
                const float wc=1-wa-wb;
                if(wa<-.00001f || wb<-.00001f || wc<-.00001f) continue;
                const float u=wa*a.u1+wb*b.u1+wc*c.u1,v=wa*a.v1+wb*b.v1+wc*c.v1;
                const auto weights=VSTerrainMaterial::weights(alpha(u,v),alpha(u-2,v),alpha(u-1,v));
                auto* pixel=pixels->getData()+(std::size_t(y)*dimension+x)*4;
                for(unsigned channel=0;channel<3;++channel)
                    pixel[channel]=U8(std::round(std::round(weights[channel+1]*precision)/precision*255.f));
            }
        }
    }
    if(!mImpl->resources.publish("terrain:local-paint",*pixels)) return false;
    gSavedSettings.setBOOL("LocalTerrainPaintEnabled",true);
    return true;
}
void VSWorldScene::refreshInactiveImages()
{
    if(mImpl->terrainAssets) mImpl->terrainAssets->releaseImages();
    auto refresh=[&](Impl::Face& face)
    {
        if(!face.surface) return;
        for(unsigned channel=0;channel<VSMaterial::ChannelCount;++channel)
        {
            LLUUID id;std::copy_n(face.surface->textures[channel].begin(),UUID_BYTES,id.mData);
            if(id.isNull()) continue;
            const auto image=mImpl->resources.image("remote:"+vs_native_image_id(id).asString());
            if(!image) continue;
            if(!channel) face.publishedImage=image;else face.publishedChannels[channel-1]=image;
        }
    };
    for(auto& [local,object]:mImpl->objects)
    {
        for(auto& face:object.faces) refresh(face);
        for(auto& face:object.admittedFaces) refresh(face);
        if(object.gltf) object.gltf->refreshInactiveImages();
        if(object.pendingGLTF) object.pendingGLTF->refreshInactiveImages();
    }
    for(const auto& [host,region]:mNeighbours) region->refreshInactiveImages();
}
void VSWorldScene::derenderAnimatedObjects()
{
    mImpl->eraseAnimated();
    for (const auto& [host, region] : mNeighbours)
        region->derenderAnimatedObjects();
}
void VSWorldScene::replaceAsset(const LLUUID& id)
{
    mImpl->assets->replace(id);
    auto replace=[&](Impl& region)
    {
        for (auto& [local,object]:region.objects)
            if (object.params.getSculptID()==id)
            {
                if ((object.params.getSculptType() & LL_SCULPT_TYPE_MASK) == LL_SCULPT_TYPE_GLTF)
                {
                    const auto token = this->object(region.host, local);
                    if (token)
                    {
                        // Retain the admitted provider while the replacement
                        // fetch, decode and GPU publication are in progress.
                        object.pendingGLTF.reset();
                        object.pendingGLTF = std::make_shared<VSWorldGLTF>(id, region.renderer, region.resources,
                                                                           region.budgets->primitiveBytes, guard(*token));
                    }
                }
                object.asset.reset();object.volume=nullptr;object.dirty=true;
                object.revision=region.nextRevision++;
                for(auto& face:object.faces){ face.mesh.reset(); face.pickSurface.reset(); face.pickTextureMatrix.reset(); }
            }
    };
    replace(*mImpl);for (auto& [host,region]:mNeighbours) replace(*region->mImpl);
}
void VSWorldScene::configureRegion(const LLSD& h)
{
    auto& state=*mImpl;
    const float water=float(h["water_height"].asReal());
    state.waterKnown=h["water_height"].isReal() && std::isfinite(water) && std::abs(water)<=65536;
    state.waterHeight=water;state.water.reset();
    for (unsigned i=0;i<4;++i)
    {
        state.terrainTextures[i]=h["terrain_textures"][i].asUUID();
        const float start=float(h["terrain_starts"][i].asReal()),range=float(h["terrain_ranges"][i].asReal());
        state.terrainStarts[i]=std::isfinite(start)?start:0;state.terrainRanges[i]=std::isfinite(range)?range:1;
    }
    state.terrainAssets.reset();
    for(auto& [key,patch]:state.ground) patch.dirty=true;
}
bool VSWorldScene::receive(LLMessageSystem& msg,U64 regionHandle,const LLUUID& agent)
{
    if (mImpl->host.isOk() && msg.getSender()!=mImpl->host)
    {
        const auto region=mNeighbours.find(msg.getSender());
        return region!=mNeighbours.end() && region->second->receive(msg,region->second->mImpl->handle,agent);
    }
    if (!mImpl->host.isOk()) {mImpl->host=msg.getSender();mImpl->handle=regionHandle;mImpl->incarnation=1;}
    const std::string name=msg.getMessageName();auto& state=*mImpl;state.agent=agent;
    if(name=="GenericStreamingMessage")
    {
        LLGenericStreamingMessage message;message.unpack(&msg);
        if(message.mMethod!=LLGenericStreamingMessage::METHOD_GLTF_MATERIAL_OVERRIDE) return false;
        if(message.mData.size()>64*1024) {++state.rejected;return true;}
        LLSD data;std::istringstream input(message.mData);
        if(LLSDSerialize::fromNotation(data,input,S32(message.mData.size()))<=0 || !data.isMap() ||
            !data["id"].isInteger() || !data["te"].isArray() || !data["od"].isArray() ||
            data["te"].size()>45 || data["te"].size()!=data["od"].size()) {++state.rejected;return true;}
        const auto id=U32(data["id"].asInteger());
        if(const auto object=state.objects.find(id);object!=state.objects.end())
        {
            if (!state.applyMaterialOverrides(object->second, data)) ++state.rejected;
            else
            {
                ObjectHandle token{state.host, object->second.id, id, state.incarnation, object->second.generation};
                token.kind = state.kind(object->second); token.account = state.budgets->account;
                token.accountGeneration = state.budgets->accountGeneration;
                // Publication is complete before observers can reenter/reset.
                state.budgets->materialUpdates(token, -1);
            }
        }
        else if(state.pendingMaterialOverrides.size()<4096) state.pendingMaterialOverrides[id]=data;
        return true;
    }

    if(name=="ObjectUpdate" || name=="ObjectUpdateCompressed" || name=="ObjectUpdateCached" || name=="ImprovedTerseObjectUpdate")
    {
        if(msg.getNumberOfBlocks("RegionData")!=1 || msg.getSize("RegionData","RegionHandle")!=8)
        {++state.rejected;return true;}
        U64 declared;msg.getU64("RegionData","RegionHandle",declared);
        if(declared!=state.handle){++state.rejected;return true;}
    }
    if (name == "ViewerEffect")
    {
        const S32 count = msg.getNumberOfBlocks("Effect");
        if (count < 0 || count > 256)
        {
            ++state.rejected;
            return true;
        }
        for (S32 block = 0; block < count; ++block)
        {
            U8  type;
            F32 duration;
            msg.getU8("Effect", "Type", type, block);
            msg.getF32("Effect", "Duration", duration, block);
            if(type==LLHUDObject::LL_HUD_EFFECT_BEAM || type==LLHUDObject::LL_HUD_EFFECT_POINT ||
               type==LLHUDObject::LL_HUD_EFFECT_SPHERE || type==LLHUDObject::LL_HUD_EFFECT_SPIRAL || type==LLHUDObject::LL_HUD_EFFECT_EDIT)
            {
                LLHUDManager::processViewerEffectBlock(&msg,block);
                continue;
            }

            if (type == LLHUDObject::LL_HUD_EFFECT_RESET_SKELETON)
            {
                if (msg.getSize("Effect", block, "TypeData") != 17)
                {
                    ++state.rejected;
                    continue;
                }
                U8 data[17];
                msg.getBinaryData("Effect", "TypeData", data, 17, block, 17);
                LLUUID source, target;
                msg.getUUID("Effect", "AgentID", source, block);
                std::memcpy(target.mData, data, 16);
                if (target.isNull())
                    target = source;
                // Match the reference protocol: network effects may reset only
                // their own avatar, not another avatar or an animesh object.
                const auto avatar = state.avatars.find(source);
                if (source != target || avatar == state.avatars.end())
                {
                    ++state.rejected;
                    continue;
                }
                if (source != agent && avatar->second->resetSkeleton((data[16] & 1) != 0))
                    ++state.accepted;
                continue;
            }
            const bool point = type == LLHUDObject::LL_HUD_EFFECT_POINTAT;
            if (!point && type != LLHUDObject::LL_HUD_EFFECT_LOOKAT)
            {
                ++state.unsupported;
                continue;
            }
            const S32 size = msg.getSize("Effect", block, "TypeData");
            if (size != 57 || !std::isfinite(duration) || duration < 0)
            {
                ++state.rejected;
                continue;
            }
            U8 data[57];
            msg.getBinaryData("Effect", "TypeData", data, 57, block, 57);
            LLUUID source, target;
            std::memcpy(source.mData, data, 16);
            std::memcpy(target.mData, data + 16, 16);
            LLVector3d offset;
            std::memcpy(offset.mdV, data + 32, 24);
            const U8 subtype = data[56];
            if (!offset.isFinite() || subtype >= (point ? U32(POINTAT_NUM_TARGETS) : U32(LOOKAT_NUM_TARGETS)))
            {
                ++state.rejected;
                continue;
            }
            const auto avatar = state.avatars.find(source);
            if (avatar == state.avatars.end() || source == agent)
                continue;
            if (!subtype || subtype == (point ? U32(POINTAT_TARGET_CLEAR) : U32(LOOKAT_TARGET_CLEAR)))
            {
                avatar->second->attention(point, {}, 0);
                ++state.accepted;
                continue;
            }
            if (target.isNull())
                // Remote effects remain active until the sender clears them.
                // Reference unpackData does not arm the local kill timer.
                avatar->second->attention(point, [offset]() -> std::optional<LLVector3d> { return offset; }, 0);
            else
            {
                const auto token = object(target);
                if (!token)
                    continue;
                const auto valid      = guard(*token);
                const auto region     = token->host == mImpl->host ? mImpl : mNeighbours.at(token->host)->mImpl;
                const auto targetWeak = std::weak_ptr<Impl>(region);
                const U32  local      = token->local;
                avatar->second->attention(
                    point,
                    [valid, targetWeak, local, offset, subtype, point]() -> std::optional<LLVector3d>
                    {
                        const auto owner = targetWeak.lock();
                        if (!valid() || !owner)
                            return {};
                        const auto found = owner->objects.find(local);
                        if (found == owner->objects.end())
                            return {};
                        LLVector3     position;
                        LLQuaternion  rotation;
                        std::set<U32> visited;
                        if (!owner->transform(local, position, rotation, visited))
                            return {};
                        if (found->second.pcode == LL_PCODE_LEGACY_AVATAR)
                        {
                            const auto avatar = owner->avatars.find(found->second.id);
                            if (avatar == owner->avatars.end())
                                return {};
                            position = avatar->second->headPosition();
                            if (point)
                            {
                                if (auto* pelvis = avatar->second->character().getJoint("mPelvis"))
                                    rotation = pelvis->getWorldRotation();
                            }
                            else if (subtype == LOOKAT_TARGET_MOUSELOOK || subtype == LOOKAT_TARGET_FREELOOK)
                                rotation = LLQuaternion::DEFAULT;
                        }
                        return from_region_handle(owner->handle) + LLVector3d(position + LLVector3(offset) * rotation);
                    },
                    0);
            }
            ++state.accepted;
        }
        return true;
    }
    if (name=="ReplyTaskInventory")
    {
        LLUUID id;S16 serial;std::string file;
        msg.getUUID("InventoryData","TaskID",id);msg.getS16("InventoryData","Serial",serial);msg.getString("InventoryData","Filename",file);
        if (file.size()>256 || file.find_first_of("/\\")!=std::string::npos || file==".." || file==".") {++state.rejected;return true;}
        for (auto& [local,object]:state.objects)
        {
            if (object.id!=id || !object.taskRequest || !current(*object.taskRequest) || LLFrameTimer::getTotalSeconds()>object.taskDeadline) continue;
            if (object.taskKnown && U16(U16(serial)-object.taskSerial)>32767) {++state.rejected;return true;}
            object.taskSerial=U16(serial);object.taskKnown=true;object.taskFile=file;object.taskRequest.reset();++state.accepted;return true;
        }
        ++state.rejected;return true;
    }
    if (name == "AvatarAppearance" || name == "AvatarAnimation" || name == "ObjectAnimation")
    {
        LLUUID id;msg.getUUID("Sender","ID",id);
        for(auto& [local,object]:state.objects)
            if(object.id==id && Impl::kind(object)==VSWorldContracts::OwnerKind::ControlAvatar)
            {
                const S32 count=msg.getNumberOfBlocks("AnimationList");
                if ((name != "AvatarAnimation" && name != "ObjectAnimation") || count < 0 || count > 256)
                {
                    ++state.rejected;
                    return true;
                }
                std::map<LLUUID,S32> animations;
                for(S32 i=0;i<count;++i)
                {
                    LLUUID animation;S32 sequence;msg.getUUID("AnimationList","AnimID",animation,i);
                    msg.getS32("AnimationList","AnimSequenceID",sequence,i);if(animation.notNull())animations[animation]=sequence;
                }
                auto residency = state.budgets->primitiveBytes.reserve(std::size_t(count) * 128 + sizeof(std::map<LLUUID, S32>));
                if (!residency)
                {
                    ++state.rejected;
                    return true;
                }
                object.controlAnimations         = std::move(animations);
                object.controlAnimationResidency = std::move(residency);
                ++state.accepted;
                return true;
            }
        if (name == "ObjectAnimation")
        {
            const S32    count = msg.getNumberOfBlocks("AnimationList");
            const double now   = LLFrameTimer::getTotalSeconds();
            for (auto pending = state.pendingAnimations.begin(); pending != state.pendingAnimations.end();)
                if (pending->second.deadline < now)
                    pending = state.pendingAnimations.erase(pending);
                else
                    ++pending;
            if (id.isNull() || count < 0 || count > 256 || (state.pendingAnimations.size() >= 1024 && !state.pendingAnimations.count(id)))
            {
                ++state.rejected;
                return true;
            }
            auto residency = state.budgets->primitiveBytes.reserve(sizeof(Impl::AnimationState) + std::size_t(count) * 128);
            if (!residency)
            {
                ++state.rejected;
                return true;
            }
            Impl::AnimationState pending;
            pending.deadline  = now + 60;
            pending.residency = std::move(residency);
            for (S32 index = 0; index < count; ++index)
            {
                LLUUID animation;
                S32    sequence;
                msg.getUUID("AnimationList", "AnimID", animation, index);
                msg.getS32("AnimationList", "AnimSequenceID", sequence, index);
                if (animation.notNull())
                    pending.animations[animation] = sequence;
            }
            state.pendingAnimations[id] = std::move(pending);
            ++state.accepted;
            return true;
        }
        auto* avatar=state.avatar(id,regionHandle);
        if (!avatar || !(name=="AvatarAppearance"?avatar->appearance(msg):avatar->animations(msg))) ++state.rejected;else ++state.accepted;
        return true;
    }
    if (name=="LayerData")
    {
        U8 type;msg.getU8("LayerID","Type",type);
        if (type != 'L' && type != 'M' && type != '7' && type != '9')
            return true;
        const S32 length=msg.getSize("LayerData","Data");
        if (length<5 || length>65535) {++state.rejected;return true;}
        std::vector<U8> bytes(length);msg.getBinaryData("LayerData","Data",bytes.data(),length,0,length);
        if ((type == '7' || type == '9') ? state.wind(bytes, type) : state.terrain(bytes, type))
            ++state.accepted;
        else
            ++state.rejected;
        return true;
    }
    if (name=="KillObject")
    {
        for (S32 i=0;i<msg.getNumberOfBlocks("ObjectData");++i)
        {
            U32 id;msg.getU32("ObjectData","ID",id,i);state.eraseObject(id);
        }
        return true;
    }
    if (name!="ObjectUpdate" && name!="ObjectUpdateCompressed" && name!="ObjectUpdateCached" && name!="ImprovedTerseObjectUpdate") return false;
    if (msg.getNumberOfBlocks("RegionData")!=1 || msg.getSize("RegionData","RegionHandle")!=8 ||
        msg.getNumberOfBlocks("ObjectData")<=0) { ++state.rejected;return true; }
    U64 handle;msg.getU64("RegionData","RegionHandle",handle);
    if (handle!=regionHandle) { ++state.rejected;return true; }
    U16 dilation;msg.getU16("RegionData","TimeDilation",dilation);state.timeDilation=float(dilation)/65535.f;
    for (S32 i=0;i<msg.getNumberOfBlocks("ObjectData");++i)
    {
        U32 id=0;
        if (name=="ObjectUpdateCached") { msg.getU32("ObjectData","ID",id,i);state.request(id);continue; }
        if (name=="ObjectUpdateCompressed" || name=="ImprovedTerseObjectUpdate")
        {
            const S32 size=msg.getSize("ObjectData",i,"Data");
            const S32 minimumSize=name=="ObjectUpdateCompressed" ? 20 : 44;
            if (size<minimumSize || size>4096) { ++state.rejected;continue; }
            std::vector<U8> bytes(size);msg.getBinaryData("ObjectData","Data",bytes.data(),size,i,size);
            if (name=="ObjectUpdateCompressed")
            {
                U32 flags;msg.getU32("ObjectData","UpdateFlags",flags,i);
                if(!state.compressed(bytes,flags))++state.rejected;
                continue;
            }
            // Improved terse: ID, state, avatar flag, position, quantized
            // velocity/acceleration, quaternion, angular velocity.
            if (size<(bytes[5]?60:44)) { ++state.rejected;continue; }
            id=littleU32(bytes.data());auto found=state.objects.find(id);
            if (found==state.objects.end()) { state.request(id);continue; }
            if (bool(bytes[5])!=(found->second.pcode==LL_PCODE_LEGACY_AVATAR)) { ++state.rejected;continue; }
            const unsigned offset=bytes[5]?16:0;const auto position=vectorAt(bytes.data()+6+offset);LLQuaternion rotation;
            LLVector3 velocity,acceleration,angularVelocity;
            for (unsigned c=0;c<3;++c)
            {
                const unsigned v=18+offset+c*2,a=38+offset+c*2;
                velocity.mV[c]=float(U16(bytes[v]) | U16(bytes[v+1])<<8)/65535.f*256.f-128.f;
                const unsigned accelerationOffset=24+offset+c*2;
                acceleration.mV[c]=float(U16(bytes[accelerationOffset]) | U16(bytes[accelerationOffset+1])<<8)/65535.f*128.f-64.f;
                angularVelocity.mV[c]=float(U16(bytes[a]) | U16(bytes[a+1])<<8)/65535.f*128.f-64.f;
            }
            for (unsigned c=0;c<4;++c)
            {
                const unsigned p=30+offset+c*2;const U16 q=U16(bytes[p]) | U16(bytes[p+1])<<8;
                rotation.mQ[c]=float(q)/65535.f*2.f-1.f;
            }
            if (!finiteVector(position) || !rotation.isFinite()) { ++state.rejected;continue; }
            rotation.normalize();found->second.position=position;found->second.rotation=rotation;
            found->second.velocity=velocity;found->second.angularVelocity=angularVelocity;
            found->second.acceleration=acceleration;
            found->second.motion.accept(position,rotation,velocity,acceleration,angularVelocity,LLFrameTimer::getTotalSeconds());
            found->second.attachment=bytes[4];found->second.dirty=true;
            state.dirtyChildren(id);++state.accepted;
            if (msg.getSize("ObjectData",i,"TextureEntry")>0) state.request(id);
            continue;
        }
        msg.getU32("ObjectData","ID",id,i);
        U8 pcode,attachment;msg.getU8("ObjectData","PCode",pcode,i);msg.getU8("ObjectData","State",attachment,i);
        if (!id) { ++state.rejected;continue; }
        const S32 length=msg.getSize("ObjectData",i,"ObjectData");
        if ((pcode==LL_PCODE_LEGACY_AVATAR && length!=76 && length!=140) || (pcode!=LL_PCODE_LEGACY_AVATAR && length!=60 && length!=124)) { ++state.rejected;continue; }
        std::vector<U8> bytes(length);msg.getBinaryData("ObjectData","ObjectData",bytes.data(),length,i,length);
        const unsigned offset=pcode==LL_PCODE_LEGACY_AVATAR?16:0;
        Impl::Object object;object.pcode=pcode;object.attachment=attachment;object.position=vectorAt(bytes.data()+offset);
        object.velocity=vectorAt(bytes.data()+offset+12);object.acceleration=vectorAt(bytes.data()+offset+24);object.angularVelocity=vectorAt(bytes.data()+offset+48);
        object.rotation.unpackFromVector3(vectorAt(bytes.data()+offset+36));
        msg.getVector3("ObjectData","Scale",object.scale,i);msg.getUUID("ObjectData","FullID",object.id,i);
        msg.getU32("ObjectData","ParentID",object.parent,i);
        msg.getU32("ObjectData","UpdateFlags",object.updateFlags,i);
        msg.getU8("ObjectData","Material",object.material,i);
        msg.getU8("ObjectData","ClickAction",object.clickAction,i);
        msg.getUUID("ObjectData","Sound",object.soundAsset,i);object.soundSource=object.soundAsset.notNull();
        msg.getF32("ObjectData","Gain",object.soundGain,i);
        msg.getF32("ObjectData","Radius",object.soundRadius,i);
        msg.getU8("ObjectData","Flags",object.soundFlags,i);
        // The full message's OwnerID is only guaranteed for sounding objects;
        // a null value is unknown ownership, never evidence of public/edit rights.
        msg.getUUID("ObjectData","OwnerID",object.reportedOwner,i);
        object.reportedOwnerKnown=object.reportedOwner.notNull();
        const S32 mediaSize=msg.getSize("ObjectData",i,"MediaURL");
        if(mediaSize<0 || mediaSize>4096) {++state.rejected;continue;}
        if(mediaSize) msg.getString("ObjectData","MediaURL",object.mediaVersion,i);
        const S32 namesSize = msg.getSize("ObjectData", i, "NameValue");
        if (namesSize < 0 || namesSize > 4096)
        {
            ++state.rejected;
            continue;
        }
        if (namesSize)
        {
            std::string values;
            msg.getString("ObjectData", "NameValue", values, i);
            object.inventoryItem = attachmentItem(values);object.nameValues=std::move(values);object.nameValuesPresent=true;
        }
        if (!finiteVector(object.position) || !finiteVector(object.scale) || !finiteVector(object.velocity) ||
            !finiteVector(object.acceleration) || !finiteVector(object.angularVelocity) || !object.rotation.isFinite() ||
            object.scale.mV[0]<=0 || object.scale.mV[1]<=0 || object.scale.mV[2]<=0 || object.scale.lengthSquared()>1.e8f ||
            object.parent==id || object.id.isNull()) { ++state.rejected;continue; }
        if (pcode==LL_PCODE_LEGACY_AVATAR)
        {
            if (!finiteVector(object.velocity) || !finiteVector(object.angularVelocity) || !state.avatar(object.id,regionHandle)) {++state.rejected;continue;}
            if (state.replace(id,std::move(object))) ++state.accepted;else ++state.rejected;
            continue;
        }
        if(pcode!=LL_PCODE_VOLUME)
        {
            object.renderable = pcode == LL_PCODE_LEGACY_TREE || pcode == LL_PCODE_TREE_NEW || pcode == LL_PCODE_LEGACY_GRASS;
            if (object.renderable)
            {
                object.faces.resize(1);
                object.species    = attachment;
                const S32 size    = msg.getSize("ObjectData", i, "Data");
                object.attachment = 0;
                if (size > 0 && size <= 4096)
                {
                    U8 data[4096];
                    msg.getBinaryData("ObjectData", "Data", data, size, i, 4096);
                    object.species = data[0];
                }
            }
            else
                ++state.unsupported;
            if (state.replace(id, std::move(object)))
                ++state.accepted;
            else
                ++state.rejected;
            continue;
        }
        const S32 extraSize=msg.getSize("ObjectData",i,"ExtraParams");
        if (extraSize < 0 || extraSize > 4096)
        {
            ++state.rejected;
            continue;
        }
        std::vector<U8> extra(extraSize);
        if (extraSize) msg.getBinaryData("ObjectData","ExtraParams",extra.data(),extraSize,i,extraSize);
        LLVolumeParams params;
        const char* objectBlock=LLMessageStringTable::getInstance()->getString("ObjectData");
        if (!LLVolumeMessage::unpackVolumeParams(&params,&msg,objectBlock,i)) { ++state.rejected;continue; }
        if (!ownerExtras(extra,object.extendedMeshFlags,&object.reflectionProbe)) {++state.rejected;continue;}
        object.edit=std::make_shared<VSObjectEditParameters>();
        if(!object.edit->unpack(extra)){++state.rejected;continue;}
        bool materialDataValid=true;
        for (std::size_t cursor = extra.empty() ? 0 : 1; cursor + 6 <= extra.size();)
        {
            const U16 type   = U16(extra[cursor]) | U16(extra[cursor + 1]) << 8;
            const U32 length = littleU32(extra.data() + cursor + 2);
            cursor += 6;
            if (length > extra.size() - cursor)
                break;
            if(type==LLNetworkData::PARAMS_RENDER_MATERIAL && !renderMaterialData(extra.data()+cursor,length,object.renderMaterials))
                materialDataValid=false;
            if (type == LLNetworkData::PARAMS_LIGHT && length == 16)
                object.light = extra[cursor + 3] != 0;
            cursor += length;
        }
        if(!materialDataValid) {++state.rejected;continue;}
        object.particleFullMessage=true;
        const S32 particleSize = msg.getSize("ObjectData", i, "PSBlock");
        if (particleSize < 0 || particleSize > 4096)
        {
            ++state.rejected;
            continue;
        }
        if (particleSize)
        {
            U8 data[4096];
            msg.getBinaryData("ObjectData", "PSBlock", data, particleSize, i, 4096);
            if (particleSize != 86 &&
                (particleSize < 94 || particleSize > 98 || littleU32(data) != 68 || littleU32(data + 72) != U32(particleSize - 76)))
            {
                ++state.rejected;
                continue;
            }
            LLDataPackerBinaryBuffer particleData(data, particleSize);
            LLPartSysData            parameters;
            if (!(particleSize == 86 ? parameters.unpackLegacy(particleData) : parameters.unpack(particleData)))
            {
                ++state.rejected;
                continue;
            }
            if (parameters.mCRC)
                object.particles = parameters;
        }
        const S32 animationSize = msg.getSize("ObjectData", i, "TextureAnim");
        if (animationSize < 0 || animationSize > 4096)
        {
            ++state.rejected;
            continue;
        }
        if (animationSize)
        {
            U8 data[4096];
            msg.getBinaryData("ObjectData", "TextureAnim", data, animationSize, i, 4096);
            object.animation=textureAnimationData(data,U32(animationSize));
            if(!object.animation) {++state.rejected;continue;}
            object.textureAnimation = (object.animation->mMode & LLTextureAnim::ON) != 0;
        }
        object.renderable = volumeExtras(extra, params, object.flexible);
        if (!object.renderable)
            ++state.unsupported;
        object.params=params;
        if (params.getSculptID().isNull() && object.renderable) object.volume=new LLVolume(params,2.f);
        const S32 faces=object.volume ? object.volume->getNumVolumeFaces() : 8;
        if (faces<=0 || faces>45 || (state.objects.size()>=maxObjects && !state.objects.count(id))) { ++state.rejected;continue; }
        LLPrimitive material;material.setNumTEs(U8(faces));
        if (material.unpackTEMessage(&msg,objectBlock,i)==TEM_INVALID) { ++state.rejected;continue; }
        for (S32 f=0;f<faces;++f) object.faces.push_back({{},*material.getTE(U8(f))});
        if (state.replace(id,std::move(object))) ++state.accepted;else ++state.rejected;
    }
    return true;
}
void VSWorldScene::requestUpdates(LLMessageSystem& msg,const LLHost& host,const LLUUID& agent,const LLUUID& session)
{
    for (auto& [peer,region]:mNeighbours)
        if (msg.checkCircuitAlive(peer)) region->requestUpdates(msg,peer,agent,session);
    const double now=LLFrameTimer::getTotalSeconds();unsigned count=0;
    for(auto& [local,object]:mImpl->objects)
        if(object.taskRequest && (now>object.taskDeadline || !current(*object.taskRequest))) object.taskRequest.reset();
    for(auto request=mImpl->pending.begin();request!=mImpl->pending.end();)
        if(request->second.attempts>=3 && now>=request->second.deadline)request=mImpl->pending.erase(request);
        else ++request;
    if (now<mImpl->nextRequest) return;
    for (auto& [id,request]:mImpl->pending)
    {
        if (now<request.deadline || request.attempts>=3) continue;
        if (!count) { msg.newMessage("RequestMultipleObjects");msg.nextBlock("AgentData");msg.addUUID("AgentID",agent);msg.addUUID("SessionID",session); }
        msg.nextBlock("ObjectData");msg.addU8("CacheMissType",LLViewerRegion::CACHE_MISS_TYPE_TOTAL);
        msg.addU32("ID",id);request.deadline=now+5;++request.attempts;if (++count==64) break;
    }
    if (count) { msg.sendReliable(host);mImpl->nextRequest=now+.2; }
}
void VSWorldScene::publishGroundFrame(const LLVector3& eye)
{
    if (!mRoot)
        return;
    // Height seams and LOD joins are one publication transaction across
    // all current region owners. A refused patch keeps the complete last
    // admitted terrain frame, including adjacent region boundaries.
    std::vector<std::shared_ptr<Impl>> regions{ mImpl };
    for (const auto& [host, region] : mNeighbours)
        if (!region->mImpl->retired)
            regions.push_back(region->mImpl);
    bool admitted = true;
    for (const auto& region : regions)
    {
        const auto offset = VSWorldContracts::regionOffset(mImpl->handle, region->handle);
        if (!region->prepareGround(eye - LLVector3(offset.data())))
        {
            admitted = false;
            break;
        }
    }
    for (const auto& region : regions)
    {
        if (admitted && region->groundCandidate)
            for (auto& [key, publication] : *region->groundCandidate)
            {
                auto& ground  = region->ground.at(key);
                ground.mesh   = std::move(publication.mesh);
                ground.stride = publication.stride;
                ground.north  = publication.north;
                ground.east   = publication.east;
                ground.dirty  = false;
            }
        region->groundCandidate.reset();
        region->groundCandidateResidency.reset();
    }
}
namespace
{
bool appearanceRiggedVertices(VSWorldAvatar& avatar, const LLVolumeFace& geometry, const LLSD& skin,
    const LLTextureEntry& texture, std::vector<VSWorldRenderer::Vertex>& vertices)
{
    if (!avatar.skinRiggedFace(geometry, skin, vertices)) return false;
    if (texture.getTexGen() == LLTextureEntry::TEX_GEN_PLANAR)
    {
        if (!geometry.mCenter) return false;
        for (S32 index = 0; index < geometry.mNumVertices; ++index)
        {
            LLVector2 tc = geometry.mTexCoords[index];
            VSSurfaceCoordinates::planarProjection(tc, geometry.mNormals[index], *geometry.mCenter, geometry.mPositions[index]);
            vertices[index].u = tc.mV[0];
            vertices[index].v = 1.f - tc.mV[1];
        }
    }
    return true;
}
}
std::vector<VSWorldRenderer::Packet> VSWorldScene::packets(const LLVector3& eye, float farClip, unsigned viewportHeight,
                                                           unsigned viewportWidth)
{
    std::vector<VSWorldRenderer::Packet> packets;
    mImpl->assets->pump();
    publishGroundFrame(eye);
    VSWorldMotion::Policy motionPolicy;
    motionPolicy.interpolate=gSavedSettings.getBOOL("VelocityInterpolate");
    motionPolicy.maximum=gSavedSettings.getF32("InterpolationTime");motionPolicy.phaseOut=gSavedSettings.getF32("InterpolationPhaseOut");
    if (!std::isfinite(motionPolicy.maximum) || !std::isfinite(motionPolicy.phaseOut) || motionPolicy.maximum<0 ||
        motionPolicy.phaseOut<0 || motionPolicy.phaseOut>motionPolicy.maximum) motionPolicy={motionPolicy.interpolate,1,3};
    const double frameTime=LLFrameTimer::getTotalSeconds();
    for (auto pending = mImpl->pendingAnimations.begin(); pending != mImpl->pendingAnimations.end();)
        if (pending->second.deadline < frameTime)
            pending = mImpl->pendingAnimations.erase(pending);
        else
            ++pending;
    bool healthy=true;
    if (gMessageSystem && mImpl->host.isOk())
    {
        const auto* circuit=gMessageSystem->mCircuitInfo.findCircuit(mImpl->host);
        healthy=circuit && circuit->isAlive() && !circuit->isBlocked() &&
            LLMessageSystem::getMessageTimeSeconds()-circuit->getLastPacketInTime()<=F64Seconds(motionPolicy.phaseOut);
    }
    static LLCachedControl<bool> freezeTime(gSavedSettings, "FreezeTime", false);
    static LLCachedControl<bool> animateTextures(gSavedSettings, "AnimateTextures", true);
    for (auto& [local,object]:mImpl->objects)
        if ((!freezeTime || object.pcode == LL_PCODE_LEGACY_AVATAR) &&
            object.motion.update(frameTime,mImpl->timeDilation,healthy,object.attachment!=0,motionPolicy))
        { object.dirty=true;mImpl->dirtyChildren(local); }
    mImpl->surfaces(packets,eye,viewportHeight);
    // Evaluate avatar roots and bones before resolving attached primitive transforms.
    for (const auto& [id,o]:mImpl->objects) if (o.pcode==LL_PCODE_LEGACY_AVATAR)
    {
        LLVector3 p;LLQuaternion q;std::set<U32> visited;
        const auto avatar=mImpl->avatars.find(o.id);
        if (avatar!=mImpl->avatars.end() && mImpl->transform(id,p,q,visited))
        {
            avatar->second->sitting(o.parent != 0);
            avatar->second->transform(p, q, o.velocity, o.angularVelocity, o.scale.mV[VZ]);
        }
    }
    // Use previous admitted animated extents, as the reference does. A resident's
    // rigged LOD follows its body/linkset rather than each attachment's scale.
    using Bounds = std::array<std::array<float, 3>, 2>;
    std::map<VSWorldAvatar*, Bounds> skeletonBounds;
    for (const auto& [local, object] : mImpl->objects)
    {
        if (!mImpl->linksCurrent(object))
            continue;
        if (object.pcode != LL_PCODE_LEGACY_AVATAR && (!object.asset || !object.asset->skin.isMap()))
            continue;
        auto* owner = mImpl->skeletonOwner(object);
        if (!owner || mImpl->hudObject(object))
            continue;
        const auto& faces = object.admittedFaces.empty() ? object.faces : object.admittedFaces;
        for (const auto& face : faces)
            if (face.mesh)
            {
                const auto bounds      = VSWorldRenderer::bounds(face.mesh);
                auto [found, inserted] = skeletonBounds.emplace(owner, bounds);
                if (!inserted)
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        found->second[0][axis] = std::min(found->second[0][axis], bounds[0][axis]);
                        found->second[1][axis] = std::max(found->second[1][axis], bounds[1][axis]);
                    }
            }
    }
    const auto volumeLOD = [&](const Impl::Object& object, const LLVector3& position, bool hud)
    {
        // Original Build mesh-LOD control applies to selected world meshes,
        // independently of the debug selection-LOD setting.
        const S32 editLOD=gSavedSettings.getS32("ShowSpecificLODInEdit");
        if(!hud && object.params.isMeshSculpt() && object.motion.selected() &&
           LLToolMgr::getInstance()->inEdit() && editLOD>=0)return llclamp(editLOD,0,3);
        if (!hud && object.asset && object.asset->skin.isMap())
            if (auto* owner = mImpl->skeletonOwner(object))
                if (const auto found = skeletonBounds.find(owner); found != skeletonBounds.end())
                {
                    const LLVector3 minimum(found->second[0].data()), maximum(found->second[1].data());
                    const bool      control = owner->isControlAvatar();
                    const F32       radius  = vs_volume_rigged_lod_radius((maximum - minimum).length(), control);
                    if (radius > 0)
                        return meshDetail((minimum + maximum) * .5f, LLVector3(radius, 0, 0), eye, false, nullptr,
                                          object.motion.selected());
                }
        return meshDetail(position, object.scale, eye, hud, object.volume ? &object.volume->mLODScaleBias : nullptr,
                          object.motion.selected());
    };
    // Admit completed mesh metadata before skeleton overrides and control-avatar
    // palettes are evaluated. A mesh must not publish its first skinned frame
    // with the previous skeleton and acquire its fitted overrides a frame later.
    for (auto& [local, object] : mImpl->objects)
    {
        if (!object.renderable || !object.params.isMeshSculpt() || !mImpl->linksCurrent(object))
            continue;
        LLVector3     position;
        LLQuaternion  rotation;
        std::set<U32> visited;
        if (!mImpl->transform(local, position, rotation, visited))
            continue;
        bool                hud   = false;
        const Impl::Object* child = &object;
        visited.clear();
        while (child->parent && visited.insert(child->parent).second && visited.size() <= 256)
        {
            const auto parent = mImpl->objects.find(child->parent);
            if (parent == mImpl->objects.end())
                break;
            if (parent->second.pcode == LL_PCODE_LEGACY_AVATAR)
            {
                const auto     avatar = mImpl->avatars.find(parent->second.id);
                const unsigned state  = child->attachment;
                hud                   = avatar != mImpl->avatars.end() && avatar->second->hudAttachment(((state & 15) << 4) | (state >> 4));
                break;
            }
            child = &parent->second;
        }
        const float limit = llclamp(farClip, MIN_FAR_PLANE, MAX_FAR_PLANE) + object.scale.length() * 2.f;
        if (!hud && (position - eye).lengthSquared() > limit * limit)
            continue;
        const auto lod = volumeLOD(object, position, hud);
        mImpl->assets->request(object.params, lod);
        if (const auto asset = mImpl->assets->geometry(object.params.getSculptID(), lod); asset && asset != object.asset)
        {
            object.asset    = asset;
            object.assetLOD = asset->lod;
            object.volume   = asset->volume;
            object.dirty    = true;
            object.revision = mImpl->nextRevision++;
        }
    }
    struct AttachmentCost
    {
        F32              cost = 0, area = 0;
        bool             animated = false,complete=true;
        Impl::CostTextures textures;
    };
    std::map<LLUUID, std::map<U32, AttachmentCost>> costs;
    for (const auto& [local, object] : mImpl->objects)
    {
        if (object.pcode != LL_PCODE_VOLUME || !mImpl->linksCurrent(object))
            continue;
        U32                 parent = object.parent, root = local;
        const Impl::Object* rootObject = &object;
        std::set<U32>       visited;
        LLUUID              avatar;
        while (parent && visited.insert(parent).second && visited.size() <= 256)
        {
            const auto next = mImpl->objects.find(parent);
            if (next == mImpl->objects.end())
                break;
            if (next->second.pcode == LL_PCODE_LEGACY_AVATAR)
            {
                avatar = next->second.id;
                break;
            }
            root       = parent;
            rootObject = &next->second;
            parent     = next->second.parent;
        }
        if (avatar.isNull())
            continue;
        const auto owner = mImpl->avatars.find(avatar);
        if (owner == mImpl->avatars.end())
            continue;
        const unsigned state = rootObject->attachment;
        if (owner->second->hudAttachment(((state & 15) << 4) | (state >> 4)))
            continue;
        auto& cost    = costs[avatar][root];
        cost.animated = (rootObject->extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG) != 0;
        cost.cost += mImpl->renderCost(mImpl,object, cost.animated, local == root, cost.textures);
        cost.complete &= bool(object.volume);
        if (object.volume)
            cost.area += object.volume->getSurfaceArea() * std::max({ object.scale.mV[0], object.scale.mV[1], object.scale.mV[2] });
    }
    // Match the reference's previous visibility pass when adapting remote
    // avatar detail to render load; pending and distant owners do not count.
    const unsigned visibleAvatars = unsigned(std::count_if(mImpl->avatars.begin(), mImpl->avatars.end(),
                                                           [&](const auto& owner) { return owner.second->visibleFrom(eye, farClip); }));
    std::map<U32, LLUUID> bakedAttachmentRoots;
    std::map<LLUUID, U32> bakedAttachmentMasks;
    for (const auto& [local, attached] : mImpl->objects)
    {
        if (!attached.attachment || !mImpl->linksCurrent(attached)) continue;
        const auto parent = mImpl->objects.find(attached.parent);
        if (parent != mImpl->objects.end() && parent->second.pcode == LL_PCODE_LEGACY_AVATAR)
            bakedAttachmentRoots.emplace(local, parent->second.id);
    }
    for (const auto& [local, attached] : mImpl->objects)
    {
        if (!mImpl->linksCurrent(attached)) continue;
        auto owner = bakedAttachmentRoots.find(local);
        if (owner == bakedAttachmentRoots.end()) owner = bakedAttachmentRoots.find(attached.parent);
        if (owner == bakedAttachmentRoots.end()) continue;
        auto& mask = bakedAttachmentMasks[owner->second];
        for (const auto& face : attached.faces)
        {
            const auto baked = LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::assetIdToBakedTextureIndex(face.material.getID());
            if (baked >= 0 && baked < LLAvatarAppearanceDefines::BAKED_NUM_INDICES) mask |= 1u << baked;
        }
    }
    for (auto& [id,avatar]:mImpl->avatars)
    {
        Impl::Object* owner = nullptr;
        U32 ownerLocal=0;
        for (auto& [local, object] : mImpl->objects)
            if (object.id == id && object.pcode == LL_PCODE_LEGACY_AVATAR)
            {
                owner = &object;ownerLocal=local;
                break;
            }
        if (!owner)
            continue; // Early appearance does not invent a scene transform.
        F64       complexity = 0;
        F32       area       = 0;
        bool      animatedAttachments = false;
        const F32 maximum    = std::max(gSavedSettings.getF32("MaxAttachmentComplexity"), 1.e6f);
        for (auto& [root, cost] : costs[id])
        {
            for (const auto& texture : cost.textures)
                cost.cost += mImpl->textureCost(texture);
            if (cost.animated)
            {
                cost.cost += 1000;
                animatedAttachments = true;
            }
            auto& attachment=mImpl->objects.at(root);
            attachment.attachmentComplexity=cost.complete?std::optional<U32>(U32(llclamp(cost.cost,0.f,maximum))):std::nullopt;
            complexity += llclamp(cost.cost, 0.f, maximum);
            area += cost.area;
        }
        avatar->updateVisibility(U32(std::min(complexity, F64(std::numeric_limits<U32>::max()))), area, animatedAttachments);
        avatar->attachedBakedTextures(bakedAttachmentMasks[id]);
        if (owner->gltf)
            continue;
        std::map<LLUUID, LLSD> skins;
        for (const auto& [local, object] : mImpl->objects)
        {
            if (!object.asset || !object.asset->skin.isMap() || !mImpl->linksCurrent(object))
                continue;
            U32           parent = object.parent;
            std::set<U32> visited;
            while (parent && visited.insert(parent).second && visited.size() <= 256)
            {
                const auto found = mImpl->objects.find(parent);
                if (found == mImpl->objects.end())
                    break;
                if (found->second.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)
                    break;
                if (found->second.pcode == LL_PCODE_LEGACY_AVATAR)
                {
                    if (found->second.id == id)
                        skins[object.params.getSculptID()] = object.asset->skin;
                    break;
                }
                parent = found->second.parent;
            }
        }
        avatar->attachmentSkins(skins);
        const auto      session  = VSNativeSession::active();
        const LLVector3 at       = session ? session->cameraState().at : LLVector3::x_axis;
        const auto      position = avatar->character().getCharacterPosition();
        const LLVector3 wind =
            (mImpl->windAt(position * 4) * .25f + mImpl->windAt(position * 2) * .5f + mImpl->windAt(position)) * (2.f / 1.75f);
        avatar->environment(wind, mImpl->waterHeight, eye, at);
        const auto start = packets.size();
        avatar->append(packets, eye, farClip, viewportHeight, mImpl->timeDilation, viewportWidth, visibleAvatars);
        for (auto& face : owner->faces)
            { face.mesh.reset(); face.pickSurface.reset(); face.pickTextureMatrix.reset(); }
        for (auto index=start;index<packets.size();++index)
        { mImpl->packetOwner(packets[index],*owner,ownerLocal);
            const auto face                 = packets[index].faceIndex;
            if (face >= owner->faces.size())
                owner->faces.resize(face + 1);
            owner->faces[face].mesh = packets[index].mesh;
            VSWorldContracts::Material classification;
            classification.diffuse           = packets[index].color;
            classification.fullbright        = packets[index].fullbright;
            classification.doubleSided       = packets[index].doubleSided;
            classification.diffuseImageKnown = bool(packets[index].image);
            classification.alpha             = packets[index].alpha    ? VSWorldContracts::AlphaMode::Blend
                                               : packets[index].masked ? VSWorldContracts::AlphaMode::Mask
                                                                       : VSWorldContracts::AlphaMode::Opaque;
            if(packets[index].surface)
            {
                classification.sampler=packets[index].surface->samplers[0];
                classification.diffuseEncoding=packets[index].surface->pbr?VSWorldContracts::ColorEncoding::Linear:VSWorldContracts::ColorEncoding::EncodedSRGB;
            }
            classification.generation        = owner->revision;
            owner->faces[face].sceneMaterial = classification;
        }
        owner->admittedFaces = owner->faces;
        owner->pruneMetricsRigging();
    }
    // Create and pose every independent control owner before drawing any
    // linked member, regardless of simulator local-ID ordering.
    for (auto& [local, root] : mImpl->objects)
    {
        if (!(root.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG) || !mImpl->linksCurrent(root))
            continue;
        if (!root.controlAvatar)
        {
            auto slot = mImpl->budgets->avatars.reserve(1);
            if (!slot)
                continue;
            VSWorldAvatar::initializeClass();
            const auto weak    = std::weak_ptr<Impl>(mImpl);
            root.controlAvatar = std::make_shared<VSWorldAvatar>(
                root.id, false, mImpl->handle, mImpl->renderer, mImpl->resources,
                [weak](const LLVector3& point, LLVector3& out, LLVector3& normal)
                {
                    const auto scene = weak.lock();
                    return scene && scene->groundAt(point, out, normal);
                },
                std::move(slot), true);
            root.controlAvatar->nameTagBudget(mImpl->budgets->terrainBytes);
        }
        std::map<LLUUID, LLSD>                  skins;
        std::optional<std::array<LLVector3, 2>> bounds;
        auto                                    includeBounds = [&](const Impl::Object& object)
        {
            const auto& faces = object.admittedFaces.empty() ? object.faces : object.admittedFaces;
            for (const auto& face : faces)
                if (face.mesh)
                {
                    const auto                     mesh = VSWorldRenderer::bounds(face.mesh);
                    const std::array<LLVector3, 2> candidate{ LLVector3(mesh[0].data()), LLVector3(mesh[1].data()) };
                    if (!bounds)
                        bounds = candidate;
                    else
                        for (unsigned axis = 0; axis < 3; ++axis)
                        {
                            (*bounds)[0].mV[axis] = std::min((*bounds)[0].mV[axis], candidate[0].mV[axis]);
                            (*bounds)[1].mV[axis] = std::max((*bounds)[1].mV[axis], candidate[1].mV[axis]);
                        }
                }
        };
        if (root.asset && root.asset->skin.isMap())
            skins[root.params.getSculptID()] = root.asset->skin;
        if (root.asset && root.asset->skin.isMap())
            includeBounds(root);
        for (const auto& [childLocal, child] : mImpl->objects)
        {
            if (!child.asset || !child.asset->skin.isMap() || !mImpl->linksCurrent(child))
                continue;
            U32           parent = child.parent;
            std::set<U32> visited;
            while (parent && visited.insert(parent).second && visited.size() <= 256)
            {
                if (parent == local)
                {
                    skins[child.params.getSculptID()] = child.asset->skin;
                    includeBounds(child);
                    break;
                }
                const auto next = mImpl->objects.find(parent);
                if (next == mImpl->objects.end())
                    break;
                if (next->second.pcode == LL_PCODE_LEGACY_AVATAR ||
                    (next->second.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG))
                    break;
                parent = next->second.parent;
            }
        }
        LLVector3     position;
        LLQuaternion  rotation;
        std::set<U32> visited;
        if (!mImpl->transform(local, position, rotation, visited))
            continue;
        root.controlAvatar->attachmentSkins(skins);
        root.controlAvatar->animations(root.controlAnimations);
        root.controlAvatar->controlConstraints(position, bounds ? bounds->data() : nullptr, isAttachment(root.id),
                                               root.asset ? root.asset->skin : LLSD());
        Impl::CostTextures controlTextures;
        U64              controlCost = mImpl->renderCost(mImpl,root, true, true, controlTextures);
        F32              controlArea =
            2 * (root.scale.mV[0] * root.scale.mV[1] + root.scale.mV[1] * root.scale.mV[2] + root.scale.mV[0] * root.scale.mV[2]);
        for (const auto& [childLocal, child] : mImpl->objects)
        {
            if (!mImpl->linksCurrent(child))
                continue;
            U32 parent = child.parent;
            for (unsigned depth = 0; parent && depth < 256; ++depth)
            {
                if (parent == local)
                {
                    controlCost += mImpl->renderCost(mImpl,child, true, false, controlTextures);
                    controlArea += 2 * (child.scale.mV[0] * child.scale.mV[1] + child.scale.mV[1] * child.scale.mV[2] +
                                        child.scale.mV[0] * child.scale.mV[2]);
                    break;
                }
                const auto ancestor = mImpl->objects.find(parent);
                if (ancestor == mImpl->objects.end())
                    break;
                if (ancestor->second.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)
                    break;
                parent = ancestor->second.parent;
            }
        }
        for (const auto& texture : controlTextures)
            controlCost += U32(mImpl->textureCost(texture));
        root.controlAvatar->updateVisibility(U32(std::min(controlCost, U64(std::numeric_limits<U32>::max()))), controlArea);
        root.controlAvatar->transform(position, rotation, root.velocity, root.angularVelocity);
    }
    for (auto& [id,o]:mImpl->objects)
    {
        if (!o.renderable)
        {
            o.releaseRenderingResources();
            continue;
        }
        if (o.pcode == LL_PCODE_LEGACY_AVATAR && !o.gltf && !o.pendingGLTF)
            continue;
        bool hud = false, attachmentVisible = true;
        {
            const Impl::Object* child = &o;
            std::set<U32>       visited;
            while (child->parent && visited.insert(child->parent).second && visited.size() <= 256)
            {
                const auto parent = mImpl->objects.find(child->parent);
                if (parent == mImpl->objects.end())
                    break;
                if (parent->second.pcode == LL_PCODE_LEGACY_AVATAR)
                {
                    const auto     avatar = mImpl->avatars.find(parent->second.id);
                    const unsigned state  = child->attachment;
                    hud = avatar != mImpl->avatars.end() && avatar->second->hudAttachment(((state & 15) << 4) | (state >> 4));
                    const auto session = VSNativeSession::active();
                    attachmentVisible  = avatar != mImpl->avatars.end() &&
                                         avatar->second->visibility() == VSWorldAvatar::Visibility::Normal &&
                                         avatar->second->attachmentVisible(((state & 15) << 4) | (state >> 4),
                                                                           session && session->cameraState().firstPerson);
                    break;
                }
                child = &parent->second;
            }
        }
        LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
        if (!attachmentVisible || (hud && !LLPipeline::sShowHUDAttachments))
        {
            o.releaseRenderingResources();
            continue;
        }
        if (!mImpl->transform(id,position,rotation,visited))
        {
            o.releaseRenderingResources();
            continue;
        }
        if (!o.geometryPoseKnown || o.geometryPosition != position || o.geometryRotation != rotation)
        {
            o.geometryPosition  = position;
            o.geometryRotation  = rotation;
            o.geometryPoseKnown = true;
            o.dirty             = true;
        }
        mImpl->updateSelectionIndex(id,o,position,rotation);
        const float limit=llclamp(farClip,MIN_FAR_PLANE,MAX_FAR_PLANE)+o.scale.length()*2.f;
        if (!hud && (position - eye).lengthSquared() > limit * limit)
        {
            o.releaseRenderingResources();
            continue;
        }
        if (o.pcode == LL_PCODE_LEGACY_TREE || o.pcode == LL_PCODE_TREE_NEW || o.pcode == LL_PCODE_LEGACY_GRASS)
        {
            const float distance=std::max((position-eye).length(),.01f);
            LLUUID      texture;
            S32         detail = 0;
            const F32   treeFactor = llclamp(gSavedSettings.getF32("RenderTreeLODFactor"), 0.f, 16.f);
            if (vs_vegetation_stopped(treeFactor))
            {
                o.releaseRenderingResources();
                o.vegetationLOD = -1;
                continue;
            }
            if (o.pcode == LL_PCODE_LEGACY_GRASS)
            {
                if (LLVOGrass::sSpeciesTable.empty())
                    LLVOGrass::initClass();
                auto species = LLVOGrass::sSpeciesTable.find(o.species);
                if (species == LLVOGrass::sSpeciesTable.end())
                    species = LLVOGrass::sSpeciesTable.begin();
                if (species == LLVOGrass::sSpeciesTable.end())
                    continue;
                texture          = species->second->mTextureID;
                const S32 blades = llclamp(lltrunc(o.scale.mV[VX] * o.scale.mV[VY] / distance * 5), 1, 32);
                detail           = o.vegetationLOD > 0 ? o.vegetationLOD : 32;
                if (blades >= (detail << 1))
                    while (detail < blades)
                        detail <<= 1;
                else if (blades <= (detail >> 1))
                    while (detail > blades)
                        detail >>= 1;
            }
            else
            {
                if (LLVOTree::sSpeciesTable.empty())
                    LLVOTree::initClass();
                auto species = LLVOTree::sSpeciesTable.find(o.species);
                if (species == LLVOTree::sSpeciesTable.end())
                    species = LLVOTree::sSpeciesTable.begin();
                if (species == LLVOTree::sSpeciesTable.end())
                    continue;
                texture          = species->second->mTextureID;
                const F32 range  = distance - o.scale.length() * .5f;
                const F32 angle        = (range < F_ALMOST_ZERO || hud
                                              ? 180.f
                                              : std::atan2(std::max({ o.scale.mV[0], o.scale.mV[1], o.scale.mV[2] }), range) * RAD_TO_DEG);
                const F32 volumeFactor = llclamp(gSavedSettings.getF32("RenderVolumeLODFactor"), 0.f, 16.f);
                detail                 = vs_tree_lod(angle, treeFactor, distance, o.scale.length(), species->second->mBillboardScale,
                                                     species->second->mBillboardRatio, 1.f - volumeFactor * .1f);
                if (detail == 4)
                {
                    o.releaseRenderingResources();
                    continue;
                }
            }
            auto& face = o.faces.front();
            const unsigned demand = unsigned(std::clamp(o.scale.length()*std::max(viewportHeight,1u)/distance,1.f,4096.f));
            vs_request_world_image(texture,demand);
            auto image = mImpl->resources.image("remote:" + vs_native_image_id(texture).asString());
            if (o.dirty || !face.mesh || o.vegetationLOD != detail)
            {
                auto publishVegetation = [&]
                {
                    if (!image)
                        return;
                    auto working = mImpl->budgets->primitiveBytes.reserve(8 * 1024 * 1024);
                    if (!working)
                        return;
                    VSVegetationMesh geometry;
                    if (o.pcode == LL_PCODE_LEGACY_GRASS)
                        geometry =
                            LLVOGrass::cpuMesh(o.species, position, o.scale, unsigned(detail),
                                               [&](const LLVector3& point)
                                               {
                                                   if (point.mV[0] < 0 || point.mV[1] < 0)
                                                       return position.mV[VZ];
                                                   return mImpl->height(unsigned(point.mV[0]), unsigned(point.mV[1]), position.mV[VZ]);
                                               });
                    else
                    {
                        auto species = LLVOTree::sSpeciesTable.find(o.species);
                        if (species == LLVOTree::sSpeciesTable.end())
                            species = LLVOTree::sSpeciesTable.begin();
                        VSTreeGeometry producer(*species->second);
                        geometry = producer.mesh(position, rotation, o.scale, detail);
                    }
                    std::vector<VSWorldRenderer::Vertex> vertices;
                    vertices.reserve(geometry.vertices.size());
                    for (std::size_t index = 0; index < geometry.vertices.size(); ++index)
                        vertices.push_back({ geometry.vertices[index].mV[0], geometry.vertices[index].mV[1], geometry.vertices[index].mV[2],
                                             geometry.normals[index].mV[0], geometry.normals[index].mV[1], geometry.normals[index].mV[2],
                                             geometry.uv[index].mV[0], geometry.uv[index].mV[1] });
                    std::vector<U32> indices(geometry.indices.begin(), geometry.indices.end());
                    if (mImpl->renderer.publishMesh(face.mesh, vertices, indices))
                    {
                        o.vegetationLOD = detail;
                        o.dirty         = false;
                        face.material.setID(texture);
                        face.publishedMaterial = face.material;
                        face.publishedImage    = image;
                        face.published         = true;
                    }
                };
                try
                {
                    publishVegetation();
                }
                catch (const std::invalid_argument&)
                {
                    ++mImpl->rejected;
                    o.dirty = false;
                }
                catch (const std::length_error&)
                {
                    ++mImpl->rejected;
                    o.dirty = false;
                }
            }
            // Texture discard/compression generations can change while the
            // admitted vegetation mesh and its LOD remain identical.
            if (face.mesh && image) face.publishedImage = std::move(image);
            if (face.mesh && face.published)
            {
                VSWorldRenderer::Packet packet;
                packet.mesh  = face.mesh;
                packet.image = face.publishedImage;
                if (!packet.image)
                    continue;
                packet.masked          = true;
                packet.hud             = hud;
                packet.distanceSquared = (position - eye).lengthSquared();
                mImpl->packetOwner(packet,o,id);
                packet.faceIndex        = 0;
                packets.push_back(std::move(packet));
            }
            continue;
        }
        if (o.pendingGLTF)
        {
            std::vector<VSWorldRenderer::Packet> candidate;
            o.pendingGLTF->append(candidate, position, rotation, o.scale, eye,viewportHeight);
            if (o.pendingGLTF->hasPublication())
                o.gltf = std::move(o.pendingGLTF);
        }
        if (o.gltf || (o.params.getSculptType() & LL_SCULPT_TYPE_MASK) == LL_SCULPT_TYPE_GLTF)
        {
            if (!o.gltf)
            {
                const auto weak     = std::weak_ptr<Impl>(mImpl);
                const auto identity = o.id, asset = o.params.getSculptID();
                const auto generation = o.generation;
                o.gltf = std::make_shared<VSWorldGLTF>(asset, mImpl->renderer, mImpl->resources, mImpl->budgets->primitiveBytes,
                                                       [weak, id, identity, asset, generation]
                                                       {
                                                           const auto scene = weak.lock();
                                                           if (!scene || scene->retired)
                                                               return false;
                                                           const auto object = scene->objects.find(id);
                                                           return object != scene->objects.end() && object->second.id == identity &&
                                                                  object->second.generation == generation &&
                                                                  object->second.params.getSculptID() == asset &&
                                                                  scene->linksCurrent(object->second);
                                                       });
            }
            const auto start = packets.size();
            o.gltf->append(packets, position, rotation, o.scale, eye,viewportHeight);
            // Selection uses the exact submitted topology, including removal
            // of faces when a replacement scene contains fewer primitives.
            o.faces.clear();
            for (auto packet = start; packet < packets.size(); ++packet)
            {
                auto& published = packets[packet];
                published.hud   = hud;
                published.fullbright |= hud;
                mImpl->packetOwner(published,o,id);
                if (published.faceIndex >= o.faces.size())
                    o.faces.resize(published.faceIndex + 1);
                o.faces[published.faceIndex].mesh = published.mesh;
                VSWorldContracts::Material classification;
                classification.diffuse                     = published.color;
                classification.fullbright                  = published.fullbright;
                classification.doubleSided                 = published.doubleSided;
                classification.diffuseImageKnown           = bool(published.image);
                classification.alpha                       = published.alpha    ? VSWorldContracts::AlphaMode::Blend
                                                             : published.masked ? VSWorldContracts::AlphaMode::Mask
                                                                                : VSWorldContracts::AlphaMode::Opaque;
                if(published.surface)
                {
                    classification.sampler=published.surface->samplers[0];
                    classification.diffuseEncoding=published.surface->pbr?VSWorldContracts::ColorEncoding::Linear:VSWorldContracts::ColorEncoding::EncodedSRGB;
                }
                classification.generation                  = o.revision;
                o.faces[published.faceIndex].sceneMaterial = classification;
            }
            o.admittedFaces = o.faces;
            o.pruneMetricsRigging();
            continue;
        }
        const float distance         = std::max((position - eye).length(), .01f);
        const S32   lod              = volumeLOD(o, position, hud);
        bool        producerDeferred = false;
        if (o.params.isMeshSculpt())
        {
            mImpl->assets->request(o.params,lod);
            auto asset=mImpl->assets->geometry(o.params.getSculptID(),lod);
            if (!asset) asset=o.asset; // Keep a previously decoded LOD while its replacement loads.
            if (!asset) continue;
            if (asset!=o.asset) {o.asset=asset;o.assetLOD=asset->lod;o.volume=asset->volume;o.dirty=true;o.revision=mImpl->nextRevision++;}
        }
        else if (o.params.getSculptID().notNull())
        {
            const auto key = "remote:" + o.params.getSculptID().asString();
            LLUI::getUIImageByID(o.params.getSculptID());
            const auto image      = mImpl->resources.image(key);
            const U64  generation = image ? mImpl->resources.generation(key) : 0;
            if (!generation)
                producerDeferred = true;
            if (generation && (generation != o.sculptGeneration || lod != o.assetLOD || !o.volume))
            {
                // Restore the decoder's bottom-row-first RGB map. The shared
                // tessellator handles sphere/torus/cylinder/plane and the
                // sculpt mirror/invert flags from LLVolumeParams.
                const auto extent  = VSUIRenderer::imageExtent(image);
                auto       working = mImpl->budgets->primitiveBytes.reserve(32 * 1024 * 1024);
                auto       raw     = (working && extent[0] <= 2048 && extent[1] <= 2048) ? mImpl->resources.rawImage(key) : nullptr;
                if (raw && raw->getWidth() > 0 && raw->getHeight() > 0 && raw->getWidth() <= 2048 && raw->getHeight() <= 2048)
                {
                    LLPointer<LLVolume> volume = new LLVolume(o.params, LLVolumeLODGroup::getVolumeScaleFromDetail(lod));
                    volume->sculpt(U16(raw->getWidth()), U16(raw->getHeight()), S8(raw->getComponents()), raw->getData(), 0, false);
                    producerDeferred = !mImpl->publishVolume(o, volume, lod, generation);
                }
                else
                    producerDeferred = true;
            }
        }
        else if (!o.flexible && (!o.volume || lod != LLVolumeLODGroup::getVolumeDetailFromScale(o.volume->getDetail())))
        {
            LLPointer<LLVolume> volume = new LLVolume(o.params, LLVolumeLODGroup::getVolumeScaleFromDetail(lod));
            producerDeferred           = !mImpl->publishVolume(o, volume, lod);
        }
        if (!o.volume) continue;
        if (o.flexible)
        {
            auto deform = [&]() -> bool
            {
                auto working = mImpl->budgets->primitiveBytes.reserve(8 * 1024 * 1024);
                if (!working)
                    return false;
                if (!o.flex)
                {
                    auto residency = mImpl->budgets->primitiveBytes.reserve(sizeof(Impl::Object::Flex));
                    if (!residency)
                        return false;
                    o.flex            = std::make_shared<Impl::Object::Flex>();
                    o.flex->residency = std::move(residency);
                }
                const S32  requested  = llclamp(S32(o.flexible->getSimulateLOD()), 0, S32(FLEXIBLE_OBJECT_MAX_SECTIONS));
                const S32  render     = llclamp(S32(12.f * o.scale.mV[VZ] / std::max(distance, .01f)), std::max(requested - 1, 0),
                                                S32(FLEXIBLE_OBJECT_MAX_SECTIONS));
                const S32  simulation = std::min(render, requested);
                const bool initialize = o.flex->simulation != simulation || o.flex->initialized < 0;
                if (initialize)
                {
                    vs_flexible_parameters(o.flex->sections.data(), o.flex->initialized, simulation, o.params.getPathParams(), o.scale,
                                           position, rotation);
                    o.flex->simulation = simulation;
                }
                o.flex->render = render;
                // Simulated state belongs to this object incarnation. Generate a
                // fresh bounded CPU volume; refused replacements preserve the
                // previous visible generation without mutating it in place.
                LLPointer<LLVolume> volume = new LLVolume(o.params, LLVolumeLODGroup::getVolumeScaleFromDetail(lod));
                const F32           dt     = o.flex->time ? F32(frameTime - o.flex->time) : 0;
                o.flex->time               = frameTime;
                vs_flexible_step(
                    o.flex->sections.data(), simulation, render, *volume, *o.flexible, position, rotation, o.scale, dt, distance,
                    [&](const LLVector3& p) { return mImpl->windAt(p); }, false, o.flex->lastRotation);
                std::array<float, ((1 << FLEXIBLE_OBJECT_MAX_SECTIONS) + 1) * 15 + 1> signature{};
                std::size_t                                                           size = 0;
                signature[size++]                                                          = F32(lod);
                for (S32 index = 0; index < volume->getPath().getPathLength(); ++index)
                {
                    const auto& point = volume->getPath().mPath[index];
                    for (unsigned component = 0; component < 3; ++component)
                        signature[size++] = point.mPos.getF32ptr()[component];
                    for (unsigned component = 0; component < 2; ++component)
                        signature[size++] = point.mScale.getF32ptr()[component];
                    const auto* matrix = point.mRot.getF32ptr();
                    for (unsigned row = 0; row < 3; ++row)
                        for (unsigned column = 0; column < 3; ++column)
                            signature[size++] = matrix[row * 4 + column];
                    signature[size++] = point.mTexT;
                }
                if (o.assetLOD != lod || size != o.flex->pathSignatureSize ||
                    !std::equal(signature.begin(), signature.begin() + size, o.flex->pathSignature.begin()))
                {
                    volume->setDirty();
                    volume->regen();
                    if (mImpl->publishVolume(o, volume, lod))
                    {
                        o.flex->pathSignature     = signature;
                        o.flex->pathSignatureSize = size;
                    }
                    else
                        return false;
                }
                return true;
            };
            producerDeferred = !deform();
        }
        VSWorldAvatar* riggedOwner=nullptr;
        if (o.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)
        {
            if (!o.controlAvatar)
                continue;
            riggedOwner = o.controlAvatar.get();
        }
        if (!riggedOwner && o.asset && o.asset->skin.isMap() && o.parent)
        {
            U32 parent=o.parent;std::set<U32> visited;
            while (parent && visited.insert(parent).second && visited.size()<=256)
            {
                const auto object=mImpl->objects.find(parent);if (object==mImpl->objects.end()) break;
                if (object->second.pcode==LL_PCODE_LEGACY_AVATAR)
                {const auto owner=mImpl->avatars.find(object->second.id);if (owner!=mImpl->avatars.end()) riggedOwner=owner->second.get();break;}
                if (object->second.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)
                {
                    riggedOwner = object->second.controlAvatar.get();
                    break;
                }
                parent=object->second.parent;
            }
        }
        if (!riggedOwner && o.asset && o.asset->skin.isMap())
        {
            U32           parent = o.parent;
            std::set<U32> visited;
            bool          pendingControl = false;
            while (parent && visited.insert(parent).second && visited.size() <= 256)
            {
                const auto ancestor = mImpl->objects.find(parent);
                if (ancestor == mImpl->objects.end())
                    break;
                if (ancestor->second.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)
                {
                    pendingControl = true;
                    break;
                }
                parent = ancestor->second.parent;
            }
            if (pendingControl)
                continue;
        }
        if (riggedOwner && riggedOwner->visibility() == VSWorldAvatar::Visibility::Invisible)
            continue;
        if(!freezeTime && animateTextures && o.animation && o.textureAnimation)
        {
            float offS=0,offT=0,scaleS=1,scaleT=1,rotation=0;
            const auto changed=o.animation->animateTextures(offS,offT,scaleS,scaleT,rotation);
            if(changed) o.textureAnimMode=changed|o.animation->mMode;
        }
        if(!mImpl->materialAssets) mImpl->materialAssets=std::make_unique<VSMaterialAssets>(mImpl->host);
        bool      uploadDeferred = producerDeferred;
        const S32 count=std::min(S32(o.faces.size()),o.volume->getNumVolumeFaces());
        auto      candidates     = o.faces;
        const bool hasMedia=std::any_of(o.faces.begin(),o.faces.end(),[](const auto& face){return face.material.hasMedia();});
        if(hasMedia)
        {
            if(!o.media)
            {
                const auto token=this->object(mImpl->host,id);
                if(token) o.media=std::make_shared<VSObjectMedia>(mImpl->host,o.id,guard(*token));
            }
            const double distance=std::max(double((position-eye).length()),1.0);
            const double radius=double(o.scale.length())*.5;
            const double projected=radius*std::max(viewportHeight,1u)/distance;
            if(o.media) o.media->update(o.mediaVersion,projected*projected,distance);
        }
        else o.media.reset();
        // One lazy ancestry resolver per actual object, shared by all faces/maps.
        std::optional<Impl::TextureSource> textureSource;
        for (S32 f=0;f<count;++f)
        {
            if (producerDeferred)
                break;
            auto&        face    = candidates[f];
            const auto materialID=o.legacyPreview?LLUUID::null:o.renderMaterials.getMaterial(U8(f));
            VSMaterial::State surface;
            bool explicitAlpha=false;
            if(materialID.notNull())
            {
                face.pbrAsset=gGLTFMaterialList.getMaterial(materialID);
                if(!face.pbrAsset || !face.pbrAsset->isLoaded()) {uploadDeferred=true;continue;}
                surface=VSMaterial::pbr(*face.pbrAsset,face.material.getGLTFMaterialOverride());
                explicitAlpha=true;
            }
            else
            {
                face.pbrAsset=nullptr;
                auto legacy=face.material.getMaterialParams();
                if(legacy.isNull())legacy=mImpl->materialAssets->get(face.material.getMaterialID());
                if(!face.material.getMaterialID().isNull() && !legacy) {uploadDeferred=true;continue;}
                surface=VSMaterial::legacy(face.material,legacy.get());
                explicitAlpha=bool(legacy);
            }
            face.pickTextureMatrix.reset();
            if(o.animation && o.textureAnimation && (o.animation->mFace<0 || o.animation->mFace==f))
            {
                const auto& animation=*o.animation;
                const bool translating=(o.textureAnimMode & LLViewerTextureAnim::TRANSLATE)!=0;
                const bool scaling=(o.textureAnimMode & LLViewerTextureAnim::SCALE)!=0;
                const bool rotating=(o.textureAnimMode & LLViewerTextureAnim::ROTATE)!=0;
                LLMatrix4 textureMatrix;
                textureMatrix.setIdentity();textureMatrix.translate(LLVector3(-.5f,-.5f,0));
                LLQuaternion textureRotation;textureRotation.setQuat(rotating?animation.mRot:face.material.getRotation(),0,0,-1.f);
                textureMatrix.rotate(textureRotation);
                LLMatrix4 textureScale;textureScale.initAll(LLVector3(scaling?animation.mScaleS:face.material.getScaleS(),
                    scaling?animation.mScaleT:face.material.getScaleT(),1),LLQuaternion(),LLVector3());
                textureMatrix*=textureScale;
                textureMatrix.translate(LLVector3((translating?animation.mOffS:face.material.getOffsetS())+.5f,
                    (translating?animation.mOffT:face.material.getOffsetT())+.5f,0));
                face.pickTextureMatrix=textureMatrix;
                const auto animated=VSMaterial::legacyUV(scaling?animation.mScaleS:face.material.getScaleS(),
                    scaling?animation.mScaleT:face.material.getScaleT(),translating?animation.mOffS:face.material.getOffsetS(),
                    translating?animation.mOffT:face.material.getOffsetT(),rotating?animation.mRot:face.material.getRotation());
                if(!surface.pbr) surface.uv[0]=animated;
                else for(auto& uv:surface.uv)
                {
                    const auto base=uv;
                    const auto& a=base.matrix;const auto& b=animated.matrix;
                    uv.matrix={a[0]*b[0]+a[2]*b[1],a[1]*b[0]+a[3]*b[1],
                               a[0]*b[2]+a[2]*b[3],a[1]*b[2]+a[3]*b[3]};
                    uv.offset=VSMaterial::transform(base,animated.offset[0],animated.offset[1]);
                }
                if(surface.pbr) surface.normalTangent=VSNormalBasis::animate(surface.normalTangent,animated.matrix);
            }
            std::array<VSUIRenderer::Image,VSMaterial::ChannelCount> channelImages;
            bool channelsReady=true;
            for(unsigned channel=0;channel<VSMaterial::ChannelCount;++channel)
            {
                LLUUID texture;std::copy_n(surface.textures[channel].begin(),16,texture.mData);
                if(texture.isNull()) continue;
                if(const auto* builtin=vs_builtin_texture(texture);builtin && builtin->clamp)
                    surface.samplers[channel].u=surface.samplers[channel].v=VSWorldContracts::AddressMode::Clamp;
                const float distance=std::max((position-eye).length(),1.f);
                const unsigned demand=unsigned(std::clamp(o.scale.length()*std::max(viewportHeight,1u)/distance,1.f,4096.f));
                if((surface.pbr || channel==VSMaterial::BaseColor) &&
                    LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(texture))
                {
                    if(!textureSource)
                    {
                        const auto token=object(mImpl->host,id);
                        if(!token){channelsReady=false;break;}
                        textureSource.emplace(mImpl,o,guard(*token));
                    }
                    const auto resolved=textureSource->resolve(texture,true,true,false);
                    if(resolved.asset.notNull())vs_request_world_image(resolved.asset,demand);
                    channelImages[channel]=resolved.image;
                }
                else
                {
                    vs_request_world_image(texture,demand);
                    channelImages[channel]=mImpl->resources.image("remote:"+vs_native_image_id(texture).asString());
                }
                if(!channelImages[channel]) channelsReady=false;
            }
            const bool proceduralBump=!surface.pbr && surface.bump && surface.textures[VSMaterial::Normal]==std::array<std::uint8_t,16>{};
            if(proceduralBump)
            {
                LLUUID bumpSource;
                if(surface.bump<=2) std::copy_n(surface.textures[VSMaterial::BaseColor].begin(),16,bumpSource.mData);
                else bumpSource=mImpl->resources.standardBumpSource(surface.bump);
                if(bumpSource.notNull()) vs_request_world_image(bumpSource,4096,surface.bump>=3);
                channelImages[VSMaterial::Normal]=mImpl->resources.legacyBump(bumpSource,surface.bump,gSavedSettings.getF32("RenderNormalMapScale"));
                if(!channelImages[VSMaterial::Normal]) channelsReady=false;
                surface.uv[VSMaterial::Normal]=surface.uv[VSMaterial::BaseColor];
            }
            bool mediaActive=false;
            if(o.media && face.material.hasMedia())
            {
                const auto mediaID=o.media->texture(unsigned(f));
                const auto mediaImage=mImpl->resources.image("media:"+mediaID.asString());
                if(mediaID.notNull() && mediaImage)
                {
                    mediaActive=true;
                    channelImages[0]=mediaImage;
                    // The reference fetched PBR material replaces both base
                    // color and emissive images with the active media texture.
                    if(surface.pbr) channelImages[3]=mediaImage;
                    channelsReady=true;
                    for(unsigned channel=1;channel<VSMaterial::ChannelCount;++channel)
                        if(surface.textures[channel]!=std::array<std::uint8_t,16>{} && !channelImages[channel]) channelsReady=false;
                }
            }
            if(proceduralBump && !channelImages[VSMaterial::Normal]) channelsReady=false;
            if(!channelsReady) {uploadDeferred=true;continue;}
            auto image=channelImages[0];
            std::optional<VSWorldContracts::Image> description;
            if(!explicitAlpha && image)description=VSUIRenderer::imageDescription(image);
            const bool alphaOnlySource=!mediaActive && vs_ui_image_source_format(face.material.getID())==GL_ALPHA;
            // Component1 GL_ALPHA is not an alpha-color texture in getPoolTypeFromTE.
            if(alphaOnlySource && description)description->alpha=VSWorldContracts::AlphaMode::Opaque;
            surface.alpha=Impl::classifyFaceAlpha(face.material,description?&*description:nullptr,surface.alpha,explicitAlpha,hud,bool(riggedOwner));
            for(auto& sampler:surface.samplers) sampler.anisotropy=gSavedSettings.getBOOL("RenderAnisotropic")?16u:1u;
            if(!face.surface || !(*face.surface==surface)) face.surface=std::make_shared<VSMaterial::State>(surface);
            std::copy_n(channelImages.begin()+1,VSMaterial::ChannelCount-1,face.publishedChannels.begin());
            const U64 poseGeneration = riggedOwner ? riggedOwner->poseGeneration() : 0;
            if (o.dirty || (riggedOwner && face.poseGeneration != poseGeneration) || !face.mesh)
            {
                const auto& geometry=o.volume->getVolumeFace(f);
                if (!geometry.mPositions || !geometry.mNormals || !geometry.mTexCoords || !geometry.mIndices ||
                    geometry.mNumVertices <= 0 || geometry.mNumIndices <= 0 || geometry.mNumIndices % 3)
                {
                    uploadDeferred = true;
                    continue;
                }
                o.volume->genTangents(f);
                std::vector<VSWorldRenderer::Vertex> vertices;vertices.reserve(geometry.mNumVertices);
                if (riggedOwner && geometry.mWeights)
                {
                    if (!appearanceRiggedVertices(*riggedOwner, geometry, o.asset->skin, face.material, vertices))
                    {uploadDeferred=true;continue;}
                }
                else for (S32 v=0;v<geometry.mNumVertices;++v)
                {
                    const auto* p=geometry.mPositions[v].getF32ptr();const auto* n=geometry.mNormals[v].getF32ptr();
                    const LLVector3 scale=o.flexible?LLVector3(1,1,1):o.scale;
                    LLVector3 point=LLVector3(p[0]*scale.mV[0],p[1]*scale.mV[1],p[2]*scale.mV[2])*rotation+position;
                    LLVector3 normal=LLVector3(n[0]/scale.mV[0],n[1]/scale.mV[1],n[2]/scale.mV[2])*rotation;
                    normal.normalize();
                    const auto& uv=geometry.mTexCoords[v];
                    VSWorldRenderer::Vertex vertex{point.mV[0],point.mV[1],point.mV[2],normal.mV[0],normal.mV[1],normal.mV[2],uv.mV[0],1-uv.mV[1]};
                    if(geometry.mTangents)
                    {
                        const auto* tangent=geometry.mTangents[v].getF32ptr();
                        // LLFace::getGeometryVolume applies mat_normal to tangents.
                        LLVector3 t=LLVector3(tangent[0]/scale.mV[0],tangent[1]/scale.mV[1],tangent[2]/scale.mV[2])*rotation;
                        t.normalize();vertex.tx=t.mV[0];vertex.ty=t.mV[1];vertex.tz=t.mV[2];vertex.tw=tangent[3];
                    }
                    vertices.push_back(vertex);
                }
                auto pickSurface=std::make_shared<Impl::PickSurface>();
                pickSurface->residency=mImpl->budgets->primitiveBytes.reserve(sizeof(Impl::PickSurface));
                if(!pickSurface->residency || !geometry.mCenter) {uploadDeferred=true;continue;}
                pickSurface->volume=o.volume;pickSurface->face=f;
                if(o.asset && o.asset->volume==o.volume)pickSurface->asset=o.asset;
                else pickSurface->volumeResidency=o.primitiveResidency;
                pickSurface->center=*geometry.mCenter;pickSurface->position=position;pickSurface->rotation=rotation;
                pickSurface->scale=o.scale;pickSurface->volumeGlobal=bool(o.flexible)||bool(riggedOwner && geometry.mWeights);
                pickSurface->normalGlobal=bool(o.flexible)||bool(riggedOwner && geometry.mWeights);
                pickSurface->rigged=bool(riggedOwner);
                for(S32 vertex=0;vertex<geometry.mNumVertices;++vertex)
                {
                    // Native vertex UVs are top-left; retain original ST before
                    // projecting planar texture coordinates for rendering.
                    if(face.material.getTexGen()==LLTextureEntry::TEX_GEN_PLANAR && !(riggedOwner && geometry.mWeights))
                    {
                        LLVector2 tc=geometry.mTexCoords[vertex];LLVector4a point=geometry.mPositions[vertex],normal=geometry.mNormals[vertex];
                        // Reference LLFace projects source volume positions and
                        // normals before skinning, including rigged volume faces.
                        LLVector4a scale;scale.load3((pickSurface->volumeGlobal?LLVector3(1,1,1):o.scale).mV);point.mul(scale);
                        VSSurfaceCoordinates::planarProjection(tc,normal,pickSurface->center,point);
                        vertices[vertex].u=tc.mV[0];vertices[vertex].v=1-tc.mV[1];
                    }
                }
                std::vector<unsigned> indices(geometry.mIndices,geometry.mIndices+geometry.mNumIndices);
                unsigned attributes=VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0;
                if(geometry.mTangents) attributes|=VSWorldContracts::Tangent;
                if (!mImpl->renderer.publishMesh(face.mesh, vertices, indices,VSWorldRenderer::Topology::Triangles,attributes)) uploadDeferred = true;
                else
                {
                    face.poseGeneration = poseGeneration;
                    face.pickSurface=std::move(pickSurface);
                }
            }
            if (!face.mesh)
            {
                uploadDeferred = true;
                continue;
            }
            face.publishedAlphaOnly=alphaOnlySource && !surface.pbr && face.material.getMaterialParams().isNull() &&
                !(face.material.getColor().mV[3]<.999f);
            face.publishedMaterial = face.material;
            face.publishedImage    = std::move(image);
            face.published         = true;
        }
        mImpl->updateSelectionIndex(id,o,position,rotation);
        // Every face belongs to one admitted object/pose generation. A failed
        // upload keeps the complete previous mesh/material/image snapshot.
        if (!uploadDeferred)
        {
            candidates.resize(count);
            o.faces         = std::move(candidates);
            o.admittedFaces = o.faces;
            o.pruneMetricsRigging();
        }
        for (std::size_t f = 0; f < o.admittedFaces.size(); ++f)
        {
            const auto& face = o.admittedFaces[f];
            if (!face.published)
                continue;
            if (!face.mesh) continue;
            const auto&             color = face.publishedMaterial.getColor();
            VSWorldRenderer::Packet packet;
            packet.mesh  = face.mesh;
            packet.hud   = hud;
            packet.image = face.publishedImage;
            packet.alphaOnly=face.publishedAlphaOnly;
            packet.color = { color.mV[0], color.mV[1], color.mV[2], color.mV[3] };
            packet.surface=face.surface;packet.channels=face.publishedChannels;
            mImpl->packetOwner(packet,o,id);
            packet.faceIndex                                          = unsigned(f);
            if(face.surface)
            {
                packet.color=face.surface->base;
                packet.fullbright=hud || face.surface->fullbright;
                packet.alpha=face.surface->alpha==VSWorldContracts::AlphaMode::Blend;
                packet.masked=face.surface->alpha==VSWorldContracts::AlphaMode::Mask;
                packet.alphaCutoff=face.surface->cutoff;packet.doubleSided=face.surface->doubleSided;
            }
            if (riggedOwner && riggedOwner->visibility() == VSWorldAvatar::Visibility::Jellydoll)
            {
                packet.color = { .5f, .5f, .5f, 1 };
                packet.image.reset();
                packet.fullbright = true;
                packet.alpha      = false;
                packet.masked     = false;
            }
            packet.distanceSquared=(position-eye).lengthSquared();packets.push_back(std::move(packet));
        }
        o.dirty = uploadDeferred;
    }
    if (mRoot)
    {
        std::set<std::array<std::uint8_t,16>> seen;
        const std::array<std::uint8_t,16> anonymous{};
        for (const auto& packet:packets) if (packet.objectIdentity!=anonymous) seen.insert(packet.objectIdentity);
        for (auto& [host,region]:mNeighbours)
        {
            // Subtract integer global origins before converting to float.
            const auto transform=VSWorldContracts::regionOffset(mImpl->handle,region->mImpl->handle);
            const LLVector3 offset(transform[0],transform[1],transform[2]);
            const LLVector3 regionalEye=eye-offset;
            const float dx=regionalEye.mV[0]-std::clamp(regionalEye.mV[0],0.f,float(region->mImpl->width));
            const float dy=regionalEye.mV[1]-std::clamp(regionalEye.mV[1],0.f,float(region->mImpl->regionHeight));
            if (dx*dx+dy*dy>farClip*farClip) continue;
            auto                                  neighbours = region->packets(eye - offset, farClip, viewportHeight, viewportWidth);
            std::set<std::array<std::uint8_t,16>> delivered;
            for (auto& packet:neighbours)
            {
                if (packet.background || (packet.objectIdentity!=anonymous && seen.count(packet.objectIdentity))) continue;
                packet.regionOffset=transform;
                if (packet.objectIdentity!=anonymous) delivered.insert(packet.objectIdentity);
                packets.push_back(std::move(packet));
            }
            seen.insert(delivered.begin(),delivered.end());
        }
    }
    // Resolve each exact visible source owner once, then share its immutable
    // current-incarnation publication across all body/attachment faces.
    struct TimingOwners {std::shared_ptr<const VSWorldRenderer::AvatarTiming> avatar,attachment;};
    using TimingKey=std::tuple<decltype(VSWorldRenderer::Packet::objectSource),
        decltype(VSWorldRenderer::Packet::objectAccount),decltype(VSWorldRenderer::Packet::objectIdentity),std::uint64_t>;
    constexpr std::size_t entryBytes=sizeof(std::pair<const TimingKey,TimingOwners>)+4*sizeof(void*);
    const auto ownerCount=std::max<std::size_t>(1,packets.size());
    std::shared_ptr<const VSResourceBudget::Reservation> timingResidency;
    if(ownerCount<=std::numeric_limits<std::size_t>::max()/entryBytes)
        timingResidency=reserveSpatialBytes(ownerCount*entryBytes);
    if(timingResidency)
    {
        std::map<TimingKey,TimingOwners> owners;
        const std::array<std::uint8_t,16> anonymous{};
        for(auto& packet:packets)
        {
            if(packet.avatarTiming || packet.objectIdentity==anonymous)continue;
            const TimingKey key{packet.objectSource,packet.objectAccount,packet.objectIdentity,packet.objectGeneration};
            auto found=owners.find(key);
            if(found==owners.end())
            {
                const auto token=packetObject(packet);
                TimingOwners timing;
                if(token && token->generation==packet.objectGeneration)
                {timing.avatar=avatarTiming(*token);timing.attachment=attachmentTiming(*token);}
                found=owners.emplace(key,std::move(timing)).first;
            }
            packet.avatarTiming=found->second.avatar;
            packet.attachmentTiming=found->second.attachment;
        }
    }
    mImpl->visiblePackets.clear();
    mImpl->visiblePackets.reserve(packets.size());
    for (const auto& packet : packets)
    {
        Impl::Visible visible{ packet, packet.mesh, packet.image };
        // Picking records do not pin otherwise retired GPU/image generations.
        // Actual scene/submission owners determine their lifetime.
        visible.packet.mesh.reset();
        visible.packet.image.reset();
        visible.packet.channels={};
        visible.packet.terrainMaterial.reset();
        mImpl->visiblePackets.push_back(std::move(visible));
    }
    if(mImpl->selectionIndex && mImpl->selectionIndex->valid())mImpl->selectionIndexRevision=mImpl->nextRevision;
    return packets;
}
std::optional<VSWorldScene::Pick> VSWorldScene::pick(const LLVector3& start, const LLVector3& end, bool transparent, float lineRadius,
                                                     bool hud) const
{
    PickOptions options;options.transparent=transparent;options.rigged=true;options.unselectable=true;
    return pick(start,end,options,lineRadius,hud);
}
std::optional<VSWorldScene::Pick> VSWorldScene::pick(const LLVector3& start, const LLVector3& end, const PickOptions& options, float lineRadius,
                                                     bool hud) const
{
    if (!finiteVector(start) || !finiteVector(end) || mImpl->retired)
        return {};
    if(options.exactObject && !current(*options.exactObject))return {};
    std::optional<Pick> result;
    struct AvatarCandidate{FaceHandle face;const VSWorldScene* region;std::array<float,4> offset;};
    std::map<LLUUID,AvatarCandidate> avatarCandidates;
    const Impl::Object* lastPolicyObject=nullptr;bool lastPolicyAccepted=false;
    const std::array<std::uint8_t, 16> anonymous{};
    for (const auto& visible : mImpl->visiblePackets)
    {
        const auto& packet = visible.packet;
        if (packet.background || packet.hud != hud || (packet.objectIdentity == anonymous && !packet.terrain))
            continue;
        if(options.exactObject && packet.terrain)continue;
        const auto mesh = visible.mesh.lock();
        if (!mesh)
            continue;
        if (packet.terrain)
        {
            auto currentPatch = [&](const VSWorldScene& region)
            {
                if (region.mImpl->retired || region.mImpl->incarnation != packet.objectGeneration)
                    return false;
                const auto found = region.mImpl->ground.find(packet.terrainKey);
                return found != region.mImpl->ground.end() && found->second.mesh == mesh;
            };
            bool current = currentPatch(*this);
            if (!current)
                for (const auto& [host, region] : mNeighbours)
                    if (currentPatch(*region))
                    {
                        current = true;
                        break;
                    }
            if (!current)
                continue;
            std::array<float, 3> a{}, b{};
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                a[axis] = start.mV[axis] - packet.regionOffset[axis];
                b[axis] = end.mV[axis] - packet.regionOffset[axis];
            }
            VSWorldRenderer::SurfaceHit hit;
            if (!VSWorldRenderer::intersect(mesh, a, b, hit) || (result && hit.fraction >= result->fraction))
                continue;
            Pick pick;
            pick.land     = true;
            pick.fraction = hit.fraction;
            pick.normal   = LLVector3(hit.normal.data());
            pick.uv       = hit.uv;
            for (unsigned axis = 0; axis < 3; ++axis)
                pick.position.mV[axis] = hit.point[axis] + packet.regionOffset[axis];
            result = std::move(pick);
            continue;
        }
        const auto object = packetObject(packet);
        if (!object)
            continue;
        if(options.exactObject)
        {
            const auto& exact=*options.exactObject;
            if(object->host!=exact.host || object->local!=exact.local || object->generation!=exact.generation || object->id!=exact.id || object->accountGeneration!=exact.accountGeneration || object->account!=exact.account || object->region!=exact.region)continue;
        }
        const FaceHandle handle{ *object, packet.faceIndex, packet.faceGeneration };
        if (!current(handle))
            continue;
        const VSWorldScene* region=object->host==mImpl->host?this:mNeighbours.at(object->host).get();
        const auto& sourceObject=region->mImpl->objects.at(object->local);
        if(options.exactObject && !hud && sourceObject.reflectionProbe && !options.reflectionProbe)continue;
        if(!region->mImpl->linksCurrent(sourceObject))continue;
        const auto& published=sourceObject.admittedFaces;
        const Impl::Face* sourceFace=handle.face<published.size() && published[handle.face].mesh==mesh?&published[handle.face]:nullptr;
        const bool externalGLTF=packet.gltfNode>=0;
        if(sourceObject.pcode==LL_PCODE_LEGACY_AVATAR)
        {if(!hud)avatarCandidates.emplace(sourceObject.id,AvatarCandidate{handle,region,packet.regionOffset});continue;}
        const auto buildVisible=[&]{return options.buildVisible && options.buildVisible();};
        struct ObjectPolicy
        {
            const Impl::Object& object;const Impl::Face* face;const PickOptions& options;const ObjectHandle& token;
            const Impl& region;
            bool reflectionProbe()const{return object.reflectionProbe;}
            bool clickIgnore()const{return object.clickAction==CLICK_ACTION_IGNORE;}
            bool canSelectFlag()const{return object.selectable;}
            bool dead()const{return false;} // current identity + accepted visible frame checked above
            bool rendered()const{return true;} // exact weak mesh is alive and admitted
            bool canSelect()const{return options.canSelect && options.canSelect(token);}
            bool rigged()const{return face && face->pickSurface && face->pickSurface->rigged;}
            const Impl::Object* avatar()const
            {
                const auto* owner=&object;std::set<U32> seen;
                while(owner->parent && seen.insert(owner->parent).second)
                {
                    const auto found=region.objects.find(owner->parent);if(found==region.objects.end())return nullptr;
                    owner=&found->second;if(owner->pcode==LL_PCODE_LEGACY_AVATAR)return owner;
                }
                return nullptr;
            }
            bool hasAvatar()const{return avatar()!=nullptr;}
            bool selfAvatar()const{const auto* owner=avatar();return owner && owner->id==region.agent;}
        } policy{sourceObject,sourceFace,options,*object,*region->mImpl};
        // Imported GLTF scene traversal intentionally has its original independent
        // geometry-only policy; it never used LLVOVolume admission or TE masks.
        if(!externalGLTF)
        {
            if(lastPolicyObject!=&sourceObject)
            {
                lastPolicyObject=&sourceObject;
                lastPolicyAccepted=(options.exactObject || VSPickPolicy::spatial(policy,options.reflectionProbe,buildVisible)) &&
                    (sourceObject.pcode!=LL_PCODE_VOLUME || VSPickPolicy::volume(policy,options.rigged,hud || options.unselectable,buildVisible));
            }
            if(!lastPolicyAccepted)continue;
        }
        const bool transparent=options.transparent || (!externalGLTF &&
            ((options.reflectionProbe && sourceObject.reflectionProbe) || region->mImpl->highlightedOrBeacon(sourceObject)));
        const bool specialCursor=!sourceObject.reflectionProbe &&
            ((sourceObject.updateFlags&(FLAGS_USE_PHYSICS|FLAGS_HANDLE_TOUCH)) || sourceObject.clickAction!=0);
        struct VisibilityFace
        {
            const Impl::Face* face;
            bool present()const{return face!=nullptr;}
            F32 alpha()const{return face->publishedMaterial.getColor().mV[3];}
        } visibility{sourceFace};
        if(!externalGLTF && sourceObject.pcode==LL_PCODE_VOLUME && !VSPickPolicy::faceVisible(visibility,transparent,specialCursor))continue;
        std::optional<Pick> acceptedSurface;
        const auto surface=[&](const VSWorldRenderer::SurfaceHit& sample)->std::optional<Pick>
        {return sourceFace && sample.triangleKnown?Impl::surfacePick(*sourceFace,sample,visible.image.lock()):std::nullopt;};
        std::array<float, 3> a{}, b{};
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            a[axis] = start.mV[axis] - (hud ? 0 : packet.regionOffset[axis]);
            b[axis] = end.mV[axis] - (hud ? 0 : packet.regionOffset[axis]);
        }
        const auto image  = visible.image.lock();
        auto opaque=[&](const VSWorldRenderer::SurfaceHit& sample)
        {
            if(externalGLTF)return true;
            std::optional<Pick> candidateSurface;
            const auto mask=[&]
            {
                const auto extent=VSUIRenderer::imageExtent(image);
                LLVector2 uv(sample.uv[0],1-sample.uv[1]);
                if(sourceObject.pcode==LL_PCODE_VOLUME)
                {
                    candidateSurface=surface(sample);
                    if(!candidateSurface)return false;
                    uv=LLVector2(candidateSurface->uv.data());
                }
                return VSPickPolicy::textureMask(extent[0],extent[1],VSUIRenderer::imagePixels(image),uv.mV[0],uv.mV[1]);
            };
            if(sourceObject.pcode!=LL_PCODE_VOLUME)return transparent || !image || mask();
            struct HitFace
            {
                const Impl::Face* face;const VSUIRenderer::Image& image;const decltype(mask)& query;
                bool present()const{return face!=nullptr;}
                bool hasLegacyMaterial()const{return face->publishedMaterial.getMaterialParams().notNull();}
                auto legacyMode()const{return face->publishedMaterial.getMaterialParams()->getDiffuseAlphaMode();}
                auto cutoff()const{return face->publishedMaterial.getMaterialParams()->getAlphaMaskCutoff();}
                auto emissiveMode()const{return LLMaterial::DIFFUSE_ALPHA_MODE_EMISSIVE;}
                auto noneMode()const{return LLMaterial::DIFFUSE_ALPHA_MODE_NONE;}
                auto maskMode()const{return LLMaterial::DIFFUSE_ALPHA_MODE_MASK;}
                bool hasTexture()const{return bool(image);}
                bool uploaded()const{return bool(image);}
                bool mask()const{return query();}
            } hitFace{sourceFace,image,mask};
            const bool accepted=VSPickPolicy::acceptsHit(hitFace,transparent);
            if(accepted)acceptedSurface=std::move(candidateSurface);
            return accepted;
        };
        VSWorldRenderer::SurfaceHit hit;
        F32                         radius = lineRadius;
        if (!hud && radius > 0)
        {
            const auto bounds       = VSWorldRenderer::bounds(mesh);
            F32        meshDistance = 0, rayDistance = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const F32 meshDelta = (bounds[0][axis] + bounds[1][axis]) * .5f - a[axis], rayDelta = b[axis] - a[axis];
                meshDistance += meshDelta * meshDelta;
                rayDistance += rayDelta * rayDelta;
            }
            if (rayDistance > 0)
                radius *= std::clamp(std::sqrt(meshDistance / rayDistance), .001f, 1.f);
        }
        bool intersected=false;
        if(!externalGLTF && (sourceObject.pcode==LL_PCODE_LEGACY_TREE || sourceObject.pcode==LL_PCODE_TREE_NEW))
        {
            const auto extents=VSWorldRenderer::bounds(mesh);
            LLVector3 center=(LLVector3(extents[1].data())+LLVector3(extents[0].data()))*.5f;
            LLVector3 size=LLVector3(extents[1].data())-LLVector3(extents[0].data());
            const LLVector3 begin(a.data()),finish(b.data());
            if(LLLineSegmentBoxIntersect(begin,finish,center,size*.5f))
            {
                const auto rotation=sourceObject.rotation;
                center-=LLVector3(0,0,size.magVec()*.25f)*rotation;
                size.scaleVec(LLVector3(.25f,.25f,1));size.mV[0]=llmin(size.mV[0],1.f);size.mV[1]=llmin(size.mV[1],1.f);
                LLVector3 point,normal;
                if(linesegment_tetrahedron(begin,finish,center,size,rotation,point,normal))
                {
                    std::copy_n(point.mV,3,hit.point.begin());std::copy_n(normal.mV,3,hit.normal.begin());
                    const auto direction=finish-begin;hit.fraction=((point-begin)*direction)/direction.lengthSquared();intersected=true;
                }
            }
        }
        else intersected=VSWorldRenderer::intersect(mesh,a,b,hit,radius,opaque);
        if (!intersected || (result && hit.fraction >= result->fraction))
            continue;
        Pick pick;
        pick.face     = handle;
        pick.fraction = hit.fraction;
        pick.normal   = LLVector3(hit.normal.data());
        pick.uv       = {-1,-1};
        pick.hud      = hud;
        if(!acceptedSurface)acceptedSurface=surface(hit);
        if(acceptedSurface)
        {
            pick.st=acceptedSurface->st;pick.uv=acceptedSurface->uv;pick.normal=acceptedSurface->normal;
            pick.tangent=acceptedSurface->tangent;pick.xy=acceptedSurface->xy;pick.surfaceKnown=true;
        }
        for (unsigned axis = 0; axis < 3; ++axis)
            pick.position.mV[axis] = hit.point[axis] + (hud ? 0 : packet.regionOffset[axis]);
        pick.gltfNode      = packet.gltfNode;
        pick.gltfPrimitive = packet.gltfPrimitive;
        result             = std::move(pick);
    }
    if(!hud)
    {
        for(const auto& [id,candidate]:avatarCandidates)
        {
            if(!candidate.region->current(candidate.face))continue;
            const auto avatar=candidate.region->mImpl->avatars.find(id);
            if(avatar==candidate.region->mImpl->avatars.end())continue;
            const bool renderSelf=id!=candidate.region->mImpl->agent || !options.renderSelfAvatar || options.renderSelfAvatar();
            const LLVector3 offset(candidate.offset.data());
            const auto hit=avatar->second->collisionPick(start-offset,(result?result->position:end)-offset,renderSelf);
            if(!hit || (result && result->gltfNode>=0 && hit->fraction>=1.f))continue;
            const LLVector3 position=LLVector3(hit->point.data())+offset;
            if(result && !result->land)
            {
                const auto previous=objectAdmission(result->face.object);
                // Exact LLPipeline precedence for attachment/body collisions.
                if(previous && previous->attachment && dist_vec(position,result->position)<=.1f)continue;
            }
            Pick pick;pick.face=candidate.face;pick.avatar=true;pick.surfaceKnown=true;
            pick.position=position;pick.normal=LLVector3(hit->normal.data());
            const LLVector3 ray=end-start;const F32 length=ray.lengthSquared();
            pick.fraction=length>0?((position-start)*ray)/length:1;
            result=std::move(pick);
        }
    }
    if(!hud&&gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
    {
        const LLVector3 rootOffset(from_region_handle(mImpl->handle)-gAgent.getPosGlobalFromAgent(LLVector3::zero));
        LLVector4a agentStart;agentStart.load3((start+rootOffset).mV);
        std::function<void(const VSWorldScene&)> names;
        names=[&](const VSWorldScene& region)
        {
            if(region.mImpl->retired)return;
            for(const auto& [id,avatar]:region.mImpl->avatars)
            {
                const auto token=region.object(id);
                if(!token||token->kind!=VSWorldContracts::OwnerKind::Avatar||!region.current(*token))continue;
                LLVector4a agentEnd,intersection;
                const auto endpoint=(result?result->position:end)+rootOffset;agentEnd.load3(endpoint.mV);
                if(!avatar->pickNameTag(agentStart,agentEnd,intersection))continue;
                const LLVector3 point=LLVector3(intersection.getF32ptr())-rootOffset;
                const auto ray=endpoint-(start+rootOffset);const F32 length=ray.lengthSquared();
                const F32 relative=length>0?((LLVector3(intersection.getF32ptr())-(start+rootOffset))*ray)/length:1;
                if(result&&result->gltfNode>=0&&relative>=1.f)continue;
                Pick pick;pick.face={*token,0,region.mImpl->objects.at(token->local).revision};pick.avatar=true;
                pick.position=point;const auto full=end-start;const F32 distance=full.lengthSquared();
                pick.fraction=distance>0?((point-start)*full)/distance:1;
                result=std::move(pick);
            }
            for(const auto& [host,child]:region.mNeighbours)names(*child);
        };
        names(*this);
    }
    return result;
}
std::optional<VSWorldScene::Pick> VSWorldScene::pickHUD(float x, float y, float aspect, float zoom,const PickOptions* options) const
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(aspect) || aspect <= 0 || !std::isfinite(zoom) || zoom <= 0)
        return {};
    float minimum = 0, maximum = 0;
    bool  known = false;
    for (const auto& visible : mImpl->visiblePackets)
        if (visible.packet.hud)
            if (const auto mesh = visible.mesh.lock())
            {
                const auto bounds = VSWorldRenderer::bounds(mesh);
                if (!known)
                {
                    minimum = bounds[0][0];
                    maximum = bounds[1][0];
                    known   = true;
                }
                else
                {
                    minimum = std::min(minimum, bounds[0][0]);
                    maximum = std::max(maximum, bounds[1][0]);
                }
            }
    if (!known)
        return {};
    const F32 horizontal = -x * aspect * .5f / zoom, vertical = y * .5f / zoom;
    PickOptions hudOptions=options?*options:PickOptions{};hudOptions.rigged=false;hudOptions.unselectable=true;hudOptions.reflectionProbe=false;
    return pick(LLVector3(minimum - 1, horizontal, vertical), LLVector3(maximum + 1, horizontal, vertical), hudOptions, .001f, true);
}
VSWorldRenderer::View VSWorldScene::view(const LLVector3& eye,unsigned width,unsigned height,float farClip)
{
    VSWorldRenderer::View result;if (!width || !height || !finiteVector(eye) || !std::isfinite(farClip)) return result;
    const float nearClip=.1f,farDistance=llclamp(farClip,MIN_FAR_PLANE,MAX_FAR_PLANE),cot=1/std::tan(F_PI/6);
    const float x=cot*height/width,y=cot,a=(farDistance+nearClip)/(farDistance-nearClip),b=-2*farDistance*nearClip/(farDistance-nearClip);
    result.projection={0,0,a,1, -x,0,0,0, 0,y,0,0, x*eye.mV[1],-y*eye.mV[2],b-a*eye.mV[0],-eye.mV[0]};return result;
}
VSWorldRenderer::View VSWorldScene::cameraView(const LLVector3& eye,const LLVector3& direction,const LLVector3& vertical,
                                             unsigned width,unsigned height,float farClip,float verticalFov)
{
    VSWorldRenderer::View result;
    if (!std::isfinite(verticalFov) || verticalFov <= 0.f || verticalFov >= F_PI) return result;
    if (!width || !height || !finiteVector(eye) || !finiteVector(direction) || !finiteVector(vertical) || !std::isfinite(farClip)) return result;
    LLVector3 at=direction,up=vertical;
    if (at.normalize()<1.e-6f || up.normalize()<1.e-6f) return result;
    LLVector3 left=up%at;if (left.normalize()<1.e-6f) return result;
    up=at%left;up.normalize();
    const float nearClip=.1f,farDistance=llclamp(farClip,MIN_FAR_PLANE,MAX_FAR_PLANE),cot=1/std::tan(verticalFov*.5f);
    const float x=cot*height/width,y=cot,a=(farDistance+nearClip)/(farDistance-nearClip),b=-2*farDistance*nearClip/(farDistance-nearClip);
    for (unsigned column=0;column<3;++column)
    {
        result.projection[column*4]=-x*left.mV[column];result.projection[column*4+1]=y*up.mV[column];
        result.projection[column*4+2]=a*at.mV[column];result.projection[column*4+3]=at.mV[column];
    }
    result.projection[12]=x*(left*eye);result.projection[13]=-y*(up*eye);
    result.projection[14]=b-a*(at*eye);result.projection[15]=-(at*eye);
    return result;
}
bool VSWorldScene::agentPosition(LLVector3& position) const
{
    if (mImpl->agent.isNull()) return false;
    for (const auto& [id,object]:mImpl->objects)
        if (object.pcode==LL_PCODE_LEGACY_AVATAR && object.id==mImpl->agent)
        {
            LLQuaternion rotation;std::set<U32> visited;
            return mImpl->transform(id,position,rotation,visited,false);
        }
    return false;
}
VSWorldAvatar* VSWorldScene::avatarForObject(const LLUUID& id) const
{
    if (const auto avatar = mImpl->avatars.find(id); avatar != mImpl->avatars.end())
        return avatar->second.get();
    for (const auto& [local, object] : mImpl->objects)
        if (object.id == id && mImpl->linksCurrent(object))
        {
            const Impl::Object* owner = &object;
            std::set<U32>       visited;
            for (unsigned depth = 0; owner && depth < 256; ++depth)
            {
                if (owner->controlAvatar)
                    return owner->controlAvatar.get();
                if (const auto avatar = mImpl->avatars.find(owner->id); avatar != mImpl->avatars.end())
                    return avatar->second.get();
                if (!owner->parent || !visited.insert(owner->parent).second)
                    break;
                const auto parent = mImpl->objects.find(owner->parent);
                owner             = parent == mImpl->objects.end() ? nullptr : &parent->second;
            }
        }
    for (const auto& [host, region] : mNeighbours)
        if (auto* avatar = region->avatarForObject(id))
            return avatar;
    return nullptr;
}
void VSWorldScene::resyncAvatarAnimations()
{
    for (const auto& [id, avatar] : mImpl->avatars)
        avatar->resyncAnimations();
    for (const auto& [local, object] : mImpl->objects)
        if (object.controlAvatar)
            object.controlAvatar->resyncAnimations();
    for (const auto& [host, region] : mNeighbours)
        region->resyncAvatarAnimations();
}
VSWorldAvatar* VSWorldScene::selfAvatar() const
{
    const auto avatar = mImpl->avatars.find(mImpl->agent);
    return !mImpl->retired && avatar != mImpl->avatars.end() ? avatar->second.get() : nullptr;
}
LLUUID VSWorldScene::rootID(const LLUUID& identity) const
{
    if (identity.isNull() || mImpl->retired)
        return LLUUID::null;
    for (const auto& [local, object] : mImpl->objects)
    {
        if (object.id != identity || !mImpl->linksCurrent(object))
            continue;
        const Impl::Object* root = &object;
        std::set<U32>       visited;
        while (root->parent && visited.insert(root->parent).second && visited.size() <= 256)
        {
            const auto parent = mImpl->objects.find(root->parent);
            if (parent == mImpl->objects.end())
                return LLUUID::null;
            root = &parent->second;
        }
        return root->id;
    }
    for (const auto& [host, region] : mNeighbours)
        if (const auto id = region->rootID(identity); id.notNull())
            return id;
    return LLUUID::null;
}
bool VSWorldScene::isAttachment(const LLUUID& identity) const
{
    if (identity.isNull() || mImpl->retired)
        return false;
    for (const auto& [local, object] : mImpl->objects)
    {
        if (object.id != identity || !mImpl->linksCurrent(object))
            continue;
        const Impl::Object* child = &object;
        std::set<U32>       visited;
        while (child->parent && visited.insert(child->parent).second && visited.size() <= 256)
        {
            const auto parent = mImpl->objects.find(child->parent);
            if (parent == mImpl->objects.end())
                return false;
            if (parent->second.pcode == LL_PCODE_LEGACY_AVATAR)
                return true;
            child = &parent->second;
        }
        return false;
    }
    for (const auto& [host, region] : mNeighbours)
        if (region->isAttachment(identity))
            return true;
    return false;
}
#if VS_VULKAN_DIAGNOSTICS
LLSD VSWorldScene::terrainStatus() const
{
    // Cheap, credential-free counters for live terrain/FPS diagnosis. Avoid
    // serializing every object, avatar and image as the full evidence does.
    LLSD status;
    status["received_patches"] = S32(mImpl->ground.size());
    S32 meshes = 0;
    for (const auto& [key, patch] : mImpl->ground) meshes += bool(patch.mesh);
    status["published_meshes"] = meshes;
    status["detail_ids_known"] = 0;
    status["detail_images_ready"] = 0;
    for (const auto& id : mImpl->terrainTextures)
    {
        if (id.isNull()) continue;
        status["detail_ids_known"] = status["detail_ids_known"].asInteger() + 1;
        if (mImpl->resources.image("remote:" + vs_native_image_id(id).asString()))
            status["detail_images_ready"] = status["detail_images_ready"].asInteger() + 1;
    }
    status["terrain_bytes"] = F64(mImpl->budgets->terrainBytes.used());
    return status;
}
#endif
LLSD VSWorldScene::evidence() const
{
    // Counts live decoded owners, including generations retained after cache
    // eviction/replacement, rather than only the current cache map entries.
    LLSD result;result["avatars"]=LLSD::emptyArray();for (const auto& [id,avatar]:mImpl->avatars) result["avatars"].append(avatar->evidence());result["terrain_patches"]=S32(mImpl->ground.size());result["water_height_known"]=mImpl->waterKnown;result["sky"]= "native_default_gradient";result["objects"]=S32(mImpl->objects.size());result["pending_full_updates"]=S32(mImpl->pending.size());
    const auto stats=mImpl->renderer.statistics();
    result["draw_calls"]=ll_sd_from_U64(stats.draws);result["constant_allocations"]=ll_sd_from_U64(stats.constantBuffersCreated);
    result["binding_allocations"]=ll_sd_from_U64(stats.bindingsCreated);result["mesh_uploads"]=ll_sd_from_U64(stats.meshUploads);
    result["mesh_upload_bytes"]=ll_sd_from_U64(stats.meshUploadBytes);result["submission_cpu_ms"]=stats.cpuMilliseconds;
    result["resident_mesh_bytes"]=ll_sd_from_U64(stats.residentMeshBytes);result["mesh_budget_bytes"]=ll_sd_from_U64(stats.meshBudgetBytes);
    result["resident_geometry_snapshot_bytes"] = ll_sd_from_U64(stats.residentGeometrySnapshotBytes);
    result["mesh_allocation_refusals"]=ll_sd_from_U64(stats.meshAllocationRefusals);
    result["mesh_input_rejections"]            = ll_sd_from_U64(stats.meshInputRejections);
    result["accepted"]=ll_sd_from_U64(mImpl->accepted);result["rejected"]=ll_sd_from_U64(mImpl->rejected);
    result["unsupported"]=ll_sd_from_U64(mImpl->unsupported);result["assets"]=mImpl->assets->evidence();
    result["images"]=mImpl->resources.evidence();
    result["owned_object_slots"]=S32(mImpl->budgets->objects.used());result["owned_avatar_slots"]=S32(mImpl->budgets->avatars.used());
    result["owned_object_requests"]=S32(mImpl->budgets->objectRequests.used());result["object_request_limit"]=S32(mImpl->budgets->objectRequests.limit());
    result["resident_primitive_payload_bytes"]=ll_sd_from_U64(mImpl->budgets->primitiveBytes.used());
    result["resident_terrain_bytes"]=ll_sd_from_U64(mImpl->budgets->terrainBytes.used());
    result["owners"]=LLSD::emptyArray();
    for (const auto& [local,object]:mImpl->objects)
    {
        LLSD owner;owner["id"]=object.id;owner["local"]=ll_sd_from_U64(local);owner["kind"]=S32(Impl::kind(object));
        owner["generation"]=ll_sd_from_U64(object.generation);owner["revision"]=ll_sd_from_U64(object.revision);
        owner["parent"]=ll_sd_from_U64(object.parent);owner["parent_generation"]=ll_sd_from_U64(object.parentGeneration);
        owner["parent_retired"]=object.parentRetired;owner["motion_timestamp"]=object.motion.timestamp();
        owner["authoritative_position"]=ll_sd_from_vector3(object.motion.authoritative());
        owner["predicted_position"]=ll_sd_from_vector3(object.motion.position());
        owner["predicted_rotation"]=ll_sd_from_quaternion(object.motion.rotation());
        owner["selected"]=object.motion.selected();owner["task_serial"]=object.taskSerial;
        owner["geometry_pending"]=!object.renderable;owner["task_known"]=object.taskKnown;
        owner["control_animations"]=S32(object.controlAnimations.size());
        owner["flexible"]                                               = bool(object.flexible);
        if (object.flex)
        {
            owner["flexible_simulation_lod"] = object.flex->simulation;
            owner["flexible_render_lod"]     = object.flex->render;
        }
        if (object.controlAvatar)
            owner["control_avatar"] = object.controlAvatar->evidence();
        owner["asset"]=object.params.getSculptID();owner["asset_type"]=S32(object.params.getSculptType());
        owner["asset_generation"]=ll_sd_from_U64(object.asset?object.asset->generation:0);
        if (object.gltf)
            owner["gltf"] = object.gltf->evidence();
        if(object.asset && object.asset->volume->getNumVolumeFaces()>0)
        {
            const auto& geometry=object.asset->volume->getVolumeFace(0);
            if(geometry.mPositions && geometry.mNumVertices>=3)
            {
                const auto* position=geometry.mPositions[2].getF32ptr();
                owner["asset_third_vertex"]=ll_sd_from_vector3(LLVector3(position[0],position[1],position[2]));
            }
        }
        owner["faces"]=LLSD::emptyArray();
        for (unsigned index=0;index<object.faces.size();++index)
        {
            const auto& face=object.faces[index];LLSD row;row["index"]=S32(index);
            row["material_generation"]=ll_sd_from_U64(object.revision);row["texture"]=face.material.getID();
            row["legacy_material"]=face.material.getMaterialID().asUUID();
            row["geometry_generation"]=ll_sd_from_U64(VSWorldRenderer::geometryDescription(face.mesh).generation);
            owner["faces"].append(row);
        }
        result["owners"].append(owner);
    }
    result["host"]=mImpl->host.getIPandPort();result["handle"]=ll_sd_from_U64(mImpl->handle);
    result["region_generation"]=ll_sd_from_U64(mImpl->incarnation);result["width"]=S32(mImpl->width);result["height"]=S32(mImpl->regionHeight);
    result["neighbours"]=LLSD::emptyArray();for (const auto& [host,region]:mNeighbours) result["neighbours"].append(region->evidence());
    return result;
}

bool VSWorldScene::skyCurrent() const
{
    if(mImpl->retired || !mImpl->sky || !mImpl->skyDetail || !LLEnvironment::instanceExists())return false;
    const auto sky=LLEnvironment::instance().getCurrentSky();
    return sky && mImpl->skyDetail==llclamp(gSavedSettings.getU32("WLSkyDetail"),8u,180u) && mImpl->skyRadius==sky->getDomeRadius();
}

std::optional<U32> VSWorldScene::faceCount(const ObjectHandle& token) const
{
    if(mImpl->retired || !current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->faceCount(token);
    return U32(mImpl->objects.at(token.local).faces.size());
}
std::optional<std::vector<VSWorldScene::ObjectHandle>> VSWorldScene::directChildren(const ObjectHandle& token) const
{
    if(mImpl->retired || !current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->directChildren(token);
    std::vector<ObjectHandle> result;
    const auto [begin,end]=mImpl->objectIndex.children(token.local);
    for(auto edge=begin;edge!=end;++edge)
    {
        const auto child=mImpl->objects.find(edge->second);
        if(child==mImpl->objects.end() || child->second.parent!=token.local || child->second.parentRetired ||
            (child->second.parentGeneration && child->second.parentGeneration!=token.generation))continue;
        if(auto handle=object(mImpl->host,edge->second))result.push_back(*handle);
    }
    return result;
}
std::optional<VSWorldScene::ObjectHandle> VSWorldScene::parentObject(const ObjectHandle& token) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->parentObject(token);
    const auto& value=mImpl->objects.at(token.local);
    if(!value.parent || value.parentRetired)return {};
    const auto parent=mImpl->objects.find(value.parent);
    if(parent==mImpl->objects.end() || (value.parentGeneration && value.parentGeneration!=parent->second.generation))return {};
    return object(mImpl->host,value.parent);
}

std::optional<VSWorldScene::ObjectHandle> VSWorldScene::rootObject(const ObjectHandle& token,bool stopAtAvatar) const
{
    if(mImpl->retired || !current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->rootObject(token,stopAtAvatar);
    auto root=token;
    std::set<U32> visited;
    while(true)
    {
        const auto& child=mImpl->objects.at(root.local);
        if(!child.parent || (stopAtAvatar && child.pcode==LL_PCODE_LEGACY_AVATAR))return root;
        if(child.parentRetired || !visited.insert(root.local).second)return {};
        const auto parent=mImpl->objects.find(child.parent);
        if(parent==mImpl->objects.end() || (child.parentGeneration && child.parentGeneration!=parent->second.generation))return {};
        if(stopAtAvatar && parent->second.pcode==LL_PCODE_LEGACY_AVATAR)return root;
        const auto handle=object(mImpl->host,parent->first);
        if(!handle)return {};
        root=*handle;
    }
}

bool VSWorldScene::setIcon(const ObjectHandle& requested,const std::string& imageName,bool scriptError)
{
    if(!current(requested))return false;
    if(requested.host!=mImpl->host)return mNeighbours.at(requested.host)->setIcon(requested,imageName,scriptError);
    ObjectHandle token=requested;
    if(mImpl->hudObject(mImpl->objects.at(token.local)))
    {
        const auto self=object(mImpl->agent);if(!self)return false;token=*self;
        if(token.host!=mImpl->host)return mNeighbours.at(token.host)->setIcon(token,imageName,scriptError);
    }
    auto& object=mImpl->objects.at(token.local);
    if(!mImpl->linksCurrent(object))return false;
    if(object.icon && object.icon->getSourceID()==token.id){object.icon->restartLifeTimer();return true;}
    const auto residency=reserveSpatialBytes(sizeof(LLHUDIcon)+sizeof(ObjectHandle)+sizeof(std::weak_ptr<Impl>));
    if(!residency)return false;
    const auto image=vs_ui_named_image_snapshot(imageName);
    const std::weak_ptr<Impl> weak=mImpl;const auto current=guard(token);
    LLPointer<LLHUDIcon> icon=static_cast<LLHUDIcon*>(LLHUDObject::addHUDObject(LLHUDObject::LL_HUD_ICON));
    icon->setNativeSource([weak,token,current]() -> std::optional<LLHUDIcon::NativeSource>
    {
        if(!current())return {};
        const auto state=weak.lock();if(!state)return {};
        const auto found=state->objects.find(token.local);
        if(found==state->objects.end() || !state->linksCurrent(found->second))return {};
        const auto& source=found->second;LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
        if(!state->transform(token.local,position,rotation,visited,true))return {};
        const auto global=from_region_handle(state->handle)+LLVector3d(position);
        const auto scale=source.scale;
        // Literal original volume/tree/default drawable radius assignments.
        const F32 radius=source.pcode==LL_PCODE_VOLUME?scale.length():
            ((source.pcode==LL_PCODE_LEGACY_TREE || source.pcode==LL_PCODE_TREE_NEW)?32.f:LLVector3(scale.mV[0],scale.mV[1],scale.mV[2]*.5f).length());
        return LLHUDIcon::NativeSource{source.id,gAgent.getPosAgentFromGlobal(global),scale,global,rotation,radius};
    },image,residency,[weak,token,identity=icon.get()]
    {
        if(const auto state=weak.lock())
            if(const auto found=state->objects.find(token.local);found!=state->objects.end() &&
                found->second.generation==token.generation && found->second.id==token.id && found->second.icon.get()==identity)
                found->second.icon=nullptr;
    },[imageName]{return vs_ui_named_image_snapshot(imageName);});
    icon->setScale(.03f);if(scriptError)icon->setScriptError();
    if(object.icon)object.icon->markDead();
    object.icon=icon;
    return true;
}

void VSWorldScene::refreshNameTags()
{
    if(mImpl->retired)return;
    if(mRoot)VSAvatarNameTag::refreshPreferences(vs_region_limits().getAllowRenderName());
    using Bounds=std::array<LLVector3,2>;
    auto extend=[](Bounds& into,const Bounds& from)
    {
        for(unsigned axis=0;axis<3;++axis)
        {into[0].mV[axis]=std::min(into[0].mV[axis],from[0].mV[axis]);into[1].mV[axis]=std::max(into[1].mV[axis],from[1].mV[axis]);}
    };
    auto children=[&](U32 local)
    {
        std::vector<U32> result;auto range=mImpl->objectIndex.children(local);
        for(auto i=range.first;i!=range.second;++i)
        {
            const auto child=mImpl->objects.find(i->second);
            if(child!=mImpl->objects.end()&&mImpl->linksCurrent(child->second))result.push_back(child->first);
        }
        return result;
    };
    auto rigging=[&](Impl::Object& object)
    {
        // Adopt only a coherent source generation actually admitted by every
        // face. A pending/refused mesh replacement never supplies newer bounds.
        std::shared_ptr<const VSWorldAssets::Geometry> source;
        bool complete=!object.admittedFaces.empty();
        for(const auto& face:object.admittedFaces)
        {
            const auto raw=face.pickSurface;
            if(!face.mesh||!raw||!raw->asset||!raw->rigged){complete=false;break;}
            if(!source)source=raw->asset;
            else if(source!=raw->asset){complete=false;break;}
        }
        complete=complete&&source&&source->skin.isMap()&&object.admittedFaces.size()==unsigned(source->volume->getNumVolumeFaces());
        if(complete&&(!object.nameRigging||source->lod>object.nameRiggingLOD||
            (source->lod==3&&source->generation!=object.nameRiggingGeneration)))
        {
            LLSD metadata=source->skin;LLMeshSkinInfo skin(metadata);
            const auto table=VSAvatarRiggingInfo::create(*source->volume,skin,
                [&](std::size_t bytes){return reserveSpatialBytes(bytes);});
            if(table)
            {object.nameRigging=table;object.nameRiggingLOD=source->lod;object.nameRiggingGeneration=source->generation;}
        }
        return object.nameRigging;
    };
    std::map<VSWorldAvatar*,std::optional<Bounds>> cached;
    std::function<std::optional<Bounds>(VSWorldAvatar&,U32)> envelope;
    envelope=[&](VSWorldAvatar& avatar,U32 root)->std::optional<Bounds>
    {
        if(auto found=cached.find(&avatar);found!=cached.end())return found->second;
        cached[&avatar]={}; // Reject a cycle before following any control owner.
        auto bounds=avatar.nameTagBaseBounds();if(!bounds)return {};
        if(!avatar.isControlAvatar()&&avatar.visibility()!=VSWorldAvatar::Visibility::Normal)return cached[&avatar]=bounds;
        const auto direct=children(root);
        std::vector<U32> associated;
        if(avatar.isControlAvatar())
        {
            associated.push_back(root);
            for(U32 local:direct)if(mImpl->objects.at(local).pcode==LL_PCODE_VOLUME)associated.push_back(local);
        }
        else for(U32 local:direct)
        {
            auto& object=mImpl->objects.at(local);
            if(object.pcode==LL_PCODE_VOLUME)associated.push_back(local);
            if(object.controlAvatar)continue;
            for(U32 child:children(local))if(mImpl->objects.at(child).pcode==LL_PCODE_VOLUME)associated.push_back(child);
        }
        // The original static attachment bridge includes its non-rigged
        // descendants, skips HUDs/rigged children and excludes megaprim spans.
        if(!avatar.isControlAvatar())for(U32 local:direct)
        {
            auto& object=mImpl->objects.at(local);if(mImpl->hudObject(object))continue;
            if(object.controlAvatar)
            {
                const auto control=envelope(*object.controlAvatar,local);
                if(!control)return {};
                extend(*bounds,*control);continue;
            }
            const auto token=this->object(mImpl->host,local);if(!token)continue;
            const auto family=linkedFamily(*token,false,true);if(!family)return {};
            std::optional<Bounds> bridge;bool hasRigged=false;
            for(const auto& child:*family)
            {
                const auto& part=mImpl->objects.at(child.local);
                if(part.gltf)
                    if(const auto raw=part.gltf->publishedBounds())
                    {
                        const Bounds box{LLVector3((*raw)[0].data()),LLVector3((*raw)[1].data())};
                        if(bridge)extend(*bridge,box);else bridge=box;
                    }
                for(const auto& face:part.admittedFaces)
                {
                    if(face.pickSurface&&face.pickSurface->rigged)hasRigged=true;
                    if(!face.mesh)continue;
                    const auto raw=VSWorldRenderer::bounds(face.mesh);
                    const Bounds box{LLVector3(raw[0].data()),LLVector3(raw[1].data())};
                    if(bridge)extend(*bridge,box);else bridge=box;
                }
            }
            if(hasRigged||!bridge)continue;
            const F32 maximum=get_default_max_prim_scale()*5.f;
            const auto span=(*bridge)[1]-(*bridge)[0];
            if(span.mV[0]<maximum&&span.mV[1]<maximum&&span.mV[2]<maximum)extend(*bounds,*bridge);
        }
        std::vector<std::shared_ptr<const VSAvatarRiggingInfo>> rigged;rigged.reserve(associated.size());
        for(U32 local:associated)
        {
            auto& object=mImpl->objects.at(local);
            bool isRigged=false;
            for(const auto& face:object.admittedFaces)if(face.pickSurface&&face.pickSurface->rigged){isRigged=true;break;}
            if(!isRigged)continue;
            const auto info=rigging(object);if(!info)return {};
            rigged.push_back(info);
        }
        if(!avatar.extendRiggingBounds(rigged,*bounds))return {};
        cached[&avatar]=bounds;return bounds;
    };
    // The original spatial extent is an avatar property, independent of the
    // name-tag allocation/visibility. Control avatars use the same envelope.
    for (auto& [local, object] : mImpl->objects)
        if (object.controlAvatar && mImpl->linksCurrent(object))
            if (const auto bounds = envelope(*object.controlAvatar, local)) object.controlAvatar->spatialBounds(*bounds);
    const LLVector3 offset(from_region_handle(mImpl->handle)-gAgent.getPosGlobalFromAgent(LLVector3::zero));
    for(auto& [id,avatar]:mImpl->avatars)
    {
        const auto range=mImpl->objectIndex.identities(id);
        if(range.first==range.second)continue;
        const auto token=object(mImpl->host,range.first->second);
        if(!token||token->kind!=VSWorldContracts::OwnerKind::Avatar)continue;
        auto& source=mImpl->objects.at(token->local);if(!mImpl->linksCurrent(source))continue;
        const auto attached=children(token->local);
        const unsigned count=unsigned(std::count_if(attached.begin(),attached.end(),[&](U32 local){return mImpl->objects.at(local).attachment!=0;}));
        if(mRoot && id==mImpl->agent && avatar->needsFirstVisibility())
        {
            bool pendingAttachment=false;
            for(U32 local:attached)
            {
                const auto& item=mImpl->objects.at(local);if(!item.attachment)continue;
                LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
                pendingAttachment |= !mImpl->transform(local,position,rotation,visited);
            }
            const auto pendingMeshes=[&]
            {
                const auto meshPending=[&](const Impl::Object& item)
                {
                    if(!item.params.isMeshSculpt() || item.asset)return false;
                    const auto state=mImpl->assets->request(item.params,std::max<S32>(0,item.assetLOD));
                    return state==VSWorldAssets::State::Missing || state==VSWorldAssets::State::Pending;
                };
                for(U32 local:attached)
                {
                    const auto& item=mImpl->objects.at(local);if(!item.attachment)continue;
                    if(meshPending(item))return true;
                    // Original attachment root and immediate children only.
                    for(U32 child:children(local))if(meshPending(mImpl->objects.at(child)))return true;
                }
                return false;
            };
            if(avatar->updateFirstFullyVisible(count,pendingAttachment,pendingMeshes) && current(*token))
                FSDiscordConnect::instance().onNativeSelfFullyVisible();
        }
        const auto bounds=envelope(*avatar,token->local);if(!bounds){avatar->suspendNameTag();continue;}

        if(!source.nameTagGuard)source.nameTagGuard=std::make_shared<const std::function<bool()>>(guard(*token));
        avatar->updateNameTag(source.nameValues,source.nameTagGuard,offset,mImpl->waterHeight,*bounds,count);
    }
    for(auto& [host,region]:mNeighbours)region->refreshNameTags();
}
void VSWorldScene::invalidateNameTags(const LLUUID& id)
{
    for(auto& [identity,avatar]:mImpl->avatars)if(id.isNull()||identity==id)avatar->invalidateNameTag();
    for(auto& [host,region]:mNeighbours)region->invalidateNameTags(id);
}
void VSWorldScene::nameTagChat(const LLChat& chat)
{
    if(chat.mSourceType!=CHAT_SOURCE_AGENT||chat.mFromID.isNull())return;
    const auto token=object(chat.mFromID);if(!token||token->kind!=VSWorldContracts::OwnerKind::Avatar)return;
    if(auto* avatar=avatarForObject(chat.mFromID)){avatar->nameTagTyping(false);avatar->nameTagChat(chat);}
}
void VSWorldScene::nameTagTyping(const LLUUID& id,bool typing)
{
    const auto token=object(id);if(!token||token->kind!=VSWorldContracts::OwnerKind::Avatar)return;
    if(auto* avatar=avatarForObject(id))avatar->nameTagTyping(typing);
}

bool VSWorldScene::nameTagTyping(const LLUUID& id) const
{
    const auto token=object(id);
    if(!token||token->kind!=VSWorldContracts::OwnerKind::Avatar||!current(*token))return false;
    const auto* avatar=avatarForObject(id);
    return avatar&&avatar->nameTagTyping();
}

std::optional<VSWorldScene::Attachment> VSWorldScene::objectAttachment(const ObjectHandle& token) const
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->objectAttachment(token);
    const auto root = rootObject(token);
    if (!root) return {};
    const auto& object = mImpl->objects.at(root->local);
    if (!object.attachment || !mImpl->linksCurrent(object)) return {};
    const unsigned state = object.attachment;
    return Attachment{*root, object.inventoryItem, U8(((state & 15) << 4) | (state >> 4))};
}

void VSWorldScene::appendVoice(std::vector<VSWorldRenderer::Packet>& packets)
{
    const auto origin=from_region_handle(mImpl->handle);
    for(auto& [id,avatar]:mImpl->avatars)avatar->appendVoice(packets,origin);
    for(auto& [host,scene]:mNeighbours)
    {
        const auto start=packets.size();scene->appendVoice(packets);
        const LLVector3 offset(from_region_handle(scene->mImpl->handle)-origin);
        for(auto i=start;i<packets.size();++i)
            for(unsigned axis=0;axis<3;++axis)packets[i].regionOffset[axis]+=offset.mV[axis];
    }
}

boost::signals2::connection VSWorldScene::observeMaterialUpdates(const boost::signals2::signal<void(const ObjectHandle&, S32)>::slot_type& slot)
{
    return mImpl->budgets->materialUpdates.connect(slot);
}

std::shared_ptr<const VSWorldRenderer::AvatarTiming> VSWorldScene::attachmentTiming(const ObjectHandle& token)
{
    const auto attachment=objectAttachment(token);if(!attachment)return {};
    const auto& root=attachment->object;
    if(root.host!=mImpl->host)return mNeighbours.at(root.host)->attachmentTiming(root);
    auto& object=mImpl->objects.at(root.local);
    if(object.attachmentTiming && object.attachmentTiming->current())return object.attachmentTiming;
    const U32 frame=LLDrawable::getCurrentFrame();
    if(object.attachmentTimingAttempt && *object.attachmentTimingAttempt==frame)return {};
    object.attachmentTimingAttempt=frame;
    auto residency=reserveSpatialBytes(sizeof(VSWorldRenderer::AvatarTiming)+sizeof(ObjectHandle)+8*sizeof(void*));
    if(!residency)return {};
    const auto live=guard(root);const std::weak_ptr<Impl> weak=mImpl;
    const auto parent=object.parent;const auto point=object.attachment;const auto item=object.inventoryItem;
    const auto resolve=[weak,live,local=root.local,parent,point,item]()->Impl::Object*
    {
        if(!live())return nullptr;
        const auto owner=weak.lock();if(!owner)return nullptr;
        const auto found=owner->objects.find(local);if(found==owner->objects.end())return nullptr;
        auto& object=found->second;
        return object.parent==parent && object.attachment==point && object.inventoryItem==item?&object:nullptr;
    };
    auto value=std::make_shared<VSWorldRenderer::AvatarTiming>();value->residency=std::move(residency);
    std::copy(root.id.mData,root.id.mData+UUID_BYTES,value->identity.begin());
    value->current=[resolve]{return resolve()!=nullptr;};
    value->publish=[resolve](float milliseconds)
    {
        if(std::isfinite(milliseconds) && milliseconds>=0)
            if(auto* object=resolve())object->attachmentGpuMilliseconds=milliseconds;
    };
    object.attachmentTiming=value;return value;
}

std::vector<VSWorldScene::AvatarPerformance> VSWorldScene::performanceAvatars() const
{
    std::vector<AvatarPerformance> result;std::set<LLUUID> seen;
    if(mImpl->retired)return result;
    for(const auto& [id,avatar]:mImpl->avatars)
    {
        const auto range=mImpl->objectIndex.identities(id);if(range.first==range.second)continue;
        const auto token=object(mImpl->host,range.first->second);if(!token || !current(*token))continue;
        AvatarPerformance row;row.object=*token;
        row.position=from_region_handle(mImpl->handle)+LLVector3d(avatar->character().getCharacterPosition());
        row.gpuMilliseconds=avatar->measuredGpuRenderTime();row.tuningMilliseconds=row.gpuMilliseconds?std::optional<F32>(avatar->gpuRenderTime()):std::nullopt;row.complexity=avatar->visualComplexity();
        const auto appearance=avatar->visibility();row.invisible=appearance==VSWorldAvatar::Visibility::Invisible;
        row.jellydoll=appearance==VSWorldAvatar::Visibility::Jellydoll;
        row.tooSlow=avatar->isTooSlow();row.tooSlowWithoutShadows=avatar->isTooSlowWithoutShadows();
        result.push_back(std::move(row));seen.insert(id);
    }
    for(const auto& [local,source]:mImpl->objects)
    {
        if(!source.controlAvatar || !mImpl->linksCurrent(source))continue;
        const auto token=object(mImpl->host,local);if(!token || !current(*token))continue;
        const auto& avatar=source.controlAvatar;
        AvatarPerformance row;row.object=*token;row.control=true;
        row.position=from_region_handle(mImpl->handle)+LLVector3d(avatar->character().getCharacterPosition());
        row.gpuMilliseconds=avatar->measuredGpuRenderTime();row.tuningMilliseconds=row.gpuMilliseconds?std::optional<F32>(avatar->gpuRenderTime()):std::nullopt;
        row.complexity=avatar->visualComplexity();const auto appearance=avatar->visibility();
        row.invisible=appearance==VSWorldAvatar::Visibility::Invisible;row.jellydoll=appearance==VSWorldAvatar::Visibility::Jellydoll;
        row.tooSlow=avatar->isTooSlow();row.tooSlowWithoutShadows=avatar->isTooSlowWithoutShadows();
        result.push_back(std::move(row));seen.insert(source.id);
    }
    for(const auto& [host,region]:mNeighbours)
        for(auto& row:region->performanceAvatars())if(seen.insert(row.object.id).second)result.push_back(std::move(row));
    return result;
}

std::vector<VSWorldScene::AttachmentPerformance> VSWorldScene::performanceAttachments() const
{
    std::vector<AttachmentPerformance> result;
    for(const auto& attachment:attachments())
    {
        if(!current(attachment.object))continue;
        const auto* region=this;
        if(attachment.object.host!=mImpl->host)region=mNeighbours.at(attachment.object.host).get();
        const auto& object=region->mImpl->objects.at(attachment.object.local);
        result.push_back({attachment,object.attachmentGpuMilliseconds,object.attachmentComplexity});
    }
    return result;
}

void VSWorldScene::refreshAvatarRenderSettings()
{
    if(mImpl->retired)return;
    for(auto& [id,avatar]:mImpl->avatars)avatar->refreshRenderSettings();
    for(auto& [local,object]:mImpl->objects)if(object.controlAvatar)object.controlAvatar->refreshRenderSettings();
    for(auto& [host,region]:mNeighbours)region->refreshAvatarRenderSettings();
}

void VSWorldScene::appendAppearanceAttachments(std::vector<VSWorldRenderer::Packet>& packets, VSWorldRenderer& renderer)
{
    auto* self = selfAvatar();
    if (!self || mImpl->retired) return;
    for (auto& [local, object] : mImpl->objects)
    {
        if (!object.renderable || !mImpl->linksCurrent(object) || !object.asset || !object.asset->skin.isMap() ||
            !object.volume || mImpl->hudObject(object) || mImpl->skeletonOwner(object) != self) continue;
        for (std::size_t index = 0; index < object.admittedFaces.size(); ++index)
        {
            const auto& face = object.admittedFaces[index];
            if (!face.published || !face.mesh || index >= std::size_t(object.volume->getNumVolumeFaces())) continue;
            const auto& geometry = object.volume->getVolumeFace(S32(index));
            if (!geometry.mWeights || !geometry.mPositions || !geometry.mNormals || !geometry.mTexCoords ||
                !geometry.mIndices || geometry.mNumVertices <= 0 || geometry.mNumIndices <= 0 || geometry.mNumIndices % 3) continue;
            object.volume->genTangents(S32(index));
            std::vector<VSWorldRenderer::Vertex> vertices;
            if (!appearanceRiggedVertices(*self, geometry, object.asset->skin, face.publishedMaterial, vertices)) continue;
            const std::vector<unsigned> indices(geometry.mIndices, geometry.mIndices + geometry.mNumIndices);
            unsigned attributes = VSWorldContracts::Position | VSWorldContracts::Normal | VSWorldContracts::UV0;
            if (geometry.mTangents) attributes |= VSWorldContracts::Tangent;
            VSWorldRenderer::Packet packet;
            packet.mesh = renderer.upload(vertices, indices, VSWorldRenderer::Topology::Triangles, attributes);
            if (!packet.mesh) { mImpl->resources.deferDraw(); continue; }
            packet.image = face.publishedImage;
            packet.channels = face.publishedChannels;
            packet.surface = face.surface;
            const auto& color = face.publishedMaterial.getColor();
            packet.color = {color.mV[0], color.mV[1], color.mV[2], color.mV[3]};
            if (face.surface)
            {
                packet.color = face.surface->base;
                packet.fullbright = face.surface->fullbright;
                packet.alpha = face.surface->alpha == VSWorldContracts::AlphaMode::Blend;
                packet.masked = face.surface->alpha == VSWorldContracts::AlphaMode::Mask;
                packet.alphaCutoff = face.surface->cutoff;
                packet.doubleSided = face.surface->doubleSided;
            }
            packets.push_back(std::move(packet));
        }
    }
}


S32 VSWorldScene::editTextureEntry(const ObjectHandle& token,U32 side,const std::function<S32(LLTextureEntry&)>& edit)
{
    if(!current(token))return TEM_CHANGE_NONE;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->editTextureEntry(token,side,edit);
    auto& object=mImpl->objects.at(token.local);
    if(object.pcode!=LL_PCODE_VOLUME || side>=object.faces.size())return TEM_CHANGE_NONE;
    const auto revision=object.revision;LLTextureEntry value=object.faces[side].material;
    const S32 change=edit(value);
    if(!change || !current(token))return TEM_CHANGE_NONE;
    auto& target=mImpl->objects.at(token.local);
    if(target.revision!=revision || side>=target.faces.size())return TEM_CHANGE_NONE;
    target.faces[side].material=value;target.dirty=true;target.revision=mImpl->nextRevision++;
    return change;
}
bool VSWorldScene::packTextureEntries(const ObjectHandle& token,LLMessageSystem& message) const
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->packTextureEntries(token,message);
    const auto& object=mImpl->objects.at(token.local);
    if(object.pcode!=LL_PCODE_VOLUME || object.faces.empty() || object.faces.size()>45)return false;
    LLPrimitive primitive;primitive.setNumTEs(U8(object.faces.size()));
    for(U32 side=0;side<object.faces.size();++side)primitive.setTE(U8(side),object.faces[side].material);
    message.addString("MediaURL",object.mediaVersion);
    return primitive.packTEMessage(&message);
}

std::optional<VSWorldScene::EditorTransform> VSWorldScene::editorTransform(const ObjectHandle& token,S32 gltfNode,const LLVector3d& agentOrigin,bool renderedAttachment) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->editorTransform(token,gltfNode,agentOrigin,renderedAttachment);
    const auto& source=mImpl->objects.at(token.local);
    const auto local=objectLocalTransform(token);if(!local)return {};
    EditorTransform result;result.localPosition=local->position;result.scale=local->scale;
    if(gltfNode!=-1)
    {
        LLMatrix4a nodeMatrix;if(!source.gltf || !source.gltf->nodeMatrix(gltfNode,nodeMatrix))return {};
        const auto render=focusObject(token,agentOrigin);if(!render || !render->current())return {};
        LLMatrix4a root;root.loadu(reinterpret_cast<const F32*>(render->assetToAgentMatrix.mMatrix));
        LLMatrix4a agent;matMul(nodeMatrix,root,agent);
        result.localPosition.set(agent.getTranslation().getF32ptr());
        result.globalPosition=agentOrigin+LLVector3d(result.localPosition);
        result.rotation.set(agent.asMatrix4());
        for(S32 axis=0;axis<3;++axis)result.scale.mV[axis]=agent.mMatrix[axis].getLength3().getF32();
    }
    else if(renderedAttachment && objectAttachment(token))
    {
        // The real joint transform is the same attachment-point owner used by
        // the reference selected-root and selected-child branches.
        const auto render=focusObject(token,agentOrigin);if(!render || !render->current())return {};
        result.globalPosition=agentOrigin+LLVector3d(render->renderPosition);result.rotation=render->renderRotation;
    }
    else
    {
        // getPositionRegion recursively accumulates parent positions, while
        // getRotationRegion multiplies only the immediate parent's rotation.
        // Preserve that distinction even for unusual deeper parent trees.
        std::array<const Impl::Object*,256> chain{};std::array<U32,256> locals{};std::size_t count=0;U32 id=token.local;
        for(;;)
        {
            if(count==chain.size() || std::find(locals.begin(),locals.begin()+count,id)!=locals.begin()+count)return {};
            const auto& object=mImpl->objects.at(id);locals[count]=id;chain[count++]=&object;
            if(!object.parent)break;
            const auto parent=mImpl->objects.find(object.parent);
            if(object.parentRetired || parent==mImpl->objects.end() || (object.parentGeneration && object.parentGeneration!=parent->second.generation))return {};
            id=object.parent;
        }
        LLVector3 position=chain[count-1]->motion.position();
        for(std::size_t index=count-1;index>0;--index)
            position=position+chain[index-1]->motion.position()*chain[index]->motion.rotation();
        result.globalPosition=from_region_handle(mImpl->handle)+LLVector3d(position);
        result.rotation=source.motion.rotation();if(count>1)result.rotation=result.rotation*chain[1]->motion.rotation();
    }
    return result;
}

std::optional<VSWorldScene::ObjectMetrics> VSWorldScene::objectMetrics(const ObjectHandle& token, bool requestLods)
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->objectMetrics(token, requestLods);
    auto& object = mImpl->objects.at(token.local);
    object.pruneMetricsRigging();
    if (object.pcode != LL_PCODE_VOLUME) return {};
    ObjectMetrics result;
    result.object = token;
    result.lod = object.assetLOD;
    result.mesh = object.params.isMeshSculpt();
    const auto count = [](const LLVolume& volume)
    {
        U64 triangles = 0;
        for (S32 face = 0; face < volume.getNumVolumeFaces(); ++face)
            triangles += U64(volume.getVolumeFace(face).mNumIndices) / 3;
        return triangles;
    };
    if (object.volume)
    {
        result.faces = U64(object.volume->getNumVolumeFaces());
        U64 vertices = 0;
        for (S32 face = 0; face < object.volume->getNumVolumeFaces(); ++face)
        {
            const auto& geometry = object.volume->getVolumeFace(face);
            vertices += U64(geometry.mNumVertices);
            result.rigged |= geometry.mWeights != nullptr;
        }
        result.vertices = vertices;
        result.triangles = count(*object.volume);
        // Exact non-rigged LLPanelObject::activateMeshFields radius formula.
        result.lodRadius = object.volume->mLODScaleBias.scaledVec(object.scale).length();
        Impl::CostTextures textures;
        const bool animated = (object.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG) != 0;
        U64 cost = mImpl->renderCost(mImpl,object, animated, true, textures);
        for (const auto& texture : textures) cost += U64(mImpl->textureCost(texture));
        result.renderCost = U32(std::min<U64>(cost, std::numeric_limits<U32>::max()));
    }
    for (const auto& face : object.admittedFaces)
    {
        if (!face.published || !face.mesh) continue;
        const auto description = VSWorldRenderer::geometryDescription(face.mesh);
        ++result.publishedFaces;
        result.publishedVertices += description.vertices;
        result.publishedTriangles += description.indices / 3;
        // Original LLFace angular-radius pixel-area formula, from genuine
        // currently admitted/skinned face bounds and the live native camera.
        if (const auto region = vs_region_context(token.host); region && gViewerWindow)
        {
            const auto bounds = VSWorldRenderer::bounds(face.mesh);
            LLVector3 low(bounds[0].data()), high(bounds[1].data());
            if (face.pickSurface && face.pickSurface->rigged)
            {
                const auto source = face.pickSurface->asset;
                auto* avatar = mImpl->skeletonOwner(object);
                const S32 index = face.pickSurface->face;
                if (!source || !source->volume || !avatar || index < 0) continue;
                if (object.metricsRigging.size() <= std::size_t(index)) object.metricsRigging.resize(std::size_t(index) + 1);
                auto& cached = object.metricsRigging[index];
                if (cached.first != source || !cached.second)
                {
                    LLSD metadata = source->skin; LLMeshSkinInfo skin(metadata);
                    auto info = VSAvatarRiggingInfo::create(*source->volume, skin,
                        [this](std::size_t bytes) { return reserveSpatialBytes(bytes); }, index);
                    if (!info) continue;
                    cached = {source, std::move(info)};
                }
                std::array<LLVector3, 2> rigged;
                if (!avatar->faceRiggingBounds(*cached.second, rigged)) continue;
                low = rigged[0]; high = rigged[1];
            }
            const LLVector3 size = (high - low) * 0.5f;
            const LLVector3 center = (high + low) * 0.5f;
            const LLVector3 eye = region->getPosRegionFromGlobal(gAgentCamera.getCameraPositionGlobal());
            const F32 distance = llmax((center - eye).length() - size.length(), 0.001f);
            const F32 radius = atanf(size.length() / distance) *
                (F32(gViewerWindow->getWindowHeightRaw()) / LLViewerCamera::getInstance()->getView());
            const F32 area = radius * radius * 3.14159f;
            result.pixelArea = llmax(result.pixelArea.value_or(0.f), area);
        }

    }
    // LLVOVolume::getCostData/getStreamingCost use this same actual mesh
    // header, primitive LOD triangle counts and shared LLMeshCostData formulas.
    if(object.volume)
    {
        LLMeshHeader header;
        bool known=false;
        if(result.mesh && object.asset)
        {
            for(S32 lod=0;lod<4;++lod)header.mLodSize[lod]=object.asset->lodBytes[lod];
            known=true;
        }
        else if(!result.mesh)
        {
            S32 counts[4]{};LLVolume::getLoDTriangleCounts(object.params,counts,object.volume);
            for(S32 lod=0;lod<4;++lod)header.mLodSize[lod]=counts[lod]*10;
            known=true;
        }
        LLMeshCostData costs;
        if(known && gMeshRepo.getCostData(header,costs))
        {
            const auto root=rootObject(token);
            const auto rootEntry=root?mImpl->objects.find(root->local):mImpl->objects.end();
            const bool animated=rootEntry!=mImpl->objects.end() &&
                (rootEntry->second.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG)!=0;
            const F32 overhead=animated && root && root->id==token.id?ANIMATED_OBJECT_BASE_COST:0.f;
            result.streamingCost=overhead+(result.mesh && animated && result.rigged?
                costs.getTriangleBasedStreamingCost():costs.getRadiusBasedStreamingCost(object.scale.length()*.5f));
            result.totalMeshBytes=costs.getSizeTotal();
            result.visibleMeshBytes=costs.getSizeByLOD(object.assetLOD>=0?object.assetLOD:0);
        }
        if(!result.mesh)
            for(S32 lod=0;lod<4;++lod)
            {
                if(object.params.getSculptID().isNull())
                {
                    LLVolume* source=LLPrimitive::getVolumeManager()->refVolume(object.params,lod);
                    if(source)result.lodTriangles[lod]=source->getNumTriangles();
                    if(source)LLPrimitive::getVolumeManager()->unrefVolume(source);
                }
                else result.lodTriangles[lod]=U64((31*2*31)>>(3*(3-lod)));
            }
    }
    if (result.mesh)
        for (S32 lod = 0; lod < 4; ++lod)
        {
            if (object.asset && object.asset->lodBytes[lod] == 0)
            {
                result.lodTriangles[lod] = 0; // The decoded asset header explicitly has no such LOD.
                continue;
            }
            if (requestLods) mImpl->assets->request(object.params, lod);
            if (const auto geometry = mImpl->assets->geometry(object.params.getSculptID(), lod); geometry && geometry->volume)
                result.lodTriangles[lod] = count(*geometry->volume);
        }
    if (result.rigged)
    {
        const auto* avatar = mImpl->skeletonOwner(object);
        result.lodRadius = avatar ? avatar->spatialBinRadius() : std::optional<F32>();
    }
    return result;
}

bool VSWorldScene::rebuildObject(const ObjectHandle& token)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->rebuildObject(token);
    std::array<U32,256> path{};
    // Exactly the reference LLManip::rebuild recursion: a non-volume is a
    // stopping point, including avatars and their attachment subtrees.
    const auto visit=[&](auto&& self,U32 local,std::size_t depth,bool publish)->bool
    {
        if(depth==path.size() || std::find(path.begin(),path.begin()+depth,local)!=path.begin()+depth)return false;
        const auto found=mImpl->objects.find(local);if(found==mImpl->objects.end())return false;
        auto& source=found->second;if(source.pcode!=LL_PCODE_VOLUME)return true;
        path[depth]=local;
        if(publish){source.dirty=true;source.revision=mImpl->nextRevision++;}
        const auto [begin,end]=mImpl->objectIndex.children(local);
        for(auto edge=begin;edge!=end;++edge)
        {
            const auto child=mImpl->objects.find(edge->second);if(child==mImpl->objects.end())continue;
            if(child->second.parentRetired || child->second.parent!=local ||
                (child->second.parentGeneration && child->second.parentGeneration!=source.generation))continue;
            if(!self(self,child->first,depth+1,publish))return false;
        }
        return true;
    };
    // Validate before invalidating any publication, preserving accepted scene
    // state when the current native ancestry cannot represent the operation.
    return visit(visit,token.local,0,false) && visit(visit,token.local,0,true);
}

std::optional<F32> VSWorldScene::objectLinksetRenderCost(const ObjectHandle& token) const
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->objectLinksetRenderCost(token);
    const auto& root = mImpl->objects.at(token.local);
    if (root.pcode != LL_PCODE_VOLUME || !root.volume) return {};
    const bool animated = (root.extendedMeshFlags & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG) != 0;
    Impl::CostTextures textures;
    F32 cost = F32(mImpl->renderCost(mImpl,root, animated, true, textures));
    const auto range = mImpl->objectIndex.children(token.local);
    for (auto i = range.first; i != range.second; ++i)
    {
        const auto child = mImpl->objects.find(i->second);
        if (child != mImpl->objects.end() && mImpl->linksCurrent(child->second) && child->second.pcode == LL_PCODE_VOLUME)
            cost += F32(mImpl->renderCost(mImpl,child->second, animated, false, textures));
    }
    for (const auto& texture : textures) cost += mImpl->textureCost(texture);
    return cost;
}

VSMaterialAssets* VSWorldScene::legacyMaterialAssets(const LLHost& host)
{
    if(mImpl->retired)return nullptr;
    if(host!=mImpl->host)
    {
        const auto region=mNeighbours.find(host);
        return region!=mNeighbours.end() ? region->second->legacyMaterialAssets(host) : nullptr;
    }
    if(!mImpl->materialAssets)mImpl->materialAssets=std::make_unique<VSMaterialAssets>(host);
    return mImpl->materialAssets.get();
}
void VSWorldScene::tickLegacyMaterials()
{
    if(mImpl->retired)return;
    if(mImpl->materialAssets)mImpl->materialAssets->tick();
    for(const auto& [host,region]:mNeighbours)region->tickLegacyMaterials();
}

std::optional<std::array<LLVector3,2>> VSWorldScene::publishedObjectBounds(const ObjectHandle& token,const LLVector3d& agentOrigin) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->publishedObjectBounds(token,agentOrigin);
    const auto& object=mImpl->objects.at(token.local);
    if(!mImpl->linksCurrent(object))return {};
    // Native object vertices are already in region space (including actual
    // flexible/rigged skinning), unlike the reference active spatial bridge.
    // Use precisely the accepted mesh generation, never official cube bounds.
    std::optional<std::array<LLVector3,2>> result;
    const LLVector3 offset=mImpl->hudObject(object)?LLVector3::zero:LLVector3(from_region_handle(mImpl->handle)-agentOrigin);
    const auto& faces=object.admittedFaces.empty()?object.faces:object.admittedFaces;
    for(const auto& face:faces)
    {
        if(!face.published || !face.mesh)continue;
        const auto bounds=VSWorldRenderer::bounds(face.mesh);
        const LLVector3 minimum=LLVector3(bounds[0].data())+offset,maximum=LLVector3(bounds[1].data())+offset;
        if(!minimum.isFinite() || !maximum.isFinite())return {};
        if(!result)result=std::array<LLVector3,2>{minimum,maximum};
        else for(S32 axis=0;axis<3;++axis)
        {(*result)[0].mV[axis]=llmin((*result)[0].mV[axis],minimum.mV[axis]);(*result)[1].mV[axis]=llmax((*result)[1].mV[axis],maximum.mV[axis]);}
    }
    return result;
}

std::optional<VSWorldScene::ObjectHandle> VSWorldScene::object(const LLHost& host, const LLUUID& identity) const
{
    if (mImpl->retired) return {};
    if (host != mImpl->host)
    {
        const auto region = mNeighbours.find(host);
        return region == mNeighbours.end() ? std::nullopt : region->second->object(host, identity);
    }
    const auto [begin, end] = mImpl->objectIndex.identities(identity);
    for (auto iter = begin; iter != end; ++iter)
    {
        const auto found = mImpl->objects.find(iter->second);
        if (found != mImpl->objects.end() && mImpl->linksCurrent(found->second)) return object(host, iter->second);
    }
    return {};
}

VSWorldScene::ResolvedTexture VSWorldScene::resolveTexture(const ObjectHandle& token,const LLUUID& id,bool includePixels) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->resolveTexture(token,id,includePixels);
    const auto& object=mImpl->objects.at(token.local);
    if(!mImpl->linksCurrent(object))return {};
    const auto state=mImpl;
    Impl::TextureSource source{state,object,guard(token)};return source.resolve(id,true,true,includePixels);
}
VSWorldScene::ResolvedTexture VSWorldScene::diffuseTexture(const ObjectHandle& token,U32 side,bool includePixels) const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->diffuseTexture(token,side,includePixels);
    const auto& object=mImpl->objects.at(token.local);
    if(side>=object.faces.size() || !mImpl->linksCurrent(object))return {};
    if(object.media && object.faces[side].material.hasMedia())
    {
        const auto id=object.media->texture(side);const auto key="media:"+id.asString();
        if(const auto image=mImpl->resources.image(key))
            return {true,false,id,image,includePixels?mImpl->resources.rawImage(key):nullptr,mImpl->resources.generation(key)};
    }
    return resolveTexture(token,object.faces[side].material.getID(),includePixels);
}

void VSWorldScene::appendObjectBeacons(std::vector<VSWorldRenderer::Packet>& packets,std::vector<DebugBox>& boxes)
{
    const bool debugLights = gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_LIGHTS);
    const bool debugBoxes = gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_BBOXES);
    if (mImpl->retired || (!debugLights && !debugBoxes && !LLPipeline::getRenderBeacons() && !LLPipeline::getRenderHighlights())) return;
    const VSObjectBeaconFlags flags{LLPipeline::getRenderScriptedTouchBeacons(), LLPipeline::getRenderScriptedBeacons(),
        LLPipeline::getRenderPhysicalBeacons(), LLPipeline::getRenderMOAPBeacons(),
        LLPipeline::getRenderParticleBeacons(), LLPipeline::getRenderSoundBeacons()};
    struct Row
    {
        ObjectHandle object;
        LLVector3 position;
        std::array<bool,5> categories;
        std::vector<std::size_t> faces,allFaces;
    };
    std::map<std::pair<LLUUID,U64>,Row> rows;
    const auto count = packets.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& packet = packets[index];
        if (!packet.mesh || packet.background || packet.terrain || packet.overlay || packet.glowOnly) continue;
        LLUUID identity;
        std::copy(packet.objectIdentity.begin(), packet.objectIdentity.end(), identity.mData);
        if (identity.isNull()) continue;
        const auto key = std::make_pair(identity,packet.objectGeneration);
        auto row = rows.find(key);
        if (row == rows.end())
        {
            // Reuse the actual source generation submitted this frame. A UUID
            // match alone must never move old feedback onto a replacement.
            const auto token=packetObject(packet);
            const VSWorldScene* region=token && token->host!=mImpl->host?mNeighbours.at(token->host).get():this;
            if (!token || !current(*token)) continue;
            const auto& object=region->mImpl->objects.at(token->local);
            struct Peer
            {
                const Impl::Object& object;
                bool isAvatar() const { return object.pcode==LL_PCODE_LEGACY_AVATAR; }
                bool getParent() const { return object.parent!=0; }
                bool flagScripted() const { return (object.updateFlags&FLAGS_SCRIPTED)!=0; }
                bool flagHandleTouch() const { return (object.updateFlags&FLAGS_HANDLE_TOUCH)!=0; }
                bool flagUsePhysics() const { return (object.updateFlags&FLAGS_USE_PHYSICS)!=0; }
                bool hasMedia() const
                {
                    return std::any_of(object.admittedFaces.begin(),object.admittedFaces.end(),
                        [](const auto& face){return face.publishedMaterial.hasMedia();});
                }
                bool isParticleSource() const { return bool(object.particles); }
                bool isAudioSource() const { return gAudiop && gAudiop->findAudioSource(object.id); }
            } peer{object};
            const auto transform=objectTransform(*token);
            if (!transform) continue;
            row=rows.emplace(key,Row{*token,gAgent.getPosAgentFromGlobal(transform->position),
                vs_object_beacon_categories(peer,flags),{},{}}).first;
        }
        // The original LLFace::renderSelected has no rigged-face draw path.
        const auto* region=row->second.object.host==mImpl->host?this:mNeighbours.at(row->second.object.host).get();
        const auto& object=region->mImpl->objects.at(row->second.object.local);
        const bool rigged=packet.faceIndex<object.admittedFaces.size() && object.admittedFaces[packet.faceIndex].pickSurface &&
            object.admittedFaces[packet.faceIndex].pickSurface->rigged;
        row->second.allFaces.push_back(index);
        if (!rigged) row->second.faces.push_back(index);
    }
    constexpr std::array<std::array<F32,4>,4> colors{{{1,0,0,.5f},{0,1,0,.5f},{1,1,1,.5f},{.5f,.5f,1,.5f}}};
    const bool highlight = LLPipeline::getRenderHighlights() &&
        gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_SELECTED);
    for (unsigned category=0; category<5; ++category)
        for (const auto& [identity,row] : rows)
        {
            if (!row.categories[category] || !current(row.object)) continue;
            // Sound beacons come from every actual audio-engine source below,
            // including sources whose object is not visible. Only its highlight
            // uses visible drawables, exactly like original postSort.
            if (category<4 && LLPipeline::getRenderBeacons())
            {
                const auto& color=colors[category];
                gObjectList.addDebugBeacon(row.position,"",LLColor4(color.data()),LLColor4(1,1,1,.5f),LLPipeline::DebugBeaconLineWidth);
            }
            if (highlight)
                for (const auto index : row.faces)
                {
                    auto packet=packets[index];
                    packet.image.reset();packet.channels={};packet.surface.reset();
                    // sNullImagep is the original 1x1 RGB 0x77 texture, and
                    // highlightF emits color*texture without lighting/gamma.
                    packet.color={119.f/255.f,0,0,.5f};
                    packet.constantColor=true;packet.fullbright=true;packet.alpha=true;packet.masked=false;
                    packet.overlay=true;packet.depthWriteAlpha=true;
                    packet.blend=VSWorldRenderer::Blend{7,9,0,9,7};
                    packets.push_back(std::move(packet));
                }
        }
    for (const auto& [identity,row] : rows)
    {
        if ((!debugLights && !debugBoxes) || row.allFaces.empty() || !current(row.object)) continue;
        const auto* region=row.object.host==mImpl->host?this:mNeighbours.at(row.object.host).get();
        const auto& object=region->mImpl->objects.at(row.object.local);
        const bool volume=object.pcode==LL_PCODE_VOLUME;
        const bool light=volume && object.edit && object.edit->inUse(LLNetworkData::PARAMS_LIGHT);
        std::array<std::array<float,3>,2> objectBounds{};
        bool first=true;
        const bool hud=packets[row.allFaces.front()].hud;
        std::array<float,4> color{1,0,1,1};
        if (volume) color=object.parent?std::array<float,4>{0,1,0,1}:std::array<float,4>{1,1,0,1};
        else if (object.pcode==LL_PCODE_LEGACY_TREE) color={0,.5f,0,1};
        for (const auto index : row.allFaces)
        {
            const auto& visible=packets[index];
            auto bounds=VSWorldRenderer::bounds(visible.mesh);
            for (unsigned axis=0;axis<3;++axis)
                for (unsigned edge=0;edge<2;++edge) bounds[edge][axis]+=visible.regionOffset[axis];
            if (first) { objectBounds=bounds; first=false; }
            else for (unsigned axis=0;axis<3;++axis)
            { objectBounds[0][axis]=llmin(objectBounds[0][axis],bounds[0][axis]);objectBounds[1][axis]=llmax(objectBounds[1][axis],bounds[1][axis]); }
            if (debugBoxes && volume) boxes.push_back({bounds,color,1,true,hud});
            if (debugLights && light)
            {
                auto feedback=visible;
                feedback.image.reset();feedback.channels={};feedback.surface.reset();
                feedback.color={0,1,1,.5f};feedback.constantColor=true;feedback.fullbright=true;feedback.alpha=true;feedback.masked=false;
                feedback.overlay=true;feedback.depthWriteAlpha=true;feedback.blend=VSWorldRenderer::Blend{7,9,0,9,7};
                packets.push_back(std::move(feedback));
            }
        }
        if (debugBoxes)
        {
            const bool active = !volume || !(F_APPROXIMATELY_ZERO>=object.velocity.lengthSquared() &&
                F_APPROXIMATELY_ZERO>=object.acceleration.lengthSquared() && F_APPROXIMATELY_ZERO>=object.angularVelocity.lengthSquared());
            const F32 width=active?llmax(4.f*sinf(gFrameTimeSeconds*2.f)+1.f,1.f):1.f;
            boxes.push_back({objectBounds,color,width,true,hud});
        }
        if (debugLights && light)
        {
            boxes.push_back({objectBounds,{1,1,1,1},1,false,hud});
            auto radiusBounds=objectBounds;
            const auto radius=object.edit->light.getRadius();
            for (unsigned axis=0;axis<3;++axis)
            { const auto center=(objectBounds[0][axis]+objectBounds[1][axis])*.5f;radiusBounds[0][axis]=center-radius;radiusBounds[1][axis]=center+radius; }
            boxes.push_back({radiusBounds,{1,1,0,1},1,true,hud});
        }
    }

}

bool VSWorldScene::objectVertsVisible(const ObjectHandle& token,const LLCamera& camera,bool allVerts,
                                      const LLVector3d& agentOrigin)const
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectVertsVisible(token,camera,allVerts,agentOrigin);
    const auto& object=mImpl->objects.at(token.local);
    if(!mImpl->linksCurrent(object) || object.pcode!=LL_PCODE_VOLUME || !object.volume)return false;
    const auto frame=focusObject(token,agentOrigin);if(!frame)return false;
    // Flexible LLVolume geometry is already scaled by its shared simulation;
    // its original relative transform deliberately uses unit scale.
    const auto relative=object.flexible?frame->renderMatrix:frame->assetToAgentMatrix;
    return vs_volume_vertices_visible(*object.volume,relative,LLMatrix4(),false,allVerts,
        [&](const LLVector3& point){return camera.pointInFrustum(point)>0;});
}
std::optional<std::array<LLVector3,2>> VSWorldScene::sourceObjectBounds(const ObjectHandle& token,
                                                                    const LLVector3d& agentOrigin)const
{
    if(!current(token))return {};
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->sourceObjectBounds(token,agentOrigin);
    const auto& object=mImpl->objects.at(token.local);
    if(!mImpl->linksCurrent(object) || object.pcode!=LL_PCODE_VOLUME || !object.volume)return {};
    const auto frame=focusObject(token,agentOrigin);if(!frame)return {};
    const auto relative=object.flexible?frame->renderMatrix:frame->assetToAgentMatrix;
    std::optional<std::array<LLVector3,2>> result;
    // LLVolumeFace owns the true CPU mesh/sculpt/flexible extrema. Transform
    // those extrema conservatively; texture or GPU upload refusal must not
    // remove an otherwise valid CPU selection candidate.
    for(S32 face=0;face<object.volume->getNumVolumeFaces();++face)
    {
        const auto& source=object.volume->getVolumeFace(face);
        if(!source.mExtents || source.mNumVertices<=0)continue;
        for(unsigned corner=0;corner<8;++corner)
        {
            LLVector3 local;
            for(unsigned axis=0;axis<3;++axis)local.mV[axis]=source.mExtents[(corner>>axis)&1].getF32ptr()[axis];
            const auto point=local*relative;
            if(!point.isFinite())return {};
            if(!result)result=std::array<LLVector3,2>{point,point};
            else for(unsigned axis=0;axis<3;++axis)
            {(*result)[0].mV[axis]=llmin((*result)[0].mV[axis],point.mV[axis]);(*result)[1].mV[axis]=llmax((*result)[1].mV[axis],point.mV[axis]);}
        }
    }
    return result;
}

std::optional<VSWorldScene::ObjectHandle> VSWorldScene::packetObject(const VSWorldRenderer::Packet& packet)const
{
    const auto& source=packet.objectSource;
    if(source[0]>U32_MAX || source[1]>65535 || source[2]>U32_MAX || mImpl->retired)return {};
    const LLHost host{U32(source[0]),U32(source[1])};
    const VSWorldScene* region=this;
    if(host!=mImpl->host)
    {
        const auto found=mNeighbours.find(host);if(found==mNeighbours.end())return {};
        region=found->second.get();
    }
    const auto token=region->object(host,U32(source[2]));
    if(!token || token->region!=source[3] || token->accountGeneration!=source[4] ||
       token->generation!=packet.objectGeneration ||
       !std::equal(packet.objectIdentity.begin(),packet.objectIdentity.end(),token->id.mData) ||
       !std::equal(packet.objectAccount.begin(),packet.objectAccount.end(),token->account.mData) ||
       !current(FaceHandle{*token,packet.faceIndex,packet.faceGeneration}))return {};
    return token;
}

std::optional<VSWorldScene::SelectionCandidates> VSWorldScene::selectionCandidates(const LLCamera& camera,
                                                                                const LLVector3d& agentOrigin)const
{
    const auto state=mImpl;
    if(state->retired)return {};
    // Create/rebuild acceleration only on actual demand and changed available
    // ownership; unchanged refusals must not become an allocation hot loop.
    if((!state->selectionIndex && !state->selectionIndexAttempted) || ((!state->selectionIndex || !state->selectionIndex->valid()) &&
       (state->selectionRefusedBytes!=state->budgets->primitiveBytes.used() || state->selectionRefusedRevision!=state->nextRevision)))
    {
        state->selectionIndexAttempted=true;
        try
        {
            const auto budgets=state->budgets;
            state->selectionIndex=std::make_unique<VSSpatialIndex>([budgets](std::size_t bytes){return budgets->primitiveBytes.reserve(bytes);});
            ++state->selectionIndexEpoch;state->selectionIndexRevision=0;
        }
        catch(const std::bad_alloc&)
        {state->selectionIndex.reset();state->selectionRefusedBytes=state->budgets->primitiveBytes.used();state->selectionRefusedRevision=state->nextRevision;}
    }
    if(state->selectionIndex && state->selectionIndex->valid() && state->selectionIndexRevision!=state->nextRevision)
    {
        // Intake/editor source changes can arrive between presentations. Their
        // real CPU state is authoritative even before a replacement GPU mesh.
        for(auto& [local,object]:state->objects)
        {
            LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
            if(state->transform(local,position,rotation,visited))state->updateSelectionIndex(local,object,position,rotation);
            else state->selectionIndex->erase(local);
            if(!state->selectionIndex->valid())break;
        }
        if(state->selectionIndex->valid())state->selectionIndexRevision=state->nextRevision;
    }
    const LLVector3 offset(from_region_handle(state->handle)-agentOrigin);
    VSSpatialIndex::Bounds bounds{};
    for(unsigned corner=0;corner<8;++corner)
    {
        const auto point=camera.mAgentFrustum[corner]-offset;
        for(unsigned axis=0;axis<3;++axis)
        {
            if(!std::isfinite(point.mV[axis]))return {};
            if(!corner)bounds[0][axis]=bounds[1][axis]=point.mV[axis];
            else {bounds[0][axis]=std::min(bounds[0][axis],point.mV[axis]);bounds[1][axis]=std::max(bounds[1][axis],point.mV[axis]);}
        }
    }
    const auto query=state->selectionIndex?state->selectionIndex->query(bounds):std::nullopt;
    const std::size_t count=query?query->keys.size():state->objects.size();
    SelectionCandidates result;
    if(count)
    {
        if(count>std::numeric_limits<std::size_t>::max()/sizeof(ObjectHandle))return {};
        result.residency=state->budgets->primitiveBytes.reserve(count*sizeof(ObjectHandle));
        if(!result.residency)return {};
        try {result.objects.reserve(count);}catch(const std::bad_alloc&){return {};}
    }
    const auto append=[&](U32 local)
    {
        const auto found=state->objects.find(local);
        if(found==state->objects.end() || found->second.pcode!=LL_PCODE_VOLUME || !found->second.renderable ||
           !found->second.volume || !state->linksCurrent(found->second))return;
        if(const auto token=object(state->host,local))result.objects.push_back(*token);
    };
    if(query)for(const auto local:query->keys)append(local);
    else for(const auto& [local,source]:state->objects)append(local);
    // Each circuit owns a genuine index and local coordinate frame. Keep the
    // immutable per-circuit candidate snapshots charged during combination.
    for(const auto& [host,neighbour]:mNeighbours)
    {
        auto next=neighbour->selectionCandidates(camera,agentOrigin);if(!next)return {};
        if(next->objects.size()>std::numeric_limits<std::size_t>::max()/sizeof(ObjectHandle)-result.objects.size())return {};
        const auto size=result.objects.size()+next->objects.size();
        if(size==result.objects.size())continue;
        auto residency=state->budgets->primitiveBytes.reserve(size*sizeof(ObjectHandle));if(!residency)return {};
        try
        {
            std::vector<ObjectHandle> combined;combined.reserve(size);
            combined.insert(combined.end(),result.objects.begin(),result.objects.end());
            combined.insert(combined.end(),next->objects.begin(),next->objects.end());
            result.objects=std::move(combined);result.residency=std::move(residency);
        }
        catch(const std::bad_alloc&){return {};}
    }
    return state->retired?std::nullopt:std::optional<SelectionCandidates>(std::move(result));
}

// Temporary selected-Blinn-Phong display state is separate from authorized
// texture-picker rollback and never sends or alters simulator material state.
bool VSWorldScene::legacyMaterialPreview(const ObjectHandle& token) const
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->legacyMaterialPreview(token);
    return bool(mImpl->objects.at(token.local).legacyPreview);
}
bool VSWorldScene::setLegacyMaterialPreview(const ObjectHandle& token,bool hidden)
{
    if(!current(token))return false;
    if(token.host!=mImpl->host)return mNeighbours.at(token.host)->setLegacyMaterialPreview(token,hidden);
    auto& object=mImpl->objects.at(token.local);
    if(object.pcode!=LL_PCODE_VOLUME)return true;
    if(hidden==bool(object.legacyPreview))return true;
    if(hidden)
    {
        const auto revision=object.revision;
        const auto count=object.faces.size();if(!count || count>45)return false;
        const auto bytes=sizeof(Impl::Object::LegacyMaterialPreview)+count*(sizeof(LLUUID)+2*sizeof(LLPointer<LLGLTFMaterial>)+sizeof(LLGLTFMaterial));
        auto residency=reserveSpatialBytes(bytes);if(!residency || !current(token) || object.revision!=revision)return false;
        auto preview=std::make_shared<Impl::Object::LegacyMaterialPreview>();preview->residency=std::move(residency);
        preview->ids.reserve(count);preview->bases.reserve(count);preview->overrides.reserve(count);
        for(U32 side=0;side<count;++side)
        {
            preview->ids.push_back(object.renderMaterials.getMaterial(U8(side)));
            preview->bases.emplace_back(object.faces[side].material.getGLTFMaterial());
            const auto* value=object.faces[side].material.getGLTFMaterialOverride();
            preview->overrides.emplace_back(value?new LLGLTFMaterial(*value):nullptr);
        }
        if(!current(token) || object.revision!=revision)return false;
        object.legacyPreview=std::move(preview);
    }
    else
    {
        const auto preview=std::move(object.legacyPreview);
        for(U32 side=0;side<std::min(object.faces.size(),preview->ids.size());++side)
        {
            const auto& id=preview->ids[side];
            object.renderMaterials.setMaterial(U8(side),id);
            auto& entry=object.faces[side].material;
            entry.setGLTFMaterial(preview->bases[side]);
            entry.setGLTFMaterialOverride(preview->overrides[side]);
            entry.setGLTFRenderMaterial(nullptr);
        }
    }
    object.dirty=true;object.revision=mImpl->nextRevision++;
    return true;
}

void VSWorldScene::appendAlphaHighlights(std::vector<VSWorldRenderer::Packet>& packets)
{
    if(mImpl->retired || !LLDrawPoolAlpha::sShowDebugAlpha || gCubeSnapshot)return;
    vs_request_world_image(IMG_SMOKE,1024);
    const auto smoke=mImpl->resources.image("remote:"+vs_native_image_id(IMG_SMOKE).asString());
    if(!smoke)return;
    // Original renderDebugAlpha pass order: both blend variants first, then
    // ordinary/invisible-alpha and material-mask batches for each rigging mode.
    std::array<std::vector<VSWorldRenderer::Packet>,8> passes;
    const auto count=packets.size();
    for(std::size_t i=0;i<count;++i)
    {
        const auto& source=packets[i];
        if(!source.mesh || source.overlay || source.background || source.terrain || source.glowOnly || source.gltfNode>=0)continue;
        const auto token=packetObject(source);if(!token)continue;
        const auto* region=token->host==mImpl->host?this:mNeighbours.at(token->host).get();
        const auto& object=region->mImpl->objects.at(token->local);
        if(object.pcode!=LL_PCODE_VOLUME || source.faceIndex>=object.admittedFaces.size())continue;
        const auto& face=object.admittedFaces[source.faceIndex];
        const bool rigged=face.pickSurface && face.pickSurface->rigged;
        if(rigged && !LLDrawPoolAlpha::sShowDebugAlphaRigged)continue;
        if(!source.alpha && !source.masked && !source.alphaOnly)continue;
        if(source.alphaOnly)
        {
            const auto admission=region->objectAdmission(*token);
            if(!admission || admission->attachment || rigged)continue;
        }
        const bool invisibleAlpha=face.publishedMaterial.getColor().mV[3]<=0;
        const bool materialMask=source.masked &&
            ((source.surface && source.surface->pbr) || face.publishedMaterial.getFullbright() ||
             face.publishedMaterial.getMaterialParams().notNull() || !face.publishedMaterial.getMaterialID().isNull());
        const unsigned category=source.alphaOnly?3:source.alpha && !invisibleAlpha?0:materialMask?2:1;
        auto packet=source;
        packet.channels={};packet.surface.reset();packet.image=smoke;
        packet.imageSampler.mipmaps=true;
        packet.imageSampler.anisotropy=gSavedSettings.getBOOL("RenderAnisotropic")?16:1;
        packet.alphaOnly=false;packet.alpha=true;packet.masked=false;packet.fullbright=true;packet.constantColor=false;
        packet.depthWrite=false;packet.overlay=true;
        packet.cull=source.cull.value_or(source.surface && !source.doubleSided && !source.background);
        packet.blend=VSWorldRenderer::Blend{7,9,0,9,7};
        packet.color=category==3?std::array<float,4>{0,1,0,1}:category==0?(rigged?std::array<float,4>{1,.5f,0,1}:std::array<float,4>{1,0,0,1}):
                     category==2?(rigged?std::array<float,4>{0,1,1,1}:std::array<float,4>{0,0,1,1}):
                     rigged?std::array<float,4>{0,1,0,1}:std::array<float,4>{1,0,0,1};
        const auto& entry=face.publishedMaterial;
        packet.highlightUV=VSMaterial::legacyUV(entry.getScaleS(),entry.getScaleT(),entry.getOffsetS(),entry.getOffsetT(),entry.getRotation());
        if(face.pickTextureMatrix)
        {
            const auto& matrix=*face.pickTextureMatrix;
            const auto origin=LLVector3::zero*matrix,x=LLVector3::x_axis*matrix-origin,y=LLVector3::y_axis*matrix-origin;
            packet.highlightUV=VSMaterial::Transform{{x.mV[0],x.mV[1],y.mV[0],y.mV[1]},{origin.mV[0],origin.mV[1]}};
        }
        const unsigned pass=category==0?(rigged?1:0):category==1?(rigged?5:2):category==2?(rigged?6:3):(rigged?7:4);
        passes[pass].push_back(std::move(packet));
    }
    for(unsigned pass=0;pass<2;++pass)
        std::stable_sort(passes[pass].begin(),passes[pass].end(),[](const auto& a,const auto& b)
            {return a.distanceSquared>b.distanceSquared;});
    for(auto& pass:passes)for(auto& packet:pass)packets.push_back(std::move(packet));
}

std::optional<bool> VSWorldScene::objectSelectable(const ObjectHandle& token) const
{
    if (!current(token)) return {};
    if (token.host != mImpl->host) return mNeighbours.at(token.host)->objectSelectable(token);
    return mImpl->objects.at(token.local).selectable;
}

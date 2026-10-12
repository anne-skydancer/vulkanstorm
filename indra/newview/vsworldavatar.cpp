// CPU avatar owner and native geometry publication. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldavatar.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "llimage.h"
#include "llavatarappearance.h"
#include "llwearabledata.h"
#include "llavatarjointmesh.h"
#include "llpolymesh.h"
#include "llpolymorph.h"
#include "llprimitive.h"
#include "lltextureentry.h"
#include "lltexlayer.h"
#include "llmatrix4a.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llui.h"
#include "llquantize.h"
#include "llvolume.h"
#include "llmodel.h"
#include "llregionhandle.h"
#include "message.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
using namespace LLAvatarAppearanceDefines;
namespace
{
bool ownsAppearanceClass=false;
class NativeJoint final : public LLAvatarJoint
{
public:
    NativeJoint()=default;
    explicit NativeJoint(S32 number):LLAvatarJoint(number) {}
    U32 render(F32,bool,bool) override { return 0; } // Geometry is published separately.
};
class NativeJointMesh final : public LLAvatarJointMesh
{
public:
    U32 render(F32,bool,bool) override { return 0; }
};
class NativeLayers final : public LLTexLayerSet
{
public:
    bool pending=true;
    explicit NativeLayers(LLAvatarAppearance* a):LLTexLayerSet(a) {}
    void requestUpdate() override { pending=true; }
    void createComposite() override { pending=true; }
};
bool finite(const LLVector3& v)
{ return std::isfinite(v.mV[0]) && std::isfinite(v.mV[1]) && std::isfinite(v.mV[2]); }
}
struct VSWorldAvatar::Impl : LLWearableData, LLAvatarAppearance
{
    friend class VSWorldAvatar;
    LLUUID id;
    bool self=false,positionKnown=false,appearanceKnown=false,serverBakes=false;
    U64 handle=0;
    VSWorldRenderer& renderer;
    VSUIResources& resources;
    GroundQuery ground;
    LLVector3 position,velocity,angularVelocity,hover;
    LLQuaternion rotation;
    F32 pixelArea=0,timeDilation=1;
    std::map<EBakedTextureIndex,LLPointer<LLImageRaw>> appliedMasks;
    S32 cofVersion=-1;
    LLPrimitive textures;
    std::map<LLUUID,S32> animations;
    std::map<unsigned,std::unique_ptr<LLJoint>> attachments;
    std::map<LLUUID,unsigned> simAttachments;
    std::map<S32,VSWorldRenderer::Mesh> meshes;
    bool shapeDirty=true;
    Impl(const LLUUID& uuid,bool is_self,U64 region,VSWorldRenderer& r,VSUIResources& u,GroundQuery g):
        LLWearableData(),LLAvatarAppearance(static_cast<LLWearableData*>(this)),id(uuid),self(is_self),handle(region),renderer(r),resources(u),ground(std::move(g))
    {
        setAvatarAppearance(this);textures.setNumTEs(TEX_NUM_INDICES);
        initInstance();
        if (!isBuilt()) throw std::runtime_error("Native avatar character assets failed to load");
        for (const auto* info:sAvatarXmlInfo->mAttachmentInfoList)
        {
            if (info->mAttachmentID<=0 || info->mIsHUDAttachment) continue;
            auto* parent=mRoot->findJoint(info->mJointName);
            if (!parent) continue;
            auto joint=std::make_unique<NativeJoint>();joint->setName(info->mName);parent->addChild(joint.get());
            if (info->mHasPosition) joint->setPosition(info->mPosition);
            if (info->mHasRotation)
            {
                LLQuaternion q;q.setQuat(info->mRotationEuler.mV[0]*DEG_TO_RAD,info->mRotationEuler.mV[1]*DEG_TO_RAD,info->mRotationEuler.mV[2]*DEG_TO_RAD);
                joint->setRotation(q);
            }
            attachments[unsigned(info->mAttachmentID)]=std::move(joint);
        }
        LLCharacter::sInstances.push_back(this);
    }
    ~Impl() override { LLCharacter::sInstances.remove(this);flushAllMotions();attachments.clear(); }
    bool usesGraphicsContext() const override { return false; }
    bool isSelf() const override { return self; }
    bool isUsingLocalAppearance() const override { return false; }
    bool isEditingAppearance() const override { return false; }
    bool isUsingServerBakes() const override { return serverBakes; }
    LLAvatarJoint* createAvatarJoint() override { return new NativeJoint; }
    LLAvatarJoint* createAvatarJoint(S32 number) override { return new NativeJoint(number); }
    LLAvatarJointMesh* createAvatarJointMesh() override { return new NativeJointMesh; }
    LLTexLayerSet* createTexLayerSet() override { return new NativeLayers(this); }
    void bodySizeChanged() override { shapeDirty=true; }
    void dirtyMesh() override { shapeDirty=true; }
    void dirtyMesh(S32) override { shapeDirty=true; }
    void updateMeshTextures() override { shapeDirty=true; }
    void invalidateComposite(LLTexLayerSet* set,bool) override { if (set) set->requestUpdate();shapeDirty=true; }
    void onGlobalColorChanged(const LLTexGlobalColor*,bool) override { shapeDirty=true; }
    bool isTextureDefined(ETextureIndex te,U32) const override
    {
        if (unsigned(te)>=textures.getNumTEs()) return false;
        const LLUUID& uuid=textures.getTE(U8(te))->getID();return uuid.notNull() && uuid!=IMG_DEFAULT_AVATAR;
    }
    void applyMorphMask(const U8* data,S32 w,S32 h,S32 components,EBakedTextureIndex baked) override
    {
        if (unsigned(baked)>=mBakedTextureDatas.size() || !data || w<=0 || h<=0 || components<1 || components>4) return;
        for (const auto* masked:mBakedTextureDatas[baked].mMaskedMorphs)
            if (auto* morph=dynamic_cast<LLPolyMorphTarget*>(masked->mMorphTarget)) morph->applyMask(data,w,h,components,masked->mInvert);
        shapeDirty=true;
    }
    LLVector3 getCharacterPosition() override { return position; }
    LLQuaternion getCharacterRotation() override { return rotation; }
    LLVector3 getCharacterVelocity() override { return velocity; }
    LLVector3 getCharacterAngularVelocity() override { return angularVelocity; }
    void getGround(const LLVector3& input,LLVector3& output,LLVector3& normal) override
    { if (!ground(input,output,normal)) {output=input;normal=LLVector3::zero;} }
    F32 getTimeDilation() override { return timeDilation; }
    F32 getPixelArea() const override { return pixelArea; }
    LLVector3d getPosGlobalFromAgent(const LLVector3& p) override { return from_region_handle(handle)+LLVector3d(p); }
    LLVector3 getPosAgentFromGlobal(const LLVector3d& p) override { return LLVector3(p-from_region_handle(handle)); }
    void addDebugText(const std::string&) override {}
    const LLUUID& getID() const override { return id; }
    void pose()
    {
        LLVector3 root=position+hover;
        root.mV[2]-=.5f*mBodySize.mV[2]-mPelvisToFoot;
        mRoot->setWorldPosition(root);mRoot->setWorldRotation(rotation);
        updateMotions(NORMAL_UPDATE);mRoot->updateWorldMatrixChildren();
    }
    bool skin(LLAvatarJointMesh& part,std::vector<VSWorldRenderer::Vertex>& vertices,std::vector<unsigned>& indices)
    {
        auto* mesh=part.getMesh();if (!mesh || !mesh->getNumVertices() || mesh->getNumVertices()>65536 || mesh->getNumFaces()>131072) return false;
        auto* reference=mesh->getReferenceMesh();const auto& joints=reference->mJointRenderData;
        std::vector<LLMatrix4> palette;std::vector<LLVector3> pivots;
        if (mesh->hasWeights())
        {
            if (joints.empty() || joints.size()>256) return false;
            bool last=false;
            for (auto* data:joints)
            {
                palette.push_back(*data->mWorldMatrix);
                if (data->mSkinJoint)
                {
                    if (!last) pivots.push_back(data->mSkinJoint->mRootToParentJointSkinOffset);
                    pivots.push_back(data->mSkinJoint->mRootToJointSkinOffset);last=true;
                }
                else last=false;
            }
            if (pivots.size()!=palette.size()) return false;
            for (unsigned i=0;i<palette.size();++i) palette[i].translate(pivots[i]*palette[i].getMat3());
        }
        std::vector<LLMatrix4a> aligned(palette.size());
        for (unsigned i=0;i<palette.size();++i) aligned[i].loadu(palette[i]);
        LLMatrix4a rigid;rigid.loadu(part.getWorldMatrix());
        for (unsigned i=0;i<mesh->getNumVertices();++i)
        {
            LLMatrix4a blend=rigid;
            if (mesh->hasWeights())
            {
                const float weight=mesh->getWeights()[i];
                if (!std::isfinite(weight) || weight<0 || weight>=float(aligned.size())) return false;
                const unsigned joint=unsigned(weight);const float w=weight-joint;
                if (w && joint+1>=aligned.size()) return false;
                if (w) blend.setLerp(aligned[joint],aligned[joint+1],w);else blend=aligned[joint];
            }
            LLVector4a p,n;blend.affineTransform(mesh->getCoords()[i],p);blend.rotate(mesh->getNormals()[i],n);
            LLVector3 normal(n.getF32ptr());normal.normalize();const auto* c=p.getF32ptr();const auto& uv=mesh->getTexCoords()[i];
            vertices.push_back({c[0],c[1],c[2],normal.mV[0],normal.mV[1],normal.mV[2],uv.mV[0],1-uv.mV[1]});
        }
        for (S32 f=0;f<mesh->getNumFaces();++f) for (unsigned c=0;c<3;++c)
        {
            const S32 index=mesh->getFaces()[f][c];if (index<0 || unsigned(index)>=vertices.size()) return false;indices.push_back(unsigned(index));
        }
        return !indices.empty();
    }
};
void VSWorldAvatar::initializeClass()
{
    if (!LLAvatarAppearance::getDictionary()) { LLAvatarAppearance::initClass("avatar_lad.xml","avatar_skeleton.xml");ownsAppearanceClass=true; }
}
void VSWorldAvatar::cleanupClass()
{
    if (ownsAppearanceClass) { LLAvatarAppearance::cleanupClass();LLPolyMesh::freeAllMeshes();ownsAppearanceClass=false; }
}
VSWorldAvatar::VSWorldAvatar(const LLUUID& id,bool self,U64 region,VSWorldRenderer& r,VSUIResources& u,GroundQuery g):
    mImpl(std::make_unique<Impl>(id,self,region,r,u,std::move(g))) {}
VSWorldAvatar::~VSWorldAvatar()=default;
void VSWorldAvatar::transform(const LLVector3& p,const LLQuaternion& r,const LLVector3& v,const LLVector3& a)
{
    if (!finite(p) || !r.isFinite() || !finite(v) || !finite(a)) return;
    mImpl->position=p;mImpl->rotation=r;mImpl->velocity=v;mImpl->angularVelocity=a;mImpl->positionKnown=true;mImpl->pose();
}
bool VSWorldAvatar::appearance(LLMessageSystem& msg)
{
    auto& a=*mImpl;LLUUID id;msg.getUUID("Sender","ID",id);if (id!=a.id) return false;
    U8 version=0;S32 cof=-1;
    if (msg.getNumberOfBlocks("AppearanceData")) {msg.getU8("AppearanceData","AppearanceVersion",version);msg.getS32("AppearanceData","CofVersion",cof);}
    if (version>1 || (version && cof<a.cofVersion)) return false;
    LLPrimitive textures;textures.setNumTEs(TEX_NUM_INDICES);
    if (textures.unpackTEMessage(&msg,LLMessageStringTable::getInstance()->getString("ObjectData"),0)==TEM_INVALID) return false;
    const S32 count=msg.getNumberOfBlocks("VisualParam");if (count<0 || count>1024) return false;
    std::vector<std::pair<LLVisualParam*,F32>> params;
    auto* param=a.getFirstVisualParam();
    for (S32 i=0;i<count;++i)
    {
        while (param && param->getGroup()!=VISUAL_PARAM_GROUP_TWEAKABLE && param->getGroup()!=VISUAL_PARAM_GROUP_TRANSMIT_NOT_TWEAKABLE) param=a.getNextVisualParam();
        if (!param) break;
        U8 value;msg.getU8("VisualParam","ParamValue",value,i);
        params.emplace_back(param,U8_to_F32(value,param->getMinWeight(),param->getMaxWeight()));param=a.getNextVisualParam();
    }
    LLVector3 hover;
    if (msg.getNumberOfBlocks("AppearanceHover")) msg.getVector3("AppearanceHover","HoverHeight",hover);
    if (!finite(hover) || hover.lengthSquared()>65536) return false;
    std::map<LLUUID,unsigned> attachments;
    const S32 attached=msg.getNumberOfBlocks("AttachmentBlock");if (attached>256) return false;
    for (S32 i=0;i<attached;++i) {LLUUID uuid;U8 point;msg.getUUID("AttachmentBlock","ID",uuid,i);msg.getU8("AttachmentBlock","AttachmentPoint",point,i);if (uuid.notNull()) attachments[uuid]=point;}
    for (unsigned i=0;i<TEX_NUM_INDICES;++i) a.textures.setTE(U8(i),*textures.getTE(U8(i)));
    for (auto& [p,w]:params) a.setVisualParamWeight(p,w);
    a.updateVisualParams();a.computeBodySize();a.hover=hover;a.cofVersion=cof;a.serverBakes=version!=0;a.simAttachments=std::move(attachments);
    a.appearanceKnown=true;a.shapeDirty=true;return true;
}
bool VSWorldAvatar::animations(LLMessageSystem& msg)
{
    auto& a=*mImpl;LLUUID id;msg.getUUID("Sender","ID",id);if (id!=a.id) return false;
    const S32 count=msg.getNumberOfBlocks("AnimationList");if (count<0 || count>256) return false;
    std::map<LLUUID,S32> next;
    for (S32 i=0;i<count;++i) {LLUUID animation;S32 sequence;msg.getUUID("AnimationList","AnimID",animation,i);msg.getS32("AnimationList","AnimSequenceID",sequence,i);if (animation.notNull()) next[animation]=sequence;}
    for (const auto& [uuid,sequence]:a.animations) if (!next.count(uuid)) a.stopMotion(uuid);
    for (const auto& [uuid,sequence]:next) if (!a.animations.count(uuid) || a.animations[uuid]!=sequence) {a.stopMotion(uuid,true);a.startMotion(uuid);}
    a.animations=std::move(next);return true;
}
void VSWorldAvatar::append(std::vector<VSWorldRenderer::Packet>& packets,const LLVector3& eye,float farClip,unsigned viewportHeight,float timeDilation)
{
    auto& a=*mImpl;if (!a.positionKnown || !a.appearanceKnown) return;
    const float distance=(a.position-eye).lengthSquared();if (distance>farClip*farClip) {a.meshes.clear();return;}
    a.timeDilation=llclamp(timeDilation,0.f,1.f);
    const float radius=.5f*a.mBodySize.length(),projected=radius*float(viewportHeight)*.5f/std::tan(F_PI/6);
    a.pixelArea=F_PI*projected*projected/std::max(distance,.01f);
    a.pose();
    for (const auto& [index,entry]:LLAvatarAppearance::getDictionary()->getMeshEntries())
    {
        if (entry->mBakedID==BAKED_NUM_INDICES || unsigned(index)>=a.mMeshLOD.size()) continue;
        const auto* baked=LLAvatarAppearance::getDictionary()->getBakedTexture(entry->mBakedID);
        const LLUUID& texture=a.textures.getTE(U8(baked->mTextureIndex))->getID();
        if (texture.isNull() || texture==IMG_DEFAULT_AVATAR || texture==IMG_INVISIBLE) continue;
        LLUI::getUIImageByID(texture);auto image=a.resources.image("remote:"+texture.asString());if (!image) continue;
        if (auto mask=vs_ui_image_aux(texture);mask && a.appliedMasks[entry->mBakedID].get()!=mask.get())
        {
            a.applyMorphMask(mask->getData(),mask->getWidth(),mask->getHeight(),1,entry->mBakedID);
            a.appliedMasks[entry->mBakedID]=mask;
        }
        auto& parts=a.mMeshLOD[index]->mMeshParts;LLAvatarJointMesh* part=nullptr;
        for (auto* candidate:parts) {part=candidate;if (a.pixelArea>=candidate->getLOD()) break;}
        if (!part) continue;
        std::vector<VSWorldRenderer::Vertex> vertices;std::vector<unsigned> indices;
        if (!a.skin(*part,vertices,indices)) continue;
        auto mesh=a.renderer.upload(vertices,indices);a.meshes[index]=mesh;
        VSWorldRenderer::Packet p;p.mesh=std::move(mesh);p.image=std::move(image);p.masked=true;
        p.alpha=part->isTransparent();p.distanceSquared=distance;packets.push_back(std::move(p));
    }
    a.shapeDirty=false;
}
bool VSWorldAvatar::attachmentTransform(unsigned point,LLVector3& p,LLQuaternion& q)
{
    auto& a=*mImpl;const auto i=a.attachments.find(point);if (i==a.attachments.end() || !a.positionKnown) return false;
    p=i->second->getWorldPosition();q=i->second->getWorldRotation();return finite(p) && q.isFinite();
}
bool VSWorldAvatar::skinRiggedFace(const LLVolumeFace& face,const LLSD& metadata,std::vector<VSWorldRenderer::Vertex>& vertices)
{
    auto& a=*mImpl;
    if (!a.positionKnown || !face.mWeights || face.mNumVertices<=0 || face.mNumVertices>65536 ||
        !face.mPositions || !face.mNormals || !face.mTexCoords || !metadata.isMap()) return false;
    const auto& names=metadata["joint_names"];
    if (!names.isArray() || !names.size() || names.size()>256) return false;
    if (!metadata["inverse_bind_matrix"].isArray() || metadata["inverse_bind_matrix"].size()!=names.size()) return false;
    auto matrixValid=[](const LLSD& matrix) {
        if (!matrix.isArray() || matrix.size()!=16) return false;
        for (unsigned i=0;i<16;++i) if (!std::isfinite(float(matrix[i].asReal()))) return false;
        return true;
    };
    for (unsigned i=0;i<names.size();++i)
        if (!names[i].isString() || names[i].asString().size()>256 || !matrixValid(metadata["inverse_bind_matrix"][i])) return false;
    if (metadata.has("bind_shape_matrix") && !matrixValid(metadata["bind_shape_matrix"])) return false;
    LLSD source=metadata;LLMeshSkinInfo skin(source);
    if (skin.mJointNames.size()!=skin.mInvBindMatrix.size() || skin.mJointNames.empty()) return false;
    std::vector<LLMatrix4a> palette(skin.mJointNames.size());
    for (unsigned i=0;i<palette.size();++i)
    {
        auto* joint=a.mRoot->findJoint(skin.mJointNames[i]);
        if (!joint) joint=a.mRoot->findJoint("mPelvis");
        if (!joint) return false;
        matMulUnsafe(skin.mInvBindMatrix[i],joint->getWorldMatrix4a(),palette[i]);
    }
    vertices.reserve(face.mNumVertices);
    for (S32 v=0;v<face.mNumVertices;++v)
    {
        const auto* packed=face.mWeights[v].getF32ptr();unsigned indices[4]{};float weights[4]{},total=0;
        for (unsigned c=0;c<4;++c)
        {
            if (!std::isfinite(packed[c]) || packed[c]<0) return false;
            const float integer=std::floor(packed[c]);
            weights[c]=packed[c]-integer;
            if (weights[c] && integer>=float(palette.size())) return false;
            indices[c]=weights[c]?unsigned(integer):0;total+=weights[c];
        }
        if (total<1.e-6f) return false;
        LLMatrix4a blend;blend.setMul(palette[indices[0]],weights[0]/total);
        for (unsigned c=1;c<4;++c) {LLMatrix4a term;term.setMul(palette[indices[c]],weights[c]/total);blend.add(term);}
        LLVector4a bound,point,normalBound,normal;
        skin.mBindShapeMatrix.affineTransform(face.mPositions[v],bound);blend.affineTransform(bound,point);
        skin.mBindShapeMatrix.rotate(face.mNormals[v],normalBound);blend.rotate(normalBound,normal);
        const auto* p=point.getF32ptr();LLVector3 n(normal.getF32ptr());n.normalize();
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || !finite(n)) return false;
        const auto& uv=face.mTexCoords[v];vertices.push_back({p[0],p[1],p[2],n.mV[0],n.mV[1],n.mV[2],uv.mV[0],1-uv.mV[1]});
    }
    return true;
}
LLSD VSWorldAvatar::evidence() const
{
    const auto& a=*mImpl;LLSD result;result["id"]=a.id;result["self"]=a.self;result["position_known"]=a.positionKnown;
    result["appearance_known"]=a.appearanceKnown;result["server_bakes"]=a.serverBakes;result["cof_version"]=a.cofVersion;
    result["animations"]=S32(a.animations.size());result["attachments"]=S32(a.simAttachments.size());result["meshes"]=S32(a.meshes.size());
    result["local_compositor_implemented"]=false;return result;
}

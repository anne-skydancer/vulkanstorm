// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldscene.h"
#include "vsterrainpacket.h"
#include "vsworldavatar.h"
#include "vsworldassets.h"
#include "vsworldmotion.h"
#include "vsnativesession.h"
#include "llregionhandle.h"
#include "patch_dct.h"
#include <limits>
#include "vsuiresources.h"
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
namespace
{
constexpr std::size_t maxObjects=16384;
bool finiteVector(const LLVector3& v) { return std::isfinite(v.mV[0]) && std::isfinite(v.mV[1]) && std::isfinite(v.mV[2]); }
U32 littleU32(const U8* p) { return U32(p[0]) | U32(p[1])<<8 | U32(p[2])<<16 | U32(p[3])<<24; }
F32 littleFloat(const U8* p) { const U32 n=littleU32(p);F32 v;std::memcpy(&v,&n,4);return v; }
LLVector3 vectorAt(const U8* p) { return {littleFloat(p),littleFloat(p+4),littleFloat(p+8)}; }
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
        if (length>extra.size()-p || type==0x10 || type==0x30) return false;
        p+=length;
    }
    return p==extra.size();
}
bool volumeExtras(const std::vector<U8>& extra,LLVolumeParams& params)
{
    if (baseVolume(extra)) return true;
    if (extra.empty()) return true;
    std::size_t p=1;
    for (unsigned i=0;i<extra[0];++i)
    {
        if (extra.size()-p<6) return false;
        const U16 type=U16(extra[p]) | U16(extra[p+1])<<8;
        const U32 length=littleU32(extra.data()+p+2);p+=6;
        if (length>extra.size()-p || type==0x10) return false;
        if (type==0x30)
        {
            if (length!=17) return false;
            LLUUID asset;std::memcpy(asset.mData,extra.data()+p,UUID_BYTES);
            if (asset.isNull() || !params.setSculptID(asset,extra[p+16])) return false;
            if ((extra[p+16]&LL_SCULPT_TYPE_MASK)!=LL_SCULPT_TYPE_MESH) return false;
        }
        p+=length;
    }
    return p==extra.size();
}
bool ownerExtras(const std::vector<U8>& extra,U32& meshFlags)
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
        position+=length;
    }
    return position==extra.size();
}
}
struct VSWorldScene::Budgets
{
    LLUUID account;U64 accountGeneration=0;
    VSResourceBudget objects{maxObjects},avatars{256},objectRequests{4096},primitiveBytes{128*1024*1024},terrainBytes{64*1024*1024};
};
struct VSWorldScene::Impl
{
    struct Face { VSWorldRenderer::Mesh mesh;LLTextureEntry material; };
    struct Object
    {
        LLUUID id;U32 parent=0;U8 pcode=LL_PCODE_VOLUME,attachment=0;LLVector3 position,scale,velocity,acceleration,angularVelocity;LLQuaternion rotation;
        U64 generation=0,revision=0;VSWorldMotion motion;
        U64 parentGeneration=0;bool parentRetired=false;
        U32 extendedMeshFlags=0;
        std::map<LLUUID,S32> controlAnimations;
        std::shared_ptr<const VSResourceBudget::Reservation> slot,primitiveResidency;
        U16 taskSerial=0;bool taskKnown=false;double taskDeadline=0;std::string taskFile;std::optional<ObjectHandle> taskRequest;
        LLPointer<LLVolume> volume;LLVolumeParams params;
        std::shared_ptr<const VSWorldAssets::Geometry> asset;
        S32 assetLOD=-1;
        std::vector<Face> faces;bool dirty=true,renderable=true;
    };
    VSWorldRenderer& renderer;VSUIResources& resources;
    std::shared_ptr<VSWorldAssets> assets;
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
    std::map<U32,Object> objects;std::map<U32,Request> pending;
    LLUUID agent;
    float timeDilation=1;
    std::map<LLUUID,std::unique_ptr<VSWorldAvatar>> avatars;
    bool groundAt(const LLVector3& point,LLVector3& position,LLVector3& normal) const
    {
        if (!finiteVector(point) || point.mV[0]<0 || point.mV[1]<0 || point.mV[0]>=width || point.mV[1]>=regionHeight) return false;
        const unsigned x=unsigned(point.mV[0]),y=unsigned(point.mV[1]);const float z=height(x,y,std::numeric_limits<float>::quiet_NaN());
        if (!std::isfinite(z)) return false;
        position=point;position.mV[2]=z;
        normal=LLVector3(height(x?x-1:x,y,z)-height(std::min(x+1,width),y,z),height(x,y?y-1:y,z)-height(x,std::min(y+1,regionHeight),z),2);normal.normalize();return true;
    }
    VSWorldAvatar* avatar(const LLUUID& id,U64 handle)
    {
        const auto found=avatars.find(id);if (found!=avatars.end()) return found->second.get();
        if (id.isNull() || avatars.size()>=256) return nullptr;
        auto reservation=budgets->avatars.reserve(1);if (!reservation) return nullptr;
        VSWorldAvatar::initializeClass();
        auto owner=std::make_unique<VSWorldAvatar>(id,id==agent,handle,renderer,resources,
            [this](const LLVector3& p,LLVector3& out,LLVector3& n){return groundAt(p,out,n);},std::move(reservation));
        auto* result=owner.get();avatars[id]=std::move(owner);return result;
    }
    double nextRequest=0;
    struct Ground { unsigned size=0;VSWorldRenderer::Mesh mesh;bool dirty=true;std::array<float,1024> heights{};std::shared_ptr<const VSResourceBudget::Reservation> residency; };
    std::map<U64,Ground> ground;
    unsigned patchSize=0;
    std::array<LLUUID,4> terrainTextures;
    std::array<float,4> terrainStarts{},terrainRanges{};
    VSWorldRenderer::Mesh water,sky;
    float waterHeight=20;bool waterKnown=false;
    float height(unsigned x,unsigned y,float fallback) const
    {
        if (!patchSize || x>=width || y>=regionHeight) return fallback;
        const unsigned x0=x/patchSize*patchSize,y0=y/patchSize*patchSize;
        const auto patch=ground.find((U64(x0)<<32)|y0);
        return patch==ground.end()?fallback:patch->second.heights[(y-y0)*patchSize+x-x0];
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
        return true;
    }
    void surfaces(std::vector<VSWorldRenderer::Packet>& packets,const LLVector3& eye)
    {
        if (!sky) sky=renderer.upload({{-1,-1,0,0,0,1,0,0},{1,-1,0,0,0,1,1,0},{1,1,0,0,0,1,1,1},{-1,1,0,0,0,1,0,1}},{0,1,2,0,2,3});
        if (sky) { VSWorldRenderer::Packet background;background.mesh=sky;background.background=true;background.fullbright=true;packets.push_back(background); }
        for (auto& [key,g]:ground)
        {
            const unsigned x0=unsigned(key>>32),y0=U32(key),n=g.size;
            if (g.dirty)
            {
                std::vector<VSWorldRenderer::Vertex> vertices;std::vector<unsigned> indices;
                for (unsigned y=0;y<=n;++y) for (unsigned x=0;x<=n;++x)
                {
                    const unsigned gx=x0+x,gy=y0+y;
                    const float fallback=g.heights[std::min(y,n-1)*n+std::min(x,n-1)];
                    const float z=height(gx,gy,fallback);
                    LLVector3 normal(height(gx?gx-1:gx,gy,z)-height(std::min(gx+1,width),gy,z),
                        height(gx,gy?gy-1:gy,z)-height(gx,std::min(gy+1,regionHeight),z),2);normal.normalize();
                    vertices.push_back({float(gx),float(gy),z,normal.mV[0],normal.mV[1],normal.mV[2],float(gx)/16,1-float(gy)/16});
                    if (x<n && y<n) { const unsigned i=y*(n+1)+x;indices.insert(indices.end(),{i,i+1,i+n+2,i,i+n+2,i+n+1}); }
                }
                g.dirty = !renderer.publishMesh(g.mesh, vertices, indices);
            }
            if (!g.mesh) continue;
            // First surface pass uses a single height-selected simulator detail
            // map per patch; four-way terrain splatting is a later material pass.
            const float h=height(x0+n/2,y0+n/2,0),start=terrainStarts[0],range=std::max(terrainRanges[0],1.f);
            const unsigned level=unsigned(std::clamp((h-start)/range*4.f,0.f,3.f));const auto& texture=terrainTextures[level];
            if (texture.notNull()) LLUI::getUIImageByID(texture);
            VSWorldRenderer::Packet p;p.mesh=g.mesh;p.image=resources.image("remote:"+texture.asString());
            if (!p.image) continue; // Unknown resources are not a substitute terrain material.
            packets.push_back(std::move(p));
        }
        if (waterKnown)
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
    Impl(VSWorldRenderer& r,VSUIResources& u,std::shared_ptr<VSWorldAssets> cache,std::shared_ptr<Budgets> limits):
        renderer(r),resources(u),assets(std::move(cache)),budgets(std::move(limits)) {}
    void request(U32 id)
    {
        if(!id || pending.count(id) || pending.size()>=1024)return;
        auto slot=budgets->objectRequests.reserve(1);if(!slot)return;
        Request request;request.slot=std::move(slot);pending.emplace(id,std::move(request));
    }
    void eraseObject(U32 id)
    {
        for (auto& [local,object]:objects) if (object.parent==id) object.parentRetired=true;
        dirtyChildren(id);
        if (const auto object=objects.find(id);object!=objects.end() && object->second.pcode==LL_PCODE_LEGACY_AVATAR)
            avatars.erase(object->second.id);
        objects.erase(id);pending.erase(id);
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
        object.slot=previous!=objects.end()?previous->second.slot:budgets->objects.reserve(1);
        if (!object.slot) {request(id);return false;}
        if (object.volume && object.params.getSculptID().isNull())
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
        object.generation=sameOwner?previous->second.generation:nextObjectGeneration++;
        object.revision=nextRevision++;
        if (sameOwner)
        {
            // Identity guards/revision still advance immediately; the admitted
            // GPU generation survives until its replacement can be published.
            if (object.renderable && previous->second.renderable &&
                object.params.getSculptID() == previous->second.params.getSculptID())
                for (std::size_t face = 0; face < std::min(object.faces.size(), previous->second.faces.size()); ++face)
                    object.faces[face].mesh = std::move(previous->second.faces[face].mesh);
            object.motion=previous->second.motion;
            object.taskSerial=previous->second.taskSerial;object.taskKnown=previous->second.taskKnown;
            object.taskFile=previous->second.taskFile;object.taskRequest=previous->second.taskRequest;object.taskDeadline=previous->second.taskDeadline;
            object.controlAnimations=previous->second.controlAnimations;
        }
        object.motion.accept(object.position,object.rotation,object.velocity,object.acceleration,object.angularVelocity,LLFrameTimer::getTotalSeconds());
        if (!sameOwner && previous!=objects.end())
            for (auto& [local,child]:objects) if (child.parent==id) child.parentRetired=true;
        if (const auto old=objects.find(id);old!=objects.end() && old->second.pcode==LL_PCODE_LEGACY_AVATAR &&
            (old->second.id!=object.id || object.pcode!=LL_PCODE_LEGACY_AVATAR)) avatars.erase(old->second.id);
        if(kind(object)==VSWorldContracts::OwnerKind::ControlAvatar)avatars.erase(object.id);
        objects[id]=std::move(object);pending.erase(id);dirtyChildren(id);
        return true;
    }
    bool compressed(std::vector<U8>& bytes)
    {
        LLDataPackerBinaryBuffer dp(bytes.data(),S32(bytes.size()));
        Object object;U32 id,ignored,flags;U8 pcode,state,value;LLVector3 rotation,unused;LLUUID owner;
        if (!dp.unpackUUID(object.id,"ID") || !dp.unpackU32(id,"LocalID") || !dp.unpackU8(pcode,"PCode") ||
            !dp.unpackU8(state,"State")) return false;
        object.pcode=pcode;
        if (!dp.unpackU32(ignored,"CRC") || !dp.unpackU8(value,"Material") || !dp.unpackU8(value,"ClickAction") ||
            !dp.unpackVector3(object.scale,"Scale") || !dp.unpackVector3(object.position,"Pos") ||
            !dp.unpackVector3(rotation,"Rot") || !dp.unpackU32(flags,"SpecialCode") || !dp.unpackUUID(owner,"Owner")) return false;
        object.rotation.unpackFromVector3(rotation);object.attachment=state;
        if (flags&0x80) if (!dp.unpackVector3(object.angularVelocity,"Omega") || !finiteVector(object.angularVelocity)) return false;
        if (flags&0x20) if (!dp.unpackU32(object.parent,"ParentID")) return false;
        U8 scratch[4096];S32 length;std::string text;float number;
        // The legacy packer calls strlen before checking the remaining bytes.
        // Validate termination before invoking it on simulator-owned strings.
        auto string=[&](const char* label)
        {
            const S32 remaining=dp.getBufferSize()-dp.getCurrentSize();
            return remaining>0 && std::memchr(dp.getBuffer()+dp.getCurrentSize(),0,std::size_t(remaining)) && dp.unpackString(text,label);
        };
        if (flags&2) { if (!dp.unpackU8(value,"TreeData")) return false; }
        else if (flags&1) { if (!dp.unpackU32(ignored,"ScratchPadSize") || !dp.unpackBinaryData(scratch,length,"PartData")) return false; }
        if (flags&4) if (!string("Text") || !dp.unpackBinaryDataFixed(scratch,4,"Color")) return false;
        if (flags&0x200) if (!string("MediaURL")) return false;
        if (flags&8) if (!dp.unpackBinaryDataFixed(scratch,86,"LegacyParticles")) return false;
        U8 parameters;
        if (!dp.unpackU8(parameters,"num_params")) return false;
        bool supported=true;LLUUID mesh;U8 sculptType=LL_SCULPT_TYPE_NONE;
        for (unsigned n=0;n<parameters;++n)
        {
            U16 type;
            if (!dp.unpackU16(type,"param_type") || !dp.unpackBinaryData(scratch,length,"param_data")) return false;
            if (type==0x10) supported=false;
            if (type==0x30)
            {
                if (length!=17) return false;
                std::memcpy(mesh.mData,scratch,UUID_BYTES);sculptType=scratch[16];
                if (mesh.isNull() || (sculptType&LL_SCULPT_TYPE_MASK)!=LL_SCULPT_TYPE_MESH) supported=false;
            }
            if (type==LLNetworkData::PARAMS_EXTENDED_MESH)
            { if (length!=4) return false;object.extendedMeshFlags=littleU32(scratch); }
        }
        if (flags&0x10) if (!dp.unpackUUID(owner,"SoundUUID") || !dp.unpackF32(number,"SoundGain") ||
            !dp.unpackU8(value,"SoundFlags") || !dp.unpackF32(number,"SoundRadius")) return false;
        if (flags&0x100) if (!string("NV")) return false;
        if (!id || object.id.isNull() || object.parent==id || !finiteVector(object.position) || !finiteVector(object.scale) ||
            !object.rotation.isFinite() || object.scale.mV[0]<=0 || object.scale.mV[1]<=0 || object.scale.mV[2]<=0 ||
            object.scale.lengthSquared()>1.e8f || (objects.size()>=maxObjects && !objects.count(id))) return false;
        if(pcode!=LL_PCODE_VOLUME)
        { object.renderable=false;if(!replace(id,std::move(object)))return false;++unsupported;++accepted;return true; }
        LLVolumeParams params;if (!LLVolumeMessage::unpackVolumeParams(&params,dp)) return false;
        if (mesh.notNull() && !params.setSculptID(mesh,sculptType)) return false;
        object.params=params;
        object.renderable=supported;
        if (mesh.isNull() && supported) object.volume=new LLVolume(params,2.f);
        if(!supported)++unsupported;
        const S32 faces=object.volume ? object.volume->getNumVolumeFaces() : 8;
        if (faces<=0 || faces>45) return false;
        LLPrimitive material;material.setNumTEs(U8(faces));
        if (material.unpackTEMessage(dp)==TEM_INVALID) return false;
        for (S32 f=0;f<faces;++f) object.faces.push_back({{},*material.getTE(U8(f))});
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
VSWorldScene::~VSWorldScene() { mNeighbours.clear();mImpl->retired=true;mImpl.reset();if (mRoot) VSWorldAvatar::cleanupClass(); }
void VSWorldScene::reset(bool preserveAssets)
{
    mNeighbours.clear();if (!preserveAssets) mImpl->assets->reset();
    mImpl->avatars.clear();mImpl->ground.clear();mImpl->patchSize=0;
    mImpl->water.reset();mImpl->waterKnown=false;mImpl->terrainTextures={};
    mImpl->objects.clear();mImpl->pending.clear();mImpl->nextRequest=0;
    mImpl->accepted=mImpl->rejected=mImpl->unsupported=0;
    if(mRoot && !preserveAssets) {mImpl->budgets->account.setNull();mImpl->budgets->accountGeneration=0;}
}
bool VSWorldScene::openRegion(const LLHost& host,U64 handle,U32 width,U32 height,U64 incarnation)
{
    if (!host.isOk() || width<16 || height<16 || width>65536 || height>65536 || !incarnation) return false;
    if (host==mImpl->host)
    {
        if (mImpl->handle!=handle || mImpl->incarnation!=incarnation)
        {
            auto previous=mImpl;previous->retired=true;
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
        mImpl->width=width;mImpl->regionHeight=height;return true;
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
std::optional<VSWorldScene::FaceHandle> VSWorldScene::face(const ObjectHandle& token,U32 index) const
{
    if (!current(token)) return std::nullopt;
    if (token.host!=mImpl->host) return mNeighbours.at(token.host)->face(token,index);
    const auto& object=mImpl->objects.at(token.local);
    if (index>=object.faces.size()) return std::nullopt;
    return FaceHandle{token,index,object.revision};
}
bool VSWorldScene::current(const FaceHandle& token) const
{
    const auto owned=face(token.object,token.face);
    return owned && owned->generation==token.generation;
}
std::optional<VSWorldContracts::Material> VSWorldScene::material(const FaceHandle& token) const
{
    if(!current(token))return std::nullopt;
    if(token.object.host!=mImpl->host)return mNeighbours.at(token.object.host)->material(token);
    const auto& face=mImpl->objects.at(token.object.local).faces.at(token.face);
    VSWorldContracts::Material result;
    std::copy(face.material.getColor().mV,face.material.getColor().mV+4,result.diffuse.begin());
    result.fullbright=face.material.getFullbright();result.generation=token.generation;
    const auto image=mImpl->resources.image("remote:"+face.material.getID().asString());
    result.diffuseImageKnown=face.material.getID().isNull() || bool(image);
    result.alpha=result.diffuse[3]<.999f?VSWorldContracts::AlphaMode::Blend:
        image?VSUIRenderer::imageDescription(image).alpha:VSWorldContracts::AlphaMode::Opaque;
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
void VSWorldScene::replaceAsset(const LLUUID& id)
{
    mImpl->assets->replace(id);
    auto replace=[&](Impl& region)
    {
        for (auto& [local,object]:region.objects)
            if (object.params.getSculptID()==id)
            {
                object.asset.reset();object.volume=nullptr;object.dirty=true;
                object.revision=region.nextRevision++;
                for(auto& face:object.faces)face.mesh.reset();
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
    if(name=="ObjectUpdate" || name=="ObjectUpdateCompressed" || name=="ObjectUpdateCached" || name=="ImprovedTerseObjectUpdate")
    {
        if(msg.getNumberOfBlocks("RegionData")!=1 || msg.getSize("RegionData","RegionHandle")!=8)
        {++state.rejected;return true;}
        U64 declared;msg.getU64("RegionData","RegionHandle",declared);
        if(declared!=state.handle){++state.rejected;return true;}
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
    if (name=="AvatarAppearance" || name=="AvatarAnimation")
    {
        LLUUID id;msg.getUUID("Sender","ID",id);
        for(auto& [local,object]:state.objects)
            if(object.id==id && Impl::kind(object)==VSWorldContracts::OwnerKind::ControlAvatar)
            {
                const S32 count=msg.getNumberOfBlocks("AnimationList");
                if(name!="AvatarAnimation" || count<0 || count>256){++state.rejected;return true;}
                std::map<LLUUID,S32> animations;
                for(S32 i=0;i<count;++i)
                {
                    LLUUID animation;S32 sequence;msg.getUUID("AnimationList","AnimID",animation,i);
                    msg.getS32("AnimationList","AnimSequenceID",sequence,i);if(animation.notNull())animations[animation]=sequence;
                }
                object.controlAnimations=std::move(animations);++state.accepted;return true;
            }
        auto* avatar=state.avatar(id,regionHandle);
        if (!avatar || !(name=="AvatarAppearance"?avatar->appearance(msg):avatar->animations(msg))) ++state.rejected;else ++state.accepted;
        return true;
    }
    if (name=="LayerData")
    {
        U8 type;msg.getU8("LayerID","Type",type);
        if (type!='L' && type!='M') return true; // Wind/cloud layers are separate protocols.
        const S32 length=msg.getSize("LayerData","Data");
        if (length<5 || length>65535) {++state.rejected;return true;}
        std::vector<U8> bytes(length);msg.getBinaryData("LayerData","Data",bytes.data(),length,0,length);
        if (state.terrain(bytes,type)) ++state.accepted;else ++state.rejected;
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
            if (name=="ObjectUpdateCompressed") { if (!state.compressed(bytes)) ++state.rejected;continue; }
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
        { object.renderable=false;if(state.replace(id,std::move(object)))++state.accepted;else ++state.rejected;++state.unsupported;continue; }
        const S32 extraSize=msg.getSize("ObjectData",i,"ExtraParams");
        if (extraSize<0 || extraSize>4096) { ++state.rejected;continue; }
        std::vector<U8> extra(extraSize);
        if (extraSize) msg.getBinaryData("ObjectData","ExtraParams",extra.data(),extraSize,i,extraSize);
        LLVolumeParams params;
        const char* objectBlock=LLMessageStringTable::getInstance()->getString("ObjectData");
        if (!LLVolumeMessage::unpackVolumeParams(&params,&msg,objectBlock,i)) { ++state.rejected;continue; }
        if (!ownerExtras(extra,object.extendedMeshFlags)) {++state.rejected;continue;}
        object.renderable=volumeExtras(extra,params);if(!object.renderable)++state.unsupported;
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
std::vector<VSWorldRenderer::Packet> VSWorldScene::packets(const LLVector3& eye,float farClip,unsigned viewportHeight)
{
    std::vector<VSWorldRenderer::Packet> packets;
    mImpl->assets->pump();
    VSWorldMotion::Policy motionPolicy;
    motionPolicy.interpolate=gSavedSettings.getBOOL("VelocityInterpolate");
    motionPolicy.maximum=gSavedSettings.getF32("InterpolationTime");motionPolicy.phaseOut=gSavedSettings.getF32("InterpolationPhaseOut");
    if (!std::isfinite(motionPolicy.maximum) || !std::isfinite(motionPolicy.phaseOut) || motionPolicy.maximum<0 ||
        motionPolicy.phaseOut<0 || motionPolicy.phaseOut>motionPolicy.maximum) motionPolicy={motionPolicy.interpolate,1,3};
    const double frameTime=LLFrameTimer::getTotalSeconds();
    bool healthy=true;
    if (gMessageSystem && mImpl->host.isOk())
    {
        const auto* circuit=gMessageSystem->mCircuitInfo.findCircuit(mImpl->host);
        healthy=circuit && circuit->isAlive() && !circuit->isBlocked() &&
            LLMessageSystem::getMessageTimeSeconds()-circuit->getLastPacketInTime()<=F64Seconds(motionPolicy.phaseOut);
    }
    for (auto& [local,object]:mImpl->objects)
        if (object.motion.update(frameTime,mImpl->timeDilation,healthy,object.attachment!=0,motionPolicy))
        { object.dirty=true;mImpl->dirtyChildren(local); }
    mImpl->surfaces(packets,eye);
    // Evaluate avatar roots and bones before resolving attached primitive transforms.
    for (const auto& [id,o]:mImpl->objects) if (o.pcode==LL_PCODE_LEGACY_AVATAR)
    {
        LLVector3 p;LLQuaternion q;std::set<U32> visited;
        const auto avatar=mImpl->avatars.find(o.id);
        if (avatar!=mImpl->avatars.end() && mImpl->transform(id,p,q,visited))
            avatar->second->transform(p,q,o.velocity,o.angularVelocity);
    }
    for (auto& [id,avatar]:mImpl->avatars)
    {
        const Impl::Object* owner=nullptr;
        for(const auto& [local,object]:mImpl->objects)if(object.id==id && object.pcode==LL_PCODE_LEGACY_AVATAR){owner=&object;break;}
        if(!owner)continue; // Appearance arriving early does not invent a world transform.
        const auto start=packets.size();avatar->append(packets,eye,farClip,viewportHeight,mImpl->timeDilation);
        for (auto index=start;index<packets.size();++index)
        { std::copy(id.mData,id.mData+UUID_BYTES,packets[index].objectIdentity.begin());
          packets[index].objectGeneration=owner->generation;packets[index].faceGeneration=owner->revision; }
    }
    for (auto& [id,o]:mImpl->objects)
    {
        if (o.pcode==LL_PCODE_LEGACY_AVATAR || !o.renderable) continue;
        // Control-avatar geometry has a distinct producer in R2. A bind-pose
        // mesh is not a correctly animated substitute for that owner.
        if (o.extendedMeshFlags&LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG) continue;
        LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
        if (!mImpl->transform(id,position,rotation,visited)) continue;
        const float limit=llclamp(farClip,16.f,1024.f)+o.scale.length()*2.f;
        if ((position-eye).lengthSquared()>limit*limit)
        {
            for (auto& face:o.faces) face.mesh.reset();
            o.dirty=true;continue;
        }
        if (o.params.getSculptID().notNull())
        {
            const float distance=std::max((position-eye).length(),.01f);
            const float factor=llclamp(gSavedSettings.getF32("RenderVolumeLODFactor"),.01f,16.f);
            const S32 lod=LLVolumeLODGroup::getDetailFromTan(ll_round(factor*.5f*o.scale.length()/distance,.01f));
            mImpl->assets->request(o.params,lod);
            auto asset=mImpl->assets->geometry(o.params.getSculptID(),lod);
            if (!asset) asset=o.asset; // Keep a previously decoded LOD while its replacement loads.
            if (!asset) continue;
            if (asset!=o.asset) {o.asset=asset;o.assetLOD=asset->lod;o.volume=asset->volume;o.dirty=true;o.revision=mImpl->nextRevision++;}
        }
        if (!o.volume) continue;
        VSWorldAvatar* riggedOwner=nullptr;
        if (o.asset && o.asset->skin.isMap() && o.parent)
        {
            U32 parent=o.parent;std::set<U32> visited;
            while (parent && visited.insert(parent).second && visited.size()<=256)
            {
                const auto object=mImpl->objects.find(parent);if (object==mImpl->objects.end()) break;
                if (object->second.pcode==LL_PCODE_LEGACY_AVATAR)
                {const auto owner=mImpl->avatars.find(object->second.id);if (owner!=mImpl->avatars.end()) riggedOwner=owner->second.get();break;}
                parent=object->second.parent;
            }
        }
        bool uploadDeferred = false;
        const S32 count=std::min(S32(o.faces.size()),o.volume->getNumVolumeFaces());
        for (S32 f=0;f<count;++f)
        {
            auto& face=o.faces[f];
            if (o.dirty || riggedOwner || !face.mesh)
            {
                const auto& geometry=o.volume->getVolumeFace(f);
                if (!geometry.mPositions || !geometry.mNormals || !geometry.mTexCoords || !geometry.mIndices ||
                    geometry.mNumVertices<=0 || geometry.mNumIndices<=0 || geometry.mNumIndices%3) continue;
                std::vector<VSWorldRenderer::Vertex> vertices;vertices.reserve(geometry.mNumVertices);
                const float angle=face.material.getRotation(),c=std::cos(angle),s=std::sin(angle);
                if (riggedOwner && geometry.mWeights)
                {
                    if (!riggedOwner->skinRiggedFace(geometry,o.asset->skin,vertices)) {face.mesh.reset();continue;}
                    for (auto& vertex:vertices)
                    {
                        const float u=vertex.u-.5f,t=1-vertex.v-.5f;
                        vertex.u=(u*c+t*s)*face.material.getScaleS()+face.material.getOffsetS()+.5f;
                        vertex.v=1-((-u*s+t*c)*face.material.getScaleT()+face.material.getOffsetT()+.5f);
                    }
                }
                else for (S32 v=0;v<geometry.mNumVertices;++v)
                {
                    const auto* p=geometry.mPositions[v].getF32ptr();const auto* n=geometry.mNormals[v].getF32ptr();
                    LLVector3 point(p[0]*o.scale.mV[0],p[1]*o.scale.mV[1],p[2]*o.scale.mV[2]);point=point*rotation+position;
                    LLVector3 normal(n[0]/o.scale.mV[0],n[1]/o.scale.mV[1],n[2]/o.scale.mV[2]);normal=normal*rotation;normal.normalize();
                    const auto& uv=geometry.mTexCoords[v];const float u=uv.mV[0]-.5f,t=uv.mV[1]-.5f;
                    const float texU=(u*c+t*s)*face.material.getScaleS()+face.material.getOffsetS()+.5f;
                    const float texV=(-u*s+t*c)*face.material.getScaleT()+face.material.getOffsetT()+.5f;
                    vertices.push_back({point.mV[0],point.mV[1],point.mV[2],normal.mV[0],normal.mV[1],normal.mV[2],texU,1-texV});
                }
                std::vector<unsigned> indices(geometry.mIndices,geometry.mIndices+geometry.mNumIndices);
                if (!mImpl->renderer.publishMesh(face.mesh, vertices, indices)) uploadDeferred = true;
            }
            if (!face.mesh) continue;
            const LLUUID texture=face.material.getID();if (texture.notNull()) LLUI::getUIImageByID(texture);
            const auto& color=face.material.getColor();VSWorldRenderer::Packet packet;packet.mesh=face.mesh;
            packet.image=mImpl->resources.image("remote:"+texture.asString());packet.color={color.mV[0],color.mV[1],color.mV[2],color.mV[3]};
            if (texture.notNull() && !packet.image) continue;
            std::copy(o.id.mData,o.id.mData+UUID_BYTES,packet.objectIdentity.begin());
            packet.objectGeneration=o.generation;packet.faceGeneration=o.revision;
            packet.fullbright=face.material.getFullbright()!=0;packet.alpha=color.mV[3]<1;
            if (packet.image)
            {
                const auto image=VSUIRenderer::imageDescription(packet.image);
                packet.alpha|=image.alpha==VSWorldContracts::AlphaMode::Blend;
                packet.masked=image.alpha==VSWorldContracts::AlphaMode::Mask;
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
            auto neighbours=region->packets(eye-offset,farClip,viewportHeight);
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
    return packets;
}
VSWorldRenderer::View VSWorldScene::view(const LLVector3& eye,unsigned width,unsigned height,float farClip)
{
    VSWorldRenderer::View result;if (!width || !height || !finiteVector(eye) || !std::isfinite(farClip)) return result;
    const float nearClip=.1f,farDistance=llclamp(farClip,16.f,1024.f),cot=1/std::tan(F_PI/6);
    const float x=cot*height/width,y=cot,a=(farDistance+nearClip)/(farDistance-nearClip),b=-2*farDistance*nearClip/(farDistance-nearClip);
    result.projection={0,0,a,1, -x,0,0,0, 0,y,0,0, x*eye.mV[1],-y*eye.mV[2],b-a*eye.mV[0],-eye.mV[0]};return result;
}
VSWorldRenderer::View VSWorldScene::cameraView(const LLVector3& eye,const LLVector3& direction,const LLVector3& vertical,
                                             unsigned width,unsigned height,float farClip)
{
    VSWorldRenderer::View result;
    if (!width || !height || !finiteVector(eye) || !finiteVector(direction) || !finiteVector(vertical) || !std::isfinite(farClip)) return result;
    LLVector3 at=direction,up=vertical;
    if (at.normalize()<1.e-6f || up.normalize()<1.e-6f) return result;
    LLVector3 left=up%at;if (left.normalize()<1.e-6f) return result;
    up=at%left;up.normalize();
    const float nearClip=.1f,farDistance=llclamp(farClip,16.f,1024.f),cot=1/std::tan(F_PI/6);
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
    result["mesh_allocation_refusals"]=ll_sd_from_U64(stats.meshAllocationRefusals);
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
        owner["asset"]=object.params.getSculptID();owner["asset_type"]=S32(object.params.getSculptType());
        owner["asset_generation"]=ll_sd_from_U64(object.asset?object.asset->generation:0);
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

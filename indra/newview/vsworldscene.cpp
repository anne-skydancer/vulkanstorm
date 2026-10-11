// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldscene.h"
#include "vsuiresources.h"
#include "llprimitive.h"
#include "llvolumemessage.h"
#include "llvolume.h"
#include "lltextureentry.h"
#include "llui.h"
#include "llsd.h"
#include "llsdutil.h"
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
}
struct VSWorldScene::Impl
{
    struct Face { VSWorldRenderer::Mesh mesh;LLTextureEntry material; };
    struct Object
    {
        LLUUID id;U32 parent=0;LLVector3 position,scale;LLQuaternion rotation;
        LLPointer<LLVolume> volume;std::vector<Face> faces;bool dirty=true;
    };
    VSWorldRenderer& renderer;VSUIResources& resources;
    struct Request { double deadline=0; unsigned attempts=0; };
    std::map<U32,Object> objects;std::map<U32,Request> pending;
    double nextRequest=0;
    U64 accepted=0,rejected=0,unsupported=0;
    Impl(VSWorldRenderer& r,VSUIResources& u):renderer(r),resources(u) {}
    void request(U32 id) { if (id && pending.size()<1024) pending.try_emplace(id); }
    bool compressed(std::vector<U8>& bytes)
    {
        LLDataPackerBinaryBuffer dp(bytes.data(),S32(bytes.size()));
        Object object;U32 id,ignored,flags;U8 pcode,state,value;LLVector3 rotation,unused;LLUUID owner;
        if (!dp.unpackUUID(object.id,"ID") || !dp.unpackU32(id,"LocalID") || !dp.unpackU8(pcode,"PCode") ||
            !dp.unpackU8(state,"State")) return false;
        if (pcode!=LL_PCODE_VOLUME || state) { objects.erase(id);pending.erase(id);++unsupported;return true; }
        if (!dp.unpackU32(ignored,"CRC") || !dp.unpackU8(value,"Material") || !dp.unpackU8(value,"ClickAction") ||
            !dp.unpackVector3(object.scale,"Scale") || !dp.unpackVector3(object.position,"Pos") ||
            !dp.unpackVector3(rotation,"Rot") || !dp.unpackU32(flags,"SpecialCode") || !dp.unpackUUID(owner,"Owner")) return false;
        object.rotation.unpackFromVector3(rotation);
        if (flags&0x80) if (!dp.unpackVector3(unused,"Omega")) return false;
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
        bool supported=true;
        for (unsigned n=0;n<parameters;++n)
        {
            U16 type;
            if (!dp.unpackU16(type,"param_type") || !dp.unpackBinaryData(scratch,length,"param_data")) return false;
            if (type==0x10 || type==0x30) supported=false;
        }
        if (flags&0x10) if (!dp.unpackUUID(owner,"SoundUUID") || !dp.unpackF32(number,"SoundGain") ||
            !dp.unpackU8(value,"SoundFlags") || !dp.unpackF32(number,"SoundRadius")) return false;
        if (flags&0x100) if (!string("NV")) return false;
        if (!supported) { objects.erase(id);pending.erase(id);++unsupported;return true; }
        if (!id || object.id.isNull() || object.parent==id || !finiteVector(object.position) || !finiteVector(object.scale) ||
            !object.rotation.isFinite() || object.scale.mV[0]<=0 || object.scale.mV[1]<=0 || object.scale.mV[2]<=0 ||
            object.scale.lengthSquared()>1.e8f || (objects.size()>=maxObjects && !objects.count(id))) return false;
        LLVolumeParams params;if (!LLVolumeMessage::unpackVolumeParams(&params,dp)) return false;
        object.volume=new LLVolume(params,2.f);const S32 faces=object.volume->getNumVolumeFaces();
        if (faces<=0 || faces>45) return false;
        LLPrimitive material;material.setNumTEs(U8(faces));
        if (material.unpackTEMessage(dp)==TEM_INVALID) return false;
        for (S32 f=0;f<faces;++f) object.faces.push_back({{},*material.getTE(U8(f))});
        objects[id]=std::move(object);pending.erase(id);dirtyChildren(id);++accepted;return true;
    }
    bool transform(U32 id,LLVector3& position,LLQuaternion& rotation,std::set<U32>& visited)
    {
        if (!visited.insert(id).second || visited.size()>256) return false;
        const auto it=objects.find(id);if (it==objects.end()) return false;
        position=it->second.position;rotation=it->second.rotation;
        if (it->second.parent)
        {
            LLVector3 parentPosition;LLQuaternion parentRotation;
            if (!transform(it->second.parent,parentPosition,parentRotation,visited)) return false;
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
VSWorldScene::VSWorldScene(VSWorldRenderer& r,VSUIResources& u):mImpl(std::make_unique<Impl>(r,u)) {}
VSWorldScene::~VSWorldScene()=default;
void VSWorldScene::reset() { mImpl->objects.clear();mImpl->pending.clear();mImpl->nextRequest=0;mImpl->accepted=mImpl->rejected=mImpl->unsupported=0; }
bool VSWorldScene::receive(LLMessageSystem& msg,U64 regionHandle)
{
    const std::string name=msg.getMessageName();auto& state=*mImpl;
    if (name=="KillObject")
    {
        for (S32 i=0;i<msg.getNumberOfBlocks("ObjectData");++i)
        {
            U32 id;msg.getU32("ObjectData","ID",id,i);state.dirtyChildren(id);
            state.objects.erase(id);state.pending.erase(id);
        }
        return true;
    }
    if (name!="ObjectUpdate" && name!="ObjectUpdateCompressed" && name!="ObjectUpdateCached" && name!="ImprovedTerseObjectUpdate") return false;
    if (msg.getNumberOfBlocks("RegionData")!=1 || msg.getSize("RegionData","RegionHandle")!=8 ||
        msg.getNumberOfBlocks("ObjectData")<=0) { ++state.rejected;return true; }
    U64 handle;msg.getU64("RegionData","RegionHandle",handle);
    if (handle!=regionHandle) { ++state.rejected;return true; }
    for (S32 i=0;i<msg.getNumberOfBlocks("ObjectData");++i)
    {
        U32 id=0;
        if (name=="ObjectUpdateCached") { msg.getU32("ObjectData","ID",id,i);state.request(id);continue; }
        if (name=="ObjectUpdateCompressed" || name=="ImprovedTerseObjectUpdate")
        {
            const S32 size=msg.getSize("ObjectData",i,"Data");const S32 offset=name=="ObjectUpdateCompressed" ? 16 : 0;
            if (size<offset+4 || size>4096) { ++state.rejected;continue; }
            std::vector<U8> bytes(size);msg.getBinaryData("ObjectData","Data",bytes.data(),size,i,size);
            if (name=="ObjectUpdateCompressed") { if (!state.compressed(bytes)) ++state.rejected;continue; }
            // Improved terse: ID, state, avatar flag, position, quantized
            // velocity/acceleration, quaternion, angular velocity.
            if (size<44 || bytes[5]) { ++state.unsupported;continue; }
            id=littleU32(bytes.data());auto found=state.objects.find(id);
            if (found==state.objects.end()) { state.request(id);continue; }
            const auto position=vectorAt(bytes.data()+6);LLQuaternion rotation;
            for (unsigned c=0;c<4;++c)
            {
                const unsigned p=30+c*2;const U16 q=U16(bytes[p]) | U16(bytes[p+1])<<8;
                rotation.mQ[c]=float(q)/65535.f*2.f-1.f;
            }
            if (!finiteVector(position) || !rotation.isFinite()) { ++state.rejected;continue; }
            rotation.normalize();found->second.position=position;found->second.rotation=rotation;found->second.dirty=true;
            state.dirtyChildren(id);++state.accepted;
            if (msg.getSize("ObjectData",i,"TextureEntry")>0) state.request(id);
            continue;
        }
        msg.getU32("ObjectData","ID",id,i);
        U8 pcode,attachment;msg.getU8("ObjectData","PCode",pcode,i);msg.getU8("ObjectData","State",attachment,i);
        if (!id || pcode!=LL_PCODE_VOLUME || attachment) { ++state.unsupported;state.objects.erase(id);state.pending.erase(id);continue; }
        const S32 length=msg.getSize("ObjectData",i,"ObjectData");
        if (length!=60 && length!=124) { ++state.rejected;continue; }
        std::vector<U8> bytes(length);msg.getBinaryData("ObjectData","ObjectData",bytes.data(),length,i,length);
        Impl::Object object;object.position=vectorAt(bytes.data());object.rotation.unpackFromVector3(vectorAt(bytes.data()+36));
        msg.getVector3("ObjectData","Scale",object.scale,i);msg.getUUID("ObjectData","FullID",object.id,i);
        msg.getU32("ObjectData","ParentID",object.parent,i);
        if (!finiteVector(object.position) || !finiteVector(object.scale) || !object.rotation.isFinite() ||
            object.scale.mV[0]<=0 || object.scale.mV[1]<=0 || object.scale.mV[2]<=0 || object.scale.lengthSquared()>1.e8f ||
            object.parent==id || object.id.isNull()) { ++state.rejected;continue; }
        const S32 extraSize=msg.getSize("ObjectData",i,"ExtraParams");
        if (extraSize<0 || extraSize>4096) { ++state.rejected;continue; }
        std::vector<U8> extra(extraSize);
        if (extraSize) msg.getBinaryData("ObjectData","ExtraParams",extra.data(),extraSize,i,extraSize);
        if (!baseVolume(extra)) { ++state.unsupported;state.objects.erase(id);state.pending.erase(id);continue; }
        LLVolumeParams params;
        if (!LLVolumeMessage::unpackVolumeParams(&params,&msg,"ObjectData",i)) { ++state.rejected;continue; }
        object.volume=new LLVolume(params,2.f);const S32 faces=object.volume->getNumVolumeFaces();
        if (faces<=0 || faces>45 || (state.objects.size()>=maxObjects && !state.objects.count(id))) { ++state.rejected;continue; }
        LLPrimitive material;material.setNumTEs(U8(faces));
        if (material.unpackTEMessage(&msg,"ObjectData",i)==TEM_INVALID) { ++state.rejected;continue; }
        for (S32 f=0;f<faces;++f) object.faces.push_back({{},*material.getTE(U8(f))});
        state.objects[id]=std::move(object);state.pending.erase(id);state.dirtyChildren(id);++state.accepted;
    }
    return true;
}
void VSWorldScene::requestUpdates(LLMessageSystem& msg,const LLHost& host,const LLUUID& agent,const LLUUID& session)
{
    const double now=LLFrameTimer::getTotalSeconds();unsigned count=0;
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
std::vector<VSWorldRenderer::Packet> VSWorldScene::packets(const LLVector3& eye,float farClip)
{
    std::vector<VSWorldRenderer::Packet> packets;
    for (auto& [id,o]:mImpl->objects)
    {
        LLVector3 position;LLQuaternion rotation;std::set<U32> visited;
        if (!mImpl->transform(id,position,rotation,visited)) continue;
        const float limit=llclamp(farClip,16.f,1024.f)+o.scale.length()*2.f;
        if ((position-eye).lengthSquared()>limit*limit)
        {
            for (auto& face:o.faces) face.mesh.reset();
            o.dirty=true;continue;
        }
        for (S32 f=0;f<S32(o.faces.size());++f)
        {
            auto& face=o.faces[f];
            if (o.dirty)
            {
                const auto& geometry=o.volume->getVolumeFace(f);
                if (!geometry.mPositions || !geometry.mNormals || !geometry.mTexCoords || !geometry.mIndices ||
                    geometry.mNumVertices<=0 || geometry.mNumIndices<=0 || geometry.mNumIndices%3) continue;
                std::vector<VSWorldRenderer::Vertex> vertices;vertices.reserve(geometry.mNumVertices);
                const float angle=face.material.getRotation(),c=std::cos(angle),s=std::sin(angle);
                for (S32 v=0;v<geometry.mNumVertices;++v)
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
                face.mesh=mImpl->renderer.upload(vertices,indices);
            }
            if (!face.mesh) continue;
            const LLUUID texture=face.material.getID();if (texture.notNull()) LLUI::getUIImageByID(texture);
            const auto& color=face.material.getColor();VSWorldRenderer::Packet packet;packet.mesh=face.mesh;
            packet.image=mImpl->resources.image("remote:"+texture.asString());packet.color={color.mV[0],color.mV[1],color.mV[2],color.mV[3]};
            packet.fullbright=face.material.getFullbright()!=0;packet.alpha=color.mV[3]<1;
            packet.distanceSquared=(position-eye).lengthSquared();packets.push_back(std::move(packet));
        }
        o.dirty=false;
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
LLSD VSWorldScene::evidence() const
{
    LLSD result;result["objects"]=S32(mImpl->objects.size());result["pending_full_updates"]=S32(mImpl->pending.size());
    result["accepted"]=ll_sd_from_U64(mImpl->accepted);result["rejected"]=ll_sd_from_U64(mImpl->rejected);
    result["unsupported"]=ll_sd_from_U64(mImpl->unsupported);return result;
}

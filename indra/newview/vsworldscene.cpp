// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldscene.h"
#include "vsterrainpacket.h"
#include "vsworldavatar.h"
#include "vsworldassets.h"
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
            if (length!=17 || (extra[p+16]&LL_SCULPT_TYPE_MASK)!=LL_SCULPT_TYPE_MESH) return false;
            LLUUID asset;std::memcpy(asset.mData,extra.data()+p,UUID_BYTES);
            if (asset.isNull() || !params.setSculptID(asset,extra[p+16])) return false;
        }
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
        LLUUID id;U32 parent=0;U8 pcode=LL_PCODE_VOLUME,attachment=0;LLVector3 position,scale,velocity,angularVelocity;LLQuaternion rotation;
        LLPointer<LLVolume> volume;LLVolumeParams params;
        std::shared_ptr<const VSWorldAssets::Geometry> asset;
        S32 assetLOD=-1;
        std::vector<Face> faces;bool dirty=true;
    };
    VSWorldRenderer& renderer;VSUIResources& resources;
    VSWorldAssets assets;
    struct Request { double deadline=0; unsigned attempts=0; };
    std::map<U32,Object> objects;std::map<U32,Request> pending;
    LLUUID agent;
    float timeDilation=1;
    std::map<LLUUID,std::unique_ptr<VSWorldAvatar>> avatars;
    bool groundAt(const LLVector3& point,LLVector3& position,LLVector3& normal) const
    {
        if (!finiteVector(point) || point.mV[0]<0 || point.mV[1]<0 || point.mV[0]>=256 || point.mV[1]>=256) return false;
        const unsigned x=unsigned(point.mV[0]),y=unsigned(point.mV[1]);const float z=heights[y*257+x];
        if (!std::isfinite(z)) return false;
        position=point;position.mV[2]=z;
        normal=LLVector3(height(x?x-1:x,y,z)-height(std::min(x+1,256u),y,z),height(x,y?y-1:y,z)-height(x,std::min(y+1,256u),z),2);normal.normalize();return true;
    }
    VSWorldAvatar* avatar(const LLUUID& id,U64 handle)
    {
        const auto found=avatars.find(id);if (found!=avatars.end()) return found->second.get();
        if (id.isNull() || avatars.size()>=256) return nullptr;
        VSWorldAvatar::initializeClass();
        auto owner=std::make_unique<VSWorldAvatar>(id,id==agent,handle,renderer,resources,
            [this](const LLVector3& p,LLVector3& out,LLVector3& n){return groundAt(p,out,n);});
        auto* result=owner.get();avatars[id]=std::move(owner);return result;
    }
    double nextRequest=0;
    struct Ground { unsigned size=0;VSWorldRenderer::Mesh mesh;bool dirty=true; };
    std::map<unsigned,Ground> ground;
    std::array<float,257*257> heights;
    std::array<LLUUID,4> terrainTextures;
    std::array<float,4> terrainStarts{},terrainRanges{};
    VSWorldRenderer::Mesh water,sky;
    float waterHeight=20;bool waterKnown=false;
    float height(unsigned x,unsigned y,float fallback) const
    { const float h=heights[y*257+x];return std::isfinite(h)?h:fallback; }
    bool terrain(const std::vector<U8>& data,U8 type)
    {
        std::vector<VSTerrain::Patch> patches;
        if (!VSTerrain::decode(data,type,patches)) return false;
        // Reuse the viewer's established CPU inverse DCT, after bounded decoding.
        // No legacy surface, drawable, texture or OpenGL owner is constructed.
        struct Decoded {unsigned x,y,size;std::array<float,1024> h;};
        std::vector<Decoded> decoded;
        for (auto& p:patches)
        {
            LLGroupHeader group;group.stride=p.size;group.patch_size=U8(p.size);group.layer_type=type;
            LLPatchHeader header;header.dc_offset=p.offset;header.range=U16(p.range);header.quant_wbits=U8(p.quant);header.patchids=0;
            init_patch_decompressor(p.size);set_group_of_patch_header(&group);
            Decoded d{p.x*p.size,p.y*p.size,p.size,{}};decompress_patch(d.h.data(),p.coefficients.data(),&header);
            for (unsigned i=0;i<p.size*p.size;++i) if (!std::isfinite(d.h[i]) || std::abs(d.h[i])>65536) return false;
            decoded.push_back(std::move(d));
        }
        for (const auto& d:decoded)
        {
            for (unsigned y=0;y<d.size;++y) for (unsigned x=0;x<d.size;++x) heights[(d.y+y)*257+d.x+x]=d.h[y*d.size+x];
            ground[d.y*257+d.x]=Ground{d.size,{},true};
        }
        // Neighbor samples affect edge stitching and normals. Rebuild only the
        // bounded set of received patches on the next rendering frame.
        for (auto& [key,g]:ground) g.dirty=true;
        return true;
    }
    void surfaces(std::vector<VSWorldRenderer::Packet>& packets,const LLVector3& eye)
    {
        if (!sky) sky=renderer.upload({{-1,-1,0,0,0,1,0,0},{1,-1,0,0,0,1,1,0},{1,1,0,0,0,1,1,1},{-1,1,0,0,0,1,0,1}},{0,1,2,0,2,3});
        VSWorldRenderer::Packet background;background.mesh=sky;background.background=true;background.fullbright=true;packets.push_back(background);
        for (auto& [key,g]:ground)
        {
            const unsigned x0=key%257,y0=key/257,n=g.size;
            if (g.dirty)
            {
                std::vector<VSWorldRenderer::Vertex> vertices;std::vector<unsigned> indices;
                for (unsigned y=0;y<=n;++y) for (unsigned x=0;x<=n;++x)
                {
                    const unsigned gx=x0+x,gy=y0+y;
                    const float fallback=heights[(y0+std::min(y,n-1))*257+x0+std::min(x,n-1)];
                    const float z=height(gx,gy,fallback);
                    LLVector3 normal(height(gx?gx-1:gx,gy,z)-height(std::min(gx+1,256u),gy,z),
                        height(gx,gy?gy-1:gy,z)-height(gx,std::min(gy+1,256u),z),2);normal.normalize();
                    vertices.push_back({float(gx),float(gy),z,normal.mV[0],normal.mV[1],normal.mV[2],float(gx)/16,1-float(gy)/16});
                    if (x<n && y<n) { const unsigned i=y*(n+1)+x;indices.insert(indices.end(),{i,i+1,i+n+2,i,i+n+2,i+n+1}); }
                }
                g.mesh=renderer.upload(vertices,indices);g.dirty=false;
            }
            // First surface pass uses a single height-selected simulator detail
            // map per patch; four-way terrain splatting is a later material pass.
            const float h=height(x0+n/2,y0+n/2,0),start=terrainStarts[0],range=std::max(terrainRanges[0],1.f);
            const unsigned level=unsigned(std::clamp((h-start)/range*4.f,0.f,3.f));const auto& texture=terrainTextures[level];
            if (texture.notNull()) LLUI::getUIImageByID(texture);
            VSWorldRenderer::Packet p;p.mesh=g.mesh;p.image=resources.image("remote:"+texture.asString());
            if (!p.image) p.color={.32f,.42f,.18f,1};
            packets.push_back(std::move(p));
        }
        if (waterKnown)
        {
            if (!water) water=renderer.upload({{0,0,waterHeight,0,0,1,0,0},{256,0,waterHeight,0,0,1,1,0},
                {256,256,waterHeight,0,0,1,1,1},{0,256,waterHeight,0,0,1,0,1}},{0,1,2,0,2,3});
            VSWorldRenderer::Packet p;p.mesh=water;p.color={.08f,.28f,.38f,.65f};p.alpha=true;
            p.distanceSquared=(LLVector3(128,128,waterHeight)-eye).lengthSquared();packets.push_back(std::move(p));
        }
    }
    U64 accepted=0,rejected=0,unsupported=0;
    Impl(VSWorldRenderer& r,VSUIResources& u):renderer(r),resources(u) { heights.fill(std::numeric_limits<float>::quiet_NaN()); }
    void request(U32 id) { if (id && pending.size()<1024) pending.try_emplace(id); }
    void eraseObject(U32 id)
    {
        dirtyChildren(id);
        if (const auto object=objects.find(id);object!=objects.end() && object->second.pcode==LL_PCODE_LEGACY_AVATAR)
            avatars.erase(object->second.id);
        objects.erase(id);pending.erase(id);
    }
    void replace(U32 id,Object&& object)
    {
        if (const auto old=objects.find(id);old!=objects.end() && old->second.pcode==LL_PCODE_LEGACY_AVATAR &&
            (old->second.id!=object.id || object.pcode!=LL_PCODE_LEGACY_AVATAR)) avatars.erase(old->second.id);
        objects[id]=std::move(object);pending.erase(id);dirtyChildren(id);
    }
    bool compressed(std::vector<U8>& bytes)
    {
        LLDataPackerBinaryBuffer dp(bytes.data(),S32(bytes.size()));
        Object object;U32 id,ignored,flags;U8 pcode,state,value;LLVector3 rotation,unused;LLUUID owner;
        if (!dp.unpackUUID(object.id,"ID") || !dp.unpackU32(id,"LocalID") || !dp.unpackU8(pcode,"PCode") ||
            !dp.unpackU8(state,"State")) return false;
        if (pcode!=LL_PCODE_VOLUME) { eraseObject(id);++unsupported;return true; }
        if (!dp.unpackU32(ignored,"CRC") || !dp.unpackU8(value,"Material") || !dp.unpackU8(value,"ClickAction") ||
            !dp.unpackVector3(object.scale,"Scale") || !dp.unpackVector3(object.position,"Pos") ||
            !dp.unpackVector3(rotation,"Rot") || !dp.unpackU32(flags,"SpecialCode") || !dp.unpackUUID(owner,"Owner")) return false;
        object.rotation.unpackFromVector3(rotation);object.attachment=state;
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
        bool supported=true;LLUUID mesh;U8 sculptType=LL_SCULPT_TYPE_NONE;
        for (unsigned n=0;n<parameters;++n)
        {
            U16 type;
            if (!dp.unpackU16(type,"param_type") || !dp.unpackBinaryData(scratch,length,"param_data")) return false;
            if (type==0x10) supported=false;
            if (type==0x30)
            {
                if (length!=17 || (scratch[16]&LL_SCULPT_TYPE_MASK)!=LL_SCULPT_TYPE_MESH) supported=false;
                else {std::memcpy(mesh.mData,scratch,UUID_BYTES);sculptType=scratch[16];if (mesh.isNull()) supported=false;}
            }
        }
        if (flags&0x10) if (!dp.unpackUUID(owner,"SoundUUID") || !dp.unpackF32(number,"SoundGain") ||
            !dp.unpackU8(value,"SoundFlags") || !dp.unpackF32(number,"SoundRadius")) return false;
        if (flags&0x100) if (!string("NV")) return false;
        if (!supported) { eraseObject(id);++unsupported;return true; }
        if (!id || object.id.isNull() || object.parent==id || !finiteVector(object.position) || !finiteVector(object.scale) ||
            !object.rotation.isFinite() || object.scale.mV[0]<=0 || object.scale.mV[1]<=0 || object.scale.mV[2]<=0 ||
            object.scale.lengthSquared()>1.e8f || (objects.size()>=maxObjects && !objects.count(id))) return false;
        LLVolumeParams params;if (!LLVolumeMessage::unpackVolumeParams(&params,dp)) return false;
        if (mesh.notNull() && !params.setSculptID(mesh,sculptType)) return false;
        object.params=params;
        if (mesh.isNull()) object.volume=new LLVolume(params,2.f);
        const S32 faces=object.volume ? object.volume->getNumVolumeFaces() : 8;
        if (faces<=0 || faces>45) return false;
        LLPrimitive material;material.setNumTEs(U8(faces));
        if (material.unpackTEMessage(dp)==TEM_INVALID) return false;
        for (S32 f=0;f<faces;++f) object.faces.push_back({{},*material.getTE(U8(f))});
        replace(id,std::move(object));++accepted;return true;
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
VSWorldScene::VSWorldScene(VSWorldRenderer& r,VSUIResources& u):mImpl(std::make_unique<Impl>(r,u)) {}
VSWorldScene::~VSWorldScene() { mImpl.reset();VSWorldAvatar::cleanupClass(); }
void VSWorldScene::reset() { mImpl->assets.reset();mImpl->avatars.clear();mImpl->ground.clear();mImpl->heights.fill(std::numeric_limits<float>::quiet_NaN());mImpl->water.reset();mImpl->waterKnown=false;mImpl->terrainTextures={};mImpl->objects.clear();mImpl->pending.clear();mImpl->nextRequest=0;mImpl->accepted=mImpl->rejected=mImpl->unsupported=0; }
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
    const std::string name=msg.getMessageName();auto& state=*mImpl;state.agent=agent;
    if (name=="AvatarAppearance" || name=="AvatarAnimation")
    {
        LLUUID id;msg.getUUID("Sender","ID",id);auto* avatar=state.avatar(id,regionHandle);
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
            LLVector3 velocity,angularVelocity;
            for (unsigned c=0;c<3;++c)
            {
                const unsigned v=18+offset+c*2,a=38+offset+c*2;
                velocity.mV[c]=float(U16(bytes[v]) | U16(bytes[v+1])<<8)/65535.f*256.f-128.f;
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
            found->second.attachment=bytes[4];found->second.dirty=true;
            state.dirtyChildren(id);++state.accepted;
            if (msg.getSize("ObjectData",i,"TextureEntry")>0) state.request(id);
            continue;
        }
        msg.getU32("ObjectData","ID",id,i);
        U8 pcode,attachment;msg.getU8("ObjectData","PCode",pcode,i);msg.getU8("ObjectData","State",attachment,i);
        if (!id || (pcode!=LL_PCODE_VOLUME && pcode!=LL_PCODE_LEGACY_AVATAR)) { ++state.unsupported;state.eraseObject(id);continue; }
        const S32 length=msg.getSize("ObjectData",i,"ObjectData");
        if ((pcode==LL_PCODE_LEGACY_AVATAR && length!=76 && length!=140) || (pcode!=LL_PCODE_LEGACY_AVATAR && length!=60 && length!=124)) { ++state.rejected;continue; }
        std::vector<U8> bytes(length);msg.getBinaryData("ObjectData","ObjectData",bytes.data(),length,i,length);
        const unsigned offset=pcode==LL_PCODE_LEGACY_AVATAR?16:0;
        Impl::Object object;object.pcode=pcode;object.attachment=attachment;object.position=vectorAt(bytes.data()+offset);
        object.velocity=vectorAt(bytes.data()+offset+12);object.angularVelocity=vectorAt(bytes.data()+offset+48);
        object.rotation.unpackFromVector3(vectorAt(bytes.data()+offset+36));
        msg.getVector3("ObjectData","Scale",object.scale,i);msg.getUUID("ObjectData","FullID",object.id,i);
        msg.getU32("ObjectData","ParentID",object.parent,i);
        if (!finiteVector(object.position) || !finiteVector(object.scale) || !object.rotation.isFinite() ||
            object.scale.mV[0]<=0 || object.scale.mV[1]<=0 || object.scale.mV[2]<=0 || object.scale.lengthSquared()>1.e8f ||
            object.parent==id || object.id.isNull()) { ++state.rejected;continue; }
        if (pcode==LL_PCODE_LEGACY_AVATAR)
        {
            if (!finiteVector(object.velocity) || !finiteVector(object.angularVelocity) || !state.avatar(object.id,regionHandle)) {++state.rejected;continue;}
            state.replace(id,std::move(object));++state.accepted;continue;
        }
        const S32 extraSize=msg.getSize("ObjectData",i,"ExtraParams");
        if (extraSize<0 || extraSize>4096) { ++state.rejected;continue; }
        std::vector<U8> extra(extraSize);
        if (extraSize) msg.getBinaryData("ObjectData","ExtraParams",extra.data(),extraSize,i,extraSize);
        LLVolumeParams params;
        const char* objectBlock=LLMessageStringTable::getInstance()->getString("ObjectData");
        if (!LLVolumeMessage::unpackVolumeParams(&params,&msg,objectBlock,i)) { ++state.rejected;continue; }
        if (!volumeExtras(extra,params)) { ++state.unsupported;state.eraseObject(id);continue; }
        object.params=params;
        if (params.getSculptID().isNull()) object.volume=new LLVolume(params,2.f);
        const S32 faces=object.volume ? object.volume->getNumVolumeFaces() : 8;
        if (faces<=0 || faces>45 || (state.objects.size()>=maxObjects && !state.objects.count(id))) { ++state.rejected;continue; }
        LLPrimitive material;material.setNumTEs(U8(faces));
        if (material.unpackTEMessage(&msg,objectBlock,i)==TEM_INVALID) { ++state.rejected;continue; }
        for (S32 f=0;f<faces;++f) object.faces.push_back({{},*material.getTE(U8(f))});
        state.replace(id,std::move(object));++state.accepted;
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
std::vector<VSWorldRenderer::Packet> VSWorldScene::packets(const LLVector3& eye,float farClip,unsigned viewportHeight)
{
    std::vector<VSWorldRenderer::Packet> packets;
    mImpl->assets.pump();
    mImpl->surfaces(packets,eye);
    // Evaluate avatar roots and bones before resolving attached primitive transforms.
    for (const auto& [id,o]:mImpl->objects) if (o.pcode==LL_PCODE_LEGACY_AVATAR)
    {
        LLVector3 p;LLQuaternion q;std::set<U32> visited;
        const auto avatar=mImpl->avatars.find(o.id);
        if (avatar!=mImpl->avatars.end() && mImpl->transform(id,p,q,visited))
            avatar->second->transform(p,q,o.velocity,o.angularVelocity);
    }
    for (auto& [id,avatar]:mImpl->avatars) avatar->append(packets,eye,farClip,viewportHeight,mImpl->timeDilation);
    for (auto& [id,o]:mImpl->objects)
    {
        if (o.pcode==LL_PCODE_LEGACY_AVATAR) continue;
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
            mImpl->assets.request(o.params,lod);
            auto asset=mImpl->assets.geometry(o.params.getSculptID(),lod);
            if (!asset) asset=o.asset; // Keep a previously decoded LOD while its replacement loads.
            if (!asset) continue;
            if (asset!=o.asset) {o.asset=asset;o.assetLOD=asset->lod;o.volume=asset->volume;o.dirty=true;}
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
        const S32 count=std::min(S32(o.faces.size()),o.volume->getNumVolumeFaces());
        for (S32 f=0;f<count;++f)
        {
            auto& face=o.faces[f];
            if (o.dirty || riggedOwner)
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
            return mImpl->transform(id,position,rotation,visited);
        }
    return false;
}
LLSD VSWorldScene::evidence() const
{
    LLSD result;result["avatars"]=LLSD::emptyArray();for (const auto& [id,avatar]:mImpl->avatars) result["avatars"].append(avatar->evidence());result["terrain_patches"]=S32(mImpl->ground.size());result["water_height_known"]=mImpl->waterKnown;result["sky"]= "native_default_gradient";result["objects"]=S32(mImpl->objects.size());result["pending_full_updates"]=S32(mImpl->pending.size());
    const auto stats=mImpl->renderer.statistics();
    result["draw_calls"]=ll_sd_from_U64(stats.draws);result["constant_allocations"]=ll_sd_from_U64(stats.constantBuffersCreated);
    result["binding_allocations"]=ll_sd_from_U64(stats.bindingsCreated);result["mesh_uploads"]=ll_sd_from_U64(stats.meshUploads);
    result["mesh_upload_bytes"]=ll_sd_from_U64(stats.meshUploadBytes);result["submission_cpu_ms"]=stats.cpuMilliseconds;
    result["accepted"]=ll_sd_from_U64(mImpl->accepted);result["rejected"]=ll_sd_from_U64(mImpl->rejected);
    result["unsupported"]=ll_sd_from_U64(mImpl->unsupported);return result;
}

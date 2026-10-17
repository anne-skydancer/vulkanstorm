// Exercise the actual local-file decoder/watcher and native publication. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3replay.h"
#include "lllocalbitmaps.h"
#include "lllocalgltfmaterials.h"
#include "vsnativesession.h"
#include "vsworldscene.h"
#include "llprimitive.h"
#include "llvolumemessage.h"
#include "llregionhandle.h"
#include "llframetimer.h"
#include "message.h"
#include <map>
#include <cmath>
#include "llimagepng.h"
#include "llui.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "llsdutil.h"
#include "llsdserialize.h"
#include "llgltfmateriallist.h"
#include "llfetchedgltfmaterial.h"
#include "lldatapacker.h"
#include "vsmaterial.h"
#include "llmaterial.h"
#include "llmaterialid.h"
#include "indra_constants.h"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>
namespace
{
void require(bool value,const char* message)
{
    if(!value) {LL_WARNS("NativeR3Replay")<<message<<LL_ENDL;throw std::runtime_error(message);}
}
LLPointer<LLImageRaw> pattern(unsigned width,unsigned height,unsigned phase)
{
    LLPointer<LLImageRaw> raw=new LLImageRaw(width,height,4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
    {
        auto* pixel=raw->getData()+(y*width+x)*4;
        pixel[0]=x<width/2?255:0;pixel[1]=y<height/2?255:0;
        pixel[2]=phase==0?0:phase==1?255:128;pixel[3]=255;
    }
    return raw;
}
void writePNG(const std::filesystem::path& file,unsigned width,unsigned height,unsigned phase,
              const std::filesystem::file_time_type& timestamp)
{
    LLPointer<LLImagePNG> png=new LLImagePNG;
    require(png->encode(pattern(width,height,phase),0.f) && png->save(file.string()),"R3 local PNG encode failed");
    std::filesystem::last_write_time(file,timestamp);
}
LLSD capture(VSUIResources& resources,const LLUUID& id,const std::filesystem::path& root,
             const std::string& name,unsigned width,unsigned height,unsigned phase)
{
    const auto key="remote:"+id.asString();
    const auto image=resources.image(key);
    require(image && VSUIRenderer::imageExtent(image)==std::array<unsigned,2>{width,height},"R3 local image extent mismatch");
    const auto pixels=resources.readbackImage(key);
    const auto expected=pattern(width,height,phase);
    require(pixels.size()==std::size_t(width)*height*4,"R3 local readback size mismatch");
    for(unsigned y=0;y<height;++y)
        require(!std::memcmp(pixels.data()+std::size_t(y)*width*4,expected->getData()+std::size_t(height-1-y)*width*4,width*4),
                "R3 local PNG pixels/orientation differ from the original pattern");
    const auto relative=std::filesystem::path("r3")/"local-images"/(name+".rgba");
    std::ofstream output(root/relative,std::ios::binary);
    output.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
    require(output.good(),"R3 local image readback write failed");
    LLSD row;row["id"]=id;row["width"]=S32(width);row["height"]=S32(height);row["phase"]=S32(phase);
    row["generation"]=ll_sd_from_U64(VSUIRenderer::generation(image));row["file"]=relative.generic_string();
    return row;
}

void solidPNG(const std::filesystem::path& file,unsigned width,unsigned height,const std::array<U8,4>& color)
{
    LLPointer<LLImageRaw> raw=new LLImageRaw(width,height,4);raw->clear(color[0],color[1],color[2],color[3]);
    LLPointer<LLImagePNG> png=new LLImagePNG;
    require(png->encode(raw,0.f) && png->save(file.string()),"R3 local material PNG write failed");
}
LLSD localMaterial(VSUIResources& resources,const std::filesystem::path& root)
{
    const auto folder=root/"r3"/"local-material";std::filesystem::create_directories(folder);
    const auto file=folder/"material.gltf";
    const char* document=R"({"asset":{"version":"2.0"},"materials":[{"name":"Native reload","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":2},"metallicFactor":0.3,"roughnessFactor":0.6},"normalTexture":{"index":1},"emissiveTexture":{"index":3},"occlusionTexture":{"index":4},"doubleSided":true,"alphaMode":"MASK","alphaCutoff":0.4}],"textures":[{"source":0},{"source":1},{"source":2},{"source":3},{"source":4}],"images":[{"uri":"base.png"},{"uri":"normal.png"},{"uri":"mr.png"},{"uri":"emissive.png"},{"uri":"occlusion.png"}]})";
    auto timestamp=std::filesystem::file_time_type::clock::now();
    auto write=[&] {std::ofstream output(file);output<<document;output.close();require(bool(output),"R3 local material document write failed");std::filesystem::last_write_time(file,timestamp);};
    solidPNG(folder/"normal.png",16,16,{128,128,255,255});
    solidPNG(folder/"mr.png",16,16,{11,64,192,255});
    solidPNG(folder/"emissive.png",16,16,{9,19,29,255});
    solidPNG(folder/"occlusion.png",8,8,{77,0,0,255});
    solidPNG(folder/"base.png",16,16,{73,121,199,255});write();
    LLPointer<LLLocalGLTFMaterial> material=new LLLocalGLTFMaterial(file.string(),0);
    require(material->updateSelf() && material->isLoaded(),"R3 actual local PBR material load failed");
    const auto id=material->getWorldID();const auto identities=material->mTextureId;
    std::array<VSUIRenderer::Image,4> retained;
    std::array<std::vector<U8>,4> retainedPixels;
    for(unsigned index=0;index<4;++index) {
        retained[index]=resources.image("remote:"+identities[index].asString());
        require(bool(retained[index]),"R3 local material channel was not published");
        retainedPixels[index]=VSUIRenderer::imagePixels(retained[index]);
    }
    LLSD result;
    auto captureMaterial=[&](const char* stage,unsigned phase) {
        require(material->getWorldID()==id && material->mTextureId==identities,"R3 material reload changed stable identities");
        require(!material->mBaseColorTexture && !material->mNormalTexture && !material->mMetallicRoughnessTexture && !material->mEmissiveTexture,
                "R3 native local material created GL fetched textures");
        require(std::abs(material->mMetallicFactor-.3f)<1.e-6f && std::abs(material->mRoughnessFactor-.6f)<1.e-6f && material->mDoubleSided,
                "R3 local material lost shared parser factors");
        const std::array<std::array<U8,4>,4> colors{{phase?std::array<U8,4>{199,73,121,255}:std::array<U8,4>{73,121,199,255},
            {128,128,255,255},{77,64,192,255},{9,19,29,255}}};
        for(unsigned index=0;index<4;++index)
        {
            const auto key="remote:"+identities[index].asString();const auto image=resources.image(key);
            const auto pixels=resources.readbackImage(key);require(pixels.size()==16*16*4,"R3 local material readback size mismatch");
            for(unsigned offset=0;offset<pixels.size();offset+=4) require(!std::memcmp(pixels.data()+offset,colors[index].data(),4),"R3 local material packing differs from reference pixels");
            const auto relative=std::filesystem::path("r3")/"local-material"/(std::string(stage)+"-"+std::to_string(index)+".rgba");
            std::ofstream output(root/relative,std::ios::binary);output.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            require(output.good(),"R3 local material readback archive failed");
            LLSD row;row["id"]=identities[index];row["generation"]=ll_sd_from_U64(VSUIRenderer::generation(image));row["file"]=relative.generic_string();
            result[stage].append(row);
        }
    };
    captureMaterial("initial",0);
    solidPNG(folder/"base.png",16,16,{199,73,121,255});timestamp+=std::chrono::seconds(2);write();
    require(material->updateSelf(),"R3 local material file reload failed");captureMaterial("replaced",1);
    {std::ofstream broken(folder/"mr.png",std::ios::binary);broken<<"incomplete PNG";}
    timestamp+=std::chrono::seconds(2);write();
    require(!material->updateSelf(),"R3 incomplete material channel was accepted");captureMaterial("refused",1);
    solidPNG(folder/"mr.png",16,16,{11,64,192,255});timestamp+=std::chrono::seconds(2);write();
    require(material->updateSelf(),"R3 local material did not recover from incomplete image");captureMaterial("recovered",1);
    for(unsigned index=0;index<4;++index)
        require(retained[index] && VSUIRenderer::imagePixels(retained[index])==retainedPixels[index],"R3 retained material image was mutated");
    gGLTFMaterialList.removeMaterial(id);material=nullptr;
    for(const auto& texture:identities) require(!resources.image("remote:"+texture.asString()),"R3 local material deletion leaked channel resources");
    result["stable_identities"]=true;result["retired"]=true;result["retained_generations_unchanged"]=true;result["native_only"]=true;
    return result;
}
}
void vs_r3_local_images(LLSD& evidence)
{
    if(!std::getenv("VS_VULKAN_REPLAY_LOCAL_IMAGES") || evidence.has("r3_local_images")) return;
    const auto root=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
    const auto folder=root/"r3"/"local-images";std::filesystem::create_directories(folder);
    const auto file=folder/"watch.png";
    const bool compression=gSavedSettings.getBOOL("RenderCompressTextures");
    struct Restore {bool value;~Restore(){gSavedSettings.setBOOL("RenderCompressTextures",value);}} restore{compression};
    gSavedSettings.setBOOL("RenderCompressTextures",false);
    auto timestamp=std::filesystem::file_time_type::clock::now();
    writePNG(file,32,32,0,timestamp);
    auto bitmap=std::make_unique<LLLocalBitmap>(file.string());
    require(bitmap->getValid(),"R3 actual local bitmap decoder failed");
    const auto id=bitmap->getWorldID();
    auto facade=LLUI::getUIImageByID(id);
    require(facade && vs_ui_image_ready(id),"R3 local bitmap was not exposed to ordinary UI consumers");
    auto& resources=gViewerWindow->nativeContext()->resources();
    auto original=resources.image("remote:"+id.asString());
    const auto originalPixels=VSUIRenderer::imagePixels(original);
    LLSD result;result["initial"]=capture(resources,id,root,"initial",32,32,0);
    unsigned updates=0,retirements=0;
    auto connection=bitmap->setChangedCallback([&](const LLUUID&,const LLUUID& oldID,const LLUUID& newID) {
        require(oldID==id,"R3 local notification changed the original identity");
        if(newID==id) ++updates;
        else {require(newID==IMG_DEFAULT || newID.isNull(),"R3 local deletion notification has wrong identity");++retirements;}
    });
    timestamp+=std::chrono::seconds(2);writePNG(file,32,16,1,timestamp);
    require(bitmap->updateSelf() && bitmap->getWorldID()==id,"R3 local file reload failed or changed stable identity");
    require(LLUI::getUIImageByID(id)==facade,"R3 local reload replaced the held UI facade");
    result["replaced"]=capture(resources,id,root,"replaced",32,16,1);
    require(VSUIRenderer::imagePixels(original)==originalPixels,"R3 local reload mutated a retained image generation");
    const auto generation=resources.generation("remote:"+id.asString());
    {std::ofstream broken(file,std::ios::binary);broken<<"incomplete PNG write";}
    timestamp+=std::chrono::seconds(2);std::filesystem::last_write_time(file,timestamp);
    require(!bitmap->updateSelf() && resources.generation("remote:"+id.asString())==generation,
            "R3 incomplete local file discarded the last valid image");
    result["refused"]=capture(resources,id,root,"refused",32,16,1);
    timestamp+=std::chrono::seconds(2);writePNG(file,16,32,2,timestamp);
    require(bitmap->updateSelf() && bitmap->getWorldID()==id,"R3 local decoder did not retry the completed file");
    result["recovered"]=capture(resources,id,root,"recovered",16,32,2);
    bitmap.reset();connection.disconnect();
    require(updates==2 && retirements==2 && vs_native_image_id(id)==IMG_DEFAULT && !resources.image("remote:"+id.asString()),
            "R3 local deletion did not retire resources and notify shared consumers");
    require(VSUIRenderer::imagePixels(original)==originalPixels,"R3 local deletion changed a retained generation");
    result["updates"]=S32(updates);result["retirements"]=S32(retirements);
    result["stable_facade"]=true;result["retained_generation_unchanged"]=true;result["default_alias"]=true;
    evidence["r3_local_material"]=localMaterial(resources,root);
    evidence["r3_local_images"]=result;
}

namespace
{
const LLUUID bumpObject("b3000000-0000-0000-0000-000000000001");
const LLUUID bumpSource("b3000000-0000-0000-0000-000000000002");
const LLUUID remoteObject("b3200000-0000-0000-0000-000000000001");
const LLUUID legacyObject("b3300000-0000-0000-0000-000000000001");
const LLUUID legacyMaterial("b3400000-0000-0000-0000-000000000001");
const std::array<LLUUID,2> legacySources{LLUUID("b3600000-0000-0000-0000-000000000001"),LLUUID("b3600000-0000-0000-0000-000000000002")};
const std::array<LLUUID,4> remoteAssets{
    LLUUID("b3100000-0000-0000-0000-000000000001"),LLUUID("b3100000-0000-0000-0000-000000000002"),
    LLUUID("b3100000-0000-0000-0000-000000000003"),LLUUID("b3100000-0000-0000-0000-000000000004")};
struct WorldImages
{
    unsigned step=0;
    float originalScale=0;
    double deadline=0;
    bool source=false;
    LLSD evidence;
    std::vector<std::weak_ptr<const VSUIRenderer::Texture>> images;
    std::array<LLPointer<LLFetchedGLTFMaterial>,4> remote;
    bool remoteRetiring=false,remoteReplacing=false;
    bool legacyRetiring=false;
    bool legacySourcesOwned=false;
    LLSD legacy;
};
std::unique_ptr<WorldImages> worldImages;
void bumpObjectPacket(VSNativeSession& session,bool distant=false,bool pbr=false,bool recovered=false,bool legacy=false)
{
    LLMessageSystem* msg=gMessageSystem;msg->newMessage("ObjectUpdate");msg->nextBlock("RegionData");
    msg->addU64("RegionHandle",to_region_handle(256,512));msg->addU16("TimeDilation",65535);
    msg->nextBlock("ObjectData");msg->addU32("ID",legacy?992:pbr?991:990);msg->addU8("State",0);msg->addUUID("FullID",legacy?legacyObject:pbr?remoteObject:bumpObject);
    msg->addU32("CRC",0);msg->addU8("PCode",LL_PCODE_VOLUME);msg->addU8("Material",0);msg->addU8("ClickAction",0);
    msg->addVector3("Scale",LLVector3(1,1,1));
    const auto position=session.position()+(distant?LLVector3(5000,5000,5000):legacy?LLVector3(6,-6,1):pbr?LLVector3(4,-4,1):LLVector3(3,4,0));
    const F32 pose[15]={position.mV[0],position.mV[1],position.mV[2],0,0,0,0,0,0,0,0,0,0,0,0};
    msg->addBinaryData("ObjectData",pose,sizeof(pose));msg->addU32("ParentID",0);msg->addU32("UpdateFlags",0);
    LLVolumeParams params;LLVolumeMessage::packVolumeParams(&params,msg);
    LLPrimitive material;material.setNumTEs(6);
    for(unsigned face=0;face<6;++face)
    {
        material.setTETexture(U8(face),bumpSource);material.setTEBumpmap(U8(face),pbr||legacy?0:U8(face%3+1));
        if(legacy)
        {
            if(face) {auto id=legacyMaterial;id.mData[15]+=U8(face-1);material.setTEMaterialID(U8(face),LLMaterialID(id));}
            material.setTEFullbright(U8(face),face==2 || face==5);
            material.setTEShiny(U8(face),face==0?1:face==1?2:face==3?3:0);
            if(face==4) material.setTEColor(U8(face),LLColor4(1,1,1,.5f));
        }
    }
    material.packTEMessage(msg);
    for(const char* field:{"TextureAnim","NameValue","Data","Text","MediaURL","PSBlock"}) msg->addBinaryData(field,nullptr,0);
    if(pbr)
    {
        LLRenderMaterialParams params;for(unsigned face=0;face<6;++face) params.setMaterial(U8(face),remoteAssets[recovered?3:0]);
        U8 packed[1024];LLDataPackerBinaryBuffer packer(packed,sizeof(packed));require(params.pack(packer),"R3 PBR extra parameters failed to pack");
        const U32 size=packer.getCurrentSize();const U16 type=LLNetworkData::PARAMS_RENDER_MATERIAL;
        std::vector<U8> extra{1,U8(type),U8(type>>8),U8(size),U8(size>>8),U8(size>>16),U8(size>>24)};
        extra.insert(extra.end(),packed,packed+size);msg->addBinaryData("ExtraParams",extra.data(),S32(extra.size()));
    }
    else msg->addBinaryData("ExtraParams",nullptr,0);
    const U8 white[4]={255,255,255,255};msg->addBinaryData("TextColor",white,4);
    msg->addUUID("Sound",LLUUID::null);msg->addUUID("OwnerID",LLUUID::null);
    msg->addF32("Gain",0);msg->addU8("Flags",0);msg->addF32("Radius",0);msg->addU8("JointType",0);
    msg->addVector3("JointPivot",LLVector3::zero);msg->addVector3("JointAxisOrAnchor",LLVector3::zero);
    require(msg->sendReliable(session.host())>0,"R3 bump ObjectUpdate transport failed");
}
void startRemoteMaterials(WorldImages& state,VSNativeSession& session)
{
    const auto root=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/"r3"/"remote-materials";
    std::filesystem::create_directories(root);
    LLGLTFMaterial material;material.mTextureId.fill(bumpSource);
    material.mBaseColor=LLColor4(.25f,.5f,.75f,.625f);material.mEmissiveColor=LLColor3(.125f,.25f,.375f);
    material.mMetallicFactor=.3f;material.mRoughnessFactor=.6f;material.mDoubleSided=true;
    material.mAlphaMode=LLGLTFMaterial::ALPHA_MODE_MASK;material.mAlphaCutoff=.25f;
    for(auto& transform:material.mTextureTransform) {transform.mOffset.set(.1f,.2f);transform.mScale.set(2,3);transform.mRotation=.25f;}
    for(unsigned index=0;index<remoteAssets.size();++index)
    {
        if(index==3) material.mBaseColor.mV[0]=.5f;
        const auto file=root/(remoteAssets[index].asString()+".asset");
        {
            std::ofstream output(file,std::ios::binary);
            if(index==2) output<<std::string(1024*1024+1,' ');
            else {LLSD asset;asset["version"]=LLGLTFMaterial::ASSET_VERSION;asset["type"]=LLGLTFMaterial::ASSET_TYPE;
                asset["data"]=index==1?"invalid native material fixture":material.asJSON();LLSDSerialize::toPrettyXML(asset,output);}
            require(output.good(),"R3 remote material fixture write failed");
        }
        state.remote[index]=gGLTFMaterialList.getMaterial(remoteAssets[index]);
        require(state.remote[index].notNull(),"R3 remote material request was not admitted");
    }
    bumpObjectPacket(session,false,true);
}
void startLegacyMaterial(WorldImages& state,VSNativeSession& session)
{
    const auto root=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/"r3";
    LLMaterial material;material.setNormalID(legacySources[0]);material.setSpecularID(legacySources[1]);
    material.setNormalOffset(.125f,-.25f);material.setNormalRepeat(2,3);material.setNormalRotation(.25f);
    material.setSpecularOffset(-.125f,.375f);material.setSpecularRepeat(.75f,1.25f);material.setSpecularRotation(-.5f);
    material.setSpecularLightColor(LLColor4U(64,128,192,255));material.setSpecularLightExponent(128);
    material.setEnvironmentIntensity(32);material.setDiffuseAlphaMode(LLMaterial::DIFFUSE_ALPHA_MODE_MASK);material.setAlphaMaskCutoff(64);
    LLSD data;
    const U8 modes[]={LLMaterial::DIFFUSE_ALPHA_MODE_NONE,LLMaterial::DIFFUSE_ALPHA_MODE_MASK,
        LLMaterial::DIFFUSE_ALPHA_MODE_EMISSIVE,LLMaterial::DIFFUSE_ALPHA_MODE_BLEND,LLMaterial::DIFFUSE_ALPHA_MODE_DEFAULT};
    for(unsigned index=0;index<5;++index)
    {
        auto id=legacyMaterial;id.mData[15]+=U8(index);material.setDiffuseAlphaMode(modes[index]);
        data[index]["ID"]=LLMaterialID(id).asLLSD();data[index]["Material"]=material.asLLSD();
    }
    const auto compressed=zip_llsd(data);require(!compressed.empty(),"R3 legacy material compression failed");
    LLSD envelope;envelope["Zipped"]=LLSD::Binary(compressed.begin(),compressed.end());
    {std::ofstream output(root/"legacy-materials.xml");LLSDSerialize::toPrettyXML(envelope,output);require(output.good(),"R3 legacy material envelope write failed");}
    {std::ofstream output(root/"legacy-materials.bin",std::ios::binary);LLSDSerialize::toBinary(data,output);require(output.good(),"R3 legacy material binary write failed");}
    state.legacySourcesOwned=true;
    for(unsigned index=0;index<2;++index)
    {
        LLPointer<LLImageRaw> raw=new LLImageRaw(16,16,4);
        const std::array<U8,4> color=index==0?std::array<U8,4>{128,128,255,255}:std::array<U8,4>{11,22,33,255};
        for(unsigned offset=0;offset<16*16*4;offset+=4) std::copy(color.begin(),color.end(),raw->getData()+offset);
        require(vs_publish_local_image(legacySources[index],*raw),"R3 legacy channel source publication failed");
    }
    bumpObjectPacket(session,false,false,false,true);
}
bool legacyMaterialFaces(WorldImages& state,VSNativeSession& session,VSWorldScene& world,VSUIResources& resources,LLSD& report)
{
    if(report.has("r3_legacy_material")) return true;
    if(state.legacyRetiring)
    {
        if(world.object(legacyObject)) return false;
        for(const auto& id:legacySources) vs_retire_local_image(id);
        state.legacySourcesOwned=false;
        state.legacy["object_retired"]=true;report["r3_legacy_material"]=state.legacy;return true;
    }
    unsigned faces=0;std::shared_ptr<const VSMaterial::State> surface;
    const auto normal=resources.image("remote:"+legacySources[0].asString()),specular=resources.image("remote:"+legacySources[1].asString());
    for(const auto& packet:world.packets(session.position()+LLVector3(0,-4,1),128,480,640))
        if(std::equal(packet.objectIdentity.begin(),packet.objectIdentity.end(),legacyObject.mData))
        {
            if(!packet.image || !packet.surface) return false;
            const auto face=packet.faceIndex;require(face<6 && !packet.surface->pbr,"R3 legacy face identity or material family differs");
            if(face)
            {
                if(packet.channels[0]!=normal || packet.channels[1]!=specular) return false;
                if(surface) require(surface->uv[VSMaterial::Normal]==packet.surface->uv[VSMaterial::Normal] &&
                    surface->uv[VSMaterial::ORMOrSpecular]==packet.surface->uv[VSMaterial::ORMOrSpecular],"R3 legacy faces disagree on channel UVs");
                surface=packet.surface;
            }
            else require(!packet.channels[0] && !packet.channels[1],"R3 unmaterialed diffuse face inherited material channels");
            auto& variant=state.legacy["variants"][face];variant["fullbright"]=packet.surface->fullbright;
            variant["shiny"]=S32(packet.surface->shiny);variant["emissive_alpha"]=packet.surface->emissiveAlpha;
            variant["base_alpha"]=packet.surface->base[3];
            variant["alpha"]=packet.surface->alpha==VSWorldContracts::AlphaMode::Mask?"mask":packet.surface->alpha==VSWorldContracts::AlphaMode::Blend?"blend":"opaque";
            ++faces;
        }
    if(faces!=6) return false;
    auto& row=state.legacy;row["faces"]=S32(faces);row["normal_source"]=legacySources[0];row["specular_source"]=legacySources[1];
    for(const auto value:surface->uv[VSMaterial::Normal].matrix) row["normal_uv"].append(value);
    for(const auto value:surface->uv[VSMaterial::Normal].offset) row["normal_uv"].append(value);
    for(const auto value:surface->uv[VSMaterial::ORMOrSpecular].matrix) row["specular_uv"].append(value);
    for(const auto value:surface->uv[VSMaterial::ORMOrSpecular].offset) row["specular_uv"].append(value);
    for(const auto value:surface->specular) row["specular"].append(value);
    row["roughness"]=surface->roughness;row["cutoff"]=surface->cutoff;
    const auto root=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
    for(unsigned index=0;index<2;++index)
    {
        const auto image=index==0?normal:specular;
        const auto pixels=resources.readbackImage(image);const auto file="r3/legacy-"+std::to_string(index)+".rgba";
        std::ofstream output(root/file,std::ios::binary);output.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
        require(output.good(),"R3 legacy channel readback write failed");row["readbacks"].append(file);state.images.push_back(image);
    }
    LLMessageSystem* msg=gMessageSystem;msg->newMessage("KillObject");msg->nextBlock("ObjectData");msg->addU32("ID",992);
    require(msg->sendReliable(session.host())>0,"R3 legacy KillObject transport failed");state.legacyRetiring=true;return false;
}
bool remoteMaterials(WorldImages& state,VSNativeSession& session,VSWorldScene& world,LLSD& report)
{
    if(report.has("r3_remote_materials")) return true;
    for(const auto& material:state.remote) if(material->isFetching()) return false;
    require(state.remote[0]->isLoaded() && !state.remote[1]->isLoaded() && !state.remote[2]->isLoaded(),"R3 valid/invalid/oversized remote material admission differs from policy");
    if(!state.remote[3]->isLoaded())
    {
        state.remote[3]=gGLTFMaterialList.getMaterial(remoteAssets[3]);
        return false;
    }
    if(state.remoteRetiring)
    {
        if(world.object(remoteObject)) return false;
        LLSD result;result["actual_object_faces"]=6;result["object_retired"]=true;
        result["replacement_faces"]=6;result["asset_recovered"]=true;
        result["native_only"]=!state.remote[0]->mBaseColorTexture && !state.remote[0]->mNormalTexture && !state.remote[0]->mMetallicRoughnessTexture && !state.remote[0]->mEmissiveTexture;
        result["shared_parser_factors"]=true;
        for(unsigned index=0;index<remoteAssets.size();++index)
        {LLSD row;row["id"]=remoteAssets[index];row["loaded"]=state.remote[index]->isLoaded();row["file"]="r3/remote-materials/"+remoteAssets[index].asString()+".asset";result["assets"].append(row);}
        report["r3_remote_materials"]=result;return true;
    }
    unsigned faces=0;
    for(const auto& packet:world.packets(session.position()+LLVector3(0,-4,1),128,480,640))
        if(std::equal(packet.objectIdentity.begin(),packet.objectIdentity.end(),remoteObject.mData))
        {
            const auto& surface=packet.surface;
            require(surface && surface->pbr && surface->doubleSided && surface->alpha==VSWorldContracts::AlphaMode::Mask,"R3 actual remote PBR face flags differ");
            if(std::abs(surface->base[0]-(state.remoteReplacing?.5f:.25f))>=1.e-6f) return false;
            require(std::abs(surface->base[3]-.625f)<1.e-6f &&
                std::abs(surface->metallic-.3f)<1.e-6f && std::abs(surface->roughness-.6f)<1.e-6f && std::abs(surface->cutoff-.25f)<1.e-6f,
                "R3 actual remote PBR factors differ from the shared parser");
            if(!packet.image || !packet.channels[0] || !packet.channels[1] || !packet.channels[2]) return false;
            ++faces;
        }
    if(faces!=6) return false;
    if(!state.remoteReplacing)
    {
        bumpObjectPacket(session,false,true,true);
        state.remoteReplacing=true;return false;
    }
    LLMessageSystem* msg=gMessageSystem;msg->newMessage("KillObject");msg->nextBlock("ObjectData");msg->addU32("ID",991);
    require(msg->sendReliable(session.host())>0,"R3 PBR KillObject transport failed");state.remoteRetiring=true;return false;
}
LLPointer<LLImageRaw> bumpPixels(unsigned phase)
{
    auto raw=pattern(16,16,0);
    if(phase) for(unsigned pixel=0;pixel<256;++pixel) {raw->getData()[pixel*4]=255-raw->getData()[pixel*4];raw->getData()[pixel*4+1]=255-raw->getData()[pixel*4+1];}
    return raw;
}
LLSD bumpReadback(WorldImages& state,VSUIResources& resources,const VSUIRenderer::Image& image,unsigned code,const std::string& stage)
{
    const auto sourceID=code<=2?bumpSource:resources.standardBumpSource(code);
    const auto source=resources.image("remote:"+sourceID.asString());require(bool(source),"R3 bump source was lost");
    const auto extent=VSUIRenderer::imageExtent(source);const auto width=extent[0],height=extent[1];
    const auto& pixels=VSUIRenderer::imagePixels(source);
    const auto actual=resources.readbackImage(image);
    std::vector<U8> expectedPixels(actual.size());
    require(VSUIRenderer::imageExtent(image)==extent && pixels.size()==std::size_t(width)*height*4 && actual.size()==pixels.size(),"R3 bump publication/readback dimensions differ");
    const auto components=vs_ui_image_components(sourceID);const unsigned last=components==1?0:components==3?2:3;
    auto h=[&](unsigned x,unsigned y) {
        const auto offset=(std::size_t(y)*width+x)*4;
        if(code>=3) return double(pixels[offset+last]);
        const auto value=(.2995*pixels[offset]+.5875*pixels[offset+1]+.1145*pixels[offset+2])/255.;
        return code==2?1.-value:value;
    };
    const double scale=gSavedSettings.getF32("RenderNormalMapScale"),factor=code>=3?1.:512.;
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
    {
        const double nx=2*scale*(h((x+width-1)%width,y)-h((x+1)%width,y))*factor;
        const double ny=2*scale*(h(x,(y+1)%height)-h(x,(y+height-1)%height))*factor,nz=4*scale*scale;
        const double length=std::sqrt(nx*nx+ny*ny+nz*nz);const std::array<double,3> normal{nx/length,ny/length,nz/length};
        for(unsigned channel=0;channel<4;++channel)
        {
            double value=channel==3?(code>=3?h(x,y)/255.:h(x,y)):normal[channel]*.5+.5;
            value=std::clamp(value,0.,1.)*255.;
            const int expected=code>=3 && channel<3?int(std::floor(value)):int(std::lround(value));
            expectedPixels[(std::size_t(y)*width+x)*4+channel]=U8(expected);
            require(std::abs(int(actual[(std::size_t(y)*width+x)*4+channel])-expected)<=1,"R3 actual face bump differs from reference pixels");
        }
    }
    const auto root=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
    const auto folder=std::filesystem::path("r3")/"world-bumps";std::filesystem::create_directories(root/folder);
    const auto name=stage+"-"+std::to_string(code);
    const auto write=[&](const std::string& suffix,const auto& bytes) {
        const auto relative=folder/(name+suffix);std::ofstream output(root/relative,std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());require(output.good(),"R3 bump evidence write failed");return relative.generic_string();
    };
    LLSD row;row["code"]=S32(code);row["width"]=S32(width);row["height"]=S32(height);row["components"]=components;
    row["scale"]=scale;row["generation"]=ll_sd_from_U64(VSUIRenderer::generation(image));
    row["source_generation"]=ll_sd_from_U64(VSUIRenderer::generation(source));
    row["gpu_file"]=write(".rgba",actual);row["source_file"]=write("-source.rgba",pixels);
    row["expected_file"]=write("-expected.rgba",expectedPixels);row["source_id"]=sourceID;
    state.images.push_back(image);return row;
}
}
bool vs_r3_world_images(LLSD& report)
{
    if(!std::getenv("VS_VULKAN_REPLAY_LOCAL_IMAGES") || report.has("r3_world_bumps")) return true;
    const auto session=VSNativeSession::active();require(session && session->phase()==VSNativeSession::Phase::Connected,"R3 world image owner is not connected");
    auto& resources=gViewerWindow->nativeContext()->resources();auto& world=gViewerWindow->nativeContext()->world();
    if(!worldImages)
    {
        worldImages=std::make_unique<WorldImages>();auto& state=*worldImages;
        state.deadline=LLFrameTimer::getTotalSeconds()+30;state.originalScale=gSavedSettings.getF32("RenderNormalMapScale");
        gSavedSettings.setF32("RenderNormalMapScale",128);
        require(vs_publish_local_image(bumpSource,*bumpPixels(0)),"R3 world bump source publication failed");state.source=true;
        startRemoteMaterials(state,*session);
        startLegacyMaterial(state,*session);
        bumpObjectPacket(*session);return false;
    }
    auto& state=*worldImages;require(LLFrameTimer::getTotalSeconds()<state.deadline,"R3 actual world bump publication timed out");
    if(!remoteMaterials(state,*session,world,report)) return false;
    if(!legacyMaterialFaces(state,*session,world,resources,report)) return false;
    if(state.step==3)
    {
        require(world.object(bumpObject).has_value(),"R3 inactive bump object disappeared before its retirement test");
        const auto source=resources.image("remote:"+bumpSource.asString());
        require(bool(source),"R3 inactive source was lost from its cache");
        if(source.use_count()!=2) return false; // Cache + this observer; queued work must retire first.
        state.evidence["inactive_faces_release_images"]=true;
        LLMessageSystem* msg=gMessageSystem;msg->newMessage("KillObject");msg->nextBlock("ObjectData");msg->addU32("ID",990);
        require(msg->sendReliable(session->host())>0,"R3 bump KillObject transport failed");
        state.step=4;return false;
    }
    if(state.step==4)
    {
        if(world.object(bumpObject)) return false;
        gSavedSettings.setF32("RenderNormalMapScale",state.originalScale);
        vs_retire_local_image(bumpSource);state.source=false;
        state.evidence["udp_object_update"]=true;state.evidence["stable_cached_generations"]=true;state.evidence["object_retired"]=true;
        report["r3_world_bumps"]=state.evidence;return true;
    }
    const auto packets=world.packets(session->position()+LLVector3(0,-4,1),128,480,640);
    std::map<unsigned,VSUIRenderer::Image> images;unsigned faces=0;
    for(const auto& packet:packets) if(std::equal(packet.objectIdentity.begin(),packet.objectIdentity.end(),bumpObject.mData))
    {
        if(!packet.surface || !packet.channels[0]) return false;
        const auto code=packet.surface->bump;const auto image=packet.channels[0];
        if(const auto found=images.find(code);found!=images.end()) require(found->second==image,"R3 repeated face bypassed generation cache");
        images[code]=image;++faces;
    }
    if(faces!=6 || images.size()!=3 || vs_ui_image_discard(resources.standardBumpSource(3))!=0) return false;
    const char* stage=state.step==0?"initial":state.step==1?"source_replaced":"scale_changed";
    for(const auto& [code,image]:images) state.evidence[stage].append(bumpReadback(state,resources,image,code,stage));
    if(state.step==0) require(vs_publish_local_image(bumpSource,*bumpPixels(1)),"R3 bump source replacement failed");
    else if(state.step==1) gSavedSettings.setF32("RenderNormalMapScale",64);
    else bumpObjectPacket(*session,true);
    ++state.step;return false;
}
void vs_r3_replay_retired(LLSD& report)
{
    if(!worldImages) return;
    for(const auto& image:worldImages->images) require(image.expired(),"R3 derived world image survived account retirement");
    require(gViewerWindow->nativeContext()->resources().evidence()["cached_bump_generations"].asInteger()==0,"R3 world bump cache survived account reset");
    report["r3_world_bumps"]["account_retired"]=true;
    for(const unsigned index:{0u,3u})
    {
        const auto replacement=gGLTFMaterialList.getMaterial(remoteAssets[index]);
        require(replacement && replacement!=worldImages->remote[index].get() && !replacement->isLoaded() && !replacement->isFetching(),"R3 remote material registry survived account retirement or fetched outside connection ownership");
    }
    report["r3_remote_materials"]["account_retired"]=true;
    report["r3_legacy_material"]["account_retired"]=true;
}
void vs_r3_replay_cleanup()
{
    if(worldImages) {
        gSavedSettings.setF32("RenderNormalMapScale",worldImages->originalScale);
        if(worldImages->source) vs_retire_local_image(bumpSource);
        if(worldImages->legacySourcesOwned) for(const auto& id:legacySources) vs_retire_local_image(id);
        worldImages.reset();
    }
}

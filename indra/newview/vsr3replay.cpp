// Exercise the actual local-file decoder/watcher and native publication. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3replay.h"
#include "lllocalbitmaps.h"
#include "lllocalgltfmaterials.h"
#include "llimagepng.h"
#include "llui.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "llsdutil.h"
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

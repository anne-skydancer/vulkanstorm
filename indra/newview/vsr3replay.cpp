// Exercise the actual local-file decoder/watcher and native publication. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3replay.h"
#include "lllocalbitmaps.h"
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
    evidence["r3_local_images"]=result;
}

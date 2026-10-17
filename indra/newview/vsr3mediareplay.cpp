// Actual media-plugin publication through the viewer UI owner. Diagnostic only. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3mediareplay.h"
#include "llmediactrl.h"
#include "llpluginclassmedia.h"
#include "llviewermedia.h"
#include "lluictrlfactory.h"
#include "llviewerwindow.h"
#include "llrootview.h"
#include "llframetimer.h"
#include "llsd.h"
#include "llsdutil.h"
#include "vsuiresources.h"
#include "vsvulkancontext.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace
{
void require(bool value,const char* text)
{if(!value){LL_WARNS("NativeR3Media")<<text<<LL_ENDL;throw std::runtime_error(text);}}
struct Media
{
    LLHandle<LLView> control;
    viewer_media_t consumer;
    U64 previousGeneration=0;
    unsigned stage=0;
    double deadline=0;
    VSUIRenderer::Image original;
    std::vector<std::uint8_t> originalPixels;
    std::vector<std::weak_ptr<const VSUIRenderer::Texture>> images;
};
std::unique_ptr<Media> media;
const LLUUID surface("b3700000-0000-0000-0000-000000000001");
const std::array<std::array<std::array<U8,4>,4>,2> colors{{
    {{{17,34,51,255},{68,85,102,255},{119,136,153,255},{170,187,204,255}}},
    {{{221,34,17,255},{51,204,85,255},{136,17,187,255},{68,153,238,255}}}}};
std::filesystem::path root()
{return std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));}
void phase(unsigned value)
{
    std::filesystem::create_directories(root()/"r3");
    std::ofstream output(root()/"r3/media-phase.txt");output<<value;
    require(output.good(),"Cannot write media phase fixture");
}
void close(Media& state)
{
    if(auto* control=dynamic_cast<LLMediaCtrl*>(state.control.get()))
    {
        if(auto* parent=control->getParent()) parent->removeChild(control);
        delete control;
    }
}
}
bool vs_r3_media_replay(LLSD& report)
{
    if(!std::getenv("VS_VULKAN_REPLAY_LOCAL_IMAGES") || (media && media->stage==3)) return true;
    require(VSVulkanContext::headlessReplay(),"Media qualification requires the explicit headless viewer path");
    if(!media)
    {
        media=std::make_unique<Media>();auto& state=*media;state.deadline=LLFrameTimer::getTotalSeconds()+25;
        LLMediaCtrl::Params p;p.name="native_r3_media";p.rect=LLRect(0,64,64,0);
        p.border_visible=false;p.hide_loading=true;p.decouple_texture_size=true;p.focus_on_click=false;
        p.texture_width=64;p.texture_height=64;
        auto* control=LLUICtrlFactory::create<LLMediaCtrl>(p);
        require(control!=nullptr,"Cannot construct the real media control");
        state.control=control->getHandle();gViewerWindow->getRootView()->addChild(control);
        control->setAlwaysRefresh(true);control->setTakeFocusOnClick(false);control->setVisible(true);
        phase(1);
        const std::string seed=std::getenv("VS_VULKAN_REPLAY_SEED");
        require(seed.size()>5 && seed.substr(seed.size()-5)=="/seed","Unexpected media fixture seed");
        control->navigateTo(seed.substr(0,seed.size()-4)+"r3-media","text/html");
        return false;
    }
    auto& state=*media;require(LLFrameTimer::getTotalSeconds()<state.deadline,"Actual CEF media publication timed out");
    auto* control=dynamic_cast<LLMediaCtrl*>(state.control.get());require(control!=nullptr,"Media control retired before qualification");
    if(state.consumer) state.consumer->setNativeSurface(surface,4096,1);
    auto* plugin=control->getMediaPlugin();if(!plugin || !plugin->textureValid()) return false;
    auto& resources=gViewerWindow->nativeContext()->resources();
    const auto key="media:"+control->getTextureID().asString();const auto image=resources.image(key);
    const unsigned width=state.stage==2?78:64,height=state.stage==2?46:64;
    if(!image || VSUIRenderer::imageExtent(image)!=std::array<unsigned,2>{width,height}) return false;
    const auto& pixels=VSUIRenderer::imagePixels(image);
    require(pixels.size()==std::size_t(width)*height*4,"Media CPU publication size differs");
    const unsigned palette=state.stage?1:0;
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
    {
        const auto& color=colors[palette][(y>=height/2?2:0)+(x>=width/2?1:0)];
        for(unsigned channel=0;channel<4;++channel)
            if(pixels[(std::size_t(y)*width+x)*4+channel]!=color[channel]) return false;
    }
    const U64 generation=VSUIRenderer::generation(image);
    if(generation<=state.previousGeneration) return false;
    const auto gpu=resources.readbackImage(image);
    require(gpu==pixels,"Actual media GPU pixels differ from the canonical plugin publication");
    auto* shared=LLViewerMedia::getInstance()->getMediaImplFromTextureID(control->getTextureID());
    require(shared && shared->getMediaTextureID()==control->getTextureID(),"UI media bypassed the shared media manager");
    if(!state.consumer) {state.consumer=shared;}
    state.consumer->setNativeSurface(surface,4096,1);
    state.previousGeneration=generation;
    const char* label=state.stage==0?"initial":state.stage==1?"updated":"resized";
    const auto file=std::string("r3/media-")+label+".rgba";
    {std::ofstream output(root()/file,std::ios::binary);output.write(reinterpret_cast<const char*>(gpu.data()),gpu.size());require(output.good(),"Cannot archive media GPU readback");}
    LLSD row;row["width"]=S32(width);row["height"]=S32(height);row["bits_width"]=plugin->getBitsWidth();
    row["bits_height"]=plugin->getBitsHeight();row["origin_opengl"]=plugin->getTextureCoordsOpenGL();
    row["format"]=S32(plugin->getTextureFormatPrimary());row["generation"]=ll_sd_from_U64(VSUIRenderer::generation(image));
    row["file"]=file;report["r3_media"][label]=row;
    state.images.push_back(image);
    if(state.stage==0)
    {state.original=image;state.originalPixels=pixels;phase(2);}
    else
    {
        require(VSUIRenderer::imagePixels(state.original)==state.originalPixels,"Media publication mutated an old queued generation");
        if(state.stage==1) {control->reshape(78,46);control->setTextureSize(78,46);}
        else
        {
            close(state);
            require(resources.image(key)==image,"Closing one UI consumer retired a shared media publication");
            report["r3_media"]["shared_consumer_retained"]=true;
            state.consumer->removeNativeSurface(surface);state.consumer=nullptr;state.original.reset();
            require(!resources.image(key),"Last media-source owner retained the native publication");
            report["r3_media"]["actual_plugin"]=true;report["r3_media"]["shared_manager"]=true;
            report["r3_media"]["immutable_generations"]=true;report["r3_media"]["retired"]=true;
        }
    }
    ++state.stage;state.deadline=LLFrameTimer::getTotalSeconds()+25;
    return state.stage==3;
}
void vs_r3_media_retired(LLSD& report)
{
    if(!media) return;
    for(const auto& image:media->images) require(image.expired(),"Media GPU generation survived account retirement");
    report["r3_media"]["account_retired"]=true;
}
void vs_r3_media_cleanup()
{
    if(media) {close(*media);if(media->consumer) media->consumer->removeNativeSurface(surface);media->consumer=nullptr;}
    media.reset();
}

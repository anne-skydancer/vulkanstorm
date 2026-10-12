// Capture normal startup frames independently on each peer backend. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsparitybaseline.h"
#include "llappviewer.h"
#include "llstartup.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llgl.h"
#include "llrender.h"
#include "llsdserialize.h"
#include "vsvulkancontext.h"
#include "llframetimer.h"
#include "llfocusmgr.h"
#pragma push_macro("Bool")
#pragma push_macro("False")
#pragma push_macro("True")
#pragma push_macro("MAP_TYPE")
#undef Bool
#undef False
#undef True
#undef MAP_TYPE
#include <DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <DiligentCore/Graphics/GraphicsEngine/interface/Texture.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("True")
#pragma pop_macro("False")
#pragma pop_macro("Bool")
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace
{
using Clock=std::chrono::steady_clock;
const char* directory() { return std::getenv("VS_PARITY_BASELINE_DIRECTORY"); }
unsigned frame=0;
Clock::time_point begin,render;
LLSD samples=LLSD::emptyArray();
// Freeze only UI drawing. Network/initialization deadlines keep the live clock.
// This does not claim fixed-clock world simulation or a global RNG override.
struct UIClock : LLFrameTimer
{
    static double value() { return sFrameTime; }
    static void set(double value) { sFrameTime=value; }
};
double savedClock=0;
bool frozen=false;
void restoreClock() { if (frozen) { UIClock::set(savedClock);frozen=false; } }
double milliseconds(Clock::time_point a,Clock::time_point b)
{ return std::chrono::duration<double,std::milli>(b-a).count(); }
void save(const std::vector<U8>& rgb,unsigned width,unsigned height,const std::string& backend,Clock::time_point drawn)
{
    const auto out=std::filesystem::path(directory());
    std::filesystem::create_directories(out);
    const auto index=samples.size();
    std::ofstream image(out/llformat("frame-%02d.ppm",index),std::ios::binary);
    image<<"P6\n"<<width<<' '<<height<<"\n255\n";
    image.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
    if (!image.good()) throw std::runtime_error("Cannot write production parity capture");
    LLSD row;
    row["frame"]=S32(frame);row["width"]=S32(width);row["height"]=S32(height);
    row["backend"]=backend;row["startup_state"]=S32(LLStartUp::getStartupState());
    row["pre_render_cpu_ms"]=milliseconds(begin,render);
    row["render_submit_cpu_ms"]=milliseconds(render,drawn);
    row["capture_wait_and_io_ms"]=milliseconds(drawn,Clock::now());
    row["image"]=llformat("frame-%02d.ppm",index);
    samples.append(row);
    LLSD report;
    report["scene"]="normal-startup-login-v1";
    report["backend"]=backend;report["frames"]=samples;
    report["world_replay_qualified"]=false;
    report["fixed_clock_qualified"]=false;
    report["fixed_ui_draw_clock_seconds"]=1000.;
    report["gpu_timing_available"]=false;
    if (backend=="OpenGL")
    {
        report["device"]["renderer"]=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        report["device"]["vendor"]=reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        report["device"]["version"]=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    }
    else report["device"]=gViewerWindow->nativeContext()->rendererInfo();
    for (const char* name:{"SkinCurrent","SkinCurrentTheme","Language","UIScaleFactor","RenderBackend","RenderVSyncEnable"})
        report["settings"][name]=gSavedSettings.getLLSD(name);
#if !LL_WINDOWS
    std::ifstream maps("/proc/self/maps");
    std::ofstream libraryPaths(out/"loaded-library-paths.txt");
    libraryPaths<<maps.rdbuf();
    if (!libraryPaths.good()) throw std::runtime_error("Cannot record parity libraries");
#endif
    std::ofstream meta(out/"viewer.xml");
    LLSDSerialize::toPrettyXML(report,meta);
    if (!meta.good()) throw std::runtime_error("Cannot write production parity timings");
    if (samples.size()==5) LLAppViewer::instance()->forceQuit();
}
bool capture() { return directory() && frame>=180 && samples.size()<5; }
}
void vs_parity_baseline_frame_begin()
{ if (directory()) { restoreClock();begin=Clock::now(); } }
void vs_parity_baseline_before_render()
{
    if (!directory()) return;
    render=Clock::now();
    if (LLStartUp::getStartupState()==STATE_LOGIN_WAIT) ++frame;
    if (frame>=180 && !frozen)
    {
        savedClock=UIClock::value();UIClock::set(1000.);frozen=true;
        gFocusMgr.setKeyboardFocus(nullptr); // Fixture camera/input state: no caret.
    }
}
void vs_parity_baseline_capture_gl()
{
    if (!capture()) { restoreClock();return; }
    const auto drawn=Clock::now();
    const auto rect=gViewerWindow->getWindowRectRaw();
    const unsigned width=rect.getWidth(),height=rect.getHeight();
    if (!width || !height || width>8192 || height>8192)
        throw std::runtime_error("Invalid parity capture extent");
    std::vector<U8> bottom(std::size_t(width)*height*4),rgb(std::size_t(width)*height*3);
    gGL.flush();
    GLint alignment;glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,bottom.data());
    glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL parity readback failed");
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x)
        for (unsigned c=0;c<3;++c) rgb[(std::size_t(y)*width+x)*3+c]=bottom[(std::size_t(height-1-y)*width+x)*4+c];
    save(rgb,width,height,"OpenGL",drawn);
    restoreClock();
}
void vs_parity_baseline_capture_vk(Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
                                  Diligent::ITextureView* target,unsigned width,unsigned height)
{
    if (!capture()) { restoreClock();return; }
    const auto drawn=Clock::now();
    using namespace Diligent;
    auto desc=target->GetTexture()->GetDesc();
    if (desc.Format!=TEX_FORMAT_BGRA8_UNORM && desc.Format!=TEX_FORMAT_RGBA8_UNORM)
        throw std::runtime_error("Unsupported parity capture format");
    const bool bgra=desc.Format==TEX_FORMAT_BGRA8_UNORM;
    desc.Usage=USAGE_STAGING;desc.BindFlags=BIND_NONE;desc.CPUAccessFlags=CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> readback;device->CreateTexture(desc,nullptr,&readback);
    if (!readback) throw std::runtime_error("Cannot create parity readback");
    context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    CopyTextureAttribs copy{};copy.pSrcTexture=target->GetTexture();copy.pDstTexture=readback;
    copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);context->WaitForIdle();
    MappedTextureSubresource mapped{};
    context->MapTextureSubresource(readback,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
    if (!mapped.pData) throw std::runtime_error("Cannot map parity readback");
    std::vector<U8> rgb(std::size_t(width)*height*3);
    const auto* pixels=static_cast<const U8*>(mapped.pData);
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x)
        for (unsigned c=0;c<3;++c) rgb[(std::size_t(y)*width+x)*3+c]=pixels[y*mapped.Stride+x*4+(bgra?2-c:c)];
    context->UnmapTextureSubresource(readback,0,0);
    save(rgb,width,height,"Vulkan",drawn);
    restoreClock();
}

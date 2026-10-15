// Capture normal startup frames independently on each peer backend. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsparitybaseline.h"
#include "vsparityscene.h"
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
#include "llvertexbuffer.h"
#include "llrootview.h"
#include "llmediactrl.h"
#include "llpluginclassmedia.h"
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
#if LL_WINDOWS
#include <tlhelp32.h>
#endif
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
LLSD currentReport;
bool capturedThisFrame=false;
LLSD stageMetrics=LLSD::emptyMap();
LLSD initializationMetrics=LLSD::emptyMap();
std::array<U64,3> glCounters{};
void observe(LLVertexBuffer::DiagnosticEvent event,U64 value)
{ glCounters[static_cast<unsigned>(event)]+=value; }
// Freeze only UI drawing. Network/initialization deadlines keep the live clock.
// This does not claim fixed-clock world simulation or a global RNG override.
struct UIClock : LLFrameTimer
{
    struct Snapshot { F64 elapsed,totalSeconds;U64 totalTime,delta;S32 count; };
    static double value() { return sFrameTime; }
    static double epoch() { return sTotalSeconds; }
    static void set(double value) { sFrameTime=value; }
    static Snapshot freezeWorld()
    {
        Snapshot prior{sFrameTime,sTotalSeconds,sTotalTime,sFrameDeltaTime,sFrameCount};
        sFrameTime=1000;sTotalSeconds=1700000000;sTotalTime=1700000000000000ULL;
        sFrameDeltaTime=0;sFrameCount=60;return prior;
    }
    static void restoreWorld(const Snapshot& prior)
    {
        sFrameTime=prior.elapsed;sTotalSeconds=prior.totalSeconds;sTotalTime=prior.totalTime;
        sFrameDeltaTime=prior.delta;sFrameCount=prior.count;
    }
};
UIClock::Snapshot savedWorldClock{};
U32 savedViewerCount=0;
U64MicrosecondsImplicit savedViewerStart,savedViewerTimestamp;
F32SecondsImplicit savedViewerTime,savedViewerInterval;
double savedClock=0;
bool frozen=false,worldClock=false;
bool marked=false;
bool                    loginPageReady()
{
    const auto* expected = std::getenv("VS_PARITY_BASELINE_LOGIN_URL");
    if (!expected || gSavedSettings.getString("ForceLoginURL") != expected || !gViewerWindow || !gViewerWindow->getRootView())
        return false;
    auto* browser = gViewerWindow->getRootView()->findChild<LLMediaCtrl>("login_html");
    auto* plugin  = browser ? browser->getMediaPlugin() : nullptr;
    if (!plugin || !plugin->textureValid() || plugin->getDirty() || plugin->getMediaName() != "Vulkanstorm parity login fixture v1" ||
        plugin->getTextureDepth() != 4 || plugin->getWidth() < 200 || plugin->getHeight() < 200)
        return false;
    const auto format = plugin->getTextureFormatPrimary();
    if (format != GL_RGBA && format != GL_BGRA)
        return false;
    const auto* bits = plugin->getBitsData();
    if (!bits)
        return false;
    // Wait for the actual CEF paint, not merely navigation/title completion.
    // The page centre is a fixed background, away from both coloured witnesses.
    const auto* pixel = bits + (std::size_t(plugin->getHeight() / 2) * plugin->getBitsWidth() + plugin->getWidth() / 2) * 4;
    return pixel[format == GL_BGRA ? 2 : 0] == 25 && pixel[1] == 51 && pixel[format == GL_BGRA ? 0 : 2] == 77 && pixel[3] == 255;
}
void restoreClock()
{
    if (frozen)
    {
        if (worldClock)
        { UIClock::restoreWorld(savedWorldClock);gStartTime=savedViewerStart;gFrameTime=savedViewerTimestamp;gFrameCount=savedViewerCount;gFrameTimeSeconds=savedViewerTime;gFrameIntervalSeconds=savedViewerInterval; }
        else UIClock::set(savedClock);
        frozen=false;worldClock=false;
    }
}
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
    row["stages"]=stageMetrics;
    if (backend=="OpenGL")
    {
        row["counters"]["draw_calls"]=F64(glCounters[0]);
        row["counters"]["vertex_index_upload_bytes"]=F64(glCounters[1]);
        row["counters"]["vertex_buffer_bind_calls"]=F64(glCounters[2]);
    }
    row["image"]=llformat("frame-%02d.ppm",index);
    samples.append(row);
    capturedThisFrame=true;
    LLSD report;
    report["scene"]=vs_parity_scene_active()?"static-world-primitives-v1":"normal-startup-login-v1";
    report["backend"]=backend;report["frames"]=samples;
    report["login_page_fixture_ready"]        = !vs_parity_scene_active() && loginPageReady();
    report["initialization_metrics"]=initializationMetrics;
    report["world_replay_qualified"]=false; // Acceptance belongs to the independent runner.
    report["fixed_clock_qualified"]=false; // Runner must verify identical scene inputs and repeated output.
    report["fixed_world_clock_applied"]=vs_parity_scene_active() && gFrameTimeSeconds.value()==1000 && gFrameIntervalSeconds.value()==0 && gFrameCount==60 && UIClock::epoch()==1700000000.;
    report["render_clock"]["elapsed_seconds"]=F64(gFrameTimeSeconds.value());
    report["render_clock"]["motion_delta_seconds"]=F64(gFrameIntervalSeconds.value());
    report["render_clock"]["frame"]=S32(gFrameCount);
    report["render_clock"]["epoch_seconds"]=UIClock::epoch();
    if (vs_parity_scene_active())
    {
        const auto projection=vs_parity_scene_view(width,height).projection;
        for (unsigned i=0;i<16;++i) report["world_projection"].append(F64(backend=="OpenGL"?gGLProjection[i]:projection[i]));
    }
    report["static_world_fixture_active"]=vs_parity_scene_active();
    if (vs_parity_scene_active()) report["effective_scene_inputs"]=vs_parity_scene_inputs();
    report["fixed_ui_draw_clock_seconds"]=1000.;
    report["gpu_timing_available"]=false;
    if (backend=="OpenGL")
    {
        report["device"]["renderer"]=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        report["device"]["vendor"]=reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        report["device"]["version"]=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    }
    else report["device"]=gViewerWindow->nativeContext()->rendererInfo();
    for (const char* name : { "SkinCurrent", "SkinCurrentTheme", "Language", "UIScaleFactor", "RenderBackend", "RenderVSyncEnable",
                              "RenderDebugGLSession", "ForceLoginURL" })
        report["settings"][name]=gSavedSettings.getLLSD(name);
#if LL_WINDOWS
    std::ofstream libraryPaths(out/"loaded-library-paths.txt");
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    if (snapshot==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot enumerate parity libraries");
    MODULEENTRY32W module{};module.dwSize=sizeof(module);
    if (!Module32FirstW(snapshot,&module))
    { CloseHandle(snapshot);throw std::runtime_error("Cannot read parity libraries"); }
    do { libraryPaths<<std::filesystem::path(module.szExePath).string()<<"\n"; } while (Module32NextW(snapshot,&module));
    CloseHandle(snapshot);
    if (!libraryPaths.good()) throw std::runtime_error("Cannot record parity libraries");
#else
    std::ifstream maps("/proc/self/maps");
    std::ofstream libraryPaths(out/"loaded-library-paths.txt");
    libraryPaths<<maps.rdbuf();
    if (!libraryPaths.good()) throw std::runtime_error("Cannot record parity libraries");
#endif
    std::ofstream meta(out/"viewer.xml");
    LLSDSerialize::toPrettyXML(report,meta);
    if (!meta.good()) throw std::runtime_error("Cannot write production parity timings");
    currentReport=report;
    if (samples.size()==5) LLAppViewer::instance()->forceQuit();
}
bool capture()
{ return directory() && frame >= 180 && samples.size() < 5 && (vs_parity_scene_active() || loginPageReady()); }
}
void vs_parity_baseline_frame_begin()
{
    if (directory())
    {
        restoreClock();marked=false;capturedThisFrame=false;begin=Clock::now();glCounters={};stageMetrics=LLSD::emptyMap();
        LLVertexBuffer::setDiagnosticObserver(observe);
    }
}
void vs_parity_baseline_metrics(const LLSD& metrics)
{
    if (directory()) for (auto i=metrics.beginMap();i!=metrics.endMap();++i)
    {
        if (i->first.rfind("initial_",0)==0) initializationMetrics[i->first]=i->second;
        else stageMetrics[i->first]=i->second;
    }
}
VSParityBaselineTimer::VSParityBaselineTimer(const char* name):mName(directory()?name:nullptr)
{ if (mName) mStart=Clock::now(); }
VSParityBaselineTimer::~VSParityBaselineTimer()
{ if (mName) stageMetrics[mName]=stageMetrics[mName].asReal()+milliseconds(mStart,Clock::now()); }
void vs_parity_baseline_after_present(double duration)
{
    if (!directory() || !capturedThisFrame) return;
    samples[samples.size()-1]["presentation_cpu_ms"]=duration;
    samples[samples.size()-1]["observed_total_frame_cpu_ms"]=milliseconds(begin,Clock::now());
    currentReport["frames"]=samples;
    std::ofstream output(std::filesystem::path(directory())/"viewer.xml");
    LLSDSerialize::toPrettyXML(currentReport,output);
    if (!output.good()) throw std::runtime_error("Cannot preserve presentation timings");
}
void vs_parity_baseline_before_render()
{
    if (!directory()) return;
    if (!marked)
    {
        render=Clock::now();
        if (vs_parity_scene_active() || LLStartUp::getStartupState()==STATE_LOGIN_WAIT) ++frame;
        marked=true;
    }
    if ((vs_parity_scene_active() || frame>=180) && !frozen)
    {
        worldClock=vs_parity_scene_active();
        if (worldClock)
        {
            savedWorldClock=UIClock::freezeWorld();savedViewerCount=gFrameCount;
            savedViewerStart=gStartTime;savedViewerTimestamp=gFrameTime;
            gStartTime=1700000000000000ULL-1000000000ULL;gFrameTime=1700000000000000ULL;
            savedViewerTime=gFrameTimeSeconds;savedViewerInterval=gFrameIntervalSeconds;
            gFrameCount=60;gFrameTimeSeconds=1000;gFrameIntervalSeconds=0;
        }
        else { savedClock=UIClock::value();UIClock::set(1000.); }
        frozen=true;
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

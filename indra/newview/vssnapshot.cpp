// Same snapshot controls/encoding owner, native scene capture. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vssnapshot.h"
#include "vsnativeim.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "llviewercamera.h"
#include "llimage.h"
#include "llrootview.h"
#include "llviewerdisplay.h"
#include "pipeline.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llwebprofile.h"
#include "fsprimfeedauth.h"
#include "exoflickrauth.h"
#include "llflickrconnect.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

bool vs_native_snapshot(LLViewerWindow& window, LLImageRaw& raw, int width, int height, bool keepAspect,
                        bool showUI, bool showHUD, bool noPost, bool showBalance,
                        LLSnapshotModel::ESnapshotLayerType layer, int maximumSize)
{
    auto* owner = window.nativeContext();
    if (!owner || width <= 0 || height <= 0 || maximumSize <= 0) return false;
    auto* camera = LLViewerCamera::getInstance();
    const bool previousUI = gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI);
    const bool previousHUD = LLPipeline::sShowHUDAttachments;
    const bool previousNoPost = gSnapshotNoPost;
    const auto previousHeight = camera->getViewHeightInPixels();
    const float previousAspect = camera->getAspect(), previousView = camera->getView();
    struct Restore { std::function<void()> callback; ~Restore() { callback(); } } restore{[&]
    {
        if (gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI) != previousUI)
            LLPipeline::toggleRenderDebugFeature(LLPipeline::RENDER_DEBUG_FEATURE_UI);
        LLPipeline::sShowHUDAttachments = previousHUD;
        gSnapshotNoPost = previousNoPost;
        window.updateWorldViewRect();
        camera->setViewHeightInPixels(previousHeight);
        camera->setAspect(previousAspect);
        camera->setView(previousView);
        window.setBalanceVisible(true);
        window.setCursor(UI_CURSOR_ARROW);
    }};
    if (previousUI != showUI) LLPipeline::toggleRenderDebugFeature(LLPipeline::RENDER_DEBUG_FEATURE_UI);
    LLPipeline::sShowHUDAttachments = showHUD && previousHUD;
    gSnapshotNoPost = noPost;
    window.setCursor(UI_CURSOR_WAIT);
    window.updateWorldViewRect(!showUI);
    const auto viewport = showUI ? window.getWindowRectRaw() : window.getWorldViewRectRaw();
    const int windowWidth = viewport.getWidth(), windowHeight = viewport.getHeight();
    if (windowWidth <= 0 || windowHeight <= 0) return false;
    if (showUI)
    {
        width = std::min(width, windowWidth); height = std::min(height, windowHeight);
        window.setBalanceVisible(showBalance);
    }
    const float limit = std::min(1.f, float(maximumSize) / std::max(width, height));
    width = std::max(1, int(std::floor(width * limit)));
    height = std::max(1, int(std::floor(height * limit)));
    const bool screenCapture = showUI || (keepAspect && width <= windowWidth && height <= windowHeight);
    const int captureWidth = screenCapture ? windowWidth : width;
    const int captureHeight = screenCapture ? windowHeight : height;
    camera->setAspect(float(captureWidth) / captureHeight);
    camera->setViewHeightInPixels(captureHeight);
    if (!screenCapture && !keepAspect && width <= windowWidth && height <= windowHeight)
    {
        const float sourceAspect = float(windowWidth) / windowHeight;
        const float targetAspect = float(width) / height;
        if (targetAspect > sourceAspect)
            camera->setView(2.f * std::atan(std::tan(previousView * .5f) * sourceAspect / targetAspect));
    }
    VSVulkanContext::SnapshotLayer nativeLayer;
    switch (layer)
    {
    case LLSnapshotModel::SNAPSHOT_TYPE_COLOR: nativeLayer = VSVulkanContext::SnapshotLayer::Color; break;
    case LLSnapshotModel::SNAPSHOT_TYPE_DEPTH: nativeLayer = VSVulkanContext::SnapshotLayer::Depth; break;
    case LLSnapshotModel::SNAPSHOT_TYPE_DEPTH24: nativeLayer = VSVulkanContext::SnapshotLayer::Depth24; break;
    default: return false;
    }
    try
    {
        LLPointer<LLImageRaw> captured = new LLImageRaw;
        if (!owner->snapshot(*captured, captureWidth, captureHeight, window.getDisplayScale().mV[VX],
                             showUI, showHUD && previousHUD, nativeLayer, [&window] { window.getRootView()->draw(); })) return false;
        // The reference keeps UI at screen resolution, then crops before its
        // existing LLImageRaw scale/codec flow. Never stretch UI while rendering.
        if (screenCapture && !keepAspect)
        {
            const float ratio = std::min(float(captureWidth) / width, float(captureHeight) / height);
            const int cropWidth = std::max(1, int(std::floor(ratio * width)));
            const int cropHeight = std::max(1, int(std::floor(ratio * height)));
            const int x = (captureWidth - cropWidth) / 2, y = (captureHeight - cropHeight) / 2;
            if (!raw.resize(cropWidth, cropHeight, 3)) return false;
            LLImageDataSharedLock sourceLock(captured);
            LLImageDataLock destinationLock(&raw);
            for (int row = 0; row < cropHeight; ++row)
                std::memcpy(raw.getData() + std::size_t(row) * cropWidth * 3,
                            captured->getData() + (std::size_t(y + row) * captureWidth + x) * 3, std::size_t(cropWidth) * 3);
        }
        else raw.copy(captured);
        // Retain the original BMP alignment rule and common image scaler.
        width += (width * 3) % 4;
        if (raw.getWidth() != width || raw.getHeight() != height)
            return raw.scale(width, height, std::abs(width - raw.getWidth()) > 4 || std::abs(height - raw.getHeight()) > 4);
        return !raw.isBufferInvalid();
    }
    catch (const std::exception& error)
    {
        LL_WARNS("Snapshot") << "Native snapshot failed: " << error.what() << LL_ENDL;
        return false;
    }
}

void vs_retire_snapshot_floaters()
{
    // Owners close their BigPreview first. Retire the shared preview last so
    // ordinary onClose processing can still restore the tool and pause state.
    for (const char* name : {"360capture", "flickr", "primfeed", "snapshot", "simple_snapshot",
                             "snapshot_guide_settings", "big_preview"})
    {
        if (auto* floater = LLFloaterReg::findInstance(name))
        {
            const auto handle = floater->getHandle();
            floater->closeFloater();
            if (!handle.isDead()) LLFloaterReg::destroyInstance(name);
        }
    }
    exoFlickrAuth::retireAccount();
    if (LLFlickrConnect::instanceExists()) LLFlickrConnect::instance().retireAccount();
    FSPrimfeedAuth::retireAccount();
    LLWebProfile::setAuthCookie(std::string());
    LLWebProfile::setImageUploadResultCallback({});
}


bool vs_native_simple_snapshot(LLViewerWindow& window,LLImageRaw& raw,int width,int height,int passes)
{
    auto* owner=window.nativeContext();if(!owner || width<=0 || height<=0)return false;
    const auto current=vs_native_im_guard();
    const bool previousUI=gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI);
    const bool previousHUD=LLPipeline::sShowHUDAttachments;
    struct Restore {std::function<void()> callback;~Restore(){callback();}} restore{[&]
    {
        if(gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI)!=previousUI)LLPipeline::toggleRenderDebugFeature(LLPipeline::RENDER_DEBUG_FEATURE_UI);
        LLPipeline::sShowHUDAttachments=previousHUD;window.setCursor(UI_CURSOR_ARROW);
    }};
    if(previousUI)LLPipeline::toggleRenderDebugFeature(LLPipeline::RENDER_DEBUG_FEATURE_UI);
    LLPipeline::sShowHUDAttachments=false;window.setCursor(UI_CURSOR_WAIT);
    try
    {
        bool complete=false;
        for(int pass=0;pass<std::max(1,passes);++pass)
        {
            if(!current())return false;
            complete=owner->snapshot(raw,width,height,window.getDisplayScale().mV[VX],false,false,VSVulkanContext::SnapshotLayer::Color,[]{});
        }
        return complete && current() && !raw.isBufferInvalid();
    }
    catch(const std::exception& error){LL_WARNS("360Capture")<<error.what()<<LL_ENDL;return false;}
}

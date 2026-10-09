// Native graphics and required startup views in LLViewerWindow. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "lldir.h"
#include "vsuiresources.h"
#include "llrootview.h"
#include "llpopupview.h"
#include "llpanel.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llfloatertos.h"
#include "llprogressview.h"
#include "llviewercontrol.h"
#include "llfocusmgr.h"
#include "llfontgl.h"
#include "lllayoutstack.h"
#include "llnotifications.h"
#include "llnotificationhandler.h"
#include "lluictrlfactory.h"
#include "vsvulkancontext.h"
#include "vsuicontext.h"
#include "vsstartupui.h"
#include "vsuiadmission.h"
#include <stdexcept>
namespace
{
void require(bool v, const char* m)
{
    if (!v)
        throw std::runtime_error(m);
}
} // namespace
void LLViewerWindow::initNativeWindow(const Params& p)
{
    try
    {
        mWindow = LLWindowManager::createWindow(this, p.title, p.name, p.x, p.y, p.width, p.height, 0, false, false,
                                                gSavedSettings.getBOOL("RenderVSyncEnable"), false, false, 0, 0, 4.6f,
                                                gSavedSettings.getBOOL("FSUseLegacyCursors"), LLWindowManager::GraphicsAPI::Vulkan);
        require(mWindow != nullptr, "Native viewer window creation failed");
        mWindow->setMinSize(p.min_width, p.min_height, false);
        LLCoordWindow size;
        require(mWindow->getSize(&size), "Native viewer extent is unavailable");
        const F32 dpi = llclamp(gSavedSettings.getF32("UIScaleFactor") * mWindow->getSystemUISize(), 0.75f, 4.f);
        mDisplayScale.setVec(dpi, dpi);
        mWindowRectRaw       = LLRect(0, size.mY, size.mX, 0);
        mWindowRectScaled    = LLRect(0, ll_round(size.mY / dpi), ll_round(size.mX / dpi), 0);
        mWorldViewRectRaw    = mWindowRectRaw;
        mWorldViewRectScaled = mWindowRectScaled;
        mVulkanContext       = std::make_unique<VSVulkanContext>(*mWindow, gSavedSettings.getBOOL("RenderDebugGLSession"));
        mNativeAdmission     = vs_startup_ui_admission();
        LLUI::settings_map_t settings{ { "config", &gSavedSettings },
                                       { "floater", &gSavedSettings },
                                       { "ignores", &gWarningSettings },
                                       { "account", &gSavedPerAccountSettings } };
        mNativeUI = std::make_unique<VSUIContext>(mVulkanContext->resources(), mWindow, settings, unsigned(size.mX), unsigned(size.mY), dpi,
                                                  [](const LLRect& rect) -> LLView*
                                                  {
                                                      LLView::Params params;
                                                      params.name          = "root";
                                                      params.rect          = rect;
                                                      params.mouse_opaque  = false;
                                                      params.follows.flags = FOLLOWS_NONE;
                                                      return LLUICtrlFactory::create<LLRootView>(params);
                                                  });
        mRootView = dynamic_cast<LLRootView*>(mNativeUI->root());
        require(mRootView != nullptr, "Native viewer root creation failed");
    }
    catch (...)
    {
        mNativeUI.reset();
        mRootView = nullptr;
        mNativeAdmission.reset();
        mVulkanContext.reset();
        if (mWindow)
        {
            LLWindowManager::destroyWindow(mWindow);
            mWindow = nullptr;
        }
        throw;
    }
}
void LLViewerWindow::initNativeBase()
{
    require(mRootView && !gFloaterView, "Native startup UI requires exclusive ownership");
    mFloaterSnapRegion = mRootView; // Startup dialogs follow the real native root before world chrome exists.
    LLPanel::Params holder;
    holder.name          = "login_panel_holder";
    holder.rect          = mWindowRectScaled;
    holder.follows.flags = FOLLOWS_ALL;
    auto* login          = LLUICtrlFactory::create<LLPanel>(holder, mRootView);
    require(login != nullptr, "Native login holder creation failed");
    mLoginPanelHolder = login->getHandle();
    LLFloaterView::Params floaters;
    floaters.name          = "Floater View";
    floaters.rect          = mWindowRectScaled;
    floaters.follows.flags = FOLLOWS_ALL;
    gFloaterView           = LLUICtrlFactory::create<LLFloaterView>(floaters, mRootView);
    require(gFloaterView != nullptr, "Native required-dialog container creation failed");
    // Initialize default channels before registering viewer-owned children.
    LLNotifications::instance();
    mSystemChannel.reset(new LLNotificationChannel("System", "Visible", LLNotificationFilters::includeEverything));
    mAlertsChannel.reset(new LLNotificationsUI::LLAlertHandler("Alerts", "alert", false));
    mModalAlertsChannel.reset(new LLNotificationsUI::LLAlertHandler("AlertModal", "alertmodal", true));
    LLFloaterReg::add("message_critical", "floater_critical.xml", &LLFloaterReg::build<LLFloaterTOS>);
    LLFloaterReg::add("message_tos", "floater_tos.xml", &LLFloaterReg::build<LLFloaterTOS>);
    auto progress = std::make_unique<LLProgressView>();
    require(progress->buildFromFile("panel_progress.xml"), "Required native progress XUI is missing");
    progress->setShape(mWindowRectScaled);
    mProgressView = progress.release();
    mRootView->addChild(mProgressView);
    auto mini = std::make_unique<LLProgressViewMini>();
    require(mini->buildFromFile("panel_progress_mini.xml"), "Required native mini-progress XUI is missing");
    mProgressViewMini = mini.release();
    mRootView->addChild(mProgressViewMini);
    LLPopupView::Params popups;
    popups.name = "popup_holder";
    popups.rect = mWindowRectScaled;
    popups.follows.flags = FOLLOWS_ALL;
    popups.mouse_opaque = false;
    mPopupView = LLUICtrlFactory::create<LLPopupView>(popups, mRootView);
    require(mPopupView != nullptr, "Native popup owner creation failed");
    mProgressViewMini->setVisible(false);
    mProgressView->setVisible(false);
}
void LLViewerWindow::drawNativeUI()
{
    require(mVulkanContext && mRootView, "Native viewer drawing requires initialized ownership");
    LLLayoutStack::updateClass();
    mVulkanContext->present(mDisplayScale.mV[VX], [&] { mRootView->draw(); });
}

void LLViewerWindow::reshapeNative(S32 width, S32 height)
{
    if (width <= 0 || height <= 0 || !mNativeUI)
        return;
    const F32 dpi = llclamp(gSavedSettings.getF32("UIScaleFactor") * mWindow->getSystemUISize(), .75f, 4.f);
    if (dpi != mDisplayScale.mV[VX])
    {
        mVulkanContext->wait();
        mVulkanContext->resources().releaseFontPages();
        LLFontGL::initClass(gSavedSettings.getF32("FontScreenDPI"), dpi, dpi, gDirUtilp->getAppRODataDir(),
                            gSavedSettings.getString("FSFontSettingsFile"), gSavedSettings.getF32("FSFontSizeAdjustment"), false);
        ++LLFontGL::sResolutionGeneration;
        mDisplayScale.setVec(dpi, dpi);
        LLUI::setScaleFactor(mDisplayScale);
    }
    mWindowRectRaw       = LLRect(0, height, width, 0);
    mWindowRectScaled    = LLRect(0, ll_round(height / dpi), ll_round(width / dpi), 0);
    mWorldViewRectRaw    = mWindowRectRaw;
    mWorldViewRectScaled = mWindowRectScaled;
    mRootView->reshape(mWindowRectScaled.getWidth(), mWindowRectScaled.getHeight());
}
bool LLViewerWindow::nativeMouse(LLCoordGL pos, MASK mask, EMouseClickType type, bool down)
{
    if (!mRootView)
        return false;
    pos.mX             = ll_round(pos.mX / mDisplayScale.mV[VX]);
    pos.mY             = ll_round(pos.mY / mDisplayScale.mV[VY]);
    mCurrentMousePoint = pos;
    if (type == CLICK_LEFT || type == CLICK_DOUBLELEFT)
        mLeftMouseDown = down;
    if (type == CLICK_RIGHT)
        mRightMouseDown = down;
    if (type == CLICK_MIDDLE)
        mMiddleMouseDown = down;
    auto* target = gFocusMgr.getMouseCapture();
    if (!target)
        target = mRootView;
    S32 x, y;
    target->screenPointToLocal(pos.mX, pos.mY, &x, &y);
    return target->handleAnyMouseClick(x, y, mask, type, down);
}
void LLViewerWindow::nativeHover(LLCoordGL pos, MASK mask)
{
    if (!mRootView)
        return;
    pos.mX             = ll_round(pos.mX / mDisplayScale.mV[VX]);
    pos.mY             = ll_round(pos.mY / mDisplayScale.mV[VY]);
    mCurrentMousePoint = pos;
    mMouseInWindow     = true;
    auto* target       = gFocusMgr.getMouseCapture();
    if (!target)
        target = mRootView;
    S32 x, y;
    target->screenPointToLocal(pos.mX, pos.mY, &x, &y);
    target->handleHover(x, y, mask);
}
void LLViewerWindow::nativeScroll(S32 clicks, bool horizontal)
{
    if (!mRootView)
        return;
    auto* target = gFocusMgr.getMouseCapture();
    if (!target)
        target = mRootView;
    S32 x, y;
    target->screenPointToLocal(mCurrentMousePoint.mX, mCurrentMousePoint.mY, &x, &y);
    if (horizontal)
        target->handleScrollHWheel(x, y, clicks);
    else
        target->handleScrollWheel(x, y, clicks);
}

// Native graphics and required startup views in LLViewerWindow. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "lldir.h"
#include "vsuiresources.h"
#include "llrootview.h"
#include "llpopupview.h"
#include "llpanel.h"
#include "llviewermenu.h"
#include "llmenugl.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llfloatertos.h"
#include "llfloaterabout.h"
#include "llfloaterpreference.h"
#include "llprogressview.h"
#include "llviewercontrol.h"
#include "llfocusmgr.h"
#include "llfontgl.h"
#include "lllayoutstack.h"
#include "llnotifications.h"
#include "llcommunicationchannel.h"
#include "llnotificationhandler.h"
#include "llnotificationmanager.h"
#include "llchicletbar.h"
#include "lltoolbarview.h"
#include "lltoolbar.h"
#include "llfavoritesbar.h"
#include "llnavigationbar.h"
#include "llstatusbar.h"
#include "llhints.h"
#include "fscommon.h"
#include "fsregistrarutils.h"
#include "lllocationinputctrl.h"
#include "vsnativesession.h"
#include "llbutton.h"
#include "llimview.h"
#include "llconsole.h"
#include "llchannelmanager.h"
#include "llfloateravatarpicker.h"
#include "llavataractions.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "vsvulkancontext.h"
#include "vsuicontext.h"
#include "vsstartupui.h"
#include "vsplainchat.h"
#include "vsuiadmission.h"
#include <stdexcept>
namespace
{
void require(bool v, const char* m)
{
    if (!v)
        throw std::runtime_error(m);
}
S32 nativeStatusRowHeight(LLXMLNodePtr node)
{
    if (!node) return 0;
    std::string name;
    if (node->getAttributeString("name", name) && name == "status_bar_container")
    {
        S32 height = 0;
        require(node->getAttributeS32("height", height) && height > 0,
            "Native status container has no valid skinned height");
        return height;
    }
    for (auto child = node->getFirstChild(); child; child = child->getNextSibling())
        if (const S32 height = nativeStatusRowHeight(child)) return height;
    return 0;
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
        mNativeSystemScale = mWindow->getSystemUISize();
        const F32 dpi = llclamp(gSavedSettings.getF32("UIScaleFactor") * mNativeSystemScale, 0.75f, 4.f);
        mDisplayScale.setVec(dpi, dpi);
        mWindowRectRaw       = LLRect(0, size.mY, size.mX, 0);
        mWindowRectScaled    = LLRect(0, ll_round(size.mY / dpi), ll_round(size.mX / dpi), 0);
        mWorldViewRectRaw    = mWindowRectRaw;
        mWorldViewRectScaled = mWindowRectScaled;
        // Startup consumes the one-shot setting into the active session flag.
        mVulkanContext       = std::make_unique<VSVulkanContext>(*mWindow,
            gDebugGLSession || gSavedSettings.getBOOL("RenderDebugGLSession"));
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
    // Register shared editing actions before any native text widget creates its menu.
    initialize_edit_menu();
    initialize_spellcheck_menu();
    initialize_volume_controls_callbacks();
    gFSRegistrarUtils.setEnableCheckFunction([](LLUUID id, EFSRegistrarFunctionActionType action)
    {
        switch (action)
        {
        case EFSRegistrarFunctionActionType::FS_RGSTR_ACT_ADD_FRIEND:
        case EFSRegistrarFunctionActionType::FS_RGSTR_ACT_REMOVE_FRIEND:
        case EFSRegistrarFunctionActionType::FS_RGSTR_ACT_SEND_IM:
        case EFSRegistrarFunctionActionType::FS_RGSTR_ACT_VIEW_TRANSCRIPT:
        case EFSRegistrarFunctionActionType::FS_RGSTR_ACT_SHOW_PROFILE:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_AVATAR_BLOCKED:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_IS_SELF:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_IS_NOT_SELF:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_WAITING_FOR_GROUP_DATA:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_HAVE_GROUP_DATA:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_CAN_LEAVE_GROUP:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_CAN_JOIN_GROUP:
        case EFSRegistrarFunctionActionType::FS_RGSTR_CHK_GROUP_NOT_ACTIVE:
            return FSCommon::checkIsActionEnabled(id, action);
        default:
            return false;
        }
    });
    mFloaterSnapRegion = mRootView; // Startup dialogs follow the real native root before world chrome exists.
    LLPanel::Params holder;
    holder.name          = "login_panel_holder";
    holder.rect          = mWindowRectScaled;
    holder.follows.flags = FOLLOWS_ALL;
    holder.mouse_opaque  = false;
    holder.tab_stop      = false;
    auto* login          = LLUICtrlFactory::create<LLPanel>(holder, mRootView);
    require(login != nullptr, "Native login holder creation failed");
    mLoginPanelHolder = login->getHandle();
    LLView::Params hints;
    hints.name = "hint_holder";
    hints.rect = mWindowRectScaled;
    hints.follows.flags = FOLLOWS_ALL;
    hints.mouse_opaque = false;
    auto* hint_holder = LLUICtrlFactory::create<LLView>(hints, mRootView);
    require(hint_holder != nullptr, "Native hint holder creation failed");
    mHintHolder = hint_holder->getHandle();
    LLConsole::Params console;
    console.name("console");
    console.max_lines(gSavedSettings.getS32("ConsoleBufferSize"));
    console.rect(getChatConsoleRect());
    console.parse_urls(true);
    console.background_image("Rounded_Square");
    console.session_support(true);
    console.persist_time(gSavedSettings.getF32("ChatPersistTime"));
    console.font_size_index(gSavedSettings.getS32("ChatConsoleFontSize"));
    console.follows.flags(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_BOTTOM);
    console.visible(false);
    gConsole = LLUICtrlFactory::create<LLConsole>(console, mRootView);
    require(gConsole != nullptr, "Native chat console creation failed");
    LLPanel::Params chat;
    chat.name = "native_nearby_chat";
    chat.rect = LLRect(10, 270, 510, 10);
    chat.follows.flags = FOLLOWS_LEFT | FOLLOWS_BOTTOM;
    chat.visible = false; // The connected session owner supplies transport and visibility.
    mNativeChat = LLUICtrlFactory::create<VSPlainChat>(chat, mRootView);
    require(mNativeChat != nullptr, "Required native nearby-chat owner creation failed");
    LLViewerMenuHolderGL::Params menu_holder;
    menu_holder.name = "Menu Holder";
    menu_holder.rect = mWindowRectScaled;
    menu_holder.follows.flags = FOLLOWS_ALL;
    menu_holder.mouse_opaque = false;
    gMenuHolder = LLUICtrlFactory::create<LLViewerMenuHolderGL>(menu_holder, mRootView);
    LLMenuGL::sMenuContainer = gMenuHolder;
    LLPanel::Params menu_bar;
    menu_bar.name = "menu_bar_holder";
    menu_bar.rect = LLRect(0, mWindowRectScaled.getHeight(), mWindowRectScaled.getWidth(), mWindowRectScaled.getHeight() - MENU_BAR_HEIGHT);
    menu_bar.follows.flags = FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_TOP;
    menu_bar.mouse_opaque = false;
    LLUICtrlFactory::create<LLPanel>(menu_bar, mRootView);
    // Read only the selected skin's layout metadata, without constructing the
    // world-bearing main_view graph. Status and menus have distinct row heights.
    LLXMLNodePtr main_view;
    require(LLUICtrlFactory::getLayeredXMLNode("main_view.xml", main_view),
        "Native main-view layout metadata is missing");
    const S32 status_height = nativeStatusRowHeight(main_view);
    require(status_height > 0, "Native main-view status container is missing");
    LLPanel::Params status;
    status.name = "status_bar_container";
    status.rect = LLRect(0, mWindowRectScaled.getHeight(), mWindowRectScaled.getWidth(),
        mWindowRectScaled.getHeight() - status_height);
    status.follows.flags = FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_TOP;
    status.mouse_opaque = false;
    mStatusBarContainer = LLUICtrlFactory::create<LLPanel>(status, mRootView);
    mRootView->sendChildToBack(mStatusBarContainer);

    LLFloaterView::Params floaters;
    floaters.name          = "Floater View";
    floaters.rect          = mWindowRectScaled;
    floaters.follows.flags = FOLLOWS_ALL;
    // Match main_view.xml: empty overlay space must pass input to login/chat.
    floaters.mouse_opaque = false;
    floaters.tab_stop     = false;
    gFloaterView           = LLUICtrlFactory::create<LLFloaterView>(floaters, mRootView);
    require(gFloaterView != nullptr, "Native required-dialog container creation failed");
    // Initialize default channels before registering viewer-owned children.
    LLNotifications::instance();
    mSystemChannel.reset(new LLNotificationChannel("System", "Visible", LLNotificationFilters::includeEverything));
    mCommunicationChannel.reset(new LLCommunicationChannel("Communication", "Visible"));
    LLNotificationsUI::LLNotificationManager::getInstance();
    LLFloaterAboutUtil::registerFloater();
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
    popups.tab_stop = false;
    popups.mouse_opaque = false;
    mPopupView = LLUICtrlFactory::create<LLPopupView>(popups, mRootView);
    require(mPopupView != nullptr, "Native popup owner creation failed");
    mProgressViewMini->setVisible(false);
    mProgressView->setVisible(false);
}
void LLViewerWindow::drawNativeUI()
{
    require(mVulkanContext && mRootView, "Native viewer drawing requires initialized ownership");
    if (mDisplayScale.mV[VX] != llclamp(gSavedSettings.getF32("UIScaleFactor") * mNativeSystemScale, .75f, 4.f))
        reshapeNative(mWindowRectRaw.getWidth(), mWindowRectRaw.getHeight());
    updateUI(); // Native branch updates layout and real focus/edit-menu ownership.
    LLConsole::updateClass();
    if (gConsole) gConsole->setShape(getChatConsoleRect());
    // The native simulator owner deliberately has no LLViewerRegion. Refresh the
    // shared location control from its authenticated CPU location model instead.
    if (const auto session = VSNativeSession::active(); session && session->phase() == VSNativeSession::Phase::Connected)
    {
        auto* location = mRootView->findChild<LLLocationInputCtrl>("location_combo");
        if (location && !location->hasFocus()) LLNavigationBar::instance().refreshLocationCtrl();
    }
    if (mNativeConnectedUI && mNativeChat && mFloaterSnapRegion)
    {
        // Keep the transcript in the shared usable floater area, above the
        // actual saved bottom toolbar/chat bar and clear of side toolbars.
        const LLRect area = mFloaterSnapRegion->calcScreenRect();
        const LLRect root = mRootView->calcScreenRect();
        const S32 width = llmin(500, llmax(1, area.getWidth() - 20));
        const S32 height = llmin(260, llmax(1, area.getHeight() - 20));
        LLRect rect; rect.setLeftTopAndSize(area.mLeft - root.mLeft + 10,
            area.mBottom - root.mBottom + 10 + height, width, height);
        if (mNativeChat->getRect() != rect) mNativeChat->setShape(rect);
    }
    mVulkanContext->present(mDisplayScale.mV[VX], [&] { mRootView->draw(); });
}
void LLViewerWindow::refreshNativeFonts()
{
    if (!mVulkanContext || !mNativeUI) return;
    mVulkanContext->wait();
    mVulkanContext->resources().releaseFontPages();
    const F32 dpi = mDisplayScale.mV[VX];
    LLFontGL::initClass(gSavedSettings.getF32("FontScreenDPI"), dpi, dpi, gDirUtilp->getAppRODataDir(),
        gSavedSettings.getString("FSFontSettingsFile"), gSavedSettings.getF32("FSFontSizeAdjustment"), false);
    ++LLFontGL::sResolutionGeneration;
}

void LLViewerWindow::reshapeNative(S32 width, S32 height, F32 system_scale)
{
    if (width <= 0 || height <= 0 || !mNativeUI)
        return;
    if (std::isfinite(system_scale) && system_scale > 0.f) mNativeSystemScale = system_scale;
    const F32 dpi = llclamp(gSavedSettings.getF32("UIScaleFactor") * mNativeSystemScale, .75f, 4.f);
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
    // Native mouse dispatch bypasses the ordinary viewer mouse maintenance.
    // Restore a cursor hidden while typing, and let the hovered widget replace
    // the default arrow instead of retaining a previous editor/resize cursor.
    mWindow->showCursorFromMouseMove();
    mWindow->setCursor(UI_CURSOR_ARROW);
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

void LLViewerWindow::setNativeConnected(bool connected)
{
    if (!mRootView || mNativeConnectedUI == connected) return;
    mNativeConnectedUI = connected;
    if (gConsole)
    {
        if (!connected) { gConsole->clear(); gConsole->clearSessions(); }
        gConsole->setVisible(connected && gSavedSettings.getBOOL("FSUseNearbyChatConsole"));
    }
    if (!connected && LLHints::instanceExists()) LLHints::deleteSingleton();
    if (!connected && mNativeUI) mNativeUI->resetAccountImages();
    if (LLFavoritesOrderStorage::instanceExists())
    {
        if (!connected) LLFavoritesOrderStorage::destroyClass();
        LLFavoritesOrderStorage::instance().resetAccount(connected);
    }
    if (connected)
    {
        // Native account startup does not broadcast legacy scene login hooks.
        // Preserve the ordinary localized account response defaults explicitly.
        LLFloaterPreference::initDoNotDisturbResponse();
        init_native_connected_menus();
    }
    if (gLoginMenuBarView) gLoginMenuBarView->setVisible(!connected);
    if (gMenuBarView) gMenuBarView->setVisible(connected);
    auto* navigation = mRootView->findChild<LLPanel>("navigation_bar");
    if (!navigation && connected)
    {
        // Use the same skinned navigation/favorites and configurable toolbar
        // owners as OpenGL. The native renderer changes their drawing resources,
        // not their XUI layout or saved command positions.
        LLPanel::Params navigation_params;
        navigation = LLUICtrlFactory::create<LLPanel>(navigation_params);
        require(navigation->buildFromFile("panel_navigation_bar.xml"), "Native navigation XUI is missing");
        navigation->setShape(LLRect(0, mStatusBarContainer->getRect().mBottom,
            mWindowRectScaled.getWidth(), mStatusBarContainer->getRect().mBottom - navigation->getRect().getHeight()));
        mRootView->addChildInBack(navigation);
        gToolBarView = LLUICtrlFactory::createFromFile<LLToolBarView>("panel_toolbar_view.xml",
            mRootView, LLDefaultChildRegistry::instance());
        require(gToolBarView != nullptr, "Native toolbar XUI is missing");
        gToolBarView->setShape(LLRect(0, navigation->getRect().mBottom, mWindowRectScaled.getWidth(), 0));
        mFloaterSnapRegion = gToolBarView->getChild<LLView>("floater_snap_region");
        const bool top = gSavedSettings.getBOOL("InternalShowGroupNoticesTopRight");
        auto* chiclet_host = gToolBarView->getChild<LLPanel>(top ? "chiclet_container" : "chiclet_container_bottom");
        gToolBarView->getChildView(top ? "chiclet_container_bottom" : "chiclet_container")->setVisible(false);
        auto* chiclets = LLChicletBar::getInstance();
        chiclets->setShape(chiclet_host->getLocalRect());
        chiclet_host->addChild(chiclets);
        auto* status_holder = mStatusBarContainer;
        gStatusBar = new LLStatusBar(status_holder->getLocalRect());
        gStatusBar->setFollowsAll();
        // buildFromFile supplies the XUI design rectangle. Match the ordinary
        // viewer's post-construction shape to the actual scaled container.
        gStatusBar->setShape(status_holder->getLocalRect());
        gStatusBar->setBackgroundColor(gMenuBarView->getBackgroundColor().get());
        status_holder->addChildInBack(gStatusBar);
        LLNavigationBar::instance();
    }
    gSavedSettings.setBOOL("FSInternalShowNavbarNavigationPanel", connected && gSavedSettings.getBOOL("ShowNavbarNavigationPanel"));
    gSavedSettings.setBOOL("FSInternalShowNavbarFavoritesPanel", connected && gSavedSettings.getBOOL("ShowNavbarFavoritesPanel"));
    if (navigation) navigation->setVisible(connected);
    if (gStatusBar) gStatusBar->setVisible(connected);
    if (connected && gStatusBar)
        if (auto* target = gStatusBar->findChild<LLView>("balance_bg"))
            LLHints::getInstance()->registerHintTarget("linden_balance", target->getHandle());
    if (gToolBarView)
    {
        if (connected)
        {
            require(gToolBarView->loadToolbars(), "Native account toolbar configuration could not be loaded");
            LLNavigationBar::instance().handleLoginComplete();
        }
        else gToolBarView->retireAccountToolbars();
        gToolBarView->setVisible(connected);
        gToolBarView->setToolBarsVisible(connected);
    }
    if (connected) LLNotificationsUI::LLChannelManager::instance().onNativeLoginCompleted();
}

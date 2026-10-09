// Existing XUI and input qualification inside the native viewer. LGPL-2.1.
#include "vsuifixture.h"
#include "vsplainchat.h"
#include "llbutton.h"
#include "llcontrol.h"
#include "lldir.h"
#include "llerrorcontrol.h"
#include "llfocusmgr.h"
#include "llfontgl.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "llprogressbar.h"
#include "llscrollbar.h"
#include "llscrollcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewborder.h"
#include "llviewereventrecorder.h"
#include "llviewerprecompiledheaders.h"
#include "vsuiadmission.h"
#include "vsuicontext.h"
#include "vsuiimageprovider.h"
#include "vsuiresources.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
bool widget(const std::type_info &t)
{
    return t == typeid(LLPanel) || t == typeid(LLView) || t == typeid(LLUICtrl) || t == typeid(LLTextBox) ||
           t == typeid(LLButton) || t == typeid(LLProgressBar) || t == typeid(LLLineEditor) ||
           t == typeid(VSChatInput) || t == typeid(VSPlainChat) || t == typeid(LLViewBorder) || t == typeid(LLTextEditor) ||
           t == typeid(LLScrollContainer) || t == typeid(LLScrollbar);
}
} // namespace
struct VSUIFixture::Impl
{
    VSUIResources &resources;
    VSUIAdmission admission;
    LLControlGroup config{"NativeUIConfig"}, ignores{"NativeUIIgnores"};
    std::unique_ptr<VSUIContext> context;
    LLView *root = nullptr;
    VSChatInput *input = nullptr;
    LLTextEditor *transcript = nullptr;
    VSPlainChat *chat = nullptr;
    LLError::RecorderPtr log;
    Impl(VSUIResources &r, LLWindow *window)
        : resources(r), admission(
                            widget, [](std::string_view) { return false; },
                            [](std::string_view name) { return name == "mini_progress_panel"; })
    {
        try
        {
            log = LLError::addGenericRecorder([](LLError::ELevel level, const std::string &message) {
                if (level >= LLError::LEVEL_INFO)
                    std::cerr << "NATIVE_XUI " << message << '\n';
            });
            gDirUtilp->initAppDirs("VulkanstormNativeDiagnostic", std::filesystem::current_path().string());
            require(config.loadFromFile("app_settings/settings.xml", true, false) > 0,
                    "Native UI defaults are missing");
            if (const char *skin = std::getenv("VS_VULKAN_DIAGNOSTIC_SKIN"))
                config.setString("SkinCurrent", skin);
            if (const char *theme = std::getenv("VS_VULKAN_DIAGNOSTIC_THEME"))
                config.setString("SkinCurrentTheme", theme);
            if (const char *language = std::getenv("VS_VULKAN_DIAGNOSTIC_LANGUAGE"))
                config.setString("Language", language);
            gDirUtilp->setSkinFolder(config.getString("SkinCurrent"), config.getString("SkinCurrentTheme"), "en");
            const auto skin_settings = gDirUtilp->getExpandedFilename(LL_PATH_TOP_SKIN, "settings.xml");
            if (std::filesystem::is_regular_file(skin_settings))
                require(config.loadFromFile(skin_settings, true, false) > 0, "Native skin settings cannot be loaded");
            LLUI::settings_map_t settings{
                {"config", &config}, {"floater", &config}, {"ignores", &ignores}, {"account", &config}};
            context = std::make_unique<VSUIContext>(r, window, settings, 320u, 240u, 1.f);
            root = context->root();
            LL_INFOS("NativeUI") << "Selected skin: " << context->skin() << "/" << context->theme()
                                 << " language=" << context->language() << LL_ENDL;
            if (const char *failure = std::getenv("VS_VULKAN_DIAGNOSTIC_FAIL");
                failure && std::string_view(failure) == "ui-construction")
                throw std::runtime_error("Injected failure: ui-construction");
            auto *progress = LLUICtrlFactory::createFromFile<LLPanel>("panel_progress_mini.xml", root,
                                                                      LLDefaultChildRegistry::instance());
            require(progress && progress->findChild<LLProgressBar>("progress_bar_mini") &&
                        progress->findChild<LLButton>("cancel_btn"),
                    "Corrected progress XUI controls are missing");
            progress->setRect(LLRect(10, 230, 210, 210));
            progress->findChild<LLProgressBar>("progress_bar_mini")->setValue(LLSD(50.f));
            LLPanel::Params chat_params;
            chat_params.name = "native_nearby_chat";
            chat_params.rect = LLRect(10, 190, 310, 20);
            chat = LLUICtrlFactory::create<VSPlainChat>(chat_params, root);
            require(chat, "Native plain chat was not admitted");
            input = chat->input();
            transcript = chat->transcript();
            // Fit both real production controls into this compact oracle layout.
            input->setRect(LLRect(0,170,300,140));
            transcript->setRect(LLRect(0,130,300,0));
            input->setText(LLStringExplicit("retained"));
            require(!chat->submit() && input->getText()=="retained", "Unbound native chat lost unsent text");
            chat->setSender([](const std::string&) { return false; });
            require(!chat->submit() && input->getText()=="retained", "Rejected native chat lost unsent text");
            std::string submitted;
            chat->setSender([&submitted](const std::string& value) { submitted=value; return true; });
            require(chat->submit() && submitted=="retained" && input->getText().empty(), "Native chat commit did not reach its transport interface");
            chat->setSender([](const std::string&) { return true; });
            std::string text;
            for (unsigned i = 0; i < 30; ++i)
                text += "Nearby " + std::to_string(i) + ": plain text\n";
            chat->append(text);
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }
    void cleanup()
    {
        context.reset();
        root = nullptr;
        input = nullptr;
        transcript = nullptr;
        if (log)
        {
            LLError::removeRecorder(log);
            log.reset();
        }
    }
    ~Impl()
    {
        cleanup();
    }
};
VSUIFixture::VSUIFixture(VSUIResources &r, LLWindow *w) : mImpl(std::make_unique<Impl>(r, w))
{
}
VSUIFixture::~VSUIFixture() = default;
std::array<std::string,3> VSUIFixture::skinSelection() const
{
    return {mImpl->context->skin(),mImpl->context->theme(),mImpl->context->language()};
}
bool VSUIFixture::unicode(unsigned character)
{
    auto *focus = gFocusMgr.getKeyboardFocus();
    return focus && focus->handleUnicodeChar(character, false);
}
bool VSUIFixture::key(unsigned char key, unsigned mask)
{
    auto *focus = gFocusMgr.getKeyboardFocus();
    return focus && focus->handleKey(key, mask, false);
}
bool VSUIFixture::mouse(int x, int y, unsigned mask, bool down)
{
    auto *handler = gFocusMgr.getMouseCapture();
    if (!handler)
        handler = mImpl->root;
    S32 local_x, local_y;
    handler->screenPointToLocal(x, y, &local_x, &local_y);
    return down ? handler->handleMouseDown(local_x, local_y, mask) : handler->handleMouseUp(local_x, local_y, mask);
}
void VSUIFixture::hover(int x, int y, unsigned mask)
{
    auto *handler = gFocusMgr.getMouseCapture();
    if (!handler)
        handler = mImpl->root;
    S32 local_x, local_y;
    handler->screenPointToLocal(x, y, &local_x, &local_y);
    handler->handleHover(local_x, local_y, mask);
}
bool VSUIFixture::scroll(int x, int y, int clicks)
{
    auto *handler = gFocusMgr.getMouseCapture();
    if (!handler)
        handler = mImpl->root;
    S32 local_x, local_y;
    handler->screenPointToLocal(x, y, &local_x, &local_y);
    return handler->handleScrollWheel(local_x, local_y, clicks);
}
void VSUIFixture::prepareMouseInput()
{
    mImpl->input->setFocus(false);
}
void VSUIFixture::finishMouseInput()
{
    require(!gFocusMgr.getMouseCapture(), "Native mouse release retained widget capture");
    // A synthetic button message establishes widget focus, not OS activation.
    // The queued focus transition sequence qualifies application focus separately.
    require(mImpl->input->hasFocus(), "Native mouse click did not focus the editor");
    mImpl->input->deselect();
    mImpl->input->setCursorToEnd();
}
int VSUIFixture::transcriptTop() const
{
    return mImpl->transcript->getVisibleDocumentRect().mTop;
}
void VSUIFixture::prepareScrollInput()
{
    draw(); // Complete root traversal and scroller layout before fixing the starting position.
    mImpl->transcript->setCursorAndScrollToEnd();
}
std::string VSUIFixture::inputText() const
{
    return mImpl->input->getText();
}
void VSUIFixture::focus(bool value)
{
    gFocusMgr.setAppHasFocus(value);
    if (!value)
        gFocusMgr.setMouseCapture(nullptr);
}
bool VSUIFixture::focused() const
{
    return gFocusMgr.getAppHasFocus() && mImpl->input->hasFocus();
}
void VSUIFixture::verifyInput()
{
    auto &c = *mImpl;
    c.input->setText(std::string("native "));
    c.input->setCursorToEnd();
    c.input->setFocus(true);
    require(c.input->hasFocus(), "Native editor focus was not acquired");
    require(key(KEY_TAB, MASK_NONE) && !c.input->hasFocus(), "Native tab focus traversal failed");
    c.input->setFocus(true);
    require(unicode(0x03a9) && c.input->getText() == "native \xCE\xA9", "Native Unicode editing failed");
    require(c.input->handleKeyHere(KEY_BACKSPACE, MASK_NONE) && c.input->getText() == "native ",
            "Native backspace failed");
    LLPreeditor &preedit = c.input->preeditor();
    S32 position = 0, length = 0;
    preedit.updatePreedit(LLWString{0x03a9}, {1}, {true}, 1);
    preedit.getPreeditRange(&position, &length);
    require(length == 1, "Native preedit was not installed");
    preedit.resetPreedit();
    preedit.getPreeditRange(&position, &length);
    require(length == 0, "Native preedit cancellation failed");
    require(mouse(25, 174, MASK_NONE, true) && gFocusMgr.getMouseCapture(),
            "Native editor mouse-down did not acquire capture");
    focus(false);
    require(!gFocusMgr.getMouseCapture(), "Native focus loss retained widget capture");
    focus(true);
    c.input->deselect();
    c.input->setCursorToEnd();
    c.transcript->setCursorAndScrollToEnd();
    c.resources.begin(320, 240, 1);
    c.transcript->draw(); // establish line layout before testing scrolling
    const auto before = c.transcript->getVisibleDocumentRect();
    require(c.transcript->handleScrollWheel(20, 20, -3), "Native transcript did not handle scrolling");
    c.transcript->draw();
    require(before != c.transcript->getVisibleDocumentRect(), "Native transcript viewport did not scroll");
    c.resources.finish();
}
std::vector<VSUIRenderer::Packet> VSUIFixture::draw()
{
    auto &c = *mImpl;
    c.resources.begin(320, 240, 1);
    LLView::sDirtyRect = LLRect(0, 240, 320, 0);
    c.root->draw();
    require(LLFontGL::sOriginStack.empty(), "Native widget traversal leaked matrix scopes");
    LLRender2D::setSceneBlendType(LLRender::BT_ADD);
    gl_rect_2d(240, 15, 250, 5, LLColor4(.1f, .2f, .3f, .5f));
    LLRender2D::setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);
    gl_rect_2d(260, 15, 270, 5, LLColor4(.1f, .2f, .3f, .5f));
    LLRender2D::setSceneBlendType(LLRender::BT_ALPHA);
    auto glow = c.resources.finish();
    require(glow.size() >= 2 && glow[glow.size() - 2].blend == VSUIRenderer::Blend::Additive &&
                glow.back().blend == VSUIRenderer::Blend::AdditiveAlpha,
            "Native glow blend state was lost");
    c.resources.begin(320,240,1);
    gl_line_2d(2,15,8,5,LLColor4(.7f,.4f,.2f,1.f));
    gl_triangle_2d(60,15,80,15,70,5,LLColor4(.2f,.5f,.8f,1.f),true);
    gl_triangle_2d(90,15,110,15,100,5,LLColor4(.2f,.5f,.8f,1.f),false);
    LLFontGL::getFontSansSerif()->renderUTF8("Style",0,140.f,10.f,LLColor4::white,
        LLFontGL::LEFT,LLFontGL::BOTTOM,LLFontGL::BOLD|LLFontGL::ITALIC|LLFontGL::UNDERLINE,LLFontGL::DROP_SHADOW,5,100);
    auto packets=c.resources.finish();
    glow.insert(glow.end(),packets.begin(),packets.end());
    packets=std::move(glow);
    return packets;
}

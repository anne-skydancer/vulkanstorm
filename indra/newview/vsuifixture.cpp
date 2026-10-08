// Existing XUI and input qualification inside the native viewer. LGPL-2.1.
#include "vsuifixture.h"
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
#include "vsuidrawbridge.h"
#include "vsuiimageprovider.h"
#include "vsuiresources.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace
{
// Expose the protected platform preeditor contract for diagnostic composition.
// Rendering and editing remain the existing LLLineEditor implementation.
class NativeLineEditor : public LLLineEditor
{
  public:
    explicit NativeLineEditor(const Params &p) : LLLineEditor(p)
    {
    }
    LLPreeditor &preeditor()
    {
        return *this;
    }
};
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
bool widget(const std::type_info &t)
{
    return t == typeid(LLPanel) || t == typeid(LLView) || t == typeid(LLUICtrl) || t == typeid(LLTextBox) ||
           t == typeid(LLButton) || t == typeid(LLProgressBar) || t == typeid(LLLineEditor) ||
           t == typeid(NativeLineEditor) || t == typeid(LLViewBorder) || t == typeid(LLTextEditor) ||
           t == typeid(LLScrollContainer) || t == typeid(LLScrollbar);
}
} // namespace
struct VSUIFixture::Impl
{
    VSUIResources &resources;
    VSUIDrawBridge bridge;
    VSUIAdmission admission;
    LLControlGroup config{"NativeUIConfig"}, ignores{"NativeUIIgnores"};
    std::unique_ptr<VSUIImageProvider> images;
    std::unique_ptr<LLPanel> root;
    NativeLineEditor *input = nullptr;
    LLTextEditor *transcript = nullptr;
    bool previous_focus = gFocusMgr.getAppHasFocus();
    bool owns_ui = false, owns_fonts = false, owns_factory = false, owns_recorder = false;
    LLError::RecorderPtr log;
    Impl(VSUIResources &r, LLWindow *window)
        : resources(r), bridge(r, 1), admission(
                                          widget, [](std::string_view) { return false; },
                                          [](std::string_view name) { return name == "mini_progress_panel"; })
    {
        try
        {
            require(!LLUI::instanceExists(), "Native XUI fixture requires exclusive UI ownership");
            // Static widget registration already creates the empty factory.
            // This pre-session owner acquires its subsequently loaded defaults.
            owns_factory = true;
            log = LLError::addGenericRecorder([](LLError::ELevel level, const std::string &message) {
                if (level >= LLError::LEVEL_INFO)
                    std::cerr << "NATIVE_XUI " << message << '\n';
            });
            gDirUtilp->initAppDirs("VulkanstormNativeDiagnostic", std::filesystem::current_path().string());
            gDirUtilp->setSkinFolder("default", "", "en");
            require(config.loadFromFile("app_settings/settings.xml", true, false) > 0,
                    "Native UI defaults are missing");
            require(LLUIColorTable::instance().loadFromSettings(), "Native UI colors are missing");
            images = std::make_unique<VSUIImageProvider>(r);
            LLUI::settings_map_t settings{
                {"config", &config}, {"floater", &config}, {"ignores", &ignores}, {"account", &config}};
            LLUI::createInstance(settings, images.get(), nullptr, nullptr);
            owns_ui = true;
            LLUI::getInstance()->mWindow = window;
            LLViewerEventRecorder::createInstance();
            owns_recorder = true;
            LLUI::setScaleFactor(LLVector2(1, 1));
            LLXMLNodePtr strings;
            require(LLUICtrlFactory::getLayeredXMLNode("strings.xml", strings) && LLTrans::parseStrings(strings, {}),
                    "Native UI translations are missing");
            owns_fonts = true;
            LLFontGL::initClass(96, 1, 1, std::filesystem::current_path().string(), "fonts.xml", 0, true);
            LLPanel::Params panel;
            panel.name = "native_ui_root";
            panel.focus_root = true;
            panel.rect = LLRect(0, 240, 320, 0);
            root.reset(LLUICtrlFactory::create<LLPanel>(panel));
            require(bool(root), "Native UI root was not admitted");
            if (const char *failure = std::getenv("VS_VULKAN_DIAGNOSTIC_FAIL");
                failure && std::string_view(failure) == "ui-construction")
                throw std::runtime_error("Injected failure: ui-construction");
            LLUI::getInstance()->setRootView(root.get());
            auto *progress = LLUICtrlFactory::createFromFile<LLPanel>("panel_progress_mini.xml", root.get(),
                                                                      LLDefaultChildRegistry::instance());
            require(progress && progress->findChild<LLProgressBar>("progress_bar_mini") &&
                        progress->findChild<LLButton>("cancel_btn"),
                    "Corrected progress XUI controls are missing");
            progress->setRect(LLRect(10, 230, 210, 210));
            progress->findChild<LLProgressBar>("progress_bar_mini")->setValue(LLSD(50.f));
            LLLineEditor::Params edit;
            edit.name = "native_chat_input";
            edit.rect = LLRect(10, 190, 310, 160);
            edit.spellcheck = false;
            edit.use_bg_color = true;
            edit.max_length.bytes = 1024;
            input = LLUICtrlFactory::create<NativeLineEditor>(edit, root.get());
            require(input, "Native input was not admitted");
            LLTextEditor::Params chat;
            chat.name = "native_plain_transcript";
            chat.rect = LLRect(10, 150, 310, 20);
            chat.read_only = true;
            chat.parse_urls = false;
            chat.spellcheck = false;
            chat.embedded_items = false;
            transcript = LLUICtrlFactory::create<LLTextEditor>(chat, root.get());
            require(transcript, "Native transcript was not admitted");
            std::string text;
            for (unsigned i = 0; i < 30; ++i)
                text += "Nearby " + std::to_string(i) + ": plain text\n";
            transcript->setText(text);
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }
    void cleanup()
    {
        if (owns_ui)
            LLUI::getInstance()->setRootView(nullptr);
        root.reset();
        input = nullptr;
        transcript = nullptr;
        // Default parameter blocks own native image facades and borrow font faces.
        // Release them before the font registry, asset provider and device.
        if (owns_factory)
        {
            LLUICtrlFactory::deleteSingleton();
            owns_factory = false;
        }
        if (owns_recorder)
        {
            LLViewerEventRecorder::deleteSingleton();
            owns_recorder = false;
        }
        if (owns_fonts)
        {
            LLFontGL::destroyDefaultFonts();
            owns_fonts = false;
        }
        if (owns_ui)
        {
            gFocusMgr.setAppHasFocus(previous_focus);
            LLUI::deleteSingleton();
            owns_ui = false;
        }
        images.reset();
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
std::string VSUIFixture::inputText() const
{
    return mImpl->input->getText();
}
void VSUIFixture::focus(bool value)
{
    gFocusMgr.setAppHasFocus(value);
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
    auto packets = c.resources.finish();
    require(packets.size() >= 2 && packets[packets.size() - 2].blend == VSUIRenderer::Blend::Additive &&
                packets.back().blend == VSUIRenderer::Blend::AdditiveAlpha,
            "Native glow blend state was lost");
    return packets;
}

// Native viewer UI lifecycle, preserving the existing skin/XUI/font system. LGPL-2.1.
#include "vsuicontext.h"
#include "llcontrol.h"
#include "lldir.h"
#include "llfocusmgr.h"
#include "llfontgl.h"
#include "llpanel.h"
#include "lltrans.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewereventrecorder.h"
#include "llviewerprecompiledheaders.h"
#include "vsuidrawbridge.h"
#include "vstranslations.h"
#include "vsuiaudio.h"
#include "vsuiimageprovider.h"
#include <cmath>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
struct VSUIContext::Impl
{
    VSUIDrawBridge                     bridge;
    std::unique_ptr<VSUIImageProvider> images;
    std::unique_ptr<LLView>            root;
    std::string                        skin, theme, language;
    bool                               owns_ui = false, owns_fonts = false, owns_factory = false, owns_recorder = false;
    const bool                         previous_focus = gFocusMgr.getAppHasFocus();

    Impl(VSUIResources& resources, LLWindow* window, const LLUI::settings_map_t& settings, unsigned width, unsigned height, float dpi,
         const std::function<LLView*(const LLRect&)>& root_factory) :
        bridge(resources, dpi)
    {
        try
        {
            require(!LLUI::instanceExists(), "Native UI requires exclusive lifecycle ownership");
            require(window && width && height && std::isfinite(dpi) && dpi > 0, "Invalid native UI window/extent");
            const auto found = settings.find("config");
            require(found != settings.end() && found->second, "Native UI configuration is missing");
            for (const char* group : { "floater", "ignores", "account" })
            {
                const auto entry = settings.find(group);
                require(entry != settings.end() && entry->second, "Native UI settings group is missing");
            }
            auto& config = *found->second;
            skin         = config.getString("SkinCurrent");
            if (skin.empty())
                skin = "default";
            theme = config.getString("SkinCurrentTheme");
            // Settings have already been loaded by the lifecycle caller. LLDir supplies
            // base/selected/theme/user overlays; the same paths serve both peer backends.
            gDirUtilp->setSkinFolder(skin, theme, "en");
            LLUIColorTable::instance().clear();
            require(LLUIColorTable::instance().loadFromSettings(), "Native UI skin colors are missing");
            images       = std::make_unique<VSUIImageProvider>(resources);
            owns_factory = true; // Static widget registration owns an initially empty factory.
            LLUI::createInstance(settings, images.get(), ui_audio_callback, deferred_ui_audio_callback);
            owns_ui                      = true;
            LLUI::getInstance()->mWindow = window;
            language                     = LLUI::getLanguage();
            gDirUtilp->setSkinFolder(skin, theme, language);
            if (!LLViewerEventRecorder::instanceExists())
            {
                LLViewerEventRecorder::createInstance();
                owns_recorder = true;
            }
            LLUI::setScaleFactor(LLVector2(dpi, dpi));
            LLXMLNodePtr strings;
            require(LLUICtrlFactory::getLayeredXMLNode("strings.xml", strings),
                    "Native UI skin translations are missing");
            vs_init_strings();
            owns_fonts = true;
            LLFontGL::initClass(config.getF32("FontScreenDPI"), dpi, dpi, gDirUtilp->getAppRODataDir(),
                                config.getString("FSFontSettingsFile"), config.getF32("FSFontSizeAdjustment"), false);
            LLPanel::Params panel;
            panel.name       = "native_ui_root";
            panel.focus_root = true;
            // Use the same logical-pixel rounding as LLViewerWindow's native
            // extent. A truncated root and rounded children retain their one-pixel
            // mismatch through every subsequent follows-based reshape.
            panel.rect       = LLRect(0, ll_round(height / dpi), ll_round(width / dpi), 0);
            root.reset(root_factory ? root_factory(panel.rect()) : LLUICtrlFactory::create<LLPanel>(panel));
            require(bool(root), "Native UI root was not admitted");
            LLUI::getInstance()->setRootView(root.get());
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
        root.reset(); // Controls, focus and IME detach while the native window is alive.
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
        images.reset(); // Facades/defaults and font faces are gone before the drawing bridge.
    }
    ~Impl() { cleanup(); }
};
VSUIContext::VSUIContext(VSUIResources& resources, LLWindow* window, const LLUI::settings_map_t& settings, unsigned width, unsigned height,
                         float dpi, std::function<LLView*(const LLRect&)> root_factory) :
    mImpl(std::make_unique<Impl>(resources, window, settings, width, height, dpi, root_factory))
{
}
VSUIContext::~VSUIContext() = default;
LLView* VSUIContext::root() const
{ return mImpl->root.get(); }
const std::string& VSUIContext::skin() const
{ return mImpl->skin; }
const std::string& VSUIContext::theme() const
{ return mImpl->theme; }
const std::string& VSUIContext::language() const
{ return mImpl->language; }
void VSUIContext::resetAccountImages() { mImpl->images->resetAccount(); }

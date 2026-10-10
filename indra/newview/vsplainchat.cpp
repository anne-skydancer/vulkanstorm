// Native plain nearby-chat controls, using the viewer's skin-aware editors. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsplainchat.h"
#include "lltexteditor.h"
#include "llbutton.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "llchat.h"
#include "llchatentry.h"
#include "fsfloaternearbychat.h"
#include "llviewercontrol.h"
#include "lluictrlfactory.h"
#include <stdexcept>

static LLDefaultChildRegistry::Register<VSChatInput> rNativeChatInput("vs_chat_input");
// ParamDefaults walks base_block_t and loads the selected skin's ordinary
// line_editor template. Do not copy defaults while constructing this block:
// the derived descriptor is not initialized until its constructor completes.
VSChatInput::Params::Params() = default;
VSPlainChat::VSPlainChat(const Params& p) : LLPanel(p) {}
bool VSPlainChat::postBuild()
{
    if (!mSkinBuilt)
    {
        // Factory initFromParams runs after construction. Build layered XUI here
        // so that original owner parameters cannot overwrite skin resources.
        const LLRect owner_rect = getRect();
        const bool owner_visible = getVisible();
        mSkinBuilt = true; // initPanelXML calls postBuild after creating children.
        if (!buildFromFile("panel_vs_nearby_chat.xml", LLPanel::getDefaultParams()))
            throw std::runtime_error("Required native nearby chat skin panel could not be built");
        setShape(owner_rect);
        setVisible(owner_visible);
        return true;
    }
    mTranscript = findChild<LLTextEditor>("native_plain_transcript");
    mInput = findChild<VSChatInput>("native_chat_input");
    mLogout = findChild<LLButton>("native_logout");
    mRegion = findChild<LLTextBox>("native_region");
    if (!mInput || !mTranscript || !mLogout || !mRegion)
        throw std::runtime_error("Required skinned native nearby chat controls were not admitted");
    mInput->setEnableLineHistory(true);
    mInput->setCommitCallback([this](LLUICtrl*, const LLSD&) { submit(); });
    setSession("", {});
    setSender({});
    return true;
}
void VSPlainChat::setSession(const std::string& region, std::function<void()> logout)
{
    const bool connected = bool(logout);
    mLogout->setVisible(connected); mRegion->setVisible(connected);
    mRegion->setText(region);
    mLogout->setCommitCallback([logout](LLUICtrl*, const LLSD&) { if (logout) logout(); });
}
void VSPlainChat::setSender(std::function<bool(const std::string&)> sender)
{
    mSender = std::move(sender);
    mInput->setEnabled(bool(mSender));
}
bool VSPlainChat::submit()
{
    std::string text = mInput->getText();
    LLStringUtil::trim(text);
    // Retain unsent text on transport rejection. No local fake delivery/echo.
    if (text.empty() || !mSender || !mSender(text)) return false;
    mInput->remember();
    mInput->setText(LLStringExplicit(""));
    return true;
}
void VSPlainChat::append(const std::string& text)
{
    if (mSharedFrontend)
    {
        LLChat chat;
        chat.mSourceType = CHAT_SOURCE_SYSTEM;
        chat.mText = text;
        LLSD args; args["do_not_log"] = true;
        if (auto* floater = FSFloaterNearbyChat::findInstance()) floater->addMessage(chat, true, args);
    }
    // Limit history before appending so the editor never silently drops new text.
    const std::string bounded = wstring_to_utf8str(utf8str_to_wstring(text).substr(0, 8192));
    while (mTranscript->getText().size() + bounded.size() + 1 > 65536)
        if (mTranscript->removeFirstLine() <= 0) { mTranscript->setText(LLStringExplicit("")); break; }
    mTranscript->appendText(bounded, !mTranscript->getText().empty());
}
void VSPlainChat::appendChat(const LLChat& chat)
{
    if (mSharedFrontend)
    {
        LLSD args; args["do_not_log"] = true; // Session/IM owner already applies account log policy.
        if (auto* floater = FSFloaterNearbyChat::findInstance()) floater->addMessage(chat, true, args);
    }
    // Keep protocol evidence without stripping metadata from the real frontend.
    const bool shared = mSharedFrontend;
    mSharedFrontend = false;
    const std::string prefix = chat.mChatType == CHAT_TYPE_IM || chat.mChatType == CHAT_TYPE_IM_GROUP ? "IM: " : "";
    append(prefix + chat.mFromNameGroup + chat.mFromName + (chat.mFromName.empty() ? "" : ": ") + chat.mText);
    mSharedFrontend = shared;
}
bool VSPlainChat::useSharedFrontend(bool enabled)
{
    if (enabled == mSharedFrontend) return true;
    if (enabled)
    {
        auto* floater = FSFloaterNearbyChat::getInstance();
        if (!floater) return false;
        if (gSavedPerAccountSettings.getBOOL("LogShowHistory")) floater->loadHistory();
        floater->openFloater(LLSD());
        floater->getChatBox()->setFocus(true);
    }
    else if (auto* floater = FSFloaterNearbyChat::findInstance()) floater->setVisible(false);
    mSharedFrontend = enabled;
    setVisible(!enabled);
    return true;
}
void VSPlainChat::clear()
{
    if (mSharedFrontend)
        if (auto* floater = FSFloaterNearbyChat::findInstance())
        {
            floater->clearChatHistory();
            floater->setVisible(false);
        }
    mSharedFrontend = false;
    setSender({});
    setSession("", {});
    mInput->clearHistory();
    mInput->setText(LLStringExplicit(""));
    mTranscript->setText(LLStringExplicit(""));
}

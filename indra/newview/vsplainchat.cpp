// Native plain nearby-chat controls, using the viewer's skin-aware editors. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsplainchat.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include <stdexcept>

VSPlainChat::VSPlainChat(const Params& p) : LLPanel(p)
{
    LLTextEditor::Params history;
    history.name = "native_plain_transcript";
    history.rect = LLRect(0, getRect().getHeight(), getRect().getWidth(), 36);
    history.follows.flags = FOLLOWS_ALL;
    history.read_only = true;
    history.parse_urls = false;
    history.spellcheck = false;
    history.embedded_items = false;
    history.max_text_length = 65536;
    mTranscript = LLUICtrlFactory::create<LLTextEditor>(history, this);
    LLLineEditor::Params edit;
    edit.name = "native_chat_input";
    edit.rect = LLRect(0, 30, getRect().getWidth(), 0);
    edit.follows.flags = FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_BOTTOM;
    edit.label = LLTrans::getString("NearbyChatTitle");
    edit.max_length.bytes = 1024;
    edit.spellcheck = false;
    edit.commit_on_focus_lost = false;
    mInput = LLUICtrlFactory::create<VSChatInput>(edit, this);
    if (!mInput || !mTranscript) throw std::runtime_error("Required native plain chat controls were not admitted");
    mInput->setCommitCallback([this](LLUICtrl*, const LLSD&) { submit(); });
    setSender({});
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
    mInput->setText(LLStringExplicit(""));
    return true;
}
void VSPlainChat::append(const std::string& text)
{
    // Limit history before appending so the editor never silently drops new text.
    const std::string bounded = wstring_to_utf8str(utf8str_to_wstring(text).substr(0, 8192));
    while (mTranscript->getText().size() + bounded.size() + 1 > 65536)
        if (mTranscript->removeFirstLine() <= 0) { mTranscript->setText(LLStringExplicit("")); break; }
    mTranscript->appendText(bounded, !mTranscript->getText().empty());
}
void VSPlainChat::clear()
{
    setSender({});
    mInput->setText(LLStringExplicit(""));
    mTranscript->setText(LLStringExplicit(""));
}

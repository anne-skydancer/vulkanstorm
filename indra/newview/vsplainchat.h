// Native plain nearby-chat controls; transport is supplied by the session owner. LGPL-2.1.
#pragma once
#include "llpanel.h"
#include "lllineeditor.h"
#include <functional>
class LLTextEditor;
class LLButton;
class LLTextBox;

class VSChatInput : public LLLineEditor
{
public:
    explicit VSChatInput(const Params& p) : LLLineEditor(p) {}
    LLPreeditor& preeditor() { return *this; }
    void remember()
    {
        updateHistory();
        if (mLineHistory.size() > 129) mLineHistory.erase(mLineHistory.begin(), mLineHistory.end() - 129);
        mCurrentHistoryLine = mLineHistory.empty() ? mLineHistory.end() : mLineHistory.end() - 1;
    }
    void clearHistory() { mLineHistory.clear(); mCurrentHistoryLine = mLineHistory.end(); }
};

class VSPlainChat : public LLPanel
{
public:
    explicit VSPlainChat(const Params&);
    // Root view ownership destroys controls and disconnects their callbacks.
    void setSender(std::function<bool(const std::string&)>);
    void setSession(const std::string& region, std::function<void()> logout);
    void append(const std::string& text);
    void clear();
    VSChatInput* input() const { return mInput; }
    LLTextEditor* transcript() const { return mTranscript; }
    bool submit();
private:
    VSChatInput* mInput = nullptr;
    LLTextEditor* mTranscript = nullptr;
    LLButton* mLogout = nullptr;
    LLTextBox* mRegion = nullptr;
    std::function<bool(const std::string&)> mSender;
};

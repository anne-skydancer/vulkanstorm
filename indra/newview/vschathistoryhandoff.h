// Worker-to-UI transcript publication. LGPL-2.1, like the viewer.
#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <utility>

// One instance per open preview. The history loader/deleter owns the message
// list until close; the worker never owns or dereferences the preview itself.
template<typename Messages>
class VSChatHistoryHandoff
{
public:
    struct Result
    {
        Messages*   messages;
        std::string file;
    };
    void publish(Messages* messages, const std::string& file)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mResult = Result{ messages, file };
    }
    std::optional<Result> take()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return std::exchange(mResult, std::nullopt);
    }

private:
    std::mutex            mMutex;
    std::optional<Result> mResult;
};

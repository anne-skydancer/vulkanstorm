// Shared conversation services on the native CPU session. LGPL-2.1.
#ifndef VS_NATIVE_IM_H
#define VS_NATIVE_IM_H
#include <functional>
#include <type_traits>
class LLMessageSystem;
class LLSD;
// Capture before scheduling work; a reply from a retired login cannot mutate
// the conversations belonging to the next account/session.
#if VS_NATIVE_VULKAN
std::function<bool()> vs_native_im_guard();
void vs_native_im_install(LLMessageSystem& messages);
void vs_native_im_receive(LLMessageSystem* message, void** context);
void vs_native_im_request_offline();
void vs_native_im_reset();
void vs_native_im_stamp_notification(LLSD& payload);
bool vs_native_im_notification_current(const LLSD& payload);
#else
inline std::function<bool()> vs_native_im_guard() { return [] { return true; }; }
inline void vs_native_im_stamp_notification(LLSD&) {}
inline bool vs_native_im_notification_current(const LLSD&) { return true; }
#endif
// Notification responders can outlive the producing panel. Hold only its
// handle and admit the response only in the login that created it.
template<class View, class Callback>
auto vs_native_im_ui_callback(View* owner, Callback callback)
{
    const auto weak = owner->getHandle();
    const auto current = vs_native_im_guard();
    return [weak, current, callback](const LLSD& notification, const LLSD& response) -> bool
    {
        if (!weak.get() || !current()) return false;
        if constexpr (std::is_void_v<std::invoke_result_t<Callback, const LLSD&, const LLSD&>>)
        {
            callback(notification, response);
            return false;
        }
        else
            return callback(notification, response);
    };
}
#endif

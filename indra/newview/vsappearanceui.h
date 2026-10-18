// Lifetime-scoped callbacks for the existing appearance controllers. LGPL-2.1.
#pragma once
#include "vsnativeim.h"
#include <type_traits>
#include <utility>

template<class View, class Callback>
auto vs_appearance_ui_callback(View* view, Callback callback)
{
    return [owner = view->getHandle(), current = vs_native_im_guard(), callback = std::move(callback)](auto&&... args) mutable
        -> decltype(callback(std::forward<decltype(args)>(args)...))
    {
        using Result = decltype(callback(std::forward<decltype(args)>(args)...));
        if (!current() || !owner.get())
        {
            if constexpr (!std::is_void_v<Result>) return Result{};
            else return;
        }
        return callback(std::forward<decltype(args)>(args)...);
    };
}

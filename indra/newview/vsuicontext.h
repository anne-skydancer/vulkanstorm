// Native viewer UI ownership shared by session integration and qualification. LGPL-2.1.
#pragma once
#include "llui.h"
#include <memory>
#include <functional>
#include <string>
class LLView;
class LLWindow;
class VSUIResources;
class VSUIContext
{
public:
    // Borrowed settings/resources and the native window must outlive this owner.
    // Install the caller's explicit widget/floater admission policy first.
    VSUIContext(VSUIResources&, LLWindow*, const LLUI::settings_map_t&, unsigned width, unsigned height, float dpi,
                std::function<LLView*(const LLRect&)> root_factory = {});
    ~VSUIContext();
    VSUIContext(const VSUIContext&)                  = delete;
    VSUIContext&       operator=(const VSUIContext&) = delete;
    LLView*            root() const;
    const std::string& skin() const;
    const std::string& theme() const;
    const std::string& language() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

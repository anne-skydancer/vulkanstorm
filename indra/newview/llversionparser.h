// SPDX-License-Identifier: LGPL-2.1-only
// Strict parsing of the stable and canary viewer versions used by VVM.
#ifndef LL_LLVERSIONPARSER_H
#define LL_LLVERSIONPARSER_H
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace LLViewerVersion
{
using Components = std::array<std::uint64_t, 4>;

inline std::optional<Components> parse(std::string_view text)
{
    Components components{};
    for (std::size_t i = 0; i < components.size(); ++i)
    {
        if (text.empty() || text.front() < '0' || text.front() > '9')
            return std::nullopt;
        const char* end = text.data() + text.size();
        auto result = std::from_chars(text.data(), end, components[i]);
        if (result.ec != std::errc{})
            return std::nullopt;
        text.remove_prefix(result.ptr - text.data());
        if (i == 2 && text.substr(0, 7) == "-canary")
            text.remove_prefix(7);
        if (i < 3)
        {
            if (text.empty() || text.front() != '.')
                return std::nullopt;
            text.remove_prefix(1);
        }
    }
    if (!text.empty())
        return std::nullopt;
    return components;
}

// VVM comparisons use numeric source counts, including for canary builds.
inline std::optional<int> compare(const Components& running, std::string_view required)
{
    const auto target = parse(required);
    if (!target)
        return std::nullopt;
    return running < *target ? -1 : running > *target ? 1 : 0;
}
}
#endif

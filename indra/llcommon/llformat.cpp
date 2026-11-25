/**
 * @file llformat.cpp
 * @date   January 2007
 * @brief string formatting utility
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llformat.h"

#include <cstdarg>

// common used function with va_list argument
// wrapper for vsnprintf to be called from llformatXXX functions.
static void va_format(std::string& out, const char *fmt, va_list va)
{
    // <Vulkanstorm>
    // Standard vsnprintf always NUL-terminates and returns the full length;
    // Windows' _vsnprintf did neither when the output reached the buffer size,
    // so out.assign(tstr) read past the buffer.
    char tstr[1024];    /* Flawfinder: ignore */
    va_list va2;
    va_copy(va2, va);
    int len = vsnprintf(tstr, sizeof(tstr), fmt, va); /* Flawfinder: ignore */
    if (len < 0)
    {
        // format error: no output
        out.clear();
    }
    else if (std::size_t(len) < sizeof(tstr))
    {
        out.assign(tstr, std::size_t(len));
    }
    else
    {
        // truncated: format again at the exact size
        out.resize(std::size_t(len) + 1);
        vsnprintf(&out[0], out.size(), fmt, va2); /* Flawfinder: ignore */
        out.resize(std::size_t(len));
    }
    va_end(va2);
    // </Vulkanstorm>
}

std::string llformat(const char *fmt, ...)
{
    std::string res;
    va_list va;
    va_start(va, fmt);
    va_format(res, fmt, va);
    va_end(va);
    return res;
}

std::string llformat_to_utf8(const char *fmt, ...)
{
    std::string res;
    va_list va;
    va_start(va, fmt);
    va_format(res, fmt, va);
    va_end(va);

#if LL_WINDOWS
    // made converting to utf8. See EXT-8318.
    res = ll_convert_string_to_utf8_string(res);
#endif
    return res;
}

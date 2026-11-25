/**
 * @file llformat_test.cpp
 * @brief Tests of llformat()
 *  *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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

#include <string>

#include "linden_common.h"

#include "../llformat.h"

#include "../test/lltut.h"

namespace tut
{
    struct format_data
    {
    };
    typedef test_group<format_data> format_test;
    typedef format_test::object format_object;
    tut::format_test format_group("llformat");

    // lengths around the 1024-byte stack buffer
    template<> template<>
    void format_object::test<1>()
    {
        for (std::size_t len : { 0, 1, 1022, 1023, 1024, 1025, 5000 })
        {
            std::string expect(len, 'x');
            std::string got = llformat("%s", expect.c_str());
            ensure_equals("length " + std::to_string(len), got.size(), len);
            ensure("content " + std::to_string(len), got == expect);
        }
    }

    template<> template<>
    void format_object::test<2>()
    {
        std::string expect = "a=7 " + std::string(2000, 'y') + " b=9";
        ensure_equals("mixed", llformat("a=%d %s b=%d", 7, std::string(2000, 'y').c_str(), 9), expect);
    }
} // namespace tut

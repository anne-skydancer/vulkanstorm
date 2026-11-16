// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llglfloor.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llglfloor_test.cpp
// The OpenGL 4.6 floor: which parsed versions pass, and what the message to a user below it says.

#include "llglfloor.h"

#include <cstdio>
#include <string>

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    bool has(const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    }

    void test_meets()
    {
        static_assert(LLGLFloor::REQUIRED_MAJOR == 4 && LLGLFloor::REQUIRED_MINOR == 6);
        check(!LLGLFloor::meets(0, 0), "no version string (0.0) is below the floor");
        check(!LLGLFloor::meets(1, 1), "1.1 (GDI generic) is below the floor");
        check(!LLGLFloor::meets(3, 3), "3.3 is below the floor");
        check(!LLGLFloor::meets(4, 0), "4.0 is below the floor");
        check(!LLGLFloor::meets(4, 5), "4.5 is below the floor");
        check(LLGLFloor::meets(4, 6), "4.6 meets the floor");
        check(LLGLFloor::meets(4, 7), "4.7 meets the floor");
        check(LLGLFloor::meets(4, 10), "4.10 meets the floor (minor is an integer, not a decimal)");
        check(LLGLFloor::meets(5, 0), "5.0 meets the floor");
    }

    void test_message()
    {
        const std::string m = LLGLFloor::message("4.5.0 NVIDIA 390.77", "NVIDIA GeForce GT 710");
        check(has(m, "too old for Vulkanstorm"), "says the card or driver is too old");
        check(has(m, "OpenGL 4.6"), "names what is needed");
        check(has(m, "4.5.0 NVIDIA 390.77"), "names the detected OpenGL version");
        check(has(m, "NVIDIA GeForce GT 710"), "names the detected renderer");
        check(has(m, "driver"), "points to a driver update");
        check(has(m, "This is not a crash"), "says it is not a crash");

        const std::string u = LLGLFloor::message("", "");
        check(has(u, "Graphics: unknown") && has(u, "OpenGL:   unknown"), "an empty field reads unknown");
        check(!has(u, "Graphics:  "), "no blank value");
    }
}

int main()
{
    test_meets();
    test_message();
    if (failures != 0)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("llglfloor: all checks passed\n");
    return 0;
}

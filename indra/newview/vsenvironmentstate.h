// Shared CPU environment values for native draw submission. LGPL-2.1.
#pragma once
#include <array>
class LLVector3;
struct VSEnvironmentState
{
    // std140 vec4 slots; copied values, never pointers into mutable settings.
    static constexpr unsigned Count=23;
    std::array<std::array<float,4>,Count> values{};
};
VSEnvironmentState vs_environment_state(const LLVector3& eye,const LLVector3& at,const LLVector3& up,
    float aspect,float verticalFov,float waterHeight);

// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef LL_LLPARTICLEPIPELINELAYOUT_H
#define LL_LLPARTICLEPIPELINELAYOUT_H

#include <cstddef>
#include <cstdint>

// Development ABI for particleSimulationC/particleOrderC. Not yet a viewer
// simulation service. All handles refer to stable slots; sorting moves indices.
namespace LLParticlePipeline
{
struct alignas(16) Particle
{
    float positionAge[4];
    float velocityLife[4];
    float accelerationParameter[4];
    float offsetStartGlow[4];
    float startColor[4];
    float endColor[4];
    float color[4];
    float scales[4];
    float scaleGlow[4];
    std::uint32_t identity[4]; // generation, live, flags, material
    std::uint32_t sourceParent[4]; // source slot/gen, parent slot/gen
    float axis[4]; // ribbon orientation
};
// Birth-only upload. Chain indices refer to other records in this upload, never
// resident slots; allocation and all stable handles are produced on the GPU.
struct alignas(16) Birth
{
    Particle particle;
    std::uint32_t chain[4]; // previous birth, next birth, reserved, reserved
};
static_assert(sizeof(Birth) == 208);
struct alignas(16) Source
{
    float position[4];
    float target[4];
    float callbackPosition[4];
    float callbackTarget[4];
    float regionOriginWidth[4];
    std::uint32_t control[4]; // generation, kill existing, callback kind, beam valid
    std::uint32_t wind[4]; // field offset, valid, reserved, reserved
    float ribbonAxis[4]; // source object Z axis in agent coordinates; w = object present
};
struct alignas(16) Region
{
    float originSize[4]; // agent-space origin.xy and region extent.xy
    std::uint32_t wind[4]; // field offset, valid, sampling width bits, reserved
};
static_assert(sizeof(Region) == 32);
struct alignas(16) Vertex
{
    float position[4];
    float normalMaterial[4]; // material ID stored as uint bits in w
    float color[4];
    float uvGlow[4]; // uv, glow, reserved
};
static_assert(sizeof(Vertex) == 64);
struct alignas(16) Entry
{
    std::uint32_t key[4];
    std::uint32_t value[4]; // slot, material, particle generation, world/HUD
};
// Matches ParticleMaterial in the resident alpha fragment shader. The sampler
// occupies eight bytes in std430; its texture must remain resident until all
// submitted draws using this record have completed.
struct alignas(16) Material
{
    std::uint64_t textureHandle;
    std::uint32_t blend; // source factor | (destination factor << 8)
    std::uint32_t flags; // bit 0: fullbright
};
static_assert(sizeof(Material) == 16);
static_assert(offsetof(Material, blend) == 8);
static_assert(offsetof(Material, flags) == 12);
static_assert(sizeof(Particle) == 192);
static_assert(offsetof(Particle, identity) == 144);
static_assert(offsetof(Particle, sourceParent) == 160);
static_assert(sizeof(Source) == 128);
static_assert(offsetof(Source, control) == 80);
static_assert(offsetof(Source, wind) == 96);
static_assert(sizeof(Entry) == 32);
} // namespace LLParticlePipeline
#endif

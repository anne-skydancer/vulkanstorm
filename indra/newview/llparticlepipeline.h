// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "llparticlepipelinelayout.h"
#include <array>
#include <vector>

namespace LLParticlePipeline
{
constexpr std::uint32_t MAX_CAPACITY = 8192;
using WindVelocity = std::array<float, 2>;
struct ViewBuffers
{
    std::uint32_t particles, spatial, depth, spatialRanges, materialRanges;
    std::uint32_t capacity;
};

// All methods require the render thread and current GL context.
// Program reload preserves resident state. Context destruction releases it.
// False selects the CPU simulation/ordering path; it never blocks viewer startup.
bool isSupported();
bool initGL();
void unloadShaders();
void destroyGL();

// Initialization/reset only: cannot resize or overwrite a running pool.
// The slot array is padded with inactive slots to a power of two (maximum 8192).
// Use MAX_CAPACITY inactive slots for a production pool.
bool initializePool(const std::vector<Particle>& initialSlots);
bool publishSources(const std::vector<Source>& sources,
                    const std::vector<WindVelocity>& wind);
// Upload births in emission order after advance() has retired old slots and
// repaired links. Admission is a GPU decision; success reports submission only.
// Drops the end of a burst when full. No existing live slot is overwritten.
bool emit(const std::vector<Particle>& births, std::uint32_t maxLiveCount = MAX_CAPACITY);
bool publishRegions(const std::vector<Region>& regions);
bool retireRegion(const std::array<float,3>& lower, const std::array<float,3>& upper);
bool advance(float dt, const std::array<float, 3>& originShift);
bool prepareView(const std::array<float, 3>& cameraPosition,
                 const std::array<float, 3>& cameraForward, float cellSize,
                 const float* viewProjection = nullptr, std::uint32_t viewDomain = 2);
std::uint32_t boundsBuffer();
std::uint32_t textureDemand(float pixelMeterRatio, const std::array<float,3>& camera);
struct AlphaBuffers
{
    std::uint32_t vertices, depth, intervals, commands, intervalCount;
};
// Boundaries follow the host's far-to-near alpha traversal (ties may repeat).
// Interval i draws before world group i; the final interval draws after all groups.
// Only non-particle group metadata is uploaded; particle counts stay on the GPU.
bool prepareAlphaIntervals(const std::vector<float>& groupDepths, std::uint32_t domain);
AlphaBuffers alphaBuffers();
// Caller binds the resident alpha program, detached image targets and depth
// policy. Only material resources are host-owned; counts/order remain GPU-owned.
bool drawAlphaInterval(std::uint32_t interval, std::uint32_t materials,
                       std::int32_t intervalUniform, bool interleaveGlow = false);

bool pick(const std::array<float,3>& start, const std::array<float,3>& end,
          std::array<std::uint32_t,2>& source, float& fraction);
ViewBuffers viewBuffers(); // zero handles until prepareView succeeds
}

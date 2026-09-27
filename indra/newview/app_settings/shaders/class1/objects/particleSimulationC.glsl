#version 430 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// GPU-resident simulation foundation. Not yet connected to LLViewerPartSim.
// Each invocation owns one slot; source snapshots are immutable for the dispatch.
// No particle readback is required between simulation steps.
layout(local_size_x = 64) in;
struct Particle
{
    vec4 positionAge;
    vec4 velocityLife;
    vec4 accelerationParameter;
    vec4 offsetStartGlow;
    vec4 startColor;
    vec4 endColor;
    vec4 color;
    vec4 scales; // start.xy, end.xy
    vec4 scaleGlow; // current.xy, current glow (0..255), end glow (0..1)
    uvec4 identity; // generation, live, flags, material ID
    uvec4 sourceParent; // source slot/generation, parent slot/generation
    vec4 axis; // retained ribbon orientation; xyz, reserved
};
struct Source
{
    vec4 position;
    vec4 target;
    vec4 callbackPosition;
    vec4 callbackTarget;
    vec4 regionOriginWidth; // agent-space SW corner; LLWind sampling width
    uvec4 control; // generation, kill existing, callback (0 none/1 spiral-chat/2 beam), beam valid
    uvec4 wind; // field offset in vec2 elements, field valid, reserved, reserved
    vec4 ribbonAxis;
};
layout(std430, binding = 0) buffer Particles { Particle particles[]; };
layout(std430, binding = 1) readonly buffer Sources { Source sources[]; };
layout(std430, binding = 2) readonly buffer Wind { vec2 winds[]; };
struct Region { vec4 originSize; uvec4 wind; };
layout(std430,binding=3) readonly buffer Regions { Region regions[]; };
uniform uint regionCount,killRegion;
uniform vec3 regionLower,regionUpper;
uniform uint particleCount;
uniform uint sourceCount;
uniform float deltaTime;
uniform vec3 originShift; // apply once per step; source snapshots already rebased

vec3 sampleWind(Source s, vec3 position)
{
    if (regionCount>0u)
    {
        Region region=regions[0]; // agent region is the outside-world fallback
        for(uint r=0u;r<regionCount;++r)
        {
            vec2 local=position.xy-regions[r].originSize.xy;
            if(all(greaterThanEqual(local,vec2(0))) && all(lessThan(local,regions[r].originSize.zw)))
            { region=regions[r]; break; }
        }
        s.regionOriginWidth=vec4(region.originSize.xy,0,uintBitsToFloat(region.wind.z));
        s.wind=region.wind;
    }
    float width = s.regionOriginWidth.w;
    if (s.wind.y == 0u || width <= 0.0) return vec3(0.0);
    vec2 p = max(position.xy - s.regionOriginWidth.xy, vec2(0.0));
    p = mod(p, vec2(width)) * (16.0 / width);
    ivec2 cell = clamp(ivec2(floor(p)), ivec2(0), ivec2(15));
    vec2 fraction = p - vec2(cell);
    uint k = s.wind.x + uint(cell.x + 16 * cell.y);
    if (k >= uint(winds.length())) return vec3(0.0);
    vec2 velocity = winds[k];
    // Preserve LLWind's nearest-cell behavior on the last row/column.
    if (cell.x < 15 && cell.y < 15 && k + 17u < uint(winds.length()))
    {
        float x = fraction.x, y = fraction.y;
        velocity = winds[k] * (1.0-x) * (1.0-y)
                 + winds[k+1u] * x * (1.0-y)
                 + winds[k+16u] * y * (1.0-x) + winds[k+17u] * x*y;
    }
    return vec3(velocity * 2.0, 0.0); // WIND_SCALE_HACK
}
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= particleCount) return;
    Particle p = particles[i];
    if (p.identity.y == 0u) return;
    if(killRegion!=0u)
    {
        if((p.identity.z&0x40000000u)==0u && all(greaterThanEqual(p.positionAge.xy,regionLower.xy)) &&
           all(lessThan(p.positionAge.xy,regionUpper.xy))) particles[i].identity.y=0u;
        return;
    }
    uint sourceIndex = p.sourceParent.x;
    if (sourceIndex >= sourceCount)
    {
        particles[i].identity.y = 0u;
        return;
    }
    Source s = sources[sourceIndex];
    uint flags = p.identity.z;
    float life = p.velocityLife.w;
    if (s.control.x != p.sourceParent.y || s.control.y != 0u ||
        flags == 0x80000000u || !(life > 0.0) ||
        (s.control.z == 2u && s.control.w == 0u))
    {
        particles[i].identity.y = 0u;
        return;
    }
    float dt = max(deltaTime, 0.0);
    float oldAge = p.positionAge.w;
    float age = oldAge + dt;
    // Expired slots are not simulated into NaN/Inf at remaining-life division.
    // Equality remains alive, matching the old strict > lifetime check.
    if (age > life)
    {
        particles[i].identity.y = 0u;
        return;
    }
    float fraction = age / life;
    p.positionAge.xyz += originShift;
    if ((flags & 0x10u) != 0u)
        p.positionAge.xyz = s.position.xyz + p.offsetStartGlow.xyz;
    if (s.control.z == 1u)
    {
        float oldFraction = oldAge / life;
        float angle = 6.283185307179586 * oldFraction + p.accelerationParameter.w;
        p.positionAge.xyz = s.callbackPosition.xyz +
            vec3(sin(angle), cos(angle), -0.5 + oldFraction);
    }
    else if (s.control.z == 2u)
        p.positionAge.xyz = mix(s.callbackPosition.xyz, s.callbackTarget.xyz, oldAge/life);
    if ((flags & 8u) != 0u)
        p.velocityLife.xyz = p.velocityLife.xyz * (1.0-0.1*dt)
            + 0.1*dt * sampleWind(s, p.positionAge.xyz);
    if ((flags & 0x40u) != 0u && life > oldAge)
    {
        float remaining = life - oldAge;
        float step = clamp(dt/remaining, 0.0, 0.1) * 5.0;
        p.velocityLife.xyz = p.velocityLife.xyz * (1.0-step)
            + step * (s.target.xyz - p.positionAge.xyz)/remaining;
    }
    if ((flags & 0x80u) != 0u)
    {
        p.velocityLife.xyz = s.target.xyz - s.position.xyz;
        p.positionAge.xyz = s.position.xyz + fraction * p.velocityLife.xyz;
    }
    else
    {
        p.positionAge.xyz += dt*p.velocityLife.xyz
            + 0.5*dt*dt*p.accelerationParameter.xyz;
        p.velocityLife.xyz += dt*p.accelerationParameter.xyz;
    }
    if ((flags & 4u) != 0u)
    {
        float dz = p.positionAge.z - s.position.z;
        if (dz < 0.0)
        {
            p.positionAge.z -= 2.0*dz;
            p.velocityLife.z *= -0.75;
        }
    }
    if ((flags & 0x10u) != 0u)
        p.offsetStartGlow.xyz = p.positionAge.xyz - s.position.xyz;
    if ((flags & 1u) != 0u)
        p.color = p.startColor*(1.0-fraction) + p.endColor*fraction;
    if ((flags & 2u) != 0u)
        p.scaleGlow.xy = p.scales.xy*(1.0-fraction) + p.scales.zw*fraction;
    p.scaleGlow.z = floor(mix(p.offsetStartGlow.w, p.scaleGlow.w, fraction)*255.0 + 0.5);
    p.positionAge.w = age;
    if (any(isnan(p.positionAge)) || any(isinf(p.positionAge)) ||
        any(isnan(p.velocityLife)) || any(isinf(p.velocityLife)))
        p.identity.y = 0u;
    particles[i] = p;
}

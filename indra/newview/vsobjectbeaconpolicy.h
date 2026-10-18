// Original LLPipeline beacon category predicates/order over real source peers.
// LGPL-2.1, like pipeline.cpp.
#pragma once
#include <array>
struct VSObjectBeaconFlags
{
    bool touch=false, scripted=false, physical=false, media=false, particles=false, sound=false;
};
template<class Object>
std::array<bool,5> vs_object_beacon_categories(const Object& object, const VSObjectBeaconFlags& flags)
{
    // postSort deliberately prefers touch over all scripted beacons, even if
    // both settings came from an older stored configuration.
    return {
        !object.isAvatar() && !object.getParent() && object.flagScripted() &&
            (flags.touch ? object.flagHandleTouch() : flags.scripted),
        flags.physical && !object.isAvatar() && object.flagUsePhysics(),
        flags.media && !object.isAvatar() && object.hasMedia(),
        flags.particles && object.isParticleSource(),
        flags.sound && object.isAudioSource()
    };
}

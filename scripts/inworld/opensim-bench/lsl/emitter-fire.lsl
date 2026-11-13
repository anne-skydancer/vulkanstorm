// SPDX-License-Identifier: GPL-3.0-or-later
// Particle bench emitter "fire": firework post at 12 m (4 posts around the plaza), 350-particle bursts, streaks (non-uniform scale + follow velocity).
// Commands arrive from controller.lsl over channel -74250 via llRegionSay ("fire", "all", or anything else = stop).
integer CMD = -74250;
string  SET = "fire";

// Texture from inventory chosen by position, so it is stable across runs. Put 4 different textures in each
// emitter (or at least in the emitters of one scene): different textures split the draw batches.
list tex()
{
    integer n = llGetInventoryNumber(INVENTORY_TEXTURE);
    if (n == 0) return [];
    vector p = llGetPos();
    return [PSYS_SRC_TEXTURE, llGetInventoryName(INVENTORY_TEXTURE, ((integer)(p.x + p.y)) % n)];
}

list PARAMS()
{
    return [PSYS_PART_FLAGS, PSYS_PART_EMISSIVE_MASK|PSYS_PART_INTERP_COLOR_MASK|PSYS_PART_INTERP_SCALE_MASK|PSYS_PART_FOLLOW_VELOCITY_MASK,
     PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_EXPLODE, PSYS_SRC_BURST_RATE, 1.5, PSYS_SRC_BURST_PART_COUNT, 350,
     PSYS_SRC_BURST_RADIUS, 0.3, PSYS_SRC_BURST_SPEED_MIN, 4.0, PSYS_SRC_BURST_SPEED_MAX, 9.0, PSYS_SRC_ACCEL, <0,0,-2.5>,
     PSYS_PART_MAX_AGE, 2.5, PSYS_PART_START_SCALE, <0.06,0.4,0>, PSYS_PART_END_SCALE, <0.03,0.15,0>,
     PSYS_PART_START_COLOR, <1,0.8,0.3>, PSYS_PART_END_COLOR, <1,0.2,0.1>, PSYS_PART_START_ALPHA, 1.0, PSYS_PART_END_ALPHA, 0.0,
     PSYS_PART_START_GLOW, 0.3, PSYS_PART_END_GLOW, 0.0];
}

start() { llParticleSystem(PARAMS() + tex()); }

default
{
    state_entry()
    {
        llParticleSystem([]);
        llListen(CMD, "", NULL_KEY, "");
    }
    on_rez(integer p) { llResetScript(); }
    attach(key id) { if (id) llResetScript(); }
    listen(integer c, string n, key id, string m)
    {
        if (m == SET || m == "all") start(); else llParticleSystem([]);
    }
}

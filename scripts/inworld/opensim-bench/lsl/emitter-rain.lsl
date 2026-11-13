// SPDX-License-Identifier: GPL-3.0-or-later
// Particle bench emitter "rain": rain emitter at 25 m, rotated to point down (rotation <180,0,0>). Description "follow" adds FOLLOW_SRC (set it on one of the two).
// Commands arrive from controller.lsl over channel -74250 via llRegionSay ("rain", "all", or anything else = stop).
integer CMD = -74250;
string  SET = "rain";

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
    integer fl = PSYS_PART_WIND_MASK | PSYS_PART_FOLLOW_VELOCITY_MASK | PSYS_PART_INTERP_COLOR_MASK;
    if (llGetObjectDesc() == "follow") fl = fl | PSYS_PART_FOLLOW_SRC_MASK;
    return [PSYS_PART_FLAGS, fl,
     PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_ANGLE_CONE, PSYS_SRC_ANGLE_BEGIN, 0.0, PSYS_SRC_ANGLE_END, 0.6,
     PSYS_SRC_BURST_RATE, 0.05, PSYS_SRC_BURST_PART_COUNT, 25, PSYS_SRC_BURST_RADIUS, 6.0,
     PSYS_SRC_BURST_SPEED_MIN, 1.0, PSYS_SRC_BURST_SPEED_MAX, 3.0, PSYS_SRC_ACCEL, <0,0,-4>, PSYS_PART_MAX_AGE, 6.0,
     PSYS_PART_START_SCALE, <0.04,0.35,0>, PSYS_PART_END_SCALE, <0.04,0.35,0>, PSYS_PART_START_ALPHA, 0.6, PSYS_PART_END_ALPHA, 0.2];
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

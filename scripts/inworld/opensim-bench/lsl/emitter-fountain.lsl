// SPDX-License-Identifier: GPL-3.0-or-later
// Particle bench emitter "fountain": ground fountain (bounce), cone of 10 particles per 0.03 s.
// Commands arrive from controller.lsl over channel -74250 via llRegionSay ("fountain", "all", or anything else = stop).
integer CMD = -74250;
string  SET = "fountain";

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
    return [PSYS_PART_FLAGS, PSYS_PART_BOUNCE_MASK|PSYS_PART_INTERP_SCALE_MASK, PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_ANGLE_CONE,
     PSYS_SRC_ANGLE_BEGIN, 0.0, PSYS_SRC_ANGLE_END, 0.3, PSYS_SRC_BURST_RATE, 0.03, PSYS_SRC_BURST_PART_COUNT, 10,
     PSYS_SRC_BURST_SPEED_MIN, 6.0, PSYS_SRC_BURST_SPEED_MAX, 8.0, PSYS_SRC_ACCEL, <0,0,-9.8>, PSYS_PART_MAX_AGE, 3.0,
     PSYS_PART_START_SCALE, <0.12,0.12,0>, PSYS_PART_END_SCALE, <0.3,0.3,0>];
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

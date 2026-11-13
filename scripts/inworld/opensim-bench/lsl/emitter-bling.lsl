// SPDX-License-Identifier: GPL-3.0-or-later
// Particle bench emitter "bling": 30 NPC attachments, explode burst, emissive glow, about 1440 particles in all.
// Commands arrive from controller.lsl over channel -74250 via llRegionSay ("bling", "all", or anything else = stop).
integer CMD = -74250;
string  SET = "bling";

// Texture from inventory by a static index, so it never changes during a run and is stable across runs:
// attached (NPC bling): derived from the owner key (distinct per NPC); in-world: the last digit of the object name
// ("Fire 2" -> 2). Put 4 different textures in each emitter: the differences split the draw batches.
list tex()
{
    integer n = llGetInventoryNumber(INVENTORY_TEXTURE);
    if (n == 0) return [];
    integer i;
    if (llGetAttached()) i = llAbs((integer)("0x" + llGetSubString((string)llGetOwner(), 0, 6)));
    else i = (integer)llGetSubString(llGetObjectName(), -1, -1);
    return [PSYS_SRC_TEXTURE, llGetInventoryName(INVENTORY_TEXTURE, i % n)];
}

list PARAMS()
{
    return [PSYS_PART_FLAGS, PSYS_PART_EMISSIVE_MASK|PSYS_PART_INTERP_COLOR_MASK|PSYS_PART_INTERP_SCALE_MASK,
     PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_EXPLODE, PSYS_SRC_BURST_RATE, 0.1, PSYS_SRC_BURST_PART_COUNT, 4,
     PSYS_SRC_BURST_RADIUS, 0.05, PSYS_SRC_BURST_SPEED_MIN, 0.02, PSYS_SRC_BURST_SPEED_MAX, 0.08,
     PSYS_PART_MAX_AGE, 1.2, PSYS_PART_START_SCALE, <0.08,0.08,0>, PSYS_PART_END_SCALE, <0.02,0.02,0>,
     PSYS_PART_START_COLOR, <1,1,1>, PSYS_PART_END_COLOR, <0.6,0.8,1>, PSYS_PART_START_ALPHA, 1.0,
     PSYS_PART_END_ALPHA, 0.0, PSYS_PART_START_GLOW, 0.2, PSYS_PART_END_GLOW, 0.0];
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

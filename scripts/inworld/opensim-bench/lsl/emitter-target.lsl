// SPDX-License-Identifier: GPL-3.0-or-later
// Particle bench emitter "target": two posts target each other. Description: <uuid of the other post> [linear]. "linear" uses TARGET_LINEAR, otherwise TARGET_POS.
// Commands arrive from controller.lsl over channel -74250 via llRegionSay ("target", "all", or anything else = stop).
integer CMD = -74250;
string  SET = "target";

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
    list d = llParseString2List(llGetObjectDesc(), [" "], []);
    key other = (key)llList2String(d, 0);
    if (other == NULL_KEY) llOwnerSay("emitter-target: put the UUID of the other post in the description");
    integer fl = PSYS_PART_EMISSIVE_MASK;
    if (llList2String(d, 1) == "linear") fl = fl | PSYS_PART_TARGET_LINEAR_MASK;
    else fl = fl | PSYS_PART_TARGET_POS_MASK;
    return [PSYS_PART_FLAGS, fl, PSYS_SRC_TARGET_KEY, other,
     PSYS_SRC_PATTERN, PSYS_SRC_PATTERN_DROP, PSYS_SRC_BURST_RATE, 0.1, PSYS_SRC_BURST_PART_COUNT, 10,
     PSYS_PART_MAX_AGE, 4.0, PSYS_PART_START_SCALE, <0.1,0.1,0>];
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

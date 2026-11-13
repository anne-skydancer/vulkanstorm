// SPDX-License-Identifier: GPL-3.0-or-later
// NPC spawner (OSSL; OpenSim only). Place it at the plaza centre; the 5 x 6 NPC grid (2 m spacing) is centred on it.
// Put an animation named "bench-dance" in the prim's inventory. Run it as the estate owner.
//   /8 save   wear the bling attachment first: saves the appearance as "bench-look"
//   /8 spawn  30 NPCs (refused while NPCs from this script exist; OpenSim's MaxNumberNPCsPerScene defaults to 40)
//   /8 remove removes them
// NPCs are removed when the script resets or the region restarts. They are probably not part of an OAR: after
// "load oar", say /8 spawn again. Spawn NPCs only for scenes A (bling) and D (all): they wear the bling and have
// alpha clothing, so they also load scenes B and C if present. A baseline with the NPCs present and every emitter
// stopped (/7 stop) gives the cost to subtract.
integer COUNT = 30;
list npcs;
removeAll()
{
    integer i;
    for (i = 0; i < llGetListLength(npcs); ++i) osNpcRemove(llList2Key(npcs, i));
    npcs = [];
}
default
{
    state_entry() { llListen(8, "", llGetOwner(), ""); }
    on_rez(integer p) { removeAll(); llResetScript(); }
    changed(integer c) { if (c & CHANGED_REGION_START) llResetScript(); }
    listen(integer c, string n, key id, string m)
    {
        if (m == "save")
        {
            osAgentSaveAppearance(llGetOwner(), "bench-look");
            llOwnerSay("saved");
        }
        else if (m == "spawn")
        {
            if (llGetListLength(npcs) > 0) { llOwnerSay("NPCs exist: /8 remove first"); return; }
            vector c0 = llGetPos() + <-4.0, -5.0, 0.0>;
            integer i;
            for (i = 0; i < COUNT; ++i)
            {
                vector p = c0 + <(float)(i / 6) * 2.0, (float)(i % 6) * 2.0, 0.0>;
                p.z = llGround(p - llGetPos()) + 0.5;
                key k = osNpcCreate("Bench", "Npc" + (string)i, p, "bench-look");
                npcs += k;
                osNpcPlayAnimation(k, "bench-dance");
            }
        }
        else if (m == "remove") removeAll();
    }
}

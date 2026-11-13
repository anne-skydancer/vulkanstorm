// SPDX-License-Identifier: GPL-3.0-or-later
// NPC spawner (OSSL; OpenSim only). Put an animation named "bench-dance" in the prim's inventory.
//   /8 save   (wear the bling attachment first: saves the appearance as "bench-look")
//   /8 spawn  (30 NPCs in a 5 x 6 grid, dancing)
//   /8 remove
list npcs;
default
{
    state_entry() { llListen(8, "", llGetOwner(), ""); }
    listen(integer c, string n, key id, string m)
    {
        if (m == "save")
        {
            osAgentSaveAppearance(llGetOwner(), "bench-look");
            llOwnerSay("saved");
        }
        else if (m == "spawn")
        {
            vector base = llGetPos() + <4.0, -5.0, 0.0>;
            integer i;
            for (i = 0; i < 30; ++i)
            {
                key k = osNpcCreate("Bench", "Npc" + (string)i,
                    base + <(float)(i / 6) * 2.0, (float)(i % 6) * 2.0, 0.0>, "bench-look");
                npcs += k;
                osNpcPlayAnimation(k, "bench-dance");
            }
        }
        else if (m == "remove")
        {
            integer i;
            for (i = 0; i < llGetListLength(npcs); ++i) osNpcRemove(llList2Key(npcs, i));
            npcs = [];
        }
    }
}

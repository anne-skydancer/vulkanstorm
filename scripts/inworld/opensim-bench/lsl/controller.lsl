// SPDX-License-Identifier: GPL-3.0-or-later
// Bench controller (a post near the camera seat). The owner says /7 <scene> on channel 7:
//   bling | fire | ribbon | rain | fountain | target | all | stop (anything else also stops the emitters)
// llRegionSay reaches the emitters in the region, including those worn by NPCs.
default
{
    state_entry() { llListen(7, "", llGetOwner(), ""); }
    listen(integer c, string n, key id, string m)
    {
        m = llToLower(llStringTrim(m, STRING_TRIM));
        llRegionSay(-74250, m);
        llOwnerSay("bench: " + m);
    }
}

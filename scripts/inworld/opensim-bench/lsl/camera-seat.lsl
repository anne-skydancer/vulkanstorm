// SPDX-License-Identifier: GPL-3.0-or-later
// Still camera seat. Place it 20 m from the plaza centre on the plaza's -x side, unrotated, so the view looks along +x
// at the plaza. Sit on it (click); the camera offsets fix the view.
default
{
    state_entry()
    {
        llSetClickAction(CLICK_ACTION_SIT);
        llSitTarget(<0.0, 0.0, 0.6>, ZERO_ROTATION);
        llSetCameraEyeOffset(<-1.0, 0.0, 2.2>);
        llSetCameraAtOffset(<12.0, 0.0, 1.2>);
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Still camera seat. Sit on it, the view is fixed by the camera offsets (about 20 m from the plaza centre).
default
{
    state_entry()
    {
        llSitTarget(<0.0, 0.0, 0.6>, ZERO_ROTATION);
        llSetCameraEyeOffset(<-1.0, 0.0, 2.2>);
        llSetCameraAtOffset(<12.0, 0.0, 1.2>);
    }
}

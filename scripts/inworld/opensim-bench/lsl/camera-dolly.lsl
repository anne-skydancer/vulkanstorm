// SPDX-License-Identifier: GPL-3.0-or-later
// Moving camera: the same seat on a keyframed ping-pong line, 30 m in 15 s each way.
// Point the line along the plaza's axis so the distance to the particles changes continuously.
default
{
    state_entry()
    {
        llSitTarget(<0.0, 0.0, 0.6>, ZERO_ROTATION);
        llSetCameraEyeOffset(<-1.0, 0.0, 2.2>);
        llSetCameraAtOffset(<12.0, 0.0, 1.2>);
        llSetKeyframedMotion([], []);
        llSetKeyframedMotion([<30.0, 0.0, 0.0>, 15.0], [KFM_MODE, KFM_PING_PONG, KFM_DATA, KFM_TRANSLATION]);
    }
}

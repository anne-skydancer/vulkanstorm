// SPDX-License-Identifier: GPL-3.0-or-later
// Moving camera: the seat on a keyframed ping-pong leg. Geometry: place it 20 m from the plaza centre on the plaza's
// -x side and 10 m to the -y side of the centre line, unrotated (so it looks along +x). It then travels 20 m along +y
// in 15 s and back. The plaza stays in the frustum the whole time at 20 m (middle) to 22.4 m (ends) distance
// (+-12 %), seen from -27 to +27 degrees to the side, and the particle groups cross the camera's view continuously.
// The script restarts the leg from the placed start when it is rezzed or reset.
default
{
    on_rez(integer p) { llResetScript(); }
    state_entry()
    {
        llSetClickAction(CLICK_ACTION_SIT);
        llSitTarget(<0.0, 0.0, 0.6>, ZERO_ROTATION);
        llSetCameraEyeOffset(<-1.0, 0.0, 2.2>);
        llSetCameraAtOffset(<12.0, 0.0, 1.2>);
        llSetKeyframedMotion([], []);
        llSetKeyframedMotion([<0.0, 20.0, 0.0>, 15.0], [KFM_MODE, KFM_PING_PONG, KFM_DATA, KFM_TRANSLATION]);
    }
}

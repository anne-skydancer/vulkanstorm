// Native CPU camera and simulator movement intent. LGPL-2.1.
#pragma once
#include "v3math.h"
#include "llquaternion.h"
#include "indra_constants.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

class VSNativeCamera
{
public:
    struct State
    {
        LLVector3 center, focus, at, left, up;
        LLQuaternion bodyRotation, headRotation;
        bool firstPerson = false;
    };
    void reset() { mHeld.clear(); mYaw = mBodyYaw = mRoll = mPitch = 0; mDistance = 1.f; mPan.clear(); mFlying = false; }
    void release() { mHeld.clear(); }
    void resetView() { mYaw = mBodyYaw; mPitch = mRoll = 0; mDistance = 1.f; mPan.clear(); }
    void resetOrbit() { mYaw = mBodyYaw; mPitch = mRoll = 0; }
    void resetPan() { mPan.clear(); }
    void resetRoll() { mRoll = 0; }
    F32 zoomFraction() const { return std::clamp((8.f - mDistance) / 7.75f, 0.f, 1.f); }
    void setZoomFraction(F32 fraction)
    { if (std::isfinite(fraction)) mDistance = 8.f - std::clamp(fraction, 0.f, 1.f) * 7.75f; }
    void setFirstPerson(bool first) { if (first) { mDistance = 0; mPan.clear(); mBodyYaw = mYaw; } else if (firstPerson()) mDistance = 1.f; }
    void roll(F32 angle) { if (std::isfinite(angle)) mRoll = wrap(mRoll + angle); }
    void pan(const LLVector3& displacement, const LLVector3& cameraOffset, const LLVector3& focusOffset, F32 scale)
    {
        if (!displacement.isFinite()) return;
        const auto view = state(LLVector3::zero, cameraOffset, focusOffset, scale);
        mPan += view.at * displacement.mV[VX] + view.left * displacement.mV[VY] + view.up * displacement.mV[VZ];
        if (mPan.magVecSquared() > 128.f * 128.f) { mPan.normalize(); mPan *= 128.f; }
    }
    void controls(F32 dt, F32 yaw, F32 pitch, F32 zoomIn, const LLVector3& track, F32 rollRate,
                  const LLVector3& cameraOffset, const LLVector3& focusOffset, F32 scale)
    {
        if (!std::isfinite(dt) || dt <= 0) return;
        dt = std::min(dt, .1f);
        // LLAgentCamera's reference rates: 90 degrees/s orbit, 45 roll,
        // five metres/s pan and distance-proportional zoom.
        orbit(yaw * dt * 1.57079632679f, pitch * dt * 1.57079632679f);
        roll(rollRate * dt * .78539816339f);
        if (zoomIn != 0) mDistance = std::clamp(mDistance - zoomIn * mDistance * dt, .25f, 128.f);
        if (track.magVecSquared() > 0) pan(track * (dt * 5.f), cameraOffset, focusOffset, scale);
    }
    bool lookAt(const LLVector3& agent, const LLVector3& eye, const LLVector3& target,
                const LLVector3& cameraOffset, const LLVector3& focusOffset, F32 scale)
    {
        if (!agent.isFinite() || !eye.isFinite() || !target.isFinite()) return false;
        LLVector3 direction = target - eye;
        const F32 distance = direction.normalize();
        const LLVector3 radial = LLVector3(0,0,1) + cameraOffset * scale - focusOffset;
        const F32 radialLength = radial.length();
        if (distance <= .001f || radialLength <= .001f) return false;
        const F32 baseYaw = std::atan2(-radial.mV[VY], -radial.mV[VX]);
        const F32 basePitch = std::atan2(-radial.mV[VZ], std::hypot(radial.mV[VX], radial.mV[VY]));
        mYaw = wrap(std::atan2(direction.mV[VY], direction.mV[VX]) - baseYaw);
        mPitch = std::asin(std::clamp(direction.mV[VZ], -1.f, 1.f)) - basePitch;
        mRoll = 0;
        mDistance = std::clamp(distance / radialLength, .25f, 128.f);
        mPan.clear();
        mPan = target - state(agent, cameraOffset, focusOffset, scale).focus;
        return true;
    }
    void setFlying(bool flying) { mFlying = flying; }
    void face(const LLVector3& direction)
    { if (direction.isFinite() && direction.magVecSquared() > .001f) mYaw = mBodyYaw = std::atan2(direction.mV[VY], direction.mV[VX]); }
    bool action(U32 token, const std::string& name, bool down, bool repeated)
    {
        if (!down) return mHeld.erase(token) != 0;
        if (!supported(name)) return false;
        if (name == "toggle_fly") { if (!repeated && !mHeld.count(token)) mFlying = !mFlying; }
        mHeld[token] = name; return true;
    }
    bool firstPerson() const { return mDistance <= .05f; }
    void orbit(F32 yaw, F32 pitch)
    {
        if (std::isfinite(yaw) && std::isfinite(pitch))
        { mYaw = wrap(mYaw + yaw); mPitch = std::clamp(mPitch + pitch, -1.45f, 1.45f); if (firstPerson()) mBodyYaw = mYaw; }
    }
    void zoom(S32 clicks)
    {
        mDistance = std::clamp(mDistance + F32(clicks) * std::max(.2f, mDistance * .15f), 0.f, 128.f);
        if (firstPerson()) { mPan.clear(); mBodyYaw = mYaw; }
    }
    void step(F32 dt, bool rotationCaptured = false,
              const LLVector3& cameraOffset = LLVector3(0,0,-.2f),
              const LLVector3& focusOffset = LLVector3(1,0,.2f), F32 scale = .75f)
    {
        if (!std::isfinite(dt) || dt <= 0) return;
        dt = std::min(dt, .1f); // A stalled frame cannot produce an unbounded turn.
        const F32 turn      = rotationCaptured ? 0 : axis("turn_left", "turn_right") * dt * 1.5f;
        mBodyYaw = wrap(mBodyYaw + turn); orbit(turn + axis("spin_around_cw", "spin_around_ccw") * dt * 1.5f,
            axis("spin_over", "spin_under") * dt);
        mRoll = wrap(mRoll + axis("roll_left", "roll_right") * dt);
        const F32 zoomDelta = axis("move_backward", "move_forward") * dt * std::max(1.f, mDistance);
        mDistance = std::clamp(mDistance + zoomDelta, 0.f, 128.f);
        if (firstPerson()) { mPan.clear(); mBodyYaw = mYaw; return; }
        auto view = state(LLVector3::zero, cameraOffset, focusOffset, scale);
        mPan += (view.left * axis("pan_left", "pan_right") + view.up * axis("pan_up", "pan_down")) * dt * std::max(1.f, mDistance);
        if (mPan.magVecSquared() > 128.f * 128.f) { mPan.normalize(); mPan *= 128.f; }
    }
    U32 controls() const
    {
        U32 value = mFlying ? AGENT_CONTROL_FLY : 0;
        if (held("stop_moving")) return value | AGENT_CONTROL_STOP;
        if (held("push_forward")) value |= AGENT_CONTROL_AT_POS;
        if (held("push_backward")) value |= AGENT_CONTROL_AT_NEG;
        if (held("slide_left")) value |= AGENT_CONTROL_LEFT_POS;
        if (held("slide_right")) value |= AGENT_CONTROL_LEFT_NEG;
        if (held("jump")) value |= AGENT_CONTROL_UP_POS;
        if (held("push_down")) value |= AGENT_CONTROL_UP_NEG;
        if (held("turn_left")) value |= AGENT_CONTROL_YAW_POS | AGENT_CONTROL_TURN_LEFT;
        if (held("turn_right")) value |= AGENT_CONTROL_YAW_NEG | AGENT_CONTROL_TURN_RIGHT;
        if (firstPerson()) value |= AGENT_CONTROL_MOUSELOOK;
        return value;
    }
    State state(const LLVector3& agent,
                const LLVector3& cameraOffset = LLVector3(0,0,-.2f),
                const LLVector3& focusOffset = LLVector3(1,0,.2f), F32 scale = .75f) const
    {
        // Reference rear view: eye offset is relative to the avatar/head frame,
        // and focus offset is relative to the avatar root, with X forward.
        const LLVector3 radial = LLVector3(0,0,1) + cameraOffset * scale - focusOffset;
        const F32 yaw = std::atan2(-radial.mV[VY], -radial.mV[VX]) + mYaw;
        const F32 pitch = std::clamp(std::atan2(-radial.mV[VZ],
            std::hypot(radial.mV[VX], radial.mV[VY])) + mPitch, -1.45f, 1.45f);
        const F32 c = std::cos(pitch), s = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
        State result;
        result.at = LLVector3(c * cy, c * sy, s);
        LLVector3 left(-sy, cy, 0), up(-s * cy, -s * sy, c);
        result.left = left * std::cos(mRoll) + up * std::sin(mRoll);
        result.up = up * std::cos(mRoll) - left * std::sin(mRoll);
        const F32 bodyCos = std::cos(mBodyYaw), bodySin = std::sin(mBodyYaw);
        const LLVector3 focus(focusOffset.mV[VX] * bodyCos - focusOffset.mV[VY] * bodySin,
                             focusOffset.mV[VX] * bodySin + focusOffset.mV[VY] * bodyCos, focusOffset.mV[VZ]);
        const F32 distance = std::clamp(std::sqrt(radial.magVecSquared()) * mDistance, .1f, 128.f);
        result.focus = agent + focus + mPan;
        result.center = firstPerson() ? agent + LLVector3(0,0,1) : agent + focus + mPan - result.at * distance;
        result.bodyRotation = LLQuaternion(mBodyYaw, LLVector3::z_axis);
        result.headRotation = LLQuaternion(-pitch, LLVector3::y_axis) * LLQuaternion(yaw, LLVector3::z_axis);
        result.firstPerson = firstPerson(); return result;
    }
private:
    static bool supported(const std::string& name)
    {
        return name == "push_forward" || name == "push_backward" || name == "slide_left" || name == "slide_right" ||
            name == "jump" || name == "push_down" || name == "turn_left" || name == "turn_right" || name == "toggle_fly" ||
            name == "stop_moving" || name == "spin_around_cw" || name == "spin_around_ccw" || name == "spin_over" ||
            name == "spin_under" || name == "move_forward" || name == "move_backward" || name == "pan_left" ||
            name == "pan_right" || name == "pan_up" || name == "pan_down" || name == "roll_left" || name == "roll_right";
    }
    bool held(const char* action) const { for (const auto& entry : mHeld) if (entry.second == action) return true; return false; }
    F32 axis(const char* positive, const char* negative) const { return F32(held(positive)) - F32(held(negative)); }
    static F32 wrap(F32 angle) { return std::remainder(angle, 6.28318530718f); }
    std::map<U32, std::string> mHeld;
    F32 mYaw = 0, mBodyYaw = 0, mPitch = 0, mRoll = 0, mDistance = 1.f;
    LLVector3 mPan;
    bool mFlying = false;
};

// CPU motion ownership, independent of a drawable. LGPL-2.1.
#pragma once
#include "v3math.h"
#include "llquaternion.h"
#include <algorithm>
#include <cmath>

class VSWorldMotion
{
public:
    struct Policy { bool interpolate = true; double phaseOut = 1, maximum = 3; };
    bool accept(const LLVector3& p, const LLQuaternion& q, const LLVector3& v,
                const LLVector3& a, const LLVector3& angular, double time)
    {
        if (!p.isFinite() || !q.isFinite() || !v.isFinite() || !a.isFinite() ||
            !angular.isFinite() || !std::isfinite(time) || (mKnown && time < mReceived)) return false;
        if (!mKnown || angular != mAngular) mAngularDelta.loadIdentity();
        mAuthoritative = p; mPosition = p; mRotation = q * mAngularDelta;
        mBaseRotation = q; mVelocity = v; mAcceleration = a; mAngular = angular;
        mReceived = mEvaluated = time; mKnown = true; return true;
    }
    bool update(double time, float dilation, bool circuitHealthy, bool attached, const Policy& policy)
    {
        if (!mKnown || !std::isfinite(time) || !std::isfinite(dilation) || time <= mEvaluated) return false;
        const double elapsed = time - mEvaluated;
        mEvaluated = time;
        if (mSelected || !policy.interpolate) return false;
        const float dt = float(std::min(elapsed, .2)) * std::clamp(dilation, 0.f, 1.f);
        if (dt <= 0) return false;
        LLVector3 position = mPosition;
        LLQuaternion rotation = mRotation;
        const float omega = mAngular.length();
        if (omega > .00001f)
        {
            LLQuaternion delta; delta.setQuat(omega * dt, mAngular / omega);
            mAngularDelta = mAngularDelta * delta; mRotation = mBaseRotation * mAngularDelta;
            mRotation.normalize();
        }
        float phase = 1;
        const double age = time - mReceived;
        if (!circuitHealthy && policy.maximum > 0 && age > policy.phaseOut)
            phase = policy.maximum <= policy.phaseOut ? 0.f :
                float(std::clamp((policy.maximum - age) / (policy.maximum - policy.phaseOut), 0.0, 1.0));
        if (!attached)
        {
            // Simulator velocity is the previous physics step's average, as in
            // LLViewerObject::interpolateLinearMotion; retain that correction.
            const auto predicted = mPosition + (mVelocity + mAcceleration * (.5f * (dt - 1.f/45.f))) * (dt * phase);
            if (predicted.isFinite()) { mPosition = predicted; mVelocity += mAcceleration * (dt * phase); }
        }
        return position != mPosition || rotation != mRotation;
    }
    // Local editor mutation does not invent a simulator update timestamp.
    void previewPosition(const LLVector3& position){mPosition=position;}
    void previewRotation(const LLQuaternion& rotation){mBaseRotation=mRotation=rotation;mAngularDelta.loadIdentity();}
    void freeze(bool selected) { mSelected = selected; }
    void setSelectionState(bool selected) {mSelected=selected;resetSelectionRotation();}
    void resetSelectionRotation(){mAngularDelta.loadIdentity();mBaseRotation=mRotation;}
    void initializeSelection()
    {
        mVelocity=LLVector3();mAcceleration=LLVector3();
        // Preserve the displayed pose when clearing accumulated angular motion.
        resetSelectionRotation();
    }
    void initializeDeselection(){mAngular=LLVector3();mVelocity=LLVector3();}
    bool selected() const { return mSelected; }
    const LLVector3& position() const { return mPosition; }
    const LLVector3& authoritative() const { return mAuthoritative; }
    const LLQuaternion& rotation() const { return mRotation; }
    const LLVector3& velocity() const { return mVelocity; }
    double timestamp() const { return mReceived; }
private:
    bool mKnown = false, mSelected = false;
    double mReceived = 0, mEvaluated = 0;
    LLVector3 mAuthoritative, mPosition, mVelocity, mAcceleration, mAngular;
    LLQuaternion mBaseRotation, mRotation, mAngularDelta;
};

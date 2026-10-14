/**
 * @file vsflexiblepath.cpp
 * @brief Flexible object implementation
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "vsflexiblepath.h"

void vs_flexible_remap(VSFlexibleSection* source, S32 source_sections, VSFlexibleSection* dest, S32 dest_sections, const LLVector3& scale)
{
    if (!source || !dest || source_sections < -1 || source_sections > FLEXIBLE_OBJECT_MAX_SECTIONS || dest_sections < 0 ||
        dest_sections > FLEXIBLE_OBJECT_MAX_SECTIONS)
        return;
    S32 num_output_sections   = 1 << dest_sections;
    F32 source_section_length = source_sections < 0 ? 0 : scale.mV[VZ] / (F32)(1 << source_sections);
    F32 section_length        = scale.mV[VZ] / (F32)num_output_sections;
    if (source_sections == -1)
    {
        // Generate all from section 0
        dest[0] = source[0];
        for (S32 section = 0; section < num_output_sections; ++section)
        {
            dest[section + 1] = dest[section];
            dest[section + 1].mPosition += dest[section].mDirection * section_length;
            dest[section + 1].mVelocity.setVec(LLVector3::zero);
        }
    }
    else if (source_sections > dest_sections)
    {
        // Copy, skipping sections

        S32 num_steps = 1 << (source_sections - dest_sections);

        // Copy from left to right since it may be an in-place computation
        for (S32 section = 0; section < num_output_sections; ++section)
        {
            dest[section + 1] = source[(section + 1) * num_steps];
        }
        dest[0] = source[0];
    }
    else if (source_sections < dest_sections)
    {
        // Interpolate section info
        // Iterate from right to left since it may be an in-place computation
        S32 step_shift = dest_sections - source_sections;
        S32 num_steps  = 1 << step_shift;
        for (S32 section = num_output_sections - num_steps; section >= 0; section -= num_steps)
        {
            VSFlexibleSection* last_source_section = &source[section >> step_shift];
            VSFlexibleSection* source_section      = &source[(section >> step_shift) + 1];

            // Cubic interpolation of position
            // At^3 + Bt^2 + Ct + D = f(t)
            LLVector3 D = last_source_section->mPosition;
            LLVector3 C = last_source_section->mdPosition * source_section_length;
            LLVector3 Y = source_section->mdPosition * source_section_length - C; // Helper var
            LLVector3 X = (source_section->mPosition - D - C);                    // Helper var
            LLVector3 A = Y - 2 * X;
            LLVector3 B = X - A;

            F32 t_inc = 1.f / F32(num_steps);
            F32 t     = t_inc;
            for (S32 step = 1; step < num_steps; ++step)
            {
                dest[section + step].mScale        = lerp(last_source_section->mScale, source_section->mScale, t);
                dest[section + step].mAxisRotation = slerp(t, last_source_section->mAxisRotation, source_section->mAxisRotation);

                // Evaluate output interpolated values
                F32 t_sq                        = t * t;
                dest[section + step].mPosition  = t_sq * (t * A + B) + t * C + D;
                dest[section + step].mRotation  = slerp(t, last_source_section->mRotation, source_section->mRotation);
                dest[section + step].mVelocity  = lerp(last_source_section->mVelocity, source_section->mVelocity, t);
                dest[section + step].mDirection = lerp(last_source_section->mDirection, source_section->mDirection, t);
                dest[section + step].mdPosition = lerp(last_source_section->mdPosition, source_section->mdPosition, t);
                dest[section + num_steps]       = *source_section;
                t += t_inc;
            }
        }
        dest[0] = source[0];
    }
    else
    {
        // numbers are equal. copy info
        for (S32 section = 0; section <= num_output_sections; ++section)
        {
            dest[section] = source[section];
        }
    }
}

void vs_flexible_parameters(VSFlexibleSection*  mSection,
                            S32&                mInitializedRes,
                            S32                 mSimulateRes,
                            const LLPathParams& params,
                            const LLVector3&    scale,
                            const LLVector3&    position,
                            const LLQuaternion& rotation)
{
    if (!mSection || mSimulateRes < 0 || mSimulateRes > FLEXIBLE_OBJECT_MAX_SECTIONS || mInitializedRes < -1 ||
        mInitializedRes > FLEXIBLE_OBJECT_MAX_SECTIONS || !scale.isFinite() || scale.mV[VX] <= 0 || scale.mV[VY] <= 0 ||
        scale.mV[VZ] <= 0 || !position.isFinite() || !rotation.isFinite())
        return;
    const LLVector2 bottom_scale = params.getBeginScale(), top_scale = params.getEndScale();
    const F32       begin_rot = F_PI * params.getTwistBegin(), end_rot = F_PI * params.getTwist();
    const S32       num_sections = 1 << mSimulateRes;
    mSection[0].mPosition        = position - (.5f * scale.mV[VZ]) * (LLVector3::z_axis * rotation);
    mSection[0].mDirection       = LLVector3::z_axis * rotation;
    mSection[0].mdPosition       = mSection[0].mDirection;
    mSection[0].mScale.setVec(scale.mV[VX] * bottom_scale.mV[0], scale.mV[VY] * bottom_scale.mV[1]);
    mSection[0].mVelocity.setVec(0, 0, 0);
    mSection[0].mAxisRotation.setQuat(begin_rot, 0, 0, 1);

    vs_flexible_remap(mSection, mInitializedRes, mSection, mSimulateRes, scale);
    mInitializedRes = mSimulateRes;

    F32 t_inc = 1.f / F32(num_sections);
    F32 t     = t_inc;

    for (int i = 1; i <= num_sections; i++)
    {
        mSection[i].mAxisRotation.setQuat(lerp(begin_rot, end_rot, t), 0, 0, 1);
        mSection[i].mScale = LLVector2(scale.mV[VX] * lerp(bottom_scale.mV[0], top_scale.mV[0], t),
                                       scale.mV[VY] * lerp(bottom_scale.mV[1], top_scale.mV[1], t));
        t += t_inc;
    }
}

VSFlexibleResult vs_flexible_step(VSFlexibleSection*          mSection,
                                  S32                         mSimulateRes,
                                  S32                         mRenderRes,
                                  LLVolume&                   volume,
                                  const LLFlexibleObjectData& attributes,
                                  const LLVector3&            position,
                                  const LLQuaternion&         rotation,
                                  const LLVector3&            scale,
                                  F32                         seconds,
                                  F32                         distance,
                                  const VSFlexibleWind&       wind,
                                  bool                        mUpdated,
                                  LLQuaternion&               mLastSegmentRotation)
{
    VSFlexibleResult result;
    if (!mSection || !wind || mSimulateRes < 0 || mSimulateRes > FLEXIBLE_OBJECT_MAX_SECTIONS || mRenderRes < 0 ||
        mRenderRes > FLEXIBLE_OBJECT_MAX_SECTIONS || !scale.isFinite() || scale.mV[VX] <= 0 || scale.mV[VY] <= 0 || scale.mV[VZ] <= 0 ||
        !position.isFinite() || !rotation.isFinite() || !std::isfinite(seconds) || !std::isfinite(distance))
        return result;
    distance             = llmax(distance, .01f);
    LLPath* path         = &volume.getPath();
    S32     num_sections = 1 << mSimulateRes;

    F32 secondsThisFrame = llclamp(seconds, 0.f, .2f);
    if (secondsThisFrame > 0.2f)
    {
        secondsThisFrame = 0.2f;
    }

    LLVector3    BasePosition           = position;
    LLQuaternion BaseRotation           = rotation;
    LLQuaternion parentSegmentRotation  = BaseRotation;
    LLVector3    anchorDirectionRotated = LLVector3::z_axis * parentSegmentRotation;
    LLVector3    anchorScale            = scale;

    F32 section_length     = anchorScale.mV[VZ] / (F32)num_sections;
    F32 inv_section_length = 1.f / section_length;

    S32 i;

    // ANCHOR position is offset from BASE position (centroid) by half the length
    LLVector3 AnchorPosition = BasePosition - (anchorScale.mV[VZ] / 2 * anchorDirectionRotated);

    mSection[0].mPosition  = AnchorPosition;
    mSection[0].mDirection = anchorDirectionRotated;
    mSection[0].mRotation  = BaseRotation;

    LLQuaternion deltaRotation;

    LLVector3 lastPosition;

    // Coefficients which are constant across sections
    F32 t_factor = attributes.getTension() * 0.1f;
    t_factor     = t_factor * (1 - pow(0.85f, secondsThisFrame * 30));
    if (t_factor > FLEXIBLE_OBJECT_MAX_INTERNAL_TENSION_FORCE)
    {
        t_factor = FLEXIBLE_OBJECT_MAX_INTERNAL_TENSION_FORCE;
    }

    F32 friction_coeff = (attributes.getAirFriction() * 2 + 1);
    friction_coeff     = pow(10.f, friction_coeff * secondsThisFrame);
    friction_coeff     = (friction_coeff > 1) ? friction_coeff : 1;
    F32 momentum       = 1.0f / friction_coeff;

    F32 wind_factor = (attributes.getWindSensitivity() * 0.1f) * section_length * secondsThisFrame;
    F32 max_angle   = atan(section_length * 2.f);

    F32 force_factor = section_length * secondsThisFrame;

    // Update simulated sections
    for (i = 1; i <= num_sections; ++i)
    {
        LLVector3 parentSectionVector;
        LLVector3 parentSectionPosition;
        LLVector3 parentDirection;

        //---------------------------------------------------
        // save value of position as lastPosition
        //---------------------------------------------------
        lastPosition = mSection[i].mPosition;

        //------------------------------------------------------------------------------------------
        // gravity
        //------------------------------------------------------------------------------------------
        mSection[i].mPosition.mV[2] -= attributes.getGravity() * force_factor;

        //------------------------------------------------------------------------------------------
        // wind force
        //------------------------------------------------------------------------------------------
        if (attributes.getWindSensitivity() > 0.001f)
        {
            mSection[i].mPosition += wind(mSection[i].mPosition) * wind_factor;
        }

        //------------------------------------------------------------------------------------------
        // user-defined force
        //------------------------------------------------------------------------------------------
        mSection[i].mPosition += attributes.getUserForce() * force_factor;

        //---------------------------------------------------
        // tension (rigidity, stiffness)
        //---------------------------------------------------
        parentSectionPosition = mSection[i - 1].mPosition;
        parentDirection       = mSection[i - 1].mDirection;

        if (i == 1)
        {
            parentSectionVector = mSection[0].mDirection;
        }
        else
        {
            parentSectionVector = mSection[i - 2].mDirection;
        }

        LLVector3 currentVector = mSection[i].mPosition - parentSectionPosition;

        LLVector3 difference   = (parentSectionVector * section_length) - currentVector;
        LLVector3 tensionForce = difference * t_factor;

        mSection[i].mPosition += tensionForce;

        //------------------------------------------------------------------------------------------
        // sphere collision, currently not used
        //------------------------------------------------------------------------------------------
        /*if ( attributes.mUsingCollisionSphere )
        {
            LLVector3 vectorToCenterOfCollisionSphere = mCollisionSpherePosition - mSection[i].mPosition;
            if ( vectorToCenterOfCollisionSphere.magVecSquared() < mCollisionSphereRadius * mCollisionSphereRadius )
            {
                F32 distanceToCenterOfCollisionSphere = vectorToCenterOfCollisionSphere.magVec();
                F32 penetration = mCollisionSphereRadius - distanceToCenterOfCollisionSphere;

                LLVector3 normalToCenterOfCollisionSphere;

                if ( distanceToCenterOfCollisionSphere > 0.0f )
                {
                    normalToCenterOfCollisionSphere = vectorToCenterOfCollisionSphere / distanceToCenterOfCollisionSphere;
                }
                else // rare
                {
                    normalToCenterOfCollisionSphere = LLVector3::x_axis; // arbitrary
                }

                // push the position out to the surface of the collision sphere
                mSection[i].mPosition -= normalToCenterOfCollisionSphere * penetration;
            }
        }*/

        //------------------------------------------------------------------------------------------
        // inertia
        //------------------------------------------------------------------------------------------
        mSection[i].mPosition += mSection[i].mVelocity * momentum;

        //------------------------------------------------------------------------------------------
        // clamp length & rotation
        //------------------------------------------------------------------------------------------
        mSection[i].mDirection = mSection[i].mPosition - parentSectionPosition;
        mSection[i].mDirection.normVec();
        deltaRotation.shortestArc(parentDirection, mSection[i].mDirection);

        F32       angle;
        LLVector3 axis;
        deltaRotation.getAngleAxis(&angle, axis);
        if (angle > F_PI)
            angle -= 2.f * F_PI;
        if (angle < -F_PI)
            angle += 2.f * F_PI;
        if (angle > max_angle)
        {
            // angle = 0.5f*(angle+max_angle);
            deltaRotation.setQuat(max_angle, axis);
        }
        else if (angle < -max_angle)
        {
            // angle = 0.5f*(angle-max_angle);
            deltaRotation.setQuat(-max_angle, axis);
        }
        LLQuaternion segment_rotation = parentSegmentRotation * deltaRotation;
        parentSegmentRotation         = segment_rotation;

        mSection[i].mDirection = (parentDirection * deltaRotation);
        mSection[i].mPosition  = parentSectionPosition + mSection[i].mDirection * section_length;
        mSection[i].mRotation  = segment_rotation;

        if (i > 1)
        {
            // Propogate half the rotation up to the parent
            LLQuaternion halfDeltaRotation(angle / 2, axis);
            mSection[i - 1].mRotation = mSection[i - 1].mRotation * halfDeltaRotation;
        }

        //------------------------------------------------------------------------------------------
        // calculate velocity
        //------------------------------------------------------------------------------------------
        mSection[i].mVelocity = mSection[i].mPosition - lastPosition;
        if (mSection[i].mVelocity.magVecSquared() > 1.f)
        {
            mSection[i].mVelocity.normVec();
        }
    }

    // Calculate derivatives (not necessary until normals are automagically generated)
    mSection[0].mdPosition = (mSection[1].mPosition - mSection[0].mPosition) * inv_section_length;
    // i = 1..NumSections-1
    for (i = 1; i < num_sections; ++i)
    {
        // Quadratic numerical derivative of position

        // f(-L1) = aL1^2 - bL1 + c = f1
        // f(0)   =               c = f2
        // f(L2)  = aL2^2 + bL2 + c = f3
        // f = ax^2 + bx + c
        // d/dx f = 2ax + b
        // d/dx f(0) = b

        // c = f2
        // a = [(f1-c)/L1 + (f3-c)/L2] / (L1+L2)
        // b = (f3-c-aL2^2)/L2

        LLVector3 a = (mSection[i - 1].mPosition - mSection[i].mPosition + mSection[i + 1].mPosition - mSection[i].mPosition) * 0.5f *
                      inv_section_length * inv_section_length;
        LLVector3 b = (mSection[i + 1].mPosition - mSection[i].mPosition - a * (section_length * section_length));
        b *= inv_section_length;

        mSection[i].mdPosition = b;
    }

    // i = NumSections
    mSection[i].mdPosition = (mSection[i].mPosition - mSection[i - 1].mPosition) * inv_section_length;

    // Create points
    llassert(mRenderRes > -1);
    S32 num_render_sections = 1 << mRenderRes;
    if (path->getPathLength() != num_render_sections + 1)
    {
        result.topologyChanged = true;
        volume.resizePath(num_render_sections + 1);
    }

    LLPath::PathPt* new_point;

    VSFlexibleSection newSection[(1 << FLEXIBLE_OBJECT_MAX_SECTIONS) + 1];
    vs_flexible_remap(mSection, mSimulateRes, newSection, mRenderRes, scale);

    // generate transform from global to prim space
    LLVector3    delta_scale = LLVector3(1, 1, 1);
    LLVector3    delta_pos;
    LLQuaternion delta_rot;

    delta_rot = ~rotation;
    delta_pos = -position * delta_rot;

    // Vertex transform (4x4)
    LLVector3 x_axis = LLVector3(delta_scale.mV[VX], 0.f, 0.f) * delta_rot;
    LLVector3 y_axis = LLVector3(0.f, delta_scale.mV[VY], 0.f) * delta_rot;
    LLVector3 z_axis = LLVector3(0.f, 0.f, delta_scale.mV[VZ]) * delta_rot;

    LLMatrix4 rel_xform;
    rel_xform.initRows(LLVector4(x_axis, 0.f), LLVector4(y_axis, 0.f), LLVector4(z_axis, 0.f), LLVector4(delta_pos, 1.f));

    LL_CHECK_MEMORY
    for (i = 0; i <= num_render_sections; ++i)
    {
        new_point        = &path->mPath[i];
        LLVector3    pos = newSection[i].mPosition * rel_xform;
        LLQuaternion rot = newSection[i].mAxisRotation * newSection[i].mRotation * delta_rot;

        LLVector3 np(new_point->mPos.getF32ptr());

        if (!mUpdated || (np - pos).magVec() / distance > 0.001f)
        {
            new_point->mPos.load3((newSection[i].mPosition * rel_xform).mV);
            mUpdated                = false;
            result.positionsChanged = true;
        }

        new_point->mRot.loadu(LLMatrix3(rot));
        new_point->mScale.set(newSection[i].mScale.mV[0], newSection[i].mScale.mV[1], 0, 1);
        new_point->mTexT = ((F32)i) / (num_render_sections);
    }
    LL_CHECK_MEMORY
    mLastSegmentRotation = parentSegmentRotation;
    return result;
}

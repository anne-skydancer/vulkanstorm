// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The arithmetic of LLFace's pixel area, apart from the face: multi-core stage C8, the texture list's pixel-area pass
// as the first user of LL::FrameJobs (doc/perf/multicore-simd-study.md, "C8 as built").
//
// LLFace::computePixelArea gathers a face's box and calls project(); it writes a Result and nothing else, so the texture
// list can run it for many faces at once on frame job helpers. LLFace::applyPixelArea then writes the Result to the
// face on the main thread, in the order of the serial loop, and draws the update jitter (ll_frand) there. The serial path
// (LLFace::calcPixelArea) is the same two calls, so a face computed on a helper and a face computed on the main thread
// get the same bits.
//
// Everything here is a function of its arguments: no globals, no random numbers, no writes except to the Result.
// It needs llmath only; tests/llfacepixelarea_test.cpp checks it against the code it replaces, bit for bit.
#ifndef LL_LLFACEPIXELAREA_H
#define LL_LLFACEPIXELAREA_H

#include "llcamera.h"
#include "llmath.h"
#include "llmatrix4a.h"
#include "llvector4a.h"

#include <cstring>

namespace LLFacePixelArea
{
    // What LLFace::calcPixelArea reads outside the face, taken once for a batch.
    struct View
    {
        LLVector4a origin;                   // the camera's origin (load3: w is 0)
        LLVector4a x_axis;                   // its view direction (load3)
        LLCamera*  camera = nullptr;         // for AABBInFrustum (media faces), which only reads the camera
        F32        cos_half_fov = 0.f;
        F32        average_speed = 0.f;
        F32        average_angular_speed = 0.f;
        F32        pixel_angle = 0.f;        // LLDrawable::sCurPixelAngle
        F32        frame_time = 0.f;         // gFrameTimeSeconds
    };

    enum EStatus : U8
    {
        CACHED,       // inside the update period: the face's last outputs stand, nothing is written
        NO_EXTENTS,   // a rigged face without a joint box: zero box, not in the frustum, no jitter drawn, period not armed
        COMPUTED,     // the values below; the jitter is drawn when they are applied
        NEEDS_SERIAL  // read-only mode met state it would have to write (rigging info, a dirty joint matrix): nothing computed
    };

    struct Result
    {
        LLVector4a rigged_extents[2];  // COMPUTED and rigged: the world box of the face's joints
        F32        pixel_area;
        F32        importance;
        F32        cos_angle_to_view_dir;
        F32        radius;
        EStatus    status;
        bool       rigged;             // rigged_extents is to be stored
        bool       in_frustum;         // the result of calcPixelArea

        void clear(EStatus s)
        {
            rigged_extents[0].clear();
            rigged_extents[1].clear();
            pixel_area = 0.f;
            importance = 0.f;
            cos_angle_to_view_dir = 0.f;
            radius = 0.f;
            status = s;
            rigged = false;
            in_frustum = false;
        }
    };

    // Every field bit for bit (NaNs included), for the compare mode and the tests.
    inline bool identical(const Result& a, const Result& b)
    {
        return a.status == b.status && a.rigged == b.rigged && a.in_frustum == b.in_frustum
            && std::memcmp(a.rigged_extents, b.rigged_extents, sizeof(a.rigged_extents)) == 0
            && std::memcmp(&a.pixel_area, &b.pixel_area, sizeof(F32)) == 0
            && std::memcmp(&a.importance, &b.importance, sizeof(F32)) == 0
            && std::memcmp(&a.cos_angle_to_view_dir, &b.cos_angle_to_view_dir, sizeof(F32)) == 0
            && std::memcmp(&a.radius, &b.radius, sizeof(F32)) == 0;
    }

    constexpr S32 IMPORTANCE_LEVELS = 4;
    // {distance, importance weight}
    constexpr F32 IMPORTANCE_OVER_DISTANCE[IMPORTANCE_LEVELS][2] =
        {{16.1f, 1.0f}, {32.1f, 0.5f}, {48.1f, 0.2f}, {96.1f, 0.05f}};
    // {cos(angle), importance weight}
    constexpr F32 IMPORTANCE_OVER_ANGLE[IMPORTANCE_LEVELS][2] =
        {{0.985f /*cos(10 degrees)*/, 1.0f}, {0.94f /*cos(20 degrees)*/, 0.8f}, {0.866f /*cos(30 degrees)*/, 0.64f}, {0.0f, 0.36f}};

    // LLFace::calcImportanceToCamera.
    inline F32 importanceToCamera(F32 cos_angle_to_view_dir, F32 dist, F32 cos_half_fov, F32 camera_moving_speed,
                                  F32 camera_angular_speed)
    {
        F32 importance = 0.f;

        if (cos_angle_to_view_dir > cos_half_fov && dist < IMPORTANCE_OVER_DISTANCE[IMPORTANCE_LEVELS - 1][0])
        {
            if (camera_moving_speed > 10.0f || camera_angular_speed > 1.0f)
            {
                //if camera moves or rotates too fast, ignore the importance factor
                return 0.f;
            }

            S32 i = 0;
            for (i = 0; i < IMPORTANCE_LEVELS && dist > IMPORTANCE_OVER_DISTANCE[i][0]; ++i);
            i = llmin(i, IMPORTANCE_LEVELS - 1);
            F32 dist_factor = IMPORTANCE_OVER_DISTANCE[i][1];

            for (i = 0; i < IMPORTANCE_LEVELS && cos_angle_to_view_dir < IMPORTANCE_OVER_ANGLE[i][0]; ++i);
            i = llmin(i, IMPORTANCE_LEVELS - 1);
            importance = dist_factor * IMPORTANCE_OVER_ANGLE[i][1];
        }

        return importance;
    }

    // One joint of a rigged face: its box in joint space, taken to world space and added to the face's box.
    inline void addJointBox(const LLMatrix4a& world, const LLVector4a* joint_extents, LLVector4a& minp, LLVector4a& maxp)
    {
        LLVector4a extents[2];

        matMulBoundBox(world, joint_extents, extents);

        minp.setMin(minp, extents[0]);
        maxp.setMax(maxp, extents[1]);
    }

    // The area of the circle around a box as the camera sees it, the box's angle to the view direction and its importance.
    // `size` is the box's half size. Fills pixel_area, importance, cos_angle_to_view_dir, radius and in_frustum of a
    // COMPUTED result; a media face out of the frustum has no importance and is not in the frustum.
    inline void project(const View& view, const LLVector4a& center, const LLVector4a& size, bool has_media,
                        F32 bounding_sphere_radius, Result& result)
    {
        F32 size_squared = size.dot3(size).getF32();
        LLVector4a lookAt;
        lookAt.setSub(center, view.origin);

        F32 dist = lookAt.getLength3().getF32();
        dist = llmax(dist-size.getLength3().getF32(), 0.001f);

        lookAt.normalize3fast() ;

        //get area of circle around node
        F32 app_angle = atanf((F32) sqrt(size_squared) / dist);
        F32 radius = app_angle*view.pixel_angle;
        result.radius = radius;
        result.pixel_area = radius*radius * 3.14159f;

        F32 cos_angle_to_view_dir = lookAt.dot3(view.x_axis).getF32();

        //if has media, check if the face is out of the view frustum.
        if (has_media)
        {
            if (!view.camera->AABBInFrustum(center, size))
            {
                result.importance = 0.f;
                result.cos_angle_to_view_dir = cos_angle_to_view_dir;
                result.in_frustum = false;
                return;
            }
            if (cos_angle_to_view_dir > view.cos_half_fov) //the center is within the view frustum
            {
                cos_angle_to_view_dir = 1.0f;
            }
            else
            {
                LLVector4a d;
                d.setSub(lookAt, view.x_axis);

                if (dist * dist * d.dot3(d) < size_squared)
                {
                    cos_angle_to_view_dir = 1.0f;
                }
            }
        }

        if (dist < bounding_sphere_radius) //camera is very close
        {
            cos_angle_to_view_dir = 1.0f;
            result.importance = 1.0f;
        }
        else
        {
            result.importance = importanceToCamera(cos_angle_to_view_dir, dist, view.cos_half_fov, view.average_speed,
                                                   view.average_angular_speed);
        }

        result.cos_angle_to_view_dir = cos_angle_to_view_dir;
        result.in_frustum = true;
    }
} // namespace LLFacePixelArea

#endif // LL_LLFACEPIXELAREA_H

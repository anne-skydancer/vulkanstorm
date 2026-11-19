// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Tests for llfacepixelarea.h and for the shape of multi-core stage C8, the texture list's pixel-area pass on
// LL::FrameJobs (doc/perf/multicore-simd-study.md, "C8 as built"). No viewer objects: a model face holds what LLFace
// holds for its pixel area, and the model's compute() and apply() follow LLFace::computePixelArea and
// LLFace::applyPixelArea line by line around the same llfacepixelarea.h calls.
//   1  compute() then apply() against the former LLFace::calcPixelArea, transcribed here as it was before the split:
//      every field of the face and every random number drawn, bit for bit, over several frames (the update period, the
//      rigged face without a joint box, the media face in and out of the frustum, the camera close by, a fast camera)
//   2  the pass at a fixed count: the values computed ahead on the helpers (0 to 7 of them, serial mode, several grains)
//      and stored in the loop's order, with the faces that need the main thread measured there, against the serial pass
//      over the same faces: every field and the generator's state, bit for bit, frame after frame
//   3  deliberate breaks, which the comparison of test 2 must catch: a value stored on another face, a number drawn in
//      the job's order instead of the loop's
//   4  opt-in bench (LL_FACE_PIXEL_AREA_BENCH=1): the batch at 0 to 7 helpers, and beside four busy threads at normal and
//      at below-normal priority (the decode pool during a settle)

#include "linden_common.h"

#include "../llfacepixelarea.h"

#include "llframejobs.h"
#include "llcamera.h"
#include "llmatrix4a.h"
#include "llvector4a.h"
#include "v3math.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if LL_WINDOWS
#include "llwin32headers.h"
#endif

#include "lltut.h"

using LL::FrameJobs;

namespace
{
    struct Started
    {
        explicit Started(size_t helpers) { FrameJobs::start(helpers); }
        ~Started()
        {
            FrameJobs::setSerial(false);
            FrameJobs::stop();
        }
    };

    // A small deterministic generator: the test's data, and the stand-in for ll_frand (whose generator cannot be seeded).
    struct Lcg
    {
        uint32_t mState;
        explicit Lcg(uint32_t seed) : mState(seed) {}
        uint32_t next()
        {
            mState = mState * 1664525u + 1013904223u;
            return mState >> 8;
        }
        F32 unit() { return (F32)next() / 16777216.f; }           // [0, 1)
        F32 range(F32 lo, F32 hi) { return lo + (hi - lo) * unit(); }
    };

    struct Joint
    {
        LLMatrix4a mWorld;
        LLVector4a mBox[2];
        bool       mRiggedTo = false;
        bool       mDirty = false;   // LLJoint::isWorldMatrixDirty
    };

    // What LLFace holds for its pixel area.
    struct Face
    {
        // read
        LLVector4a mCenter;          // getPositionAgent
        LLVector4a mExtents[2];
        U32        mFirstJoint = 0;
        U32        mNumJoints = 0;   // the rigging table's size
        F32        mBoundingSphereRadius = 0.f;
        bool       mRigged = false;
        bool       mHasMedia = false;
        bool       mNeedsRigUpdate = false; // LLJointRiggingInfoTab::needsUpdate
        // written
        LLVector4a mRiggedExtents[2];
        F32        mPixelArea = 0.f;
        F32        mImportanceToCamera = 0.f;
        F32        mLastPixelAreaUpdate = 0.f;
        F32        mLastCosAngleToViewDir = 1.f;
        F32        mLastPixelRadius = 0.f;
        bool       mLastInFrustum = true;
        // the caller's (updateImageDecodePriority): the result and both outputs
        bool       mInFrustum = false;
        F32        mOutCos = -2.f;
        F32        mOutRadius = -2.f;
    };

    bool sameWrites(const Face& a, const Face& b)
    {
        return std::memcmp(a.mRiggedExtents, b.mRiggedExtents, sizeof(a.mRiggedExtents)) == 0
            && std::memcmp(&a.mPixelArea, &b.mPixelArea, sizeof(F32)) == 0
            && std::memcmp(&a.mImportanceToCamera, &b.mImportanceToCamera, sizeof(F32)) == 0
            && std::memcmp(&a.mLastPixelAreaUpdate, &b.mLastPixelAreaUpdate, sizeof(F32)) == 0
            && std::memcmp(&a.mLastCosAngleToViewDir, &b.mLastCosAngleToViewDir, sizeof(F32)) == 0
            && std::memcmp(&a.mLastPixelRadius, &b.mLastPixelRadius, sizeof(F32)) == 0
            && a.mLastInFrustum == b.mLastInFrustum
            && a.mInFrustum == b.mInFrustum
            && std::memcmp(&a.mOutCos, &b.mOutCos, sizeof(F32)) == 0
            && std::memcmp(&a.mOutRadius, &b.mOutRadius, sizeof(F32)) == 0;
    }

    struct Scene
    {
        LLCamera           mCamera;
        F32                mCosHalfFov = 0.f;
        F32                mAverageSpeed = 0.f;
        F32                mAverageAngularSpeed = 0.f;
        F32                mPixelAngle = 0.f;
        F32                mFrameTime = 0.f;
        std::vector<Joint> mJoints;
        std::vector<Face>  mFaces;

        LLFacePixelArea::View view()
        {
            // LLFace::pixelAreaView
            LLFacePixelArea::View v;
            v.origin.load3(mCamera.getOrigin().mV);
            v.x_axis.load3(mCamera.getXAxis().mV);
            v.camera = &mCamera;
            v.cos_half_fov = mCosHalfFov;
            v.average_speed = mAverageSpeed;
            v.average_angular_speed = mAverageAngularSpeed;
            v.pixel_angle = mPixelAngle;
            v.frame_time = mFrameTime;
            return v;
        }
    };

    // --- The former code --------------------------------------------------------------------------------------------
    // LLFace::calcPixelArea and LLFace::calcImportanceToCamera as they were before the split (vkstorm-devel 9d8388e7e0),
    // on the model face: the statements and their order are the original's.
    constexpr F32 PIXEL_AREA_UPDATE_PERIOD = 0.1f;

    const S8 FACE_IMPORTANCE_LEVEL = 4 ;
    const F32 FACE_IMPORTANCE_TO_CAMERA_OVER_DISTANCE[FACE_IMPORTANCE_LEVEL][2] = //{distance, importance_weight}
        {{16.1f, 1.0f}, {32.1f, 0.5f}, {48.1f, 0.2f}, {96.1f, 0.05f} } ;
    const F32 FACE_IMPORTANCE_TO_CAMERA_OVER_ANGLE[FACE_IMPORTANCE_LEVEL][2] =    //{cos(angle), importance_weight}
        {{0.985f /*cos(10 degrees)*/, 1.0f}, {0.94f /*cos(20 degrees)*/, 0.8f}, {0.866f /*cos(30 degrees)*/, 0.64f}, {0.0f, 0.36f}} ;

    F32 formerCalcImportanceToCamera(const Scene& scene, F32 cos_angle_to_view_dir, F32 dist)
    {
        F32 importance = 0.f ;

        if(cos_angle_to_view_dir > scene.mCosHalfFov &&
            dist < FACE_IMPORTANCE_TO_CAMERA_OVER_DISTANCE[FACE_IMPORTANCE_LEVEL - 1][0])
        {
            F32 camera_moving_speed = scene.mAverageSpeed ;
            F32 camera_angular_speed = scene.mAverageAngularSpeed;

            if(camera_moving_speed > 10.0f || camera_angular_speed > 1.0f)
            {
                //if camera moves or rotates too fast, ignore the importance factor
                return 0.f ;
            }

            S32 i = 0 ;
            for(i = 0; i < FACE_IMPORTANCE_LEVEL && dist > FACE_IMPORTANCE_TO_CAMERA_OVER_DISTANCE[i][0]; ++i);
            i = llmin(i, FACE_IMPORTANCE_LEVEL - 1) ;
            F32 dist_factor = FACE_IMPORTANCE_TO_CAMERA_OVER_DISTANCE[i][1] ;

            for(i = 0; i < FACE_IMPORTANCE_LEVEL && cos_angle_to_view_dir < FACE_IMPORTANCE_TO_CAMERA_OVER_ANGLE[i][0] ; ++i) ;
            i = llmin(i, FACE_IMPORTANCE_LEVEL - 1) ;
            importance = dist_factor * FACE_IMPORTANCE_TO_CAMERA_OVER_ANGLE[i][1] ;
        }

        return importance ;
    }

    bool formerCalcPixelArea(Scene& scene, Face& f, Lcg& frand, F32& cos_angle_to_view_dir, F32& radius)
    {
        if (scene.mFrameTime - f.mLastPixelAreaUpdate < PIXEL_AREA_UPDATE_PERIOD)
        {
            cos_angle_to_view_dir = f.mLastCosAngleToViewDir;
            radius = f.mLastPixelRadius;
            return f.mLastInFrustum;
        }

        //get area of circle around face
        LLVector4a center;
        LLVector4a size;

        if (f.mRigged)
        {
            bool hasRiggedExtents = false;

            {
                {
                    // (updateRiggingInfo and getWorldMatrix bring the table and the joints up to date here)

                    // calculate the world space bounding box of the face by combining the bounding boxes of all the joints
                    LLVector4a& minp = f.mRiggedExtents[0];
                    LLVector4a& maxp = f.mRiggedExtents[1];
                    minp = LLVector4a(FLT_MAX, FLT_MAX, FLT_MAX);
                    maxp = LLVector4a(-FLT_MAX, -FLT_MAX, -FLT_MAX);

                    for (U32 i = 0; i < f.mNumJoints; i++)
                    {
                        Joint& jointInfo = scene.mJoints[f.mFirstJoint + i];
                        if (jointInfo.mRiggedTo)
                        {
                            {
                                LLMatrix4a worldMat = jointInfo.mWorld;

                                LLVector4a extents[2];

                                matMulBoundBox(worldMat, jointInfo.mBox, extents);

                                minp.setMin(minp, extents[0]);
                                maxp.setMax(maxp, extents[1]);
                                hasRiggedExtents = true;
                            }
                        }
                    }
                }
            }

            if (!hasRiggedExtents)
            {
                // no rigged extents, zero out bounding box and skip update
                f.mRiggedExtents[0] = f.mRiggedExtents[1] = LLVector4a(0.f, 0.f, 0.f);

                return false;
            }

            center.setAdd(f.mRiggedExtents[1], f.mRiggedExtents[0]);
            center.mul(0.5f);
            size.setSub(f.mRiggedExtents[1], f.mRiggedExtents[0]);
        }
        else
        {
            center = f.mCenter;
            size.setSub(f.mExtents[1], f.mExtents[0]);
        }
        size.mul(0.5f);

        LLCamera* camera = &scene.mCamera;

        F32 size_squared = size.dot3(size).getF32();
        LLVector4a lookAt;
        LLVector4a t;
        t.load3(camera->getOrigin().mV);
        lookAt.setSub(center, t);

        F32 dist = lookAt.getLength3().getF32();
        dist = llmax(dist-size.getLength3().getF32(), 0.001f);

        lookAt.normalize3fast() ;

        //get area of circle around node
        F32 app_angle = atanf((F32) sqrt(size_squared) / dist);
        radius = app_angle*scene.mPixelAngle;
        f.mPixelArea = radius*radius * 3.14159f;

        // remember last update time, add 10% noise to avoid all faces updating at the same time
        f.mLastPixelAreaUpdate = scene.mFrameTime + frand.unit() * PIXEL_AREA_UPDATE_PERIOD * 0.1f;

        LLVector4a x_axis;
        x_axis.load3(camera->getXAxis().mV);
        cos_angle_to_view_dir = lookAt.dot3(x_axis).getF32();

        //if has media, check if the face is out of the view frustum.
        if(f.mHasMedia)
        {
            if(!camera->AABBInFrustum(center, size))
            {
                f.mImportanceToCamera = 0.f ;
                f.mLastCosAngleToViewDir = cos_angle_to_view_dir;
                f.mLastPixelRadius = radius;
                f.mLastInFrustum = false;
                return false ;
            }
            if(cos_angle_to_view_dir > scene.mCosHalfFov) //the center is within the view frustum
            {
                cos_angle_to_view_dir = 1.0f ;
            }
            else
            {
                LLVector4a d;
                d.setSub(lookAt, x_axis);

                if(dist * dist * d.dot3(d) < size_squared)
                {
                    cos_angle_to_view_dir = 1.0f ;
                }
            }
        }

        if(dist < f.mBoundingSphereRadius) //camera is very close
        {
            cos_angle_to_view_dir = 1.0f ;
            f.mImportanceToCamera = 1.0f ;
        }
        else
        {
            f.mImportanceToCamera = formerCalcImportanceToCamera(scene, cos_angle_to_view_dir, dist) ;
        }

        f.mLastCosAngleToViewDir = cos_angle_to_view_dir;
        f.mLastPixelRadius = radius;
        f.mLastInFrustum = true;
        return true ;
    }

    // --- The two steps, as LLFace has them ----------------------------------------------------------------------------
    // LLFace::computePixelArea
    void compute(const LLFacePixelArea::View& view, const Face& f, const Joint* joints, bool read_only,
                 LLFacePixelArea::Result& result)
    {
        if (view.frame_time - f.mLastPixelAreaUpdate < PIXEL_AREA_UPDATE_PERIOD)
        {
            result.clear(LLFacePixelArea::CACHED);
            return;
        }
        result.clear(LLFacePixelArea::COMPUTED);

        LLVector4a center;
        LLVector4a size;

        if (f.mRigged)
        {
            bool hasRiggedExtents = false;
            LLVector4a& minp = result.rigged_extents[0];
            LLVector4a& maxp = result.rigged_extents[1];

            if (f.mNeedsRigUpdate && read_only)
            {
                result.clear(LLFacePixelArea::NEEDS_SERIAL);
                return;
            }

            minp = LLVector4a(FLT_MAX, FLT_MAX, FLT_MAX);
            maxp = LLVector4a(-FLT_MAX, -FLT_MAX, -FLT_MAX);

            for (U32 i = 0; i < f.mNumJoints; i++)
            {
                const Joint& jointInfo = joints[f.mFirstJoint + i];
                if (jointInfo.mRiggedTo)
                {
                    if (read_only && jointInfo.mDirty)
                    {
                        result.clear(LLFacePixelArea::NEEDS_SERIAL);
                        return;
                    }
                    LLMatrix4a worldMat = jointInfo.mWorld;

                    LLFacePixelArea::addJointBox(worldMat, jointInfo.mBox, minp, maxp);
                    hasRiggedExtents = true;
                }
            }

            if (!hasRiggedExtents)
            {
                result.clear(LLFacePixelArea::NO_EXTENTS);
                return;
            }

            result.rigged = true;
            center.setAdd(maxp, minp);
            center.mul(0.5f);
            size.setSub(maxp, minp);
        }
        else
        {
            center = f.mCenter;
            size.setSub(f.mExtents[1], f.mExtents[0]);
        }
        size.mul(0.5f);

        LLFacePixelArea::project(view, center, size, f.mHasMedia, f.mBoundingSphereRadius, result);
    }

    // LLFace::applyPixelArea
    bool apply(F32 frame_time, Face& f, const LLFacePixelArea::Result& result, Lcg& frand, F32& cos_angle_to_view_dir,
               F32& radius)
    {
        switch (result.status)
        {
        case LLFacePixelArea::CACHED:
            cos_angle_to_view_dir = f.mLastCosAngleToViewDir;
            radius = f.mLastPixelRadius;
            return f.mLastInFrustum;

        case LLFacePixelArea::NO_EXTENTS:
            f.mRiggedExtents[0] = f.mRiggedExtents[1] = LLVector4a(0.f, 0.f, 0.f);
            return false;

        case LLFacePixelArea::COMPUTED:
            break;

        case LLFacePixelArea::NEEDS_SERIAL:
            tut::fail("a value that needs the main thread was applied");
            return false;
        }

        if (result.rigged)
        {
            f.mRiggedExtents[0] = result.rigged_extents[0];
            f.mRiggedExtents[1] = result.rigged_extents[1];
        }

        f.mPixelArea = result.pixel_area;

        f.mLastPixelAreaUpdate = frame_time + frand.unit() * PIXEL_AREA_UPDATE_PERIOD * 0.1f;

        f.mImportanceToCamera = result.importance;

        cos_angle_to_view_dir = result.cos_angle_to_view_dir;
        radius = result.radius;

        f.mLastCosAngleToViewDir = cos_angle_to_view_dir;
        f.mLastPixelRadius = radius;
        f.mLastInFrustum = result.in_frustum;
        return result.in_frustum;
    }

    // --- Data -------------------------------------------------------------------------------------------------------
    // A perspective camera built as LLViewerCamera::updateFrustumPlanes builds one (as in llsimdkernels_test.cpp).
    void setPerspective(LLCamera& cam, const LLVector3& origin, const LLVector3& at, const LLVector3& up, F32 fov, F32 aspect,
                        F32 near_d, F32 far_d)
    {
        const LLVector3 left = up % at;
        cam.setOrigin(origin);
        cam.setAxes(at, left, up);
        const F32 t = tanf(0.5f * fov);
        LLVector3 frust[8];
        const F32 dist[2] = { near_d, far_d };
        for (S32 f = 0; f < 2; ++f)
        {
            const F32 h = t * dist[f];
            const F32 w = h * aspect;
            const LLVector3 c = origin + at * dist[f];
            frust[4 * f + 0] = c + left * w - up * h; // bottom left
            frust[4 * f + 1] = c - left * w - up * h; // bottom right
            frust[4 * f + 2] = c - left * w + up * h; // top right
            frust[4 * f + 3] = c + left * w + up * h; // top left
        }
        cam.calcAgentFrustumPlanes(frust);
    }

    void setCamera(Scene& scene, const LLVector3& origin, F32 speed, F32 angular_speed)
    {
        const F32 fov = 1.047f;
        setPerspective(scene.mCamera, origin, LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 0.f, 1.f), fov, 1.87f, 0.1f, 256.f);
        scene.mCosHalfFov = cosf(0.5f * fov);
        scene.mAverageSpeed = speed;
        scene.mAverageAngularSpeed = angular_speed;
        scene.mPixelAngle = 1080.f / fov; // LLDrawable::sCurPixelAngle: window height / view angle
    }

    // Faces around a camera at (128, 128, 30) looking along +x: about two thirds rigged (a crowd), each with a table of
    // `joints_per_face` joints of which most are rigged to; some with media, some with a bounding sphere the camera can
    // be inside, some rigged faces with no joint at all, some whose rigging table or joints need the main thread.
    void buildScene(Scene& scene, U32 num_faces, U32 joints_per_face, uint32_t seed, bool with_serial_faces)
    {
        Lcg rng(seed);
        scene.mFaces.assign(num_faces, Face());
        scene.mJoints.assign((size_t)num_faces * joints_per_face, Joint());
        setCamera(scene, LLVector3(128.f, 128.f, 30.f), 0.f, 0.f);
        scene.mFrameTime = 10.f;

        for (U32 fi = 0; fi < num_faces; ++fi)
        {
            Face& f = scene.mFaces[fi];
            const LLVector3 pos(128.f + rng.range(-40.f, 120.f), 128.f + rng.range(-80.f, 80.f), 30.f + rng.range(-20.f, 20.f));
            f.mCenter.load3(pos.mV);
            const F32 half = rng.range(0.01f, 6.f);
            f.mExtents[0].set(-half, -half * rng.range(0.1f, 1.f), -half * rng.range(0.1f, 1.f));
            f.mExtents[1].set(half, half * rng.range(0.1f, 1.f), half * rng.range(0.1f, 1.f));
            f.mRiggedExtents[0].clear();
            f.mRiggedExtents[1].clear();
            f.mBoundingSphereRadius = (rng.next() % 8 == 0) ? rng.range(20.f, 200.f) : rng.range(0.f, 2.f);
            f.mHasMedia = (rng.next() % 6 == 0);
            f.mRigged = (rng.next() % 3 != 0);
            f.mLastPixelAreaUpdate = (rng.next() % 4 == 0) ? scene.mFrameTime - rng.range(0.f, 0.2f) : 0.f;
            f.mFirstJoint = fi * joints_per_face;
            f.mNumJoints = joints_per_face;
            const bool no_joints = (rng.next() % 16 == 0);
            if (with_serial_faces)
            {
                f.mNeedsRigUpdate = (rng.next() % 32 == 0);
            }
            for (U32 j = 0; j < joints_per_face; ++j)
            {
                Joint& joint = scene.mJoints[f.mFirstJoint + j];
                joint.mRiggedTo = !no_joints && (rng.next() % 5 != 0);
                joint.mDirty = with_serial_faces && (rng.next() % 256 == 0);
                // a rotation about z, a scale and the face's position plus a limb's offset
                const F32 a = rng.range(0.f, 6.2831853f);
                const F32 s = rng.range(0.5f, 1.5f);
                const F32 c = cosf(a) * s;
                const F32 sn = sinf(a) * s;
                joint.mWorld.mMatrix[0].set(c, sn, 0.f, 0.f);
                joint.mWorld.mMatrix[1].set(-sn, c, 0.f, 0.f);
                joint.mWorld.mMatrix[2].set(0.f, 0.f, s, 0.f);
                joint.mWorld.mMatrix[3].set(pos.mV[0] + rng.range(-1.f, 1.f), pos.mV[1] + rng.range(-1.f, 1.f),
                                            pos.mV[2] + rng.range(-1.f, 1.f), 1.f);
                const F32 b = rng.range(0.02f, 0.4f);
                joint.mBox[0].set(-b, -b * 0.5f, -b * 0.25f);
                joint.mBox[1].set(b, b * 0.5f, b * 0.25f);
            }
        }
    }

    // The next frame: time moves on, the camera moves and turns its speed up now and then.
    void nextFrame(Scene& scene, U32 frame)
    {
        scene.mFrameTime += 0.031f;
        const bool fast = (frame % 5 == 3);
        setCamera(scene, LLVector3(128.f + 0.7f * (F32)frame, 128.f - 0.3f * (F32)frame, 30.f + 0.1f * (F32)frame),
                  fast ? 14.f : 0.4f, (frame % 7 == 5) ? 1.6f : 0.1f);
    }

    // --- The pass ---------------------------------------------------------------------------------------------------
    // The serial pass over the first `count` faces: updateImageDecodePriority's call for each, as it was.
    void serialPass(Scene& scene, size_t count, Lcg& frand)
    {
        for (size_t i = 0; i < count; ++i)
        {
            Face& f = scene.mFaces[i];
            f.mInFrustum = formerCalcPixelArea(scene, f, frand, f.mOutCos, f.mOutRadius);
        }
    }

    // LLViewerTextureList::precomputePixelAreas: one batch of listed faces, [first, last), on the helpers.
    void computeAhead(Scene& scene, size_t grain, std::vector<LLFacePixelArea::Result>& results, size_t first, size_t last)
    {
        results.resize(scene.mFaces.size());
        const LLFacePixelArea::View view = scene.view();
        const Face* faces = scene.mFaces.data() + first;
        const Joint* joints = scene.mJoints.data();
        LLFacePixelArea::Result* out = results.data() + first;
        FrameJobs::parallelFor(last - first, grain,
            [&view, faces, joints, out](size_t begin, size_t end, size_t /*chunk*/)
            {
                for (size_t i = begin; i < end; ++i)
                {
                    compute(view, faces[i], joints, true, out[i]);
                }
            });
    }

    // Every face in one batch.
    void computeAhead(Scene& scene, size_t grain, std::vector<LLFacePixelArea::Result>& results)
    {
        computeAhead(scene, grain, results, 0, scene.mFaces.size());
    }

    // LLViewerTextureList::calcFacePixelArea in the loop's order, for the faces [first, last): the value computed ahead,
    // or the main thread's own computation for a face that needs it.
    void applyPass(Scene& scene, size_t first, size_t last, const std::vector<LLFacePixelArea::Result>& results, Lcg& frand)
    {
        for (size_t i = first; i < last; ++i)
        {
            Face& f = scene.mFaces[i];
            const LLFacePixelArea::Result& result = results[i];
            if (result.status != LLFacePixelArea::NEEDS_SERIAL)
            {
                f.mInFrustum = apply(scene.mFrameTime, f, result, frand, f.mOutCos, f.mOutRadius);
            }
            else
            {
                // LLFace::calcPixelArea
                LLFacePixelArea::Result own;
                compute(scene.view(), f, scene.mJoints.data(), false, own);
                f.mInFrustum = apply(scene.mFrameTime, f, own, frand, f.mOutCos, f.mOutRadius);
            }
        }
    }

    void applyPass(Scene& scene, size_t count, const std::vector<LLFacePixelArea::Result>& results, Lcg& frand)
    {
        applyPass(scene, 0, count, results, frand);
    }

    // The pass as updateImagesFetchTextures runs it: a batch computed ahead when the loop gets to its first face, the
    // loop over that batch, then the next batch, until the loop ends at `count` (which may fall inside a batch: the rest
    // of that batch is computed and dropped). Returns the faces that went back to the main thread.
    U32 batchedPass(Scene& scene, size_t count, size_t batch, size_t grain, std::vector<LLFacePixelArea::Result>& results,
                    Lcg& frand)
    {
        U32 needs_serial = 0;
        for (size_t first = 0; first < count; first += batch)
        {
            const size_t last = llmin(first + batch, scene.mFaces.size());
            computeAhead(scene, grain, results, first, last);
            for (size_t i = first; i < last; ++i)
            {
                needs_serial += (results[i].status == LLFacePixelArea::NEEDS_SERIAL);
            }
            applyPass(scene, first, llmin(last, count), results, frand);
        }
        return needs_serial;
    }

    size_t firstDifference(const Scene& a, const Scene& b)
    {
        for (size_t i = 0; i < a.mFaces.size(); ++i)
        {
            if (!sameWrites(a.mFaces[i], b.mFaces[i]))
            {
                return i;
            }
        }
        return a.mFaces.size();
    }

    std::string at(const char* what, size_t helpers, size_t grain, U32 frame, size_t index)
    {
        char text[160];
        snprintf(text, sizeof(text), "%s (helpers %u, grain %u, frame %u, face %u)", what, (unsigned)helpers, (unsigned)grain,
                 (unsigned)frame, (unsigned)index);
        return text;
    }
}

namespace tut
{
    struct pixel_area_data
    {
    };
    typedef test_group<pixel_area_data> pixel_area_group;
    typedef pixel_area_group::object object;
    pixel_area_group pixel_area_tests("LLFacePixelArea");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("compute then apply equals the former calcPixelArea, bit for bit");

        // (on the heap: a Scene is aligned as its LLCamera and vectors are)
        std::unique_ptr<Scene> former(new Scene);
        std::unique_ptr<Scene> split(new Scene);
        buildScene(*former, 6000, 9, 20261119u, true);
        buildScene(*split, 6000, 9, 20261119u, true);
        Lcg frand_former(77u);
        Lcg frand_split(77u);

        U32 counts[4] = { 0, 0, 0, 0 };
        U32 media_out = 0;
        for (U32 frame = 0; frame < 12; ++frame)
        {
            for (size_t i = 0; i < former->mFaces.size(); ++i)
            {
                Face& a = former->mFaces[i];
                a.mInFrustum = formerCalcPixelArea(*former, a, frand_former, a.mOutCos, a.mOutRadius);

                Face& b = split->mFaces[i];
                LLFacePixelArea::Result result;
                compute(split->view(), b, split->mJoints.data(), false, result);
                ensure(at("the main thread's computation never asks for itself", 0, 0, frame, i),
                       result.status != LLFacePixelArea::NEEDS_SERIAL);
                ++counts[result.status];
                media_out += (result.status == LLFacePixelArea::COMPUTED && !result.in_frustum);
                b.mInFrustum = apply(split->mFrameTime, b, result, frand_split, b.mOutCos, b.mOutRadius);

                ensure(at("face", 0, 0, frame, i), sameWrites(a, b));
            }
            ensure_equals("the same random numbers drawn", frand_split.mState, frand_former.mState);
            nextFrame(*former, frame);
            nextFrame(*split, frame);
        }
        // every path was taken
        ensure("inside the update period", counts[LLFacePixelArea::CACHED] > 1000);
        ensure("rigged faces without a joint box", counts[LLFacePixelArea::NO_EXTENTS] > 100);
        ensure("computed", counts[LLFacePixelArea::COMPUTED] > 10000);
        ensure("media faces out of the frustum", media_out > 100);
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("the pass on the helpers equals the serial pass at a fixed count, bit for bit");

        const size_t NUM_FACES = 4133;  // not a multiple of any grain below
        const size_t COUNT = 3000;      // the faces the loop reaches before its time box ends it: the fixed count
        const size_t helper_counts[] = { 0, 1, 2, 3, 5, 7 };
        const size_t grains[] = { 1, 64, 1000 };
        const size_t batches[] = { NUM_FACES, 1024, 700 }; // all at once; the viewer's batch; one the count falls inside

        for (size_t helpers : helper_counts)
        {
            Started started(helpers);
            for (int serial_mode = 0; serial_mode < 2; ++serial_mode)
            {
                FrameJobs::setSerial(serial_mode != 0);
                for (size_t grain : grains)
                {
                    for (size_t batch : batches)
                    {
                        std::unique_ptr<Scene> serial(new Scene);
                        std::unique_ptr<Scene> ahead(new Scene);
                        buildScene(*serial, (U32)NUM_FACES, 9, 4133u, true);
                        buildScene(*ahead, (U32)NUM_FACES, 9, 4133u, true);
                        Lcg frand_serial(5u);
                        Lcg frand_ahead(5u);
                        std::vector<LLFacePixelArea::Result> results;
                        U32 needs_serial = 0;

                        for (U32 frame = 0; frame < 8; ++frame)
                        {
                            serialPass(*serial, COUNT, frand_serial);

                            needs_serial += batchedPass(*ahead, COUNT, batch, grain, results, frand_ahead);

                            const size_t diff = firstDifference(*serial, *ahead);
                            ensure(at("face", helpers, grain, frame, diff), diff == NUM_FACES);
                            ensure_equals(at("random numbers", helpers, grain, frame, 0), frand_ahead.mState, frand_serial.mState);

                            nextFrame(*serial, frame);
                            nextFrame(*ahead, frame);
                        }
                        ensure("some faces went back to the main thread", needs_serial > 100);
                    }
                }
            }
        }
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("deliberate breaks are caught");

        const size_t NUM_FACES = 2000;
        Started started(3);

        // a value stored on the neighbouring face
        {
            std::unique_ptr<Scene> serial(new Scene);
            std::unique_ptr<Scene> ahead(new Scene);
            buildScene(*serial, (U32)NUM_FACES, 9, 99u, false);
            buildScene(*ahead, (U32)NUM_FACES, 9, 99u, false);
            Lcg frand_serial(5u);
            Lcg frand_ahead(5u);
            std::vector<LLFacePixelArea::Result> results;
            serialPass(*serial, NUM_FACES, frand_serial);
            computeAhead(*ahead, 64, results);
            std::swap(results[700], results[701]);
            applyPass(*ahead, NUM_FACES, results, frand_ahead);
            ensure("a value on the wrong face goes unnoticed", firstDifference(*serial, *ahead) <= 701);
        }

        // the jitter drawn where the value is computed (chunks in another order), not where it is stored
        {
            std::unique_ptr<Scene> serial(new Scene);
            std::unique_ptr<Scene> ahead(new Scene);
            buildScene(*serial, (U32)NUM_FACES, 9, 99u, false);
            buildScene(*ahead, (U32)NUM_FACES, 9, 99u, false);
            Lcg frand_serial(5u);
            Lcg frand_ahead(5u);
            std::vector<LLFacePixelArea::Result> results;
            serialPass(*serial, NUM_FACES, frand_serial);
            computeAhead(*ahead, 64, results);
            // the second chunk's faces draw before the first chunk's
            for (size_t i = 64; i < 128; ++i)
            {
                Face& f = ahead->mFaces[i];
                f.mInFrustum = apply(ahead->mFrameTime, f, results[i], frand_ahead, f.mOutCos, f.mOutRadius);
            }
            for (size_t i = 0; i < NUM_FACES; ++i)
            {
                if (i < 64 || i >= 128)
                {
                    Face& f = ahead->mFaces[i];
                    f.mInFrustum = apply(ahead->mFrameTime, f, results[i], frand_ahead, f.mOutCos, f.mOutRadius);
                }
            }
            ensure("numbers drawn in another order go unnoticed", firstDifference(*serial, *ahead) < 128);
        }
    }

    // --- Bench (opt-in) -------------------------------------------------------------------------------------------------
    using bench_clock = std::chrono::steady_clock;

    F64 microseconds(bench_clock::time_point from, bench_clock::time_point to)
    {
        return std::chrono::duration<F64, std::micro>(to - from).count();
    }

    void spinFor(F64 us)
    {
        const bench_clock::time_point start = bench_clock::now();
        while (microseconds(start, bench_clock::now()) < us)
        {
        }
    }

    struct BenchResult
    {
        F64 median = 0.0;
        F64 p90 = 0.0;
        F64 max = 0.0;
    };

    // One "frame" a repetition: the frame-work window opens, the main thread works alone for `lead_us` (the culls before
    // the texture list), the batch runs, the window closes and the rest of the frame passes (`rest_ms`, helpers park).
    BenchResult benchBatch(Scene& scene, size_t grain, U32 reps, F64 lead_us, U32 rest_ms)
    {
        std::vector<LLFacePixelArea::Result> results;
        std::vector<F64> times;
        times.reserve(reps);
        for (U32 r = 0; r < reps + 10; ++r)
        {
            FrameJobs::beginFrameWork();
            spinFor(lead_us);
            const bench_clock::time_point start = bench_clock::now();
            computeAhead(scene, grain, results);
            const bench_clock::time_point end = bench_clock::now();
            FrameJobs::endFrameWork();
            if (r >= 10) // warm-up
            {
                times.push_back(microseconds(start, end));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(rest_ms));
        }
        std::sort(times.begin(), times.end());
        BenchResult result;
        result.median = times[times.size() / 2];
        result.p90 = times[(times.size() * 9) / 10];
        result.max = times.back();
        return result;
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("bench (opt-in: LL_FACE_PIXEL_AREA_BENCH=1)");
        const char* bench = getenv("LL_FACE_PIXEL_AREA_BENCH");
        if (!bench || !*bench || *bench == '0')
        {
            return;
        }

        // Rooftops, S1b: about 1,800 rigged calls a frame and up to 2,800 faces; every face outside its update period
        const U32 NUM_FACES = 2600;
        const U32 REPS = 120;
        const F64 LEAD_US = 1000.0;
        const U32 REST_MS = 4;
        const size_t GRAIN = 64;

        std::unique_ptr<Scene> scene(new Scene);
        buildScene(*scene, NUM_FACES, 24, 31u, false);
        for (Face& f : scene->mFaces)
        {
            f.mLastPixelAreaUpdate = 0.f;
        }

        const char* source = "";
        const size_t cores = FrameJobs::fastPhysicalCores(&source);
        printf("\nC8 bench: %u faces (two thirds rigged, 24 joint slots), grain %u, %u repetitions a row, %g us of main-thread "
               "work before each batch, %u ms between frames\n", NUM_FACES, (unsigned)GRAIN, REPS, LEAD_US, REST_MS);
        printf("fast physical cores: %u (%s); width rule: %u helpers; logical processors: %u\n", (unsigned)cores, source,
               (unsigned)FrameJobs::widthForCores(cores), std::thread::hardware_concurrency());

        // Rows are interleaved (A B C A B C ...) so that a busy machine touches them alike.
        const size_t widths[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        const U32 ROUNDS = 3;
        std::vector<std::vector<F64>> medians(std::size(widths)), p90s(std::size(widths));
        for (U32 round = 0; round < ROUNDS; ++round)
        {
            for (size_t w = 0; w < std::size(widths); ++w)
            {
                Started started(widths[w]);
                const BenchResult result = benchBatch(*scene, GRAIN, REPS / ROUNDS, LEAD_US, REST_MS);
                medians[w].push_back(result.median);
                p90s[w].push_back(result.p90);
            }
        }
        printf("width sweep (batch time in us, median of %u rounds):\n helpers   median      p90   speed-up\n", ROUNDS);
        F64 serial_median = 0.0;
        for (size_t w = 0; w < std::size(widths); ++w)
        {
            std::sort(medians[w].begin(), medians[w].end());
            std::sort(p90s[w].begin(), p90s[w].end());
            const F64 median = medians[w][ROUNDS / 2];
            if (w == 0)
            {
                serial_median = median;
            }
            printf(" %7u %8.1f %8.1f %9.2fx\n", (unsigned)widths[w], median, p90s[w][ROUNDS / 2], serial_median / median);
        }

#if LL_WINDOWS
        // Beside the decode pool at work: four threads that never sleep, at normal and at below-normal priority.
        const size_t rule = FrameJobs::widthForCores(cores);
        const size_t contention_widths[] = { 0, rule > 2 ? rule - 2 : rule, rule };
        const int priorities[] = { THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_BELOW_NORMAL };
        const char* priority_names[] = { "normal", "below normal" };
        const size_t NUM_ROWS = std::size(contention_widths) * std::size(priorities);
        std::vector<std::vector<F64>> c_medians(NUM_ROWS), c_p90s(NUM_ROWS), c_max(NUM_ROWS);
        for (U32 round = 0; round < ROUNDS; ++round)
        {
            for (size_t p = 0; p < std::size(priorities); ++p)
            {
                std::atomic<bool> stop(false);
                std::vector<std::thread> busy;
                for (int t = 0; t < 4; ++t)
                {
                    busy.emplace_back([&stop, priority = priorities[p]]()
                    {
                        SetThreadPriority(GetCurrentThread(), priority);
                        volatile F64 x = 1.0;
                        while (!stop.load(std::memory_order_relaxed))
                        {
                            for (int i = 0; i < 4096; ++i)
                            {
                                x = x * 1.0000001 + 0.5;
                            }
                        }
                    });
                }
                for (size_t w = 0; w < std::size(contention_widths); ++w)
                {
                    Started started(contention_widths[w]);
                    const BenchResult result = benchBatch(*scene, GRAIN, REPS / ROUNDS, LEAD_US, REST_MS);
                    const size_t row = p * std::size(contention_widths) + w;
                    c_medians[row].push_back(result.median);
                    c_p90s[row].push_back(result.p90);
                    c_max[row].push_back(result.max);
                }
                stop = true;
                for (std::thread& t : busy)
                {
                    t.join();
                }
            }
        }
        printf("beside four busy threads (batch time in us, median of %u rounds):\n busy threads   helpers   median      p90      max\n",
               ROUNDS);
        for (size_t row = 0; row < NUM_ROWS; ++row)
        {
            std::sort(c_medians[row].begin(), c_medians[row].end());
            std::sort(c_p90s[row].begin(), c_p90s[row].end());
            std::sort(c_max[row].begin(), c_max[row].end());
            printf(" %-14s %7u %8.1f %8.1f %8.1f\n", priority_names[row / std::size(contention_widths)],
                   (unsigned)contention_widths[row % std::size(contention_widths)], c_medians[row][ROUNDS / 2],
                   c_p90s[row][ROUNDS / 2], c_max[row][ROUNDS / 2]);
        }
#endif
        fflush(stdout);
    }
}

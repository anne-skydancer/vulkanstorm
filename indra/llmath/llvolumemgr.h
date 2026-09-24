/**
 * @file llvolumemgr.h
 * @brief LLVolumeMgr class.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#ifndef LL_LLVOLUMEMGR_H
#define LL_LLVOLUMEMGR_H

#include <chrono>
#include <map>

#include "llvolume.h"
#include "llpointer.h"
#include "llthread.h"

class LLVolumeParams;
class LLVolumeLODGroup;

class LLVolumeLODGroup
{
    LOG_CLASS(LLVolumeLODGroup);

public:
    enum
    {
        NUM_LODS = 4
    };

    LLVolumeLODGroup(const LLVolumeParams &params);
    ~LLVolumeLODGroup();
    bool cleanupRefs();

    static S32 getDetailFromTan(const F32 tan_angle);
    static void getDetailProximity(const F32 tan_angle, F32 &to_lower, F32& to_higher);
    static F32 getVolumeScaleFromDetail(const S32 detail);
    static S32 getVolumeDetailFromScale(F32 scale);

    LLVolume* refLOD(const S32 detail);
    bool derefLOD(LLVolume *volumep);
    S32 getNumRefs() const { return mRefs; }
    bool takeUnusedLOD(bool pressure, LLPointer<LLVolume>& released);

    const LLVolumeParams* getVolumeParams() const { return &mVolumeParams; };

    F32 dump();
    friend std::ostream& operator<<(std::ostream& s, const LLVolumeLODGroup& volgroup);

protected:
    LLVolumeParams mVolumeParams;

    S32 mRefs;
    S32 mLODRefs[NUM_LODS];
    std::chrono::steady_clock::time_point mUnusedSince[NUM_LODS];
    LLPointer<LLVolume> mVolumeLODs[NUM_LODS];
    static F32 mDetailThresholds[NUM_LODS];
    static F32 mDetailScales[NUM_LODS];
    S32     mAccessCount[NUM_LODS];
};

class LLVolumeMgr
{
public:
    LLVolumeMgr();
    virtual ~LLVolumeMgr();
    bool cleanup();         // Cleanup all volumes being managed, returns true if no dangling references

    virtual LLVolumeLODGroup* getGroup( const LLVolumeParams& volume_params ) const;

    // whatever calls getVolume() never owns the LLVolume* and
    // cannot keep references for long since it may be deleted
    // later.  For best results hold it in an LLPointer<LLVolume>.
    virtual LLVolume *refVolume(const LLVolumeParams &volume_params, const S32 detail);
    virtual void unrefVolume(LLVolume *volumep);

    void dump();
    // Main-thread maintenance: bounded group scan, at most one payload released
    // outside the manager lock per call. Active or externally referenced LODs stay.
    U32 trimUnusedLODs(bool pressure);


    // manually call this for mutex magic
    void useMutex();

    friend std::ostream& operator<<(std::ostream& s, const LLVolumeMgr& volume_mgr);

protected:
    void insertGroup(LLVolumeLODGroup* volgroup);
    // Overridden in llphysics/abstract/utils/llphysicsvolumemanager.h
    virtual LLVolumeLODGroup* createNewGroup(const LLVolumeParams& volume_params);

protected:
    typedef std::map<const LLVolumeParams*, LLVolumeLODGroup*, LLVolumeParams::compare> volume_lod_group_map_t;
    volume_lod_group_map_t mVolumeLODGroups;

    LLMutex* mDataMutex;
    LLVolumeParams mTrimCursor;
    bool mHaveTrimCursor = false;
};

// Move-only lease into the existing decoded-volume asset pool. The manager's
// per-LOD reference is the pin; destruction releases exactly that acquisition.
// Legacy object/picking owners continue to use their established references.
class LLVolumeLease
{
    LLVolumeMgr* mManager = nullptr;
    LLVolume* mVolume = nullptr;
public:
    LLVolumeLease() = default;
    LLVolumeLease(LLVolumeMgr& manager, const LLVolumeParams& params, S32 lod)
        : mManager(&manager), mVolume(manager.refVolume(params, lod)) {}
    ~LLVolumeLease() { reset(); }
    LLVolumeLease(const LLVolumeLease&) = delete;
    LLVolumeLease& operator=(const LLVolumeLease&) = delete;
    LLVolumeLease(LLVolumeLease&& other) noexcept
        : mManager(other.mManager), mVolume(other.mVolume)
    { other.mManager = nullptr; other.mVolume = nullptr; }
    LLVolumeLease& operator=(LLVolumeLease&& other) noexcept
    {
        if (this != &other)
        {
            reset(); mManager = other.mManager; mVolume = other.mVolume;
            other.mManager = nullptr; other.mVolume = nullptr;
        }
        return *this;
    }
    LLVolume* get() const { return mVolume; }
    LLVolume* operator->() const { return mVolume; }
    explicit operator bool() const { return mVolume != nullptr; }
    void reset()
    {
        auto* volume = mVolume;
        mVolume = nullptr;
        if (volume) mManager->unrefVolume(volume);
        mManager = nullptr;
    }
};

#endif // LL_LLVOLUMEMGR_H

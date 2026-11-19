// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The joint-name -> joint-number table the mesh repository threads use to initialise a skin's joint numbers
// (LLMeshRepoThread::skinInfoReceived). They used to ask gAgentAvatarp for each joint, from the repo and pool threads,
// while the main thread could still be building that avatar (gAgentAvatarp is assigned before initInstance runs) and
// both repo threads could run the same skin copy's init at once. The main thread now builds one immutable table after
// LLVOAvatarSelf::initAllJoints and publishes it once (release); the threads read it (acquire) and never touch the
// avatar. A table that was not published yet, or that lacks a name of the skin, leaves the skin uninitialised: the main
// thread then initialises it on first use (LLSkinningUtil::initJointNums), as it always did for such skins.
// Pure functions and types: tests/lljointnumtable_test.cpp.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class LLJointNumTable
{
public:
    void add(const std::string& name, int32_t joint_num) { mNums[name] = joint_num; }
    size_t size() const { return mNums.size(); }

    // The number of the joint, false when the table has no such name.
    bool find(const std::string& name, int32_t& joint_num) const
    {
        auto it = mNums.find(name);
        if (it == mNums.end())
        {
            return false;
        }
        joint_num = it->second;
        return true;
    }

    // Fills nums (one per name) when the table knows every name; nums is untouched otherwise.
    bool lookupAll(const std::vector<std::string>& names, std::vector<int32_t>& nums) const
    {
        std::vector<int32_t> found(names.size());
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (!find(names[i], found[i]))
            {
                return false;
            }
        }
        nums.swap(found);
        return true;
    }

private:
    std::unordered_map<std::string, int32_t> mNums;
};

// Publishes tables one after another (one per self avatar built in a session). A published table is never changed or
// freed before the program exits, so a reader's pointer stays valid on any thread.
class LLJointNumTablePublisher
{
public:
    // Main thread, once the table is complete. The table is immutable from here on.
    void publish(std::unique_ptr<const LLJointNumTable> table)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const LLJointNumTable* raw = table.get();
        mOwned.push_back(std::move(table));
        mCurrent.store(raw, std::memory_order_release);
    }

    // Any thread; null until the first publish.
    const LLJointNumTable* get() const { return mCurrent.load(std::memory_order_acquire); }

private:
    std::atomic<const LLJointNumTable*> mCurrent{nullptr};
    std::mutex mMutex;
    std::vector<std::unique_ptr<const LLJointNumTable>> mOwned;
};

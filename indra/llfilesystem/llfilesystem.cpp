/**
 * @file filesystem.h
 * @brief Simulate local file system operations.
 * @Note The initial implementation does actually use standard C++
 *       file operations but eventually, there will be another
 *       layer that caches and manages file meta data too.
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

#include "linden_common.h"

#include "lldir.h"
#include "llfilesystem.h"
#include "llfasttimer.h"
#include "lldiskcache.h"

#include "vkstorm_ffi.h"

// The file operations are vkstorm-assets (vkstorm-rust rung five,
// diskcache::file); this class keeps the position bookkeeping.

constexpr S32 LLFileSystem::READ        = 0x00000001;
constexpr S32 LLFileSystem::WRITE       = 0x00000002;
constexpr S32 LLFileSystem::READ_WRITE  = 0x00000003;  // LLFileSystem::READ & LLFileSystem::WRITE
constexpr S32 LLFileSystem::APPEND      = 0x00000006;  // 0x00000004 & LLFileSystem::WRITE

static LLTrace::BlockTimerStatHandle FTM_VFILE_WAIT("VFile Wait");

LLFileSystem::LLFileSystem(const LLUUID& file_id, const LLAssetType::EType file_type, S32 mode)
{
    mFileType = file_type;
    mFileID = file_id;
    mPosition = 0;
    mBytesRead = 0;
    mMode = mode;

    // This block of code was originally called in the read() method but after comments here:
    // https://bitbucket.org/lindenlab/viewer/commits/e28c1b46e9944f0215a13cab8ee7dded88d7fc90#comment-10537114
    // we moved it to the constructor to avoid the file open/close/update time overhead.
    // We also moved the deletion of the LLFile from the read() method since we may read the file
    // several times and only want to update the access time once.
    if (mode == LLFileSystem::READ)
    {
        // build the filename (TODO: we do this in a few places - perhaps we should factor into a single function)
        const std::string filename = LLDiskCache::metaDataToFilepath(mFileID, mFileType);

        // update the last access time for the file if it exists - this is required
        // even though we are reading and not writing because this is the
        // way the cache works - it relies on a valid "last accessed time" for
        // each file so it knows how to remove the oldest, unused files
        bool exists = gDirUtilp->fileExists(filename);
        if (exists)
        {
            updateFileAccessTime(filename);
        }
    }
}

// static
bool LLFileSystem::getExists(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    LL_PROFILE_ZONE_SCOPED;
    const std::string filename = LLDiskCache::metaDataToFilepath(file_id, file_type);
    return vks_asset_file_exists(filename.data(), filename.size()) != 0;
}

// static
bool LLFileSystem::removeFile(const LLUUID& file_id, const LLAssetType::EType file_type, int suppress_error /*= 0*/)
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    const std::string filename = LLDiskCache::metaDataToFilepath(file_id, file_type);
    vks_asset_file_remove(filename.data(), filename.size());
    return true;
}

// static
bool LLFileSystem::renameFile(const LLUUID& old_file_id, const LLAssetType::EType old_file_type,
                              const LLUUID& new_file_id, const LLAssetType::EType new_file_type)
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    const std::string old_filename = LLDiskCache::metaDataToFilepath(old_file_id, old_file_type);
    const std::string new_filename = LLDiskCache::metaDataToFilepath(new_file_id, new_file_type);

    // The rename replaces an existing file, as LLFile::rename did; renaming
    // a file onto itself (the same id under another asset type names the
    // same cache file) leaves it as it is.
    if (!vks_asset_file_rename(old_filename.data(), old_filename.size(), new_filename.data(), new_filename.size()))
    {
        LL_WARNS() << "Failed to rename " << old_file_id << " to " << new_file_id << LL_ENDL;
    }

    return true;
}

// static
S32 LLFileSystem::getFileSize(const LLUUID& file_id, const LLAssetType::EType file_type)
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    const std::string filename = LLDiskCache::metaDataToFilepath(file_id, file_type);
    return vks_asset_file_size(filename.data(), filename.size());
}

bool LLFileSystem::read(U8* buffer, S32 bytes)
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    bool success = false;

    const std::string filename = LLDiskCache::metaDataToFilepath(mFileID, mFileType);
    const S32 bytes_read = vks_asset_file_read(filename.data(), filename.size(), mPosition, buffer, (size_t)llmax(bytes, 0));
    if (bytes_read >= 0)
    {
        mBytesRead = bytes_read;
        mPosition += mBytesRead;
        if (mBytesRead)
        {
            success = true;
        }
    }

    return success;
}

S32 LLFileSystem::getLastBytesRead() const
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    return mBytesRead;
}

bool LLFileSystem::eof() const
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    return mPosition >= getSize();
}

bool LLFileSystem::write(const U8* buffer, S32 bytes)
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    const std::string filename = LLDiskCache::metaDataToFilepath(mFileID, mFileType);
    const S32 new_position = vks_asset_file_write(filename.data(), filename.size(), mMode, mPosition, buffer, (size_t)llmax(bytes, 0));
    if (new_position < 0)
    {
        return false;
    }
    mPosition = new_position;
    return true;
}

bool LLFileSystem::seek(S32 offset, S32 origin)
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    if (-1 == origin)
    {
        origin = mPosition;
    }

    S32 new_pos = origin + offset;
    S32 size = getSize();

    if (new_pos > size)
    {
        LL_WARNS() << "Attempt to seek past end of file" << LL_ENDL;
    }
    else if (new_pos < 0)
    {
        LL_WARNS() << "Attempt to seek past beginning of file" << LL_ENDL;
    }
    S32 clamped = 0;
    const bool within = vks_asset_file_seek(mPosition, offset, origin, size, &clamped) != 0;
    mPosition = clamped;
    return within;
}

S32 LLFileSystem::tell() const
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    return mPosition;
}

S32 LLFileSystem::getSize() const
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    return LLFileSystem::getFileSize(mFileID, mFileType);
}

S32 LLFileSystem::getMaxSize() const
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    // offer up a huge size since we don't care what the max is
    return INT_MAX;
}

bool LLFileSystem::rename(const LLUUID& new_id, const LLAssetType::EType new_type)
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    LLFileSystem::renameFile(mFileID, mFileType, new_id, new_type);

    mFileID = new_id;
    mFileType = new_type;

    return true;
}

bool LLFileSystem::remove() const
{
    LL_PROFILE_ZONE_COLOR(tracy::Color::Gold); // <FS:Beq> measure cache performance
    LLFileSystem::removeFile(mFileID, mFileType);

    return true;
}

void LLFileSystem::updateFileAccessTime(const std::string& file_path)
{
    /**
     * Threshold in time_t units that is used to decide if the last access time
     * time of the file is updated or not. Added as a precaution for the concern
     * outlined in SL-14582  about frequent writes on older SSDs reducing their
     * lifespan. I think this is the right place for the threshold value - rather
     * than it being a pref - do comment on that Jira if you disagree...
     *
     * Let's start with 1 hour in time_t units and see how that unfolds
     */
    vks_asset_file_touch(file_path.data(), file_path.size());
}

/**
 * @file llcloudnoise.cpp
 * @brief The volumetric clouds' noise file: load and verify at start-up
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#include "llviewerprecompiledheaders.h"

#include "llcloudnoise.h"

#include "llcloudnoisehash.h" // LL_CLOUD_NOISE_SHA256, generated from indra/cmake/CloudNoise.cmake
#include "lldir.h"
#include "llfile.h"
#include "lltimer.h"

#include <cerrno>
#include <cstring>

namespace
{
    std::vector<uint8_t> sFile;
    LLCloudNoise::Parsed sParsed;
    bool sLoaded = false;
}

namespace LLCloudNoise
{
bool loadAndVerify(std::string& path, std::string& error)
{
    release();
    path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, APP_SETTINGS_DIR, FILE_NAME);

    // A virus scanner or an installer can hold the file for a moment: log why an open failed and try once more.
    llifstream file;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        errno = 0;
        file.open(path.c_str(), std::ios::in | std::ios::binary);
        if (file.is_open())
        {
            break;
        }
        const int reason = errno;
        LL_WARNS("InitInfo") << "Cannot open " << path << " (attempt " << (attempt + 1) << "): "
                             << (reason ? std::strerror(reason) : "unknown reason") << " (errno " << reason << ")" << LL_ENDL;
        if (attempt == 0)
        {
            ms_sleep(200);
        }
    }
    if (!file.is_open())
    {
        error = "cannot open the file";
        return false;
    }

    // the size first: anything but the exact size is rejected before it is read
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size < 0 || (uint64_t)size != FILE_BYTES)
    {
        error = "wrong file size " + std::to_string((long long)size);
        return false;
    }
    sFile.resize((size_t)size);
    file.read(reinterpret_cast<char*>(sFile.data()), size);
    if (file.gcount() != size)
    {
        error = "short read (" + std::to_string((long long)file.gcount()) + " of " + std::to_string((long long)size) + " bytes)";
        release();
        return false;
    }
    if (!LLCloudNoise::verify(sFile.data(), sFile.size(), LL_CLOUD_NOISE_SHA256, sParsed, error))
    {
        release();
        return false;
    }
    sLoaded = true;
    return true;
}

const Parsed* data()
{
    return sLoaded ? &sParsed : nullptr;
}

void release()
{
    sLoaded = false;
    sParsed = Parsed();
    std::vector<uint8_t>().swap(sFile);
}
}

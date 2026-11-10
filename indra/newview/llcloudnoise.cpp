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

#include "lldir.h"
#include "llfile.h"

#include <iterator>

#ifndef LL_CLOUD_NOISE_SHA256
#error "LL_CLOUD_NOISE_SHA256 is set by indra/newview/CMakeLists.txt from indra/cmake/CloudNoise.cmake"
#endif

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
    llifstream file(path.c_str(), std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        error = "cannot open the file";
        return false;
    }
    sFile.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
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

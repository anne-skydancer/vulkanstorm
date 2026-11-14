/**
 * @file vlc_cache_gen.cpp
 * @brief Writes libvlc's plugin cache (plugins.dat) for a plugins directory.
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

// Without plugins.dat, libvlc_new() opens every plugin DLL of the plugins
// directory (about 370) to read its descriptor, and Windows scans each one
// the first time it is loaded: the parcel music then takes half a minute or
// more to start after a fresh install or build. With the cache, libvlc reads
// the descriptors from the file and loads a module only when it is used.
//
// The cache records each plugin's size and modification time, so it must be
// written where the files finally live, with their final timestamps: the
// build runs this tool on the staged llplugin/plugins (viewer_manifest.py),
// and the packaging keeps the timestamps. media_plugin_libvlc regenerates a
// missing cache on its first start when the directory is writable.
//
//     vlc_cache_gen <plugins directory>
//
// libvlc.dll and libvlccore.dll must be on the PATH (or beside this tool).

#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/stat.h>

#include "vlc/vlc.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace
{
    bool file_exists(const std::string& path)
    {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: vlc_cache_gen <plugins directory>\n");
        return 2;
    }
    const std::string plugins = argv[1];
    const std::string cache = plugins + "/plugins.dat";

#if defined(_WIN32)
    SetEnvironmentVariableA("VLC_PLUGIN_PATH", plugins.c_str());
#else
    setenv("VLC_PLUGIN_PATH", plugins.c_str(), 1);
#endif

    // --reset-plugins-cache scans the directory and writes plugins.dat there;
    // a plain start would only read an existing cache.
    const char* args[] = { "--reset-plugins-cache", "--no-xlib", "--quiet" };
    libvlc_instance_t* vlc = libvlc_new(sizeof(args) / sizeof(*args), args);
    if (!vlc)
    {
        std::fprintf(stderr, "vlc_cache_gen: libvlc_new failed for %s\n", plugins.c_str());
        return 1;
    }
    libvlc_release(vlc);

    if (!file_exists(cache))
    {
        std::fprintf(stderr, "vlc_cache_gen: libvlc wrote no %s (is the directory writable?)\n", cache.c_str());
        return 1;
    }
    std::printf("vlc_cache_gen: wrote %s\n", cache.c_str());
    return 0;
}

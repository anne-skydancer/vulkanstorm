// SPDX-License-Identifier: LGPL-2.1-only
// POSIX file creation for the external editor's per-user handshake challenge.
#pragma once

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace LLScriptEditorPOSIX
{
inline bool writeUserOnlyFile(const std::string& file, const std::string& text)
{
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0)
    {
        return false;
    }

    bool ok = true;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const auto written = ::write(fd, text.data() + offset, text.size() - offset);
        if (written > 0)
        {
            offset += static_cast<std::size_t>(written);
        }
        else if (written < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            ok = false;
            break;
        }
    }

    // Do not retry close(): on Linux the descriptor is released even on error.
    if (::close(fd) != 0)
    {
        ok = false;
    }
    if (!ok)
    {
        ::unlink(file.c_str());
    }
    return ok;
}
}
#endif

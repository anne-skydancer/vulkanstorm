/**
 * @file   llsys_test.cpp
 * @brief  Test gzip_file() and gunzip_file() on valid, corrupt and truncated input.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

#include "../llsys.h"

#include "../test/lltut.h"
#include "../test/namedtempfile.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace
{
    // Temporary paths for one gzip/gunzip round, removed on scope exit.
    struct GzPaths
    {
        GzPaths():
            src(NamedTempFile::temp_path("llsys_src_", ".txt").string()),
            gz(NamedTempFile::temp_path("llsys_gz_", ".gz").string()),
            out(NamedTempFile::temp_path("llsys_out_", ".txt").string())
        {}
        ~GzPaths()
        {
            for (const std::string& path : { src, gz, out, out + ".t", gz + ".t" })
            {
                boost::system::error_code ec;
                boost::filesystem::remove(path, ec);
            }
        }
        std::string src, gz, out;
    };

    std::string read_file(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    void write_file(const std::string& path, const std::string& data)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), data.size());
    }

    bool exists(const std::string& path)
    {
        boost::system::error_code ec;
        return boost::filesystem::exists(path, ec);
    }

    // More than one gunzip_file() read buffer (32 KB) of uncompressed data.
    std::string payload()
    {
        std::ostringstream out;
        for (int i = 0; i < 8000; ++i)
        {
            out << "inventory cache line " << i << " value " << (i * 7919 % 10007) << '\n';
        }
        return out.str();
    }

    // Write payload() to paths.src and compress it into paths.gz.
    std::string make_gz(const GzPaths& paths)
    {
        write_file(paths.src, payload());
        tut::ensure("gzip_file() compresses the fixture", gzip_file(paths.src, paths.gz));
        std::string gz = read_file(paths.gz);
        // 10-byte header, deflate data, 8-byte trailer (CRC32 and size)
        tut::ensure("fixture has a gzip header", gz.size() > 18 && U8(gz[0]) == 0x1f && U8(gz[1]) == 0x8b);
        return gz;
    }

    void ensure_failed_cleanly(const std::string& desc, const GzPaths& paths)
    {
        tut::ensure(desc + ": gunzip_file() returns false", !gunzip_file(paths.gz, paths.out));
        tut::ensure(desc + ": no output file", !exists(paths.out));
        tut::ensure(desc + ": no partial output file", !exists(paths.out + ".t"));
    }
}

namespace tut
{
    struct llsys_data
    {
    };
    typedef test_group<llsys_data> llsys_group;
    typedef llsys_group::object object;
    llsys_group llsysgrp("llsys");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("gzip_file() and gunzip_file() round trip");
        GzPaths paths;
        make_gz(paths);
        ensure("gunzip_file() succeeds", gunzip_file(paths.gz, paths.out));
        ensure("round trip preserves the data", read_file(paths.out) == payload());
        ensure("no temporary file left", !exists(paths.out + ".t"));
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("gunzip_file() rejects a bad CRC");
        GzPaths paths;
        std::string gz = make_gz(paths);
        gz[gz.size() - 8] ^= 0xff; // first byte of the CRC32 trailer
        write_file(paths.gz, gz);
        ensure_failed_cleanly("bad CRC", paths);
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("gunzip_file() rejects garbage after a valid header");
        GzPaths paths;
        std::string gz = make_gz(paths);
        // 0xff starts a final deflate block of the reserved type 3
        gz.replace(10, gz.size() - 10, gz.size() - 10, '\xff');
        write_file(paths.gz, gz);
        ensure_failed_cleanly("garbage after header", paths);
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("gunzip_file() rejects a truncated file");
        GzPaths paths;
        std::string gz = make_gz(paths);
        gz.resize(gz.size() / 2);
        write_file(paths.gz, gz);
        ensure_failed_cleanly("truncated", paths);
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("gzip_file() keeps the old file when gzclose() fails");
#if LL_LINUX
        // A small input stays in zlib's buffer until gzclose(), so pointing
        // the temporary file at /dev/full makes the close's flush fail with
        // ENOSPC. Windows and macOS have no portable equivalent.
        if (!exists("/dev/full"))
        {
            skip("no /dev/full");
        }
        GzPaths paths;
        const std::string previous("previous cache contents\n");
        write_file(paths.src, "new cache contents\n");
        write_file(paths.gz, previous);
        boost::filesystem::create_symlink("/dev/full", paths.gz + ".t");
        ensure("gzip_file() returns false", !gzip_file(paths.src, paths.gz));
        // Check the type before reading: /dev/full reads zeros forever.
        ensure("the previous file is still a regular file",
               boost::filesystem::symlink_status(paths.gz).type() == boost::filesystem::regular_file);
        ensure("the previous file is unchanged", read_file(paths.gz) == previous);
        boost::system::error_code ec;
        ensure("no temporary file left",
               boost::filesystem::symlink_status(paths.gz + ".t", ec).type() == boost::filesystem::file_not_found);
#else
        skip("needs /dev/full to make gzclose() fail");
#endif
    }
}

// SPDX-License-Identifier: LGPL-2.1-only
// Standalone POSIX challenge-file contract checks; no viewer/UI dependencies.
#include <algorithm>
#include <atomic>
#include <barrier>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

enum class Fault { None, InterruptedShortWrites, ZeroWrite, WriteFailure, CloseFailure };
static Fault fault = Fault::None;
static unsigned write_calls = 0;

static ssize_t challenge_test_write(int fd, const void* data, std::size_t size)
{
    const int flags = ::fcntl(fd, F_GETFD);
    assert(flags >= 0 && (flags & FD_CLOEXEC) != 0);
    if (fault == Fault::InterruptedShortWrites && write_calls++ == 0)
    {
        errno = EINTR;
        return -1;
    }
    if (fault == Fault::WriteFailure && write_calls++ != 0)
    {
        errno = EIO;
        return -1;
    }
    if (fault == Fault::ZeroWrite) return 0;
    if (fault == Fault::InterruptedShortWrites || fault == Fault::WriteFailure)
    {
        size = std::min<std::size_t>(size, 7);
    }
    return ::write(fd, data, size);
}

static int challenge_test_close(int fd)
{
    const int result = ::close(fd);
    if (fault == Fault::CloseFailure) { errno = EIO; return -1; }
    return result;
}

// Inject I/O faults into the exact implementation used by the viewer.
#define write challenge_test_write
#define close challenge_test_close
#include "llscripteditorposixfile.h"
#undef close
#undef write

static std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main()
{
    namespace fs = std::filesystem;
    char pattern[] = "/tmp/vkstorm-challenge-test-XXXXXX";
    char* created = ::mkdtemp(pattern);
    assert(created);
    const fs::path dir(created);
    const auto old_umask = ::umask(0);
    const std::string text("nonce\nsecret\0tail", 17);
    const auto file = dir / "challenge";
    assert(LLScriptEditorPOSIX::writeUserOnlyFile(file.string(), text));
    struct stat info{};
    assert(::stat(file.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    assert(read_file(file) == text);

    assert(!LLScriptEditorPOSIX::writeUserOnlyFile(file.string(), "overwrite"));
    assert(read_file(file) == text);
    const auto link = dir / "link";
    assert(::symlink(file.c_str(), link.c_str()) == 0);
    assert(!LLScriptEditorPOSIX::writeUserOnlyFile(link.string(), "overwrite"));
    assert(fs::is_symlink(link) && read_file(file) == text);
    assert(!LLScriptEditorPOSIX::writeUserOnlyFile((dir / "missing" / "file").string(), text));

    fault = Fault::InterruptedShortWrites;
    write_calls = 0;
    const auto partial = dir / "partial";
    assert(LLScriptEditorPOSIX::writeUserOnlyFile(partial.string(), text));
    assert(write_calls > 2 && read_file(partial) == text);

    for (const auto failure : {Fault::ZeroWrite, Fault::WriteFailure, Fault::CloseFailure})
    {
        fault = failure;
        write_calls = 0;
        const auto failed = dir / "failed";
        assert(!LLScriptEditorPOSIX::writeUserOnlyFile(failed.string(), text));
        assert(!fs::exists(failed));
    }

    fault = Fault::None;
    const auto raced = dir / "raced";
    std::atomic<unsigned> winners{0};
    std::barrier start(9);
    std::vector<std::thread> threads;
    std::vector<std::string> candidates;
    for (unsigned i = 0; i < 8; ++i) candidates.push_back("creator-" + std::to_string(i));
    for (const auto& candidate : candidates)
    {
        threads.emplace_back([&, candidate]() {
            start.arrive_and_wait();
            if (LLScriptEditorPOSIX::writeUserOnlyFile(raced.string(), candidate)) ++winners;
        });
    }
    start.arrive_and_wait();
    for (auto& thread : threads) thread.join();
    assert(winners == 1);
    assert(std::find(candidates.begin(), candidates.end(), read_file(raced)) != candidates.end());

    const auto empty = dir / "empty";
    assert(LLScriptEditorPOSIX::writeUserOnlyFile(empty.string(), ""));
    assert(fs::file_size(empty) == 0);
    assert(::stat(empty.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    ::umask(old_umask);
    assert(dir.parent_path() == "/tmp" && dir.filename().string().starts_with("vkstorm-challenge-test-"));
    fs::remove_all(dir);
    std::cout << "POSIX challenge file: 10 scenarios passed\n";
}

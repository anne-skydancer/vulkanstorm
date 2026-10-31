/**
 * @file llinventory_task_oracle_test.cpp
 * @brief Oracle for object contents ("task inventory"), recorded from the
 * C++ before vkstorm-rust rung seven (recorder R4a of the inventory rung).
 *
 * Drives the frozen copies of the task-inventory code of llviewerobject.cpp
 * and llviewerinventory.cpp (llinventory_task_oracle.cpp, checked against the
 * live source by check_r4a_copies.py) with fakes for the viewer singletons,
 * on the real llinventory readers and LLSD forms, the real message system
 * built from the shipped template, the real xfer manager and LLDir:
 *
 *   set 1  task inventory files: bytes -> records (loadTaskInvFile's loop
 *          over LLInventoryObject/LLViewerInventoryItem::importLegacyStream),
 *          the scrubbed-filename rule through processTaskInv and the real
 *          LLXferManager, and a seeded random sweep of mutated files;
 *   set 2  RequestTaskInventory capability replies -> records, serials and
 *          state, and a seeded random sweep of reply bodies;
 *   set 3  serial / request-state traces: every event sequence up to length
 *          four over a reduced alphabet, per configuration, and hand-written
 *          sequences (wrap at 32767/-32768, every stuck-pending case);
 *   set 4  the task-inventory messages the viewer sends, packed by the real
 *          builder and captured as UDP datagrams on a loopback receiver,
 *          RequestXfer/AbortXfer from the real xfer manager, the capability
 *          URL and the CreateTaskInventoryItem body.
 *
 * The harness reaches the decision code only through the boundary in
 * llinventory_task_oracle_boundary.h (bnd() below): the frozen copies today,
 * the Rust decisions over FFI at W5, with the same fixtures and fakes.
 *
 * Without VKS_TASK_DUMP the test regenerates every corpus and compares it
 * with the fixtures in tests/fixtures/inventory/task (text compared line by
 * line ignoring '#' header lines; binary inputs byte for byte; the large
 * sweeps by SHA-256 of their canonical text, whole and per block of 97) and
 * reports the first differences; test 7 requires the fixture tree to hold
 * exactly the produced files plus README.md and .gitattributes; test 8 runs
 * check_r4a_copies.py. With VKS_TASK_DUMP=<dir> it writes the fixtures to
 * <dir> and fails (a dump is never a passing run). VKS_TASK_ONLY=<n> runs one
 * set and is honoured only with VKS_TASK_DEV=1.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "llinventory_task_oracle.h"
#include "llinventory_task_oracle_boundary.h"

#include "llapr.h"
#include "llcorehttputil.h"
#include "lltemplatemessagebuilder.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llmessagetemplate.h"
#include "llmessagetemplateparser.h"
#include "lltemplatemessagereader.h"
#include "llxfermanager.h"
#include "message_prehash.h"
#include "net.h"

#if LL_WINDOWS
#include "lldir_win32.h"
typedef LLDir_Win32 PlatformDir;
#elif LL_DARWIN
#include "lldir_mac.h"
typedef LLDir_Mac PlatformDir;
#else
#include "lldir_linux.h"
typedef LLDir_Linux PlatformDir;
#endif

#if LL_WINDOWS
#include "llwin32headers.h"
#include <process.h>
#define vks_getpid _getpid
#else
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#define vks_getpid getpid
#endif

namespace
{
    // Whether a process with this id is running (a work directory of a
    // running process is never removed; a reused id only keeps a stale one).
    bool process_alive(long long pid)
    {
#if LL_WINDOWS
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if (!h)
        {
            return GetLastError() == ERROR_ACCESS_DENIED;
        }
        DWORD code = 0;
        bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        return alive;
#else
        return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
    }
}

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef VKS_TASK_FIXTURE_DIR
#error VKS_TASK_FIXTURE_DIR must name indra/newview/tests/fixtures/inventory/task
#endif
#ifndef VKS_TASK_TEMPLATE_FILE
#error VKS_TASK_TEMPLATE_FILE must name scripts/messages/message_template.msg
#endif
#ifndef VKS_TASK_WORK_DIR
#error VKS_TASK_WORK_DIR must name a scratch directory in the build tree
#endif
#ifndef VKS_TASK_PYTHON
#error VKS_TASK_PYTHON must name the Python interpreter
#endif
#ifndef VKS_TASK_COPY_CHECK
#error VKS_TASK_COPY_CHECK must name indra/newview/tests/check_r4a_copies.py
#endif

namespace vto = vks_task_oracle;

// Every call into the task-inventory decision code goes through this boundary
// (llinventory_task_oracle_boundary.h): the frozen copies today, Rust at W5.
static vto::TaskInventoryBoundary& bnd() { return vto::boundary(); }

namespace
{
    //------------------------------------------------------------------
    // constants of the recording
    //------------------------------------------------------------------
    const char* const BASE_COMMIT = "6f23ef84f0 (vkstorm-rust)";
    const char* const OBJECT_BLOB = "0ade67da75e4367e1fcae9e53e8e6388ddb612bf";
    const char* const INVENTORY_BLOB = "41e2a9b23982f076e14a5de056d443438f8980b3";
    const U64 SEED_FILES = 0x52344154u;     // "R4AT"
    const U64 SEED_REPLIES = 0x52344152u;   // "R4AR"
    const int FILE_SWEEP_COUNT = 3000;
    const int REPLY_SWEEP_COUNT = 2000;
    const int SAMPLE_EVERY = 97;

#if LL_WINDOWS
    const char* const PLATFORM = "windows-msvc";
#elif LL_DARWIN
    const char* const PLATFORM = "macos-clang";
#else
    const char* const PLATFORM = "linux-gcc";
#endif

    // Patterned ids.
    LLUUID U(const char* s) { return LLUUID(s); }
    const LLUUID AGENT = U("a0000000-0000-4000-8000-000000000001");
    const LLUUID SESSION = U("a0000000-0000-4000-8000-000000000002");
    const LLUUID AGENT_GROUP = U("a0000000-0000-4000-8000-000000000003");
    const LLUUID OTHER = U("a0000000-0000-4000-8000-000000000004");
    const LLUUID OTHER_GROUP = U("a0000000-0000-4000-8000-000000000005");
    const LLUUID CREATOR = U("a0000000-0000-4000-8000-000000000006");
    const LLUUID OBJ = U("0b000000-0000-4000-8000-0000000000aa");
    const LLUUID OBJ2 = U("0b000000-0000-4000-8000-0000000000bb");
    const LLUUID ITEM_A = U("1a000000-0000-4000-8000-00000000000a");
    const LLUUID ITEM_B = U("1a000000-0000-4000-8000-00000000000b");
    const LLUUID ITEM_C = U("1a000000-0000-4000-8000-00000000000c");
    const LLUUID ITEM_D = U("1a000000-0000-4000-8000-00000000000d");
    const LLUUID ITEM_E = U("1a000000-0000-4000-8000-00000000000e");
    const LLUUID ASSET_A = U("a5000000-0000-4000-8000-00000000000a");
    const LLUUID ASSET_B = U("a5000000-0000-4000-8000-00000000000b");
    const LLUUID ASSET_C = U("a5000000-0000-4000-8000-00000000000c");
    const LLUUID ASSET_D = U("a5000000-0000-4000-8000-00000000000d");
    const LLUUID ASSET_E = U("a5000000-0000-4000-8000-00000000000e");
    const LLUUID ASSET_PENDING = U("a5000000-0000-4000-8000-0000000000ff");
    const LLUUID THUMB = U("7b000000-0000-4000-8000-000000000001");
    const LLUUID TXN = U("7a000000-0000-4000-8000-000000000001");
    const LLUUID FOLDER = U("f0000000-0000-4000-8000-000000000001");
    const LLUUID TEMPLATE_ITEM = U("7e000000-0000-4000-8000-000000000001");

    const std::string CAP_RTI = "https://sim.example/cap/rti";
    const std::string CAP_CREATE = "https://sim.example/cap/ctii";

    //------------------------------------------------------------------
    // text helpers
    //------------------------------------------------------------------
    std::string fmt(const char* format, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, format);
        vsnprintf(buf, sizeof(buf), format, ap);
        va_end(ap);
        return buf;
    }

    std::string hex(const U8* p, size_t n)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(n * 2);
        for (size_t i = 0; i < n; ++i)
        {
            out += digits[p[i] >> 4];
            out += digits[p[i] & 15];
        }
        return out;
    }
    std::string hex(const std::vector<U8>& v) { return hex(v.data(), v.size()); }

    // Bytes as a quoted string: printable ASCII as is, '"' and '\' escaped,
    // everything else \xHH.
    std::string esc(const std::string& s)
    {
        std::string out = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += (char)c;
            }
            else if (c >= 0x20 && c < 0x7f)
            {
                out += (char)c;
            }
            else
            {
                out += fmt("\\x%02x", c);
            }
        }
        return out + "\"";
    }

    std::string nm(const LLUUID& id) { return vto::uuid_name(id); }

    std::string notation(const LLSD& sd)
    {
        // one line, binary values as b16"..." (no raw bytes in the text)
        std::ostringstream s;
        LLPointer<LLSDNotationFormatter> f = new LLSDNotationFormatter();
        f->format(sd, s, LLSDFormatter::OPTIONS_PRETTY_BINARY);
        return s.str();
    }

    std::vector<U8> binary_llsd(const LLSD& sd)
    {
        std::ostringstream s;
        LLSDSerialize::toBinary(sd, s);
        std::string b = s.str();
        return std::vector<U8>(b.begin(), b.end());
    }

    // SHA-256 (FIPS 180-4) for the digests of the large sweeps.
    struct Sha256
    {
        U32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        U8 block[64];
        size_t used = 0;
        U64 total = 0;

        static U32 rotr(U32 x, int n) { return (x >> n) | (x << (32 - n)); }

        void compress(const U8* p)
        {
            static const U32 k[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
            U32 w[64];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (U32(p[i * 4]) << 24) | (U32(p[i * 4 + 1]) << 16) | (U32(p[i * 4 + 2]) << 8) | U32(p[i * 4 + 3]);
            }
            for (int i = 16; i < 64; ++i)
            {
                const U32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const U32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            U32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int i = 0; i < 64; ++i)
            {
                const U32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                const U32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }

        void update(const U8* p, size_t n)
        {
            total += n;
            while (n)
            {
                const size_t take = std::min(n, sizeof(block) - used);
                memcpy(block + used, p, take);
                used += take; p += take; n -= take;
                if (used == sizeof(block))
                {
                    compress(block);
                    used = 0;
                }
            }
        }
        void update(const std::string& s) { update((const U8*)s.data(), s.size()); }

        std::string hexdigest()
        {
            const U64 bits = total * 8;
            const U8 pad = 0x80;
            update(&pad, 1);
            const U8 zero = 0;
            while (used != 56)
            {
                update(&zero, 1);
            }
            U8 length[8];
            for (int i = 0; i < 8; ++i)
            {
                length[i] = U8(bits >> (56 - 8 * i));
            }
            update(length, 8);
            std::string out;
            for (U32 word : h)
            {
                out += fmt("%08x", word);
            }
            return out;
        }
    };

    std::string sha256(const std::string& s)
    {
        Sha256 h;
        h.update(s);
        return h.hexdigest();
    }

    // xorshift64* (as rung two's corpus generator).
    struct Rng
    {
        U64 s;
        explicit Rng(U64 seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
        U64 next()
        {
            s ^= s >> 12;
            s ^= s << 25;
            s ^= s >> 27;
            return s * 0x2545F4914F6CDD1Dull;
        }
        U32 below(U32 n) { return n ? (U32)(next() % n) : 0; }
        bool coin() { return (next() >> 63) != 0; }
    };

    //------------------------------------------------------------------
    // outputs: the fixture files of this recording
    //------------------------------------------------------------------
    struct Digest
    {
        std::string name;
        U64 count = 0;
        std::string sha;
        std::vector<std::string> chunks;
    };

    // SHA-256 of a sweep's canonical text, whole and per block of
    // SAMPLE_EVERY units (cases or traces): block k holds units
    // k*SAMPLE_EVERY .. k*SAMPLE_EVERY+SAMPLE_EVERY-1 and starts with sample k,
    // so a mismatch names the range to look at.
    struct DigestBuilder
    {
        std::string name;
        Sha256 total;
        Sha256 chunk;
        U64 count = 0;
        std::vector<std::string> chunks;

        explicit DigestBuilder(const std::string& n) : name(n) {}

        void add(const std::string& unit)
        {
            total.update(unit);
            chunk.update(unit);
            ++count;
            if (count % SAMPLE_EVERY == 0)
            {
                flush();
            }
        }

        void flush()
        {
            const U64 k = (count - 1) / SAMPLE_EVERY;
            chunks.push_back(name + fmt(" chunk %llu units %llu-%llu ", (unsigned long long)k,
                                        (unsigned long long)(k * SAMPLE_EVERY), (unsigned long long)(count - 1))
                             + chunk.hexdigest());
            chunk = Sha256();
        }

        Digest finish()
        {
            if (count % SAMPLE_EVERY)
            {
                flush();
            }
            Digest d;
            d.name = name;
            d.count = count;
            d.sha = total.hexdigest();
            d.chunks = chunks;
            return d;
        }
    };

    struct Outputs
    {
        std::map<std::string, std::string> text;              // path -> content
        std::map<std::string, std::vector<U8> > binary;       // path -> bytes
        std::vector<Digest> digests;
    };

    std::string header(const std::string& what, const std::string& seed = std::string())
    {
        std::string h;
        h += "# vkstorm-inventory task fixtures: " + what + "\n";
        h += "# generator: INTEGRATION_TEST_llinventory_task_oracle (indra/newview/tests/llinventory_task_oracle_test.cpp), VKS_TASK_DUMP\n";
        h += std::string("# viewer: ") + BASE_COMMIT + "; frozen copies of llviewerobject.cpp blob " + OBJECT_BLOB
            + " and llviewerinventory.cpp blob " + INVENTORY_BLOB + "\n";
        h += std::string("# platform: ") + PLATFORM + "\n";
        if (!seed.empty())
        {
            h += "# seed: " + seed + "\n";
        }
        return h;
    }

    //------------------------------------------------------------------
    // environment: LLDir, message system, loopback receiver
    //------------------------------------------------------------------
    class TestDir : public PlatformDir
    {
    public:
        void setTemp(const std::string& dir) { mTempDir = dir; }
    };

    struct Env
    {
        TestDir dir;
        std::string work;
        std::string cache;
        std::string temp;
        S32 rsock = -1;
        int rport = 0;
        LLHost loopback;
        std::map<std::string, LLMessageTemplate*> templates;
        LLTemplateMessageBuilder::message_template_name_map_t name_map;
        LLTemplateMessageReader::message_template_number_map_t number_map;
        LLTemplateMessageBuilder* builder = NULL;
        LLTemplateMessageReader* reader = NULL;
        bool ok = false;
    };

    bool warm_up_loopback();

    Env& env()
    {
        static Env* e = NULL;
        if (e)
        {
            return *e;
        }
        e = new Env;
        ll_init_apr();
        // A directory of this process's own under the build tree's work
        // directory, so that concurrent runs (a replay and a dump, two build
        // trees sharing a source) never share cache files. Removed at exit.
        LLFile::mkdir(VKS_TASK_WORK_DIR);
        // A crashed run never reaches atexit: remove the directories of
        // processes that are no longer running.
        {
            std::error_code ec;
            std::vector<std::filesystem::path> stale;
            for (std::filesystem::directory_iterator it(std::filesystem::path(VKS_TASK_WORK_DIR), ec), end; !ec && it != end; it.increment(ec))
            {
                const std::string leaf = it->path().filename().string();
                if (leaf.compare(0, 4, "pid-") != 0 || leaf.size() == 4 || leaf.size() > 22
                    || leaf.find_first_not_of("0123456789", 4) != std::string::npos)
                {
                    continue;
                }
                const long long pid = std::stoll(leaf.substr(4));
                if (pid != (long long)vks_getpid() && !process_alive(pid))
                {
                    stale.push_back(it->path());
                }
            }
            for (const std::filesystem::path& p : stale)
            {
                std::error_code rec;
                std::filesystem::remove_all(p, rec);
            }
        }
        e->work = std::string(VKS_TASK_WORK_DIR) + "/pid-" + std::to_string((long long)vks_getpid());
        { std::error_code ec; std::filesystem::remove_all(std::filesystem::path(e->work), ec); }
        LLFile::mkdir(e->work);
        static std::string work_to_remove = e->work;
        atexit([]()
        {
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::path(work_to_remove), ec);
        });
        e->cache = e->work + "/cache";
        e->temp = e->work + "/temp";
        LLFile::mkdir(e->cache);
        LLFile::mkdir(e->temp);
        e->dir.setTemp(e->temp);
        e->dir.setCacheDir(e->cache);
        gDirUtilp = &e->dir;

        vto::gMessageSystem = new vto::LLMessageSystem(VKS_TASK_TEMPLATE_FILE);
        ::gMessageSystem = vto::gMessageSystem;

        // An independent parse of the same template, to walk a message's
        // blocks and variables when dumping captured packets.
        std::ifstream in(VKS_TASK_TEMPLATE_FILE, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string body = ss.str();
        LLTemplateTokenizer tokens(body);
        LLTemplateParser parsed(tokens);
        for (LLTemplateParser::message_iterator it = parsed.getMessagesBegin(); it != parsed.getMessagesEnd(); ++it)
        {
            e->templates[(*it)->mName] = *it;
            e->name_map[(*it)->mName] = *it;
            e->number_map[(*it)->mMessageNumber] = *it;
        }
        e->builder = new LLTemplateMessageBuilder(e->name_map);
        e->reader = new LLTemplateMessageReader(e->number_map);

        if (start_net(e->rsock, e->rport) == 0)
        {
            e->loopback = LLHost(ip_string_to_u32("127.0.0.1"), e->rport);
            vto::gMessageSystem->enableCircuit(e->loopback, false);
            e->ok = warm_up_loopback();
        }

        vto::gAgent.mID = AGENT;
        vto::gAgent.mSessionID = SESSION;
        vto::gAgent.mGroupID = AGENT_GROUP;

        struct { LLUUID id; const char* alias; } names[] = {
            {AGENT, "agent"}, {SESSION, "session"}, {AGENT_GROUP, "agentgroup"}, {OTHER, "other"},
            {OTHER_GROUP, "othergroup"}, {CREATOR, "creator"}, {OBJ, "obj"}, {OBJ2, "obj2"},
            {ITEM_A, "A"}, {ITEM_B, "B"}, {ITEM_C, "C"}, {ITEM_D, "D"}, {ITEM_E, "E"},
            {ASSET_A, "assetA"}, {ASSET_B, "assetB"}, {ASSET_C, "assetC"}, {ASSET_D, "assetD"},
            {ASSET_E, "assetE"}, {ASSET_PENDING, "assetP"}, {THUMB, "thumb"}, {TXN, "txn"},
            {FOLDER, "folder"}, {TEMPLATE_ITEM, "template"}};
        for (const auto& n : names)
        {
            vto::set_uuid_alias(n.id, n.alias);
        }
        return *e;
    }

    // Every datagram sent to the loopback receiver so far. A marker
    // (StartPingCheck, OldestUnacked = a counter) is sent through the same
    // message-system socket and circuit after them; datagrams from one socket
    // to one loopback address arrive in order, so reading up to the marker
    // collects everything sent before it. The marker is not returned.
    U32 send_marker()
    {
        static U32 counter = 0x52340000;
        const U32 marker = ++counter;
        vto::LLMessageSystem* m = vto::gMessageSystem;
        m->::LLMessageSystem::newMessageFast(_PREHASH_StartPingCheck);
        m->nextBlockFast(_PREHASH_PingID);
        m->addU8Fast(_PREHASH_PingID, 0xa5);
        m->addU32Fast(_PREHASH_OldestUnacked, marker);
        m->::LLMessageSystem::sendMessage(env().loopback);
        return marker;
    }

    // Reads datagrams until `marker` arrives (true) or `timeout_ms` passes
    // (false); other datagrams are appended to `out`, older markers skipped.
    bool read_until_marker(U32 marker, int timeout_ms, std::vector<std::vector<U8> >& out)
    {
        const auto start = std::chrono::steady_clock::now();
        std::vector<char> buf(NET_BUFFER_SIZE);
        while (true)
        {
            S32 n = receive_packet(env().rsock, buf.data());
            if (n > 0)
            {
                std::vector<U8> p(buf.begin(), buf.begin() + n);
                // StartPingCheck: High 1 -> byte 6 is 0x01; PingID, OldestUnacked
                if (n == 12 && p[6] == 0x01 && p[7] == 0xa5)
                {
                    U32 got;
                    memcpy(&got, &p[8], 4);
                    if (got == marker)
                    {
                        return true;
                    }
                    continue; // an older marker
                }
                out.push_back(p);
                continue;
            }
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms))
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // On Windows the first datagram of a new loopback flow has been seen to
    // arrive seconds late under load. Before any capture, markers are sent
    // (and resent every 500 ms, for at most 60 s) until one arrives; after
    // that the flow is established. Nothing is recorded from this.
    bool warm_up_loopback()
    {
        std::vector<std::vector<U8> > ignored;
        for (int attempt = 0; attempt < 120; ++attempt)
        {
            if (read_until_marker(send_marker(), 500, ignored))
            {
                return true;
            }
        }
        return false;
    }

    // Every datagram sent to the loopback receiver so far. A marker
    // (StartPingCheck, OldestUnacked = a counter) is sent through the same
    // message-system socket and circuit after them; datagrams from one socket
    // to one loopback address arrive in order, so reading up to the marker
    // collects everything sent before it. The marker is not returned; a
    // marker missing after 30 s shows as an empty datagram (a failure).
    std::vector<std::vector<U8> > receive_packets()
    {
        std::vector<std::vector<U8> > out;
        if (!read_until_marker(send_marker(), 30000, out))
        {
            out.push_back(std::vector<U8>());
        }
        return out;
    }

    //------------------------------------------------------------------
    // items
    //------------------------------------------------------------------
    struct ItemSpec
    {
        LLUUID id;
        LLUUID parent = OBJ;
        LLUUID creator = CREATOR;
        LLUUID owner = AGENT;
        LLUUID last_owner = LLUUID::null;
        LLUUID group = LLUUID::null;
        bool group_owned = false;
        U32 base = PERM_ALL;
        U32 owner_mask = PERM_ALL;
        U32 group_mask = PERM_NONE;
        U32 everyone = PERM_NONE;
        U32 next = PERM_MOVE | PERM_TRANSFER;
        LLUUID asset;
        LLAssetType::EType type = LLAssetType::AT_NOTECARD;
        LLInventoryType::EType inv_type = LLInventoryType::IT_NOTECARD;
        std::string name = "Item";
        std::string desc = "";
        LLSaleInfo::EForSale sale_type = LLSaleInfo::FS_NOT;
        S32 sale_price = 10;
        U32 flags = 0;
        time_t date = 1700000000;
        LLUUID txn;
    };

    LLPermissions make_perm(const ItemSpec& s)
    {
        LLPermissions p;
        p.init(s.creator, s.owner, s.last_owner, s.group);
        if (s.group_owned)
        {
            p.setOwnerAndGroup(LLUUID::null, LLUUID::null, s.group, true);
        }
        p.initMasks(s.base, s.owner_mask, s.everyone, s.group_mask, s.next);
        return p;
    }

    LLPointer<vto::LLViewerInventoryItem> make_item(const ItemSpec& s)
    {
        LLPointer<vto::LLViewerInventoryItem> item = new vto::LLViewerInventoryItem(
            s.id, s.parent, make_perm(s), s.asset, s.type, s.inv_type, s.name, s.desc,
            LLSaleInfo(s.sale_type, s.sale_price), s.flags, s.date);
        if (s.txn.notNull())
        {
            LLTransactionID txn;
            memcpy(txn.mData, s.txn.mData, UUID_BYTES);
            item->setTransactionID(txn);
        }
        return item;
    }

    // The standard contents: A a copyable notecard, B a no-copy object.
    ItemSpec spec_a()
    {
        ItemSpec s;
        s.id = ITEM_A;
        s.asset = ASSET_A;
        s.name = "Notecard A";
        s.desc = "copyable";
        return s;
    }

    ItemSpec spec_b()
    {
        ItemSpec s;
        s.id = ITEM_B;
        s.asset = ASSET_B;
        s.type = LLAssetType::AT_OBJECT;
        s.inv_type = LLInventoryType::IT_OBJECT;
        s.base = PERM_ALL & ~PERM_COPY;
        s.owner_mask = PERM_ALL & ~PERM_COPY;
        s.next = PERM_MOVE;
        s.name = "Object B";
        s.desc = "no copy";
        return s;
    }

    // C: a notecard with A's asset, written and then stripped of its type
    // line (see item_type_missing_shares_asset).
    ItemSpec spec_c_none()
    {
        ItemSpec s = spec_a();
        s.id = ITEM_C;
        s.name = "No type C";
        return s;
    }

    std::string legacy_object(const LLUUID& id, const std::string& name, LLAssetType::EType type = LLAssetType::AT_CATEGORY,
                              const LLUUID& parent = LLUUID::null)
    {
        LLPointer<LLInventoryObject> o = new LLInventoryObject(id, parent, type, name);
        std::ostringstream s;
        o->exportLegacyStream(s, true);
        return s.str();
    }

    std::string legacy_item(const ItemSpec& spec)
    {
        LLPointer<vto::LLViewerInventoryItem> item = make_item(spec);
        std::ostringstream s;
        item->exportLegacyStream(s, true);
        return s.str();
    }

    std::string standard_file()
    {
        return legacy_object(OBJ, "Contents") + legacy_item(spec_a()) + legacy_item(spec_b());
    }

    LLSD standard_reply(S32 serial)
    {
        LLSD body;
        body["inventory_serial"] = serial;
        LLSD contents = LLSD::emptyArray();
        contents.append(make_item(spec_a())->asLLSD());
        contents.append(make_item(spec_b())->asLLSD());
        body["contents"] = contents;
        return body;
    }

    //------------------------------------------------------------------
    // dumping the object's state
    //------------------------------------------------------------------
    const char* state_letter(vto::LLViewerObject* o)
    {
        switch (o->mInvRequestState)
        {
        case vto::LLViewerObject::INVENTORY_REQUEST_STOPPED: return "S";
        case vto::LLViewerObject::INVENTORY_REQUEST_WAIT: return "W";
        case vto::LLViewerObject::INVENTORY_REQUEST_PENDING: return "P";
        case vto::LLViewerObject::INVENTORY_XFER: return "X";
        }
        return "?";
    }

    std::string inv_brief(LLInventoryObject::object_list_t* inv)
    {
        if (!inv)
        {
            return "-";
        }
        std::string out = "[";
        bool first = true;
        for (const LLPointer<LLInventoryObject>& p : *inv)
        {
            out += (first ? "" : ",") + nm(p->getUUID());
            first = false;
        }
        return out + "]";
    }

    std::string state_line(vto::LLViewerObject* o)
    {
        return fmt("st=%s ser=%d exp=%d xid=", state_letter(o), (int)o->mInventorySerialNum,
                   (int)o->mExpectedInventorySerialNum) + vto::xfer_id_text(o->mInvRequestXFerId)
            + fmt(" dirty=%d inv=", (int)o->mInventoryDirty) + inv_brief(o->mInventory);
    }

    std::string perm_text(const LLPermissions& p)
    {
        return fmt("base=%08x owner=%08x group=%08x every=%08x next=%08x", p.getMaskBase(), p.getMaskOwner(),
                   p.getMaskGroup(), p.getMaskEveryone(), p.getMaskNextOwner())
            + " creator=" + nm(p.getCreator()) + " ownerid=" + nm(p.getOwner()) + " lastowner=" + nm(p.getLastOwner())
            + " groupid=" + nm(p.getGroup()) + fmt(" groupowned=%d", (int)p.isGroupOwned());
    }

    // The object's own read helpers over its contents: getInventoryRoot,
    // getInventoryContents, countInventoryContents for every type present,
    // and getInventoryItemByAsset (both forms) for every item's asset. The
    // lookups cast every entry of the matching type to LLViewerInventoryItem
    // without a check; where a plain LLInventoryObject would be cast (an
    // inv_object of a type other than category/none), the call would read
    // past the object (undefined behaviour), so it is not made and the line
    // says so.
    std::string helpers_text(vto::LLViewerObject* obj, const std::string& indent)
    {
        LLInventoryObject::object_list_t* inv = obj->mInventory;
        std::string out = indent + "helpers root=";
        LLInventoryObject* root = bnd().getInventoryRoot(obj);
        out += root ? nm(root->getUUID()) : std::string("none");
        LLInventoryObject::object_list_t contents;
        bnd().getInventoryContents(obj, contents);
        out += " contents=" + inv_brief(&contents);
        std::vector<int> types;
        std::vector<LLUUID> assets;
        std::vector<std::pair<LLUUID, int> > typed;
        std::set<int> plain_object_types;
        if (inv)
        {
            for (const LLPointer<LLInventoryObject>& p : *inv)
            {
                int t = (int)p->LLInventoryObject::getType();
                if (std::find(types.begin(), types.end(), t) == types.end())
                {
                    types.push_back(t);
                }
                const vto::LLViewerInventoryItem* item = dynamic_cast<const vto::LLViewerInventoryItem*>(p.get());
                if (!item)
                {
                    plain_object_types.insert(t);
                    continue;
                }
                const LLUUID& a = item->LLInventoryItem::getAssetUUID();
                if (std::find(assets.begin(), assets.end(), a) == assets.end())
                {
                    assets.push_back(a);
                }
                std::pair<LLUUID, int> at(a, t);
                if (std::find(typed.begin(), typed.end(), at) == typed.end())
                {
                    typed.push_back(at);
                }
            }
        }
        out += " count{";
        for (size_t i = 0; i < types.size(); ++i)
        {
            out += fmt("%s%d:%d", i ? "," : "", types[i], (int)bnd().countInventoryContents(obj, (LLAssetType::EType)types[i]));
        }
        out += "}";
        bool asset_lookup_safe = true;
        for (int t : plain_object_types)
        {
            if (t != LLAssetType::AT_CATEGORY && t != LLAssetType::AT_NONE)
            {
                asset_lookup_safe = false;
            }
        }
        out += " byasset{";
        if (!asset_lookup_safe)
        {
            out += "not-called";
        }
        else
        {
            for (size_t i = 0; i < assets.size(); ++i)
            {
                vto::LLViewerInventoryItem* found = bnd().getInventoryItemByAsset(obj, assets[i]);
                out += (i ? "," : "") + nm(assets[i]) + ":" + (found ? nm(found->getUUID()) : std::string("none"));
            }
        }
        out += "} bytype{";
        bool first = true;
        for (const auto& at : typed)
        {
            if (at.second == LLAssetType::AT_CATEGORY)
            {
                continue; // llassert(0) in the viewer
            }
            out += first ? "" : ",";
            first = false;
            out += nm(at.first) + fmt("/%d:", at.second);
            if (plain_object_types.count(at.second))
            {
                out += "not-called";
                continue;
            }
            vto::LLViewerInventoryItem* found = bnd().getInventoryItemByAsset(obj, at.first, (LLAssetType::EType)at.second);
            out += found ? nm(found->getUUID()) : std::string("none");
        }
        // getInventoryObject / getInventoryItem for every id present (first
        // match; getInventoryItem is NULL for a category or a plain object)
        out += "} byid{";
        std::vector<LLUUID> ids;
        if (inv)
        {
            for (const LLPointer<LLInventoryObject>& p : *inv)
            {
                if (std::find(ids.begin(), ids.end(), p->getUUID()) == ids.end())
                {
                    ids.push_back(p->getUUID());
                }
            }
        }
        for (size_t i = 0; i < ids.size(); ++i)
        {
            LLInventoryObject* o = bnd().getInventoryObject(obj, ids[i]);
            LLInventoryItem* it = bnd().getInventoryItem(obj, ids[i]);
            out += (i ? "," : "") + nm(ids[i]) + ":" + (o ? (dynamic_cast<vto::LLViewerInventoryItem*>(o) ? "item" : "object") : "none")
                + "/" + (it ? (it == o ? "same" : "other") : "null");
        }
        // isAssetInInventory(item, its type) for every item (pending list or
        // contents; not called where the typed lookup would cast a plain object)
        out += "} inasset{";
        first = true;
        if (inv)
        {
            for (const LLPointer<LLInventoryObject>& p : *inv)
            {
                vto::LLViewerInventoryItem* item = dynamic_cast<vto::LLViewerInventoryItem*>(p.get());
                if (!item)
                {
                    continue;
                }
                const int t = (int)item->LLInventoryItem::getType();
                out += first ? "" : ",";
                first = false;
                out += nm(item->getUUID()) + ":";
                if (t == LLAssetType::AT_CATEGORY || plain_object_types.count(t))
                {
                    out += "not-called";
                    continue;
                }
                out += bnd().isAssetInInventory(obj, item, (LLAssetType::EType)t) ? "1" : "0";
            }
        }
        out += fmt("} state{listeners=%d dirty=%d serial=%d pending=%d}\n", (int)bnd().hasInventoryListeners(obj),
                   (int)bnd().isInventoryDirty(obj), (int)bnd().getInventorySerial(obj), (int)bnd().isInventoryPending(obj));
        return out;
    }

    // Every field of every entry, front to back, then the read helpers.
    std::string entries_text(vto::LLViewerObject* obj, const std::string& indent)
    {
        LLInventoryObject::object_list_t* inv = obj->mInventory;
        if (!inv)
        {
            return indent + "inventory null\n" + helpers_text(obj, indent);
        }
        std::string out = indent + fmt("inventory %d entries", (int)inv->size());
        out += inv->empty() ? " root none\n" : " root " + nm(inv->back()->getUUID()) + "\n";
        int i = 0;
        for (const LLPointer<LLInventoryObject>& p : *inv)
        {
            const LLInventoryObject* o = p.get();
            const vto::LLViewerInventoryItem* item = dynamic_cast<const vto::LLViewerInventoryItem*>(o);
            out += indent + fmt(" [%d] %s", i++, item ? "item" : "object");
            out += " uuid=" + nm(o->getUUID()) + " parent=" + nm(o->getParentUUID());
            out += fmt(" type=%d", (int)o->LLInventoryObject::getType());
            out += " name=" + esc(o->LLInventoryObject::getName());
            out += " thumb=" + nm(o->LLInventoryObject::getThumbnailUUID());
            out += fmt(" fav=%d", (int)o->LLInventoryObject::getIsFavorite());
            out += " runtime=" + esc(o->LLInventoryObject::getRuntime());
            out += fmt(" created=%lld", (long long)o->LLInventoryObject::getCreationDate());
            if (item)
            {
                out += " asset=" + nm(item->LLInventoryItem::getAssetUUID());
                out += " desc=" + esc(item->LLInventoryItem::getDescription());
                out += fmt(" invtype=%d flags=%08x", (int)item->LLInventoryItem::getInventoryType(), item->LLInventoryItem::getFlags());
                out += fmt(" sale=%d/%d", (int)item->LLInventoryItem::getSaleInfo().getSaleType(),
                           item->LLInventoryItem::getSaleInfo().getSalePrice());
                out += " " + perm_text(item->LLInventoryItem::getPermissions());
                out += fmt(" complete=%d", (int)item->mIsComplete);
                out += " txn=" + nm(item->mTransactionID);
                out += fmt(" running=%d faulted=%d crc=%08x", (int)item->mIsRunning, (int)item->mIsFaulted,
                           item->LLInventoryItem::getCRC32());
            }
            out += "\n";
        }
        return out + helpers_text(obj, indent);
    }

    //------------------------------------------------------------------
    // listener
    //------------------------------------------------------------------
    struct RecListener : public vto::LLVOInventoryListener
    {
        int mIndex;
        explicit RecListener(int index) : mIndex(index) {}
        void inventoryChanged(vto::LLViewerObject* object, LLInventoryObject::object_list_t* inventory,
                              S32 serial_num, void* user_data) override
        {
            vto::recorder().add(fmt("callback L%d ser=%d inv=", mIndex, (int)serial_num) + inv_brief(inventory));
        }
    };

    std::string take_actions()
    {
        std::string out;
        for (const std::string& a : vto::recorder().actions)
        {
            out += (out.empty() ? "" : "; ") + a;
        }
        vto::recorder().actions.clear();
        return out.empty() ? "-" : out;
    }

    //------------------------------------------------------------------
    // message helpers
    //------------------------------------------------------------------
    // Builds a ReplyTaskInventory with the template builder, reads it with
    // the template reader (both from the shipped template) and hands it to
    // processTaskInv, whose getters read the injected message.
    std::vector<U8> build_reply(const LLUUID& task, S16 serial, const std::string& filename)
    {
        Env& e = env();
        e.builder->newMessage(_PREHASH_ReplyTaskInventory);
        e.builder->nextBlock(_PREHASH_InventoryData);
        e.builder->addUUID(_PREHASH_TaskID, task);
        e.builder->addS16(_PREHASH_Serial, serial);
        e.builder->addString(_PREHASH_Filename, filename);
        std::vector<U8> buf(MAX_BUFFER_SIZE);
        U32 n = e.builder->buildMessage(buf.data(), MAX_BUFFER_SIZE, 0);
        e.builder->clearMessage();
        buf.resize(n);
        return buf;
    }

    struct Injected
    {
        Injected() { vto::gMessageSystem->mInjected = env().reader; }
        ~Injected() { vto::gMessageSystem->mInjected = NULL; }
    };

    void deliver_reply(const LLUUID& task, S16 serial, const std::string& filename)
    {
        std::vector<U8> buf = build_reply(task, serial, filename);
        S32 n = (S32)buf.size();
        Env& e = env();
        buf.resize(MAX_BUFFER_SIZE);
        e.reader->validateMessage(buf.data(), n, LLHost(), true);
        e.reader->readMessage(buf.data(), LLHost());
        Injected injected;
        bnd().processTaskInv(vto::gMessageSystem);
    }

    // What processTaskInv reads from the Filename field when the sender
    // packed `filename` (Variable 1: the builder's own limit applies).
    std::string read_back_filename(const std::string& filename)
    {
        std::vector<U8> buf = build_reply(OBJ, 0, filename);
        S32 n = (S32)buf.size();
        Env& e = env();
        buf.resize(MAX_BUFFER_SIZE);
        e.reader->validateMessage(buf.data(), n, LLHost(), true);
        e.reader->readMessage(buf.data(), LLHost());
        std::string out;
        e.reader->getString(_PREHASH_InventoryData, _PREHASH_Filename, out);
        return out;
    }

    // One captured datagram: the wire bytes with the packet id (bytes 1-4)
    // zeroed, the zero-expanded body, and every block and variable of the
    // message as raw bytes. The random xfer id of RequestXfer/AbortXfer is
    // replaced by its ordinal.
    std::string packet_text(const std::vector<U8>& wire_in, const std::map<U64, std::string>& xfer_ids)
    {
        if (wire_in.empty())
        {
            return "  packet <MARKER NOT RECEIVED WITHIN 30 s>\n";
        }
        Env& e = env();
        std::vector<U8> wire = wire_in;
        for (size_t i = 1; i < 5 && i < wire.size(); ++i)
        {
            wire[i] = 0;
        }
        std::vector<U8> buf(MAX_BUFFER_SIZE * 2);
        memcpy(buf.data(), wire.data(), wire.size());
        U8* p = buf.data();
        S32 size = (S32)wire.size();
        const bool zerocoded = (wire[0] & LL_ZERO_CODE_FLAG) != 0;
        vto::gMessageSystem->zeroCodeExpand(&p, &size);
        std::vector<U8> body(p, p + size);

        LLTemplateMessageReader* r = e.reader;
        std::vector<U8> rbuf = body;
        rbuf.resize(MAX_BUFFER_SIZE);
        bool valid = r->validateMessage(rbuf.data(), (S32)body.size(), LLHost(), true);
        bool read = valid && r->readMessage(rbuf.data(), LLHost());
        std::string name = read ? r->getMessageName() : "?";
        std::string fields;
        bool random_id = false;
        if (read && e.templates.count(name))
        {
            const LLMessageTemplate* t = e.templates[name];
            for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit)
            {
                const LLMessageBlock* b = *bit;
                S32 count = r->getNumberOfBlocks(b->mName);
                fields += fmt("    %s count=%d\n", b->mName, count);
                for (S32 rep = 0; rep < count; ++rep)
                {
                    for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit)
                    {
                        const LLMessageVariable* var = *vit;
                        S32 vsize = r->getSize(b->mName, rep, var->getName());
                        std::vector<U8> raw(vsize > 0 ? vsize : 0);
                        if (vsize > 0)
                        {
                            r->getBinaryData(b->mName, var->getName(), raw.data(), vsize, rep);
                        }
                        std::string shown = hex(raw);
                        if ((name == "RequestXfer" || name == "AbortXfer") && std::string(var->getName()) == "ID" && raw.size() == 8)
                        {
                            U64 id;
                            memcpy(&id, raw.data(), 8);
                            std::map<U64, std::string>::const_iterator it = xfer_ids.find(id);
                            shown = "<xfer id " + (it == xfer_ids.end() ? std::string("unknown") : it->second) + ">";
                            // the id sits right after the 6-byte header and the 4-byte Low number
                            if (body.size() >= 18 && memcmp(&body[10], raw.data(), 8) == 0)
                            {
                                memset(&body[10], 0, 8);
                                random_id = true;
                            }
                        }
                        else if (var->getType() == MVT_VARIABLE)
                        {
                            shown += " " + esc(std::string(raw.begin(), raw.end()));
                        }
                        fields += fmt("      [%d] %s = ", rep, var->getName()) + shown + "\n";
                    }
                }
            }
        }
        std::string out = fmt("  packet %s flags=%02x zerocoded=%d wirelen=%d\n", name.c_str(), wire[0], (int)zerocoded,
                              random_id && zerocoded ? -1 : (int)wire.size());
        if (random_id && zerocoded)
        {
            out += "    wire <zero-coded with the random xfer id: see body>\n";
        }
        else if (random_id)
        {
            memset(&wire[10], 0, 8);
            out += "    wire " + hex(wire) + "\n";
        }
        else
        {
            out += "    wire " + hex(wire) + "\n";
        }
        out += "    body " + hex(body) + "\n";
        return out + fields;
    }

    std::string packets_text(const std::vector<std::vector<U8> >& packets, const std::map<U64, std::string>& xfer_ids)
    {
        std::string out = fmt("  packets %d\n", (int)packets.size());
        for (const std::vector<U8>& p : packets)
        {
            out += packet_text(p, xfer_ids);
        }
        return out;
    }

    //------------------------------------------------------------------
    // a test object
    //------------------------------------------------------------------
    struct Scene
    {
        vto::LLViewerRegion region;
        vto::LLViewerObject* obj = NULL;
        std::vector<RecListener*> listeners;
        std::vector<std::string> written; // cache files the scene wrote

        Scene(bool caps, int listener_count, const LLHost& host = LLHost())
        {
            if (caps)
            {
                region.mCaps["RequestTaskInventory"] = CAP_RTI;
                region.mCaps["CreateTaskInventoryItem"] = CAP_CREATE;
            }
            region.mHost = host;
            obj = new vto::LLViewerObject(OBJ, 4321, &region);
            vto::gObjectList.mObjects[OBJ] = obj;
            for (int i = 0; i < listener_count; ++i)
            {
                RecListener* l = new RecListener(i + 1);
                listeners.push_back(l);
                bnd().registerInventoryListener(obj, l, NULL);
            }
            vto::LLSelectMgr::getInstance()->mSelectedObject = NULL;
            vto::reset_fake_xfers();
            vto::recorder().actions.clear();
        }

        ~Scene()
        {
            vto::abandon_all();
            vto::gObjectList.mObjects.erase(OBJ);
            delete obj;
            for (RecListener* l : listeners)
            {
                delete l;
            }
            for (const std::string& f : written)
            {
                LLFile::remove(f, ENOENT);
            }
            vto::LLSelectMgr::getInstance()->mSelectedObject = NULL;
            vto::reset_fake_xfers();
            vto::recorder().actions.clear();
        }

        std::string cache_path(const std::string& name) { return gDirUtilp->getExpandedFilename(LL_PATH_CACHE, name); }

        void write_file(const std::string& path, const std::string& bytes)
        {
            std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
            f.write(bytes.data(), bytes.size());
            if (f.good())
            {
                written.push_back(path);
            }
            else if (getenv("VKS_TASK_VERBOSE"))
            {
                std::cerr << "write_file failed " << path << " errno=" << errno << std::endl;
            }
        }
    };

    bool file_exists(const std::string& path)
    {
        llstat st;
        return LLFile::stat(path, &st) == 0;
    }

    //------------------------------------------------------------------
    // the event language of the traces
    //------------------------------------------------------------------
    // The oldest fake xfer that has neither completed nor been aborted.
    int oldest_live_xfer(bool newest = false)
    {
        std::deque<vto::FakeXfer>& xs = vto::fake_xfers();
        int found = -1;
        for (int i = 0; i < (int)xs.size(); ++i)
        {
            if (!xs[i].aborted)
            {
                found = i;
                if (!newest)
                {
                    break;
                }
            }
        }
        return found;
    }

    std::string serial_file_name(S16 serial) { return fmt("inv_s%d.tmp", (int)serial); }

    void complete_xfer(Scene& s, const std::string& how, bool newest)
    {
        int i = oldest_live_xfer(newest);
        if (i < 0)
        {
            vto::recorder().add("xfer.complete none-live");
            return;
        }
        vto::FakeXfer x = vto::fake_xfers()[i];
        vto::fake_xfers().erase(vto::fake_xfers().begin() + i);
        S32 code = 0;
        if (how == "ok")
        {
            s.write_file(x.local_filename, standard_file());
        }
        else if (how == "nofile")
        {
            LLFile::remove(x.local_filename, ENOENT);
        }
        else
        {
            code = -1;
        }
        vto::recorder().add(fmt("xfer.complete id=%llu code=%d", (unsigned long long)x.id, (int)code));
        x.callback(x.user_data, code, LLExtStat::NONE);
    }

    void update_item(Scene& s, const ItemSpec& spec, U8 key, bool is_new)
    {
        LLPointer<vto::LLViewerInventoryItem> item = make_item(spec);
        bnd().updateInventory(s.obj, item, key, is_new);
    }

    void create_item(Scene& s, bool succeed)
    {
        size_t before = vto::pending_http_count();
        LLPermissions perm;
        perm.init(AGENT, AGENT, LLUUID::null, LLUUID::null);
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_MOVE | PERM_TRANSFER);
        bnd().createInventoryItem(s.obj, LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL, 0, "New Script", "", perm, LLSD(),
            [](bool success, const LLSD& result)
            {
                vto::recorder().add(fmt("create.callback success=%d message=", (int)success)
                                    + esc(result["message"].asString()));
            });
        if (vto::pending_http_count() > before)
        {
            LLSD body;
            body["success"] = succeed;
            vto::complete_http(body, ::LLCore::HttpStatus(200), "POST");
        }
    }

    // Applies one event; returns false for an unknown event name.
    bool apply(Scene& s, const std::string& ev)
    {
        vto::LLViewerObject* o = s.obj;
        if (ev == "req") { bnd().requestInventory(o); return true; }
        if (ev == "dirty") { bnd().dirtyInventory(o); return true; }
        if (ev == "timer") { vto::fire_timer(); return true; }
        if (ev == "die") { vto::gObjectList.mObjects.erase(OBJ); vto::recorder().add("object.gone"); return true; }
        if (ev == "revive") { vto::gObjectList.mObjects[OBJ] = o; vto::recorder().add("object.back"); return true; }
        if (ev == "unlisten")
        {
            if (!s.listeners.empty()) { bnd().removeInventoryListener(o, s.listeners.back()); delete s.listeners.back(); s.listeners.pop_back(); }
            return true;
        }
        if (ev == "listen")
        {
            RecListener* l = new RecListener((int)s.listeners.size() + 1);
            s.listeners.push_back(l);
            bnd().registerInventoryListener(o, l, NULL);
            return true;
        }
        if (ev == "select")
        {
            vto::LLSelectMgr* sm = vto::LLSelectMgr::getInstance();
            sm->mSelectedObject = o;
            sm->mSelectedPermissions.init(CREATOR, OTHER, LLUUID::null, OTHER_GROUP);
            sm->mSelectedPermissions.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL);
            return true;
        }
        if (ev == "notowner") { o->mFakeYouOwner = false; return true; }
        if (ev == "clearlisteners") { bnd().clearInventoryListeners(o); return true; }
        if (ev.compare(0, 4, "rep.") == 0)
        {
            S16 serial;
            std::string name;
            if (ev == "rep.lo") serial = (S16)(o->mExpectedInventorySerialNum - 1);
            else if (ev == "rep.cur") serial = o->mInventorySerialNum;
            else if (ev == "rep.exp" || ev == "rep.none") serial = o->mExpectedInventorySerialNum;
            else if (ev == "rep.hi") serial = (S16)(o->mExpectedInventorySerialNum + 1);
            else return false;
            name = ev == "rep.none" ? std::string() : serial_file_name(serial);
            vto::recorder().add(fmt("in ReplyTaskInventory serial=%d filename=", (int)serial) + esc(name));
            deliver_reply(OBJ, serial, name);
            return true;
        }
        if (ev.compare(0, 4, "rep=") == 0)
        {
            S16 serial = (S16)atoi(ev.c_str() + 4);
            std::string name = serial_file_name(serial);
            vto::recorder().add(fmt("in ReplyTaskInventory serial=%d filename=", (int)serial) + esc(name));
            deliver_reply(OBJ, serial, name);
            return true;
        }
        if (ev == "xfer.ok") { complete_xfer(s, "ok", false); return true; }
        if (ev == "xfer.ok.newest") { complete_xfer(s, "ok", true); return true; }
        if (ev == "xfer.err") { complete_xfer(s, "err", false); return true; }
        if (ev == "xfer.nofile") { complete_xfer(s, "nofile", false); return true; }
        if (ev == "cap.ok") { vto::complete_http(standard_reply(o->mExpectedInventorySerialNum), ::LLCore::HttpStatus(200), "GET"); return true; }
        if (ev == "cap.stale") { vto::complete_http(standard_reply((S16)(o->mExpectedInventorySerialNum - 1)), ::LLCore::HttpStatus(200), "GET"); return true; }
        if (ev.compare(0, 4, "cap=") == 0) { vto::complete_http(standard_reply(atoi(ev.c_str() + 4)), ::LLCore::HttpStatus(200), "GET"); return true; }
        if (ev == "cap.304") { vto::complete_http(LLSD(), ::LLCore::HttpStatus(304), "GET"); return true; }
        if (ev == "cap.err") { vto::complete_http(LLSD(), ::LLCore::HttpStatus(500), "GET"); return true; }
        if (ev == "cap.nocontents")
        {
            LLSD body;
            body["inventory_serial"] = (S32)o->mExpectedInventorySerialNum;
            vto::complete_http(body, ::LLCore::HttpStatus(200), "GET");
            return true;
        }
        if (ev == "upd.known") { ItemSpec a = spec_a(); a.desc = "edited"; update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); return true; }
        if (ev == "upd.guess") { ItemSpec c = spec_a(); c.id = ITEM_C; c.asset = ASSET_C; c.name = "Guessed C"; update_item(s, c, TASK_INVENTORY_ITEM_KEY, false); return true; }
        if (ev == "upd.new") { ItemSpec d = spec_a(); d.id = ITEM_D; d.asset = ASSET_D; d.name = "New D"; update_item(s, d, TASK_INVENTORY_ITEM_KEY, true); return true; }
        if (ev == "upd.asset") { ItemSpec a = spec_a(); a.id = ITEM_E; a.desc = "by asset"; update_item(s, a, TASK_INVENTORY_ASSET_KEY, false); return true; }
        if (ev == "remove") { bnd().removeInventory(o, ITEM_A); return true; }
        if (ev == "move.nocopy") { bnd().moveInventory(o, LLUUID::null, ITEM_B); return true; }
        if (ev == "move.copy") { bnd().moveInventory(o, LLUUID::null, ITEM_A); return true; }
        if (ev == "create.ok") { create_item(s, true); return true; }
        if (ev == "create.fail") { create_item(s, false); return true; }
        if (ev.compare(0, 11, "set.serial=") == 0) { o->mInventorySerialNum = (S16)atoi(ev.c_str() + 11); return true; }
        if (ev.compare(0, 13, "set.expected=") == 0) { o->mExpectedInventorySerialNum = (S16)atoi(ev.c_str() + 13); return true; }
        return false;
    }

    // Runs one trace; the text is one header line and one line per event.
    // The task inventory files of the scene still in the cache (a file is
    // removed only by a successful loadTaskInvFile; survey 4 section 5.8).
    std::string cache_text(Scene& s)
    {
        std::vector<std::string> names;
        for (const std::string& f : s.written)
        {
            if (!file_exists(f))
            {
                continue;
            }
            size_t slash = f.find_last_of("/\\");
            std::string name = slash == std::string::npos ? f : f.substr(slash + 1);
            if (std::find(names.begin(), names.end(), name) == names.end())
            {
                names.push_back(name);
            }
        }
        std::string out = "[";
        for (size_t i = 0; i < names.size(); ++i)
        {
            out += (i ? "," : "") + names[i];
        }
        return out + "]";
    }

    std::string run_trace(bool caps, int listeners, const std::vector<std::string>& events, const std::string& label = std::string())
    {
        std::string out = "trace " + std::string(caps ? "cap" : "udp") + fmt("/L%d", listeners);
        if (!label.empty())
        {
            out += " " + label;
        }
        out += " :";
        for (const std::string& e : events)
        {
            out += " " + e;
        }
        out += "\n";
        Scene s(caps, listeners);
        for (const std::string& e : events)
        {
            if (!apply(s, e))
            {
                out += " " + e + " | UNKNOWN EVENT\n";
                continue;
            }
            std::string actions = take_actions();
            out += " " + e + " | " + state_line(s.obj) + " cache=" + cache_text(s) + " | " + actions + "\n";
        }
        return out;
    }

    //------------------------------------------------------------------
    // set 1: task inventory files
    //------------------------------------------------------------------
    struct FileCase
    {
        std::string label;
        std::string bytes;
        bool via_xfer = false; // also through processTaskInvFile with a pending list
    };

    std::string replace_once(std::string s, const std::string& from, const std::string& to)
    {
        size_t p = s.find(from);
        if (p != std::string::npos)
        {
            s.replace(p, from.size(), to);
        }
        return s;
    }

    std::string replace_all(std::string s, const std::string& from, const std::string& to)
    {
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos)
        {
            s.replace(p, from.size(), to);
            p += to.size();
        }
        return s;
    }

    // A desc line "\t\tdesc\t<value>|" of exactly n bytes (without the newline).
    std::string desc_line_of(size_t n)
    {
        const std::string head = "\t\tdesc\t";
        size_t value = n - head.size() - 1;
        return head + std::string(value, 'd') + "|";
    }

    std::vector<FileCase> file_cases()
    {
        std::vector<FileCase> c;
        const std::string obj = legacy_object(OBJ, "Contents");
        const std::string a = legacy_item(spec_a());
        const std::string b = legacy_item(spec_b());
        ItemSpec sc = spec_a();
        sc.id = ITEM_C;
        sc.asset = ASSET_C;
        sc.type = LLAssetType::AT_LSL_TEXT;
        sc.inv_type = LLInventoryType::IT_LSL;
        sc.name = "Script C";
        sc.base = PERM_MOVE | PERM_TRANSFER; // not unrestricted: written with shadow_id
        sc.owner_mask = PERM_MOVE | PERM_TRANSFER;
        const std::string cs = legacy_item(sc);
        const std::string normal = obj + a + b + cs;

        auto add = [&c](const std::string& label, const std::string& bytes, bool via_xfer = false)
        {
            FileCase f;
            f.label = label;
            f.bytes = bytes;
            f.via_xfer = via_xfer;
            c.push_back(f);
        };

        add("empty", "");
        add("whitespace_only", "  \t \n\t\n");
        add("newline_only", "\n");
        add("object_only", obj);
        add("normal", normal, true);
        add("item_before_object", a + obj + b);
        add("items_only", a + b);
        add("two_objects", obj + a + legacy_object(OBJ2, "Second", LLAssetType::AT_CATEGORY, OBJ) + b);
        add("object_renamed", legacy_object(OBJ, "Not Contents"));
        add("object_texture_type", legacy_object(OBJ, "Contents", LLAssetType::AT_TEXTURE));
        add("blank_line_between_blocks", obj + "\n" + a + b);
        add("whitespace_line_between_blocks", obj + a + " \t\n" + b);
        add("blank_line_after_permissions", replace_once(a, "\t}\n\t\tasset_id", "\t}\n\n\t\tasset_id") + b);
        add("blank_line_after_sale_info", replace_once(a, "\t}\n\t\tname", "\t}\n\n\t\tname") + b);
        add("crlf", replace_all(normal, "\n", "\r\n"));
        add("no_trailing_newline", normal.substr(0, normal.size() - 1));
        add("eof_inside_item", obj + a.substr(0, a.size() / 2));
        add("eof_after_keyword", obj + "\tinv_item\t0");
        add("keyword_without_block", obj + "\tinv_item\t0\n" + b);
        for (size_t n : {253, 254, 255, 300})
        {
            ItemSpec m = spec_b();
            std::string bb = legacy_item(m);
            size_t p = bb.find("\t\tdesc\t");
            size_t e = bb.find('\n', p);
            bb.replace(p, e - p, desc_line_of(n));
            add(fmt("desc_line_%d", (int)n), obj + a + bb + cs);
        }
        // A top-level line of exactly n characters (an unknown token) between
        // the blocks: loadTaskInvFile's own getline(buffer, 255) stops the
        // file at 255 characters or more.
        for (size_t n : {253, 254, 255, 256, 300})
        {
            add(fmt("toplevel_line_%d", (int)n), obj + a + "\t" + std::string(n - 1, 't') + "\n" + b + cs);
        }
        {
            // an item without a type line keeps AT_NONE; it shares A's asset
            std::string none = legacy_item(spec_c_none());
            add("item_type_missing_shares_asset", obj + a + replace_once(none, "\t\ttype\tnotecard\n", "") + b);
            // two items with A's id: the first match wins in every lookup and deletion
            ItemSpec dup = spec_a();
            dup.name = "Duplicate A";
            dup.asset = ASSET_E;
            add("duplicate_item_id", obj + a + legacy_item(dup) + b);
        }
        {
            std::string meta = "\t\tmetadata\t<llsd><map><key>script</key><map><key>runtime</key><string>"
                + std::string(240, 'r') + "</string></map></map></llsd>\n|\n";
            add("metadata_line_over_254", obj + replace_once(a, "\t\tasset_id", meta + "\t\tasset_id") + b);
        }
        {
            std::string unknown;
            for (int i = 0; i < 24; ++i) unknown += fmt("\tjunk%d\t0\n", i);
            // the limit is 25 unknown top-level tokens: 24, 25 and 26
            add("unknown_24", unknown + normal);
            add("unknown_24_between", obj + a + unknown + b);
            unknown += "\tjunk24\t0\n";
            add("unknown_25", unknown + normal);
            add("unknown_26", unknown + "\tjunk25\t0\n" + normal);
            add("unknown_25_between", obj + a + unknown + b);
            add("unknown_26_between", obj + a + unknown + "\tjunk25\n" + b);
        }
        add("unknown_inside_block", obj + replace_once(a, "\t\tflags", "\t\tbogus_key\tvalue\n\t\tflags") + b);
        add("open_brace_top_level", "\t{\n" + normal);
        add("close_brace_top_level", "\t}\n" + normal);
        add("keyword_with_suffix", obj + replace_once(a, "inv_item", "inv_itemx") + b);
        add("missing_close_brace", obj + replace_once(a, "\t\tcreation_date\t1700000000\n\t}\n", "\t\tcreation_date\t1700000000\n") + b);
        add("nul_in_line", obj + std::string("\tinv_item\0junk\n", 15) + b);
        {
            ItemSpec s = spec_a();
            s.base = PERM_MOVE | PERM_TRANSFER;
            std::string shadow = legacy_item(s); // written with shadow_id
            add("shadow_id", obj + shadow);
            std::string both = replace_once(a, "\t\tasset_id\t", "\t\tshadow_id\t" + ASSET_E.asString() + "\n\t\tasset_id\t");
            add("shadow_then_asset", obj + both);
            std::string both2 = replace_once(a, "\t\ttype\t", "\t\tshadow_id\t" + ASSET_E.asString() + "\n\t\ttype\t");
            add("asset_then_shadow", obj + both2);
        }
        {
            std::string md = "\t\tmetadata\t<llsd><map><key>favorite</key><map><key>toggled</key><boolean>1</boolean></map>"
                "<key>script</key><map><key>runtime</key><string>lua</string></map>"
                "<key>thumbnail</key><map><key>asset_id</key><uuid>" + THUMB.asString() + "</uuid></map></map></llsd>\n";
            add("metadata_all_268_bytes", obj + replace_once(a, "\t\tasset_id", md + "\t\tasset_id") + b);
            std::string md_tf = "\t\tmetadata\t<llsd><map><key>favorite</key><map><key>toggled</key><boolean>1</boolean></map>"
                "<key>thumbnail</key><map><key>asset_id</key><uuid>" + THUMB.asString() + "</uuid></map></map></llsd>\n|\n";
            add("metadata_thumbnail_favorite", obj + replace_once(a, "\t\tasset_id", md_tf + "\t\tasset_id") + b);
            std::string md_rt = "\t\tmetadata\t<llsd><map><key>script</key><map><key>runtime</key><string>lua</string></map></map></llsd>\n|\n";
            add("metadata_runtime", obj + replace_once(a, "\t\tasset_id", md_rt + "\t\tasset_id") + b);
            {
                ItemSpec m = spec_a();
                LLPointer<vto::LLViewerInventoryItem> mi = make_item(m);
                mi->setThumbnailUUID(THUMB);
                std::ostringstream w;
                mi->exportLegacyStream(w, true); // the writer's own metadata line and orphan '|'
                add("metadata_written_by_exporter", obj + w.str() + b);
            }
            add("metadata_not_llsd", obj + replace_once(a, "\t\tasset_id", "\t\tmetadata\tnot-llsd\n\t\tasset_id") + b);
            add("metadata_malformed", obj + replace_once(a, "\t\tasset_id", "\t\tmetadata\t<llsd><map><key>x\n\t\tasset_id") + b);
            add("object_metadata", replace_once(obj, "\t\tname", "\t\tmetadata\t<llsd><map><key>thumbnail</key><map><key>asset_id</key><uuid>" + THUMB.asString() + "</uuid></map></map></llsd>\n\t\tname") + a);
        }
        auto with_name = [&](const std::string& line) { return obj + replace_once(a, "\t\tname\tNotecard A|", line) + b; };
        add("name_empty", with_name("\t\tname\t|"));
        add("name_pipe_only", with_name("\t\tname\t||"));
        add("name_no_pipe", with_name("\t\tname\tNo pipe"));
        add("name_space_separator", with_name("\t\tname Spaced|"));
        add("name_leading_tabs", with_name("\t\tname\t\t\tTabbed|"));
        add("name_leading_spaces", with_name("\t\tname\t   Spaced|"));
        add("name_inner_tab", with_name("\t\tname\tIn\tside|"));
        add("name_utf8", with_name("\t\tname\tCaf\xc3\xa9 \xe2\x9c\x93|"));
        add("name_control", with_name("\t\tname\tCtl\x01\x7f|"));
        add("name_long_80", with_name("\t\tname\t" + std::string(80, 'n') + "|"));
        add("name_localizable", with_name("\t\tname\tNew Script|"));
        auto with_desc = [&](const std::string& line) { return obj + replace_once(a, "\t\tdesc\tcopyable|", line) + b; };
        add("desc_empty", with_desc("\t\tdesc\t|"));
        add("desc_no_pipe", with_desc("\t\tdesc\tno pipe"));
        add("desc_pipe_inside", with_desc("\t\tdesc\ta|b|"));
        add("desc_utf8", with_desc("\t\tdesc\t\xc3\xa9t\xc3\xa9|"));
        add("object_name_empty", replace_once(obj, "Contents|", "|") + a);
        add("object_name_spaces", replace_once(obj, "\t\tname\tContents|", "\t\tname\t   Spaced Object  |") + a);
        auto with_line = [&](const std::string& from, const std::string& to) { return obj + replace_once(a, from, to) + b; };
        add("invtype_mismatch", with_line("\t\tinv_type\tnotecard", "\t\tinv_type\ttexture"));
        add("type_keyword_only", with_line("\t\ttype\tnotecard", "\t\ttype"));
        add("name_keyword_only", with_name("\t\tname"));
        add("desc_keyword_only", with_desc("\t\tdesc"));
        add("invtype_missing",with_line("\t\tinv_type\tnotecard\n", ""));
        add("invtype_unknown", with_line("\t\tinv_type\tnotecard", "\t\tinv_type\tbogus"));
        add("type_unknown", with_line("\t\ttype\tnotecard", "\t\ttype\tbogus"));
        add("sale_perm_mask_none", with_line("\t\tsale_price\t10\n", "\t\tsale_price\t10\n\t\tperm_mask\t00000000\n"));
        add("sale_perm_mask_nocopy", with_line("\t\tsale_price\t10\n", "\t\tsale_price\t10\n\t\tperm_mask\t00082000\n"));
        add("sale_perm_mask_full", with_line("\t\tsale_price\t10\n", "\t\tsale_price\t10\n\t\tperm_mask\t7fffffff\n"));
        add("sale_for_sale", with_line("\t\tsale_type\tnot", "\t\tsale_type\tcopy"));
        add("sale_price_negative", with_line("\t\tsale_price\t10", "\t\tsale_price\t-5"));
        add("flags_0x", with_line("\t\tflags\t00000000", "\t\tflags\t0x80000000"));
        add("flags_minus_one", with_line("\t\tflags\t00000000", "\t\tflags\t-1"));
        add("flags_ffffffff", with_line("\t\tflags\t00000000", "\t\tflags\tffffffff"));
        add("flags_nonhex", with_line("\t\tflags\t00000000", "\t\tflags\tzz"));
        add("date_negative", with_line("\t\tcreation_date\t1700000000", "\t\tcreation_date\t-5"));
        add("date_int_max", with_line("\t\tcreation_date\t1700000000", "\t\tcreation_date\t2147483647"));
        add("date_over_int_max", with_line("\t\tcreation_date\t1700000000", "\t\tcreation_date\t4000000000"));
        add("perm_masks_raw", with_line("\t\tnext_owner_mask\t", "\t\tnext_owner_mask\tffffffff\n\t\tbogus_next\t"));
        add("group_owned", with_line("\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n", "\t\tgroup_id\t" + OTHER_GROUP.asString() + "\n\t\tgroup_owned\t1\n"));
        add("uuid_uppercase", with_line("\t\titem_id\t" + ITEM_A.asString(), "\t\titem_id\t" + replace_all(ITEM_A.asString(), "a", "A")));
        add("uuid_bad", with_line("\t\titem_id\t" + ITEM_A.asString(), "\t\titem_id\tnot-a-uuid"));
        {
            ItemSpec t = spec_a();
            t.id = ITEM_D;
            t.asset = ASSET_PENDING;
            t.type = LLAssetType::AT_TEXTURE;
            t.inv_type = LLInventoryType::IT_TEXTURE;
            t.name = "Pending texture";
            add("pending_cleared", obj + a + legacy_item(t) + b, true);
            add("pending_none", obj + a + b, true);
        }
        {
            std::string many = obj;
            for (int i = 0; i < 100; ++i)
            {
                ItemSpec m = spec_a();
                m.id = LLUUID(fmt("1a000000-0000-4000-8000-%012x", 0x100 + i).c_str());
                m.asset = LLUUID(fmt("a5000000-0000-4000-8000-%012x", 0x100 + i).c_str());
                m.name = fmt("Item %03d", i);
                many += legacy_item(m);
            }
            add("hundred_items", many);
        }
        return c;
    }

    std::string run_file_case(const FileCase& fc, const std::string& input_path)
    {
        std::string out = "case " + fc.label + " input=" + input_path + fmt(" bytes=%d", (int)fc.bytes.size())
            + " sha256=" + sha256(fc.bytes) + "\n";
        {
            Scene s(false, 1);
            const std::string name = "task_" + fc.label + ".inv";
            const std::string path = s.cache_path(name);
            s.write_file(path, fc.bytes);
            bool returned = bnd().loadTaskInvFile(s.obj, name);
            out += fmt(" direct returned=%d removed=%d ", (int)returned, (int)!file_exists(path)) + state_line(s.obj)
                + " | " + take_actions() + "\n";
            out += entries_text(s.obj, " ");
        }
        if (fc.via_xfer)
        {
            Scene s(false, 1);
            s.obj->mPendingInventoryItemsIDs.push_back(ASSET_PENDING);
            s.obj->mPendingInventoryItemsIDs.push_back(ASSET_E);
            s.obj->mPendingInventoryItemsIDs.push_back(ASSET_A);
            bnd().requestInventory(s.obj);
            deliver_reply(OBJ, 1, "task_" + fc.label + ".inv");
            int i = oldest_live_xfer();
            if (i >= 0)
            {
                vto::FakeXfer x = vto::fake_xfers()[i];
                vto::fake_xfers().erase(vto::fake_xfers().begin() + i);
                s.write_file(x.local_filename, fc.bytes);
                x.callback(x.user_data, 0, LLExtStat::NONE);
            }
            std::string pending;
            for (const LLUUID& id : s.obj->mPendingInventoryItemsIDs)
            {
                pending += (pending.empty() ? "" : ",") + nm(id);
            }
            out += " via_xfer " + state_line(s.obj) + " pending=[" + pending + "] | " + take_actions() + "\n";
        }
        return out;
    }

    // The open failure: no file in the cache.
    std::string run_missing_file_case()
    {
        Scene s(false, 1);
        bool returned = bnd().loadTaskInvFile(s.obj, "task_absent.inv");
        return "case missing_file input=- bytes=-1\n" + fmt(" direct returned=%d ", (int)returned) + state_line(s.obj)
            + " | " + take_actions() + "\n" + entries_text(s.obj, " ");
    }

    // Scrubbed filenames through processTaskInv and the real xfer manager.
    std::vector<std::string> scrub_names()
    {
        std::vector<std::string> n = {"", "a", "ab", "abc", "abcd", ".tmp", "x.tm", "a.tmp", ".", "..", "...", "../x",
                                      "..\\x", "a/..", "../../etc/passwd", "inventory_task.tmp"};
        const std::string forbidden = "\\/:*?\"<>|";
        for (char c : forbidden)
        {
            n.push_back(std::string("inv") + c + "x.tmp");
        }
        n.push_back("all" + forbidden + ".tmp");
        n.push_back(forbidden);
        n.push_back("|");
        n.push_back("::");
        n.push_back("C:\\x.tmp");
        n.push_back(std::string("tab\tname.tmp"));
        n.push_back(std::string("ctl\x01.tmp"));
        n.push_back(std::string(254, 'f'));
        n.push_back(std::string(255, 'f'));
        n.push_back(std::string(300, 'f'));
        return n;
    }

    std::string run_scrub_case(const std::string& raw)
    {
        Env& e = env();
        std::string out = "scrub raw=" + esc(raw) + fmt(" rawlen=%d", (int)raw.size());
        const std::string read_back = read_back_filename(raw);
        const std::string scrubbed = LLDir::getScrubbedFileName(read_back);
        out += " received=" + esc(read_back) + " scrubbed=" + esc(scrubbed) + fmt(" len=%d\n", (int)scrubbed.size());

        ::LLXferManager* real = new ::LLXferManager();
        vto::set_real_xfer(real);
        vto::gMessageSystem->mCapture = true;
        receive_packets(); // drain
        std::map<U64, std::string> ids;
        {
            Scene s(false, 1, e.loopback);
            // a local file of the scrubbed name, to show whether the
            // ".tmp" rule removes it before the transfer
            const std::string local = s.cache_path(scrubbed);
            bool planted = false;
            if (!scrubbed.empty() && scrubbed.find_first_of("\t\x01") == std::string::npos && scrubbed.size() < 200
                && scrubbed.find_first_not_of('.') != std::string::npos && scrubbed.find("..") == std::string::npos)
            {
                s.write_file(local, "sentinel");
                planted = file_exists(local);
            }
            std::string outcome;
            try
            {
                deliver_reply(OBJ, 0, raw);
                outcome = "returned";
            }
            catch (const std::out_of_range&)
            {
                outcome = std::string("threw std::out_of_range");
            }
            catch (const std::exception&)
            {
                outcome = std::string("threw std::exception");
            }
            out += " outcome " + outcome + fmt(" planted=%d remaining=%d ", (int)planted, (int)(planted && file_exists(local)))
                + state_line(s.obj) + " | " + take_actions() + "\n";
            for (size_t i = 0; i < real->mReceiveList.size(); ++i)
            {
                ids[real->mReceiveList[i]->mID] = fmt("real#%d", (int)i + 1);
            }
            out += entries_text(s.obj, " ");
            out += packets_text(receive_packets(), ids);
        }
        vto::gMessageSystem->mCapture = false;
        vto::set_real_xfer(NULL);
        delete real;
        return out;
    }

    // A deterministic mutation of the normal file.
    std::string mutate_file(Rng& rng, const std::string& base)
    {
        std::vector<std::string> lines;
        {
            size_t p = 0;
            while (p < base.size())
            {
                size_t e = base.find('\n', p);
                if (e == std::string::npos) { lines.push_back(base.substr(p)); break; }
                lines.push_back(base.substr(p, e - p + 1));
                p = e + 1;
            }
        }
        static const char* tokens[] = {"\tinv_item\t0\n", "\tinv_object\t0\n", "\t{\n", "\t}\n", "\n", " \n", "\tjunk\t1\n",
                                       "\t\tname\tX|\n", "\t\ttype\ttexture\n", "\tpermissions 0\n", "\tsale_info\t0\n",
                                       "\t\tflags\t-1\n", "\t\tsale_price\tx\n"};
        // Lines whose value the readers scan into an uninitialised local
        // (creation_date's S32, the permissions block's mask and uuid_str):
        // a corrupted or missing value there is undefined behaviour (stack
        // garbage, see the README), so the sweep never alters them in place.
        auto is_protected = [](const std::string& line)
        {
            static const char* keys[] = {"creation_date", "base_mask", "owner_mask", "group_mask", "everyone_mask",
                                         "next_owner_mask", "creator_mask", "creator_id", "owner_id", "last_owner_id",
                                         "group_id", "group_owned"};
            char word[64] = {0};
            if (sscanf(line.c_str(), " %63s", word) != 1) return false;
            for (const char* k : keys)
            {
                if (!strcmp(k, word)) return true;
            }
            return false;
        };
        int ops = 1 + (int)rng.below(4);
        for (int k = 0; k < ops && !lines.empty(); ++k)
        {
            U32 at = rng.below((U32)lines.size());
            U32 op = rng.below(7);
            if (op >= 4 && is_protected(lines[at]))
            {
                op = 1; // duplicate the line instead of altering it
            }
            switch (op)
            {
            case 0: lines.erase(lines.begin() + at); break;
            case 1: lines.insert(lines.begin() + at, lines[at]); break;
            case 2: lines.insert(lines.begin() + at, tokens[rng.below(sizeof(tokens) / sizeof(tokens[0]))]); break;
            case 3: std::swap(lines[at], lines[rng.below((U32)lines.size())]); break;
            case 4:
            {
                std::string& l = lines[at];
                if (!l.empty())
                {
                    // the order the recording used (C++17 sequences the right
                    // operand of = first): the byte, then the position
                    const char value = (char)(0x20 + rng.below(0x5f));
                    const U32 pos = rng.below((U32)l.size());
                    l[pos] = value;
                }
                break;
            }
            case 5:
            {
                std::string& l = lines[at];
                l = l.substr(0, rng.below((U32)l.size() + 1));
                break;
            }
            default:
            {
                std::string& l = lines[at];
                // two statements: the recording evaluated the count before
                // the position (MSVC and GCC take these arguments right to
                // left); the order is fixed here for every compiler
                const U32 count = rng.below(300);
                const U32 pos = rng.below((U32)l.size() + 1);
                l.insert(pos, std::string(count, 'm'));
                break;
            }
            }
        }
        std::string out;
        for (const std::string& l : lines) out += l;
        return out;
    }

    void build_set1(Outputs& o)
    {
        std::string text = header("set 1, task inventory files -> records (loadTaskInvFile)");
        text += "# case <label> input=<file> bytes=<n> sha256=<input>\n"
                "#  direct returned=<loadTaskInvFile> removed=<cache file gone> st=<S|W|P|X> ser exp xid dirty inv=[front..back] | <side effects>\n"
                "#  [via_xfer: the same bytes through ReplyTaskInventory -> xfer -> processTaskInvFile with pending=[assetP,assetE,assetA]]\n"
                "#  inventory <n> entries root <uuid of back()>, then every field of every entry, front to back\n";
        for (const FileCase& fc : file_cases())
        {
            const std::string input = "files/" + fc.label + ".inv";
            o.binary[input] = std::vector<U8>(fc.bytes.begin(), fc.bytes.end());
            text += run_file_case(fc, input);
        }
        text += run_missing_file_case();
        o.text["task_files.txt"] = text;

        std::string scrub = header("set 1, scrubbed filenames through processTaskInv and the real LLXferManager");
        scrub += "# scrub raw=<Filename packed> received=<Filename read back> scrubbed=<LLDir::getScrubbedFileName> len=<n>\n"
                 "#  outcome <returned|threw ...> planted=<sentinel cache file of the scrubbed name> remaining=<still there> <state> | <effects>\n"
                 "#  packets: the datagrams sent (RequestXfer), wire with packet id zeroed, body, fields\n";
        for (const std::string& raw : scrub_names())
        {
            scrub += run_scrub_case(raw);
        }
        o.text["scrub.txt"] = scrub;
    }

    // Set 1's random sweep, a test of its own after the named cases, so that a
    // change which crashes on some mutated file (undefined behaviour) still
    // reports the named cases first.
    void build_set1_random(Outputs& o)
    {
        Rng rng(SEED_FILES);
        const std::string base = file_cases()[4].bytes; // "normal"
        DigestBuilder digest("task_files_random");
        std::string samples = header("set 1, random sweep of mutated task files (samples)", fmt("0x%llx xorshift64*", (unsigned long long)SEED_FILES));
        samples += fmt("# %d files mutated from files/normal.inv; every %dth is kept here with its input; all are in digests_set1.txt\n",
                       FILE_SWEEP_COUNT, SAMPLE_EVERY);
        for (int i = 0; i < FILE_SWEEP_COUNT; ++i)
        {
            FileCase fc;
            fc.label = fmt("random_%04d", i);
            fc.bytes = mutate_file(rng, base);
            const std::string input = "random/" + fc.label + ".inv";
            std::string result = run_file_case(fc, input);
            digest.add(result);
            if (i % SAMPLE_EVERY == 0)
            {
                o.binary[input] = std::vector<U8>(fc.bytes.begin(), fc.bytes.end());
                samples += result;
            }
        }
        o.text["task_files_random_samples.txt"] = samples;
        o.digests.push_back(digest.finish());
    }

    //------------------------------------------------------------------
    // set 2: capability replies
    //------------------------------------------------------------------
    struct ReplyCase
    {
        std::string label;
        LLSD body;
        bool has_body = true;
        S16 expected = 0;
    };

    std::vector<ReplyCase> reply_cases()
    {
        std::vector<ReplyCase> c;
        auto add = [&c](const std::string& label, const LLSD& body, S16 expected = 0)
        {
            ReplyCase r;
            r.label = label;
            r.body = body;
            r.expected = expected;
            c.push_back(r);
        };
        LLSD a = make_item(spec_a())->asLLSD();
        LLSD b = make_item(spec_b())->asLLSD();
        auto body_of = [](const LLSD& serial, const LLSD& contents)
        {
            LLSD body;
            if (serial.isDefined()) body["inventory_serial"] = serial;
            body["contents"] = contents;
            return body;
        };
        LLSD ab = LLSD::emptyArray();
        ab.append(a);
        ab.append(b);
        add("standard", body_of(LLSD(5), ab));
        add("standard_stale", body_of(LLSD(5), ab), 6);
        add("contents_missing", LLSD().with("inventory_serial", 5));
        add("contents_undefined", body_of(LLSD(5), LLSD()));
        add("contents_empty", body_of(LLSD(5), LLSD::emptyArray()));
        add("contents_map", body_of(LLSD(5), LLSD().with("item_id", ITEM_A)));
        add("contents_string", body_of(LLSD(5), LLSD("text")));
        {
            LLSD arr = LLSD::emptyArray();
            arr.append(LLSD().with("name", "no id"));
            arr.append(a);
            arr.append(LLSD("string entry"));
            arr.append(LLSD(42));
            arr.append(LLSD::emptyMap());
            arr.append(b);
            add("entries_without_item_id", body_of(LLSD(5), arr));
        }
        {
            LLSD arr = LLSD::emptyArray();
            LLSD r1 = a; r1["running"] = true; r1["faulted"] = false;
            LLSD r2 = b; r2["running"] = "1"; r2["faulted"] = 2;
            LLSD r3 = make_item(spec_a())->asLLSD(); r3["item_id"] = ITEM_C; r3["running"] = LLSD(); r3["faulted"] = true;
            arr.append(r1); arr.append(r2); arr.append(r3);
            add("running_faulted", body_of(LLSD(5), arr));
        }
        for (S32 v : {0, 1, 32767, 32768, 65535, 65536, -1, -32768, -32769, 2147483647, (S32)0x80000000})
        {
            add(fmt("serial_int_%d", v), body_of(LLSD(v), ab));
        }
        add("serial_missing", body_of(LLSD(), ab));
        add("serial_string", body_of(LLSD("12"), ab));
        add("serial_string_junk", body_of(LLSD("x12"), ab));
        add("serial_real", body_of(LLSD(12.9), ab));
        add("serial_real_negative", body_of(LLSD(-1.5), ab));
        add("serial_bool", body_of(LLSD(true), ab));
        add("serial_uuid", body_of(LLSD(ITEM_A), ab));
        add("serial_below_expected_wrap", body_of(LLSD(32767), ab), -32768);
        {
            static const char* names[] = {"New Script", "Contents", "New Note", "Male  - Get lost", "Male - Get lost",
                                          "new script", "New Script ", "Gesture", "dance1", "/yes!", "Female  - Get lost"};
            LLSD arr = LLSD::emptyArray();
            int k = 0;
            for (const char* n : names)
            {
                ItemSpec s = spec_a();
                s.id = LLUUID(fmt("1a000000-0000-4000-8000-%012x", 0x200 + k++).c_str());
                s.name = n;
                arr.append(make_item(s)->asLLSD());
            }
            add("localized_names", body_of(LLSD(5), arr));
        }
        {
            LLSD arr = LLSD::emptyArray();
            ItemSpec l = spec_a();
            l.id = ITEM_C;
            l.type = LLAssetType::AT_LINK;
            l.inv_type = LLInventoryType::IT_NOTECARD;
            l.asset = ITEM_A;
            l.name = "Link";
            arr.append(make_item(l)->asLLSD());
            ItemSpec g = spec_b();
            g.id = ITEM_D;
            g.group = OTHER_GROUP;
            g.group_owned = true;
            g.owner = LLUUID::null;
            g.flags = 0x80000001;
            g.sale_type = LLSaleInfo::FS_COPY;
            g.sale_price = 250;
            g.date = (time_t)4000000000LL;
            arr.append(make_item(g)->asLLSD());
            ItemSpec sh = spec_a();
            sh.id = ITEM_E;
            sh.base = PERM_MOVE | PERM_TRANSFER;
            sh.owner_mask = PERM_MOVE;
            arr.append(make_item(sh)->asLLSD()); // shadow_id form
            LLSD t = make_item(spec_a())->asLLSD();
            t["item_id"] = LLUUID(fmt("1a000000-0000-4000-8000-%012x", 0x300).c_str());
            t["thumbnail"] = LLSD().with("asset_id", THUMB);
            t["metadata"] = LLSD().with("favorite", LLSD().with("toggled", true));
            arr.append(t);
            add("item_variants", body_of(LLSD(5), arr));
        }
        {
            LLSD arr = LLSD::emptyArray();
            LLSD m = a;
            m.erase("permissions");
            m.erase("sale_info");
            arr.append(m);
            LLSD n = b;
            n["item_id"] = "not-a-uuid";
            n["type"] = "bogus";
            n["inv_type"] = 99;
            n["flags"] = -1;
            n["created_at"] = "x";
            arr.append(n);
            add("item_malformed", body_of(LLSD(5), arr));
        }
        add("body_not_map", LLSD(LLSD::emptyArray()));
        {
            ReplyCase r;
            r.label = "body_empty";
            r.has_body = false;
            c.push_back(r);
        }
        return c;
    }

    std::string run_reply_case(const ReplyCase& rc, const std::string& input_path)
    {
        std::string out = "case " + rc.label + " input=" + (rc.has_body ? input_path : std::string("-"))
            + fmt(" expected_before=%d", (int)rc.expected) + "\n";
        out += "  body " + (rc.has_body ? notation(rc.body) : std::string("<no body>")) + "\n";
        Scene s(true, 1);
        s.obj->mExpectedInventorySerialNum = rc.expected;
        bnd().requestInventory(s.obj);
        out += " request " + state_line(s.obj) + " | " + take_actions() + "\n";
        vto::complete_http(rc.has_body ? rc.body : LLSD(), ::LLCore::HttpStatus(200));
        out += " reply " + state_line(s.obj) + " | " + take_actions() + "\n";
        out += entries_text(s.obj, " ");
        return out;
    }

    LLSD random_reply(Rng& rng)
    {
        static const char* names[] = {"New Script", "Contents", "Thing", "", "  padded  ", "dance8", "a|b",
                                      "\xc3\xa9", "New Folder"};
        static const LLAssetType::EType types[] = {LLAssetType::AT_NOTECARD, LLAssetType::AT_OBJECT, LLAssetType::AT_LSL_TEXT,
                                                   LLAssetType::AT_TEXTURE, LLAssetType::AT_LINK, LLAssetType::AT_CATEGORY,
                                                   LLAssetType::AT_MATERIAL, LLAssetType::AT_SETTINGS};
        static const LLInventoryType::EType inv_types[] = {LLInventoryType::IT_NOTECARD, LLInventoryType::IT_OBJECT,
                                                           LLInventoryType::IT_LSL, LLInventoryType::IT_TEXTURE,
                                                           LLInventoryType::IT_NONE, LLInventoryType::IT_MATERIAL};
        LLSD body;
        switch (rng.below(6))
        {
        case 0: break;
        case 1: body["inventory_serial"] = (S32)(rng.next() & 0xffffffff); break;
        case 2: body["inventory_serial"] = fmt("%d", (int)rng.below(70000) - 35000); break;
        default: body["inventory_serial"] = (S32)rng.below(70000) - 35000; break;
        }
        if (rng.below(10) == 0)
        {
            return body; // no contents
        }
        LLSD arr = LLSD::emptyArray();
        int n = (int)rng.below(6);
        for (int i = 0; i < n; ++i)
        {
            U32 kind = rng.below(10);
            if (kind == 0) { arr.append(LLSD("junk")); continue; }
            if (kind == 1) { arr.append(LLSD().with("name", "no id")); continue; }
            ItemSpec s;
            s.id = LLUUID(fmt("1a000000-0000-4000-8000-%012llx", (unsigned long long)(rng.next() & 0xffff)).c_str());
            s.asset = LLUUID(fmt("a5000000-0000-4000-8000-%012llx", (unsigned long long)(rng.next() & 0xffff)).c_str());
            s.type = types[rng.below(sizeof(types) / sizeof(types[0]))];
            s.inv_type = inv_types[rng.below(sizeof(inv_types) / sizeof(inv_types[0]))];
            s.name = names[rng.below(sizeof(names) / sizeof(names[0]))];
            s.base = (U32)rng.next();
            s.owner_mask = (U32)rng.next();
            s.group_mask = (U32)rng.next();
            s.everyone = (U32)rng.next();
            s.next = (U32)rng.next();
            s.owner = rng.coin() ? AGENT : OTHER;
            s.group = rng.coin() ? LLUUID::null : OTHER_GROUP;
            s.flags = (U32)rng.next();
            s.sale_type = (LLSaleInfo::EForSale)rng.below(4);
            s.sale_price = (S32)rng.below(1000);
            s.date = (time_t)(rng.next() & 0xffffffff);
            LLSD item = make_item(s)->asLLSD();
            if (rng.below(4) == 0) item["running"] = rng.coin();
            if (rng.below(4) == 0) item["faulted"] = rng.coin();
            if (rng.below(8) == 0) item.erase("permissions");
            if (rng.below(8) == 0) item.erase("sale_info");
            arr.append(item);
        }
        body["contents"] = arr;
        return body;
    }

    void build_set2(Outputs& o)
    {
        std::string text = header("set 2, RequestTaskInventory capability replies (200) -> serials, state, records");
        text += "# case <label> input=<binary LLSD body> expected_before=<mExpectedInventorySerialNum before the request>\n"
                "#  body <notation, for review>\n"
                "#  request <state> | <effects of requestInventory>; reply <state> | <effects of the reply>\n"
                "#  inventory ... (as task_files.txt; names show {L10N:<key>} where LLLocalizedInventoryItemsDictionary applied)\n";
        for (const ReplyCase& rc : reply_cases())
        {
            const std::string input = "replies/" + rc.label + ".llsd";
            if (rc.has_body)
            {
                o.binary[input] = binary_llsd(rc.body);
            }
            text += run_reply_case(rc, input);
        }
        o.text["cap_replies.txt"] = text;

        Rng rng(SEED_REPLIES);
        DigestBuilder digest("cap_replies_random");
        std::string samples = header("set 2, random sweep of capability reply bodies (samples)", fmt("0x%llx xorshift64*", (unsigned long long)SEED_REPLIES));
        samples += fmt("# %d bodies; every %dth is kept here with its input; all are in digests_set2.txt\n", REPLY_SWEEP_COUNT, SAMPLE_EVERY);
        for (int i = 0; i < REPLY_SWEEP_COUNT; ++i)
        {
            ReplyCase rc;
            rc.label = fmt("random_%04d", i);
            rc.body = random_reply(rng);
            rc.expected = (S16)(rng.below(5) == 0 ? (S32)rng.below(70000) - 35000 : 0);
            const std::string input = "replies_random/" + rc.label + ".llsd";
            std::string result = run_reply_case(rc, input);
            digest.add(result);
            if (i % SAMPLE_EVERY == 0)
            {
                o.binary[input] = binary_llsd(rc.body);
                samples += result;
            }
        }
        o.text["cap_replies_random_samples.txt"] = samples;
        o.digests.push_back(digest.finish());
    }

    //------------------------------------------------------------------
    // set 3: serial traces
    //------------------------------------------------------------------
    const std::vector<std::string>& udp_alphabet()
    {
        static const std::vector<std::string> a = {"req", "dirty", "rep.lo", "rep.cur", "rep.exp", "rep.hi", "rep.none",
                                                   "xfer.ok", "xfer.err", "xfer.nofile", "timer", "upd.known", "upd.guess",
                                                   "upd.new", "remove", "move.nocopy", "create.ok"};
        return a;
    }

    const std::vector<std::string>& cap_alphabet()
    {
        static const std::vector<std::string> a = {"req", "dirty", "rep.lo", "rep.exp", "cap.ok", "cap.stale", "cap.304",
                                                   "cap.err", "cap.nocontents", "timer", "upd.known", "upd.guess", "upd.new",
                                                   "remove", "move.nocopy", "create.ok"};
        return a;
    }

    struct HandTrace
    {
        const char* label;
        bool caps;
        int listeners;
        const char* events;
    };

    const std::vector<HandTrace>& hand_traces()
    {
        static const std::vector<HandTrace> t = {
            // loading through each path
            {"udp_load", false, 1, "req rep.exp xfer.ok"},
            {"udp_load_no_listener", false, 0, "req rep.exp xfer.ok req"},
            {"cap_load", true, 1, "req cap.ok"},
            {"cap_url_never_has_serial", true, 1, "req cap=7 dirty req"},
            {"cap_url_after_local_edits", true, 0, "req cap=7 upd.known req dirty req"},
            // wrap at 32767 / -32768
            {"wrap_update_new", false, 1, "set.serial=32767 set.expected=32767 upd.new rep=-32768 xfer.ok"},
            {"wrap_old_reply_loaded", false, 1, "set.serial=32767 set.expected=32767 upd.new rep=32767 xfer.ok"},
            {"wrap_reply_minus_one", false, 1, "set.serial=-32768 set.expected=-32768 rep=32767 xfer.ok"},
            {"wrap_loop_protection", false, 1, "set.serial=32767 set.expected=-32768 rep=32767 timer"},
            {"wrap_cap", true, 1, "set.serial=32767 set.expected=32767 upd.new req cap=-32768"},
            {"wrap_cap_stale", true, 1, "set.serial=32767 set.expected=32767 upd.new req cap=32767 timer"},
            {"wrap_cap_out_of_range", true, 1, "req cap=32768"},
            {"wrap_remove", false, 1, "req rep.exp xfer.ok set.expected=32767 remove"},
            {"wrap_guess_down", false, 1, "set.expected=-32768 req rep=-32768 xfer.ok upd.guess"},
            // stuck pending
            {"stuck_xfer_error", false, 1, "req rep.exp xfer.err req req timer"},
            {"stuck_xfer_error_unstuck_by_reply", false, 1, "req rep.exp xfer.err rep.exp xfer.ok"},
            {"older_serial_file_ignored", false, 1, "req rep=1 rep.none rep=2 xfer.ok req xfer.ok"},
            {"stuck_older_serial_file_then_error", false, 1, "req rep=1 rep.none rep=2 xfer.ok xfer.err req timer"},
            {"newer_reply_aborts_older_xfer", false, 1, "req rep=1 rep=2 xfer.ok.newest xfer.ok"},
            {"stuck_older_serial_file_2", false, 1, "req rep=2 xfer.ok rep=1 xfer.ok req"},
            {"stuck_missing_file", false, 1, "req rep.exp xfer.nofile req"},
            {"stuck_object_gone_during_xfer", false, 1, "req rep.exp die xfer.ok revive req"},
            {"stuck_cap_no_contents", true, 1, "req cap.nocontents req dirty req"},
            {"stuck_cap_no_contents_reply_does_not_unstick", true, 1, "req cap.nocontents rep.exp rep.lo rep.hi timer req"},
            {"stuck_cap_object_gone", true, 1, "req die cap.ok revive req"},
            {"stuck_aborted_xfer", false, 1, "req rep.exp rep.lo xfer.ok timer"},
            {"xfer_completion_records_serial", false, 1, "rep.hi rep.none rep.lo xfer.ok"},
            {"object_gone_during_delay_udp", false, 1, "req upd.new rep.lo die timer revive req"},
            {"object_gone_during_delay_cap", true, 1, "req upd.new rep.lo die timer revive req"},
            {"cap_error_not_stuck", true, 1, "req cap.err req cap.ok"},
            {"cap_304", true, 1, "req cap.304 req cap.ok"},
            {"cap_304_after_edit", true, 1, "req cap.ok upd.new dirty req cap.304 timer cap.ok"},
            // reply branches
            {"loop_protection", false, 1, "req rep.exp xfer.ok upd.new rep.cur timer rep.exp xfer.ok"},
            {"out_of_date_reply", false, 1, "req upd.new rep.lo timer"},
            {"newer_reply_replaces_xfer", false, 1, "req rep.exp rep.hi xfer.ok xfer.ok"},
            {"same_reply_twice", false, 1, "req rep.exp rep.exp xfer.ok xfer.ok"},
            {"empty_filename_mock", false, 1, "req rep.none upd.new remove"},
            {"unsolicited_reply_cap_lowers_expected", true, 1, "req cap=5 upd.new upd.new rep.lo"},
            {"unsolicited_reply_udp", false, 1, "rep.exp xfer.ok"},
            {"delayed_twice", false, 1, "req upd.new rep.lo rep.lo timer timer"},
            // local mutations
            {"update_known_owner", false, 1, "req rep.exp xfer.ok upd.known"},
            {"update_guess_owner", false, 1, "req rep.exp xfer.ok upd.guess"},
            {"update_guess_not_owner", false, 1, "notowner req rep.exp xfer.ok upd.guess"},
            {"update_selected", false, 1, "select req rep.exp xfer.ok upd.known upd.guess"},
            {"update_by_asset", false, 1, "req rep.exp xfer.ok upd.asset"},
            {"update_no_inventory", false, 1, "upd.known upd.guess upd.new"},
            {"remove_absent", false, 1, "req rep.none remove"},
            {"move_copy_and_nocopy", false, 1, "req rep.exp xfer.ok move.copy move.nocopy move.nocopy"},
            {"create_ok_udp_no_cap", false, 1, "create.ok"},
            {"create_ok_cap", true, 1, "req cap.ok create.ok cap.ok"},
            {"create_fail_cap", true, 1, "req cap.ok create.fail"},
            // listeners
            {"dirty_with_listener", false, 1, "req rep.exp xfer.ok dirty req"},
            {"dirty_without_listener", false, 0, "req rep.exp xfer.ok dirty req listen req"},
            {"two_listeners", false, 2, "req rep.exp xfer.ok unlisten upd.new"},
            {"clear_listeners", false, 2, "req rep.exp xfer.ok clearlisteners upd.new remove req"},
        };
        return t;
    }

    std::vector<std::string> split(const std::string& s)
    {
        std::vector<std::string> out;
        std::istringstream in(s);
        std::string w;
        while (in >> w) out.push_back(w);
        return out;
    }

    // Every sequence of exactly `length` events over the alphabet, in
    // lexicographic order of the alphabet indices.
    void for_each_sequence(const std::vector<std::string>& alphabet, int length,
                           const std::function<void(const std::vector<std::string>&)>& fn)
    {
        std::vector<size_t> idx(length, 0);
        std::vector<std::string> seq(length);
        while (true)
        {
            for (int i = 0; i < length; ++i) seq[i] = alphabet[idx[i]];
            fn(seq);
            int k = length - 1;
            while (k >= 0 && ++idx[k] == alphabet.size())
            {
                idx[k] = 0;
                --k;
            }
            if (k < 0) break;
        }
    }

    struct Config
    {
        bool caps;
        int listeners;
    };
    const Config CONFIGS[] = {{false, 0}, {false, 1}, {true, 0}, {true, 1}};

    void build_set3(Outputs& o)
    {
        const std::string legend =
            "# trace <udp|cap>/<listeners> [label] : <events>\n"
            "#  <event> | st=<S stopped|W wait|P pending|X xfer> ser=<mInventorySerialNum> exp=<mExpectedInventorySerialNum>\n"
            "#            xid=<mInvRequestXFerId> dirty=<mInventoryDirty> inv=<entries front..back, or ->\n"
            "#            cache=<task inventory files of the trace still in the cache> | <side effects in order>\n"
            "# digests of the complete sweeps: digests_set3.txt\n"
            "# events: req requestInventory; dirty dirtyInventory; rep.lo|cur|exp|hi ReplyTaskInventory with serial expected-1 |\n"
            "#  current | expected | expected+1 and filename inv_s<serial>.tmp; rep.none empty filename; rep=<n>;\n"
            "#  xfer.ok|err|nofile complete the oldest live xfer with the standard file | error -1 | no file (xfer.ok.newest: the newest);\n"
            "#  cap.ok|stale 200 with the standard contents and serial expected | expected-1; cap=<n>; cap.304; cap.err (500);\n"
            "#  cap.nocontents 200 without contents; timer fires the oldest pending timer; upd.known updateInventory(A edited, key 0);\n"
            "#  upd.guess (C, absent); upd.new (D, is_new); upd.asset (E with A's asset, key 1); remove A; move.nocopy B; move.copy A;\n"
            "#  create.ok|fail createInventoryItem then the POST answered success true|false; die/revive the object in gObjectList;\n"
            "#  select (selection manager holds owner=other group=othergroup); notowner (permYouOwner false); listen/unlisten; clearlisteners;\n"
            "#  set.serial=<n>/set.expected=<n> set the fields directly.\n"
            "# Standard file/contents: Contents (obj), A copyable notecard (assetA), B no-copy object (assetB).\n";

        std::string hand = header("set 3, hand-written serial and request-state traces") + legend;
        for (const HandTrace& h : hand_traces())
        {
            hand += run_trace(h.caps, h.listeners, split(h.events), h.label);
        }
        o.text["serial_traces_hand.txt"] = hand;

        std::string short_text = header("set 3, every event sequence of length 1 and 2, per configuration") + legend;
        short_text += "# udp alphabet:";
        for (const std::string& e : udp_alphabet()) short_text += " " + e;
        short_text += "\n# cap alphabet:";
        for (const std::string& e : cap_alphabet()) short_text += " " + e;
        short_text += "\n# order: udp/L0, udp/L1, cap/L0, cap/L1; within each, length 1 then 2, lexicographic by alphabet index\n";
        for (int length = 1; length <= 4; ++length)
        {
            std::string stream; // kept for lengths 1 and 2 only
            std::string samples;
            U64 count = 0;
            DigestBuilder digest(fmt("serial_traces_len%d", length));
            for (const Config& cfg : CONFIGS)
            {
                const std::vector<std::string>& alphabet = cfg.caps ? cap_alphabet() : udp_alphabet();
                for_each_sequence(alphabet, length, [&](const std::vector<std::string>& seq)
                {
                    std::string t = run_trace(cfg.caps, cfg.listeners, seq);
                    digest.add(t);
                    if (length <= 2)
                    {
                        stream += t;
                    }
                    if (length >= 3 && count % SAMPLE_EVERY == 0)
                    {
                        samples += t;
                    }
                    ++count;
                });
            }
            if (length <= 2)
            {
                short_text += stream;
            }
            else
            {
                std::string head = header(fmt("set 3, every event sequence of length %d (sample)", length)) + legend;
                head += fmt("# %llu traces in the order of serial_traces_len1_2.txt; every %dth (0, %d, ...) is kept here; "
                            "the SHA-256 of all of them, concatenated, and of each block of %d starting at a sample, is in digests_set3.txt\n",
                            (unsigned long long)count, SAMPLE_EVERY, SAMPLE_EVERY, SAMPLE_EVERY);
                o.text[fmt("serial_traces_len%d_samples.txt", length)] = head + samples;
            }
            o.digests.push_back(digest.finish());
        }
        o.text["serial_traces_len1_2.txt"] = short_text;
    }

    //------------------------------------------------------------------
    // set 4: outgoing messages
    //------------------------------------------------------------------
    struct Capture
    {
        ::LLXferManager* real = NULL;
        Capture()
        {
            real = new ::LLXferManager();
            vto::set_real_xfer(real);
            vto::gMessageSystem->mCapture = true;
            receive_packets();
        }
        ~Capture()
        {
            vto::gMessageSystem->mCapture = false;
            vto::set_real_xfer(NULL);
            delete real;
        }
        std::map<U64, std::string> ids() const
        {
            std::map<U64, std::string> m;
            int i = 0;
            for (LLXfer* x : real->mReceiveList)
            {
                m[x->mID] = fmt("real#%d", ++i);
            }
            return m;
        }
    };

    // Loads the standard contents through the UDP path with the fake xfer
    // manager (nothing sent is captured here).
    void preload(Scene& s, const std::string& file_bytes = std::string())
    {
        bool capture = vto::gMessageSystem->mCapture;
        vto::gMessageSystem->mCapture = false;
        bnd().requestInventory(s.obj);
        deliver_reply(OBJ, s.obj->mExpectedInventorySerialNum, "inv_preload.tmp");
        int i = oldest_live_xfer();
        if (getenv("VKS_TASK_VERBOSE")) std::cerr << "preload xfers=" << vto::fake_xfers().size() << " live=" << i << std::endl;
        if (i >= 0)
        {
            vto::FakeXfer x = vto::fake_xfers()[i];
            vto::fake_xfers().erase(vto::fake_xfers().begin() + i);
            s.write_file(x.local_filename, file_bytes.empty() ? standard_file() : file_bytes);
            x.callback(x.user_data, 0, LLExtStat::NONE);
        }
        std::string actions = take_actions();
        if (getenv("VKS_TASK_VERBOSE")) std::cerr << "preload " << state_line(s.obj) << " | " << actions << std::endl;
        vto::gMessageSystem->mCapture = capture;
    }

    struct MsgCase
    {
        std::string label;
        std::function<void(Scene&)> setup;   // before capture (fake xfer, no capture)
        std::function<void(Scene&)> action;  // captured
        bool caps = false;
    };

    std::vector<MsgCase> message_cases()
    {
        std::vector<MsgCase> c;
        auto add = [&c](const std::string& label, std::function<void(Scene&)> setup, std::function<void(Scene&)> action, bool caps = false)
        {
            MsgCase m;
            m.label = label;
            m.setup = setup;
            m.action = action;
            m.caps = caps;
            c.push_back(m);
        };
        auto none = [](Scene&) {};
        auto loaded = [](Scene& s) { preload(s); };

        ItemSpec rich = spec_a();
        rich.id = ITEM_A;
        rich.name = "Rich item with a name longer than sixty-three characters, cut by the setter";
        rich.desc = std::string("Desc \xc3\xa9 with | pipe");
        rich.txn = TXN;
        rich.flags = 0xffffffff;
        rich.date = (time_t)3000000000LL;
        rich.sale_type = LLSaleInfo::FS_ORIGINAL;
        rich.sale_price = 1234;
        rich.group = OTHER_GROUP;
        rich.base = 0x7fffffff;
        rich.owner_mask = PERM_ALL & ~PERM_MODIFY;
        rich.group_mask = PERM_COPY;
        rich.everyone = PERM_MOVE;
        rich.next = PERM_ALL & ~PERM_TRANSFER;

        // UpdateTaskInventory, each permission-guess branch of doUpdateInventory
        add("update_key0_new", loaded, [](Scene& s) { ItemSpec d = spec_a(); d.id = ITEM_D; d.asset = ASSET_D; d.name = "New D"; update_item(s, d, TASK_INVENTORY_ITEM_KEY, true); });
        add("update_key0_selected", [](Scene& s) { preload(s); apply(s, "select"); }, [](Scene& s) { ItemSpec a = spec_a(); a.desc = "edited"; update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_key0_group_owned", [](Scene& s)
            {
                preload(s);
                // A is in the contents as group owned
                ItemSpec g = spec_a();
                g.group = OTHER_GROUP;
                g.group_owned = true;
                g.owner = LLUUID::null;
                LLPointer<vto::LLViewerInventoryItem> gi = make_item(g);
                bnd().deleteInventoryItem(s.obj, ITEM_A);
                s.obj->mInventory->push_front(gi.get());
                take_actions();
            },
            [](Scene& s) { ItemSpec a = spec_a(); a.desc = "edited"; update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_key0_owner_known", loaded, [](Scene& s) { ItemSpec a = spec_a(); a.desc = "edited"; a.owner = OTHER; update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_key0_guess_owner", loaded, [](Scene& s) { ItemSpec c2 = spec_a(); c2.id = ITEM_C; c2.asset = ASSET_C; c2.group = OTHER_GROUP; update_item(s, c2, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_key0_guess_not_owner", [](Scene& s) { preload(s); s.obj->mFakeYouOwner = false; }, [](Scene& s) { ItemSpec c2 = spec_a(); c2.id = ITEM_C; c2.asset = ASSET_C; update_item(s, c2, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_key0_object_type_not_atomic", loaded, [](Scene& s) { ItemSpec b = spec_b(); b.id = ITEM_C; update_item(s, b, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_key1_by_asset_known", loaded, [](Scene& s) { ItemSpec e = spec_a(); e.id = ITEM_E; e.desc = "by asset"; update_item(s, e, TASK_INVENTORY_ASSET_KEY, false); });
        add("update_key1_by_asset_guess", loaded, [](Scene& s) { ItemSpec e = spec_a(); e.id = ITEM_E; e.asset = ASSET_E; update_item(s, e, TASK_INVENTORY_ASSET_KEY, false); });
        add("update_no_inventory", none, [](Scene& s) { ItemSpec a = spec_a(); update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_rich_item", loaded, [rich](Scene& s) { update_item(s, rich, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_material_new", loaded, [](Scene& s)
            {
                ItemSpec t = spec_a();
                t.id = ITEM_D;
                t.asset = ASSET_D;
                t.type = LLAssetType::AT_TEXTURE;
                t.inv_type = LLInventoryType::IT_TEXTURE;
                LLPointer<vto::LLViewerInventoryItem> item = make_item(t);
                bnd().updateMaterialInventory(s.obj, item, TASK_INVENTORY_ITEM_KEY, true);
                bnd().updateMaterialInventory(s.obj, item, TASK_INVENTORY_ITEM_KEY, true); // already pending: nothing
            });
        // is_atomic: an owner change (selected object, or not owner) with the
        // next-owner mask PERM_MOVE; setOwnerAndGroup adds PERM_TRANSFER to
        // the new base only for atomic items (not AT_OBJECT)
        add("update_selected_notecard_next_move", [](Scene& s) { preload(s); apply(s, "select"); },
            [](Scene& s) { ItemSpec a = spec_a(); a.next = PERM_MOVE; a.desc = "atomic"; update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_selected_object_next_move", [](Scene& s) { preload(s); apply(s, "select"); },
            [](Scene& s) { ItemSpec b = spec_b(); b.next = PERM_MOVE; b.desc = "not atomic"; update_item(s, b, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_notowner_notecard_next_move", [](Scene& s) { preload(s); s.obj->mFakeYouOwner = false; },
            [](Scene& s) { ItemSpec c2 = spec_a(); c2.id = ITEM_C; c2.asset = ASSET_C; c2.next = PERM_MOVE; update_item(s, c2, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_notowner_object_next_move", [](Scene& s) { preload(s); s.obj->mFakeYouOwner = false; },
            [](Scene& s) { ItemSpec c2 = spec_b(); c2.id = ITEM_C; c2.asset = ASSET_C; c2.next = PERM_MOVE; update_item(s, c2, TASK_INVENTORY_ITEM_KEY, false); });
        // ... on the group-owned branch (the old item in the contents is group
        // owned: new owner null, so the incoming owner changes) ...
        auto make_group_owned = [](Scene& s, const ItemSpec& old_spec)
        {
            ItemSpec g = old_spec;
            g.group = OTHER_GROUP;
            g.group_owned = true;
            g.owner = LLUUID::null;
            LLPointer<vto::LLViewerInventoryItem> gi = make_item(g);
            bnd().deleteInventoryItem(s.obj, g.id);
            s.obj->mInventory->push_front(gi.get());
            take_actions();
        };
        add("update_groupowned_notecard_next_move", [make_group_owned](Scene& s) { preload(s); make_group_owned(s, spec_a()); },
            [](Scene& s) { ItemSpec a = spec_a(); a.next = PERM_MOVE; a.desc = "atomic"; update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_groupowned_object_next_move", [make_group_owned](Scene& s) { preload(s); make_group_owned(s, spec_b()); },
            [](Scene& s) { ItemSpec b = spec_b(); b.next = PERM_MOVE; b.desc = "not atomic"; update_item(s, b, TASK_INVENTORY_ITEM_KEY, false); });
        // ... on the known-owner branch (the old item's owner, the agent,
        // replaces the incoming owner, other) ...
        add("update_known_notecard_next_move", loaded,
            [](Scene& s) { ItemSpec a = spec_a(); a.owner = OTHER; a.next = PERM_MOVE; a.desc = "atomic"; update_item(s, a, TASK_INVENTORY_ITEM_KEY, false); });
        add("update_known_object_next_move", loaded,
            [](Scene& s) { ItemSpec b = spec_b(); b.owner = OTHER; b.next = PERM_MOVE; b.desc = "not atomic"; update_item(s, b, TASK_INVENTORY_ITEM_KEY, false); });
        // ... and for a script (AT_LSL_TEXT is atomic) on the selected branch
        add("update_selected_script_next_move", [](Scene& s) { preload(s); apply(s, "select"); },
            [](Scene& s)
            {
                ItemSpec sc = spec_a();
                sc.id = ITEM_C;
                sc.asset = ASSET_C;
                sc.type = LLAssetType::AT_LSL_TEXT;
                sc.inv_type = LLInventoryType::IT_LSL;
                sc.name = "Script C";
                sc.next = PERM_MOVE;
                update_item(s, sc, TASK_INVENTORY_ITEM_KEY, false);
            });
        // deleteInventoryItem itself on a duplicated id: the first match only
        add("delete_duplicate_id_direct", [](Scene& s)
            {
                ItemSpec dup = spec_a();
                dup.name = "Duplicate A";
                dup.asset = ASSET_E;
                preload(s, legacy_object(OBJ, "Contents") + legacy_item(spec_a()) + legacy_item(dup) + legacy_item(spec_b()));
            },
            [](Scene& s) { bnd().deleteInventoryItem(s.obj, ITEM_A); });
        // deleteInventoryItem erases the first match only: a file with A twice
        add("remove_duplicate_id", [](Scene& s)
            {
                ItemSpec dup = spec_a();
                dup.name = "Duplicate A";
                dup.asset = ASSET_E;
                preload(s, legacy_object(OBJ, "Contents") + legacy_item(spec_a()) + legacy_item(dup) + legacy_item(spec_b()));
            },
            [](Scene& s) { bnd().removeInventory(s.obj, ITEM_A); });
        // the local-only updates (no message)
        add("update_local_known", loaded, [](Scene& s)
            {
                ItemSpec a = spec_a();
                a.desc = "local";
                a.txn = TXN;
                LLPointer<vto::LLViewerInventoryItem> item = make_item(a);
                bnd().updateInventoryLocal(s.obj, item, TASK_INVENTORY_ITEM_KEY);
            });
        add("update_viewer_asset", loaded, [](Scene& s)
            {
                LLPointer<vto::LLViewerInventoryItem> item = make_item(spec_a());
                bnd().updateViewerInventoryAsset(s.obj, item, ASSET_E);
            });
        // RemoveTaskInventory
        add("remove_present", loaded, [](Scene& s) { bnd().removeInventory(s.obj, ITEM_A); });
        add("remove_absent", loaded, [](Scene& s) { bnd().removeInventory(s.obj, ITEM_E); });
        // MoveTaskInventory
        add("move_null_folder_copyable", loaded, [](Scene& s) { bnd().moveInventory(s.obj, LLUUID::null, ITEM_A); });
        add("move_explicit_folder_nocopy", loaded, [](Scene& s) { bnd().moveInventory(s.obj, FOLDER, ITEM_B); });
        add("move_absent", loaded, [](Scene& s) { bnd().moveInventory(s.obj, FOLDER, ITEM_E); });
        // RequestTaskInventory
        add("request_udp", none, [](Scene& s) { bnd().requestInventory(s.obj); });
        add("request_udp_pending_dropped", none, [](Scene& s) { bnd().requestInventory(s.obj); bnd().requestInventory(s.obj); });
        // RezScript
        add("rezscript_new_template", none, [](Scene& s)
            {
                ItemSpec sc = spec_a(); sc.id = ITEM_C; sc.asset = LLUUID::null; sc.type = LLAssetType::AT_LSL_TEXT; sc.inv_type = LLInventoryType::IT_LSL; sc.name = "New Script"; sc.txn = TXN;
                bnd().saveScript(s.obj, make_item(sc), true, true, TEMPLATE_ITEM);
            });
        add("rezscript_asset_with_template", loaded, [](Scene& s)
            {
                ItemSpec sc = spec_a(); sc.id = ITEM_C; sc.asset = ASSET_C; sc.type = LLAssetType::AT_LSL_TEXT; sc.inv_type = LLInventoryType::IT_LSL; sc.name = "Script";
                bnd().saveScript(s.obj, make_item(sc), false, false, TEMPLATE_ITEM);
            });
        // a template with no asset sends NewScriptInfo whether or not the
        // script is new (the test is on the asset and the template only)
        add("rezscript_template_not_new", loaded, [](Scene& s)
            {
                ItemSpec sc = spec_a(); sc.id = ITEM_C; sc.asset = LLUUID::null; sc.type = LLAssetType::AT_LSL_TEXT; sc.inv_type = LLInventoryType::IT_LSL; sc.name = "Script";
                bnd().saveScript(s.obj, make_item(sc), true, false, TEMPLATE_ITEM);
            });
        add("rezscript_new_no_template", none, [](Scene& s)
            {
                ItemSpec sc = spec_a(); sc.id = ITEM_C; sc.asset = LLUUID::null; sc.type = LLAssetType::AT_LSL_TEXT; sc.inv_type = LLInventoryType::IT_LSL; sc.name = "New Script";
                bnd().saveScript(s.obj, make_item(sc), true, true, LLUUID::null);
            });
        // RequestXfer / AbortXfer from the real xfer manager
        add("xfer_request", none, [](Scene& s) { bnd().requestInventory(s.obj); deliver_reply(OBJ, 0, "inventory_obj.tmp"); });
        add("xfer_replaced_by_newer_reply", none, [](Scene& s)
            {
                bnd().requestInventory(s.obj);
                deliver_reply(OBJ, 0, "inventory_first.tmp");
                deliver_reply(OBJ, 1, "inventory_second.tmp");
            });
        add("xfer_aborted_by_delayed_fetch", none, [](Scene& s)
            {
                bnd().requestInventory(s.obj);
                deliver_reply(OBJ, 0, "inventory_first.tmp");
                s.obj->mExpectedInventorySerialNum = 3;
                deliver_reply(OBJ, 1, "inventory_old.tmp");
            });
        // the capability path: URL, and the CreateTaskInventoryItem body
        add("cap_request_url", none, [](Scene& s) { bnd().requestInventory(s.obj); }, true);
        add("cap_request_url_after_load", [](Scene& s)
            {
                bnd().requestInventory(s.obj);
                vto::complete_http(standard_reply(9), ::LLCore::HttpStatus(200));
                take_actions();
            },
            [](Scene& s) { bnd().dirtyInventory(s.obj); bnd().requestInventory(s.obj); }, true);
        return c;
    }

    struct CreateCase
    {
        std::string label;
        bool region = true;
        bool cap = true;
        LLAssetType::EType asset_type;
        LLInventoryType::EType inv_type;
        U8 sub_type;
        std::string name;
        std::string desc;
        U32 masks[5]; // base owner everyone group next
        LLSD params;
        int status;
        bool success;
    };

    std::vector<CreateCase> create_cases()
    {
        std::vector<CreateCase> c;
        c.push_back({"script", true, true, LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL, 0, "New Script", "",
                     {PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_MOVE | PERM_TRANSFER}, LLSD(), 200, true});
        c.push_back({"lua_script_params", true, true, LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL, 1, "Lua \xc3\xa9", "d|e",
                     {0xffffffff, 0x7fffffff, PERM_COPY, PERM_MOVE, 0x80000000},
                     LLSD().with("language", "lua").with("template", TEMPLATE_ITEM), 200, true});
        c.push_back({"notecard", true, true, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, 0, "New Note", "desc",
                     {PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL}, LLSD::emptyMap(), 200, true});
        c.push_back({"server_says_no", true, true, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, 0, "N", "",
                     {PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL}, LLSD(), 200, false});
        c.push_back({"http_error", true, true, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, 0, "N", "",
                     {PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL}, LLSD(), 500, true});
        c.push_back({"no_cap", true, false, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, 0, "N", "",
                     {PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL}, LLSD(), 200, true});
        c.push_back({"no_region", false, true, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, 0, "N", "",
                     {PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL}, LLSD(), 200, true});
        return c;
    }

    void build_set4(Outputs& o)
    {
        Env& e = env();
        std::string text = header("set 4, task-inventory messages sent by the viewer (UDP datagrams captured on loopback)");
        text += "# case <label> <udp|cap>\n"
                "#  before <state>; after <state> | <side effects in order (msg.send = LLMessageSystem::sendReliable)>\n"
                "#  packets <n>: packet <name> flags=<byte 0> zerocoded wirelen\n"
                "#   wire <datagram, packet id (bytes 1-4) zeroed; no acks are appended: nothing is ever received>\n"
                "#   (a StartPingCheck marker sent after each action through the same socket delimits the capture;\n"
                "#   it is not listed)\n"
                "#   body <zero-expanded> and every block/variable as raw bytes (Variable fields also as text)\n"
                "#  RequestXfer/AbortXfer ids are random (LLXferManager::getNextID): shown as <xfer id real#k> and zeroed\n"
                "#  in body (and in wire when not zero-coded)\n"
                "#  inventory after the action (as task_files.txt)\n";
        if (!e.ok)
        {
            text += "# LOOPBACK RECEIVER UNAVAILABLE\n";
        }
        for (const MsgCase& mc : message_cases())
        {
            if (getenv("VKS_TASK_VERBOSE")) std::cerr << "set 4 case " << mc.label << std::endl;
            text += "case " + mc.label + (mc.caps ? " cap\n" : " udp\n");
            Scene s(mc.caps, 1, e.loopback);
            mc.setup(s);
            take_actions();
            std::string before = state_line(s.obj);
            Capture cap;
            mc.action(s);
            std::string actions = take_actions();
            text += "  before " + before + "\n  after " + state_line(s.obj) + " | " + actions + "\n";
            if (vto::pending_http_count() > 0)
            {
                text += "  pending " + vto::pending_http_method(0) + " " + vto::pending_http_url(0) + "\n";
            }
            text += packets_text(receive_packets(), cap.ids());
            text += entries_text(s.obj, "  ");
        }

        std::string bodies = header("set 4, CreateTaskInventoryItem capability (POST body and outcome)");
        bodies += "# case <label>: the POST url, body (binary LLSD in create/<label>.llsd, notation here), then the reply and effects\n";
        for (const CreateCase& cc : create_cases())
        {
            Scene s(true, 1, e.loopback);
            if (!cc.cap)
            {
                s.region.mCaps.erase("CreateTaskInventoryItem");
            }
            // the standard contents through the capability
            bnd().requestInventory(s.obj);
            vto::complete_http(standard_reply(0), ::LLCore::HttpStatus(200));
            take_actions();
            s.obj->mRegionp = cc.region ? &s.region : NULL;
            bodies += "case " + cc.label + "\n  before " + state_line(s.obj) + "\n";
            LLPermissions perm;
            perm.init(AGENT, AGENT, LLUUID::null, LLUUID::null);
            perm.initMasks(cc.masks[0], cc.masks[1], cc.masks[2], cc.masks[3], cc.masks[4]);
            bodies += "  permissions " + perm_text(perm) + "\n";
            size_t before = vto::pending_http_count();
            bnd().createInventoryItem(s.obj, cc.asset_type, cc.inv_type, cc.sub_type, cc.name, cc.desc, perm, cc.params,
                [](bool success, const LLSD& result)
                {
                    LLSD shown = result;
                    shown.erase(::LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS);
                    vto::recorder().add(fmt("create.callback success=%d result=", (int)success) + notation(shown)
                                        + fmt(" http_type=%d", result[::LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]["type"].asInteger()));
                });
            bodies += "  call | " + take_actions() + "\n";
            if (vto::pending_http_count() > before)
            {
                const LLSD body = vto::pending_http_body(before);
                const std::string path = "create/" + cc.label + ".llsd";
                o.binary[path] = binary_llsd(body);
                std::ostringstream xml;
                LLSDSerialize::toXML(body, xml);
                std::string xml_text = xml.str();
                while (!xml_text.empty() && (xml_text.back() == '\n' || xml_text.back() == '\r'))
                {
                    xml_text.pop_back();
                }
                bodies += "  post " + vto::pending_http_url(before) + "\n  body " + notation(body) + "\n  body_xml "
                    + xml_text + "\n  body_file " + path + "\n";
                LLSD reply;
                reply["success"] = cc.success;
                vto::complete_http(reply, ::LLCore::HttpStatus(cc.status), "POST");
            }
            bodies += "  after " + state_line(s.obj) + " | " + take_actions() + "\n";
        }
        o.text["messages.txt"] = text;
        o.text["create_item.txt"] = bodies;
    }

    //------------------------------------------------------------------
    // replay / dump
    //------------------------------------------------------------------
    bool read_file(const std::string& path, std::string& out)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
        {
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    std::vector<std::string> content_lines(const std::string& text)
    {
        std::vector<std::string> out;
        size_t p = 0;
        while (p < text.size())
        {
            size_t e = text.find('\n', p);
            std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty() || line[0] != '#')
            {
                out.push_back(line);
            }
            if (e == std::string::npos) break;
            p = e + 1;
        }
        return out;
    }

    std::string digests_text(const std::vector<Digest>& digests)
    {
        std::string out = header("SHA-256 of the canonical text of the large sweeps (the text each sample file excerpts)");
        out += "# <name> <count> <sha256 of all units>, then <name> chunk <k> units <first>-<last> <sha256 of those units>\n";
        out += fmt("# (blocks of %d units; block k starts with the k-th sample of the sample file)\n", SAMPLE_EVERY);
        for (const Digest& d : digests)
        {
            out += d.name + fmt(" %llu ", (unsigned long long)d.count) + d.sha + "\n";
        }
        for (const Digest& d : digests)
        {
            for (const std::string& c : d.chunks)
            {
                out += c + "\n";
            }
        }
        return out;
    }

    void mkdirs_for(const std::string& root, const std::string& rel)
    {
        size_t p = 0;
        while ((p = rel.find('/', p)) != std::string::npos)
        {
            LLFile::mkdir(root + "/" + rel.substr(0, p));
            ++p;
        }
    }

    // Writes (dump) or compares (replay) one corpus. Returns the failure
    // report, empty when everything matches.
    // Every fixture path the sets produced in this run (for the tree check).
    std::set<std::string>& produced_paths()
    {
        static std::set<std::string> p;
        return p;
    }

    bool dump_mode()
    {
        const char* dump = getenv("VKS_TASK_DUMP");
        return dump && *dump;
    }

    // Writes (dump) or compares (replay) one corpus. Returns the failure
    // report, empty when everything matches. In dump mode the report says
    // the fixtures were written and not compared, so that a dump never
    // passes as a test run.
    std::string finish(Outputs& o, const std::string& digest_file)
    {
        if (!o.digests.empty())
        {
            o.text[digest_file] = digests_text(o.digests);
        }
        for (const auto& t : o.text)
        {
            produced_paths().insert(t.first);
        }
        for (const auto& b : o.binary)
        {
            produced_paths().insert(b.first);
        }
        const char* dump = getenv("VKS_TASK_DUMP");
        if (dump && *dump)
        {
            const std::string root = dump;
            LLFile::mkdir(root);
            for (const auto& t : o.text)
            {
                mkdirs_for(root, t.first);
                std::ofstream f((root + "/" + t.first).c_str(), std::ios::binary | std::ios::trunc);
                f.write(t.second.data(), t.second.size());
            }
            for (const auto& b : o.binary)
            {
                mkdirs_for(root, b.first);
                std::ofstream f((root + "/" + b.first).c_str(), std::ios::binary | std::ios::trunc);
                f.write((const char*)b.second.data(), b.second.size());
            }
            std::cerr << "VKS_TASK_DUMP: wrote " << o.text.size() << " text and " << o.binary.size() << " binary files to " << root << std::endl;
            return "VKS_TASK_DUMP: fixtures written to " + root + ", not compared (dump mode always fails)";
        }
        std::string report;
        int failures = 0;
        for (const auto& t : o.text)
        {
            std::string committed;
            if (!read_file(std::string(VKS_TASK_FIXTURE_DIR) + "/" + t.first, committed))
            {
                report += "missing fixture " + t.first + "\n";
                ++failures;
                continue;
            }
            std::vector<std::string> want = content_lines(committed);
            std::vector<std::string> got = content_lines(t.second);
            int shown = 0;
            std::string last_case;
            size_t n = std::max(want.size(), got.size());
            for (size_t i = 0; i < n && shown < 5; ++i)
            {
                const std::string w = i < want.size() ? want[i] : std::string("<end of fixture>");
                const std::string g = i < got.size() ? got[i] : std::string("<end of output>");
                if (i < want.size() && (w.compare(0, 5, "case ") == 0 || w.compare(0, 6, "trace ") == 0 || w.compare(0, 6, "scrub ") == 0))
                {
                    last_case = w;
                }
                if (w != g)
                {
                    report += "difference in " + t.first + " at content line " + std::to_string(i + 1) + " (in: " + last_case
                        + ")\n  expected: " + w + "\n  actual:   " + g + "\n";
                    ++shown;
                    ++failures;
                }
            }
        }
        for (const auto& b : o.binary)
        {
            std::string committed;
            if (!read_file(std::string(VKS_TASK_FIXTURE_DIR) + "/" + b.first, committed))
            {
                report += "missing fixture " + b.first + "\n";
                ++failures;
                continue;
            }
            if (committed.size() != b.second.size() || memcmp(committed.data(), b.second.data(), b.second.size()) != 0)
            {
                report += "input bytes differ: " + b.first + "\n";
                ++failures;
            }
        }
        if (failures)
        {
            report = std::to_string(failures) + " difference(s)\n" + report;
        }
        return report;
    }
}

namespace
{
    // VKS_TASK_ONLY=<n> runs only set n: a development aid, honoured only
    // with VKS_TASK_DEV=1; otherwise a skipped set fails. Any skip also
    // disables the fixture-tree check.
    bool g_incomplete = false;

    bool skip_set(int n)
    {
        const char* only = getenv("VKS_TASK_ONLY");
        if (!(only && *only && atoi(only) != n))
        {
            return false;
        }
        g_incomplete = true;
        const char* dev = getenv("VKS_TASK_DEV");
        if (!(dev && std::string(dev) == "1"))
        {
            tut::ensure("set skipped by VKS_TASK_ONLY without VKS_TASK_DEV=1", false);
        }
        return true;
    }

    // The implementation a set runs against: the viewer's (the Rust
    // decisions and lltaskinventoryexecutor.inl) for a replay; the frozen
    // copies for a dump (the fixtures are recorded from the C++ as it was)
    // and for a replay with VKS_TASK_BOUNDARY=frozen.
    const char* select_boundary()
    {
        const char* which = getenv("VKS_TASK_BOUNDARY");
        const bool frozen = dump_mode() || (which && std::string(which) == "frozen");
        vto::set_boundary(frozen ? NULL : &vto::rust_boundary());
        std::cerr << "boundary: " << bnd().name() << std::endl;
        return bnd().name();
    }

    // The fixture tree must hold exactly the files the sets produce, plus
    // README.md and .gitattributes.
    std::string tree_report()
    {
        std::set<std::string> want = produced_paths();
        want.insert("README.md");
        want.insert(".gitattributes");
        std::set<std::string> have;
        std::error_code ec;
        const std::filesystem::path root(VKS_TASK_FIXTURE_DIR);
        for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->is_regular_file())
            {
                have.insert(std::filesystem::relative(it->path(), root).generic_string());
            }
        }
        std::string report;
        for (const std::string& p : want)
        {
            if (!have.count(p))
            {
                report += "missing from the fixture tree: " + p + "\n";
            }
        }
        for (const std::string& p : have)
        {
            if (!want.count(p))
            {
                report += "unexpected file in the fixture tree: " + p + "\n";
            }
        }
        return report;
    }
}

namespace tut
{
    struct task_oracle_data
    {
    };
    typedef test_group<task_oracle_data> task_oracle_t;
    typedef task_oracle_t::object task_oracle_object_t;
    tut::task_oracle_t tut_task_oracle("llinventory_task_oracle");

    template<> template<>
    void task_oracle_object_t::test<1>()
    {
        set_test_name("environment: template, loopback receiver, LLDir");
        Env& e = env();
        ensure("message system", vto::gMessageSystem->getErrorCode() == 0);
        ensure("loopback receiver", e.ok);
        ensure("ReplyTaskInventory in the template", e.templates.count("ReplyTaskInventory") == 1);
    }

    template<> template<>
    void task_oracle_object_t::test<2>()
    {
        set_test_name("set 1: task inventory files and scrubbed filenames");
        if (skip_set(1)) return;
        env();
        select_boundary();
        Outputs o;
        build_set1(o);
        std::string report = finish(o, "");
        ensure(report, report.empty());
    }

    template<> template<>
    void task_oracle_object_t::test<3>()
    {
        set_test_name("set 2: capability replies");
        if (skip_set(2)) return;
        env();
        select_boundary();
        Outputs o;
        build_set2(o);
        std::string report = finish(o, "digests_set2.txt");
        ensure(report, report.empty());
    }

    template<> template<>
    void task_oracle_object_t::test<4>()
    {
        set_test_name("set 3: serial and request-state traces");
        if (skip_set(3)) return;
        env();
        select_boundary();
        Outputs o;
        auto t0 = std::chrono::steady_clock::now();
        build_set3(o);
        auto t1 = std::chrono::steady_clock::now();
        std::cerr << "set 3: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms" << std::endl;
        std::string report = finish(o, "digests_set3.txt");
        ensure(report, report.empty());
    }

    template<> template<>
    void task_oracle_object_t::test<5>()
    {
        set_test_name("set 4: task-inventory messages");
        if (skip_set(4)) return;
        env();
        select_boundary();
        Outputs o;
        build_set4(o);
        std::string report = finish(o, "digests_set4.txt");
        ensure(report, report.empty());
    }

    template<> template<>
    void task_oracle_object_t::test<6>()
    {
        set_test_name("set 1: random sweep of mutated task files");
        if (skip_set(1)) return;
        env();
        select_boundary();
        Outputs o;
        build_set1_random(o);
        std::string report = finish(o, "digests_set1.txt");
        ensure(report, report.empty());
    }

    template<> template<>
    void task_oracle_object_t::test<7>()
    {
        set_test_name("fixture tree: exactly the produced files, README.md and .gitattributes");
        if (dump_mode() || g_incomplete)
        {
            // a dump already fails; an incomplete development run cannot know the full set
            return;
        }
        std::string report = tree_report();
        ensure(report, report.empty());
    }

    template<> template<>
    void task_oracle_object_t::test<8>()
    {
        set_test_name("frozen copies identical to the live source (check_r4a_copies.py)");
        Env& e = env();
        const std::string out = e.work + "/check_r4a_copies.out";
        std::string cmd = std::string("\"") + VKS_TASK_PYTHON + "\" -I \"" + VKS_TASK_COPY_CHECK + "\" > \"" + out + "\" 2>&1";
#if LL_WINDOWS
        cmd = "\"" + cmd + "\""; // cmd.exe /c strips one pair of outer quotes
#endif
        int rc = std::system(cmd.c_str());
        std::string text;
        read_file(out, text);
        ensure("check_r4a_copies.py failed (rc " + std::to_string(rc) + "):\n" + text, rc == 0);
    }

    template<> template<>
    void task_oracle_object_t::test<9>()
    {
        set_test_name("bench: frozen copies against the Rust decisions (VKS_TASK_BENCH=<rounds>)");
        // The lookups stayed C++ (a Rust lookup copies the keys per call, 25 to
        // 3,000 times slower: doc/rust_port.md, R4a); the rows are the loads and
        // a decision that reads the list (the owner guess of a local update).
        const char* rounds_env = getenv("VKS_TASK_BENCH");
        if (!(rounds_env && *rounds_env))
        {
            return;
        }
        env();
        const int rounds = std::max(1, atoi(rounds_env));
        std::ostringstream report;
        report << "# task inventory bench, best of " << rounds << " rounds, ns (Windows: RelWithDebInfo)\n";
        report << "row\tentries\tfrozen C++\tRust\n";
        vto::TaskInventoryBoundary* sides[2] = { &vto::frozen_boundary(), &vto::rust_boundary() };
        for (int n : { 100, 1000, 10000 })
        {
            std::string file = legacy_object(OBJ, "Contents");
            for (int i = 0; i < n; ++i)
            {
                ItemSpec m = spec_a();
                m.id = LLUUID(fmt("1a000000-0000-4000-8000-%012x", 0x100 + i).c_str());
                m.asset = LLUUID(fmt("a5000000-0000-4000-8000-%012x", 0x100 + i).c_str());
                m.name = fmt("Item %05d", i);
                m.desc = "a description of the item";
                file += legacy_item(m);
            }
            double load[2], update[2];
            for (int side = 0; side < 2; ++side)
            {
                vto::set_boundary(sides[side]);
                double best_load = 1e300, best_update = 1e300;
                for (int r = 0; r < rounds; ++r)
                {
                    Scene s(false, 0);
                    const std::string name = "task_bench.inv";
                    const std::string path = s.cache_path(name);
                    s.write_file(path, file);
                    auto t0 = std::chrono::steady_clock::now();
                    bnd().loadTaskInvFile(s.obj, name);
                    auto t1 = std::chrono::steady_clock::now();
                    best_load = std::min(best_load, (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

                    LLPointer<vto::LLViewerInventoryItem> item = make_item(spec_a());
                    const int updates = std::max(1, 20000 / n);
                    t0 = std::chrono::steady_clock::now();
                    for (int k = 0; k < updates; ++k)
                    {
                        bnd().updateInventoryLocal(s.obj, item, TASK_INVENTORY_ITEM_KEY);
                    }
                    t1 = std::chrono::steady_clock::now();
                    best_update = std::min(best_update, (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / updates);
                }
                load[side] = best_load;
                update[side] = best_update;
            }
            vto::set_boundary(NULL);
            report << fmt("loadTaskInvFile (whole load)\t%d\t%.0f\t%.0f\n", n, load[0], load[1]);
            report << fmt("loadTaskInvFile per entry\t%d\t%.1f\t%.1f\n", n, load[0] / (n + 1), load[1] / (n + 1));
            report << fmt("updateInventoryLocal (owner guess)\t%d\t%.1f\t%.1f\n", n, update[0], update[1]);
        }
        std::cerr << report.str();
        const char* out = getenv("VKS_TASK_BENCH_OUT");
        if (out && *out)
        {
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f << report.str();
        }
    }
}

/**
 * @file llaisupdate_parity_test.cpp
 * @brief The AIS response decision corpus (inventory rung, recorder R3a).
 *
 * Feeds scripted AIS results to the real AISAPI::InvokeAISCommandCoro of
 * llaisapi.cpp (with an invoke function that returns the scripted result
 * instead of making a request), which runs its error branch,
 * AISUpdate(result, type, request_body) and doUpdate(), and the completion
 * callback contract. llaisupdate_parity_stubs.cpp records every call the
 * code makes of the inventory model and the viewer inventory objects, the
 * yields, the outgoing messages, the notifications and the callbacks.
 *
 * Each case runs twice: with AISUpdate's timers never expiring, and with
 * the timers on an injected clock (a scheduled expiry). The recorded op
 * lists and the final skeleton are compared with the committed fixtures
 * in tests/fixtures/inventory/ais/update (binary LLSD, byte for byte);
 * the large case is compared by digest and sample. A difference reports
 * the case, the run, the op index, the field path, expected and actual.
 *
 * With VKS_AISUPDATE_DUMP=<dir> in the environment the test writes the
 * fixtures to <dir> instead of comparing (tools/inventory-fixtures/
 * ais-update/regenerate.py in vkstorm-core-rust drives that). With
 * VKS_AISUPDATE_FULL=<dir> it also writes the full output of the large
 * case there (never committed).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

// The renames llaisupdate_parity_stubs.cpp compiles llaisapi.cpp with, so
// this file sees the same AISUpdate class. They must precede every include.
#define LLTimer vks_r3a_LLTimer
#define suspendUntilNextFrame vks_r3a_suspendUntilNextFrame

#include "linden_common.h"

#include "../llviewerprecompiledheaders.h"

#include "../llaisapi.h"
#include "../llviewercontrol.h"

#include "httprequest.h"
#include "llerrorcontrol.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llsys.h"

#include "../test/lltut.h"

#include "llaisupdate_parity_stubs.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#ifndef VKS_AISUPDATE_FIXTURE_DIR
#error VKS_AISUPDATE_FIXTURE_DIR must name tests/fixtures/inventory/ais/update
#endif
#ifndef VKS_AISUPDATE_TEMPLATE_FILE
#error VKS_AISUPDATE_TEMPLATE_FILE must name scripts/messages/message_template.msg
#endif
#ifndef VKS_AISUPDATE_WORK_DIR
#error VKS_AISUPDATE_WORK_DIR must name a scratch directory in the build tree
#endif

namespace
{
    //------------------------------------------------------------------------
    // SHA-256 (FIPS 180-4), for the digests of the large case.
    //------------------------------------------------------------------------
    struct Sha256
    {
        U32 h[8];
        U8 block[64];
        size_t used = 0;
        U64 total = 0;

        Sha256()
        {
            static const U32 init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
            for (int i = 0; i < 8; ++i)
            {
                h[i] = init[i];
            }
        }

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
                w[i] = ((U32)p[i * 4] << 24) | ((U32)p[i * 4 + 1] << 16) | ((U32)p[i * 4 + 2] << 8) | (U32)p[i * 4 + 3];
            }
            for (int i = 16; i < 64; ++i)
            {
                U32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                U32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            U32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int i = 0; i < 64; ++i)
            {
                U32 S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                U32 ch = (e & f) ^ (~e & g);
                U32 t1 = hh + S1 + ch + k[i] + w[i];
                U32 S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                U32 maj = (a & b) ^ (a & c) ^ (b & c);
                U32 t2 = S0 + maj;
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }

        void update(const void* data, size_t len)
        {
            const U8* p = static_cast<const U8*>(data);
            total += len;
            while (len)
            {
                size_t n = std::min(len, (size_t)64 - used);
                memcpy(block + used, p, n);
                used += n;
                p += n;
                len -= n;
                if (used == 64)
                {
                    compress(block);
                    used = 0;
                }
            }
        }

        std::string hexdigest()
        {
            U64 bits = total * 8;
            U8 pad = 0x80;
            update(&pad, 1);
            U8 zero = 0;
            while (used != 56)
            {
                update(&zero, 1);
            }
            U8 len[8];
            for (int i = 0; i < 8; ++i)
            {
                len[i] = (U8)(bits >> (56 - 8 * i));
            }
            update(len, 8);
            std::ostringstream out;
            for (int i = 0; i < 8; ++i)
            {
                out << std::hex << std::setw(8) << std::setfill('0') << h[i];
            }
            return out.str();
        }
    };

    std::string sha256(const std::string& s)
    {
        Sha256 sha;
        sha.update(s.data(), s.size());
        return sha.hexdigest();
    }

    //------------------------------------------------------------------------
    // Files and LLSD
    //------------------------------------------------------------------------
    const std::string FIXTURES(VKS_AISUPDATE_FIXTURE_DIR);

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

    bool write_file(const std::string& path, const std::string& data)
    {
        std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out.write(data.data(), data.size());
        return (bool)out;
    }

    std::string to_binary(const LLSD& sd)
    {
        std::ostringstream out;
        LLSDSerialize::toBinary(sd, out);
        return out.str();
    }

    LLSD from_binary(const std::string& bytes)
    {
        LLSD sd;
        std::istringstream in(bytes);
        LLSDSerialize::fromBinary(sd, in, (S32)bytes.size());
        return sd;
    }

    // Compact notation, binary as b16 (the message bodies).
    std::string to_line(const LLSD& sd)
    {
        std::ostringstream out;
        LLPointer<LLSDNotationFormatter> f = new LLSDNotationFormatter(false, "", LLSDFormatter::OPTIONS_PRETTY_BINARY);
        f->format(sd, out);
        return out.str();
    }

    // The review rendering beside each binary file: maps open into one line
    // per key, arrays into one line per element (an op, a category, an
    // item), each in compact notation.
    void render_lines(const LLSD& sd, const std::string& path, std::string& out)
    {
        if (sd.isMap())
        {
            for (LLSD::map_const_iterator it = sd.beginMap(); it != sd.endMap(); ++it)
            {
                const std::string sub = path.empty() ? it->first : path + "/" + it->first;
                if (it->second.isMap() && it->second.size() > 0 && it->first != "counts")
                {
                    render_lines(it->second, sub, out);
                }
                else if (it->second.isArray())
                {
                    out += "== " + sub + " (" + std::to_string(it->second.size()) + ")\n";
                    for (S32 i = 0; i < (S32)it->second.size(); ++i)
                    {
                        out += "[" + std::to_string(i) + "] " + to_line(it->second[i]) + "\n";
                    }
                }
                else
                {
                    out += sub + " = " + to_line(it->second) + "\n";
                }
            }
            return;
        }
        out += path + " = " + to_line(sd) + "\n";
    }

    std::string render(const LLSD& sd)
    {
        std::string out;
        render_lines(sd, "", out);
        return out;
    }

    // The response bodies go through the real LLSD XML parser, as the HTTP
    // adapter's do (duplicate map keys and all).
    bool parse_xml_file(const std::string& path, LLSD& out, std::string& error)
    {
        std::string bytes;
        std::string source = path;
        if (path.size() > 3 && path.compare(path.size() - 3, 3, ".gz") == 0)
        {
            const std::string work(VKS_AISUPDATE_WORK_DIR);
            LLFile::mkdir(work);
            source = work + "/gunzipped.xml";
            if (!gunzip_file(path, source))
            {
                error = "cannot gunzip " + path;
                return false;
            }
        }
        if (!read_file(source, bytes))
        {
            error = "cannot read " + source;
            return false;
        }
        std::istringstream in(bytes);
        out = LLSD();
        if (LLSDSerialize::fromXML(out, in) == LLSDParser::PARSE_FAILURE)
        {
            error = "XML parse failure in " + path;
            return false;
        }
        return true;
    }

    std::vector<std::string> read_index()
    {
        std::vector<std::string> ids;
        std::string text;
        if (!read_file(FIXTURES + "/cases/index.txt", text))
        {
            return ids;
        }
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            {
                line.pop_back();
            }
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            ids.push_back(line);
        }
        return ids;
    }

    //------------------------------------------------------------------------
    // Running a case
    //------------------------------------------------------------------------
    struct Case
    {
        std::string id;
        LLSD def;
        std::vector<LLSD> responses;
    };

    bool load_case(const std::string& id, Case& c, std::string& error)
    {
        c.id = id;
        if (!parse_xml_file(FIXTURES + "/cases/" + id + ".case.xml", c.def, error))
        {
            return false;
        }
        const LLSD& steps = c.def["steps"];
        for (LLSD::array_const_iterator it = steps.beginArray(); it != steps.endArray(); ++it)
        {
            LLSD result;
            if (!parse_xml_file(FIXTURES + "/cases/" + (*it)["response"].asString(), result, error))
            {
                return false;
            }
            if (vks_ais::command_from_name((*it)["type"].asString()) < 0)
            {
                error = "unknown command type " + (*it)["type"].asString();
                return false;
            }
            c.responses.push_back(result);
        }
        return true;
    }

    struct RunTiming
    {
        double never_ms = 0.0;
        double sched_ms = 0.0;
    };

    LLSD run_mode(const Case& c, vks_ais::TimerMode mode, double& elapsed_ms)
    {
        const LLSD& sched = c.def["sched"];
        U64 call_us = sched.has("call_us") ? (U64)sched["call_us"].asInteger() : 100;
        U64 frame_us = sched.has("frame_us") ? (U64)sched["frame_us"].asInteger() : 16667;

        const bool trace = getenv("VKS_AISUPDATE_TRACE") != NULL;
        if (trace)
        {
            std::cerr << "[aisupdate] " << c.id << " " << vks_ais::timer_mode_name(mode) << " load" << std::endl;
        }
        vks_ais::skeleton_load(c.def);
        std::vector<U64> others_us;
        for (LLSD::array_const_iterator it = sched["others_us"].beginArray(); it != sched["others_us"].endArray(); ++it)
        {
            others_us.push_back((U64)it->asInteger());
        }
        vks_ais::begin_run(mode, call_us, frame_us, others_us);
        auto t0 = std::chrono::steady_clock::now();
        const LLSD& steps = c.def["steps"];
        for (S32 i = 0; i < steps.size(); ++i)
        {
            const LLSD& step = steps[i];
            if (i > 0 && !step["same_frame"].asBoolean())
            {
                vks_ais::next_frame();
            }
            AISAPI::COMMAND_TYPE type = (AISAPI::COMMAND_TYPE)vks_ais::command_from_name(step["type"].asString());
            LLSD marker = LLSD::emptyMap();
            marker["op"] = "step";
            marker["index"] = i;
            marker["type"] = step["type"].asString();
            vks_ais::record(marker, false);
            vks_ais::set_disconnected(step["disconnected"].asBoolean());
            if (trace)
            {
                std::cerr << "[aisupdate] " << c.id << " step " << i << " " << step["type"].asString() << std::endl;
            }
            try
            {
                vks_ais::invoke_command(type, step["url"].asString(), step["target_id"].asUUID(), step["body"],
                                        c.responses[i], step.has("callback") ? step["callback"].asBoolean() : true);
            }
            catch (const std::exception& e)
            {
                LLSD op = LLSD::emptyMap();
                op["op"] = "exception";
                op["what"] = std::string(e.what());
                vks_ais::record(op, false);
            }
            vks_ais::set_disconnected(false);
        }
        elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        LLSD out = LLSD::emptyMap();
        out["ops"] = vks_ais::take_ops();
        out["final"] = vks_ais::skeleton_dump();
        return out;
    }

    LLSD run_case(const Case& c, RunTiming& timing)
    {
        LLSD out = LLSD::emptyMap();
        out["case"] = c.id;
        out["never"] = run_mode(c, vks_ais::TIMERS_NEVER, timing.never_ms);
        out["sched"] = run_mode(c, vks_ais::TIMERS_SCHEDULED, timing.sched_ms);
        return out;
    }

    //------------------------------------------------------------------------
    // The large case: digest and sample
    //------------------------------------------------------------------------
    bool is_event(const std::string& op)
    {
        return op != "updateItem" && op != "updateCategory" && op != "q.getItem" && op != "q.getCategory"
            && op != "q.getVersion";
    }

    LLSD digest_of(const LLSD& full)
    {
        LLSD out = LLSD::emptyMap();
        out["case"] = full["case"];
        out["sha256"] = sha256(to_binary(full));
        const char* modes[] = {"never", "sched"};
        for (const char* mode : modes)
        {
            const LLSD& run = full[mode];
            const LLSD& ops = run["ops"];
            LLSD m = LLSD::emptyMap();
            m["ops_sha256"] = sha256(to_binary(ops));
            m["final_sha256"] = sha256(to_binary(run["final"]));
            m["op_count"] = (LLSD::Integer)ops.size();
            m["final_categories"] = (LLSD::Integer)run["final"]["categories"].size();
            m["final_items"] = (LLSD::Integer)run["final"]["items"].size();
            LLSD counts = LLSD::emptyMap();
            LLSD head = LLSD::emptyArray();
            LLSD tail = LLSD::emptyArray();
            LLSD events = LLSD::emptyArray();
            const S32 HEAD = 60;
            const S32 TAIL = 40;
            for (S32 i = 0; i < ops.size(); ++i)
            {
                const std::string kind = ops[i]["op"].asString();
                counts[kind] = counts[kind].asInteger() + 1;
                if (i < HEAD)
                {
                    head.append(ops[i]);
                }
                if (i >= (S32)ops.size() - TAIL)
                {
                    tail.append(ops[i]);
                }
                if (is_event(kind))
                {
                    LLSD e = ops[i];
                    e["index"] = i;
                    events.append(e);
                }
            }
            m["counts"] = counts;
            m["head"] = head;
            m["tail"] = tail;
            m["events"] = events;
            out[mode] = m;
        }
        return out;
    }

    //------------------------------------------------------------------------
    // Readable differences
    //------------------------------------------------------------------------
    std::string short_value(const LLSD& v)
    {
        std::string s = to_line(v);
        if (s.size() > 400)
        {
            s = s.substr(0, 400) + "...";
        }
        return s;
    }

    // The first path at which two LLSD values differ.
    bool first_diff(const LLSD& e, const LLSD& a, const std::string& path, std::string& report)
    {
        if (e.type() != a.type())
        {
            report = path + ": expected " + short_value(e) + " actual " + short_value(a);
            return true;
        }
        if (e.isMap())
        {
            for (LLSD::map_const_iterator it = e.beginMap(); it != e.endMap(); ++it)
            {
                if (!a.has(it->first))
                {
                    report = path + "/" + it->first + ": expected " + short_value(it->second) + " actual (missing)";
                    return true;
                }
                if (first_diff(it->second, a[it->first], path + "/" + it->first, report))
                {
                    return true;
                }
            }
            for (LLSD::map_const_iterator it = a.beginMap(); it != a.endMap(); ++it)
            {
                if (!e.has(it->first))
                {
                    report = path + "/" + it->first + ": expected (missing) actual " + short_value(it->second);
                    return true;
                }
            }
            return false;
        }
        if (e.isArray())
        {
            S32 n = (S32)std::min(e.size(), a.size());
            for (S32 i = 0; i < n; ++i)
            {
                if (first_diff(e[i], a[i], path + "[" + std::to_string(i) + "]", report))
                {
                    return true;
                }
            }
            if (e.size() != a.size())
            {
                report = path + ": expected " + std::to_string(e.size()) + " entries, actual " + std::to_string(a.size())
                    + (e.size() > n ? "; first missing: " + short_value(e[n]) : "; first extra: " + short_value(a[n]));
                return true;
            }
            return false;
        }
        if (to_binary(e) != to_binary(a))
        {
            report = path + ": expected " + short_value(e) + " actual " + short_value(a);
            return true;
        }
        return false;
    }

    //------------------------------------------------------------------------
    // Dump mode output
    //------------------------------------------------------------------------
    std::string header_text(const std::string& what)
    {
        const char* commit = getenv("VKS_AISUPDATE_VIEWER_COMMIT");
#if LL_WINDOWS
        const char* platform = "windows";
#elif LL_LINUX
        const char* platform = "linux";
#else
        const char* platform = "other";
#endif
        std::string h;
        h += "# " + what + "\n";
        h += "# generator: INTEGRATION_TEST_llaisupdate_parity (indra/newview/tests/llaisupdate_parity_test.cpp), dump mode\n";
        h += std::string("# viewer commit: ") + (commit && *commit ? commit : "unknown") + "\n";
        h += std::string("# platform: ") + platform + "\n";
        return h;
    }
}

namespace tut
{
    struct aisupdate_data
    {
        aisupdate_data()
        {
            static bool once = false;
            if (!once)
            {
                once = true;
                gSavedSettings.declareBOOL("DebugAvatarAppearanceMessage", false, "pinned by the AISUpdate parity test");
                gSavedSettings.declareBOOL("FSUseAis3Api", true, "pinned by the AISUpdate parity test");
                vks_ais::message_system_init(VKS_AISUPDATE_TEMPLATE_FILE);
                // The coroutine creates an LLCore::HttpRequest, which counts
                // itself in the service's statistics: the service must exist
                // (its thread is never started; no request is made).
                LLCore::HttpRequest::createService();
            }
        }
    };
    typedef test_group<aisupdate_data> aisupdate_group;
    typedef aisupdate_group::object aisupdate_object;
    tut::aisupdate_group aisupdate_test("llaisupdate_parity");

    // The corpus: every case in cases/index.txt, in order (the order is
    // part of the record: the 403 notification's first-call flag is a
    // function-static of the coroutine and lives for the process).
    template<> template<>
    void aisupdate_object::test<1>()
    {
        set_test_name("AIS update decision corpus");
        ensure("message system", gMessageSystem != NULL && gMessageSystem->isOK());

        const std::vector<std::string> ids = read_index();
        ensure("case index is present and not empty", !ids.empty());

        const char* dump_dir = getenv("VKS_AISUPDATE_DUMP");
        const bool dump = dump_dir && *dump_dir;
        const char* full_dir = getenv("VKS_AISUPDATE_FULL");
        std::string manifest;
        if (dump)
        {
            LLFile::mkdir(dump_dir);
            manifest = header_text("llaisupdate_parity expected outputs: <case> <file> <sha256 of the binary file>");
        }

        // Log arguments are not evaluated: getters called only to build a
        // warning or a debug line would otherwise appear as model queries.
        LLError::ELevel saved_level = LLError::getDefaultLevel();
        LLError::setDefaultLevel(LLError::LEVEL_ERROR);

        std::string failures;
        int failed = 0;
        int compared = 0;
        for (const std::string& id : ids)
        {
            Case c;
            std::string error;
            if (!load_case(id, c, error))
            {
                failures += id + ": " + error + "\n";
                ++failed;
                continue;
            }
            RunTiming timing;
            LLSD actual = run_case(c, timing);
            const bool large = c.def["large"].asBoolean();
            if (large)
            {
                std::cout << "\n[aisupdate] " << id << ": never " << std::fixed << std::setprecision(1) << timing.never_ms
                          << " ms, sched " << timing.sched_ms << " ms, ops " << actual["never"]["ops"].size()
                          << " / " << actual["sched"]["ops"].size() << std::endl;
            }
            if (full_dir && *full_dir && large)
            {
                LLFile::mkdir(full_dir);
                write_file(std::string(full_dir) + "/" + id + ".full.llsd", to_binary(actual));
                write_file(std::string(full_dir) + "/" + id + ".full.txt", render(actual));
            }
            const LLSD record = large ? digest_of(actual) : actual;
            const std::string suffix = large ? ".digest" : ".expected";
            const std::string bytes = to_binary(record);
            if (dump)
            {
                write_file(std::string(dump_dir) + "/" + id + suffix + ".llsd", bytes);
                write_file(std::string(dump_dir) + "/" + id + suffix + ".txt",
                           header_text("case " + id + (large ? " (digest and sample of the full output)" : "")
                                       + "; review rendering of " + id + suffix + ".llsd, one op per line") + render(record));
                manifest += id + " " + id + suffix + ".llsd " + sha256(bytes) + "\n";
                continue;
            }
            std::string expected_bytes;
            const std::string expected_path = FIXTURES + "/expected/" + id + suffix + ".llsd";
            if (!read_file(expected_path, expected_bytes))
            {
                failures += id + ": missing " + expected_path + "\n";
                ++failed;
                continue;
            }
            ++compared;
            if (expected_bytes == bytes)
            {
                continue;
            }
            ++failed;
            LLSD expected = from_binary(expected_bytes);
            std::string report;
            const char* modes[] = {"never", "sched"};
            for (const char* mode : modes)
            {
                if (large)
                {
                    if (first_diff(expected[mode], record[mode], std::string(mode), report))
                    {
                        break;
                    }
                    continue;
                }
                const LLSD& eops = expected[mode]["ops"];
                const LLSD& aops = record[mode]["ops"];
                S32 n = (S32)std::min(eops.size(), aops.size());
                for (S32 i = 0; i < n && report.empty(); ++i)
                {
                    first_diff(eops[i], aops[i], std::string(mode) + " op " + std::to_string(i) + " (" + eops[i]["op"].asString() + ")", report);
                }
                if (report.empty() && eops.size() != aops.size())
                {
                    report = std::string(mode) + ": expected " + std::to_string(eops.size()) + " ops, actual " + std::to_string(aops.size())
                        + (eops.size() > n ? "; first missing: " + short_value(eops[n]) : "; first extra: " + short_value(aops[n]));
                }
                if (report.empty())
                {
                    first_diff(expected[mode]["final"], record[mode]["final"], std::string(mode) + " final", report);
                }
                if (!report.empty())
                {
                    break;
                }
            }
            if (report.empty())
            {
                first_diff(expected, record, "", report);
            }
            failures += "case " + id + ": " + (report.empty() ? std::string("binary bytes differ") : report) + "\n";
        }

        LLError::setDefaultLevel(saved_level);

        if (dump)
        {
            write_file(std::string(dump_dir) + "/manifest.txt", manifest);
            std::cout << "\n[aisupdate] dumped " << ids.size() << " cases to " << dump_dir << std::endl;
        }
        else
        {
            std::cout << "\n[aisupdate] compared " << compared << " cases, " << failed << " differ" << std::endl;
        }
        if (!failures.empty())
        {
            std::cout << failures << std::endl;
        }
        ensure("AIS update corpus differs from the fixtures:\n" + failures, failed == 0);
    }
}

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

#include "linden_common.h"

#include "../llviewerprecompiledheaders.h"

#include "../llviewercontrol.h"

#include "httprequest.h"
#include "llerrorcontrol.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llsys.h"

#include "../test/lltut.h"

#include "llaisupdate_parity_stubs.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <set>
#if LL_WINDOWS
#include <process.h>
#define vks_getpid _getpid
#else
#include <unistd.h>
#define vks_getpid getpid
#endif
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
#ifndef VKS_AISUPDATE_PYTHON
#error VKS_AISUPDATE_PYTHON must name the Python interpreter
#endif
#ifndef VKS_AISUPDATE_COPY_CHECK
#error VKS_AISUPDATE_COPY_CHECK must name tests/check_r3a_copies.py
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
    // The fixture tree: the compiled-in one, or VKS_AISUPDATE_FIXTURES
    // (regenerate.py replays a freshly written tree before it replaces the
    // committed one).
    std::string fixtures_dir()
    {
        const char* o = getenv("VKS_AISUPDATE_FIXTURES");
        return (o && *o) ? std::string(o) : std::string(VKS_AISUPDATE_FIXTURE_DIR);
    }

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

    // The sched run's final skeleton is shown in full only where it differs
    // from the never run's (the binary file always holds both).
    std::string render(const LLSD& sd)
    {
        std::string out;
        if (sd.has("never") && sd.has("sched") && sd["sched"].has("final")
            && to_binary(sd["sched"]["final"]) == to_binary(sd["never"]["final"]))
        {
            LLSD shown = sd;
            shown["sched"]["final"] = "(same as never/final)";
            render_lines(shown, "", out);
            return out;
        }
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
            // per process: concurrent test runs must not share it
            source = work + "/gunzipped." + std::to_string(vks_getpid()) + ".xml";
            if (!gunzip_file(path, source))
            {
                error = "cannot gunzip " + path;
                return false;
            }
        }
        bool ok = read_file(source, bytes);
        if (source != path)
        {
            LLFile::remove(source);
        }
        if (!ok)
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
        if (!read_file(fixtures_dir() + "/cases/index.txt", text))
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
        std::set<std::string> files;    // every input file the case uses
    };

    bool load_response(const std::string& name, Case& c, LLSD& out, std::string& error)
    {
        if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        {
            error = "bad response file name '" + name + "'";
            return false;
        }
        c.files.insert(name);
        return parse_xml_file(fixtures_dir() + "/cases/" + name, out, error);
    }

    bool load_case(const std::string& id, Case& c, std::string& error)
    {
        c.id = id;
        c.files.insert(id + ".case.xml");
        if (!parse_xml_file(fixtures_dir() + "/cases/" + id + ".case.xml", c.def, error))
        {
            return false;
        }
        if (c.def["id"].asString() != id)
        {
            error = "case file names id '" + c.def["id"].asString() + "'";
            return false;
        }
        LLSD& steps = c.def["steps"];
        for (S32 i = 0; i < (S32)steps.size(); ++i)
        {
            LLSD& step = steps[i];
            LLSD result;
            if (step.has("response") && !load_response(step["response"].asString(), c, result, error))
            {
                return false;
            }
            if (!step.has("response") && !step.has("call"))
            {
                error = "step " + std::to_string(i) + " has no response";
                return false;
            }
            if (!step.has("call") && vks_ais::command_from_name(step["type"].asString()) < 0)
            {
                error = "unknown command type " + step["type"].asString();
                return false;
            }
            // The results of the steps hooks run.
            LLSD& hooks = step["hooks"];
            for (S32 h = 0; h < (S32)hooks.size(); ++h)
            {
                LLSD& actions = hooks[h]["actions"];
                for (S32 a = 0; a < (S32)actions.size(); ++a)
                {
                    if (actions[a].has("run_step"))
                    {
                        LLSD nested;
                        if (!load_response(actions[a]["run_step"]["response"].asString(), c, nested, error))
                        {
                            return false;
                        }
                        actions[a]["result"] = nested;
                    }
                }
            }
            c.responses.push_back(result);
        }
        return true;
    }

    std::vector<U64> u64_list(const LLSD& a)
    {
        std::vector<U64> out;
        for (LLSD::array_const_iterator it = a.beginArray(); it != a.endArray(); ++it)
        {
            out.push_back((U64)it->asInteger());
        }
        return out;
    }

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
        vks_ais::set_case(c.id, mode);
        vks_ais::skeleton_load(c.def);
        vks_ais::begin_run(mode, call_us, frame_us, u64_list(sched["others_us"]), u64_list(sched["yield_extra_us"]));
        auto t0 = std::chrono::steady_clock::now();
        const LLSD& steps = c.def["steps"];
        for (S32 i = 0; i < (S32)steps.size(); ++i)
        {
            const LLSD& step = steps[i];
            if (i > 0 && !step["same_frame"].asBoolean())
            {
                vks_ais::next_frame();
            }
            LLSD marker = LLSD::emptyMap();
            marker["op"] = "step";
            marker["index"] = i;
            marker["type"] = step.has("call") ? step["call"].asString() : step["type"].asString();
            vks_ais::record(marker, false);
            vks_ais::set_disconnected(step["disconnected"].asBoolean());
            if (trace)
            {
                std::cerr << "[aisupdate] " << c.id << " step " << i << " " << marker["type"].asString() << std::endl;
            }
            try
            {
                vks_ais::run_step(step, c.responses[i]);
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

    //------------------------------------------------------------------------
    // The large case: digests and samples
    //------------------------------------------------------------------------
    const S32 CHUNK = 2000;

    bool is_event(const std::string& op)
    {
        return op != "updateItem" && op != "updateCategory" && op != "q.getItem" && op != "q.getCategory"
            && op != "q.getVersion";
    }

    LLSD digest_of_run(const LLSD& run)
    {
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
        LLSD chunks = LLSD::emptyArray();
        const S32 HEAD = 60;
        const S32 TAIL = 40;
        const S32 n = (S32)ops.size();
        for (S32 i = 0; i < n; ++i)
        {
            const std::string kind = ops[i]["op"].asString();
            counts[kind] = counts[kind].asInteger() + 1;
            if (i < HEAD)
            {
                head.append(ops[i]);
            }
            if (i >= n - TAIL)
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
        // Chunk digests: SHA-256 of the binary LLSD array of ops [first, last].
        for (S32 first = 0; first < n; first += CHUNK)
        {
            S32 last = std::min(n, first + CHUNK) - 1;
            LLSD part = LLSD::emptyArray();
            for (S32 i = first; i <= last; ++i)
            {
                part.append(ops[i]);
            }
            LLSD ch = LLSD::emptyMap();
            ch["first"] = first;
            ch["last"] = last;
            ch["sha256"] = sha256(to_binary(part));
            chunks.append(ch);
        }
        m["counts"] = counts;
        m["head"] = head;
        m["tail"] = tail;
        m["events"] = events;
        m["chunks"] = chunks;
        return m;
    }

    LLSD digest_of(const LLSD& full)
    {
        LLSD out = LLSD::emptyMap();
        out["case"] = full["case"];
        out["sha256"] = sha256(to_binary(full));
        out["never"] = digest_of_run(full["never"]);
        out["sched"] = digest_of_run(full["sched"]);
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
                    + ((S32)e.size() > n ? "; first missing: " + short_value(e[n]) : "; first extra: " + short_value(a[n]));
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

    // One run of one case, readable: the first differing op (by index, op
    // kind and field path), else the final skeleton; for the large case the
    // first differing chunk of ops, then the sample.
    std::string describe(const LLSD& expected, const LLSD& actual, const char* mode, bool large)
    {
        std::string report;
        if (large)
        {
            const LLSD& ec = expected["chunks"];
            const LLSD& ac = actual["chunks"];
            for (S32 i = 0; i < (S32)std::min(ec.size(), ac.size()); ++i)
            {
                if (ec[i]["sha256"].asString() != ac[i]["sha256"].asString())
                {
                    report = std::string(mode) + " ops [" + std::to_string(ec[i]["first"].asInteger()) + ".."
                        + std::to_string(ec[i]["last"].asInteger()) + "] differ (chunk digest); ";
                    break;
                }
            }
            std::string detail;
            first_diff(expected, actual, mode, detail);
            return report + detail;
        }
        const LLSD& eops = expected["ops"];
        const LLSD& aops = actual["ops"];
        S32 n = (S32)std::min(eops.size(), aops.size());
        for (S32 i = 0; i < n && report.empty(); ++i)
        {
            first_diff(eops[i], aops[i], std::string(mode) + " op " + std::to_string(i) + " (" + eops[i]["op"].asString() + ")", report);
        }
        if (report.empty() && eops.size() != aops.size())
        {
            report = std::string(mode) + ": expected " + std::to_string(eops.size()) + " ops, actual " + std::to_string(aops.size())
                + ((S32)eops.size() > n ? "; first missing: " + short_value(eops[n]) : "; first extra: " + short_value(aops[n]));
        }
        if (report.empty())
        {
            first_diff(expected["final"], actual["final"], std::string(mode) + " final", report);
        }
        if (report.empty())
        {
            first_diff(expected, actual, mode, report);
        }
        return report.empty() ? std::string(mode) + ": binary bytes differ" : report;
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

    //------------------------------------------------------------------------
    // The corpus, run once per process (case after case in index order: the
    // 403 alert flag is a function-static of the coroutine).
    //------------------------------------------------------------------------
    struct Corpus
    {
        std::vector<std::string> ids;
        std::vector<LLSD> records;      // the expected-file form: full, or digest for the large case
        std::vector<bool> large;
        std::set<std::string> case_files;
        std::string errors;             // load failures, unexpected exceptions, tid failures
        bool dump = false;
    };

    std::set<std::string> list_dir(const std::string& dir)
    {
        std::set<std::string> out;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(dir), ec))
        {
            out.insert(e.path().filename().string());
        }
        return out;
    }

    // The fixture tree must hold exactly what the index names: a missing or
    // an unexpected file fails.
    std::string check_tree(const Corpus& c)
    {
        std::string out;
        const std::string root = fixtures_dir();
        std::set<std::string> want_top = {".gitattributes", "README.md", "cases", "expected"};
        std::set<std::string> have_top = list_dir(root);
        std::set<std::string> want_cases = c.case_files;
        want_cases.insert("index.txt");
        std::set<std::string> want_expected = {"manifest.txt"};
        for (size_t i = 0; i < c.ids.size(); ++i)
        {
            const std::string suffix = c.large[i] ? ".digest" : ".expected";
            want_expected.insert(c.ids[i] + suffix + ".llsd");
            want_expected.insert(c.ids[i] + suffix + ".txt");
        }
        struct { const char* name; const std::set<std::string>& want; std::set<std::string> have; } dirs[] = {
            {"", want_top, have_top},
            {"cases/", want_cases, list_dir(root + "/cases")},
            {"expected/", want_expected, list_dir(root + "/expected")}};
        for (auto& d : dirs)
        {
            for (const std::string& f : d.want)
            {
                if (!d.have.count(f))
                {
                    out += "missing fixture file " + std::string(d.name) + f + "\n";
                }
            }
            for (const std::string& f : d.have)
            {
                if (!d.want.count(f))
                {
                    out += "unexpected fixture file " + std::string(d.name) + f + "\n";
                }
            }
        }
        return out;
    }

    const Corpus& corpus()
    {
        static Corpus* c = NULL;
        if (c)
        {
            return *c;
        }
        c = new Corpus;
        const char* dump_dir = getenv("VKS_AISUPDATE_DUMP");
        c->dump = dump_dir && *dump_dir;
        const char* full_dir = getenv("VKS_AISUPDATE_FULL");
        c->ids = read_index();
        if (c->ids.empty())
        {
            c->errors += "case index missing or empty: " + fixtures_dir() + "/cases/index.txt\n";
        }
        std::string manifest;
        if (c->dump)
        {
            LLFile::mkdir(dump_dir);
            manifest = header_text("llaisupdate_parity expected outputs: <case> <file> <sha256 of the binary file>");
        }

        // Log arguments are not evaluated: getters called only to build a
        // warning or a debug line would otherwise appear as model queries.
        LLError::ELevel saved_level = LLError::getDefaultLevel();
        LLError::setDefaultLevel(LLError::LEVEL_ERROR);
        for (const std::string& id : c->ids)
        {
            Case kase;
            std::string error;
            if (!load_case(id, kase, error))
            {
                c->errors += id + ": " + error + "\n";
                c->records.push_back(LLSD());
                c->large.push_back(false);
                continue;
            }
            c->case_files.insert(kase.files.begin(), kase.files.end());
            double never_ms = 0.0, sched_ms = 0.0;
            LLSD actual = LLSD::emptyMap();
            actual["case"] = id;
            actual["never"] = run_mode(kase, vks_ais::TIMERS_NEVER, never_ms);
            actual["sched"] = run_mode(kase, vks_ais::TIMERS_SCHEDULED, sched_ms);
            const char* modes[] = {"never", "sched"};
            for (const char* mode : modes)
            {
                const LLSD& ops = actual[mode]["ops"];
                for (S32 i = 0; i < (S32)ops.size(); ++i)
                {
                    const std::string kind = ops[i]["op"].asString();
                    if (kind == "exception")
                    {
                        c->errors += id + " " + mode + ": exception " + ops[i]["what"].asString() + "\n";
                    }
                    // The shutdown contract, in both runs: after the stop no
                    // callback and no model call, the model as it was then.
                    if (kind == "abort" && (ops[i]["ops_after_stop"].asInteger() != 0 || !ops[i]["model_unchanged"].asBoolean()))
                    {
                        c->errors += id + " " + mode + " op " + std::to_string(i) + ": the shutdown contract is broken ("
                            + std::to_string(ops[i]["ops_after_stop"].asInteger()) + " ops after the stop, model "
                            + (ops[i]["model_unchanged"].asBoolean() ? "unchanged" : "changed") + ")\n";
                    }
                }
            }
            const bool large = kase.def["large"].asBoolean();
            if (large)
            {
                std::cout << "\n[aisupdate] " << id << ": never " << std::fixed << std::setprecision(1) << never_ms
                          << " ms, sched " << sched_ms << " ms, ops " << actual["never"]["ops"].size()
                          << " / " << actual["sched"]["ops"].size() << std::endl;
                if (full_dir && *full_dir)
                {
                    LLFile::mkdir(full_dir);
                    write_file(std::string(full_dir) + "/" + id + ".full.llsd", to_binary(actual));
                    write_file(std::string(full_dir) + "/" + id + ".full.txt", render(actual));
                }
            }
            const LLSD record = large ? digest_of(actual) : actual;
            c->records.push_back(record);
            c->large.push_back(large);
            if (c->dump)
            {
                const std::string suffix = large ? ".digest" : ".expected";
                const std::string bytes = to_binary(record);
                write_file(std::string(dump_dir) + "/" + id + suffix + ".llsd", bytes);
                write_file(std::string(dump_dir) + "/" + id + suffix + ".txt",
                           header_text("case " + id + (large ? " (digest and sample of the full output)" : "")
                                       + "; review rendering of " + id + suffix + ".llsd, one op per line") + render(record));
                manifest += id + " " + id + suffix + ".llsd " + sha256(bytes) + "\n";
            }
        }
        LLError::setDefaultLevel(saved_level);
        for (const std::string& f : vks_ais::tid_failures())
        {
            c->errors += f + "\n";
        }
        if (c->dump)
        {
            write_file(std::string(dump_dir) + "/manifest.txt", manifest);
            std::cout << "\n[aisupdate] dumped " << c->ids.size() << " cases to " << dump_dir << std::endl;
        }
        return *c;
    }

    // Compares one run of every case with the fixtures.
    void compare_runs(const char* mode)
    {
        const Corpus& c = corpus();
        tut::ensure("AIS update corpus could not be run:\n" + c.errors, c.errors.empty());
        tut::ensure(std::string("dump mode: fixtures written to VKS_AISUPDATE_DUMP; nothing compared (failing on purpose)"), !c.dump);
        std::string failures = check_tree(c);
        int failed = 0;
        for (size_t i = 0; i < c.ids.size(); ++i)
        {
            const std::string& id = c.ids[i];
            const std::string suffix = c.large[i] ? ".digest" : ".expected";
            std::string bytes;
            if (!read_file(fixtures_dir() + "/expected/" + id + suffix + ".llsd", bytes))
            {
                continue;   // reported by check_tree
            }
            LLSD expected = from_binary(bytes);
            const LLSD& actual = c.records[i];
            if (to_binary(expected[mode]) == to_binary(actual[mode]) && expected["case"].asString() == id)
            {
                continue;
            }
            ++failed;
            failures += "case " + id + ": " + describe(expected[mode], actual[mode], mode, c.large[i]) + "\n";
        }
        std::cout << "\n[aisupdate] check " << mode << ": compared " << c.ids.size() << " cases, " << failed << " differ"
                  << std::endl;
        if (!failures.empty())
        {
            std::cout << failures << std::endl;
        }
        tut::ensure(std::string("AIS update corpus, check '") + mode + "', differs from the fixtures:\n" + failures,
                    failures.empty());
    }

    // Runs a command line; true on exit status 0.
    bool run_command(const std::string& cmd)
    {
#if LL_WINDOWS
        // cmd.exe /c strips one pair of outer quotes.
        int rc = std::system(("\"" + cmd + "\"").c_str());
#else
        int rc = std::system(cmd.c_str());
#endif
        return rc == 0;
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

    // Check "never": every case's op list (model calls, queries, messages,
    // notifications, callbacks, hooks) and final skeleton with the timers
    // never expiring; the fixture tree; the write tids. Mandatory.
    template<> template<>
    void aisupdate_object::test<1>()
    {
        set_test_name("never: op lists and query streams");
        ensure("message system", gMessageSystem && gMessageSystem->isOK());
        compare_runs("never");
    }

    // Check "sched": the same with the timers on the injected clock, which
    // adds the yields (and the scripted shutdown aborts). A port that moves
    // the yield points may relax this check only as the README describes.
    template<> template<>
    void aisupdate_object::test<2>()
    {
        set_test_name("sched: yield points");
        compare_runs("sched");
    }

    // The drift guard: the frozen copies against the live source, the
    // fingerprints of the live functions the stub emulates, the identifier
    // fence (tests/check_r3a_copies.py).
    template<> template<>
    void aisupdate_object::test<3>()
    {
        set_test_name("drift guard: frozen copies and emulated functions");
        const std::string cmd = std::string("\"") + VKS_AISUPDATE_PYTHON + "\" \"" + VKS_AISUPDATE_COPY_CHECK + "\"";
        ensure("check_r3a_copies.py failed (see its output above): " + cmd, run_command(cmd));
    }

    template<> template<>
    void aisupdate_object::test<4>()
    {
        // The bench (VKS_AISUPDATE_BENCH=<rounds>): prints, never fails.
        set_test_name("bench: the large reply, C++ before R3a against Rust");
        const char* rounds = getenv("VKS_AISUPDATE_BENCH");
        if (!rounds || !*rounds)
        {
            return;
        }
        const std::string gz = fixtures_dir() + "/cases/s10a_large_recursive.0.xml.gz";
        const std::string work(VKS_AISUPDATE_WORK_DIR);
        LLFile::mkdir(work);
        const std::string xml = work + "/bench." + std::to_string(vks_getpid()) + ".xml";
        std::string body;
        ensure("cannot gunzip " + gz, gunzip_file(gz, xml) && read_file(xml, body));
        LLFile::remove(xml);
        std::cout << vks_ais::bench(body, std::max(1, atoi(rounds))) << std::flush;
        // A typical small reply (a folder fetch at depth 0: 3 items, a link,
        // a subfolder), many rounds: the per-reply cost of the crossing.
        std::string small;
        ensure("cannot read s01a", read_file(fixtures_dir() + "/cases/s01a_children_depth0_full.0.xml", small));
        std::cout << vks_ais::bench(small, 2000) << std::flush;
    }
}

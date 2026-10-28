/**
 * @file llinvfetch_parity_test.cpp
 * @brief The inventory fetch wire oracle (recorder R3b; survey 3 section
 *        8.2 layer B, and the Second Life fallbacks).
 *
 * Test 1 runs check_r3b_copies.py: the frozen copies in
 * llinvfetch_parity_support.cpp still match the live source.
 *
 * Test 2 runs every scenario of llinvfetch_parity_scenarios.cpp against the
 * mock AIS / legacy capability server (vksinventory_mock_server.py, which
 * runs this executable) and compares each scenario's record (request
 * counts, the request log byte for byte after normalisation, the
 * side-effect journal with the scheduler's states, the public fetch state,
 * the final model) and its response bodies with
 * tests/fixtures/inventory/ais/fetch/<library> (msvc or libstdcxx: the
 * model's maps are std::unordered_map since upstream 28fee038e0, so request
 * and queue order are the standard library's). The fixture tree must hold
 * exactly the files a run produces; index.txt and manifest.txt must agree
 * with it.
 *
 * Environment:
 *   VKS_INVFETCH_DUMP=<dir>      write the fixtures to <dir>; the test then
 *                                fails on purpose (a dump is not a replay)
 *   VKS_INVFETCH_FIXTURES=<dir>  replay against <dir> instead of the
 *                                committed tree (the regeneration script's
 *                                check of a new dump)
 *   VKS_INVFETCH_ONLY=a,b        run only these scenarios; refused unless
 *                                VKS_INVFETCH_DEV=1 (diagnostics only: a
 *                                subset never passes as a replay)
 *   VKS_INVFETCH_ACTUAL=<dir>    replay also writes each scenario's output to
 *                                <dir> (diagnostics)
 *   VKS_INVFETCH_COMMIT=<sha>    the viewer commit written into the headers
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "llinvfetch_parity_scenarios.h"

#include "lltut.h"
#include "lldir.h"
#include "llfile.h"


#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#if LL_WINDOWS
#include <process.h>
#define vks_getpid _getpid
#else
#include <unistd.h>
#define vks_getpid getpid
#endif

#ifndef VKS_INVFETCH_FIXTURE_DIR
#error VKS_INVFETCH_FIXTURE_DIR must name the fixture directory
#endif

namespace fs = std::filesystem;

namespace
{
    std::string getenv_str(const char* name)
    {
        const char* v = getenv(name);
        return v ? std::string(v) : std::string();
    }

    // The C++ standard library this test runs on, as the fixture tree names it.
    const char* stdlib_name()
    {
#if LL_WINDOWS
        return "msvc";
#else
        return "libstdcxx";
#endif
    }

    std::string platform()
    {
#if LL_WINDOWS
        return "windows-msvc";
#elif defined(__clang__)
        return "linux-clang";
#else
        return "linux-gcc" + std::to_string(__GNUC__);
#endif
    }

    std::string header(const vks_fetch::ScenarioInfo& info, const std::string& commit)
    {
        std::string h;
        h += "# vkstorm inventory fetch wire oracle (recorder R3b): " + info.id + "\n";
        h += "# " + info.title + "\n";
        h += "# generator: INTEGRATION_TEST_llinvfetch_parity with VKS_INVFETCH_DUMP (indra/newview/tests/llinvfetch_parity_test.cpp)\n";
        h += "#            under indra/newview/tests/vksinventory_mock_server.py\n";
        h += "# viewer commit: " + commit + "\n";
        h += "# platform: " + platform() + "\n";
        h += "# seed: none (patterned ids, no random input; tid and viewer-generated ids normalised)\n";
        h += "# format: README.md\n";
        return h;
    }

    std::string responses_header(const std::string& id)
    {
        return "# vkstorm inventory fetch wire oracle (recorder R3b): response bodies of " + id + "\n"
               "# (normalised as the request log; compared byte for byte, and by digest in the scenario file)\n";
    }

    // Lines that are not comments, CR stripped.
    std::vector<std::string> data_lines(const std::string& text)
    {
        std::vector<std::string> out;
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            out.push_back(line);
        }
        return out;
    }

    // The first difference of two case lists, readable: the case, the line,
    // the field (the key=value token holding the first differing column, or
    // the line's leading tag), and a window of the two lines around it.
    std::string field_at(const std::string& l, size_t col)
    {
        size_t a = l.rfind(' ', col == 0 ? 0 : col - 1);
        a = (a == std::string::npos || col == 0) ? 0 : a + 1;
        size_t b = l.find(' ', a);
        std::string tok = l.substr(a, b == std::string::npos ? std::string::npos : b - a);
        size_t eq = tok.find('=');
        if (eq != std::string::npos && eq > 0) return tok.substr(0, eq);
        size_t t = l.find(' ');
        return "'" + l.substr(0, t) + "' column " + std::to_string(col);
    }

    std::string window(const std::string& l, size_t col)
    {
        size_t a = col > 80 ? col - 80 : 0;
        std::string w = l.substr(a, 160);
        return (a ? "..." : "") + w + (a + 160 < l.size() ? "..." : "");
    }

    std::string compare_report(const vks_inv::Cases& expected, const vks_inv::Cases& actual, size_t max_reports)
    {
        std::ostringstream report;
        size_t reports = 0;
        std::map<std::string, const vks_inv::Case*> by_id;
        for (const vks_inv::Case& c : actual) by_id[c.id] = &c;
        for (const vks_inv::Case& e : expected)
        {
            if (reports >= max_reports) break;
            auto it = by_id.find(e.id);
            if (it == by_id.end()) { report << "case " << e.id << ": not produced\n"; ++reports; continue; }
            const vks_inv::Case& a = *it->second;
            const size_t n = std::max(e.lines.size(), a.lines.size());
            for (size_t i = 0; i < n; ++i)
            {
                const std::string ev = i < e.lines.size() ? e.lines[i] : std::string("<end of case>");
                const std::string av = i < a.lines.size() ? a.lines[i] : std::string("<end of case>");
                if (ev == av) continue;
                size_t col = 0;
                while (col < ev.size() && col < av.size() && ev[col] == av[col]) ++col;
                report << "case " << e.id << " line " << (i + 1) << " field " << field_at(ev, col) << " (column " << col << ")\n"
                       << "  expected: " << window(ev, col) << "\n"
                       << "  actual:   " << window(av, col) << "\n";
                ++reports;
                break;
            }
        }
        std::set<std::string> ids;
        for (const vks_inv::Case& e : expected) ids.insert(e.id);
        for (const vks_inv::Case& a : actual)
        {
            if (!ids.count(a.id)) report << "case " << a.id << ": produced but not in the fixture\n";
        }
        return report.str();
    }

    // Files below dir, as paths relative to it with '/'.
    std::set<std::string> tree(const std::string& dir)
    {
        std::set<std::string> out;
        if (!fs::exists(dir)) return out;
        for (const auto& e : fs::recursive_directory_iterator(dir))
        {
            if (!e.is_regular_file()) continue;
            out.insert(fs::relative(e.path(), dir).generic_string());
        }
        return out;
    }
}

namespace tut
{
    struct invfetch_data
    {
    };
    typedef test_group<invfetch_data> invfetch_group;
    typedef invfetch_group::object invfetch_object;
    tut::invfetch_group invfetch_test("llinvfetch_parity");

    // The frozen copies (check_r3b_copies.py), run on every test run.
    template<> template<>
    void invfetch_object::test<1>()
    {
        set_test_name("frozen copies match the live source");
        const std::string cmd = std::string("\"") + VKS_INVFETCH_PYTHON + "\" \"" + VKS_INVFETCH_SOURCE_DIR
                                + "/tests/check_r3b_copies.py\" \"" + VKS_INVFETCH_SOURCE_DIR + "/../..\"";
#if LL_WINDOWS
        // cmd.exe strips one pair of outer quotes.
        const std::string run = "\"" + cmd + "\"";
#else
        const std::string run = cmd;
#endif
        int rc = std::system(run.c_str());
        ensure_equals("check_r3b_copies.py exit status (" + cmd + ")", rc, 0);
    }

    template<> template<>
    void invfetch_object::test<2>()
    {
        set_test_name("fetch wire scenarios");
        // The committed tree is per C++ standard library: since upstream
        // 28fee038e0 the model's maps are std::unordered_map, and the order
        // the scheduler and the request builders walk them in (the fetch
        // queue, the children= lists of a request) is the library's.
        // VKS_INVFETCH_FIXTURES names one library's tree directly.
        std::string fixtures = getenv_str("VKS_INVFETCH_FIXTURES");
        if (fixtures.empty()) fixtures = std::string(VKS_INVFETCH_FIXTURE_DIR) + "/" + stdlib_name();
        const std::string out = getenv_str("VKS_INVFETCH_DUMP");
        std::string commit = getenv_str("VKS_INVFETCH_COMMIT");
        if (commit.empty()) commit = "unknown";
        std::set<std::string> only;
        {
            std::string list = getenv_str("VKS_INVFETCH_ONLY");
            std::stringstream ss(list);
            std::string item;
            while (std::getline(ss, item, ',')) if (!item.empty()) only.insert(item);
        }
        ensure("VKS_INVFETCH_ONLY is a developer filter: set VKS_INVFETCH_DEV=1 to use it (a subset never passes as a replay)",
               only.empty() || getenv_str("VKS_INVFETCH_DEV") == "1");

        // Rule PRE_D2_SL_FALLBACKS (README, "D2"): with the CMake option
        // VKS_INVFETCH_PRE_D2_GATES OFF, the Second Life fallback scenarios
        // are not run. Each skip is reported; their fixtures must still be
        // in the tree; a dump refuses to run with the rule on.
        std::set<std::string> skip;
#if VKS_INVFETCH_SKIP_PRE_D2
        for (const auto& kv : vks_fetch::pre_d2_sl_fallbacks())
        {
            skip.insert(kv.first);
            std::cerr << "[r3b] skip " << kv.first << ": rule PRE_D2_SL_FALLBACKS (replaced by " << kv.second << ")" << std::endl;
        }
#endif
        ensure("rule PRE_D2_SL_FALLBACKS is on: a dump must record every scenario", skip.empty() || out.empty());

        // A work directory of this process only.
        const std::string work = std::string(VKS_INVFETCH_WORK_DIR) + "/run-" + std::to_string(vks_getpid());
        fs::remove_all(work);
        fs::create_directories(work);
        std::string error;
        ensure("mock server: " + error, vks_fetch::init(VKS_INVFETCH_TEMPLATE_FILE, work, error));

        if (!out.empty())
        {
            fs::create_directories(fs::path(out) / "scenarios");
            fs::create_directories(fs::path(out) / "responses");
        }

        std::ostringstream report;
        size_t differing = 0, compared = 0;
        std::string index, manifest;
        std::set<std::string> produced;   // relative paths a run produces
        vks_fetch::run_scenarios(work, only, skip, [&](const vks_fetch::ScenarioInfo& info, const vks_inv::Case& c, const std::string& responses)
        {
            vks_inv::Cases cases;
            cases.push_back(c);
            const std::string text = vks_inv::format_cases(header(info, commit), cases);
            const std::string rtext = responses.empty() ? std::string() : responses_header(c.id) + responses;
            for (const std::string& l : c.lines)
            {
                if (l.compare(0, 8, "FAILURE ") == 0)
                {
                    report << "case " << c.id << ": " << l << "\n";
                    ++differing;
                }
            }
            index += c.id + "\n";
            produced.insert("scenarios/" + c.id + ".txt");
            manifest += c.id + " scenarios/" + c.id + ".txt " + vks_inv::sha256_hex(text) + "\n";
            if (!rtext.empty())
            {
                produced.insert("responses/" + c.id + ".txt");
                manifest += c.id + " responses/" + c.id + ".txt " + vks_inv::sha256_hex(rtext) + "\n";
            }
            if (!out.empty())
            {
                vks_inv::write_file((fs::path(out) / "scenarios" / (c.id + ".txt")).string(), text);
                if (!rtext.empty())
                {
                    vks_inv::write_file((fs::path(out) / "responses" / (c.id + ".txt")).string(), rtext);
                }
                std::cerr << "[r3b] recorded " << c.id << " (" << c.lines.size() << " lines)" << std::endl;
                return;
            }
            ++compared;
            const std::string file = fixtures + "/scenarios/" + c.id + ".txt";
            if (!vks_inv::file_exists(file))
            {
                report << "case " << c.id << ": fixture missing: " << file << "\n";
                ++differing;
                return;
            }
            vks_inv::Cases expected = vks_inv::parse_cases(vks_inv::read_file(file));
            // The run's case read back as the fixture is (comment lines,
            // the informational measurements, are not compared).
            vks_inv::Cases actual = vks_inv::parse_cases(text);
            std::string diff = compare_report(expected, actual, 10);
            const std::string actual_dir = getenv_str("VKS_INVFETCH_ACTUAL");
            if (!actual_dir.empty())
            {
                // Diagnostics: the run's output beside the fixture's.
                fs::create_directories(actual_dir);
                vks_inv::write_file((fs::path(actual_dir) / (c.id + ".txt")).string(), text);
            }
            const std::string rfile = fixtures + "/responses/" + c.id + ".txt";
            if (!rtext.empty() || vks_inv::file_exists(rfile))
            {
                if (data_lines(vks_inv::read_file(rfile)) != data_lines(rtext))
                {
                    diff += "case " + c.id + ": response bodies differ from " + rfile + "\n";
                }
            }
            if (!diff.empty())
            {
                ++differing;
                report << diff;
            }
            std::cerr << "[r3b] " << c.id << (diff.empty() ? ": 0 differences" : ": DIFFERENCES") << std::endl;
        });
        vks_fetch::shutdown();

        if (!out.empty())
        {
            std::string ih = "# vkstorm inventory fetch wire oracle (recorder R3b): scenario ids in run order\n";
            vks_inv::write_file((fs::path(out) / "index.txt").string(), ih + index);
            std::string mh = "# vkstorm inventory fetch wire oracle (recorder R3b): <scenario> <file> <sha256>\n"
                             "# viewer commit: " + commit + "\n# platform: " + platform() + "\n";
            vks_inv::write_file((fs::path(out) / "manifest.txt").string(), mh + manifest);
            std::cerr << report.str();
            ensure("dump mode wrote the fixtures to " + out + " (" + std::to_string(differing)
                   + " scenarios with failures); a dump never passes as a replay", false);
            return;
        }
        if (only.empty())
        {
            // The fixture tree holds exactly what a run produces.
            std::set<std::string> want = produced;
            // Skipped scenarios keep their fixtures (as the manifest lists them).
            for (const std::string& l : data_lines(vks_inv::read_file(fixtures + "/manifest.txt")))
            {
                std::istringstream ls(l);
                std::string id, path;
                ls >> id >> path;
                if (skip.count(id)) want.insert(path);
            }
            want.insert("index.txt");
            want.insert("manifest.txt");
            std::set<std::string> have = tree(fixtures);
            // The README and .gitattributes live beside the library trees.
            have.erase("README.md");
            have.erase(".gitattributes");
            for (const std::string& f : want)
            {
                if (!have.count(f)) { report << "fixture tree: missing " << f << "\n"; ++differing; }
            }
            for (const std::string& f : have)
            {
                if (!want.count(f)) { report << "fixture tree: unexpected " << f << "\n"; ++differing; }
            }
            std::vector<std::string> recorded_index;
            for (const std::string& l : data_lines(vks_inv::read_file(fixtures + "/index.txt")))
            {
                if (!skip.count(l)) recorded_index.push_back(l);
            }
            if (recorded_index != data_lines(index))
            {
                report << "index.txt differs from the scenarios run, in order\n";
                ++differing;
            }
            if (data_lines(vks_inv::read_file(fixtures + "/manifest.txt")) != data_lines(manifest))
            {
                // The digests cover the header lines (commit, platform), so a
                // tree recorded elsewhere differs here only if its data does:
                // compare per file, ignoring headers.
                std::map<std::string, std::string> recorded;
                for (const std::string& l : data_lines(vks_inv::read_file(fixtures + "/manifest.txt")))
                {
                    std::istringstream ls(l);
                    std::string id, path, sha;
                    ls >> id >> path >> sha;
                    recorded[path] = sha;
                }
                for (const auto& kv : recorded)
                {
                    const std::string body = vks_inv::read_file(fixtures + "/" + kv.first);
                    if (vks_inv::sha256_hex(body) != kv.second)
                    {
                        report << "manifest.txt: " << kv.first << " does not match its recorded sha256\n";
                        ++differing;
                    }
                }
                size_t skipped_files = 0;
                for (const std::string& l : data_lines(vks_inv::read_file(fixtures + "/manifest.txt")))
                {
                    std::istringstream ls(l);
                    std::string id;
                    ls >> id;
                    if (skip.count(id)) ++skipped_files;
                }
                if (recorded.size() != produced.size() + skipped_files)
                {
                    report << "manifest.txt lists " << recorded.size() << " files, a run produces " << produced.size() << "\n";
                    ++differing;
                }
            }
        }
        else
        {
            report << "VKS_INVFETCH_ONLY ran a subset (" << compared << " scenarios): not a replay\n";
            ++differing;
        }
        std::cerr << "[r3b] compared " << compared << " scenarios, " << differing << " problems" << std::endl;
        ensure(report.str(), differing == 0);
    }
}

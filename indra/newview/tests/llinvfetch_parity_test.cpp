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
 * tests/fixtures/inventory/ais/fetch. The fixture tree must hold exactly the
 * files a run produces (plus README.md and .gitattributes); index.txt and
 * manifest.txt must agree with it.
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
        std::string fixtures = getenv_str("VKS_INVFETCH_FIXTURES");
        if (fixtures.empty()) fixtures = VKS_INVFETCH_FIXTURE_DIR;
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
        vks_fetch::run_scenarios(work, only, [&](const vks_fetch::ScenarioInfo& info, const vks_inv::Case& c, const std::string& responses)
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
            std::string diff = vks_inv::compare_cases(expected, actual, 10);
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
            want.insert("index.txt");
            want.insert("manifest.txt");
            want.insert("README.md");
            want.insert(".gitattributes");
            std::set<std::string> have = tree(fixtures);
            for (const std::string& f : want)
            {
                if (!have.count(f)) { report << "fixture tree: missing " << f << "\n"; ++differing; }
            }
            for (const std::string& f : have)
            {
                if (!want.count(f)) { report << "fixture tree: unexpected " << f << "\n"; ++differing; }
            }
            if (data_lines(vks_inv::read_file(fixtures + "/index.txt")) != data_lines(index))
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
                if (recorded.size() != produced.size())
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

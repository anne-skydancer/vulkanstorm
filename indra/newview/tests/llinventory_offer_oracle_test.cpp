/**
 * @file llinventory_offer_oracle_test.cpp
 * @brief Recorder R4b (inventory rung): the offer and give oracle.
 *
 * The decision code of inventory offers and giving (llimprocessing.cpp,
 * llviewermessage.cpp, llgiveinventory.cpp, lltooldraganddrop.cpp, the
 * world-to-agent accept test of llinventorybridge.cpp, the share dialog of
 * llavataractions.cpp) is frozen verbatim in llinventory_offer_oracle_copies.*
 * and driven with the fakes of llinventory_offer_oracle_fakes.*; every packet
 * is built by the real message system (pack_instant_message and the template
 * builder). The corpora are in llinventory_offer_oracle.cpp.
 *
 * The test
 *  - packs seeded random instant messages through the live
 *    pack_instant_message and the frozen copy of pack_instant_message_block
 *    and requires identical packets;
 *  - replays every committed fixture (tests/fixtures/inventory/offer and
 *    give) and reports the first differences as case, key, expected, actual;
 *  - recomputes the digest of every sweep and its committed sample.
 *
 * VKS_R4B_DUMP=<dir> writes the fixtures to <dir>/offer and <dir>/give
 * instead (VKS_R4B_COMMIT names the viewer commit for the headers).
 * check_r4b_copies.py confirms the frozen copies match the live source.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "llinventory_offer_oracle.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef VKS_R4B_TEMPLATE_FILE
#error VKS_R4B_TEMPLATE_FILE must name scripts/messages/message_template.msg
#endif
#ifndef VKS_R4B_FIXTURE_DIR
#error VKS_R4B_FIXTURE_DIR must name tests/fixtures/inventory
#endif

using namespace r4b_oracle;

namespace
{
    std::string platform()
    {
#if defined(_MSC_VER)
        return "windows-msvc-" + std::to_string(_MSC_VER);
#elif defined(__clang__)
        return "linux-clang-" + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
        return "linux-gcc-" + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
        return "unknown";
#endif
    }

    std::string env(const char* name)
    {
        const char* v = getenv(name);
        return v ? v : "";
    }

    std::string header(const Corpus& c, const char* kind)
    {
        std::ostringstream h;
        h << "# vkstorm inventory oracle r4b: " << c.area << "/" << c.name << kind << "\n";
        h << "# what: " << c.what << "\n";
        h << "# generator: INTEGRATION_TEST_llinventory_offer_oracle (VKS_R4B_DUMP)\n";
        h << "# viewer: " << (env("VKS_R4B_COMMIT").empty() ? "unknown" : env("VKS_R4B_COMMIT")) << "\n";
        h << "# platform: " << platform() << "\n";
        char seed[32];
        snprintf(seed, sizeof(seed), "0x%016llx", (unsigned long long)c.seed);
        h << "# seed: " << (c.seed ? seed : "none (fixed case list)") << "\n";
        h << "# format: <case>\\t<key>\\t<value>; note.* lines are for review, neither compared nor digested\n";
        return h.str();
    }

    std::string case_of(const std::string& line)
    {
        return line.substr(0, line.find('\t'));
    }

    // Streams a corpus into its digest, its sample, and (for full corpora)
    // all its lines.
    struct CollectSink : Sink
    {
        bool keep_all = false;
        uint32_t modulus = 0;
        Sha256 sha;
        size_t lines = 0;
        size_t cases = 0;
        std::string last_case;
        bool last_in_sample = false;
        std::vector<std::string> all;
        std::vector<std::string> sample;
        // policy outcome classes (the canonical lines of a case without its id)
        bool classes = false;
        std::string cur_outcome;
        struct Class { size_t count = 0; std::string first; std::string text; };
        std::unordered_map<std::string, Class> outcome_classes;
        std::vector<std::string> class_order;

        void finish_case()
        {
            if (classes && !last_case.empty())
            {
                Class& k = outcome_classes[cur_outcome];
                if (k.count++ == 0)
                {
                    k.first = last_case;
                    k.text = cur_outcome;
                    class_order.push_back(cur_outcome);
                }
            }
            cur_outcome.clear();
        }

        void line(const std::string& l) override
        {
            std::string c = case_of(l);
            if (c != last_case)
            {
                finish_case();
                last_case = c;
                ++cases;
                last_in_sample = modulus && in_sample(c, modulus);
            }
            if (keep_all) all.push_back(l);
            if (last_in_sample) sample.push_back(l);
            if (!is_note(l))
            {
                sha.update(l.data(), l.size());
                sha.update("\n", 1);
                ++lines;
                if (classes) cur_outcome += l.substr(l.find('\t') + 1) + "\n";
            }
        }
    };

    std::vector<std::string> read_lines(const std::string& path, bool& ok)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        ok = (bool)in;
        std::vector<std::string> out;
        std::string l;
        while (std::getline(in, l))
        {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            out.push_back(l);
        }
        return out;
    }

    std::vector<std::string> comparable(const std::vector<std::string>& v)
    {
        std::vector<std::string> out;
        for (const std::string& l : v)
        {
            if (l.empty() || l[0] == '#' || is_note(l)) continue;
            out.push_back(l);
        }
        return out;
    }

    // "<case> <key>: expected <e> actual <a>" for the first few differences
    std::string diff(const std::vector<std::string>& expected, const std::vector<std::string>& actual, int& count)
    {
        std::ostringstream out;
        count = 0;
        size_t n = std::max(expected.size(), actual.size());
        for (size_t i = 0; i < n; ++i)
        {
            const std::string e = i < expected.size() ? expected[i] : std::string("<missing>");
            const std::string a = i < actual.size() ? actual[i] : std::string("<missing>");
            if (e == a) continue;
            if (count++ < 8)
            {
                auto split = [](const std::string& l, std::string& c, std::string& k, std::string& v) {
                    size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
                    c = l.substr(0, t1);
                    k = t1 == std::string::npos ? "" : l.substr(t1 + 1, t2 - t1 - 1);
                    v = t2 == std::string::npos ? l : l.substr(t2 + 1);
                };
                std::string ec, ek, ev, ac, ak, av;
                split(e, ec, ek, ev);
                split(a, ac, ak, av);
                out << "\n  line " << i << ": case " << ec << " key " << ek << "\n    expected: " << ev
                    << "\n    actual:   " << (ac == ec && ak == ek ? av : a);
            }
        }
        return out.str();
    }

    void write_file(const std::string& path, const std::string& text)
    {
        std::ofstream out(path.c_str(), std::ios::binary);
        out << text;
    }

    void make_dir(const std::string& path)
    {
#if LL_WINDOWS
        std::string cmd = "mkdir \"" + path + "\" 2>nul";
        for (char& ch : cmd) if (ch == '/') ch = '\\';
#else
        std::string cmd = "mkdir -p '" + path + "'";
#endif
        int rc = system(cmd.c_str());
        (void)rc;
    }

    std::string join(const std::vector<std::string>& v)
    {
        std::string out;
        for (const std::string& l : v) { out += l; out += "\n"; }
        return out;
    }

    CollectSink run_corpus(const Corpus& c, bool keep_all, bool classes)
    {
        CollectSink s;
        s.keep_all = keep_all;
        s.modulus = c.sample_modulus;
        s.classes = classes;
        c.run(s);
        s.finish_case();
        return s;
    }
}

namespace tut
{
    struct r4b_data
    {
        r4b_data() { init_messaging(VKS_R4B_TEMPLATE_FILE); }
    };
    typedef test_group<r4b_data> r4b_group;
    typedef r4b_group::object r4b_object;
    r4b_group r4b_g("llinventory_offer_oracle");

    // dump mode: write every fixture
    template<> template<> void r4b_object::test<1>()
    {
        set_test_name("dump");
        const std::string dir = env("VKS_R4B_DUMP");
        if (dir.empty()) return;
        make_dir(dir + "/offer");
        make_dir(dir + "/give");
        const std::string only = env("VKS_R4B_ONLY");
        for (const Corpus& c : corpora())
        {
            if (!only.empty() && ("," + only + ",").find("," + c.name + ",") == std::string::npos) continue;
            auto t0 = std::chrono::steady_clock::now();
            const bool classes = c.name == "policy";
            CollectSink s = run_corpus(c, !c.digest_only, classes);
            const std::string base = dir + "/" + c.area + "/" + c.name;
            if (!c.digest_only)
            {
                write_file(base + ".txt", header(c, "") + join(s.all));
            }
            else
            {
                std::ostringstream d;
                d << header(c, " (digest)");
                d << "lines=" << s.lines << "\n";
                d << "cases=" << s.cases << "\n";
                d << "sample_modulus=" << c.sample_modulus << "\n";
                d << "sha256=" << s.sha.hexdigest() << "\n";
                write_file(base + ".digest", d.str());
                write_file(base + ".sample.txt", header(c, " (sample)") + join(s.sample));
                if (classes)
                {
                    std::ostringstream o;
                    o << header(c, " (outcome classes)");
                    o << "# every distinct outcome (the canonical lines of a case without its id), in order of first\n";
                    o << "# appearance: class <n> count <cases> first <case id>, then its lines\n";
                    size_t n = 0;
                    for (const std::string& key : s.class_order)
                    {
                        const auto& k = s.outcome_classes[key];
                        o << "class " << n++ << " count " << k.count << " first " << k.first << "\n" << k.text;
                    }
                    write_file(base + ".outcomes.txt", o.str());
                }
            }
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "r4b dump %s/%s: %zu cases, %zu lines, %.1f s\n", c.area.c_str(), c.name.c_str(), s.cases, s.lines, secs);
        }
    }

    // live pack_instant_message against the frozen copy
    template<> template<> void r4b_object::test<2>()
    {
        set_test_name("pack live vs frozen");
        std::string first;
        int diffs = pack_live_vs_frozen(0x5234424c49564531ull, 3000, first);
        ensure("pack_instant_message live != frozen in " + std::to_string(diffs) + " of 3000 cases; " + first, diffs == 0);
    }

    // the committed fixtures, replayed
    template<> template<> void r4b_object::test<3>()
    {
        set_test_name("fixture replay");
        if (!env("VKS_R4B_DUMP").empty()) return;
        std::string failures;
        int corpora_checked = 0;
        for (const Corpus& c : corpora())
        {
            if (c.digest_only) continue;
            const std::string path = std::string(VKS_R4B_FIXTURE_DIR) + "/" + c.area + "/" + c.name + ".txt";
            bool ok = false;
            std::vector<std::string> expected = comparable(read_lines(path, ok));
            if (!ok)
            {
                failures += "\nmissing fixture " + path;
                continue;
            }
            CollectSink s = run_corpus(c, true, false);
            int count = 0;
            std::string d = diff(expected, comparable(s.all), count);
            if (count)
            {
                failures += "\n" + c.area + "/" + c.name + ": " + std::to_string(count) + " differing lines" + d;
            }
            ++corpora_checked;
        }
        ensure("offer/give fixtures differ:" + failures, failures.empty());
        ensure("no fixture checked", corpora_checked > 0);
    }

    // the sweeps: digest and sample
    template<> template<> void r4b_object::test<4>()
    {
        set_test_name("sweep digests");
        if (!env("VKS_R4B_DUMP").empty()) return;
        std::string failures;
        for (const Corpus& c : corpora())
        {
            if (!c.digest_only) continue;
            auto t0 = std::chrono::steady_clock::now();
            const std::string base = std::string(VKS_R4B_FIXTURE_DIR) + "/" + c.area + "/" + c.name;
            bool ok = false;
            std::vector<std::string> dig = read_lines(base + ".digest", ok);
            if (!ok)
            {
                failures += "\nmissing digest " + base + ".digest";
                continue;
            }
            std::map<std::string, std::string> kv;
            for (const std::string& l : dig)
            {
                size_t e = l.find('=');
                if (!l.empty() && l[0] != '#' && e != std::string::npos) kv[l.substr(0, e)] = l.substr(e + 1);
            }
            CollectSink s = run_corpus(c, false, false);
            std::string sha = s.sha.hexdigest();
            if (kv["sha256"] != sha || kv["lines"] != std::to_string(s.lines) || kv["cases"] != std::to_string(s.cases))
            {
                failures += "\n" + c.area + "/" + c.name + ": digest expected sha256=" + kv["sha256"] + " lines=" + kv["lines"] +
                            " cases=" + kv["cases"] + ", actual sha256=" + sha + " lines=" + std::to_string(s.lines) +
                            " cases=" + std::to_string(s.cases);
            }
            std::vector<std::string> expected = comparable(read_lines(base + ".sample.txt", ok));
            int count = 0;
            std::string d = diff(expected, comparable(s.sample), count);
            if (count)
            {
                failures += "\n" + c.area + "/" + c.name + " sample: " + std::to_string(count) + " differing lines" + d;
            }
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "r4b sweep %s/%s: %zu cases, %.1f s\n", c.area.c_str(), c.name.c_str(), s.cases, secs);
        }
        ensure("offer/give sweeps differ:" + failures, failures.empty());
    }
}

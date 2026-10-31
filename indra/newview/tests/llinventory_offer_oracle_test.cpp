/**
 * @file llinventory_offer_oracle_test.cpp
 * @brief Recorder R4b (inventory rung): the offer and give oracle.
 *
 * The decision code of inventory offers and giving (llimprocessing.cpp,
 * llviewermessage.cpp, llgiveinventory.cpp, lltooldraganddrop.cpp, the
 * world-to-agent accept test of llinventorybridge.cpp, the share dialog of
 * llavataractions.cpp) is frozen verbatim in llinventory_offer_oracle_copies.*,
 * reached only through the W6/W7 seam (llinventory_offer_oracle_boundary.h)
 * and driven with the fakes of llinventory_offer_oracle_fakes.*; every packet
 * is built by the real message system. The corpora are in
 * llinventory_offer_oracle.cpp.
 *
 * The test
 *  1. (dump mode only) writes the fixtures and then fails on purpose;
 *  2. packs seeded random instant messages through the live
 *     pack_instant_message and the frozen pack_instant_message_block;
 *  3. replays every committed full fixture (case, key, expected, actual);
 *  4. recomputes every sweep's digest, its chunk digests (naming the case
 *     range that changed), its sample (aligned by case id) and, for the policy
 *     tables, every outcome class;
 *  5. requires the fixture directories to hold exactly the registered files;
 *  6. runs check_r4b_copies.py (the frozen copies against the live source).
 *
 * VKS_R4B_DUMP=<dir> writes the fixtures to <dir>/offer and <dir>/give
 * (VKS_R4B_COMMIT names the viewer commit for the headers) and exits non-zero.
 * VKS_R4B_ONLY=<name,...> limits a dump and needs VKS_R4B_DEVELOPER=1.
 * VKS_R4B_FULL=<dir> also writes the sweeps' full canonical streams there for
 * diagnosis.
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
#include "llinventory_offer_oracle_boundary.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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
#ifndef VKS_R4B_PYTHON
#error VKS_R4B_PYTHON must name the Python interpreter
#endif
#ifndef VKS_R4B_VIEWER_ROOT
#error VKS_R4B_VIEWER_ROOT must name the viewer checkout
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

    // the policy tables: the sample also holds, for RLVa cases with an RLVa
    // action, the first case of every (outcome class, RLVa state) pair
    bool stratified(const Corpus& c) { return c.name == "policy" || c.name == "policy_response"; }
    bool hashed_outcomes(const Corpus& c) { return c.name == "policy_response"; }
    uint32_t chunk_cases(const Corpus& c) { return c.name == "policy" ? 8192 : c.name == "policy_response" ? 2048 : 512; }

    std::string rlv_state(const std::string& case_id)
    {
        size_t a = case_id.find(".rlv1.");
        if (a == std::string::npos) return std::string();
        size_t b = case_id.find(".type_", a);
        return case_id.substr(a, b == std::string::npos ? std::string::npos : b - a);
    }

    // Streams a corpus into its digest, its chunk digests, its sample, its
    // outcome classes and (for full corpora) all its lines.
    struct CollectSink : Sink
    {
        bool keep_all = false;
        bool strat = false;
        uint32_t modulus = 0;
        uint32_t chunk = 512;
        std::ofstream* full = nullptr;
        Sha256 sha;
        size_t lines = 0;
        size_t cases = 0;
        std::vector<std::string> all;
        std::vector<std::string> sample;
        // the current case
        std::string cur;
        std::vector<std::string> cur_lines;
        std::string cur_outcome;
        // chunks
        Sha256 chunk_sha;
        size_t chunk_n = 0;
        std::string chunk_first, chunk_last;
        std::vector<std::string> chunk_lines; // "chunk=<i> first=<id> last=<id> cases=<n> sha256=<hex>"
        // outcome classes
        struct Class { size_t count = 0; std::string first; std::string text; };
        std::unordered_map<std::string, Class> classes;
        std::vector<std::string> class_order;
        std::set<std::string> rlv_seen;

        void close_chunk()
        {
            if (!chunk_n) return;
            chunk_lines.push_back("chunk=" + std::to_string(chunk_lines.size()) + " first=" + chunk_first + " last=" + chunk_last +
                                  " cases=" + std::to_string(chunk_n) + " sha256=" + chunk_sha.hexdigest());
            chunk_sha = Sha256();
            chunk_n = 0;
        }

        void finish_case()
        {
            if (cur.empty()) return;
            Class& k = classes[cur_outcome];
            bool first_of_class = k.count++ == 0;
            if (first_of_class)
            {
                k.first = cur;
                k.text = cur_outcome;
                class_order.push_back(cur_outcome);
            }
            bool take = modulus && in_sample(cur, modulus);
            if (strat && !take)
            {
                std::string rs = rlv_state(cur);
                bool rlv_action = cur_outcome.find("RlvGiveToRLV") != std::string::npos || cur_outcome.find("rlv.") != std::string::npos;
                if (!rs.empty() && rlv_action && rlv_seen.insert(std::to_string(std::hash<std::string>()(cur_outcome)) + rs).second)
                {
                    take = true;
                }
            }
            if (take) sample.insert(sample.end(), cur_lines.begin(), cur_lines.end());
            if (keep_all) all.insert(all.end(), cur_lines.begin(), cur_lines.end());
            if (!chunk_n) chunk_first = cur;
            chunk_last = cur;
            if (++chunk_n == chunk) close_chunk();
            cur.clear();
            cur_lines.clear();
            cur_outcome.clear();
        }

        void line(const std::string& l) override
        {
            std::string c = case_of(l);
            if (c != cur)
            {
                finish_case();
                cur = c;
                ++cases;
            }
            cur_lines.push_back(l);
            if (full) *full << l << "\n";
            if (!is_note(l))
            {
                sha.update(l.data(), l.size());
                sha.update("\n", 1);
                chunk_sha.update(l.data(), l.size());
                chunk_sha.update("\n", 1);
                ++lines;
                cur_outcome += l.substr(l.find('\t') + 1) + "\n";
            }
        }

        void finish()
        {
            finish_case();
            close_chunk();
        }

        // hashed: each class as its count, first case and the SHA-256 of its
        // lines (the policy_response table, for the size budget; VKS_R4B_FULL
        // writes the full stream)
        std::string outcomes_text(const std::string& head, bool hashed = false)
        {
            std::ostringstream o;
            o << head;
            o << "# every distinct outcome (the canonical lines of a case without its id), in order of first\n";
            o << "# appearance: class <n> count <cases> first <case id>, then its lines; compared in full\n";
            size_t n = 0;
            for (const std::string& key : class_order)
            {
                const Class& k = classes[key];
                o << "class " << n++ << " count " << k.count << " first " << k.first;
                if (hashed)
                {
                    Sha256 h;
                    h.update(k.text.data(), k.text.size());
                    o << " sha256=" << h.hexdigest() << "\n";
                }
                else
                {
                    o << "\n" << k.text;
                }
            }
            return o.str();
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

    void split(const std::string& l, std::string& c, std::string& k, std::string& v)
    {
        size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
        c = l.substr(0, t1);
        k = t1 == std::string::npos ? "" : l.substr(t1 + 1, t2 - t1 - 1);
        v = t2 == std::string::npos ? l : l.substr(t2 + 1);
    }

    // Line diff aligned by case id: for each case, its keys and values;
    // cases missing on either side are named.
    std::string diff(const std::vector<std::string>& expected, const std::vector<std::string>& actual, int& count)
    {
        std::ostringstream out;
        count = 0;
        std::vector<std::string> order;
        std::map<std::string, std::vector<std::string>> e, a;
        for (const std::string& l : expected) { std::string c = case_of(l); if (!e.count(c)) order.push_back(c); e[c].push_back(l); }
        for (const std::string& l : actual) { std::string c = case_of(l); if (!e.count(c) && !a.count(c)) order.push_back(c); a[c].push_back(l); }
        for (const std::string& c : order)
        {
            const std::vector<std::string>& el = e[c];
            const std::vector<std::string>& al = a[c];
            if (el == al) continue;
            ++count;
            if (count > 8) continue;
            if (el.empty()) { out << "\n  case " << c << ": not expected (" << al.size() << " lines)"; continue; }
            if (al.empty()) { out << "\n  case " << c << ": missing (" << el.size() << " lines expected)"; continue; }
            size_t n = std::max(el.size(), al.size());
            for (size_t i = 0; i < n; ++i)
            {
                std::string ec, ek, ev, ac, ak, av;
                if (i < el.size()) split(el[i], ec, ek, ev); else { ek = "<none>"; ev = "<missing>"; }
                if (i < al.size()) split(al[i], ac, ak, av); else { ak = "<none>"; av = "<missing>"; }
                if (ek == ak && ev == av) continue;
                out << "\n  case " << c << " key " << ek << "\n    expected: " << ev << "\n    actual:   " << (ak == ek ? av : ak + " " + av);
                break;
            }
        }
        return out.str();
    }

    void write_file(const std::string& path, const std::string& text)
    {
        std::ofstream out(path.c_str(), std::ios::binary);
        out << text;
    }

    std::string join(const std::vector<std::string>& v)
    {
        std::string out;
        for (const std::string& l : v) { out += l; out += "\n"; }
        return out;
    }

    CollectSink run_corpus(const Corpus& c, bool keep_all, std::ofstream* full = nullptr)
    {
        CollectSink s;
        s.keep_all = keep_all;
        s.modulus = c.sample_modulus;
        s.strat = stratified(c);
        s.chunk = chunk_cases(c);
        s.full = full;
        c.run(s);
        s.finish();
        return s;
    }

    std::string digest_text(const Corpus& c, CollectSink& s)
    {
        std::ostringstream d;
        d << header(c, " (digest)");
        d << "lines=" << s.lines << "\n";
        d << "cases=" << s.cases << "\n";
        d << "sample_modulus=" << c.sample_modulus << "\n";
        d << "chunk_cases=" << chunk_cases(c) << "\n";
        d << "sha256=" << s.sha.hexdigest() << "\n";
        for (const std::string& l : s.chunk_lines) d << l << "\n";
        return d.str();
    }

    // the files a fixture directory must hold, exactly
    std::set<std::string> registered(const std::string& area)
    {
        std::set<std::string> out = { "README.md", ".gitattributes" };
        for (const Corpus& c : corpora())
        {
            if (c.area != area) continue;
            if (!c.digest_only) out.insert(c.name + ".txt");
            else
            {
                out.insert(c.name + ".digest");
                out.insert(c.name + ".sample.txt");
                if (stratified(c)) out.insert(c.name + ".outcomes.txt");
            }
        }
        return out;
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

    // dump mode: write every fixture, then fail (a dump never passes)
    template<> template<> void r4b_object::test<1>()
    {
        set_test_name("dump");
        const std::string dir = env("VKS_R4B_DUMP");
        if (dir.empty()) return;
        const std::string only = env("VKS_R4B_ONLY");
        ensure("VKS_R4B_ONLY needs VKS_R4B_DEVELOPER=1", only.empty() || env("VKS_R4B_DEVELOPER") == "1");
        std::filesystem::create_directories(dir + "/offer");
        std::filesystem::create_directories(dir + "/give");
        for (const Corpus& c : corpora())
        {
            if (!only.empty() && ("," + only + ",").find("," + c.name + ",") == std::string::npos) continue;
            auto t0 = std::chrono::steady_clock::now();
            CollectSink s = run_corpus(c, !c.digest_only);
            const std::string base = dir + "/" + c.area + "/" + c.name;
            if (!c.digest_only)
            {
                write_file(base + ".txt", header(c, "") + join(s.all));
            }
            else
            {
                write_file(base + ".digest", digest_text(c, s));
                write_file(base + ".sample.txt", header(c, " (sample)") + join(s.sample));
                if (stratified(c)) write_file(base + ".outcomes.txt", s.outcomes_text(header(c, " (outcome classes)"), hashed_outcomes(c)));
            }
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "r4b dump %s/%s: %zu cases, %zu lines, %zu classes, %.1f s\n", c.area.c_str(), c.name.c_str(), s.cases,
                    s.lines, s.class_order.size(), secs);
        }
        write_file(dir + "/DUMP_COMPLETE", "r4b dump complete\n");
        ensure("dump mode: fixtures written to " + dir + "; they are not checked in this mode", false);
    }

    // live pack_instant_message against the frozen copy
    template<> template<> void r4b_object::test<2>()
    {
        set_test_name("pack live vs frozen");
        std::string first;
        int diffs = pack_live_vs_frozen(0x5234424c49564531ull, 3000, first);
        ensure("pack_instant_message live != frozen in " + std::to_string(diffs) + " of 3000 cases; " + first, diffs == 0);
    }

    // the committed full fixtures, replayed
    template<> template<> void r4b_object::test<3>()
    {
        set_test_name("fixture replay");
        if (!env("VKS_R4B_DUMP").empty()) return;
        fprintf(stderr, "r4b decisions: %s (VKS_R4B_IMPL)\n", r4b_boundary::decisions().name());
        std::string failures;
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
            CollectSink s = run_corpus(c, true);
            int count = 0;
            std::string d = diff(expected, comparable(s.all), count);
            if (count) failures += "\n" + c.area + "/" + c.name + ": " + std::to_string(count) + " differing cases" + d;
        }
        ensure("offer/give fixtures differ:" + failures, failures.empty());
    }

    // the sweeps: digest, chunks, sample, outcome classes
    template<> template<> void r4b_object::test<4>()
    {
        set_test_name("sweep digests");
        if (!env("VKS_R4B_DUMP").empty()) return;
        std::string failures;
        const std::string full_dir = env("VKS_R4B_FULL");
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
            std::unique_ptr<std::ofstream> full;
            if (!full_dir.empty())
            {
                std::filesystem::create_directories(full_dir + "/" + c.area);
                full.reset(new std::ofstream(full_dir + "/" + c.area + "/" + c.name + ".full.txt", std::ios::binary));
            }
            CollectSink s = run_corpus(c, false, full.get());
            std::map<std::string, std::string> kv;
            std::vector<std::string> chunks;
            for (const std::string& l : dig)
            {
                if (l.empty() || l[0] == '#') continue;
                if (l.compare(0, 6, "chunk=") == 0) { chunks.push_back(l); continue; }
                size_t e = l.find('=');
                if (e != std::string::npos) kv[l.substr(0, e)] = l.substr(e + 1);
            }
            std::string sha = s.sha.hexdigest();
            if (kv["sha256"] != sha || kv["lines"] != std::to_string(s.lines) || kv["cases"] != std::to_string(s.cases))
            {
                failures += "\n" + c.area + "/" + c.name + ": digest expected sha256=" + kv["sha256"] + " lines=" + kv["lines"] +
                            " cases=" + kv["cases"] + ", actual sha256=" + sha + " lines=" + std::to_string(s.lines) +
                            " cases=" + std::to_string(s.cases);
                for (size_t i = 0; i < std::max(chunks.size(), s.chunk_lines.size()); ++i)
                {
                    std::string e = i < chunks.size() ? chunks[i] : "<none>";
                    std::string a = i < s.chunk_lines.size() ? s.chunk_lines[i] : "<none>";
                    if (e != a)
                    {
                        failures += "\n  first changed chunk: expected " + e + "\n                       actual   " + a;
                        break;
                    }
                }
            }
            std::vector<std::string> expected = comparable(read_lines(base + ".sample.txt", ok));
            if (!ok) failures += "\nmissing sample " + base + ".sample.txt";
            int count = 0;
            std::string d = diff(expected, comparable(s.sample), count);
            if (count) failures += "\n" + c.area + "/" + c.name + " sample: " + std::to_string(count) + " differing cases" + d;
            if (stratified(c))
            {
                std::vector<std::string> exp_cls = comparable(read_lines(base + ".outcomes.txt", ok));
                if (!ok) failures += "\nmissing outcome classes " + base + ".outcomes.txt";
                std::vector<std::string> act_cls = comparable(read_lines("", ok));
                std::istringstream text(s.outcomes_text("", hashed_outcomes(c)));
                std::string l;
                act_cls.clear();
                while (std::getline(text, l)) if (!l.empty() && l[0] != '#') act_cls.push_back(l);
                // compare class by class
                auto classes_of = [](const std::vector<std::string>& v) {
                    std::vector<std::pair<std::string, std::string>> out;
                    for (const std::string& x : v)
                    {
                        if (x.compare(0, 6, "class ") == 0) out.push_back({ x, std::string() });
                        else if (!out.empty()) out.back().second += x + "\n";
                    }
                    return out;
                };
                auto ec = classes_of(exp_cls), ac = classes_of(act_cls);
                for (size_t i = 0; i < std::max(ec.size(), ac.size()); ++i)
                {
                    std::string e = i < ec.size() ? ec[i].first + "\n" + ec[i].second : "<none>";
                    std::string a = i < ac.size() ? ac[i].first + "\n" + ac[i].second : "<none>";
                    if (e != a)
                    {
                        failures += "\n" + c.area + "/" + c.name + " outcome classes: " + std::to_string(ec.size()) + " expected, " +
                                    std::to_string(ac.size()) + " actual; first changed class " + std::to_string(i) +
                                    "\n  expected: " + (i < ec.size() ? ec[i].first : "<none>") +
                                    "\n  actual:   " + (i < ac.size() ? ac[i].first : "<none>");
                        break;
                    }
                }
            }
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "r4b sweep %s/%s: %zu cases, %.1f s\n", c.area.c_str(), c.name.c_str(), s.cases, secs);
        }
        ensure("offer/give sweeps differ:" + failures, failures.empty());
    }

    // the fixture directories hold exactly the registered files
    template<> template<> void r4b_object::test<5>()
    {
        set_test_name("fixture directory");
        if (!env("VKS_R4B_DUMP").empty()) return;
        std::string failures;
        for (const char* area : { "offer", "give" })
        {
            const std::string dir = std::string(VKS_R4B_FIXTURE_DIR) + "/" + area;
            std::set<std::string> want = registered(area), have;
            if (!std::filesystem::is_directory(dir))
            {
                failures += "\nmissing directory " + dir;
                continue;
            }
            for (const auto& e : std::filesystem::directory_iterator(dir)) have.insert(e.path().filename().string());
            for (const std::string& f : have) if (!want.count(f)) failures += "\nunexpected fixture file " + std::string(area) + "/" + f;
            for (const std::string& f : want) if (!have.count(f)) failures += "\nmissing fixture file " + std::string(area) + "/" + f;
        }
        ensure("offer/give fixture directories:" + failures, failures.empty());
    }

    // the frozen copies against the live source (until W6/W7 replace them)
    template<> template<> void r4b_object::test<6>()
    {
        set_test_name("copy check");
        if (!env("VKS_R4B_DUMP").empty()) return;
        const std::string script = std::string(VKS_R4B_VIEWER_ROOT) + "/indra/newview/tests/check_r4b_copies.py";
        std::string cmd = std::string("\"") + VKS_R4B_PYTHON + "\" \"" + script + "\" --root \"" + VKS_R4B_VIEWER_ROOT + "\"";
#if LL_WINDOWS
        cmd = "\"" + cmd + "\"";
#endif
        int rc = system(cmd.c_str());
        ensure("check_r4b_copies.py failed (rc " + std::to_string(rc) + "): the frozen copies no longer match the live source", rc == 0);
    }

    // VKS_R4B_BENCH=<reps>: the frozen C++ against the Rust decisions, per
    // operation (not run otherwise)
    template<> template<> void r4b_object::test<7>()
    {
        set_test_name("bench");
        const std::string reps = env("VKS_R4B_BENCH");
        if (reps.empty()) return;
        fprintf(stderr, "%s", run_bench(std::max(1, atoi(reps.c_str()))).c_str());
    }
}

/**
 * @file llsdserialize_tool.cpp
 * @brief Benchmark and fixture generator for the Rust LLSD serialization
 * (vkstorm-rust rung two) against the former C++ implementation.
 *
 *   llsdserialize_tool bench [iterations]   timings, C++ oracle vs Rust
 *   llsdserialize_tool dump <directory>     fixtures from the oracle for
 *                                           vkstorm-core-rust/crates/vkstorm-llsd
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "llsdserialize_corpus.h"
#include "llsdserialize_oracle.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>

using namespace llsd_corpus;

namespace
{
    class OpaqueBuf : public std::streambuf
    {
    public:
        OpaqueBuf(const std::string& data) : mData(data)
        {
            char* p = const_cast<char*>(mData.data());
            setg(p, p, p + mData.size());
        }
        void rewind()
        {
            char* p = const_cast<char*>(mData.data());
            setg(p, p, p + mData.size());
        }
    protected:
        int underflow() override { return gptr() < egptr() ? (unsigned char)*gptr() : EOF; }
    private:
        std::string mData;
    };

    const char* formatName(Format f)
    {
        switch (f)
        {
        case NOTATION: return "notation";
        case BINARY: return "binary";
        default: return "xml";
        }
    }

    std::string oracleFormat(const LLSD& v, Format f, LLSDFormatter::EFormatterOptions o = LLSDFormatter::OPTIONS_PRETTY_BINARY,
                             bool boolalpha = false, const std::string& real_format = "")
    {
        LLPointer<llsd_oracle::OracleFormatter> fm;
        switch (f)
        {
        case NOTATION: fm = new llsd_oracle::OracleNotationFormatter(boolalpha, real_format); break;
        case BINARY: fm = new llsd_oracle::OracleBinaryFormatter(boolalpha, real_format); break;
        default: fm = new llsd_oracle::OracleXMLFormatter(boolalpha, real_format); break;
        }
        std::ostringstream out;
        fm->format(v, out, (llsd_oracle::OracleFormatter::EFormatterOptions)o);
        return out.str();
    }

    typedef std::function<void()> Op;

    struct Timing
    {
        double min_ms;
        double median_ms;
    };

    Timing timeOp(const Op& op, int iterations)
    {
        std::vector<double> samples;
        for (int i = 0; i < iterations; ++i)
        {
            auto t0 = std::chrono::steady_clock::now();
            op();
            auto t1 = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        std::sort(samples.begin(), samples.end());
        Timing t;
        t.min_ms = samples.front();
        t.median_ms = samples[samples.size() / 2];
        return t;
    }

    struct Row
    {
        std::string doc;
        std::string op;
        size_t bytes;
        Timing cpp;
        Timing rust;
    };

    void printRows(const std::vector<Row>& rows)
    {
        printf("\n| document | operation | bytes | C++ min ms | C++ median ms | Rust min ms | Rust median ms | Rust/C++ (median) |\n");
        printf("|---|---|---:|---:|---:|---:|---:|---:|\n");
        for (const Row& r : rows)
        {
            printf("| %s | %s | %zu | %.3f | %.3f | %.3f | %.3f | %.2fx |\n", r.doc.c_str(), r.op.c_str(), r.bytes,
                   r.cpp.min_ms, r.cpp.median_ms, r.rust.min_ms, r.rust.median_ms,
                   r.rust.median_ms > 0 ? r.cpp.median_ms / r.rust.median_ms : 0.0);
        }
        fflush(stdout);
    }

    LLPointer<llsd_oracle::OracleParser> oracleParser(Format f)
    {
        switch (f)
        {
        case NOTATION: return new llsd_oracle::OracleNotationParser;
        case BINARY: return new llsd_oracle::OracleBinaryParser;
        default: return new llsd_oracle::OracleXMLParser;
        }
    }

    LLPointer<LLSDParser> rustParser(Format f)
    {
        switch (f)
        {
        case NOTATION: return new LLSDNotationParser;
        case BINARY: return new LLSDBinaryParser;
        default: return new LLSDXMLParser;
        }
    }

    LLPointer<llsd_oracle::OracleFormatter> oracleFormatter(Format f)
    {
        switch (f)
        {
        case NOTATION: return new llsd_oracle::OracleNotationFormatter;
        case BINARY: return new llsd_oracle::OracleBinaryFormatter;
        default: return new llsd_oracle::OracleXMLFormatter;
        }
    }

    LLPointer<LLSDFormatter> rustFormatter(Format f)
    {
        switch (f)
        {
        case NOTATION: return new LLSDNotationFormatter;
        case BINARY: return new LLSDBinaryFormatter;
        default: return new LLSDXMLFormatter;
        }
    }

    void benchDocument(const std::string& name, const LLSD& value, int iterations, std::vector<Row>& rows)
    {
        for (Format f : { NOTATION, BINARY, XML })
        {
            std::string bytes = oracleFormat(value, f);
            std::string doc = name + " / " + formatName(f);

            // Parse from a string stream (memory backed: bulk feeding).
            {
                Row r;
                r.doc = doc;
                r.op = "parse (stringstream)";
                r.bytes = bytes.size();
                LLPointer<llsd_oracle::OracleParser> op = oracleParser(f);
                LLPointer<LLSDParser> rp = rustParser(f);
                r.cpp = timeOp([&] { std::istringstream in(bytes); LLSD out; op->reset(); op->parse(in, out, bytes.size()); }, iterations);
                r.rust = timeOp([&] { std::istringstream in(bytes); LLSD out; rp->reset(); rp->parse(in, out, bytes.size()); }, iterations);
                rows.push_back(r);
            }
            // Parse from an opaque stream (exact or line feeding).
            {
                Row r;
                r.doc = doc;
                r.op = "parse (opaque stream)";
                r.bytes = bytes.size();
                LLPointer<llsd_oracle::OracleParser> op = oracleParser(f);
                LLPointer<LLSDParser> rp = rustParser(f);
                OpaqueBuf ob(bytes);
                r.cpp = timeOp([&] { ob.rewind(); std::istream in(&ob); LLSD out; op->reset(); op->parse(in, out, bytes.size()); }, iterations);
                r.rust = timeOp([&] { ob.rewind(); std::istream in(&ob); LLSD out; rp->reset(); rp->parse(in, out, bytes.size()); }, iterations);
                rows.push_back(r);
            }
            // Format to a string stream.
            {
                Row r;
                r.doc = doc;
                r.op = "format";
                r.bytes = bytes.size();
                LLPointer<llsd_oracle::OracleFormatter> of = oracleFormatter(f);
                LLPointer<LLSDFormatter> rf = rustFormatter(f);
                r.cpp = timeOp([&] { std::ostringstream out; of->format(value, out, llsd_oracle::OracleFormatter::OPTIONS_PRETTY_BINARY); }, iterations);
                r.rust = timeOp([&] { std::ostringstream out; rf->format(value, out, LLSDFormatter::OPTIONS_PRETTY_BINARY); }, iterations);
                rows.push_back(r);
            }
        }
    }

    int bench(int iterations)
    {
        std::vector<Row> rows;
        // Small: a login-response-like map.
        for (const ValueCase& vc : values())
        {
            if (vc.name == "login response" || vc.name == "map all types")
            {
                benchDocument(vc.name, vc.value, iterations * 20, rows);
            }
        }
        // Real: the viewer's settings.xml.
        {
            std::string bytes = readFile(std::string(VKS_LLSD_APP_SETTINGS_DIR) + "/settings.xml");
            if (!bytes.empty())
            {
                LLSD settings;
                std::istringstream in(bytes);
                LLSDSerialize::fromXML(settings, in);
                benchDocument("settings.xml (" + std::to_string(settings.size()) + " settings)", settings, iterations, rows);
            }
        }
        // Large synthetic documents.
        for (const ValueCase& vc : largeValues())
        {
            benchDocument(vc.name, vc.value, std::max(3, iterations / 2), rows);
        }
        printRows(rows);
        return 0;
    }

    // ---- fixtures

    std::string sanitize(const std::string& name)
    {
        std::string s;
        for (char c : name)
        {
            s.push_back(isalnum((unsigned char)c) ? c : '_');
        }
        return s;
    }

    void writeFile(const std::string& path, const std::string& bytes)
    {
        std::ofstream out(path.c_str(), std::ios::binary);
        out.write(bytes.data(), (std::streamsize)bytes.size());
    }

    int dump(const std::string& dir)
    {
        std::ostringstream manifest;
        manifest << "# vkstorm-llsd fixtures, generated by llsdserialize_tool dump from the C++ oracle.\n"
                    "# parse <name> <format> <max_bytes> <max_depth> <lines> <count>   -> <name>.in [+ <name>.out binary LLSD]\n"
                    "# format <name> <format> <options> <boolalpha> <real_format> <count> -> <name>.val binary LLSD, <name>.expect\n";
        // Basenames carry the case index: sanitized names alone collide on a
        // case-insensitive filesystem ("n bool false" / "n bool FALSE").
        int n = 0;
        size_t index = 0;
        for (const ParseCase& pc : parseCases())
        {
            std::string base = "p" + std::to_string(index++) + "_" + sanitize(pc.name);
            std::istringstream in(pc.input);
            LLPointer<llsd_oracle::OracleParser> p = oracleParser(pc.format);
            LLSD value;
            S32 count;
            if (pc.runtime_edge)
            {
                // The product's stated behaviour, not the platform's oracle.
                count = pc.edge_count;
                value = pc.edge_value;
            }
            else
            {
                count = pc.lines ? p->parseLines(in, value) : p->parse(in, value, pc.max_bytes, pc.max_depth);
            }
            writeFile(dir + "/" + base + ".in", pc.input);
            if (count > 0)
            {
                writeFile(dir + "/" + base + ".out", oracleFormat(value, BINARY, LLSDFormatter::OPTIONS_NONE));
            }
            manifest << "parse " << base << " " << pc.format << " " << pc.max_bytes << " " << pc.max_depth << " "
                     << (pc.lines ? 1 : 0) << " " << count << "\n";
            ++n;
        }
        const LLSDFormatter::EFormatterOptions options[] = {
            LLSDFormatter::OPTIONS_NONE, LLSDFormatter::OPTIONS_PRETTY, LLSDFormatter::OPTIONS_PRETTY_BINARY,
            LLSDFormatter::EFormatterOptions(LLSDFormatter::OPTIONS_PRETTY | LLSDFormatter::OPTIONS_PRETTY_BINARY),
        };
        index = 0;
        for (const ValueCase& vc : values())
        {
            std::string val = oracleFormat(vc.value, BINARY, LLSDFormatter::OPTIONS_NONE);
            std::string base = "v" + std::to_string(index++) + "_" + sanitize(vc.name);
            writeFile(dir + "/" + base + ".val", val);
            int k = 0;
            for (Format f : { NOTATION, BINARY, XML })
            {
                for (LLSDFormatter::EFormatterOptions o : options)
                {
                    for (bool alpha : { false, true })
                    {
                        std::string name = base + "_" + std::to_string(k++);
                        LLPointer<llsd_oracle::OracleFormatter> fm = oracleFormatter(f);
                        fm->boolalpha(alpha);
                        std::ostringstream out;
                        S32 count = fm->format(vc.value, out, (llsd_oracle::OracleFormatter::EFormatterOptions)o);
                        writeFile(dir + "/" + name + ".expect", out.str());
                        manifest << "format " << name << " " << base << " " << f << " " << (int)o << " " << (alpha ? 1 : 0)
                                 << " - " << count << "\n";
                    }
                }
                for (const char* rf : { "%.2f", "%.0f", "%e" })
                {
                    std::string name = base + "_" + std::to_string(k++);
                    LLPointer<llsd_oracle::OracleFormatter> fm = oracleFormatter(f);
                    fm->realFormat(rf);
                    std::ostringstream out;
                    S32 count = fm->format(vc.value, out, llsd_oracle::OracleFormatter::OPTIONS_PRETTY_BINARY);
                    writeFile(dir + "/" + name + ".expect", out.str());
                    manifest << "format " << name << " " << base << " " << f << " 2 0 " << rf << " " << count << "\n";
                }
            }
            ++n;
        }
        writeFile(dir + "/manifest.txt", manifest.str());
        printf("wrote %d cases to %s\n", n, dir.c_str());
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "bench")
    {
        int iterations = argc >= 3 ? atoi(argv[2]) : 20;
        return bench(iterations);
    }
    if (argc >= 3 && std::string(argv[1]) == "dump")
    {
        return dump(argv[2]);
    }
    fprintf(stderr, "usage: llsdserialize_tool bench [iterations] | dump <directory>\n");
    return 2;
}

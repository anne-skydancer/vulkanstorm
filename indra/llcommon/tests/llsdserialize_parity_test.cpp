/**
 * @file llsdserialize_parity_test.cpp
 * @brief The Rust LLSD serialization (vkstorm-rust rung two) against the
 * former C++ implementation: byte-identical formatting, identical parse
 * results, counts, failures and stream consumption.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "llsd.h"
#include "llsdserialize.h"
#include "llsdserialize_corpus.h"
#include "llsdserialize_oracle.h"
#include "llmemorystream.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <streambuf>

using namespace llsd_corpus;

namespace
{
    /// A memory stream buffer the production code does not recognise as
    /// memory backed, so the Rust parsers take the exact (notation, binary)
    /// or line (XML) feeding path, as they would on a file or a pipe.
    class OpaqueBuf : public std::streambuf
    {
    public:
        OpaqueBuf(const std::string& data) : mData(data)
        {
            char* p = const_cast<char*>(mData.data());
            setg(p, p, p + mData.size());
        }
    protected:
        int underflow() override { return gptr() < egptr() ? (unsigned char)*gptr() : EOF; }
    private:
        std::string mData;
    };

    /// A sink that accepts nothing: with exceptions enabled on the stream,
    /// every write throws std::ios_base::failure, as LLSettingsVOBase::exportFile
    /// arranges for its file stream.
    class FailingOutBuf : public std::streambuf
    {
    protected:
        int overflow(int) override { return EOF; }
        std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
    };

    /// A source whose reads throw.
    class ThrowingInBuf : public std::streambuf
    {
    protected:
        int underflow() override { throw std::runtime_error("read failed"); }
    };

    std::string rest(std::istream& is)
    {
        is.clear();
        return std::string((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    }

    struct Outcome
    {
        S32 count;
        LLSD value;
        std::string remaining;
    };

    Outcome parseOracle(const ParseCase& c, std::istream& is)
    {
        LLPointer<llsd_oracle::OracleParser> p;
        switch (c.format)
        {
        case NOTATION: p = new llsd_oracle::OracleNotationParser; break;
        case BINARY: p = new llsd_oracle::OracleBinaryParser; break;
        default: p = new llsd_oracle::OracleXMLParser; break;
        }
        Outcome o;
        o.count = c.lines ? p->parseLines(is, o.value) : p->parse(is, o.value, c.max_bytes, c.max_depth);
        o.remaining = rest(is);
        return o;
    }

    Outcome parseRust(const ParseCase& c, std::istream& is)
    {
        LLPointer<LLSDParser> p;
        switch (c.format)
        {
        case NOTATION: p = new LLSDNotationParser; break;
        case BINARY: p = new LLSDBinaryParser; break;
        default: p = new LLSDXMLParser; break;
        }
        Outcome o;
        o.count = c.lines ? p->parseLines(is, o.value) : p->parse(is, o.value, c.max_bytes, c.max_depth);
        o.remaining = rest(is);
        return o;
    }

    std::string hex(const std::string& s)
    {
        static const char* d = "0123456789abcdef";
        std::string out;
        for (unsigned char c : s.substr(0, 120))
        {
            if (c >= 0x20 && c < 0x7f) out.push_back((char)c);
            else { out.push_back('\\'); out.push_back('x'); out.push_back(d[c >> 4]); out.push_back(d[c & 15]); }
        }
        if (s.size() > 120) out += "...";
        return out;
    }

    void sameCount(const std::string& name, S32 got, S32 want)
    {
        tut::ensure(name + ": count (rust " + std::to_string(got) + ", oracle " + std::to_string(want) + ")", got == want);
    }

    void sameBytes(const std::string& name, const std::string& got, const std::string& want)
    {
        tut::ensure(name + " (rust " + hex(got) + ", oracle " + hex(want) + ")", got == want);
    }

    void compareOutcomes(const std::string& name, const Outcome& oracle, const Outcome& rust)
    {
        sameCount(name, rust.count, oracle.count);
        tut::ensure(name + ": value (oracle " + describe(oracle.value) + ", rust " + describe(rust.value) + ")",
                    equalBits(oracle.value, rust.value));
        sameBytes(name + ": remaining stream", rust.remaining, oracle.remaining);
    }

    void checkEdgeCase(const ParseCase& c)
    {
        // The oracle's answer is platform dependent here; the product's
        // behaviour is the corpus's stated one, on both stream kinds.
        for (int opaque = 0; opaque < 2; ++opaque)
        {
            std::istringstream ss(c.input);
            OpaqueBuf ob(c.input);
            std::istream os(&ob);
            Outcome rust = parseRust(c, opaque ? os : static_cast<std::istream&>(ss));
            std::string name = c.name + (opaque ? " [opaque]" : " [stringstream]");
            sameCount(name, rust.count, c.edge_count);
            tut::ensure(name + ": value (want " + describe(c.edge_value) + ", rust " + describe(rust.value) + ")",
                        equalBits(c.edge_value, rust.value));
            sameBytes(name + ": remaining stream", rust.remaining, std::string());
        }
    }

    void checkParseCase(const ParseCase& c)
    {
        if (c.runtime_edge)
        {
            return checkEdgeCase(c);
        }
        // A memory-backed stream (bulk feeding) ...
        {
            std::istringstream a(c.input), b(c.input);
            compareOutcomes(c.name + " [stringstream]", parseOracle(c, a), parseRust(c, b));
        }
        // ... and an opaque one (exact or line feeding).
        {
            OpaqueBuf ba(c.input), bb(c.input);
            std::istream a(&ba), b(&bb);
            compareOutcomes(c.name + " [opaque]", parseOracle(c, a), parseRust(c, b));
        }
    }

    typedef std::pair<std::string, std::string> Rendering; // name suffix, bytes

    std::string formatOracle(const LLSD& v, Format f, LLSDFormatter::EFormatterOptions options,
                             bool boolalpha, const std::string& real_format, S32* count)
    {
        LLPointer<llsd_oracle::OracleFormatter> fm;
        switch (f)
        {
        case NOTATION: fm = new llsd_oracle::OracleNotationFormatter(boolalpha, real_format); break;
        case BINARY: fm = new llsd_oracle::OracleBinaryFormatter(boolalpha, real_format); break;
        default: fm = new llsd_oracle::OracleXMLFormatter(boolalpha, real_format); break;
        }
        std::ostringstream out;
        *count = fm->format(v, out, (llsd_oracle::OracleFormatter::EFormatterOptions)options);
        return out.str();
    }

    std::string formatRust(const LLSD& v, Format f, LLSDFormatter::EFormatterOptions options,
                           bool boolalpha, const std::string& real_format, S32* count)
    {
        LLPointer<LLSDFormatter> fm;
        switch (f)
        {
        case NOTATION: fm = new LLSDNotationFormatter(boolalpha, real_format); break;
        case BINARY: fm = new LLSDBinaryFormatter(boolalpha, real_format); break;
        default: fm = new LLSDXMLFormatter(boolalpha, real_format); break;
        }
        std::ostringstream out;
        *count = fm->format(v, out, options);
        return out.str();
    }

    const LLSDFormatter::EFormatterOptions ALL_OPTIONS[] = {
        LLSDFormatter::OPTIONS_NONE,
        LLSDFormatter::OPTIONS_PRETTY,
        LLSDFormatter::OPTIONS_PRETTY_BINARY,
        LLSDFormatter::EFormatterOptions(LLSDFormatter::OPTIONS_PRETTY | LLSDFormatter::OPTIONS_PRETTY_BINARY),
    };

    void checkFormatValue(const ValueCase& vc)
    {
        for (Format f : { NOTATION, BINARY, XML })
        {
            for (LLSDFormatter::EFormatterOptions o : ALL_OPTIONS)
            {
                for (bool alpha : { false, true })
                {
                    S32 c1 = 0, c2 = 0;
                    std::string name = vc.name + " f" + std::to_string(f) + " o" + std::to_string(o) + (alpha ? " alpha" : "");
                    std::string a = formatOracle(vc.value, f, o, alpha, "", &c1);
                    std::string b = formatRust(vc.value, f, o, alpha, "", &c2);
                    sameBytes(name + ": bytes", b, a);
                    sameCount(name, c2, c1);
                }
            }
        }
        for (const char* rf : { "%.2f", "%.4f", "%.0f", "%g", "%e", "%10.3f" })
        {
            for (Format f : { NOTATION, XML, BINARY })
            {
                S32 c1 = 0, c2 = 0;
                std::string name = vc.name + " realFormat " + rf + " f" + std::to_string(f);
                std::string a = formatOracle(vc.value, f, LLSDFormatter::OPTIONS_PRETTY_BINARY, false, rf, &c1);
                std::string b = formatRust(vc.value, f, LLSDFormatter::OPTIONS_PRETTY_BINARY, false, rf, &c2);
                sameBytes(name + ": bytes", b, a);
            }
        }
    }

    void checkRoundTrip(const ValueCase& vc)
    {
        for (Format f : { NOTATION, BINARY, XML })
        {
            for (LLSDFormatter::EFormatterOptions o : ALL_OPTIONS)
            {
                S32 c = 0;
                std::string bytes = formatOracle(vc.value, f, o, false, "", &c);
                for (llssize limit : { (llssize)bytes.size(), (llssize)LLSDSerialize::SIZE_UNLIMITED })
                {
                    ParseCase pc;
                    pc.name = vc.name + " round trip f" + std::to_string(f) + " o" + std::to_string(o) + " limit " + std::to_string(limit);
                    pc.format = f;
                    pc.input = bytes;
                    pc.max_bytes = limit;
                    pc.max_depth = -1;
                    pc.lines = false;
                    checkParseCase(pc);
                }
            }
        }
    }
}

namespace tut
{
    struct parity_data {};
    typedef test_group<parity_data> parity_group;
    typedef parity_group::object parity_object;
    tut::parity_group parity("LLSD serialization parity (Rust vs C++ oracle)");

    // Every formatter, every option, every corpus value: byte-identical.
    template<> template<>
    void parity_object::test<1>()
    {
        for (const ValueCase& vc : values())
        {
            checkFormatValue(vc);
        }
    }

    // The escape helpers over every byte.
    template<> template<>
    void parity_object::test<2>()
    {
        std::string all;
        for (int i = 0; i < 256; ++i) all.push_back((char)i);
        sameBytes("notation escape", LLSDNotationFormatter::escapeString(all), llsd_oracle::OracleNotationFormatter::escapeString(all));
        sameBytes("xml escape", LLSDXMLFormatter::escapeString(all), llsd_oracle::OracleXMLFormatter::escapeString(all));
        ensure("escape empty", LLSDXMLFormatter::escapeString("").empty());
    }

    // Every parse case: same count, same value, same stream position, on a
    // string stream and on an opaque stream.
    template<> template<>
    void parity_object::test<3>()
    {
        for (const ParseCase& pc : parseCases())
        {
            checkParseCase(pc);
        }
    }

    // Every corpus value serialized by the oracle parses identically, with
    // and without a byte limit.
    template<> template<>
    void parity_object::test<4>()
    {
        for (const ValueCase& vc : values())
        {
            checkRoundTrip(vc);
        }
    }

    // Trailing data after a value is left in the stream the same way.
    template<> template<>
    void parity_object::test<5>()
    {
        for (const ValueCase& vc : values())
        {
            for (Format f : { NOTATION, BINARY })
            {
                S32 c = 0;
                std::string bytes = formatOracle(vc.value, f, LLSDFormatter::OPTIONS_NONE, false, "", &c);
                ParseCase pc;
                pc.name = vc.name + " with trailer f" + std::to_string(f);
                pc.format = f;
                pc.input = bytes + "\n{'next':i1}\n";
                pc.max_bytes = LLSDSerialize::SIZE_UNLIMITED;
                pc.max_depth = -1;
                pc.lines = false;
                checkParseCase(pc);
            }
        }
    }

    // deserialize() with each header, and the legacy <llsd> path through
    // parsePart().
    template<> template<>
    void parity_object::test<6>()
    {
        for (const ValueCase& vc : values())
        {
            for (LLSDSerialize::ELLSD_Serialize t : { LLSDSerialize::LLSD_BINARY, LLSDSerialize::LLSD_XML, LLSDSerialize::LLSD_NOTATION })
            {
                std::ostringstream out;
                LLSDSerialize::serialize(vc.value, out, t);
                std::string bytes = out.str();
                std::istringstream in(bytes);
                LLSD back;
                bool ok = LLSDSerialize::deserialize(back, in, bytes.size());
                // What the oracle parser makes of the same body (the
                // notation form is not lossless for reals, and neither parser
                // reads "rinf", so compare parsers, not the original value).
                std::string body = bytes.substr(bytes.find('\n') + 1);
                std::istringstream body_in(body);
                LLSD expected;
                LLPointer<llsd_oracle::OracleParser> p;
                switch (t)
                {
                case LLSDSerialize::LLSD_BINARY: p = new llsd_oracle::OracleBinaryParser; break;
                case LLSDSerialize::LLSD_XML: p = new llsd_oracle::OracleXMLParser; break;
                default: p = new llsd_oracle::OracleNotationParser; break;
                }
                S32 expected_count = p->parse(body_in, expected, body.size());
                ensure(vc.name + ": deserialize ok", ok == (expected_count > 0));
                ensure(vc.name + ": deserialize value (oracle " + describe(expected) + ", rust " + describe(back) + ")", equalBits(back, expected));
            }
            // No header: XML and notation are sniffed.
            {
                std::ostringstream out;
                LLSDSerialize::toXML(vc.value, out);
                std::istringstream in(out.str());
                LLSD back;
                bool ok = LLSDSerialize::deserialize(back, in, out.str().size());
                std::istringstream in2(out.str());
                LLSD expected;
                LLPointer<llsd_oracle::OracleXMLParser> p = new llsd_oracle::OracleXMLParser;
                ensure(vc.name + ": sniffed xml", ok == (p->parse(in2, expected, out.str().size()) > 0));
                ensure(vc.name + ": sniffed xml value", equalBits(back, expected));
            }
            {
                std::ostringstream out;
                LLSDSerialize::toNotation(vc.value, out);
                std::istringstream in(out.str());
                LLSD back;
                bool ok = LLSDSerialize::deserialize(back, in, out.str().size());
                std::istringstream in2(out.str());
                LLSD expected;
                LLPointer<llsd_oracle::OracleNotationParser> p = new llsd_oracle::OracleNotationParser;
                ensure(vc.name + ": sniffed notation", ok == (p->parse(in2, expected, out.str().size()) > 0));
                ensure(vc.name + ": sniffed notation value", equalBits(back, expected));
            }
        }
    }

    // The viewer's own XML files parse identically and re-serialize
    // identically, from a file stream and from memory.
    template<> template<>
    void parity_object::test<7>()
    {
        std::string dir(VKS_LLSD_APP_SETTINGS_DIR);
        int checked = 0;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.path().extension() != ".xml")
            {
                continue;
            }
            std::string name = entry.path().filename().string();
            std::string path = dir + "/" + name;
            std::string bytes = readFile(path);
            if (bytes.find("<llsd>") == std::string::npos)
            {
                continue;
            }
            ParseCase pc;
            pc.name = name;
            pc.format = XML;
            pc.input = bytes;
            pc.max_bytes = LLSDSerialize::SIZE_UNLIMITED;
            pc.max_depth = -1;
            pc.lines = false;
            checkParseCase(pc);
            pc.lines = true;
            pc.name = name + " (lines)";
            checkParseCase(pc);

            // Through real file streams, as the viewer reads them.
            LLSD a, b;
            {
                std::ifstream fa(path.c_str(), std::ios::binary);
                LLPointer<llsd_oracle::OracleXMLParser> p = new llsd_oracle::OracleXMLParser;
                p->parse(fa, a, LLSDSerialize::SIZE_UNLIMITED);
            }
            {
                std::ifstream fb(path.c_str(), std::ios::binary);
                ensure(name + ": file parse", LLSDSerialize::fromXML(b, fb) > 0 || a.isUndefined());
            }
            ensure(name + ": file value", equalBits(a, b));
            {
                std::ifstream fb(path.c_str());
                LLSD c;
                LLSDSerialize::fromXMLDocument(c, fb);
                ensure(name + ": text-mode file value", equalBits(a, c));
            }
            S32 c1 = 0, c2 = 0;
            std::string pa = formatOracle(a, XML, LLSDFormatter::OPTIONS_PRETTY, false, "", &c1);
            std::string pb = formatRust(a, XML, LLSDFormatter::OPTIONS_PRETTY, false, "", &c2);
            sameBytes(name + ": pretty xml", pb, pa);
            sameCount(name + ": pretty xml", c2, c1);
            ++checked;
        }
        ensure("checked app_settings files", checked > 10);
    }

    // The large documents: identical both ways, all formats.
    template<> template<>
    void parity_object::test<8>()
    {
        for (const ValueCase& vc : largeValues())
        {
            for (Format f : { NOTATION, BINARY, XML })
            {
                S32 c1 = 0, c2 = 0;
                std::string a = formatOracle(vc.value, f, LLSDFormatter::OPTIONS_PRETTY_BINARY, false, "", &c1);
                std::string b = formatRust(vc.value, f, LLSDFormatter::OPTIONS_PRETTY_BINARY, false, "", &c2);
                sameBytes(vc.name + " f" + std::to_string(f) + ": bytes", b, a);
                sameCount(vc.name + " f" + std::to_string(f), c2, c1);
                ParseCase pc;
                pc.name = vc.name + " f" + std::to_string(f);
                pc.format = f;
                pc.input = a;
                pc.max_bytes = (llssize)a.size();
                pc.max_depth = -1;
                pc.lines = false;
                checkParseCase(pc);
            }
        }
    }

    // zip/unzip round trip (zlib stays C++, the binary parser inside is Rust).
    template<> template<>
    void parity_object::test<9>()
    {
        for (const ValueCase& vc : values())
        {
            LLSD v = vc.value;
            std::string zipped = zip_llsd(v);
            ensure(vc.name + ": zipped", !zipped.empty());
            LLSD back;
            LLUZipHelper::EZipRresult r = LLUZipHelper::unzip_llsd(back, reinterpret_cast<const U8*>(zipped.data()), (S32)zipped.size());
            ensure(vc.name + ": unzip result", r == LLUZipHelper::ZR_OK);
            ensure(vc.name + ": unzip value", equalBits(back, vc.value));
        }
    }

    // A stream that throws on write (exceptions enabled, as
    // LLSettingsVOBase::exportFile does) makes the formatter throw a
    // catchable std::ios_base::failure; a throwing read surfaces from the
    // parser the same way. Neither may cross the Rust frames.
    template<> template<>
    void parity_object::test<11>()
    {
        LLSD value;
        for (const ValueCase& vc : values())
        {
            if (vc.name == "login response") value = vc.value;
        }
        for (LLSDSerialize::ELLSD_Serialize t : { LLSDSerialize::LLSD_XML, LLSDSerialize::LLSD_NOTATION, LLSDSerialize::LLSD_BINARY })
        {
            FailingOutBuf buf;
            std::ostream os(&buf);
            os.exceptions(std::ios_base::failbit | std::ios_base::badbit);
            bool threw = false;
            try
            {
                LLSDSerialize::serialize(value, os, t);
            }
            catch (const std::ios_base::failure&)
            {
                threw = true;
            }
            ensure("failing write throws ios_base::failure " + std::to_string((int)t), threw);
        }
        {
            FailingOutBuf buf;
            std::ostream os(&buf);
            os.exceptions(std::ios_base::failbit | std::ios_base::badbit);
            bool threw = false;
            try
            {
                LLSDSerialize::toPrettyXML(value, os);
            }
            catch (const std::ios_base::failure&)
            {
                threw = true;
            }
            ensure("failing pretty xml write throws", threw);
        }
        {
            // Without exceptions enabled the stream just goes bad, as before.
            FailingOutBuf buf;
            std::ostream os(&buf);
            LLSDSerialize::toXML(value, os);
            ensure("failing write without exceptions sets badbit", os.bad());
        }
        for (int format = 0; format < 3; ++format)
        {
            ThrowingInBuf buf;
            std::istream is(&buf);
            LLSD out;
            bool threw = false;
            try
            {
                switch (format)
                {
                case 0: LLSDSerialize::fromNotation(out, is, LLSDSerialize::SIZE_UNLIMITED); break;
                case 1: LLSDSerialize::fromBinary(out, is, LLSDSerialize::SIZE_UNLIMITED); break;
                default: LLSDSerialize::fromXML(out, is); break;
                }
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            ensure("throwing read surfaces from the parser " + std::to_string(format), threw);
        }
    }

    // The XML parser is reusable after reset(), as the unit tests use it.
    template<> template<>
    void parity_object::test<10>()
    {
        LLPointer<LLSDXMLParser> p = new LLSDXMLParser;
        for (int i = 0; i < 3; ++i)
        {
            std::istringstream in("<llsd><integer>" + std::to_string(i) + "</integer></llsd>");
            LLSD v;
            p->reset();
            sameCount("reused parser", p->parse(in, v, LLSDSerialize::SIZE_UNLIMITED), 1);
            ensure("reused parser value", v.asInteger() == i);
        }
        // Multiple XML documents in one stream, one per line.
        std::istringstream in("<llsd><integer>1</integer></llsd>\n<llsd><integer>2</integer></llsd>\n<llsd><integer>3</integer></llsd>\n");
        int sum = 0;
        LLSD cur;
        while (!in.eof() && LLSDParser::PARSE_FAILURE != LLSDSerialize::fromXML(cur, in))
        {
            sum += cur.asInteger();
        }
        ensure("multi-document stream", sum == 6);
    }
}

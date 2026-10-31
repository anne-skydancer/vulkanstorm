/**
 * @file llsdserialize.cpp
 * @author Phoenix
 * @date 2006-03-05
 * @brief Implementation of LLSD parsers and formatters
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// Since vkstorm-rust rung two the three parsers and three formatters are the
// Rust crate vkstorm-llsd (vkstorm-core-rust), reached through the vkstorm-ffi
// C ABI. This file keeps the stream plumbing: feeding an istream to the Rust
// parsers without reading past the parsed value, building LLSD from the
// parsed node buffer, walking LLSD into the Rust formatters, the header
// sniffing of LLSDSerialize::deserialize(), and the zlib helpers.

#include "linden_common.h"
#include "llsdserialize.h"
#include "llpointer.h"
#include "llstreamtools.h" // for cat_streambuf

#include <exception>
#include <iostream>
#include <sstream>
#include <vector>

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#ifdef LL_USESYSTEMLIBS
# include <zlib.h>
#else
# include "zlib-ng/zlib.h"  // for davep's dirty little zip functions
#endif

#include "lldate.h"
#include "llmemorystream.h"
#include "llsd.h"
#include "llstring.h"
#include "lluri.h"
#include "vkstorm_ffi.h"

// File constants
static const size_t MAX_HDR_LEN = 20;
static const S32 UNZIP_LLSD_MAX_DEPTH = 96;
static const char LEGACY_NON_HEADER[] = "<llsd>";
const std::string LLSD_BINARY_HEADER("LLSD/Binary");
const std::string LLSD_XML_HEADER("LLSD/XML");
const std::string LLSD_NOTATION_HEADER("llsd/notation");

//used to deflate a gzipped asset (currently used for navmeshes)
#define windowBits 15
#define ENABLE_ZLIB_GZIP 32

// If we published this in llsdserialize.h, we could use it in the
// implementation of LLSDOStreamer's operator<<().
template <class Formatter>
void format_using(const LLSD& data, std::ostream& ostr,
                  LLSDFormatter::EFormatterOptions options=LLSDFormatter::OPTIONS_PRETTY_BINARY)
{
    LLPointer<Formatter> f{ new Formatter };
    f->format(data, ostr, options);
}

template <class Parser>
S32 parse_using(std::istream& istr, LLSD& data, size_t max_bytes, S32 max_depth=-1)
{
    LLPointer<Parser> p{ new Parser };
    return p->parse(istr, data, max_bytes, max_depth);
}

// Case-insensitive test for the "<llsd>" start that older XML documents
// have in place of a header (formerly strncasecmp from the APR headers).
static bool starts_with_legacy_header(const char* buf, size_t len)
{
    const size_t tag_len = strlen(LEGACY_NON_HEADER);
    if (len < tag_len)
    {
        return false;
    }
    for (size_t i = 0; i < tag_len; ++i)
    {
        if (tolower((unsigned char)buf[i]) != LEGACY_NON_HEADER[i])
        {
            return false;
        }
    }
    return true;
}

/**
 * LLSDSerialize
 */

// static
void LLSDSerialize::serialize(const LLSD& sd, std::ostream& str, ELLSD_Serialize type,
                              LLSDFormatter::EFormatterOptions options)
{
    LLPointer<LLSDFormatter> f = NULL;

    switch (type)
    {
    case LLSD_BINARY:
        str << "<? " << LLSD_BINARY_HEADER << " ?>\n";
        f = new LLSDBinaryFormatter;
        break;

    case LLSD_XML:
        str << "<? " << LLSD_XML_HEADER << " ?>\n";
        f = new LLSDXMLFormatter;
        break;

    case LLSD_NOTATION:
        str << "<? " << LLSD_NOTATION_HEADER << " ?>\n";
        f = new LLSDNotationFormatter;
        break;

    default:
        LL_WARNS() << "serialize request for unknown ELLSD_Serialize" << LL_ENDL;
    }

    if (f.notNull())
    {
        f->format(sd, str, options);
    }
}

// static
bool LLSDSerialize::deserialize(LLSD& sd, std::istream& str, llssize max_bytes)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    char hdr_buf[MAX_HDR_LEN + 1] = ""; /* Flawfinder: ignore */
    bool fail_if_not_legacy = false;

    /*
     * Get the first line before anything. Don't read more than max_bytes:
     * this get() overload reads no more than (count-1) bytes into the
     * specified buffer. In the usual case when max_bytes exceeds
     * sizeof(hdr_buf), get() will read no more than sizeof(hdr_buf)-2.
     */
    llssize max_hdr_read = MAX_HDR_LEN;
    if (max_bytes != LLSDSerialize::SIZE_UNLIMITED)
    {
        max_hdr_read = llmin(max_bytes + 1, max_hdr_read);
    }
    str.get(hdr_buf, max_hdr_read, '\n');
    auto inbuf = str.gcount();

    // https://en.cppreference.com/w/cpp/io/basic_istream/get
    // When the get() above sees the specified delimiter '\n', it stops there
    // without pulling it from the stream. If it turns out that the stream
    // does NOT contain a header, and the content includes meaningful '\n',
    // it's important to pull that into hdr_buf too.
    if (inbuf < max_bytes && str.get(hdr_buf[inbuf]))
    {
        // got the delimiting '\n'
        ++inbuf;
        // None of the following requires that hdr_buf contain a final '\0'
        // byte. We could store one if needed, since even the incremented
        // inbuf won't exceed sizeof(hdr_buf)-1, but there's no need.
    }
    std::string header{ hdr_buf, static_cast<std::string::size_type>(inbuf) };
    if (str.fail())
    {
        str.clear();
        fail_if_not_legacy = true;
    }

    if (starts_with_legacy_header(hdr_buf, (size_t)inbuf))
    {   // Create a LLSD XML parser, and parse the first chunk read above.
        LLSDXMLParser x;
        x.parsePart(hdr_buf, inbuf);    // Parse the first part that was already read
        auto parsed = x.parse(str, sd, max_bytes - inbuf); // Parse the rest of it
        // Formally we should probably check (parsed != PARSE_FAILURE &&
        // parsed > 0), but since PARSE_FAILURE is -1, this suffices.
        return (parsed > 0);
    }

    if (fail_if_not_legacy)
    {
        LL_WARNS() << "deserialize LLSD parse failure" << LL_ENDL;
        return false;
    }

    /*
    * Remove the newline chars
    */
    std::string::size_type lastchar = header.find_last_not_of("\r\n");
    if (lastchar != std::string::npos)
    {
        // It's important that find_last_not_of() returns size_type, which is
        // why lastchar explicitly declares the type above. erase(size_type)
        // erases from that offset to the end of the string, whereas
        // erase(iterator) erases only a single character.
        header.erase(lastchar+1);
    }

    // trim off the <? ... ?> header syntax
    auto start = header.find_first_not_of("<? ");
    if (start != std::string::npos)
    {
        auto end = header.find_first_of(" ?", start);
        if (end != std::string::npos)
        {
            header = header.substr(start, end - start);
            ws(str);
        }
    }
    /*
     * Create the parser as appropriate
     */
    if (0 == LLStringUtil::compareInsensitive(header, LLSD_BINARY_HEADER))
    {
        return (parse_using<LLSDBinaryParser>(str, sd, max_bytes-inbuf) > 0);
    }
    else if (0 == LLStringUtil::compareInsensitive(header, LLSD_XML_HEADER))
    {
        return (parse_using<LLSDXMLParser>(str, sd, max_bytes-inbuf) > 0);
    }
    else if (0 == LLStringUtil::compareInsensitive(header, LLSD_NOTATION_HEADER))
    {
        return (parse_using<LLSDNotationParser>(str, sd, max_bytes-inbuf) > 0);
    }
    else // no header we recognize
    {
        LLPointer<LLSDParser> p;
        if (inbuf && hdr_buf[0] == '<')
        {
            // looks like XML
            LL_DEBUGS() << "deserialize request with no header, assuming XML" << LL_ENDL;
            p = new LLSDXMLParser;
        }
        else
        {
            // assume notation
            LL_DEBUGS() << "deserialize request with no header, assuming notation" << LL_ENDL;
            p = new LLSDNotationParser;
        }
        // Since we've already read 'inbuf' bytes into 'hdr_buf', prepend that
        // data to whatever remains in 'str'.
        LLMemoryStreamBuf already(reinterpret_cast<const U8*>(hdr_buf), (S32)inbuf);
        cat_streambuf prebuff(&already, str.rdbuf());
        std::istream  prepend(&prebuff);
        return (p->parse(prepend, sd, max_bytes) > 0);
    }
}

/**
 * The Rust boundary.
 */
namespace
{
    /**
     * How a stream is fed to the Rust parser.
     *
     * BULK: the stream buffer is memory backed and seekable (std::stringbuf,
     * LLMemoryStreamBuf, a boost array source); pull what is in memory in
     * chunks and seek back whatever the parser did not consume.
     * LINE: XML on any other stream; pull one line at a time, which is how
     * the expat-based parser read, and what it consumed: the parse ends at
     * </llsd> with the rest of that line.
     * EXACT: notation or binary on any other stream; pull exactly the bytes
     * the parser asks for, peeking through the stream buffer, so the stream
     * is left right after the value, as the C++ parsers left it.
     */
    enum class SourceMode { BULK, LINE, EXACT };

    struct StreamSource
    {
        std::streambuf* mBuf;
        SourceMode mMode;
        bool mHitEof = false;
        // A C++ exception from the stream must not unwind through the Rust
        // frames (undefined behaviour; Rust aborts on a foreign exception).
        // It is captured here, the source reports end of input from then
        // on, and the caller rethrows once the Rust call has returned.
        std::exception_ptr mError;

        static size_t readCb(void* ctx, uint8_t* buf, size_t cap)
        {
            StreamSource& self = *static_cast<StreamSource*>(ctx);
            if (cap == 0 || self.mError)
            {
                return 0;
            }
            try
            {
                return self.read(buf, cap);
            }
            catch (...)
            {
                self.mError = std::current_exception();
                self.mHitEof = true;
                return 0;
            }
        }

        size_t read(uint8_t* buf, size_t cap)
        {
            StreamSource& self = *this;
            switch (self.mMode)
            {
            case SourceMode::BULK:
            {
                std::streamsize avail = self.mBuf->in_avail();
                std::streamsize want = (std::streamsize)cap;
                if (avail > 0 && avail < want)
                {
                    want = avail;
                }
                std::streamsize got = self.mBuf->sgetn(reinterpret_cast<char*>(buf), want);
                if (got <= 0)
                {
                    self.mHitEof = true;
                    return 0;
                }
                return (size_t)got;
            }
            case SourceMode::LINE:
            {
                size_t n = 0;
                while (n < cap)
                {
                    int c = self.mBuf->sbumpc();
                    if (c == EOF)
                    {
                        self.mHitEof = true;
                        break;
                    }
                    buf[n++] = (uint8_t)c;
                    if (c == '\n')
                    {
                        break;
                    }
                }
                return n;
            }
            case SourceMode::EXACT:
            default:
            {
                std::streamsize got = self.mBuf->sgetn(reinterpret_cast<char*>(buf), (std::streamsize)cap);
                if (got < (std::streamsize)cap)
                {
                    self.mHitEof = true;
                }
                return got > 0 ? (size_t)got : 0;
            }
            }
        }

        static int32_t peekCb(void* ctx)
        {
            StreamSource& self = *static_cast<StreamSource*>(ctx);
            if (self.mError)
            {
                return -1;
            }
            int c;
            try
            {
                c = self.mBuf->sgetc();
            }
            catch (...)
            {
                self.mError = std::current_exception();
                self.mHitEof = true;
                return -1;
            }
            if (c == EOF)
            {
                self.mHitEof = true;
                return -1;
            }
            return c & 0xff;
        }
    };

    SourceMode chooseMode(std::istream& istr, int32_t format)
    {
        std::streambuf* sb = istr.rdbuf();
        if (dynamic_cast<std::stringbuf*>(sb)
            || dynamic_cast<LLMemoryStreamBuf*>(sb)
            || dynamic_cast<boost::iostreams::stream_buffer<boost::iostreams::array_source>*>(sb))
        {
            return SourceMode::BULK;
        }
        return format == VKS_LLSD_XML ? SourceMode::LINE : SourceMode::EXACT;
    }

    /**
     * Builds LLSD from the parsed node buffer. Map keys repeat as the C++
     * parsers had it: the notation and binary parsers inserted (first value
     * stays), the XML parser assigned through operator[] (last value wins).
     * A second top-level value replaces the first, as the XML parser did.
     */
    void buildLLSD(const VksLlsdDoc* doc, LLSD& out, bool last_key_wins)
    {
        size_t node_count = 0;
        const VksLlsdNode* nodes = vks_llsd_doc_nodes(doc, &node_count);
        size_t arena_len = 0;
        const uint8_t* arena = vks_llsd_doc_bytes(doc, &arena_len);

        struct Frame
        {
            LLSD* target;
            uint32_t remaining;
            bool map;
        };
        std::vector<Frame> stack;
        std::string key;

        // A value completed: close every container that just became full.
        auto completeValue = [&stack]()
        {
            while (!stack.empty() && --stack.back().remaining == 0)
            {
                stack.pop_back();
            }
        };

        for (size_t i = 0; i < node_count; ++i)
        {
            const VksLlsdNode& node = nodes[i];
            const char* bytes = reinterpret_cast<const char*>(arena + node.payload);
            if (node.kind == VKS_LLSD_NODE_KEY)
            {
                key.assign(bytes, node.len);
                continue;
            }

            LLSD* slot;
            if (stack.empty())
            {
                slot = &out;
            }
            else if (stack.back().map)
            {
                LLSD& map = *stack.back().target;
                if (last_key_wins)
                {
                    slot = &map[key];
                }
                else
                {
                    // One lookup: operator[] inserts a new entry or finds the
                    // existing one; the size tells which.
                    size_t before = map.size();
                    slot = &map[key];
                    if (map.size() == before)
                    {
                        // A repeated key: the first value stays. Skip this
                        // whole subtree in the node buffer rather than build
                        // it anywhere; building it in shared scratch storage
                        // let a nested repeated key destroy an object the
                        // frames above still pointed into.
                        uint32_t pending = 1;
                        while (pending > 0 && i < node_count)
                        {
                            const VksLlsdNode& skipped = nodes[i];
                            if (skipped.kind == VKS_LLSD_NODE_KEY)
                            {
                                ++i;
                                continue;
                            }
                            --pending;
                            if (skipped.kind == VKS_LLSD_NODE_MAP || skipped.kind == VKS_LLSD_NODE_ARRAY)
                            {
                                pending += skipped.len;
                            }
                            if (pending > 0)
                            {
                                ++i;
                            }
                        }
                        completeValue();
                        continue;
                    }
                }
            }
            else
            {
                slot = &stack.back().target->append(LLSD());
            }

            bool container = false;
            switch (node.kind)
            {
            case VKS_LLSD_NODE_UNDEF:
                slot->clear();
                break;
            case VKS_LLSD_NODE_BOOLEAN:
                *slot = (node.payload != 0);
                break;
            case VKS_LLSD_NODE_INTEGER:
                *slot = (S32)(int64_t)node.payload;
                break;
            case VKS_LLSD_NODE_REAL:
            {
                F64 v;
                memcpy(&v, &node.payload, sizeof(v));
                *slot = v;
                break;
            }
            case VKS_LLSD_NODE_STRING:
                *slot = std::string(bytes, node.len);
                break;
            case VKS_LLSD_NODE_UUID:
            {
                LLUUID id;
                memcpy(id.mData, bytes, UUID_BYTES);
                *slot = id;
                break;
            }
            case VKS_LLSD_NODE_DATE:
            {
                F64 v;
                memcpy(&v, &node.payload, sizeof(v));
                *slot = LLDate(v);
                break;
            }
            case VKS_LLSD_NODE_URI:
                *slot = LLURI(std::string(bytes, node.len));
                break;
            case VKS_LLSD_NODE_BINARY:
                *slot = std::vector<U8>(bytes, bytes + node.len);
                break;
            case VKS_LLSD_NODE_MAP:
                *slot = LLSD::emptyMap();
                container = true;
                break;
            case VKS_LLSD_NODE_ARRAY:
                *slot = LLSD::emptyArray();
                container = true;
                break;
            default:
                slot->clear();
                break;
            }

            if (container && node.len > 0)
            {
                stack.push_back(Frame{ slot, node.len, node.kind == VKS_LLSD_NODE_MAP });
                continue;
            }
            completeValue();
        }
    }

    /**
     * Runs the Rust parser of `format` over `istr` and delivers the result
     * with the C++ parsers' conventions for `data`.
     */
    S32 parseWithRust(int32_t format, std::istream& istr, LLSD& data, llssize max_bytes, S32 max_depth,
                      const std::string& prefix, bool lines)
    {
        SourceMode mode = chooseMode(istr, format);
        StreamSource source{ istr.rdbuf(), mode };
        VksLlsdSource cs;
        cs.ctx = &source;
        cs.read = &StreamSource::readCb;
        cs.peek = &StreamSource::peekCb;
        cs.exact = (mode == SourceMode::EXACT) ? 1 : 0;

        VksLlsdParseResult result = { 0, 0, 0 };
        VksLlsdDoc* doc = vks_llsd_parse(format, &cs,
                                         reinterpret_cast<const uint8_t*>(prefix.data()), prefix.size(),
                                         (max_bytes == LLSDSerialize::SIZE_UNLIMITED) ? -1 : (int64_t)max_bytes,
                                         max_depth, lines ? 1 : 0, &result);

        if (result.unconsumed)
        {
            // Give back what was pulled ahead of the parsed value: seek on a
            // seekable buffer, else unget byte by byte (a stream that is not
            // memory backed is fed exactly, so this is at most the one byte
            // a C++ parser would have put back).
            std::streambuf* sb = istr.rdbuf();
            if (sb->pubseekoff(-(std::streamoff)result.unconsumed, std::ios::cur, std::ios::in) == std::streampos(-1))
            {
                for (size_t i = 0; i < result.unconsumed; ++i)
                {
                    if (sb->sungetc() == EOF)
                    {
                        break;
                    }
                }
            }
        }
        if (source.mError)
        {
            // The stream threw inside a callback: the Rust call has returned
            // and nothing of it is live any more, so the exception can go on
            // to the caller as it would have from the C++ parsers.
            vks_llsd_doc_free(doc);
            std::rethrow_exception(source.mError);
        }
        if (source.mHitEof)
        {
            istr.setstate(std::ios::eofbit);
        }

        if (result.count == LLSDParser::PARSE_FAILURE)
        {
            data.clear();
        }
        else if (result.count > 0)
        {
            LLSD built;
            buildLLSD(doc, built, format == VKS_LLSD_XML);
            data = std::move(built);
        }
        else if (format == VKS_LLSD_XML)
        {
            data = LLSD();
        }
        vks_llsd_doc_free(doc);
        return result.count;
    }

    inline bool is_eol(char c)
    {
        return (c == '\n' || c == '\r');
    }

    // As the expat-based parser: eat the line ends after the document.
    void clear_eol(std::istream& input)
    {
        char c = input.peek();
        while (input.good() && is_eol(c))
        {
            input.get(c);
            c = input.peek();
        }
    }

    // ---- formatting

    // The formatter output sink. A stream with exceptions enabled (as
    // LLSettingsVOBase::exportFile arranges) throws std::ios_base::failure on
    // a failed write; that must not unwind through the Rust frames, so the
    // exception is captured, further output is dropped, and format()
    // rethrows it once the Rust formatter has been released.
    struct OstreamSink
    {
        std::ostream* mStream;
        std::exception_ptr mError;
    };

    void writeToOstream(void* ctx, const uint8_t* data, size_t len)
    {
        OstreamSink& sink = *static_cast<OstreamSink*>(ctx);
        if (sink.mError)
        {
            return;
        }
        try
        {
            sink.mStream->write(reinterpret_cast<const char*>(data), (std::streamsize)len);
        }
        catch (...)
        {
            sink.mError = std::current_exception();
        }
    }

    void appendToString(void* ctx, const uint8_t* data, size_t len)
    {
        static_cast<std::string*>(ctx)->append(reinterpret_cast<const char*>(data), len);
    }

    void walkLLSD(const LLSD& data, VksLlsdFormatter* f, const std::string& real_format)
    {
        switch (data.type())
        {
        case LLSD::TypeMap:
        {
            vks_llsd_fmt_map_begin(f, data.size());
            LLSD::map_const_iterator iter = data.beginMap();
            LLSD::map_const_iterator end = data.endMap();
            for (; iter != end; ++iter)
            {
                vks_llsd_fmt_key(f, reinterpret_cast<const uint8_t*>(iter->first.data()), iter->first.size());
                walkLLSD(iter->second, f, real_format);
            }
            vks_llsd_fmt_map_end(f);
            break;
        }
        case LLSD::TypeArray:
        {
            vks_llsd_fmt_array_begin(f, data.size());
            LLSD::array_const_iterator iter = data.beginArray();
            LLSD::array_const_iterator end = data.endArray();
            for (; iter != end; ++iter)
            {
                walkLLSD(*iter, f, real_format);
            }
            vks_llsd_fmt_array_end(f);
            break;
        }
        case LLSD::TypeBoolean:
            vks_llsd_fmt_boolean(f, data.asBoolean() ? 1 : 0);
            break;
        case LLSD::TypeInteger:
            vks_llsd_fmt_integer(f, data.asInteger());
            break;
        case LLSD::TypeReal:
            if (real_format.empty())
            {
                vks_llsd_fmt_real(f, data.asReal());
            }
            else
            {
                std::string text = llformat(real_format.c_str(), data.asReal());
                vks_llsd_fmt_real_text(f, data.asReal(), reinterpret_cast<const uint8_t*>(text.data()), text.size());
            }
            break;
        case LLSD::TypeUUID:
        {
            LLUUID id = data.asUUID();
            vks_llsd_fmt_uuid(f, id.mData);
            break;
        }
        case LLSD::TypeString:
        {
            const std::string& s = data.asStringRef();
            vks_llsd_fmt_string(f, reinterpret_cast<const uint8_t*>(s.data()), s.size());
            break;
        }
        case LLSD::TypeDate:
            vks_llsd_fmt_date(f, data.asDate().secondsSinceEpoch());
            break;
        case LLSD::TypeURI:
        {
            std::string s = data.asString();
            vks_llsd_fmt_uri(f, reinterpret_cast<const uint8_t*>(s.data()), s.size());
            break;
        }
        case LLSD::TypeBinary:
        {
            const std::vector<U8>& b = data.asBinary();
            vks_llsd_fmt_binary(f, b.empty() ? nullptr : b.data(), b.size());
            break;
        }
        case LLSD::TypeUndefined:
        default:
            vks_llsd_fmt_undef(f);
            break;
        }
    }

    std::string escapeWithRust(int32_t kind, const std::string& in)
    {
        std::string out;
        vks_llsd_escape(kind, reinterpret_cast<const uint8_t*>(in.data()), in.size(), &out, &appendToString);
        return out;
    }
}

/**
 * LLSDParser
 */
LLSDParser::LLSDParser()
    : mMaxBytes(LLSDSerialize::SIZE_UNLIMITED), mParseLines(false)
{
}

// virtual
LLSDParser::~LLSDParser()
{ }

S32 LLSDParser::parse(std::istream& istr, LLSD& data, llssize max_bytes, S32 max_depth)
{
    mMaxBytes = max_bytes;
    mParseLines = false;
    return doParse(istr, data, max_depth);
}


// Parse using routine to get() lines, faster than parse()
S32 LLSDParser::parseLines(std::istream& istr, LLSD& data)
{
    mMaxBytes = LLSDSerialize::SIZE_UNLIMITED;
    mParseLines = true;
    return doParse(istr, data);
}


/**
 * LLSDNotationParser
 */
LLSDNotationParser::LLSDNotationParser()
{
}

// virtual
LLSDNotationParser::~LLSDNotationParser()
{ }

// virtual
S32 LLSDNotationParser::doParse(std::istream& istr, LLSD& data, S32 max_depth) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    return parseWithRust(VKS_LLSD_NOTATION, istr, data, mMaxBytes, max_depth, std::string(), false);
}


/**
 * LLSDBinaryParser
 */
LLSDBinaryParser::LLSDBinaryParser()
{
}

// virtual
LLSDBinaryParser::~LLSDBinaryParser()
{
}

// virtual
S32 LLSDBinaryParser::doParse(std::istream& istr, LLSD& data, S32 max_depth) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    return parseWithRust(VKS_LLSD_BINARY, istr, data, mMaxBytes, max_depth, std::string(), false);
}


/**
 * LLSDXMLParser
 */
LLSDXMLParser::LLSDXMLParser(bool emit_errors /* = true */) : mEmitErrors(emit_errors)
{
}

LLSDXMLParser::~LLSDXMLParser()
{
}

void LLSDXMLParser::parsePart(const char *buf, llssize len)
{
    if (buf != NULL && len > 0)
    {
        mPrefix.append(buf, (size_t)len);
    }
}

// virtual
S32 LLSDXMLParser::doParse(std::istream& input, LLSD& data, S32 max_depth) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_LLSD;
    std::string prefix;
    prefix.swap(mPrefix);
    if (mParseLines)
    {
        // Must get rid of any leading \n, otherwise the stream gets into an error/eof state
        clear_eol(input);
    }
    // <Vulkanstorm> max_depth: the viewer's XML callers pass -1 (unlimited,
    // as the C++ XML parser always was); a depth-capped caller (AIS replies)
    // gets PARSE_FAILURE for a value max_depth containers deep or more, as
    // from the binary and notation parsers.
    S32 count = parseWithRust(VKS_LLSD_XML, input, data, LLSDSerialize::SIZE_UNLIMITED, max_depth, prefix, mParseLines);
    // </Vulkanstorm>
    if (count == LLSDParser::PARSE_FAILURE)
    {
        if (mEmitErrors)
        {
            LL_INFOS() << "LLSDXMLParser::doParse: XML parse error" << LL_ENDL;
        }
        return count;
    }
    clear_eol(input);
    return count;
}

//  virtual
void LLSDXMLParser::doReset()
{
    mPrefix.clear();
}


/**
 * LLSDFormatter
 */
LLSDFormatter::LLSDFormatter(bool boolAlpha, const std::string& realFmt, EFormatterOptions options):
    mOptions(options)
{
    boolalpha(boolAlpha);
    realFormat(realFmt);
}

// virtual
LLSDFormatter::~LLSDFormatter()
{ }

void LLSDFormatter::boolalpha(bool alpha)
{
    mBoolAlpha = alpha;
}

void LLSDFormatter::realFormat(const std::string& format)
{
    mRealFormat = format;
}

S32 LLSDFormatter::format(const LLSD& data, std::ostream& ostr) const
{
    // pass options captured by constructor
    return format(data, ostr, mOptions);
}

S32 LLSDFormatter::format(const LLSD& data, std::ostream& ostr, EFormatterOptions options) const
{
    uint32_t opts = 0;
    if (options & OPTIONS_PRETTY) opts |= VKS_LLSD_OPT_PRETTY;
    if (options & OPTIONS_PRETTY_BINARY) opts |= VKS_LLSD_OPT_PRETTY_BINARY;
    if (mBoolAlpha || (ostr.flags() & std::ios::boolalpha)) opts |= VKS_LLSD_OPT_BOOLALPHA;

    std::ios::fmtflags ff = ostr.flags() & std::ios::floatfield;
    int32_t floatfield = (ff == std::ios::fixed) ? 1 : (ff == std::ios::scientific) ? 2 : 0;

    OstreamSink sink{ &ostr, nullptr };
    VksLlsdFormatter* f = vks_llsd_formatter_new(rustFormat(), opts, (int32_t)ostr.precision(), floatfield,
                                                 &sink, &writeToOstream);
    if (!f)
    {
        return 0;
    }
    walkLLSD(data, f, mRealFormat);
    S32 count = vks_llsd_formatter_finish(f);
    if (sink.mError)
    {
        std::rethrow_exception(sink.mError);
    }
    return count;
}

/**
 * LLSDNotationFormatter
 */
LLSDNotationFormatter::LLSDNotationFormatter(bool boolAlpha, const std::string& realFormat,
                                             EFormatterOptions options):
    LLSDFormatter(boolAlpha, realFormat, options)
{
}

// virtual
LLSDNotationFormatter::~LLSDNotationFormatter()
{ }

// static
std::string LLSDNotationFormatter::escapeString(const std::string& in)
{
    return escapeWithRust(0, in);
}

S32 LLSDNotationFormatter::rustFormat() const
{
    return VKS_LLSD_NOTATION;
}

/**
 * LLSDXMLFormatter
 */
LLSDXMLFormatter::LLSDXMLFormatter(bool boolAlpha, const std::string& realFormat,
                                   EFormatterOptions options):
    LLSDFormatter(boolAlpha, realFormat, options)
{
}

// virtual
LLSDXMLFormatter::~LLSDXMLFormatter()
{
}

// static
std::string LLSDXMLFormatter::escapeString(const std::string& in)
{
    return escapeWithRust(1, in);
}

S32 LLSDXMLFormatter::rustFormat() const
{
    return VKS_LLSD_XML;
}

/**
 * LLSDBinaryFormatter
 */
LLSDBinaryFormatter::LLSDBinaryFormatter(bool boolAlpha, const std::string& realFormat,
                                         EFormatterOptions options):
    LLSDFormatter(boolAlpha, realFormat, options)
{
}

// virtual
LLSDBinaryFormatter::~LLSDBinaryFormatter()
{ }

S32 LLSDBinaryFormatter::rustFormat() const
{
    return VKS_LLSD_BINARY;
}

std::ostream& operator<<(std::ostream& s, const LLSD& llsd)
{
    s << LLSDNotationStreamer(llsd);
    return s;
}


//dirty little zippers -- yell at davep if these are horrid

//return a string containing gzipped bytes of binary serialized LLSD
// VERY inefficient -- creates several copies of LLSD block in memory
std::string zip_llsd(LLSD& data)
{
    std::stringstream llsd_strm;

    LLSDSerialize::toBinary(data, llsd_strm);

    const U32 CHUNK = 65536;

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    S32 ret = deflateInit(&strm, Z_BEST_COMPRESSION);
    if (ret != Z_OK)
    {
        LL_WARNS() << "Failed to compress LLSD block." << LL_ENDL;
        return std::string();
    }

    std::string source = std::move(llsd_strm).str();

    U8 out[CHUNK];

    strm.avail_in = narrow<size_t>(source.size());
    strm.next_in = (U8*) source.data();
    U8* output = NULL;

    U32 cur_size = 0;

    U32 have = 0;

    do
    {
        strm.avail_out = CHUNK;
        strm.next_out = out;

        ret = deflate(&strm, Z_FINISH);
        if (ret == Z_OK || ret == Z_STREAM_END)
        { //copy result into output
            if (strm.avail_out >= CHUNK)
            {
                deflateEnd(&strm);
                if(output)
                    free(output);
                LL_WARNS() << "Failed to compress LLSD block." << LL_ENDL;
                return std::string();
            }

            have = CHUNK-strm.avail_out;
            U8* new_output = (U8*) realloc(output, cur_size+have);
            if (new_output == NULL)
            {
                LL_WARNS() << "Failed to compress LLSD block: can't reallocate memory, current size: " << cur_size << " bytes; requested " << cur_size + have << " bytes." << LL_ENDL;
                deflateEnd(&strm);
                if (output)
                {
                    free(output);
                }
                return std::string();
            }
            output = new_output;
            memcpy(output+cur_size, out, have);
            cur_size += have;
        }
        else
        {
            deflateEnd(&strm);
            if(output)
                free(output);
            LL_WARNS() << "Failed to compress LLSD block." << LL_ENDL;
            return std::string();
        }
    }
    while (ret == Z_OK);

    std::string::size_type size = cur_size;

    std::string result((char*) output, size);
    deflateEnd(&strm);
    if(output)
        free(output);

    return result;
}

//decompress a block of LLSD from provided istream
// not very efficient -- creats a copy of decompressed LLSD block in memory
// and deserializes from that copy using LLSDSerialize
LLUZipHelper::EZipRresult LLUZipHelper::unzip_llsd(LLSD& data, std::istream& is, S32 size)
{
    std::unique_ptr<U8[]> in = std::unique_ptr<U8[]>(new(std::nothrow) U8[size]);
    if (!in)
    {
        return ZR_MEM_ERROR;
    }
    is.read((char*) in.get(), size);

    return unzip_llsd(data, in.get(), size);
}

LLUZipHelper::EZipRresult LLUZipHelper::unzip_llsd(LLSD& data, const U8* in, S32 size)
{
    U8* result = NULL;
    llssize cur_size = 0;
    z_stream strm;

    constexpr U32 CHUNK = 1024 * 512;

    static thread_local std::unique_ptr<U8[]> out;
    if (!out)
    {
        out = std::unique_ptr<U8[]>(new(std::nothrow) U8[CHUNK]);
    }

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = size;
    strm.next_in = const_cast<U8*>(in);

    S32 ret = inflateInit(&strm);

    do
    {
        strm.avail_out = CHUNK;
        strm.next_out = out.get();
        ret = inflate(&strm, Z_NO_FLUSH);
        switch (ret)
        {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        {
            inflateEnd(&strm);
            // free(result);
            if( result )
                free(result);
            return ZR_DATA_ERROR;
        }
        case Z_STREAM_ERROR:
        case Z_BUF_ERROR:
        {
            inflateEnd(&strm);
            free(result);
            return ZR_BUFFER_ERROR;
        }

        case Z_MEM_ERROR:
        {
            inflateEnd(&strm);
            // free(result);
            if( result )
                free(result);
            return ZR_MEM_ERROR;
        }
        }

        U32 have = CHUNK-strm.avail_out;

        U8* new_result = (U8*)realloc(result, cur_size + have);
        if (new_result == NULL)
        {
            inflateEnd(&strm);
            if (result)
            {
                free(result);
            }
            return ZR_MEM_ERROR;
        }
        result = new_result;
        memcpy(result+cur_size, out.get(), have);
        cur_size += have;

    } while (ret == Z_OK && ret != Z_STREAM_END);

    inflateEnd(&strm);

    if (ret != Z_STREAM_END)
    {
        // free(result);
        if( result )
            free(result);
        return ZR_DATA_ERROR;
    }

    //result now points to the decompressed LLSD block
    {
        char* result_ptr = strip_deprecated_header((char*)result, cur_size);

        boost::iostreams::stream<boost::iostreams::array_source> istrm(result_ptr, cur_size);

        if (!LLSDSerialize::fromBinary(data, istrm, cur_size, UNZIP_LLSD_MAX_DEPTH))
        {
            // free(result);
            if( result )
                free(result);
            return ZR_PARSE_ERROR;
        }
    }

    // free(result);
    if( result )
        free(result);
    return ZR_OK;
}
//This unzip function will only work with a gzip header and trailer - while the contents
//of the actual compressed data is the same for either format (gzip vs zlib ), the headers
//and trailers are different for the formats.
U8* unzip_llsdNavMesh( bool& valid, size_t& outsize, std::istream& is, S32 size )
{
    if (size == 0)
    {
        LL_WARNS() << "No data to unzip." << LL_ENDL;
        return NULL;
    }

    U8* result = NULL;
    U32 cur_size = 0;
    z_stream strm;

    const U32 CHUNK = 0x4000;

    U8 *in = new(std::nothrow) U8[size];
    if (in == NULL)
    {
        LL_WARNS() << "Memory allocation failure." << LL_ENDL;
        return NULL;
    }
    is.read((char*) in, size);

    U8 out[CHUNK];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = size;
    strm.next_in = in;


    S32 ret = inflateInit2(&strm,  windowBits | ENABLE_ZLIB_GZIP );
    do
    {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR)
        {
            inflateEnd(&strm);
            free(result);
            delete [] in;
            valid = false;
        }

        switch (ret)
        {
        case Z_NEED_DICT:
            ret = Z_DATA_ERROR;
            [[fallthrough]];
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            inflateEnd(&strm);
            free(result);
            delete [] in;
            valid = false;
            break;
        }

        U32 have = CHUNK-strm.avail_out;

        U8* new_result = (U8*) realloc(result, cur_size + have);
        if (new_result == NULL)
        {
            LL_WARNS() << "Failed to unzip LLSD NavMesh block: can't reallocate memory, current size: " << cur_size
                << " bytes; requested " << cur_size + have
                << " bytes; total syze: ." << size << " bytes."
                << LL_ENDL;
            inflateEnd(&strm);
            if (result)
            {
                free(result);
            }
            delete[] in;
            valid = false;
            return NULL;
        }
        result = new_result;
        memcpy(result+cur_size, out, have);
        cur_size += have;

    } while (ret == Z_OK);

    inflateEnd(&strm);
    delete [] in;

    if (ret != Z_STREAM_END)
    {
        free(result);
        valid = false;
        return NULL;
    }

    //result now points to the decompressed LLSD block
    {
        outsize= cur_size;
        valid = true;
    }

    return result;
}

char* strip_deprecated_header(char* in, llssize& cur_size, llssize* header_size)
{
    const char* deprecated_header = "<? LLSD/Binary ?>";
    constexpr size_t deprecated_header_size = 17;

    if (cur_size > deprecated_header_size
        && memcmp(in, deprecated_header, deprecated_header_size) == 0)
    {
        in = in + deprecated_header_size;
        cur_size = cur_size - deprecated_header_size;
        if (header_size)
        {
            *header_size = deprecated_header_size + 1;
        }
    }

    return in;
}

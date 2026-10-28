/**
 * @file llmessagetemplate_test.cpp
 * @brief The message template system against a recorded corpus (vkstorm-rust).
 *
 * The corpus (tests/fixtures/llmessage_corpus.vksm) was recorded from the
 * C++ template parser, message builder and reader, zero-coder and data
 * packers at the commit before the port to Rust, by this same program with
 * VKS_MSG_DUMP=<file>. It holds the template dump, every message of the
 * template packed with deterministic values at four sizes (with the
 * zero-coded form), those packets read back (every variable's bytes and its
 * typed getter's value) complete, truncated at several points and with
 * corrupted counts and sizes, zero-code expansion of coded and malformed
 * packets, and scripted LLDataPacker runs in the binary, ASCII buffer and
 * ASCII file forms. Where the C++ read past the packet (a variable-length
 * field whose declared size runs past the end) the recorder stored the
 * stated product behaviour (the field is empty and reported) rather than
 * what the C++ copied from beyond the packet; the same for the text
 * packers' conversions that left a value uninitialised.
 *
 * VKS_MSG_BENCH=<iterations> times the hot paths and prints a table.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "llapr.h"
#include "lldatapacker.h"
#include "llmessagetemplate.h"
#include "llmessagetemplateparser.h"
#include "lltemplatemessagebuilder.h"
#include "lltemplatemessagereader.h"
#include "llerrorcontrol.h"
#include "message.h"
#include "message_prehash.h"
#include "llmath.h"
#include "llquaternion.h"
#include "lluuid.h"
#include "v2math.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4color.h"
#include "v4coloru.h"
#include "v4math.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef VKS_MSG_TEMPLATE_FILE
#error VKS_MSG_TEMPLATE_FILE must name scripts/messages/message_template.msg
#endif
#ifndef VKS_MSG_FIXTURE_DIR
#error VKS_MSG_FIXTURE_DIR must name the directory of the recorded corpus
#endif

#define VKS_MSG_FIXTURE VKS_MSG_FIXTURE_DIR "/llmessage_corpus.vksm"

namespace
{
    using tut::ensure;
    using tut::ensure_equals;

    constexpr U32 FIXTURE_MAGIC = 0x4D534B56; // "VKSM"
    constexpr U32 FIXTURE_VERSION = 1;

    //------------------------------------------------------------------
    // byte stream helpers
    //------------------------------------------------------------------
    struct Writer
    {
        std::vector<U8> b;
        void raw(const void* p, size_t n) { const U8* s = (const U8*)p; b.insert(b.end(), s, s + n); }
        void u8(U8 v) { b.push_back(v); }
        void u16(U16 v) { raw(&v, 2); }
        void u32(U32 v) { raw(&v, 4); }
        void i32(S32 v) { raw(&v, 4); }
        void f32(F32 v) { raw(&v, 4); }
        void str(const std::string& s) { u32((U32)s.size()); raw(s.data(), s.size()); }
        void bytes(const std::vector<U8>& v) { u32((U32)v.size()); raw(v.data(), v.size()); }
        void bytes(const void* p, size_t n) { u32((U32)n); raw(p, n); }
    };

    struct Reader
    {
        const U8* p;
        const U8* end;
        bool ok;
        Reader(const std::vector<U8>& v) : p(v.data()), end(v.data() + v.size()), ok(true) {}
        bool take(void* dst, size_t n)
        {
            if ((size_t)(end - p) < n) { ok = false; memset(dst, 0, n); return false; }
            memcpy(dst, p, n); p += n; return true;
        }
        U8 u8() { U8 v; take(&v, 1); return v; }
        U16 u16() { U16 v; take(&v, 2); return v; }
        U32 u32() { U32 v; take(&v, 4); return v; }
        S32 i32() { S32 v; take(&v, 4); return v; }
        F32 f32() { F32 v; take(&v, 4); return v; }
        std::string str() { U32 n = u32(); if ((size_t)(end - p) < n) { ok = false; return std::string(); } std::string s((const char*)p, n); p += n; return s; }
        std::vector<U8> bytes() { U32 n = u32(); if ((size_t)(end - p) < n) { ok = false; return std::vector<U8>(); } std::vector<U8> v(p, p + n); p += n; return v; }
    };

    struct Rng
    {
        U32 s;
        explicit Rng(U32 seed) : s(seed ? seed : 0x9E3779B9u) {}
        U32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        U8 byte() { return (U8)(next() >> 24); }
    };

    U32 seed_of(U32 a, U32 b, U32 c, U32 d, U32 e)
    {
        U32 h = 2166136261u;
        for (U32 v : { a, b, c, d, e }) { h ^= v; h *= 16777619u; h ^= h >> 13; }
        return h;
    }

    //------------------------------------------------------------------
    // the template
    //------------------------------------------------------------------
    struct TemplateSet
    {
        F32 version = 0.f;
        std::vector<LLMessageTemplate*> messages; // in file order (the parser's list)
        LLTemplateMessageBuilder::message_template_name_map_t name_map;
        LLTemplateMessageReader::message_template_number_map_t number_map;
    };

    std::string read_file(const std::string& path)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    TemplateSet& templates()
    {
        static TemplateSet* set = NULL;
        if (!set)
        {
            set = new TemplateSet;
            std::string body = read_file(VKS_MSG_TEMPLATE_FILE);
            LLTemplateTokenizer tokens(body);
            LLTemplateParser parsed(tokens);
            set->version = parsed.getVersion();
            for (LLTemplateParser::message_iterator it = parsed.getMessagesBegin(); it != parsed.getMessagesEnd(); ++it)
            {
                LLMessageTemplate* t = *it;
                t->setHandlerFunc(null_message_callback, NULL);
                set->messages.push_back(t);
                set->name_map[t->mName] = t;
                set->number_map[t->mMessageNumber] = t;
            }
        }
        return *set;
    }

    void ensure_message_system()
    {
        static bool init = false;
        if (!init)
        {
            ll_init_apr();
            start_messaging_system("notafile", 13036, 1, 0, 0, false, "notasharedsecret", NULL, false, 5.f, 100.f);
            init = true;
        }
    }

    // ---- exception counting
    struct ExceptionCounts
    {
        S32 ran_off_end = 0;
        S32 wrote_past = 0;
    };
    ExceptionCounts g_exceptions;
    void exception_counter(LLMessageSystem*, void*, EMessageException e)
    {
        if (e == MX_RAN_OFF_END_OF_PACKET) g_exceptions.ran_off_end++;
        if (e == MX_WROTE_PAST_BUFFER_SIZE) g_exceptions.wrote_past++;
    }
    void install_exception_counter()
    {
        gMessageSystem->setExceptionFunc(MX_RAN_OFF_END_OF_PACKET, exception_counter, NULL);
        gMessageSystem->setExceptionFunc(MX_WROTE_PAST_BUFFER_SIZE, exception_counter, NULL);
    }

    //------------------------------------------------------------------
    // the corpus
    //------------------------------------------------------------------
    struct VarDump { std::string name; S32 type; S32 size; };
    struct BlockDump { std::string name; S32 type; S32 number; S32 total_size; std::vector<VarDump> vars; };
    struct MessageDump
    {
        std::string name; U32 number; S32 frequency; S32 trust; S32 encoding; S32 deprecation; S32 total_size;
        std::vector<BlockDump> blocks;
    };

    struct VarInput
    {
        std::vector<U8> data;   // what add* received (post-conversion bytes)
        U8 as_string;           // added with addString (data holds the string without its NUL)
    };
    struct BuildCase
    {
        std::string message;
        U8 variant;
        U8 offset;
        std::vector<S32> repeats;                   // per template block
        std::vector<std::vector<VarInput>> inputs;  // per block: repeats * vars
        std::vector<U8> packet;
        S32 send_total;
        U8 full;                                    // isMessageFull(NULL)
        std::vector<U8> coded;                      // empty when not coded
    };

    struct VarRead
    {
        std::vector<U8> raw;    // getBinaryData of getSize bytes
        std::vector<U8> typed;  // the typed getter's result
        U8 stated;              // 1: the stated behaviour, not the oracle
    };
    struct ReadCase
    {
        std::string message;    // "" when the number is not registered
        std::string kind;       // full, cut, count255, size_max, unknown, short
        std::vector<U8> packet;
        U8 valid;               // validateMessage
        U8 read;                // readMessage
        S32 ran_off_end;        // exception callbacks
        S32 message_size;
        std::vector<S32> counts;                    // per template block
        std::vector<std::vector<VarRead>> vars;     // per block: counts * vars
    };

    struct ExpandCase
    {
        std::string kind;
        std::vector<U8> input;
        S32 result;             // zeroCodeExpand's return
        S32 wrote_past;         // exception callbacks
        std::vector<U8> output;
    };

    enum DpKind
    {
        DP_STRING = 0, DP_BINARY, DP_BINARY_FIXED, DP_U8, DP_U16, DP_S16, DP_U32, DP_S32, DP_F32,
        DP_COLOR4, DP_COLOR4U, DP_VECTOR2, DP_VECTOR3, DP_VECTOR4, DP_UUID, DP_FIXED
    };
    struct DpOp
    {
        U8 kind;
        std::string name;
        std::vector<U8> value;  // the C++ value's bytes (strings without NUL; DP_FIXED: f32 value, u8 signed, u8 int bits, u8 frac bits)
        S32 size;               // DP_BINARY_FIXED and the unpack size of DP_BINARY's destination
    };
    struct DpOpResult
    {
        U8 ok;
        S32 cursor;
        std::vector<U8> value;  // unpack: the result's bytes (DP_BINARY: 4-byte size then bytes)
        U8 stated;
    };
    struct DpCase
    {
        std::string kind;       // binary, ascii, ascii_names, file
        std::vector<DpOp> ops;
        S32 buffer_size;        // the packer's buffer size for packing
        std::vector<U8> packed;
        std::vector<DpOpResult> pack_results;
        S32 measured;           // write-disabled cursor after the ops (buffer variants)
        S32 unpack_size;        // the buffer size handed to the unpacker
        std::vector<DpOpResult> unpack_results;
    };

    struct Corpus
    {
        F32 version = 0.f;
        std::vector<MessageDump> dump;
        std::vector<BuildCase> builds;
        std::vector<ReadCase> reads;
        std::vector<ExpandCase> expands;
        std::vector<DpCase> packers;
    };

    void write_corpus(const Corpus& c, Writer& w)
    {
        w.u32(FIXTURE_MAGIC);
        w.u32(FIXTURE_VERSION);
        w.f32(c.version);
        w.u32((U32)c.dump.size());
        for (const MessageDump& m : c.dump)
        {
            w.str(m.name); w.u32(m.number); w.i32(m.frequency); w.i32(m.trust); w.i32(m.encoding); w.i32(m.deprecation); w.i32(m.total_size);
            w.u32((U32)m.blocks.size());
            for (const BlockDump& b : m.blocks)
            {
                w.str(b.name); w.i32(b.type); w.i32(b.number); w.i32(b.total_size);
                w.u32((U32)b.vars.size());
                for (const VarDump& v : b.vars) { w.str(v.name); w.i32(v.type); w.i32(v.size); }
            }
        }
        w.u32((U32)c.builds.size());
        for (const BuildCase& b : c.builds)
        {
            w.str(b.message); w.u8(b.variant); w.u8(b.offset);
            w.u32((U32)b.repeats.size());
            for (size_t i = 0; i < b.repeats.size(); ++i)
            {
                w.i32(b.repeats[i]);
                w.u32((U32)b.inputs[i].size());
                for (const VarInput& v : b.inputs[i]) { w.bytes(v.data); w.u8(v.as_string); }
            }
            w.bytes(b.packet); w.i32(b.send_total); w.u8(b.full); w.bytes(b.coded);
        }
        w.u32((U32)c.reads.size());
        for (const ReadCase& r : c.reads)
        {
            w.str(r.message); w.str(r.kind); w.bytes(r.packet); w.u8(r.valid); w.u8(r.read); w.i32(r.ran_off_end); w.i32(r.message_size);
            w.u32((U32)r.counts.size());
            for (size_t i = 0; i < r.counts.size(); ++i)
            {
                w.i32(r.counts[i]);
                w.u32((U32)r.vars[i].size());
                for (const VarRead& v : r.vars[i]) { w.bytes(v.raw); w.bytes(v.typed); w.u8(v.stated); }
            }
        }
        w.u32((U32)c.expands.size());
        for (const ExpandCase& e : c.expands)
        {
            w.str(e.kind); w.bytes(e.input); w.i32(e.result); w.i32(e.wrote_past); w.bytes(e.output);
        }
        w.u32((U32)c.packers.size());
        for (const DpCase& d : c.packers)
        {
            w.str(d.kind);
            w.u32((U32)d.ops.size());
            for (const DpOp& op : d.ops) { w.u8(op.kind); w.str(op.name); w.bytes(op.value); w.i32(op.size); }
            w.i32(d.buffer_size); w.bytes(d.packed);
            w.u32((U32)d.pack_results.size());
            for (const DpOpResult& r : d.pack_results) { w.u8(r.ok); w.i32(r.cursor); w.bytes(r.value); w.u8(r.stated); }
            w.i32(d.measured); w.i32(d.unpack_size);
            w.u32((U32)d.unpack_results.size());
            for (const DpOpResult& r : d.unpack_results) { w.u8(r.ok); w.i32(r.cursor); w.bytes(r.value); w.u8(r.stated); }
        }
    }

    bool read_corpus(Reader& r, Corpus& c)
    {
        if (r.u32() != FIXTURE_MAGIC || r.u32() != FIXTURE_VERSION) return false;
        c.version = r.f32();
        U32 n = r.u32();
        for (U32 i = 0; i < n && r.ok; ++i)
        {
            MessageDump m;
            m.name = r.str(); m.number = r.u32(); m.frequency = r.i32(); m.trust = r.i32(); m.encoding = r.i32(); m.deprecation = r.i32(); m.total_size = r.i32();
            U32 nb = r.u32();
            for (U32 j = 0; j < nb && r.ok; ++j)
            {
                BlockDump b;
                b.name = r.str(); b.type = r.i32(); b.number = r.i32(); b.total_size = r.i32();
                U32 nv = r.u32();
                for (U32 k = 0; k < nv && r.ok; ++k) { VarDump v; v.name = r.str(); v.type = r.i32(); v.size = r.i32(); b.vars.push_back(v); }
                m.blocks.push_back(b);
            }
            c.dump.push_back(m);
        }
        n = r.u32();
        for (U32 i = 0; i < n && r.ok; ++i)
        {
            BuildCase b;
            b.message = r.str(); b.variant = r.u8(); b.offset = r.u8();
            U32 nb = r.u32();
            for (U32 j = 0; j < nb && r.ok; ++j)
            {
                b.repeats.push_back(r.i32());
                U32 nv = r.u32();
                std::vector<VarInput> inputs;
                for (U32 k = 0; k < nv && r.ok; ++k) { VarInput v; v.data = r.bytes(); v.as_string = r.u8(); inputs.push_back(v); }
                b.inputs.push_back(inputs);
            }
            b.packet = r.bytes(); b.send_total = r.i32(); b.full = r.u8(); b.coded = r.bytes();
            c.builds.push_back(b);
        }
        n = r.u32();
        for (U32 i = 0; i < n && r.ok; ++i)
        {
            ReadCase rc;
            rc.message = r.str(); rc.kind = r.str(); rc.packet = r.bytes(); rc.valid = r.u8(); rc.read = r.u8(); rc.ran_off_end = r.i32(); rc.message_size = r.i32();
            U32 nb = r.u32();
            for (U32 j = 0; j < nb && r.ok; ++j)
            {
                rc.counts.push_back(r.i32());
                U32 nv = r.u32();
                std::vector<VarRead> vars;
                for (U32 k = 0; k < nv && r.ok; ++k) { VarRead v; v.raw = r.bytes(); v.typed = r.bytes(); v.stated = r.u8(); vars.push_back(v); }
                rc.vars.push_back(vars);
            }
            c.reads.push_back(rc);
        }
        n = r.u32();
        for (U32 i = 0; i < n && r.ok; ++i)
        {
            ExpandCase e;
            e.kind = r.str(); e.input = r.bytes(); e.result = r.i32(); e.wrote_past = r.i32(); e.output = r.bytes();
            c.expands.push_back(e);
        }
        n = r.u32();
        for (U32 i = 0; i < n && r.ok; ++i)
        {
            DpCase d;
            d.kind = r.str();
            U32 no = r.u32();
            for (U32 j = 0; j < no && r.ok; ++j) { DpOp op; op.kind = r.u8(); op.name = r.str(); op.value = r.bytes(); op.size = r.i32(); d.ops.push_back(op); }
            d.buffer_size = r.i32(); d.packed = r.bytes();
            U32 np = r.u32();
            for (U32 j = 0; j < np && r.ok; ++j) { DpOpResult x; x.ok = r.u8(); x.cursor = r.i32(); x.value = r.bytes(); x.stated = r.u8(); d.pack_results.push_back(x); }
            d.measured = r.i32(); d.unpack_size = r.i32();
            U32 nu = r.u32();
            for (U32 j = 0; j < nu && r.ok; ++j) { DpOpResult x; x.ok = r.u8(); x.cursor = r.i32(); x.value = r.bytes(); x.stated = r.u8(); d.unpack_results.push_back(x); }
            c.packers.push_back(d);
        }
        return r.ok;
    }

    //------------------------------------------------------------------
    // the generator: deterministic values for every variable
    //------------------------------------------------------------------
    // The size classes: variable block repeats, variable field lengths
    // (Variable 1 / Variable 2), the data offset, and whether floats carry
    // NaN (the typed getters zero them).
    struct Variant { S32 repeats; S32 len1; S32 len2; U8 offset; bool nan; bool clamp; };
    const Variant VARIANTS[4] = {
        { 0, 0, 0, 0, false, false },
        { 1, 1, 7, 0, false, false },
        { 3, 40, 300, 3, false, false },
        { 255, 255, 1500, 255, true, true },
    };
    const S32 PACKET_BUDGET[4] = { 7000, 7000, 7000, 2800 };

    S32 fixed_size_of_repeat(const LLMessageBlock* b, const Variant& v)
    {
        S32 n = 0;
        for (LLMessageBlock::message_variable_map_t::const_iterator it = b->mMemberVariables.begin(); it != b->mMemberVariables.end(); ++it)
        {
            const LLMessageVariable* var = *it;
            if (var->getType() == MVT_VARIABLE)
            {
                n += var->getSize() + (var->getSize() == 1 ? v.len1 : v.len2);
            }
            else
            {
                n += var->getSize();
            }
        }
        return n;
    }

    // Repeats of a block: as many as the variant asks for, within an equal
    // share (`budget` bytes) of what the 8 KB packet buffer leaves after
    // the message's fixed blocks, so every packet fits.
    S32 repeats_for(const LLMessageBlock* b, const Variant& v, S32 budget)
    {
        switch (b->mType)
        {
        case MBT_SINGLE: return 1;
        case MBT_MULTIPLE: return b->mNumber;
        default:
        {
            S32 per = llmax(1, fixed_size_of_repeat(b, v) + 1);
            S32 n = llmin(v.repeats, budget / per);
            return llmax(v.repeats == 0 ? 0 : 1, n);
        }
        }
    }

    // Adds one variable through its typed adder and returns what the
    // builder stores (the bytes after the adder's conversion).
    VarInput add_variable(LLTemplateMessageBuilder& builder, const LLMessageVariable* var, const Variant& v, Rng& rng, bool first_variable_field)
    {
        VarInput in;
        in.as_string = 0;
        const char* name = var->getName();
        bool zero = (rng.next() % 4) == 0;
        switch (var->getType())
        {
        case MVT_U8: { U8 x = zero ? 0 : rng.byte(); builder.addU8(name, x); in.data.assign(&x, &x + 1); break; }
        case MVT_S8: { S8 x = zero ? 0 : (S8)rng.byte(); builder.addS8(name, x); in.data.assign((U8*)&x, (U8*)&x + 1); break; }
        case MVT_BOOL: { bool x = !zero; builder.addBOOL(name, x); U8 b = x; in.data.assign(&b, &b + 1); break; }
        case MVT_U16: { U16 x = zero ? 0 : (U16)rng.next(); builder.addU16(name, x); in.data.assign((U8*)&x, (U8*)&x + 2); break; }
        case MVT_S16: { S16 x = zero ? 0 : (S16)rng.next(); builder.addS16(name, x); in.data.assign((U8*)&x, (U8*)&x + 2); break; }
        case MVT_IP_PORT: { U16 x = zero ? 0 : (U16)rng.next(); builder.addIPPort(name, x); U16 n = htons(x); in.data.assign((U8*)&n, (U8*)&n + 2); break; }
        case MVT_U32: { U32 x = zero ? 0 : rng.next(); builder.addU32(name, x); in.data.assign((U8*)&x, (U8*)&x + 4); break; }
        case MVT_S32: { S32 x = zero ? 0 : (S32)rng.next(); builder.addS32(name, x); in.data.assign((U8*)&x, (U8*)&x + 4); break; }
        case MVT_IP_ADDR: { U32 x = zero ? 0 : rng.next(); builder.addIPAddr(name, x); in.data.assign((U8*)&x, (U8*)&x + 4); break; }
        case MVT_U64: { U64 x = zero ? 0 : ((U64)rng.next() << 32) | rng.next(); builder.addU64(name, x); in.data.assign((U8*)&x, (U8*)&x + 8); break; }
        case MVT_S64: { U64 x = zero ? 0 : ((U64)rng.next() << 32) | rng.next(); builder.addBinaryData(name, &x, 8); in.data.assign((U8*)&x, (U8*)&x + 8); break; }
        case MVT_F32:
        {
            F32 x = zero ? 0.f : (F32)((S32)rng.next()) / 1000.f;
            if (v.nan && (rng.next() % 3) == 0) x = std::numeric_limits<F32>::quiet_NaN();
            builder.addF32(name, x); in.data.assign((U8*)&x, (U8*)&x + 4); break;
        }
        case MVT_F64:
        {
            F64 x = zero ? 0.0 : (F64)((S32)rng.next()) / 1000.0;
            if (v.nan && (rng.next() % 3) == 0) x = std::numeric_limits<F64>::infinity();
            builder.addF64(name, x); in.data.assign((U8*)&x, (U8*)&x + 8); break;
        }
        case MVT_LLVector3:
        {
            LLVector3 x(zero ? 0.f : (F32)((S32)rng.next()) / 1000.f, (F32)((S32)rng.next()) / 1000.f, (F32)((S32)rng.next()) / 1000.f);
            if (v.nan && (rng.next() % 3) == 0) x.mV[1] = std::numeric_limits<F32>::quiet_NaN();
            builder.addVector3(name, x); in.data.assign((U8*)x.mV, (U8*)x.mV + 12); break;
        }
        case MVT_LLVector4:
        {
            LLVector4 x(zero ? 0.f : (F32)((S32)rng.next()) / 1000.f, (F32)((S32)rng.next()) / 1000.f, (F32)((S32)rng.next()) / 1000.f, (F32)((S32)rng.next()) / 1000.f);
            if (v.nan && (rng.next() % 3) == 0) x.mV[3] = -std::numeric_limits<F32>::infinity();
            builder.addVector4(name, x); in.data.assign((U8*)x.mV, (U8*)x.mV + 16); break;
        }
        case MVT_LLVector3d:
        {
            LLVector3d x(zero ? 0.0 : (F64)((S32)rng.next()) / 1000.0, (F64)((S32)rng.next()) / 1000.0, (F64)((S32)rng.next()) / 1000.0);
            if (v.nan && (rng.next() % 3) == 0) x.mdV[0] = std::numeric_limits<F64>::quiet_NaN();
            builder.addVector3d(name, x); in.data.assign((U8*)x.mdV, (U8*)x.mdV + 24); break;
        }
        case MVT_LLQuaternion:
        {
            // components rather than an angle and axis: sqrtf and division
            // only, so the input is the same on every platform
            LLQuaternion q((F32)((S32)rng.next() % 2000) / 1000.f, (F32)((S32)rng.next() % 2000) / 1000.f,
                           (F32)((S32)rng.next() % 2000) / 1000.f, zero ? 1.f : (F32)((S32)rng.next() % 2000) / 1000.f);
            if (zero) q.loadIdentity();
            q.normalize();
            LLVector3 packed = q.packToVector3();
            if (v.nan && (rng.next() % 5) == 0)
            {
                // a packed vector the reader rejects: the getter loads identity
                packed.mV[0] = std::numeric_limits<F32>::quiet_NaN();
                builder.addVector3(name, packed);
            }
            else
            {
                builder.addQuat(name, q);
            }
            in.data.assign((U8*)packed.mV, (U8*)packed.mV + 12); break;
        }
        case MVT_LLUUID:
        {
            LLUUID x;
            if (!zero) { for (S32 i = 0; i < 16; ++i) x.mData[i] = (i % 5 == 0) ? 0 : rng.byte(); }
            builder.addUUID(name, x); in.data.assign(x.mData, x.mData + 16); break;
        }
        case MVT_VARIABLE:
        {
            S32 len = var->getSize() == 1 ? v.len1 : v.len2;
            if (v.clamp && first_variable_field && var->getSize() == 1) len = 300;
            bool as_string = len > 0 && (rng.next() % 2) == 0;
            std::vector<U8> data(len);
            for (S32 i = 0; i < len; ++i)
            {
                U8 c = (U8)('a' + (rng.next() % 26));
                if ((rng.next() % 7) == 0) c = 0;
                if (as_string && c == 0) c = ' ';
                data[i] = c;
            }
            if (as_string)
            {
                // addString(const char*) stores strlen + 1
                std::string s(data.begin(), data.end());
                builder.addString(name, s.c_str());
                in.as_string = 1;
                in.data = data;
                in.data.push_back(0);
            }
            else
            {
                std::vector<U8> copy = data; // the C++ writes a 0 into the caller's buffer when clamping
                builder.addBinaryData(name, copy.empty() ? NULL : copy.data(), (S32)copy.size());
                in.data = data;
            }
            break;
        }
        case MVT_FIXED:
        default:
        {
            std::vector<U8> data(var->getSize());
            for (size_t i = 0; i < data.size(); ++i) data[i] = (zero || (rng.next() % 3) == 0) ? 0 : rng.byte();
            builder.addBinaryData(name, data.empty() ? NULL : data.data(), (S32)data.size());
            in.data = data;
            break;
        }
        }
        return in;
    }

    // Replays recorded inputs (the parity run).
    void add_recorded(LLTemplateMessageBuilder& builder, const LLMessageVariable* var, const VarInput& in)
    {
        if (in.as_string)
        {
            std::string s(in.data.begin(), in.data.end() - 1);
            builder.addString(var->getName(), s.c_str());
        }
        else
        {
            std::vector<U8> copy = in.data;
            builder.addBinaryData(var->getName(), copy.empty() ? NULL : copy.data(), (S32)copy.size());
        }
    }

    // The template walk the reader performs, kept here to know where each
    // field lies in a packet (truncation points) and which variable-length
    // fields a short packet makes the C++ read past.
    struct FieldPos { S32 block; S32 repeat; S32 var; S32 nvars; S32 size_field; S32 data_pos; S32 data_len; };
    std::vector<FieldPos> walk(const LLMessageTemplate* t, const std::vector<U8>& packet, S32 receive_size, std::vector<S32>* counts_out)
    {
        std::vector<FieldPos> fields;
        S32 pos = LL_PACKET_ID_SIZE + (S32)t->mFrequency + packet[PHL_OFFSET];
        S32 bi = 0;
        for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit, ++bi)
        {
            const LLMessageBlock* b = *bit;
            U8 repeat = 0;
            if (b->mType == MBT_SINGLE) repeat = 1;
            else if (b->mType == MBT_MULTIPLE) repeat = (U8)b->mNumber;
            else if (pos < receive_size) { repeat = packet[pos]; pos++; }
            if (counts_out) counts_out->push_back(repeat);
            for (S32 r = 0; r < repeat; ++r)
            {
                S32 vi = 0;
                for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit, ++vi)
                {
                    const LLMessageVariable* var = *vit;
                    FieldPos f = { bi, r, vi, (S32)b->mMemberVariables.size(), 0, 0, 0 };
                    if (var->getType() == MVT_VARIABLE)
                    {
                        S32 ds = var->getSize();
                        f.size_field = pos;
                        S32 tsize = 0;
                        if (pos + ds <= receive_size)
                        {
                            if (ds == 1) tsize = packet[pos];
                            else if (ds == 2) { U16 s; memcpy(&s, &packet[pos], 2); tsize = s; }
                            else if (ds == 4) { U32 s; memcpy(&s, &packet[pos], 4); tsize = (S32)s; }
                        }
                        pos += ds;
                        f.data_pos = pos;
                        f.data_len = tsize;
                        pos += tsize;
                    }
                    else
                    {
                        f.size_field = -1;
                        f.data_pos = pos;
                        f.data_len = var->getSize();
                        pos += var->getSize();
                    }
                    fields.push_back(f);
                }
            }
        }
        return fields;
    }

    std::vector<U8> typed_get(LLTemplateMessageReader& reader, const char* block, const LLMessageVariable* var, S32 blocknum)
    {
        std::vector<U8> out;
        const char* name = var->getName();
        switch (var->getType())
        {
        case MVT_U8: { U8 x; reader.getU8(block, name, x, blocknum); out.assign(&x, &x + 1); break; }
        case MVT_S8: { S8 x; reader.getS8(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 1); break; }
        case MVT_BOOL: { bool x; reader.getBOOL(block, name, x, blocknum); U8 b = x; out.assign(&b, &b + 1); break; }
        case MVT_U16: { U16 x; reader.getU16(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 2); break; }
        case MVT_S16: { S16 x; reader.getS16(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 2); break; }
        case MVT_IP_PORT: { U16 x; reader.getIPPort(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 2); break; }
        case MVT_U32: { U32 x; reader.getU32(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 4); break; }
        case MVT_S32: { S32 x; reader.getS32(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 4); break; }
        case MVT_IP_ADDR: { U32 x; reader.getIPAddr(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 4); break; }
        case MVT_U64: { U64 x; reader.getU64(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 8); break; }
        case MVT_F32: { F32 x; reader.getF32(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 4); break; }
        case MVT_F64: { F64 x; reader.getF64(block, name, x, blocknum); out.assign((U8*)&x, (U8*)&x + 8); break; }
        case MVT_LLVector3: { LLVector3 x; reader.getVector3(block, name, x, blocknum); out.assign((U8*)x.mV, (U8*)x.mV + 12); break; }
        case MVT_LLVector4: { LLVector4 x; reader.getVector4(block, name, x, blocknum); out.assign((U8*)x.mV, (U8*)x.mV + 16); break; }
        case MVT_LLVector3d: { LLVector3d x; reader.getVector3d(block, name, x, blocknum); out.assign((U8*)x.mdV, (U8*)x.mdV + 24); break; }
        case MVT_LLQuaternion: { LLQuaternion x; reader.getQuat(block, name, x, blocknum); out.assign((U8*)x.mQ, (U8*)x.mQ + 16); break; }
        case MVT_LLUUID: { LLUUID x; reader.getUUID(block, name, x, blocknum); out.assign(x.mData, x.mData + 16); break; }
        case MVT_VARIABLE:
        {
            std::string s;
            reader.getString(block, name, s, blocknum);
            out.assign(s.begin(), s.end());
            break;
        }
        default:
        {
            S32 size = reader.getSize(block, blocknum, name);
            out.resize(size > 0 ? size : 0);
            if (size > 0) reader.getBinaryData(block, name, out.data(), size, blocknum);
            break;
        }
        }
        return out;
    }

    // Reads one packet through the real reader and records everything the
    // getters say. `overrun` marks fields (block, repeat, var) whose data
    // the C++ copied from beyond the packet; those get the stated values.
    ReadCase read_packet(const LLMessageTemplate* t, const std::string& kind, const std::vector<U8>& packet)
    {
        ReadCase rc;
        rc.kind = kind;
        rc.packet = packet;
        rc.message = "";
        TemplateSet& ts = templates();
        LLTemplateMessageReader reader(ts.number_map);
        // the oracle's receive buffer: MAX_BUFFER_SIZE plus room for the
        // largest variable field, so an over-read stays in bounds
        std::vector<U8> buffer(MAX_BUFFER_SIZE + 70000, 0xA5);
        memcpy(buffer.data(), packet.data(), packet.size());
        g_exceptions = ExceptionCounts();
        rc.valid = reader.validateMessage(buffer.data(), (S32)packet.size(), LLHost(), false) ? 1 : 0;
        rc.read = 0;
        rc.ran_off_end = 0;
        rc.message_size = reader.getMessageSize();
        if (!rc.valid)
        {
            return rc;
        }
        rc.message = reader.getMessageName();
        rc.read = reader.readMessage(buffer.data(), LLHost()) ? 1 : 0;
        rc.ran_off_end = g_exceptions.ran_off_end;
        // where the fields lie, by our own walk of the template
        std::vector<S32> walk_counts;
        std::vector<FieldPos> fields = walk(t, packet, (S32)packet.size(), &walk_counts);
        std::map<std::pair<S32, std::pair<S32, S32>>, const FieldPos*> by_slot;
        for (const FieldPos& f : fields) by_slot[std::make_pair(f.block, std::make_pair(f.repeat, f.var))] = &f;
        S32 bi = 0;
        for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit, ++bi)
        {
            const LLMessageBlock* b = *bit;
            S32 count = reader.getNumberOfBlocks(b->mName);
            rc.counts.push_back(count);
            std::vector<VarRead> vars;
            for (S32 r = 0; r < count; ++r)
            {
                S32 vi = 0;
                for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit, ++vi)
                {
                    const LLMessageVariable* var = *vit;
                    VarRead vr;
                    vr.stated = 0;
                    const FieldPos* f = by_slot[std::make_pair(bi, std::make_pair(r, vi))];
                    bool overrun = f && f->size_field >= 0 && f->data_len > 0 && f->data_pos + f->data_len > (S32)packet.size();
                    if (overrun)
                    {
                        // the stated behaviour: empty, and one more ran-off-end
                        vr.stated = 1;
                        rc.ran_off_end++;
                    }
                    else
                    {
                        S32 size = reader.getSize(b->mName, r, var->getName());
                        vr.raw.resize(size > 0 ? size : 0);
                        if (size > 0) reader.getBinaryData(b->mName, var->getName(), vr.raw.data(), size, r);
                        if (var->getType() != MVT_VARIABLE && var->getType() != MVT_FIXED)
                        {
                            vr.typed = typed_get(reader, b->mName, var, r);
                        }
                    }
                    vars.push_back(vr);
                }
            }
            rc.vars.push_back(vars);
        }
        return rc;
    }

    // Checks the oracle against the fields our walk predicts (every value
    // that is not an overrun must be the packet's bytes or zeros).
    void self_check(const LLMessageTemplate* t, const ReadCase& rc)
    {
        if (!rc.valid || !rc.read) return;
        std::vector<S32> walk_counts;
        std::vector<FieldPos> fields = walk(t, rc.packet, (S32)rc.packet.size(), &walk_counts);
        ensure_equals(rc.message + " walk block count", walk_counts.size(), rc.counts.size());
        for (size_t i = 0; i < walk_counts.size(); ++i)
        {
            ensure_equals(rc.message + " walk repeats", walk_counts[i], rc.counts[i]);
        }
        for (const FieldPos& f : fields)
        {
            const VarRead& vr = rc.vars[f.block][f.repeat * f.nvars + f.var];
            if (vr.stated) continue;
            if (f.data_pos + f.data_len <= (S32)rc.packet.size())
            {
                ensure_equals(rc.message + " walk len", (S32)vr.raw.size(), f.data_len);
                ensure(rc.message + " walk bytes", f.data_len == 0 || memcmp(vr.raw.data(), &rc.packet[f.data_pos], f.data_len) == 0);
            }
            else
            {
                // a fixed field past the end: zeros of its size, or an
                // absent size field: empty
                if (f.size_field >= 0)
                {
                    ensure_equals(rc.message + " walk absent size", (S32)vr.raw.size(), 0);
                }
                else
                {
                    ensure_equals(rc.message + " walk zero len", (S32)vr.raw.size(), f.data_len);
                    for (U8 c : vr.raw) ensure_equals(rc.message + " walk zero", (S32)c, 0);
                }
            }
        }
    }

    // Set while the corpus is being recorded from the C++ oracle.
    bool g_recording = false;

    //------------------------------------------------------------------
    // the data packer scripts
    //------------------------------------------------------------------
    DpOp op_of(U8 kind, const char* name, const void* p, size_t n, S32 size = 0)
    {
        DpOp op; op.kind = kind; op.name = name; op.value.assign((const U8*)p, (const U8*)p + n); op.size = size; return op;
    }
    DpOp op_str(const char* name, const std::string& s) { return op_of(DP_STRING, name, s.data(), s.size()); }
    DpOp op_bin(const char* name, const std::vector<U8>& v) { return op_of(DP_BINARY, name, v.data(), v.size(), (S32)v.size()); }
    DpOp op_binf(const char* name, const std::vector<U8>& v) { return op_of(DP_BINARY_FIXED, name, v.data(), v.size(), (S32)v.size()); }
    DpOp op_u8(const char* name, U8 v) { return op_of(DP_U8, name, &v, 1); }
    DpOp op_u16(const char* name, U16 v) { return op_of(DP_U16, name, &v, 2); }
    DpOp op_s16(const char* name, S16 v) { return op_of(DP_S16, name, &v, 2); }
    DpOp op_u32(const char* name, U32 v) { return op_of(DP_U32, name, &v, 4); }
    DpOp op_s32(const char* name, S32 v) { return op_of(DP_S32, name, &v, 4); }
    DpOp op_f32(const char* name, F32 v) { return op_of(DP_F32, name, &v, 4); }
    DpOp op_c4(const char* name, const LLColor4& v) { return op_of(DP_COLOR4, name, v.mV, 16); }
    DpOp op_c4u(const char* name, const LLColor4U& v) { return op_of(DP_COLOR4U, name, v.mV, 4); }
    DpOp op_v2(const char* name, const LLVector2& v) { return op_of(DP_VECTOR2, name, v.mV, 8); }
    DpOp op_v3(const char* name, const LLVector3& v) { return op_of(DP_VECTOR3, name, v.mV, 12); }
    DpOp op_v4(const char* name, const LLVector4& v) { return op_of(DP_VECTOR4, name, v.mV, 16); }
    DpOp op_uuid(const char* name, const LLUUID& v) { return op_of(DP_UUID, name, v.mData, 16); }
    DpOp op_fixed(const char* name, F32 v, bool is_signed, U8 int_bits, U8 frac_bits)
    {
        U8 b[7]; memcpy(b, &v, 4); b[4] = is_signed; b[5] = int_bits; b[6] = frac_bits;
        return op_of(DP_FIXED, name, b, 7);
    }

    std::vector<U8> rand_bytes(Rng& rng, S32 n, bool zeros)
    {
        std::vector<U8> v(n);
        for (S32 i = 0; i < n; ++i) v[i] = (zeros && (rng.next() % 3) == 0) ? 0 : rng.byte();
        return v;
    }

    std::vector<std::vector<DpOp>> packer_scripts()
    {
        std::vector<std::vector<DpOp>> scripts;
        // A: one of each, plain values
        {
            std::vector<DpOp> s;
            s.push_back(op_str("linden_lab_str", "SecondLife is virtual World"));
            std::vector<U8> b = { 'S', 'e', 'c', 'o', 'n', 'd', 0 };
            s.push_back(op_bin("linden_lab_bd", b));
            std::vector<U8> f = { 'F', 'i', 'x', 'e', 'd', ' ', 'D', 'a', 't', 'a', 0 };
            s.push_back(op_binf("linden_lab_bdf", f));
            s.push_back(op_u8("linden_lab_u8", 'C'));
            s.push_back(op_u16("linden_lab_u16", 0xFFFF));
            s.push_back(op_s16("linden_lab_s16", -12345));
            s.push_back(op_u32("linden_lab_u32", 0xFFFFFFFF));
            s.push_back(op_s32("linden_lab_s32", -94967295));
            s.push_back(op_f32("linden_lab_f32", 4354355.44f));
            s.push_back(op_c4("linden_lab_col4", LLColor4(3.3f, 0, 4.4f, 5.5f)));
            s.push_back(op_c4u("linden_lab_col4u", LLColor4U(3, 128, 24, 33)));
            s.push_back(op_v2("linden_lab_vec2", LLVector2(333.33f, 444.44f)));
            s.push_back(op_v3("linden_lab_vec3", LLVector3(333.33f, 444.44f, 555.55f)));
            s.push_back(op_v4("linden_lab_vec4", LLVector4(333.33f, 444.44f, 555.55f, 666.66f)));
            LLUUID id("3c115e51-04f4-523c-9fa6-98aff1034730");
            s.push_back(op_uuid("linden_lab_uuid", id));
            s.push_back(op_fixed("fixed_8_8", 44.44f, false, 8, 8));
            s.push_back(op_fixed("fixed_14_16", 12344.443232f, false, 14, 16));
            s.push_back(op_fixed("fixed_8_23", 44.4456789f, false, 8, 23));
            s.push_back(op_fixed("fixed_s_7_8", -100.25f, true, 7, 8));
            s.push_back(op_fixed("fixed_s_3_4", 3.0f, true, 3, 4));
            scripts.push_back(s);
        }
        // B: extremes and non-finite floats
        {
            std::vector<DpOp> s;
            s.push_back(op_u8("u8_0", 0));
            s.push_back(op_u8("u8_255", 255));
            s.push_back(op_u16("u16_0", 0));
            s.push_back(op_s16("s16_min", -32768));
            s.push_back(op_s16("s16_max", 32767));
            s.push_back(op_u32("u32_0", 0));
            s.push_back(op_s32("s32_min", (S32)0x80000000));
            s.push_back(op_s32("s32_max", 0x7FFFFFFF));
            s.push_back(op_f32("f32_0", 0.f));
            s.push_back(op_f32("f32_neg0", -0.f));
            s.push_back(op_f32("f32_tiny", 1.17549435e-38f));
            s.push_back(op_f32("f32_big", 3.4028235e38f));
            s.push_back(op_f32("f32_inf", std::numeric_limits<F32>::infinity()));
            s.push_back(op_f32("f32_ninf", -std::numeric_limits<F32>::infinity()));
            s.push_back(op_f32("f32_nan", std::numeric_limits<F32>::quiet_NaN()));
            s.push_back(op_f32("f32_third", 1.f / 3.f));
            s.push_back(op_f32("f32_half", 0.5f));
            s.push_back(op_f32("f32_round", 2.0000005f));
            s.push_back(op_v3("v3_mixed", LLVector3(-1e-7f, 1e7f, 0.000001f)));
            s.push_back(op_c4u("c4u_edges", LLColor4U(0, 255, 1, 254)));
            s.push_back(op_str("empty", ""));
            s.push_back(op_str("spaces", "a b\tc  d"));
            s.push_back(op_bin("bin_empty", std::vector<U8>()));
            s.push_back(op_binf("binf_empty", std::vector<U8>()));
            s.push_back(op_uuid("uuid_null", LLUUID::null));
            s.push_back(op_fixed("fixed_clamp_hi", 1e9f, false, 8, 8));
            s.push_back(op_fixed("fixed_clamp_lo", -1e9f, true, 8, 7));
            s.push_back(op_fixed("fixed_31", 12.75f, false, 15, 16));
            scripts.push_back(s);
        }
        // C: long data (past the text packers' 512-byte line)
        {
            Rng rng(77);
            std::vector<DpOp> s;
            std::string long_str(300, 'x');
            for (size_t i = 0; i < long_str.size(); ++i) long_str[i] = (char)('a' + (i % 26));
            s.push_back(op_str("long_str", long_str));
            s.push_back(op_bin("long_bin", rand_bytes(rng, 200, true)));
            s.push_back(op_binf("long_binf", rand_bytes(rng, 120, true)));
            s.push_back(op_bin("mid_bin", rand_bytes(rng, 160, true)));
            s.push_back(op_u32("after", 42));
            scripts.push_back(s);
        }
        // D: random values
        {
            Rng rng(1234);
            std::vector<DpOp> s;
            for (S32 i = 0; i < 40; ++i)
            {
                U32 k = rng.next() % 16;
                switch (k)
                {
                case DP_STRING: { std::vector<U8> b = rand_bytes(rng, rng.next() % 20, false); for (U8& c : b) c = (U8)('!' + (c % 90)); s.push_back(op_str("s", std::string(b.begin(), b.end()))); break; }
                case DP_BINARY: s.push_back(op_bin("b", rand_bytes(rng, rng.next() % 24, true))); break;
                case DP_BINARY_FIXED: s.push_back(op_binf("bf", rand_bytes(rng, 1 + rng.next() % 24, true))); break;
                case DP_U8: s.push_back(op_u8("u8", rng.byte())); break;
                case DP_U16: s.push_back(op_u16("u16", (U16)rng.next())); break;
                case DP_S16: s.push_back(op_s16("s16", (S16)rng.next())); break;
                case DP_U32: s.push_back(op_u32("u32", rng.next())); break;
                case DP_S32: s.push_back(op_s32("s32", (S32)rng.next())); break;
                case DP_F32: s.push_back(op_f32("f32", (F32)((S32)rng.next()) / 977.f)); break;
                case DP_COLOR4: s.push_back(op_c4("c4", LLColor4((F32)(rng.next() % 1000) / 1000.f, (F32)(rng.next() % 1000) / 1000.f, (F32)(rng.next() % 1000) / 1000.f, (F32)(rng.next() % 1000) / 1000.f))); break;
                case DP_COLOR4U: s.push_back(op_c4u("c4u", LLColor4U(rng.byte(), rng.byte(), rng.byte(), rng.byte()))); break;
                case DP_VECTOR2: s.push_back(op_v2("v2", LLVector2((F32)((S32)rng.next()) / 977.f, (F32)((S32)rng.next()) / 977.f))); break;
                case DP_VECTOR3: s.push_back(op_v3("v3", LLVector3((F32)((S32)rng.next()) / 977.f, (F32)((S32)rng.next()) / 977.f, (F32)((S32)rng.next()) / 977.f))); break;
                case DP_VECTOR4: s.push_back(op_v4("v4", LLVector4((F32)((S32)rng.next()) / 977.f, (F32)((S32)rng.next()) / 977.f, (F32)((S32)rng.next()) / 977.f, (F32)((S32)rng.next()) / 977.f))); break;
                case DP_UUID: { LLUUID id; for (S32 j = 0; j < 16; ++j) id.mData[j] = rng.byte(); s.push_back(op_uuid("id", id)); break; }
                default: s.push_back(op_fixed("fx", (F32)((S32)rng.next()) / 1e6f, (rng.next() & 1) != 0, 1 + rng.next() % 8, 1 + rng.next() % 16)); break;
                }
            }
            scripts.push_back(s);
        }
        return scripts;
    }

    // Scripts whose last value line is cut to its name alone (the text
    // packers with names, and the file form): the unpack of the last op
    // reads an empty value and must leave the caller's value as it was.
    // The pre-port C++ left U8, U16, S16, fixed-point and fixed binary
    // reads uninitialised there (a local sscanf never assigned); the
    // stated behaviour is the sentinel, unchanged.
    std::vector<std::vector<DpOp>> unchanged_scripts()
    {
        std::vector<std::vector<DpOp>> scripts;
        std::vector<DpOp> targets;
        targets.push_back(op_fixed("fx_u_8_8", 44.44f, false, 8, 8));
        targets.push_back(op_fixed("fx_s_7_8", -100.25f, true, 7, 8));
        targets.push_back(op_fixed("fx_s_3_4", 3.0f, true, 3, 4));
        targets.push_back(op_fixed("fx_u_15_16", 12.75f, false, 15, 16));
        targets.push_back(op_u8("u8", 200));
        targets.push_back(op_u16("u16", 50000));
        targets.push_back(op_s16("s16", -12345));
        targets.push_back(op_u32("u32", 0xDEADBEEF));
        targets.push_back(op_s32("s32", -94967295));
        targets.push_back(op_f32("f32", 1.5f));
        targets.push_back(op_v3("v3", LLVector3(1.f, 2.f, 3.f)));
        targets.push_back(op_c4u("c4u", LLColor4U(1, 2, 3, 4)));
        std::vector<U8> three = { 1, 2, 3 };
        targets.push_back(op_bin("bin", three));
        targets.push_back(op_binf("binf", three));
        targets.push_back(op_str("str", "gone"));
        for (const DpOp& t : targets)
        {
            std::vector<DpOp> s;
            s.push_back(op_u8("lead", 7));
            s.push_back(t);
            scripts.push_back(s);
        }
        return scripts;
    }

    // run_packer_case: cut the text after the last op's name (and tab)
    const S32 CUT_AFTER_LAST_NAME = std::numeric_limits<S32>::min();

    // Runs one pack op on a packer, returning the C++ result and cursor.
    DpOpResult run_pack(LLDataPacker& dp, const DpOp& op, S32 (*cursor)(LLDataPacker&))
    {
        DpOpResult r;
        r.stated = 0;
        const char* name = op.name.c_str();
        bool ok = false;
        switch (op.kind)
        {
        case DP_STRING: ok = dp.packString(std::string(op.value.begin(), op.value.end()), name); break;
        case DP_BINARY: ok = dp.packBinaryData(op.value.data(), (S32)op.value.size(), name); break;
        case DP_BINARY_FIXED: ok = dp.packBinaryDataFixed(op.value.data(), (S32)op.value.size(), name); break;
        case DP_U8: ok = dp.packU8(op.value[0], name); break;
        case DP_U16: { U16 v; memcpy(&v, op.value.data(), 2); ok = dp.packU16(v, name); break; }
        case DP_S16: { S16 v; memcpy(&v, op.value.data(), 2); ok = dp.packS16(v, name); break; }
        case DP_U32: { U32 v; memcpy(&v, op.value.data(), 4); ok = dp.packU32(v, name); break; }
        case DP_S32: { S32 v; memcpy(&v, op.value.data(), 4); ok = dp.packS32(v, name); break; }
        case DP_F32: { F32 v; memcpy(&v, op.value.data(), 4); ok = dp.packF32(v, name); break; }
        case DP_COLOR4: { LLColor4 v; memcpy(v.mV, op.value.data(), 16); ok = dp.packColor4(v, name); break; }
        case DP_COLOR4U: { LLColor4U v; memcpy(v.mV, op.value.data(), 4); ok = dp.packColor4U(v, name); break; }
        case DP_VECTOR2: { LLVector2 v; memcpy(v.mV, op.value.data(), 8); ok = dp.packVector2(v, name); break; }
        case DP_VECTOR3: { LLVector3 v; memcpy(v.mV, op.value.data(), 12); ok = dp.packVector3(v, name); break; }
        case DP_VECTOR4: { LLVector4 v; memcpy(v.mV, op.value.data(), 16); ok = dp.packVector4(v, name); break; }
        case DP_UUID: { LLUUID v; memcpy(v.mData, op.value.data(), 16); ok = dp.packUUID(v, name); break; }
        case DP_FIXED: { F32 v; memcpy(&v, op.value.data(), 4); ok = dp.packFixed(v, name, op.value[4] != 0, op.value[5], op.value[6]); break; }
        }
        r.ok = ok ? 1 : 0;
        r.cursor = cursor(dp);
        return r;
    }

    bool text_has_int(const std::vector<U8>& text)
    {
        size_t i = 0;
        while (i < text.size() && (text[i] == ' ' || (text[i] >= '\t' && text[i] <= '\r'))) ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        return i < text.size() && text[i] >= '0' && text[i] <= '9';
    }

    // Runs one unpack op. Outputs start as a sentinel so "unchanged" is
    // observable. While recording, the oracle's nondeterministic results
    // are replaced by the stated behaviour; the product's results are
    // compared as they are.
    DpOpResult run_unpack(LLDataPacker& dp, const DpOp& op, S32 (*cursor)(LLDataPacker&), bool ascii)
    {
        DpOpResult r;
        r.stated = 0;
        const char* name = op.name.c_str();
        bool ok = false;
        switch (op.kind)
        {
        case DP_STRING: { std::string s; ok = dp.unpackString(s, name); r.value.assign(s.begin(), s.end()); break; }
        case DP_BINARY:
        {
            std::vector<U8> buf(70000, 0xA5);
            S32 size = (S32)0xA5A5A5A5;
            ok = dp.unpackBinaryData(buf.data(), (S32)buf.size(), size, name);
            r.value.assign((U8*)&size, (U8*)&size + 4);
            S32 n = ok ? llclamp(size, 0, (S32)buf.size()) : 0;
            if (ascii && ok)
            {
                // the text form holds at most 167 bytes of data; beyond
                // them the C++ read from past its line buffer
                n = llmin(n, 167);
                if (size > 167) r.stated = 1;
            }
            r.value.insert(r.value.end(), buf.begin(), buf.begin() + n);
            break;
        }
        case DP_BINARY_FIXED:
        {
            std::vector<U8> buf(op.size, 0xA5);
            ok = dp.unpackBinaryDataFixed(buf.data(), op.size, name);
            r.value = buf;
            if (ascii && ok && op.size > 170) { r.stated = 1; r.value.resize(170); }
            break;
        }
        case DP_U8: { U8 v = 0xA5; ok = dp.unpackU8(v, name); r.value.assign(&v, &v + 1); break; }
        case DP_U16: { U16 v = 0xA5A5; ok = dp.unpackU16(v, name); r.value.assign((U8*)&v, (U8*)&v + 2); break; }
        case DP_S16: { S16 v = (S16)0xA5A5; ok = dp.unpackS16(v, name); r.value.assign((U8*)&v, (U8*)&v + 2); break; }
        case DP_U32: { U32 v = 0xA5A5A5A5; ok = dp.unpackU32(v, name); r.value.assign((U8*)&v, (U8*)&v + 4); break; }
        case DP_S32: { S32 v = (S32)0xA5A5A5A5; ok = dp.unpackS32(v, name); r.value.assign((U8*)&v, (U8*)&v + 4); break; }
        case DP_F32: { U32 s = 0xA5A5A5A5; F32 v; memcpy(&v, &s, 4); ok = dp.unpackF32(v, name); r.value.assign((U8*)&v, (U8*)&v + 4); break; }
        case DP_COLOR4: { LLColor4 v; memset(v.mV, 0xA5, 16); ok = dp.unpackColor4(v, name); r.value.assign((U8*)v.mV, (U8*)v.mV + 16); break; }
        case DP_COLOR4U: { LLColor4U v; memset(v.mV, 0xA5, 4); ok = dp.unpackColor4U(v, name); r.value.assign(v.mV, v.mV + 4); break; }
        case DP_VECTOR2: { LLVector2 v; memset(v.mV, 0xA5, 8); ok = dp.unpackVector2(v, name); r.value.assign((U8*)v.mV, (U8*)v.mV + 8); break; }
        case DP_VECTOR3: { LLVector3 v; memset(v.mV, 0xA5, 12); ok = dp.unpackVector3(v, name); r.value.assign((U8*)v.mV, (U8*)v.mV + 12); break; }
        case DP_VECTOR4: { LLVector4 v; memset(v.mV, 0xA5, 16); ok = dp.unpackVector4(v, name); r.value.assign((U8*)v.mV, (U8*)v.mV + 16); break; }
        case DP_UUID: { LLUUID v; memset(v.mData, 0xA5, 16); ok = dp.unpackUUID(v, name); r.value.assign(v.mData, v.mData + 16); break; }
        case DP_FIXED:
        {
            U32 s = 0xA5A5A5A5; F32 v; memcpy(&v, &s, 4);
            ok = dp.unpackFixed(v, name, op.value[4] != 0, op.value[5], op.value[6]);
            if (!ok) { memcpy(&v, &s, 4); r.stated = 1; }
            r.value.assign((U8*)&v, (U8*)&v + 4);
            break;
        }
        }
        r.ok = ok ? 1 : 0;
        r.cursor = cursor(dp);
        if (ascii && ok && g_recording)
        {
            // (b) a NaN read from text: the C runtimes give different
            // payloads; the stated value is the canonical quiet NaN of the
            // sign written
            S32 nfloats = 0;
            switch (op.kind)
            {
            case DP_F32: nfloats = 1; break;
            case DP_VECTOR2: nfloats = 2; break;
            case DP_VECTOR3: nfloats = 3; break;
            case DP_COLOR4: case DP_VECTOR4: nfloats = 4; break;
            default: break;
            }
            for (S32 i = 0; i < nfloats; ++i)
            {
                F32 f; memcpy(&f, &r.value[i * 4], 4);
                U32 bits; memcpy(&bits, &f, 4);
                if (f != f && bits != 0xA5A5A5A5)
                {
                    U32 canon = (bits & 0x80000000u) | 0x7FC00000u;
                    memcpy(&r.value[i * 4], &canon, 4);
                    r.stated = 1;
                }
            }
        }
        return r;
    }

    S32 cursor_binary(LLDataPacker& dp) { return static_cast<LLDataPackerBinaryBuffer&>(dp).getCurrentSize(); }
    S32 cursor_ascii(LLDataPacker& dp) { return static_cast<LLDataPackerAsciiBuffer&>(dp).getCurrentSize(); }
    S32 cursor_none(LLDataPacker&) { return 0; }

    std::vector<U8> run_unpack_sentinel(const DpOp& op)
    {
        size_t n = 0;
        switch (op.kind)
        {
        case DP_STRING: return std::vector<U8>();
        case DP_BINARY: return std::vector<U8>(4, 0xA5);
        case DP_BINARY_FIXED: n = llmin((S32)170, op.size); break;
        case DP_U8: n = 1; break;
        case DP_U16: case DP_S16: n = 2; break;
        case DP_U32: case DP_S32: case DP_F32: case DP_COLOR4U: case DP_FIXED: n = 4; break;
        case DP_VECTOR2: n = 8; break;
        case DP_VECTOR3: n = 12; break;
        case DP_COLOR4: case DP_VECTOR4: case DP_UUID: n = 16; break;
        }
        return std::vector<U8>(n, 0xA5);
    }

    // Flags the conversions of a value line the C++ left uninitialised
    // (an integer with no digits, a missing colour component or hex pair):
    // the stated behaviour is the value unchanged.
    void flag_uninitialised_value(const DpOp& op, DpOpResult& r, const std::vector<U8>& line)
    {
        if (!r.ok) return;
        switch (op.kind)
        {
        case DP_U8: case DP_U16: case DP_S16: case DP_FIXED:
            if (!text_has_int(line))
            {
                r.stated = 1;
                r.value.assign(op.kind == DP_U8 ? 1 : (op.kind == DP_FIXED ? 4 : 2), 0xA5);
            }
            break;
        case DP_COLOR4U:
        {
            size_t pos = 0; S32 got = 0;
            for (S32 c = 0; c < 4; ++c)
            {
                while (pos < line.size() && (line[pos] == ' ' || (line[pos] >= '\t' && line[pos] <= '\r'))) ++pos;
                size_t start = pos;
                if (pos < line.size() && (line[pos] == '+' || line[pos] == '-')) ++pos;
                if (!(pos < line.size() && line[pos] >= '0' && line[pos] <= '9')) { pos = start; break; }
                while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') ++pos;
                got++;
            }
            if (got < 4)
            {
                r.stated = 1;
                for (S32 c = got; c < 4; ++c) r.value[c] = 0xA5;
            }
            break;
        }
        case DP_BINARY: case DP_BINARY_FIXED:
        {
            S32 start_at = op.kind == DP_BINARY ? 11 : 0;
            S32 n = op.kind == DP_BINARY ? (S32)r.value.size() - 4 : (S32)r.value.size();
            for (S32 b = 0; b < n; ++b)
            {
                size_t p = start_at + 3 * b;
                while (p < line.size() && (line[p] == ' ' || (line[p] >= '\t' && line[p] <= '\r'))) ++p;
                bool hex = p < line.size() && isxdigit(line[p]);
                if (!hex)
                {
                    r.stated = 1;
                    r.value[(op.kind == DP_BINARY ? 4 : 0) + b] = 0xA5;
                }
            }
            break;
        }
        default: break;
        }
    }

    DpCase run_packer_case(const std::string& kind, const std::vector<DpOp>& ops, S32 buffer_size, S32 unpack_delta)
    {
        DpCase d;
        d.kind = kind;
        d.ops = ops;
        d.buffer_size = buffer_size;
        d.measured = 0;
        if (kind == "binary")
        {
            std::vector<U8> buf(buffer_size + 16, 0xA5);
            LLDataPackerBinaryBuffer dp(buf.data(), buffer_size);
            for (const DpOp& op : ops) d.pack_results.push_back(run_pack(dp, op, cursor_binary));
            S32 n = llclamp(dp.getCurrentSize(), 0, buffer_size);
            d.packed.assign(buf.begin(), buf.begin() + n);
            LLDataPackerBinaryBuffer measure;
            for (const DpOp& op : ops) run_pack(measure, op, cursor_binary);
            d.measured = measure.getCurrentSize();
            d.unpack_size = llmax(0, n + unpack_delta);
            std::vector<U8> in(buf.begin(), buf.begin() + n);
            in.resize(llmax((S32)in.size(), d.unpack_size) + 16, 0xA5);
            LLDataPackerBinaryBuffer up(in.data(), d.unpack_size);
            for (const DpOp& op : ops) d.unpack_results.push_back(run_unpack(up, op, cursor_binary, false));
        }
        else if (kind == "ascii" || kind == "ascii_names")
        {
            bool names = kind == "ascii_names";
            std::vector<char> buf(buffer_size + 16, (char)0xA5);
            LLDataPackerAsciiBuffer dp(buf.data(), buffer_size);
            dp.setIncludeNames(names);
            for (const DpOp& op : ops) d.pack_results.push_back(run_pack(dp, op, cursor_ascii));
            S32 n = llclamp(dp.getCurrentSize(), 0, buffer_size);
            d.packed.assign(buf.begin(), buf.begin() + n);
            LLDataPackerAsciiBuffer measure;
            measure.setIncludeNames(names);
            measure.assignBuffer(NULL, buffer_size);
            measure.reset();
            for (const DpOp& op : ops) run_pack(measure, op, cursor_ascii);
            d.measured = measure.getCurrentSize();
            if (unpack_delta == CUT_AFTER_LAST_NAME)
            {
                // the text ends after the last tab: unpack_size - 1 is
                // the NUL
                S32 t = n - 1;
                while (t >= 0 && d.packed[t] != '\t') --t;
                unpack_delta = t + 2 - n;
            }
            d.unpack_size = llmax(1, n + unpack_delta);
            std::vector<char> in(buf.begin(), buf.begin() + n);
            in.resize(llmax((S32)in.size(), d.unpack_size) + 16, 0);
            if (unpack_delta < 0 && d.unpack_size - 1 < (S32)in.size()) in[d.unpack_size - 1] = 0;
            LLDataPackerAsciiBuffer up(in.data(), d.unpack_size);
            up.setIncludeNames(names);
            // the C string the unpacker reads: up to the first NUL
            std::vector<U8> text;
            for (S32 i = 0; i < (S32)in.size() && in[i] != 0; ++i) text.push_back((U8)in[i]);
            S32 pos = 0;
            for (size_t k = 0; k < ops.size(); ++k)
            {
                const DpOp& op = ops[k];
                if (pos >= (S32)text.size())
                {
                    // past the end of the text: the C++ read on through
                    // whatever followed its buffer. The stated behaviour
                    // is an empty line: no value with names (the keyword
                    // does not match), an empty value without.
                    DpOpResult r;
                    r.stated = 1;
                    r.ok = names ? 0 : 1;
                    pos += 1;
                    r.cursor = pos + 1;
                    if (!names && op.kind == DP_STRING) r.value.clear();
                    else r.value = run_unpack_sentinel(op);
                    d.unpack_results.push_back(r);
                    continue;
                }
                // the value line this op reads, by the C++ rules
                S32 end = pos;
                while (end < (S32)text.size() && end - pos < 511 && text[end] != '\n') ++end;
                std::vector<U8> line(text.begin() + pos, text.begin() + end);
                pos = end + 1;
                std::vector<U8> value = line;
                bool expect_ok = true;
                if (names)
                {
                    size_t t = 0;
                    while (t < line.size() && (line[t] == ' ' || (line[t] >= '\t' && line[t] <= '\r'))) ++t;
                    size_t ks = t;
                    while (t < line.size() && !(line[t] == ' ' || (line[t] >= '\t' && line[t] <= '\r')) && t - ks < 511) ++t;
                    std::string keyword(line.begin() + ks, line.begin() + t);
                    while (t < line.size() && (line[t] == ' ' || (line[t] >= '\t' && line[t] <= '\r'))) ++t;
                    value.assign(line.begin() + t, line.end());
                    if (value.size() > 511) value.resize(511);
                    expect_ok = keyword == op.name;
                }
                DpOpResult r = run_unpack(up, op, cursor_ascii, true);
                ensure_equals(kind + " op " + op.name + " cursor as simulated", r.cursor, pos + 1);
                ensure_equals(kind + " op " + op.name + " result as simulated", (bool)r.ok, expect_ok);
                if (g_recording) flag_uninitialised_value(op, r, value); // the product is compared as it is
                d.unpack_results.push_back(r);
            }
        }
        else
        {
            std::ostringstream out;
            LLDataPackerAsciiFile dp(out, 2);
            for (const DpOp& op : ops) d.pack_results.push_back(run_pack(dp, op, cursor_none));
            // The stated text: the C++ never wrote the indent it was given
            // (writeIndentedName filled a string past its size; the
            // indent came out empty), the product writes it. The reader
            // skips it, so the C++ reads the stated text back.
            std::string text = out.str();
            if (g_recording)
            {
                std::string written = text;
                text.clear();
                size_t start = 0;
                while (start < written.size())
                {
                    size_t end = written.find('\n', start);
                    if (end == std::string::npos) end = written.size() - 1;
                    text += "\t\t";
                    text.append(written, start, end - start + 1);
                    start = end + 1;
                }
            }
            d.packed.assign(text.begin(), text.end());
            if (unpack_delta == CUT_AFTER_LAST_NAME)
            {
                size_t t = text.rfind('\t');
                unpack_delta = (S32)(t == std::string::npos ? 0 : t + 1) - (S32)text.size();
            }
            d.unpack_size = llmax(0, (S32)text.size() + unpack_delta);
            text.resize(d.unpack_size);
            std::istringstream in(text);
            LLDataPackerAsciiFile up(in, 2);
            // getline(buffer, 512): a line longer than 511 bytes is cut
            // there and fails the stream, so every later read fails too
            size_t pos = 0;
            bool failed = false;
            for (size_t k = 0; k < ops.size(); ++k)
            {
                const DpOp& op = ops[k];
                std::vector<U8> value;
                bool expect_ok = false;
                if (!failed && pos <= text.size())
                {
                    size_t end = pos;
                    while (end < text.size() && end - pos < 511 && text[end] != '\n') ++end;
                    std::vector<U8> line(text.begin() + pos, text.begin() + end);
                    if (end - pos == 511 && end < text.size() && text[end] != '\n')
                    {
                        failed = true;
                        pos = end;
                    }
                    else
                    {
                        pos = end + 1;
                    }
                    size_t t = 0;
                    while (t < line.size() && (line[t] == ' ' || (line[t] >= '\t' && line[t] <= '\r'))) ++t;
                    size_t ks = t;
                    while (t < line.size() && !(line[t] == ' ' || (line[t] >= '\t' && line[t] <= '\r')) && t - ks < 511) ++t;
                    std::string keyword(line.begin() + ks, line.begin() + t);
                    while (t < line.size() && (line[t] == ' ' || (line[t] >= '\t' && line[t] <= '\r'))) ++t;
                    value.assign(line.begin() + t, line.end());
                    if (value.size() > 511) value.resize(511);
                    expect_ok = !keyword.empty() && keyword == op.name;
                }
                DpOpResult r = run_unpack(up, op, cursor_none, true);
                ensure_equals(kind + " op " + op.name + " result as simulated", (bool)r.ok, expect_ok);
                if (g_recording) flag_uninitialised_value(op, r, value); // the product is compared as it is
                d.unpack_results.push_back(r);
            }
        }
        return d;
    }

    //------------------------------------------------------------------
    // recording
    //------------------------------------------------------------------
    std::vector<U8> truncate_at(const std::vector<U8>& p, size_t n)
    {
        return std::vector<U8>(p.begin(), p.begin() + llmin(n, p.size()));
    }

    void record(Corpus& c)
    {
        TemplateSet& ts = templates();
        c.version = ts.version;
        for (LLMessageTemplate* t : ts.messages)
        {
            MessageDump m;
            m.name = t->mName; m.number = t->mMessageNumber; m.frequency = t->mFrequency; m.trust = t->mTrust;
            m.encoding = t->mEncoding; m.deprecation = t->mDeprecation; m.total_size = t->mTotalSize;
            for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit)
            {
                const LLMessageBlock* b = *bit;
                BlockDump bd; bd.name = b->mName; bd.type = b->mType; bd.number = b->mNumber; bd.total_size = b->mTotalSize;
                for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit)
                {
                    const LLMessageVariable* v = *vit;
                    VarDump vd; vd.name = v->getName(); vd.type = v->getType(); vd.size = v->getSize();
                    bd.vars.push_back(vd);
                }
                m.blocks.push_back(bd);
            }
            c.dump.push_back(m);
        }

        LLTemplateMessageBuilder builder(ts.name_map);
        U32 mi = 0;
        for (LLMessageTemplate* t : ts.messages)
        {
            for (U8 variant = 0; variant < 4; ++variant)
            {
                const Variant& v = VARIANTS[variant];
                BuildCase bc;
                bc.message = t->mName;
                bc.variant = variant;
                bc.offset = v.offset;
                builder.newMessage(t->mName);
                S32 bi = 0;
                bool first_variable_field = true;
                S32 variable_blocks = 0;
                S32 fixed_bytes = LL_PACKET_ID_SIZE + 4 + v.offset;
                for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit)
                {
                    const LLMessageBlock* b = *bit;
                    if (b->mType == MBT_VARIABLE) variable_blocks++;
                    else fixed_bytes += (b->mType == MBT_MULTIPLE ? b->mNumber : 1) * fixed_size_of_repeat(b, v);
                }
                S32 budget = llmax(0, PACKET_BUDGET[variant] - fixed_bytes) / llmax(1, variable_blocks);
                for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit, ++bi)
                {
                    const LLMessageBlock* b = *bit;
                    S32 repeats = repeats_for(b, v, budget);
                    bc.repeats.push_back(repeats);
                    std::vector<VarInput> inputs;
                    for (S32 r = 0; r < repeats; ++r)
                    {
                        builder.nextBlock(b->mName);
                        S32 vi = 0;
                        for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit, ++vi)
                        {
                            const LLMessageVariable* var = *vit;
                            Rng rng(seed_of(mi, variant, bi, r, vi));
                            bool fvf = first_variable_field && var->getType() == MVT_VARIABLE;
                            inputs.push_back(add_variable(builder, var, v, rng, fvf));
                            if (fvf) first_variable_field = false;
                        }
                    }
                    bc.inputs.push_back(inputs);
                }
                std::vector<U8> buffer(MAX_BUFFER_SIZE, 0);
                U32 size = builder.buildMessage(buffer.data(), MAX_BUFFER_SIZE, v.offset);
                bc.packet.assign(buffer.begin(), buffer.begin() + size);
                bc.send_total = builder.getMessageSize();
                bc.full = builder.isMessageFull(NULL) ? 1 : 0;
                U8* ptr = buffer.data();
                U32 len = size;
                builder.compressMessage(ptr, len);
                if (ptr != buffer.data())
                {
                    bc.coded.assign(ptr, ptr + len);
                }
                c.builds.push_back(bc);
            }
            ++mi;
        }

        // reads: every packet complete, variant 1 and 2 cut at several
        // points, variant 1 with corrupted counts and sizes
        for (const BuildCase& bc : c.builds)
        {
            LLMessageTemplate* t = ts.name_map[LLMessageStringTable::getInstance()->getString(bc.message.c_str())];
            ReadCase full = read_packet(t, "full", bc.packet);
            self_check(t, full);
            c.reads.push_back(full);
            if (bc.variant == 1 || bc.variant == 2)
            {
                std::vector<FieldPos> fields = walk(t, bc.packet, (S32)bc.packet.size(), NULL);
                std::vector<size_t> cuts;
                cuts.push_back(LL_PACKET_ID_SIZE + 1);
                cuts.push_back(LL_PACKET_ID_SIZE + (S32)t->mFrequency + bc.offset);
                cuts.push_back(bc.packet.size() / 2);
                cuts.push_back(bc.packet.size() - 1);
                for (const FieldPos& f : fields)
                {
                    if (f.size_field >= 0 && f.data_len > 1) { cuts.push_back(f.data_pos + f.data_len / 2); cuts.push_back(f.size_field + 1); }
                }
                if (fields.size() > 2) cuts.push_back(fields[fields.size() / 2].data_pos);
                std::sort(cuts.begin(), cuts.end());
                cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
                size_t limit = bc.variant == 1 ? 12 : 6;
                for (size_t i = 0; i < cuts.size() && i < limit; ++i)
                {
                    size_t n = cuts[i];
                    if (n >= bc.packet.size() || n < 1) continue;
                    ReadCase cut = read_packet(t, "cut", truncate_at(bc.packet, n));
                    self_check(t, cut);
                    c.reads.push_back(cut);
                }
            }
            if (bc.variant == 1)
            {
                std::vector<FieldPos> fields = walk(t, bc.packet, (S32)bc.packet.size(), NULL);
                // every variable block's count byte set to 255
                S32 pos = LL_PACKET_ID_SIZE + (S32)t->mFrequency + bc.offset;
                bool has_variable_block = false;
                std::vector<U8> p = bc.packet;
                S32 bi = 0;
                for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit, ++bi)
                {
                    const LLMessageBlock* b = *bit;
                    if (b->mType == MBT_VARIABLE && pos < (S32)p.size()) { p[pos] = 255; has_variable_block = true; break; }
                    // skip this block's data
                    for (const FieldPos& f : fields)
                    {
                        if (f.block == bi) pos = llmax(pos, f.data_pos + f.data_len);
                    }
                    if (b->mType == MBT_VARIABLE) pos++;
                }
                if (has_variable_block)
                {
                    ReadCase rc = read_packet(t, "count255", p);
                    self_check(t, rc);
                    c.reads.push_back(rc);
                }
                // every size field set to its maximum
                bool has_size = false;
                p = bc.packet;
                for (const FieldPos& f : fields)
                {
                    if (f.size_field < 0) continue;
                    const LLMessageBlock* b = NULL;
                    S32 k = 0;
                    for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit, ++k) if (k == f.block) b = *bit;
                    const LLMessageVariable* var = NULL;
                    k = 0;
                    for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit, ++k) if (k == f.var) var = *vit;
                    for (S32 i = 0; i < var->getSize() && f.size_field + i < (S32)p.size(); ++i) p[f.size_field + i] = 0xFF;
                    has_size = true;
                    break; // the first one is enough: everything after it is past the end
                }
                if (has_size)
                {
                    ReadCase rc = read_packet(t, "size_max", p);
                    self_check(t, rc);
                    c.reads.push_back(rc);
                }
            }
        }
        // unknown numbers and short packets
        {
            LLMessageTemplate* any = ts.messages[0];
            std::vector<U8> p(12, 0);
            p[6] = 200; c.reads.push_back(read_packet(any, "unknown", p));
            p[6] = 255; p[7] = 200; c.reads.push_back(read_packet(any, "unknown", p));
            p[6] = 255; p[7] = 255; p[8] = 0x99; p[9] = 0x99; c.reads.push_back(read_packet(any, "unknown", p));
            for (size_t n = 1; n <= 9; ++n)
            {
                std::vector<U8> s(n, 0);
                if (n >= 7) s[6] = 255;
                if (n >= 8) s[7] = 255;
                c.reads.push_back(read_packet(any, "short", s));
            }
        }

        // zero-code expansion: every coded packet, cut and corrupted forms,
        // and synthetic overflows
        {
            S32 count = 0;
            for (const BuildCase& bc : c.builds)
            {
                if (bc.coded.empty()) continue;
                std::vector<std::pair<std::string, std::vector<U8>>> inputs;
                inputs.push_back(std::make_pair("coded", bc.coded));
                if (bc.variant == 1 || bc.variant == 2)
                {
                    inputs.push_back(std::make_pair("coded_cut", truncate_at(bc.coded, bc.coded.size() - 1)));
                    inputs.push_back(std::make_pair("coded_cut", truncate_at(bc.coded, bc.coded.size() / 2)));
                    std::vector<U8> zeroed = bc.coded;
                    // the first count byte after a zero becomes a wrap
                    for (size_t i = LL_PACKET_ID_SIZE; i + 1 < zeroed.size(); ++i)
                    {
                        if (zeroed[i] == 0) { zeroed[i + 1] = 0; break; }
                    }
                    inputs.push_back(std::make_pair("coded_wrap", zeroed));
                }
                for (const std::pair<std::string, std::vector<U8>>& in : inputs)
                {
                    ExpandCase e;
                    e.kind = in.first;
                    e.input = in.second;
                    std::vector<U8> buf(MAX_BUFFER_SIZE, 0);
                    memcpy(buf.data(), e.input.data(), e.input.size());
                    U8* ptr = buf.data();
                    S32 size = (S32)e.input.size();
                    g_exceptions = ExceptionCounts();
                    e.result = gMessageSystem->zeroCodeExpand(&ptr, &size);
                    e.wrote_past = g_exceptions.wrote_past;
                    e.output.assign(ptr, ptr + size);
                    if (in.first == "coded")
                    {
                        ensure(bc.message + " expands back", e.output == bc.packet);
                    }
                    c.expands.push_back(e);
                    ++count;
                }
            }
            // synthetic: many 255-runs (past the buffer), many wraps
            {
                ExpandCase e;
                e.kind = "overflow_runs";
                e.input.assign(LL_PACKET_ID_SIZE, 0);
                e.input[0] = LL_ZERO_CODE_FLAG;
                for (S32 i = 0; i < 40; ++i) { e.input.push_back(0); e.input.push_back(255); }
                std::vector<U8> buf(MAX_BUFFER_SIZE, 0);
                memcpy(buf.data(), e.input.data(), e.input.size());
                U8* ptr = buf.data(); S32 size = (S32)e.input.size();
                g_exceptions = ExceptionCounts();
                e.result = gMessageSystem->zeroCodeExpand(&ptr, &size);
                e.wrote_past = g_exceptions.wrote_past;
                e.output.assign(ptr, ptr + size);
                c.expands.push_back(e);
            }
            {
                ExpandCase e;
                e.kind = "overflow_wraps";
                e.input.assign(LL_PACKET_ID_SIZE, 0);
                e.input[0] = LL_ZERO_CODE_FLAG;
                for (S32 i = 0; i < 40; ++i) e.input.push_back(0);
                std::vector<U8> buf(MAX_BUFFER_SIZE, 0);
                memcpy(buf.data(), e.input.data(), e.input.size());
                U8* ptr = buf.data(); S32 size = (S32)e.input.size();
                g_exceptions = ExceptionCounts();
                e.result = gMessageSystem->zeroCodeExpand(&ptr, &size);
                e.wrote_past = g_exceptions.wrote_past;
                e.output.assign(ptr, ptr + size);
                c.expands.push_back(e);
            }
            {
                ExpandCase e;
                e.kind = "uncoded";
                e.input = c.builds[0].packet;
                std::vector<U8> buf(MAX_BUFFER_SIZE, 0);
                memcpy(buf.data(), e.input.data(), e.input.size());
                U8* ptr = buf.data(); S32 size = (S32)e.input.size();
                g_exceptions = ExceptionCounts();
                e.result = gMessageSystem->zeroCodeExpand(&ptr, &size);
                e.wrote_past = g_exceptions.wrote_past;
                e.output.assign(ptr, ptr + size);
                c.expands.push_back(e);
            }
        }

        // data packers
        {
            std::vector<std::vector<DpOp>> scripts = packer_scripts();
            const char* kinds[4] = { "binary", "ascii", "ascii_names", "file" };
            for (const std::vector<DpOp>& ops : scripts)
            {
                for (const char* kind : kinds)
                {
                    c.packers.push_back(run_packer_case(kind, ops, 8192, 0));
                    if (std::string(kind) != "file")
                    {
                        c.packers.push_back(run_packer_case(kind, ops, 8192, -5));
                        c.packers.push_back(run_packer_case(kind, ops, 64, 0));
                    }
                }
            }
            for (const std::vector<DpOp>& ops : unchanged_scripts())
            {
                c.packers.push_back(run_packer_case("ascii_names", ops, 8192, CUT_AFTER_LAST_NAME));
                c.packers.push_back(run_packer_case("file", ops, 8192, CUT_AFTER_LAST_NAME));
            }
        }
    }

    //------------------------------------------------------------------
    // comparing
    //------------------------------------------------------------------
    struct Mismatches
    {
        std::vector<std::string> lines;
        S32 count = 0;
        void add(const std::string& s) { ++count; if (lines.size() < 40) lines.push_back(s); }
        template <typename T> void eq(const std::string& what, const T& expected, const T& actual)
        {
            if (!(expected == actual)) { std::ostringstream ss; ss << what << ": expected " << expected << " got " << actual; add(ss.str()); }
        }
        void bytes(const std::string& what, const std::vector<U8>& expected, const std::vector<U8>& actual)
        {
            if (expected != actual)
            {
                std::ostringstream ss;
                ss << what << ": expected " << expected.size() << " bytes got " << actual.size();
                size_t n = llmin(expected.size(), actual.size());
                for (size_t i = 0; i < n; ++i) if (expected[i] != actual[i]) { ss << ", first difference at " << i << " (" << (int)expected[i] << " vs " << (int)actual[i] << ")"; break; }
                add(ss.str());
            }
        }
        void report(const std::string& what)
        {
            if (count)
            {
                std::ostringstream ss;
                ss << what << ": " << count << " mismatches";
                for (const std::string& l : lines) ss << "\n  " << l;
                ensure(ss.str(), false);
            }
        }
    };

    Corpus& corpus()
    {
        static Corpus* c = NULL;
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            c = new Corpus;
            ensure_message_system();
            install_exception_counter();
            const char* dump = getenv("VKS_MSG_DUMP");
            if (dump && *dump)
            {
                g_recording = true;
                record(*c);
                Writer w;
                write_corpus(*c, w);
                std::ofstream out(dump, std::ios::binary);
                out.write((const char*)w.b.data(), w.b.size());
                ensure("corpus written", out.good());
                LL_INFOS() << "Recorded " << c->builds.size() << " build, " << c->reads.size() << " read, " << c->expands.size()
                           << " expand and " << c->packers.size() << " packer cases (" << w.b.size() << " bytes) to " << dump << LL_ENDL;
            }
            else
            {
                std::string bytes = read_file(VKS_MSG_FIXTURE);
                ensure("fixture corpus present: " VKS_MSG_FIXTURE, !bytes.empty());
                std::vector<U8> v(bytes.begin(), bytes.end());
                Reader r(v);
                ensure("fixture read completely", read_corpus(r, *c));
            }
        }
        return *c;
    }
}

namespace tut
{
    struct llmessagetemplate_data {};
    typedef test_group<llmessagetemplate_data> group;
    typedef group::object object;
    tut::group llmessagetemplate_group("llmessagetemplate");

    // the template parser: the dump of every message, block and variable
    template<> template<>
    void object::test<1>()
    {
        set_test_name("template parser parity");
        Corpus& c = corpus();
        if (g_recording || getenv("VKS_MSG_BENCH")) return; // the oracle does not have the stated behaviours; a timing run is not a parity run
        TemplateSet& ts = templates();
        Mismatches m;
        m.eq<F32>("version", c.version, ts.version);
        m.eq<size_t>("message count", c.dump.size(), ts.messages.size());
        for (size_t i = 0; i < c.dump.size() && i < ts.messages.size(); ++i)
        {
            const MessageDump& d = c.dump[i];
            const LLMessageTemplate* t = ts.messages[i];
            m.eq<std::string>("name", d.name, t->mName);
            m.eq<U32>(d.name + " number", d.number, t->mMessageNumber);
            m.eq<S32>(d.name + " frequency", d.frequency, t->mFrequency);
            m.eq<S32>(d.name + " trust", d.trust, t->mTrust);
            m.eq<S32>(d.name + " encoding", d.encoding, t->mEncoding);
            m.eq<S32>(d.name + " deprecation", d.deprecation, t->mDeprecation);
            m.eq<S32>(d.name + " total size", d.total_size, t->mTotalSize);
            m.eq<size_t>(d.name + " blocks", d.blocks.size(), t->mMemberBlocks.size());
            size_t bi = 0;
            for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end() && bi < d.blocks.size(); ++bit, ++bi)
            {
                const LLMessageBlock* b = *bit;
                const BlockDump& bd = d.blocks[bi];
                m.eq<std::string>(d.name + " block name", bd.name, b->mName);
                m.eq<S32>(d.name + "." + bd.name + " type", bd.type, b->mType);
                m.eq<S32>(d.name + "." + bd.name + " number", bd.number, b->mNumber);
                m.eq<S32>(d.name + "." + bd.name + " total size", bd.total_size, b->mTotalSize);
                m.eq<size_t>(d.name + "." + bd.name + " variables", bd.vars.size(), b->mMemberVariables.size());
                size_t vi = 0;
                for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end() && vi < bd.vars.size(); ++vit, ++vi)
                {
                    const LLMessageVariable* v = *vit;
                    m.eq<std::string>(d.name + "." + bd.name + " variable name", bd.vars[vi].name, v->getName());
                    m.eq<S32>(d.name + "." + bd.name + "." + bd.vars[vi].name + " type", bd.vars[vi].type, v->getType());
                    m.eq<S32>(d.name + "." + bd.name + "." + bd.vars[vi].name + " size", bd.vars[vi].size, v->getSize());
                }
            }
        }
        m.report("template parser");
    }

    // the builder: every packet and its zero-coded form, byte for byte
    template<> template<>
    void object::test<2>()
    {
        set_test_name("builder parity");
        Corpus& c = corpus();
        if (g_recording || getenv("VKS_MSG_BENCH")) return; // the oracle does not have the stated behaviours; a timing run is not a parity run
        TemplateSet& ts = templates();
        Mismatches m;
        LLTemplateMessageBuilder builder(ts.name_map);
        for (const BuildCase& bc : c.builds)
        {
            std::string tag = bc.message + "/" + std::to_string(bc.variant);
            LLMessageTemplate* t = ts.name_map[LLMessageStringTable::getInstance()->getString(bc.message.c_str())];
            if (!t) { m.add(tag + ": no such message"); continue; }
            builder.newMessage(t->mName);
            S32 bi = 0;
            for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit, ++bi)
            {
                const LLMessageBlock* b = *bit;
                S32 repeats = bc.repeats[bi];
                S32 k = 0;
                for (S32 r = 0; r < repeats; ++r)
                {
                    builder.nextBlock(b->mName);
                    for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit, ++k)
                    {
                        add_recorded(builder, *vit, bc.inputs[bi][k]);
                    }
                }
            }
            std::vector<U8> buffer(MAX_BUFFER_SIZE, 0);
            U32 size = builder.buildMessage(buffer.data(), MAX_BUFFER_SIZE, bc.offset);
            std::vector<U8> packet(buffer.begin(), buffer.begin() + size);
            m.bytes(tag + " packet", bc.packet, packet);
            m.eq<S32>(tag + " send total", bc.send_total, builder.getMessageSize());
            m.eq<S32>(tag + " full", bc.full, builder.isMessageFull(NULL) ? 1 : 0);
            m.eq<S32>(tag + " built", 1, builder.isBuilt() ? 1 : 0);
            U8* ptr = buffer.data();
            U32 len = size;
            builder.compressMessage(ptr, len);
            std::vector<U8> coded;
            if (ptr != buffer.data()) coded.assign(ptr, ptr + len);
            m.bytes(tag + " coded", bc.coded, coded);
        }
        m.report("builder");
    }

    // the reader: every packet, complete, cut and corrupted
    template<> template<>
    void object::test<3>()
    {
        set_test_name("reader parity");
        Corpus& c = corpus();
        if (g_recording || getenv("VKS_MSG_BENCH")) return; // the oracle does not have the stated behaviours; a timing run is not a parity run
        TemplateSet& ts = templates();
        Mismatches m;
        LLTemplateMessageReader reader(ts.number_map);
        for (size_t i = 0; i < c.reads.size(); ++i)
        {
            const ReadCase& rc = c.reads[i];
            std::string tag = rc.message + "/" + rc.kind + "/" + std::to_string(rc.packet.size()) + "#" + std::to_string(i);
            std::vector<U8> buffer(MAX_BUFFER_SIZE, 0xA5);
            memcpy(buffer.data(), rc.packet.data(), rc.packet.size());
            g_exceptions = ExceptionCounts();
            reader.clearMessage();
            bool valid = reader.validateMessage(buffer.data(), (S32)rc.packet.size(), LLHost(), false);
            m.eq<S32>(tag + " valid", rc.valid, valid ? 1 : 0);
            m.eq<S32>(tag + " message size", rc.message_size, reader.getMessageSize());
            if (!valid || !rc.valid) continue;
            m.eq<std::string>(tag + " name", rc.message, reader.getMessageName());
            bool read = reader.readMessage(buffer.data(), LLHost());
            m.eq<S32>(tag + " read", rc.read, read ? 1 : 0);
            m.eq<S32>(tag + " ran off end", rc.ran_off_end, g_exceptions.ran_off_end);
            LLMessageTemplate* tt = ts.name_map[LLMessageStringTable::getInstance()->getString(rc.message.c_str())];
            S32 bi = 0;
            for (LLMessageTemplate::message_block_map_t::const_iterator bit = tt->mMemberBlocks.begin(); bit != tt->mMemberBlocks.end(); ++bit, ++bi)
            {
                const LLMessageBlock* b = *bit;
                S32 count = reader.getNumberOfBlocks(b->mName);
                m.eq<S32>(tag + " " + b->mName + " count", rc.counts[bi], count);
                if (count != rc.counts[bi]) continue;
                S32 k = 0;
                for (S32 r = 0; r < count; ++r)
                {
                    for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit, ++k)
                    {
                        const LLMessageVariable* var = *vit;
                        const VarRead& vr = rc.vars[bi][k];
                        std::string vtag = tag + " " + b->mName + "[" + std::to_string(r) + "]." + var->getName();
                        S32 size = reader.getSize(b->mName, r, var->getName());
                        std::vector<U8> raw(size > 0 ? size : 0);
                        if (size > 0) reader.getBinaryData(b->mName, var->getName(), raw.data(), size, r);
                        m.bytes(vtag + " raw", vr.raw, raw);
                        if (b->mType == MBT_SINGLE)
                        {
                            m.eq<S32>(vtag + " size", (S32)vr.raw.size(), reader.getSize(b->mName, var->getName()));
                        }
                        if (var->getType() != MVT_VARIABLE && var->getType() != MVT_FIXED)
                        {
                            m.bytes(vtag + " typed", vr.typed, typed_get(reader, b->mName, var, r));
                        }
                    }
                }
            }
            reader.clearMessage();
        }
        m.report("reader");
    }

    // zero-code expansion
    template<> template<>
    void object::test<4>()
    {
        set_test_name("zero-code expansion parity");
        Corpus& c = corpus();
        if (g_recording || getenv("VKS_MSG_BENCH")) return; // the oracle does not have the stated behaviours; a timing run is not a parity run
        Mismatches m;
        for (size_t i = 0; i < c.expands.size(); ++i)
        {
            const ExpandCase& e = c.expands[i];
            std::string tag = e.kind + "#" + std::to_string(i);
            std::vector<U8> buf(MAX_BUFFER_SIZE, 0);
            memcpy(buf.data(), e.input.data(), e.input.size());
            U8* ptr = buf.data();
            S32 size = (S32)e.input.size();
            g_exceptions = ExceptionCounts();
            S32 result = gMessageSystem->zeroCodeExpand(&ptr, &size);
            m.eq<S32>(tag + " result", e.result, result);
            m.eq<S32>(tag + " wrote past", e.wrote_past, g_exceptions.wrote_past);
            std::vector<U8> out(ptr, ptr + size);
            m.bytes(tag + " output", e.output, out);
        }
        m.report("zero-code expansion");
    }

    // the data packers
    template<> template<>
    void object::test<5>()
    {
        set_test_name("data packer parity");
        Corpus& c = corpus();
        if (g_recording || getenv("VKS_MSG_BENCH")) return; // the oracle does not have the stated behaviours; a timing run is not a parity run
        Mismatches m;
        for (size_t i = 0; i < c.packers.size(); ++i)
        {
            const DpCase& d = c.packers[i];
            std::string tag = d.kind + "#" + std::to_string(i);
            DpCase actual = run_packer_case(d.kind, d.ops, d.buffer_size, d.unpack_size - (S32)d.packed.size());
            m.bytes(tag + " packed", d.packed, actual.packed);
            m.eq<S32>(tag + " measured", d.measured, actual.measured);
            m.eq<S32>(tag + " unpack size", d.unpack_size, actual.unpack_size);
            for (size_t k = 0; k < d.ops.size(); ++k)
            {
                std::string otag = tag + " op " + std::to_string(k) + " " + d.ops[k].name;
                m.eq<S32>(otag + " pack ok", d.pack_results[k].ok, actual.pack_results[k].ok);
                m.eq<S32>(otag + " pack cursor", d.pack_results[k].cursor, actual.pack_results[k].cursor);
                m.eq<S32>(otag + " unpack ok", d.unpack_results[k].ok, actual.unpack_results[k].ok);
                m.eq<S32>(otag + " unpack cursor", d.unpack_results[k].cursor, actual.unpack_results[k].cursor);
                std::vector<U8> expected_value = d.unpack_results[k].value;
                // Stated behaviour since upstream #5486 (ce139f26e0): an
                // unpackBinaryData that fails before it has read a size
                // reports out_size 0. The corpus was recorded before #5486,
                // when such a failure left the caller's size untouched (the
                // 0xA5 sentinel). Stated results (no call was made) and every
                // other byte are compared as recorded.
                if (d.ops[k].kind == DP_BINARY && !d.unpack_results[k].ok && !d.unpack_results[k].stated && expected_value.size() >= 4
                    && expected_value[0] == 0xA5 && expected_value[1] == 0xA5 && expected_value[2] == 0xA5 && expected_value[3] == 0xA5)
                {
                    std::fill(expected_value.begin(), expected_value.begin() + 4, (U8)0);
                }
                m.bytes(otag + " unpack value", expected_value, actual.unpack_results[k].value);
            }
        }
        m.report("data packers");
    }

    // Representative packets, built with the viewer's own builder (no real
    // session: nothing is captured without logging in, so the payloads are
    // constructed; see doc/rust_port.md). ObjectUpdate: RegionData and four
    // ObjectData repeats of a typical prim (one with hover text and name
    // values, one with two textures), AgentUpdate with a moving agent.
    struct PacketSet
    {
        std::vector<U8> object_update;
        std::vector<U8> object_update_coded;
        std::vector<U8> agent_update;
    };

    void rng_uuid(Rng& rng, LLUUID& id)
    {
        for (S32 i = 0; i < 16; ++i) id.mData[i] = rng.byte();
    }

    // A TextureEntry field as LLPrimitive::packTEField writes it for faces
    // that all share one value, or with face 0 differing.
    void te_field(std::vector<U8>& out, const U8* value, S32 size, const U8* face0 = NULL)
    {
        out.insert(out.end(), value, value + size);
        if (face0)
        {
            out.push_back(0x01); // exception bitmask: face 0
            out.insert(out.end(), face0, face0 + size);
        }
        out.push_back(0);
    }

    std::vector<U8> texture_entry(Rng& rng, bool two_textures)
    {
        std::vector<U8> te;
        LLUUID image; rng_uuid(rng, image);
        LLUUID image0; rng_uuid(rng, image0);
        te_field(te, image.mData, 16, two_textures ? image0.mData : NULL);
        U8 colors[4] = { 0, 0, 0, 0 }; // white, sent as 255 - component
        te_field(te, colors, 4);
        F32 one = 1.f;
        te_field(te, (U8*)&one, 4);
        te_field(te, (U8*)&one, 4);
        S16 zero16 = 0;
        te_field(te, (U8*)&zero16, 2);
        te_field(te, (U8*)&zero16, 2);
        te_field(te, (U8*)&zero16, 2);
        U8 bump = 0, media = 0, glow = 0;
        te_field(te, &bump, 1);
        te_field(te, &media, 1);
        te_field(te, &glow, 1);
        LLUUID material; // null for most prims
        te.insert(te.end(), material.mData, material.mData + 16);
        return te;
    }

    PacketSet build_packets(LLTemplateMessageBuilder& builder)
    {
        PacketSet ps;
        Rng rng(20261020);
        builder.newMessage(_PREHASH_ObjectUpdate);
        builder.nextBlock(_PREHASH_RegionData);
        builder.addU64(_PREHASH_RegionHandle, ((U64)256000 << 32) | 256256);
        builder.addU16(_PREHASH_TimeDilation, 65535);
        for (S32 i = 0; i < 4; ++i)
        {
            builder.nextBlock(_PREHASH_ObjectData);
            builder.addU32(_PREHASH_ID, 1000000 + rng.next() % 100000);
            builder.addU8(_PREHASH_State, 0);
            LLUUID full; rng_uuid(rng, full);
            builder.addUUID(_PREHASH_FullID, full);
            builder.addU32(_PREHASH_CRC, rng.next());
            builder.addU8(_PREHASH_PCode, 9); // PCODE_VOLUME
            builder.addU8(_PREHASH_Material, 3);
            builder.addU8(_PREHASH_ClickAction, 0);
            builder.addVector3(_PREHASH_Scale, LLVector3(0.5f + i * 0.25f, 0.5f, 1.f + i));
            // position, velocity, acceleration, rotation, angular velocity
            F32 od[15] = { 128.5f + i * 2.f, 130.25f, 22.75f, 0, 0, 0, 0, 0, 0, 0, 0, 0.70710677f, 0, 0, 0 };
            if (i == 1) { od[3] = 0.5f; od[4] = -0.25f; }
            builder.addBinaryData(_PREHASH_ObjectData, od, sizeof(od));
            builder.addU32(_PREHASH_ParentID, i == 3 ? 1000000 + 7 : 0);
            builder.addU32(_PREHASH_UpdateFlags, 0x00080000 | (i == 0 ? 0x00000080 : 0));
            builder.addU8(_PREHASH_PathCurve, 16);
            builder.addU8(_PREHASH_ProfileCurve, 1);
            builder.addU16(_PREHASH_PathBegin, 0);
            builder.addU16(_PREHASH_PathEnd, 0);
            builder.addU8(_PREHASH_PathScaleX, 100);
            builder.addU8(_PREHASH_PathScaleY, 100);
            builder.addU8(_PREHASH_PathShearX, 0);
            builder.addU8(_PREHASH_PathShearY, 0);
            builder.addS8(_PREHASH_PathTwist, 0);
            builder.addS8(_PREHASH_PathTwistBegin, 0);
            builder.addS8(_PREHASH_PathRadiusOffset, 0);
            builder.addS8(_PREHASH_PathTaperX, 0);
            builder.addS8(_PREHASH_PathTaperY, 0);
            builder.addU8(_PREHASH_PathRevolutions, 0);
            builder.addS8(_PREHASH_PathSkew, 0);
            builder.addU16(_PREHASH_ProfileBegin, 0);
            builder.addU16(_PREHASH_ProfileEnd, 0);
            builder.addU16(_PREHASH_ProfileHollow, i == 2 ? 25000 : 0);
            std::vector<U8> te = texture_entry(rng, i == 2);
            builder.addBinaryData(_PREHASH_TextureEntry, te.data(), (S32)te.size());
            U8 anim[16] = { 0 };
            builder.addBinaryData(_PREHASH_TextureAnim, anim, i == 1 ? 16 : 0);
            if (i == 3)
            {
                builder.addString(_PREHASH_NameValue, "AttachItemID STRING RW SV 4f3ea7f8-5d61-4c7e-9a52-0b2f6e8d1c3a\nAttachmentOrientation VEC3 RW DS 0, 0, 0");
                builder.addBinaryData(_PREHASH_Data, NULL, 0);
                builder.addString(_PREHASH_Text, "Hover text");
                U8 color[4] = { 255, 255, 255, 255 };
                builder.addBinaryData(_PREHASH_TextColor, color, 4);
            }
            else
            {
                builder.addBinaryData(_PREHASH_NameValue, NULL, 0);
                builder.addBinaryData(_PREHASH_Data, NULL, 0);
                builder.addBinaryData(_PREHASH_Text, NULL, 0);
                U8 color[4] = { 0, 0, 0, 0 };
                builder.addBinaryData(_PREHASH_TextColor, color, 4);
            }
            builder.addBinaryData(_PREHASH_MediaURL, NULL, 0);
            builder.addBinaryData(_PREHASH_PSBlock, NULL, 0);
            U8 extra = 0; // no extra parameters: a count of zero
            builder.addBinaryData(_PREHASH_ExtraParams, &extra, 1);
            builder.addUUID(_PREHASH_Sound, LLUUID::null);
            builder.addUUID(_PREHASH_OwnerID, LLUUID::null);
            builder.addF32(_PREHASH_Gain, 0.f);
            builder.addU8(_PREHASH_Flags, 0);
            builder.addF32(_PREHASH_Radius, 0.f);
            builder.addU8(_PREHASH_JointType, 0);
            builder.addVector3(_PREHASH_JointPivot, LLVector3::zero);
            builder.addVector3(_PREHASH_JointAxisOrAnchor, LLVector3::zero);
        }
        std::vector<U8> buffer(MAX_BUFFER_SIZE, 0);
        U32 len = builder.buildMessage(buffer.data(), MAX_BUFFER_SIZE, 0);
        ps.object_update.assign(buffer.begin(), buffer.begin() + len);
        U8* cptr = buffer.data();
        U32 clen = len;
        builder.compressMessage(cptr, clen);
        ps.object_update_coded.assign(cptr, cptr + clen);

        builder.newMessage(_PREHASH_AgentUpdate);
        builder.nextBlock(_PREHASH_AgentData);
        LLUUID agent; rng_uuid(rng, agent);
        LLUUID session; rng_uuid(rng, session);
        builder.addUUID(_PREHASH_AgentID, agent);
        builder.addUUID(_PREHASH_SessionID, session);
        LLQuaternion body(0.f, 0.f, 0.38268343f, 0.92387953f);
        LLQuaternion head(0.05f, 0.f, 0.38268343f, 0.92387953f);
        head.normalize();
        builder.addQuat(_PREHASH_BodyRotation, body);
        builder.addQuat(_PREHASH_HeadRotation, head);
        builder.addU8(_PREHASH_State, 0);
        builder.addVector3(_PREHASH_CameraCenter, LLVector3(128.3f, 130.1f, 24.9f));
        builder.addVector3(_PREHASH_CameraAtAxis, LLVector3(0.70710677f, 0.70710677f, 0.f));
        builder.addVector3(_PREHASH_CameraLeftAxis, LLVector3(-0.70710677f, 0.70710677f, 0.f));
        builder.addVector3(_PREHASH_CameraUpAxis, LLVector3(0.f, 0.f, 1.f));
        builder.addF32(_PREHASH_Far, 128.f);
        builder.addU32(_PREHASH_ControlFlags, 0x00000001); // AGENT_CONTROL_AT_POS
        builder.addU8(_PREHASH_Flags, 0);
        len = builder.buildMessage(buffer.data(), MAX_BUFFER_SIZE, 0);
        ps.agent_update.assign(buffer.begin(), buffer.begin() + len);
        return ps;
    }

    // The getters the viewer's ObjectUpdate handler issues for a full
    // update of a prim (LLViewerObjectList::processObjectUpdate,
    // LLViewerObject::processUpdateMessage, LLVolumeMessage::unpackVolumeParams,
    // LLPrimitive::unpackTEMessage), in that order.
    U64 handler_getters(LLTemplateMessageReader& reader)
    {
        U64 sink = 0;
        S32 num_objects = reader.getNumberOfBlocks(_PREHASH_ObjectData);
        U64 region_handle; reader.getU64(_PREHASH_RegionData, _PREHASH_RegionHandle, region_handle);
        U16 time_dilation; reader.getU16(_PREHASH_RegionData, _PREHASH_TimeDilation, time_dilation);
        sink += region_handle + time_dilation;
        U8 data[256];
        U8 te[1024];
        std::string s;
        for (S32 i = 0; i < num_objects; ++i)
        {
            LLUUID fullid; reader.getUUID(_PREHASH_ObjectData, _PREHASH_FullID, fullid, i);
            U32 local_id; reader.getU32(_PREHASH_ObjectData, _PREHASH_ID, local_id, i);
            U8 pcode; reader.getU8(_PREHASH_ObjectData, _PREHASH_PCode, pcode, i);
            U32 flags; reader.getU32(_PREHASH_ObjectData, _PREHASH_UpdateFlags, flags, i);
            U32 crc; reader.getU32(_PREHASH_ObjectData, _PREHASH_CRC, crc, i);
            U32 parent_id; reader.getU32(_PREHASH_ObjectData, _PREHASH_ParentID, parent_id, i);
            LLUUID audio; reader.getUUID(_PREHASH_ObjectData, _PREHASH_Sound, audio, i);
            LLUUID owner; reader.getUUID(_PREHASH_ObjectData, _PREHASH_OwnerID, owner, i);
            F32 gain; reader.getF32(_PREHASH_ObjectData, _PREHASH_Gain, gain, i);
            F32 cutoff; reader.getF32(_PREHASH_ObjectData, _PREHASH_Radius, cutoff, i);
            U8 sound_flags; reader.getU8(_PREHASH_ObjectData, _PREHASH_Flags, sound_flags, i);
            U8 material; reader.getU8(_PREHASH_ObjectData, _PREHASH_Material, material, i);
            U8 click; reader.getU8(_PREHASH_ObjectData, _PREHASH_ClickAction, click, i);
            LLVector3 scale; reader.getVector3(_PREHASH_ObjectData, _PREHASH_Scale, scale, i);
            S32 length = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_ObjectData);
            reader.getBinaryData(_PREHASH_ObjectData, _PREHASH_ObjectData, data, length, i, 256);
            U8 u8v; U16 u16v; S8 s8v;
            reader.getU8(_PREHASH_ObjectData, _PREHASH_ProfileCurve, u8v, i); sink += u8v;
            reader.getU16(_PREHASH_ObjectData, _PREHASH_ProfileBegin, u16v, i); sink += u16v;
            reader.getU16(_PREHASH_ObjectData, _PREHASH_ProfileEnd, u16v, i); sink += u16v;
            reader.getU16(_PREHASH_ObjectData, _PREHASH_ProfileHollow, u16v, i); sink += u16v;
            reader.getU8(_PREHASH_ObjectData, _PREHASH_PathCurve, u8v, i); sink += u8v;
            reader.getU16(_PREHASH_ObjectData, _PREHASH_PathBegin, u16v, i); sink += u16v;
            reader.getU16(_PREHASH_ObjectData, _PREHASH_PathEnd, u16v, i); sink += u16v;
            reader.getU8(_PREHASH_ObjectData, _PREHASH_PathScaleX, u8v, i); sink += u8v;
            reader.getU8(_PREHASH_ObjectData, _PREHASH_PathScaleY, u8v, i); sink += u8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathShearX, s8v, i); sink += s8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathShearY, s8v, i); sink += s8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathTwist, s8v, i); sink += s8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathTwistBegin, s8v, i); sink += s8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathRadiusOffset, s8v, i); sink += s8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathTaperX, s8v, i); sink += s8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathTaperY, s8v, i); sink += s8v;
            reader.getU8(_PREHASH_ObjectData, _PREHASH_PathRevolutions, u8v, i); sink += u8v;
            reader.getS8(_PREHASH_ObjectData, _PREHASH_PathSkew, s8v, i); sink += s8v;
            S32 te_size = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_TextureEntry);
            reader.getBinaryData(_PREHASH_ObjectData, _PREHASH_TextureEntry, te, 0, i, 1023);
            reader.getU32(_PREHASH_ObjectData, _PREHASH_UpdateFlags, flags, i);
            U8 state; reader.getU8(_PREHASH_ObjectData, _PREHASH_State, state, i);
            S32 nv_size = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_NameValue);
            if (nv_size > 0) reader.getString(_PREHASH_ObjectData, _PREHASH_NameValue, s, i);
            S32 data_size = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_Data);
            if (data_size > 0) reader.getBinaryData(_PREHASH_ObjectData, _PREHASH_Data, data, data_size, i);
            S32 text_size = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_Text);
            if (text_size > 0)
            {
                reader.getString(_PREHASH_ObjectData, _PREHASH_Text, s, i);
                reader.getBinaryData(_PREHASH_ObjectData, _PREHASH_TextColor, data, 4, i);
            }
            reader.getString(_PREHASH_ObjectData, _PREHASH_MediaURL, s, i);
            S32 ps_size = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_PSBlock);
            if (ps_size > 0) reader.getBinaryData(_PREHASH_ObjectData, _PREHASH_PSBlock, data, ps_size, i);
            S32 ep_size = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_ExtraParams);
            if (ep_size > 0) reader.getBinaryData(_PREHASH_ObjectData, _PREHASH_ExtraParams, data, ep_size, i);
            S32 anim_size = reader.getSize(_PREHASH_ObjectData, i, _PREHASH_TextureAnim);
            if (anim_size > 0) reader.getBinaryData(_PREHASH_ObjectData, _PREHASH_TextureAnim, data, anim_size, i);
            sink += local_id + pcode + flags + crc + parent_id + (U64)gain + (U64)cutoff + sound_flags + material + click + (U64)scale.mV[0]
                + data[0] + te[0] + te_size + state + nv_size + data_size + text_size + ps_size + ep_size + anim_size + fullid.mData[0] + audio.mData[0] + owner.mData[0];
        }
        return sink;
    }

    // What the pre-port C++ reader built for every packet: an LLMsgData with
    // a block object per repeat, an indexed vector of variables per block
    // and a heap copy per variable. Timed here through the same public
    // classes on both builds, from a walk of the template over the packet,
    // so the data structure's own cost is visible.
    LLMsgData* build_msgdata(const LLMessageTemplate* t, const U8* buffer, S32 size)
    {
        S32 pos = LL_PACKET_ID_SIZE + (S32)t->mFrequency + buffer[PHL_OFFSET];
        LLMsgData* md = new LLMsgData(t->mName);
        for (LLMessageTemplate::message_block_map_t::const_iterator bit = t->mMemberBlocks.begin(); bit != t->mMemberBlocks.end(); ++bit)
        {
            const LLMessageBlock* b = *bit;
            U8 repeat = b->mType == MBT_SINGLE ? 1 : (b->mType == MBT_MULTIPLE ? (U8)b->mNumber : (pos < size ? buffer[pos++] : 0));
            for (S32 r = 0; r < repeat; ++r)
            {
                LLMsgBlkData* blk = new LLMsgBlkData(b->mName, repeat);
                blk->mName = b->mName + r;
                md->addBlock(blk);
                for (LLMessageBlock::message_variable_map_t::const_iterator vit = b->mMemberVariables.begin(); vit != b->mMemberVariables.end(); ++vit)
                {
                    const LLMessageVariable* var = *vit;
                    blk->addVariable(var->getName(), var->getType());
                    S32 n = var->getSize();
                    if (var->getType() == MVT_VARIABLE)
                    {
                        U32 tsize = 0;
                        if (n == 1) tsize = buffer[pos]; else if (n == 2) { U16 v; memcpy(&v, &buffer[pos], 2); tsize = v; } else { memcpy(&tsize, &buffer[pos], 4); }
                        pos += n;
                        blk->addData(var->getName(), &buffer[pos], tsize, var->getType());
                        pos += tsize;
                    }
                    else
                    {
                        blk->addData(var->getName(), &buffer[pos], n, var->getType());
                        pos += n;
                    }
                }
            }
        }
        return md;
    }

    // timing of the hot paths, only when asked for (VKS_MSG_BENCH=<iterations>)
    template<> template<>
    void object::test<6>()
    {
        set_test_name("hot path timing");
        const char* bench = getenv("VKS_MSG_BENCH");
        if (!bench || !*bench) return;
        S32 iterations = atoi(bench);
        if (iterations <= 0) iterations = 20000;
        // The test runner logs at LEVEL_DEBUG; the viewer runs at LEVEL_INFO.
        // Time at the viewer's level: the pre-port reader has an LL_DEBUGS
        // per template block, and formatting it is the runner's cost, not
        // the viewer's.
        LLError::setDefaultLevel(LLError::LEVEL_INFO);
        ensure_message_system();
        install_exception_counter();
        TemplateSet& ts = templates();
        LLMessageTemplate* object_update = ts.name_map[_PREHASH_ObjectUpdate];
        LLMessageTemplate* agent_update = ts.name_map[_PREHASH_AgentUpdate];
        ensure("ObjectUpdate and AgentUpdate present", object_update && agent_update);

        LLTemplateMessageBuilder builder(ts.name_map);
        PacketSet ps = build_packets(builder);
        const char* packets_out = getenv("VKS_MSG_PACKETS_OUT");
        if (packets_out && *packets_out)
        {
            std::ofstream a((std::string(packets_out) + "/objectupdate.bin").c_str(), std::ios::binary);
            a.write((const char*)ps.object_update.data(), ps.object_update.size());
            std::ofstream b((std::string(packets_out) + "/objectupdate_coded.bin").c_str(), std::ios::binary);
            b.write((const char*)ps.object_update_coded.data(), ps.object_update_coded.size());
            std::ofstream c((std::string(packets_out) + "/agentupdate.bin").c_str(), std::ios::binary);
            c.write((const char*)ps.agent_update.data(), ps.agent_update.size());
        }
        const U32 len = (U32)ps.object_update.size();
        std::vector<U8> buffer(MAX_BUFFER_SIZE, 0);
        memcpy(buffer.data(), ps.object_update.data(), len);
        std::vector<U8> work(MAX_BUFFER_SIZE, 0);
        LLTemplateMessageReader reader(ts.number_map);

        auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        typedef std::chrono::steady_clock clock;
        auto time_it = [&](S32 reps, const std::function<void()>& f)
        {
            std::vector<double> rounds;
            for (S32 round = 0; round < 7; ++round)
            {
                clock::time_point start = clock::now();
                for (S32 i = 0; i < reps; ++i) f();
                rounds.push_back(std::chrono::duration<double, std::micro>(clock::now() - start).count() / reps);
            }
            return median(rounds);
        };

        U64 sink = 0;
        // VKS_MSG_BENCH_ONLY=decode: only the validate+read loop, for a
        // profiler to sample
        const char* only = getenv("VKS_MSG_BENCH_ONLY");
        if (only && std::string(only) == "decode")
        {
            double us = time_it(iterations * 5, [&]()
            {
                reader.clearMessage();
                reader.validateMessage(buffer.data(), (S32)len, LLHost(), false);
                reader.readMessage(buffer.data(), LLHost());
            });
            printf("decode only,%f\n", us);
            return;
        }
        double validate_us = time_it(iterations, [&]()
        {
            reader.clearMessage();
            reader.validateMessage(buffer.data(), (S32)len, LLHost(), false);
        });
        double decode_us = time_it(iterations, [&]()
        {
            reader.clearMessage();
            reader.validateMessage(buffer.data(), (S32)len, LLHost(), false);
            reader.readMessage(buffer.data(), LLHost());
        });
        double handler_us = time_it(iterations, [&]()
        {
            reader.clearMessage();
            reader.validateMessage(buffer.data(), (S32)len, LLHost(), false);
            reader.readMessage(buffer.data(), LLHost());
            sink += handler_getters(reader);
        });
        reader.clearMessage();
        reader.validateMessage(buffer.data(), (S32)len, LLHost(), false);
        reader.readMessage(buffer.data(), LLHost());
        double getters_only_us = time_it(iterations, [&]() { sink += handler_getters(reader); });

        // the C++ data structure's own cost, same code on both builds
        double msgdata_build_us = time_it(iterations, [&]()
        {
            LLMsgData* md = build_msgdata(object_update, buffer.data(), (S32)len);
            sink += md->mMemberBlocks.size();
            delete md;
        });
        // build and delete timed apart: 1000 live objects per round
        std::vector<double> alloc_rounds, free_rounds;
        for (S32 round = 0; round < 7; ++round)
        {
            std::vector<LLMsgData*> keep;
            keep.reserve(1000);
            clock::time_point start = clock::now();
            for (S32 i = 0; i < 1000; ++i) keep.push_back(build_msgdata(object_update, buffer.data(), (S32)len));
            alloc_rounds.push_back(std::chrono::duration<double, std::micro>(clock::now() - start).count() / 1000);
            start = clock::now();
            for (size_t i = 0; i < keep.size(); ++i) delete keep[i];
            free_rounds.push_back(std::chrono::duration<double, std::micro>(clock::now() - start).count() / 1000);
        }
        double msgdata_alloc_us = median(alloc_rounds);
        double msgdata_free_us = median(free_rounds);

        double expand_us = time_it(iterations, [&]()
        {
            memcpy(work.data(), ps.object_update_coded.data(), ps.object_update_coded.size());
            U8* ptr = work.data();
            S32 size = (S32)ps.object_update_coded.size();
            gMessageSystem->zeroCodeExpand(&ptr, &size);
            sink += size;
        });

        std::vector<U8> out(MAX_BUFFER_SIZE, 0);
        double pack_us = time_it(iterations, [&]()
        {
            PacketSet one;
            // the AgentUpdate half of build_packets
            builder.newMessage(_PREHASH_AgentUpdate);
            builder.nextBlock(_PREHASH_AgentData);
            builder.addUUID(_PREHASH_AgentID, LLUUID("5f0d8a8e-4c53-4a3b-9c8e-1f2a3b4c5d6e"));
            builder.addUUID(_PREHASH_SessionID, LLUUID("9a7b6c5d-4e3f-4a1b-8c9d-0e1f2a3b4c5d"));
            builder.addQuat(_PREHASH_BodyRotation, LLQuaternion(0.f, 0.f, 0.38268343f, 0.92387953f));
            builder.addQuat(_PREHASH_HeadRotation, LLQuaternion(0.05f, 0.f, 0.38268343f, 0.92387953f));
            builder.addU8(_PREHASH_State, 0);
            builder.addVector3(_PREHASH_CameraCenter, LLVector3(128.3f, 130.1f, 24.9f));
            builder.addVector3(_PREHASH_CameraAtAxis, LLVector3(0.70710677f, 0.70710677f, 0.f));
            builder.addVector3(_PREHASH_CameraLeftAxis, LLVector3(-0.70710677f, 0.70710677f, 0.f));
            builder.addVector3(_PREHASH_CameraUpAxis, LLVector3(0.f, 0.f, 1.f));
            builder.addF32(_PREHASH_Far, 128.f);
            builder.addU32(_PREHASH_ControlFlags, 0x00000001);
            builder.addU8(_PREHASH_Flags, 0);
            U32 n = builder.buildMessage(out.data(), MAX_BUFFER_SIZE, 0);
            U8* ptr = out.data();
            U32 l = n;
            builder.compressMessage(ptr, l);
            sink += l;
        });

        // the binary data packer: a TextureEntry-like sequence of 50 values
        std::vector<U8> dpbuf(4096);
        double dp_us = time_it(iterations, [&]()
        {
            LLDataPackerBinaryBuffer dp(dpbuf.data(), (S32)dpbuf.size());
            for (S32 k = 0; k < 10; ++k)
            {
                dp.packUUID(LLUUID::null, "id");
                dp.packColor4U(LLColor4U(1, 2, 3, 4), "c");
                dp.packF32(1.5f, "f");
                dp.packU8((U8)k, "u");
                dp.packS16((S16)-k, "s");
            }
            LLDataPackerBinaryBuffer up(dpbuf.data(), dp.getCurrentSize());
            for (S32 k = 0; k < 10; ++k)
            {
                LLUUID id; LLColor4U c; F32 f; U8 u; S16 s;
                up.unpackUUID(id, "id"); up.unpackColor4U(c, "c"); up.unpackF32(f, "f"); up.unpackU8(u, "u"); up.unpackS16(s, "s");
                sink += u;
            }
        });

        std::ostringstream table;
        table << "\nVKS_MSG_BENCH iterations=" << iterations << " (median of 7 rounds, microseconds per operation; sink " << sink << ")\n";
        table << "unpack ObjectUpdate " << len << " B (validate only)," << validate_us << "\n";
        table << "unpack ObjectUpdate " << len << " B (validate+read)," << decode_us << "\n";
        table << "unpack ObjectUpdate " << len << " B (validate+read+handler getters)," << handler_us << "\n";
        table << "ObjectUpdate handler getters only," << getters_only_us << "\n";
        table << "LLMsgData build+delete of ObjectUpdate (C++ data structure)," << msgdata_build_us << "\n";
        table << "LLMsgData build only (allocations)," << msgdata_alloc_us << "\n";
        table << "LLMsgData delete only," << msgdata_free_us << "\n";
        table << "zero-code expand ObjectUpdate " << ps.object_update_coded.size() << " B," << expand_us << "\n";
        table << "pack AgentUpdate " << ps.agent_update.size() << " B (new+add+build+compress)," << pack_us << "\n";
        table << "binary data packer 50 pack + 50 unpack," << dp_us << "\n";
        LL_INFOS() << table.str() << LL_ENDL;
        printf("%s", table.str().c_str());
        const char* out_path = getenv("VKS_MSG_BENCH_OUT");
        if (out_path && *out_path)
        {
            std::ofstream f(out_path);
            f << table.str();
        }
    }
}

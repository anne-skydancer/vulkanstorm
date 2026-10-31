/**
 * @file llinventory_tool.cpp
 * @brief Recorder of the inventory data model's oracle (vkstorm-rust rung
 * seven, recorder R1): the corpora C1-C10 of the inventory survey, generated
 * from the frozen copies (llinventory_oracle.cpp) and from the live classes,
 * the fixture writer (`llinventory_tool dump <dir>`) and the C++ baseline
 * benchmark (`llinventory_tool bench <rounds>`).
 *
 * The same file is compiled into INTEGRATION_TEST_llinventory_parity with
 * VKS_INV_NO_MAIN, which compares the live classes with the frozen copies in
 * process and replays the committed fixtures against the live classes.
 *
 * Every corpus is a sequence of text lines "<case>\t<field>\t<value>..." (the
 * format is described in tests/fixtures/README.md). Inputs are fixed or drawn
 * from xorshift64* generators with the seeds below; nothing depends on the
 * clock, the locale or the machine, except the runtime edges listed in
 * kRuntimeEdges, whose stated per-platform expectations are recorded.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llinventory_oracle.h"

#include "llapr.h"
#include "llassettype.h"
#include "llcategory.h"
#include "llerrorcontrol.h"
#include "llfoldertype.h"
#include "llinventory.h"
#include "llinventorydefines.h"
#include "llinventorysettings.h"
#include "llinventorytype.h"
#include "llnotecard.h"
#include "llpermissions.h"
#include "llsaleinfo.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llstring.h"
#include "lltransactionflags.h"
#include "lltransactiontypes.h"
#include "lldate.h"
#include "lluri.h"
#include "message.h"
#include "message_prehash.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#if LL_WINDOWS
# include <excpt.h>
#endif

#ifdef LL_USESYSTEMLIBS
# include <zlib.h>
#else
# include "zlib-ng/zlib.h"
#endif

#ifndef VKS_MSG_TEMPLATE_FILE
#error VKS_MSG_TEMPLATE_FILE must name scripts/messages/message_template.msg
#endif

namespace vksinv
{

//============================================================================
// Platform, seeds, sample policy
//============================================================================

#if LL_WINDOWS
static const char* PLATFORM = "win";
#else
static const char* PLATFORM = "linux";
#endif

std::string platform_description()
{
    std::ostringstream os;
#if defined(_MSC_VER)
    os << "windows x86_64, MSVC " << _MSC_VER << " (" << _MSC_FULL_VER << "), " << sizeof(time_t) * 8 << "-bit time_t";
#elif defined(__GNUC__)
    os << "linux x86_64, GCC " << __VERSION__ << ", " << sizeof(time_t) * 8 << "-bit time_t";
#else
    os << "unknown";
#endif
    return os.str();
}

static const U64 SEED_C3 = 0x7633000000000001ULL;   // permissions random stream
static const U64 SEED_C4 = 0x7634000000000001ULL;   // sale info random stream
static const U64 SEED_C5 = 0x7635000000000001ULL;   // item random stream
static const U64 SEED_BENCH = 0x7642000000000001ULL; // benchmark population

static const U64 C3_RANDOM_CASES = 200000;
static const U64 C4_RANDOM_CASES = 100000;
static const U64 C5_RANDOM_CASES = 20000;

// A random stream's case i is kept in the committed sample when
// i < SAMPLE_HEAD or i % SAMPLE_EVERY == 0.
static const U64 SAMPLE_HEAD = 200;
static const U64 SAMPLE_EVERY = 1000;
inline bool sampled(U64 i) { return i < SAMPLE_HEAD || (i % SAMPLE_EVERY) == 0; }

//============================================================================
// xorshift64* and SHA-256
//============================================================================

struct Rng
{
    U64 s;
    explicit Rng(U64 seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    U64 next()
    {
        U64 x = s;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        s = x;
        return x * 0x2545F4914F6CDD1DULL;
    }
    U32 below(U32 n) { return (U32)(next() % n); }
    bool coin() { return (next() & 1) != 0; }
};

// SHA-256 (FIPS 180-4).
struct Sha256
{
    U32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    U8 block[64];
    size_t used = 0;
    U64 total = 0;

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
            w[i] = (U32(p[i * 4]) << 24) | (U32(p[i * 4 + 1]) << 16) | (U32(p[i * 4 + 2]) << 8) | U32(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i)
        {
            const U32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const U32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        U32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const U32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const U32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const void* data, size_t n)
    {
        const U8* p = (const U8*)data;
        total += n;
        while (n)
        {
            const size_t take = std::min(n, sizeof(block) - used);
            memcpy(block + used, p, take);
            used += take; p += take; n -= take;
            if (used == sizeof(block))
            {
                compress(block);
                used = 0;
            }
        }
    }

    std::string hex()
    {
        const U64 bits = total * 8;
        const U8 pad = 0x80;
        update(&pad, 1);
        const U8 zero = 0;
        while (used != 56)
        {
            update(&zero, 1);
        }
        U8 length[8];
        for (int i = 0; i < 8; ++i)
        {
            length[i] = U8(bits >> (56 - 8 * i));
        }
        update(length, 8);
        std::string out;
        char buf[9];
        for (U32 word : h)
        {
            snprintf(buf, sizeof(buf), "%08x", word);
            out += buf;
        }
        return out;
    }
};

//============================================================================
// Formatting
//============================================================================

// Bytes 0x20-0x7e except '\' as they are; '\' as "\\"; every other byte as \xHH.
std::string esc(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    char buf[8];
    for (unsigned char c : s)
    {
        if (c == '\\')
        {
            out += "\\\\";
        }
        else if (c >= 0x20 && c <= 0x7e)
        {
            out += (char)c;
        }
        else
        {
            snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

std::string hexbytes(const void* data, size_t n)
{
    static const char* digits = "0123456789abcdef";
    const U8* p = (const U8*)data;
    std::string out;
    out.resize(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        out[2 * i] = digits[p[i] >> 4];
        out[2 * i + 1] = digits[p[i] & 15];
    }
    return out;
}
std::string hexbytes(const std::string& s) { return hexbytes(s.data(), s.size()); }
std::string hexbytes(const std::vector<U8>& v) { return hexbytes(v.data(), v.size()); }

std::string u(const LLUUID& id) { return id.asString(); }
std::string x8(U32 v) { char b[16]; snprintf(b, sizeof(b), "%08x", v); return b; }
std::string x2(U32 v) { char b[8]; snprintf(b, sizeof(b), "%02x", v & 0xff); return b; }
std::string d(S64 v) { char b[32]; snprintf(b, sizeof(b), "%lld", (long long)v); return b; }
std::string bl(bool v) { return v ? "1" : "0"; }
std::string cstr(const char* s) { return s ? esc(s) : std::string("NULL"); }

std::string llsd_bin(const LLSD& sd)
{
    std::ostringstream os;
    LLSDSerialize::toBinary(sd, os);
    return hexbytes(os.str());
}
std::string llsd_txt(const LLSD& sd)
{
    std::ostringstream os;
    LLSDSerialize::toNotation(sd, os);
    return esc(os.str());
}

//============================================================================
// Sinks
//============================================================================

struct Sink
{
    virtual ~Sink() {}
    virtual void put(const std::string& line) = 0;
};

struct VecSink : public Sink
{
    std::vector<std::string> lines;
    void put(const std::string& line) override { lines.push_back(line); }
};

// Runtime edges: fields whose value depends on the C runtime or is
// undefined behaviour. win/linux name the stated expectation on each
// platform; both null marks undefined behaviour (the value is recorded as
// "UB" and described in the README).
struct RuntimeEdge
{
    const char* cs;
    const char* field;
    int column;        // index among the values; -1 for a scalar line
    const char* win;   // the stated value with the MSVC runtime
    const char* lin;   // the stated value with glibc and libstdc++
    const char* crate; // the value the crate's own mapping must give (NULL: none stated)
};

// Filled from the Windows and Linux recordings (see the README).
static const RuntimeEdge kRuntimeEdges[] = {
    // LLSD::asInteger of the String "0x10" is (int)asReal(), an istringstream >> F64:
    // the MSVC runtime reads hexadecimal floating point (16), libstdc++ stops at 'x' (0).
    // The viewer keeps the C++ LLSD coercions (per platform); the crate's mapping over
    // vkstorm_llsd::Value follows vkstorm-llsd, which gives 16 (coordinator decision F5).
    {"c7.created_at.str.0x10.1", "ret+item", 20 /* ret, then date */, "16", "0", "16"},
    {"c7.created_at.str.0x10.0", "ret+item", 20 /* ret, then date */, "16", "0", "16"},
    {"c8.type.str.0x10", "fromllsd", 1 + 3, "16", "0", "16"},
    {"c8.type_default.str.0x10", "fromllsd", 1 + 3, "16", "0", "16"},
    {"c8.version.str.0x10", "vimportllsd", 1 + 10, "16", "0", "16"},
    {NULL, NULL, 0, NULL, NULL, NULL},
};

// "RT(win=..;linux=..[;crate=..])" when the platform's observed value is the stated
// one, else "RT-MISMATCH(...)" (which fails every comparison).
std::string stated_rt(const std::string& observed, const char* win, const char* lin, const char* crate)
{
    std::string stated = std::string("RT(win=") + win + ";linux=" + lin + (crate ? std::string(";crate=") + crate : std::string()) + ")";
    const char* expect = (strcmp(PLATFORM, "win") == 0) ? win : lin;
    if (observed == expect) return stated;
    return std::string("RT-MISMATCH(") + PLATFORM + "=" + observed + ";stated=" + stated + ")";
}

struct EdgeIndex
{
    std::map<std::string, std::vector<const RuntimeEdge*> > by_key;
    EdgeIndex()
    {
        for (const RuntimeEdge& e : kRuntimeEdges)
        {
            if (!e.cs) continue;
            by_key[std::string(e.cs) + "\t" + e.field].push_back(&e);
        }
    }
    static EdgeIndex& get() { static EdgeIndex ix; return ix; }
};

void emit(Sink& s, const std::string& cs, const std::string& field, std::vector<std::string> vals)
{
    auto& ix = EdgeIndex::get().by_key;
    if (!ix.empty())
    {
        auto it = ix.find(cs + "\t" + field);
        if (it != ix.end())
        {
            for (const RuntimeEdge* e : it->second)
            {
                if (e->column < 0 || e->column >= (int)vals.size()) continue;
                std::string& v = vals[e->column];
                v = stated_rt(v, e->win, e->lin, e->crate);
            }
        }
    }
    std::string line = cs;
    line += '\t';
    line += field;
    for (const std::string& v : vals)
    {
        line += '\t';
        line += v;
    }
    s.put(line);
}

//============================================================================
// Fixed ids and values
//============================================================================

LLUUID U(U8 tag)
{
    static const U8 pat[14] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba};
    LLUUID id;
    id.mData[0] = tag;
    memcpy(id.mData + 1, pat, 14);
    id.mData[15] = tag;
    return id;
}
const LLUUID& NUL() { return LLUUID::null; }
LLUUID MAGIC() { return LLUUID("3c115e51-04f4-523c-9fa6-98aff1034730"); }
LLUUID NOT_MAGIC()
{
    LLUUID id = MAGIC();
    for (int i = 0; i < 16; ++i) id.mData[i] = (U8)~id.mData[i];
    return id;
}
// Agents and groups of the permission corpora.
LLUUID ID_A() { return U(0x0a); } // an owner
LLUUID ID_B() { return U(0x0b); } // another agent
LLUUID ID_C() { return U(0x0c); } // a creator
LLUUID ID_G() { return U(0x06); } // a group
LLUUID ID_H() { return U(0x07); } // another group

std::string idname(const LLUUID& id)
{
    if (id.isNull()) return "null";
    if (id == ID_A()) return "A";
    if (id == ID_B()) return "B";
    if (id == ID_C()) return "C";
    if (id == ID_G()) return "G";
    if (id == ID_H()) return "H";
    return u(id);
}

const U32 T_ = PERM_TRANSFER, M_ = PERM_MODIFY, C_ = PERM_COPY, X_ = PERM_EXPORT, V_ = PERM_MOVE;

// Mask edges (C3 structured cases and the random draws).
static const U32 kMaskEdges[] = {
    0x00000000, 0x7fffffff, 0xffffffff, 0x0000e000,
    0x00002000, 0x00004000, 0x00008000, 0x00010000, 0x00080000, 0x00000001, 0x80000000,
    0x7fffbfff, 0x7fff7fff, 0x7fff3fff, 0x7fffdfff, 0x7fff9fff, 0x7fff5fff, 0x7fff1fff,
};
static const size_t kMaskEdgeCount = sizeof(kMaskEdges) / sizeof(kMaskEdges[0]);

//============================================================================
// Field records (implementation independent)
//============================================================================

struct PermF
{
    LLUUID creator, owner, last, group;
    U32 base = PERM_ALL, owner_mask = PERM_ALL, group_mask = PERM_ALL, everyone = PERM_ALL, next = PERM_ALL;
    bool group_owned = false;
};

struct ItemF
{
    LLUUID id, parent, asset, thumb;
    S32 type = -1, inv_type = -1;
    U32 flags = 0;
    PermF perm;
    S32 sale_type = 0, sale_price = 10;
    S64 date = 0;
    bool fav = false;
    std::string runtime, name, desc;
};

struct CatF
{
    LLUUID id, parent, thumb;
    S32 type = 8, pref = -1;
    bool fav = false;
    S64 date = 0;
    std::string runtime, name;
    LLUUID owner;   // viewer subclass
    S32 version = -1;
};

//============================================================================
// Raw access to the protected fields (harness only)
//============================================================================

template<class S> class RawSale : public S
{
public:
    void set(S32 type, S32 price)
    {
        this->mSaleType = static_cast<typename S::EForSale>(type);
        this->mSalePrice = price;
    }
};

template<class P> P make_perm(const PermF& f)
{
    P p;
    p.init(f.creator, f.owner, f.last, f.group);
    p.setMaskBase(f.base);
    p.setMaskOwner(f.owner_mask);
    p.setMaskGroup(f.group_mask);
    p.setMaskEveryone(f.everyone);
    p.setMaskNext(f.next);
    p.yesReallySetOwner(f.owner, f.group_owned);
    return p;
}

template<class S> S make_sale(S32 type, S32 price)
{
    RawSale<S> r;
    r.set(type, price);
    return r;
}

template<class B> class RawItem : public B
{
public:
    void assign(const ItemF& f)
    {
        typedef typename std::remove_reference<decltype(this->mPermissions)>::type P;
        typedef typename std::remove_reference<decltype(this->mSaleInfo)>::type S;
        this->mUUID = f.id;
        this->mParentUUID = f.parent;
        this->mAssetUUID = f.asset;
        this->mThumbnailUUID = f.thumb;
        this->mType = static_cast<decltype(this->mType)>(f.type);
        this->mInventoryType = static_cast<decltype(this->mInventoryType)>(f.inv_type);
        this->mFlags = f.flags;
        this->mPermissions = make_perm<P>(f.perm);
        this->mSaleInfo = make_sale<S>(f.sale_type, f.sale_price);
        this->mCreationDate = (time_t)f.date;
        this->mFavorite = f.fav;
        this->mRuntime = f.runtime;
        this->mName = f.name;
        this->mDescription = f.desc;
    }
};

template<class B> class RawCat : public B
{
public:
    void assign(const CatF& f)
    {
        this->mUUID = f.id;
        this->mParentUUID = f.parent;
        this->mThumbnailUUID = f.thumb;
        this->mType = static_cast<decltype(this->mType)>(f.type);
        this->mPreferredType = static_cast<decltype(this->mPreferredType)>(f.pref);
        this->mFavorite = f.fav;
        this->mCreationDate = (time_t)f.date;
        this->mRuntime = f.runtime;
        this->mName = f.name;
    }
};

template<class B> class RawObj : public B
{
public:
    void assign(const CatF& f)
    {
        this->mUUID = f.id;
        this->mParentUUID = f.parent;
        this->mThumbnailUUID = f.thumb;
        this->mType = static_cast<decltype(this->mType)>(f.type);
        this->mFavorite = f.fav;
        this->mCreationDate = (time_t)f.date;
        this->mRuntime = f.runtime;
        this->mName = f.name;
    }
};

//============================================================================
// The two implementations
//============================================================================

struct NullBridge : public LLTranslationBridge
{
    std::string getString(const std::string&) override { return std::string(); }
};

struct Live
{
    static const char* name() { return "live"; }
    typedef ::LLInventoryObject Object;
    typedef ::LLInventoryItem Item;
    typedef ::LLInventoryCategory Category;
    typedef ::LLPermissions Perm;
    typedef ::LLAggregatePermissions Agg;
    typedef ::LLSaleInfo Sale;
    typedef ::LLAssetType AT;
    typedef ::LLInventoryType IT;
    typedef ::LLFolderType FT;
    typedef ::LLCategory Cat;
    typedef ::LLSettingsType Settings;
    typedef ::LLNotecard Notecard;
    typedef llinventory_live_shim::LLViewerInventoryItem VItem;
    typedef llinventory_live_shim::LLViewerInventoryCategory VCat;
    static bool types_match(S32 it, S32 at) { return ::inventory_and_asset_types_match((IT::EType)it, (AT::EType)at); }
    static LLSD sd_from_item(Item* i) { return ::ll_create_sd_from_inventory_item(i); }
    static LLSD sd_from_cat(Category* c) { return ::ll_create_sd_from_inventory_category(c); }
    static LLPointer<Category> cat_from_sd(const LLSD& sd) { return ::ll_create_category_from_sd(sd); }
    static LLSD sd_from_perm(const Perm& p) { return ::ll_create_sd_from_permissions(p); }
    static void fill_sd_from_perm(LLSD& sd, const Perm& p) { ::ll_fill_sd_from_permissions(sd, p); }
    static Perm perm_from_sd(const LLSD& sd) { return ::ll_permissions_from_sd(sd); }
    static LLSD sd_from_sale(const Sale& s) { return ::ll_create_sd_from_sale_info(s); }
    static Sale sale_from_sd(const LLSD& sd) { return ::ll_sale_info_from_sd(sd); }
    static std::string mask_str(U32 m, bool os) { return ::mask_to_string(m, os); }
    static U8 pack_tf(bool a, bool b) { return ::pack_transaction_flags(a, b); }
    static bool tf_src(U8 f) { return ::is_tf_source_group(f); }
    static bool tf_dst(U8 f) { return ::is_tf_dest_group(f); }
    static bool tf_own(U8 f) { return ::is_tf_owner_group(f); }
    static std::string msg_src(S32 a, const LLUUID& s, const LLUUID& dd, const std::string& n, S32 t, const std::string& ds)
    { return ::build_transfer_message_to_source(a, s, dd, n, t, ds); }
    static std::string msg_dst(S32 a, const LLUUID& dd, const LLUUID& s, const std::string& n, S32 t, const std::string& ds)
    { return ::build_transfer_message_to_destination(a, dd, s, n, t, ds); }
    static void set_opensim(bool v) { Perm::setupIsInOpenSim(v); }
    static U8 task_item_key() { return ::TASK_INVENTORY_ITEM_KEY; }
    static U8 task_asset_key() { return ::TASK_INVENTORY_ASSET_KEY; }
    static U8 tf_const(int i)
    {
        const U8 v[] = {::TRANSACTION_FLAGS_NONE, ::TRANSACTION_FLAG_SOURCE_GROUP, ::TRANSACTION_FLAG_DEST_GROUP,
                        ::TRANSACTION_FLAG_OWNER_GROUP, ::TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION,
                        ::TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION_REMOVAL};
        return v[i];
    }
    static S32 max_inventory_buffer() { return ::MAX_INVENTORY_BUFFER_SIZE; }
    static S32 default_price() { return ::DEFAULT_PRICE; }
    static S32 sst(int i) { return i ? ::SST_LUA : ::SST_LSL; }
};

struct Oracle
{
    static const char* name() { return "oracle"; }
    typedef llinventory_oracle::LLInventoryObject Object;
    typedef llinventory_oracle::LLInventoryItem Item;
    typedef llinventory_oracle::LLInventoryCategory Category;
    typedef llinventory_oracle::LLPermissions Perm;
    typedef llinventory_oracle::LLAggregatePermissions Agg;
    typedef llinventory_oracle::LLSaleInfo Sale;
    typedef llinventory_oracle::LLAssetType AT;
    typedef llinventory_oracle::LLInventoryType IT;
    typedef llinventory_oracle::LLFolderType FT;
    typedef llinventory_oracle::LLCategory Cat;
    typedef llinventory_oracle::LLSettingsType Settings;
    typedef llinventory_oracle::LLNotecard Notecard;
    typedef llinventory_oracle::LLViewerInventoryItem VItem;
    typedef llinventory_oracle::LLViewerInventoryCategory VCat;
    static bool types_match(S32 it, S32 at) { return llinventory_oracle::inventory_and_asset_types_match((IT::EType)it, (AT::EType)at); }
    static LLSD sd_from_item(Item* i) { return llinventory_oracle::ll_create_sd_from_inventory_item(i); }
    static LLSD sd_from_cat(Category* c) { return llinventory_oracle::ll_create_sd_from_inventory_category(c); }
    static LLPointer<Category> cat_from_sd(const LLSD& sd) { return llinventory_oracle::ll_create_category_from_sd(sd); }
    static LLSD sd_from_perm(const Perm& p) { return llinventory_oracle::ll_create_sd_from_permissions(p); }
    static void fill_sd_from_perm(LLSD& sd, const Perm& p) { llinventory_oracle::ll_fill_sd_from_permissions(sd, p); }
    static Perm perm_from_sd(const LLSD& sd) { return llinventory_oracle::ll_permissions_from_sd(sd); }
    static LLSD sd_from_sale(const Sale& s) { return llinventory_oracle::ll_create_sd_from_sale_info(s); }
    static Sale sale_from_sd(const LLSD& sd) { return llinventory_oracle::ll_sale_info_from_sd(sd); }
    static std::string mask_str(U32 m, bool os) { return llinventory_oracle::mask_to_string(m, os); }
    static U8 pack_tf(bool a, bool b) { return llinventory_oracle::pack_transaction_flags(a, b); }
    static bool tf_src(U8 f) { return llinventory_oracle::is_tf_source_group(f); }
    static bool tf_dst(U8 f) { return llinventory_oracle::is_tf_dest_group(f); }
    static bool tf_own(U8 f) { return llinventory_oracle::is_tf_owner_group(f); }
    static std::string msg_src(S32 a, const LLUUID& s, const LLUUID& dd, const std::string& n, S32 t, const std::string& ds)
    { return llinventory_oracle::build_transfer_message_to_source(a, s, dd, n, t, ds); }
    static std::string msg_dst(S32 a, const LLUUID& dd, const LLUUID& s, const std::string& n, S32 t, const std::string& ds)
    { return llinventory_oracle::build_transfer_message_to_destination(a, dd, s, n, t, ds); }
    static void set_opensim(bool v) { Perm::setupIsInOpenSim(v); }
    static U8 task_item_key() { return llinventory_oracle::TASK_INVENTORY_ITEM_KEY; }
    static U8 task_asset_key() { return llinventory_oracle::TASK_INVENTORY_ASSET_KEY; }
    static U8 tf_const(int i)
    {
        const U8 v[] = {llinventory_oracle::TRANSACTION_FLAGS_NONE, llinventory_oracle::TRANSACTION_FLAG_SOURCE_GROUP,
                        llinventory_oracle::TRANSACTION_FLAG_DEST_GROUP, llinventory_oracle::TRANSACTION_FLAG_OWNER_GROUP,
                        llinventory_oracle::TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION,
                        llinventory_oracle::TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION_REMOVAL};
        return v[i];
    }
    static S32 max_inventory_buffer() { return llinventory_oracle::MAX_INVENTORY_BUFFER_SIZE; }
    static S32 default_price() { return llinventory_oracle::DEFAULT_PRICE; }
    static S32 sst(int i) { return i ? llinventory_oracle::SST_LUA : llinventory_oracle::SST_LSL; }
};

//============================================================================
// Construction and dumps
//============================================================================

template<class I> LLPointer<typename I::Item> make_item(const ItemF& f)
{
    RawItem<typename I::Item>* r = new RawItem<typename I::Item>();
    r->assign(f);
    return LLPointer<typename I::Item>(r);
}
template<class I> LLPointer<typename I::VItem> make_vitem(const ItemF& f, const LLUUID& transaction)
{
    RawItem<typename I::VItem>* r = new RawItem<typename I::VItem>();
    r->assign(f);
    static_cast<LLUUID&>(r->mTransactionID) = transaction;
    return LLPointer<typename I::VItem>(r);
}
template<class I> LLPointer<typename I::Category> make_cat(const CatF& f)
{
    RawCat<typename I::Category>* r = new RawCat<typename I::Category>();
    r->assign(f);
    return LLPointer<typename I::Category>(r);
}
template<class I> LLPointer<typename I::VCat> make_vcat(const CatF& f)
{
    RawCat<typename I::VCat>* r = new RawCat<typename I::VCat>();
    r->assign(f);
    r->mOwnerID = f.owner;
    r->mVersion = f.version;
    return LLPointer<typename I::VCat>(r);
}
template<class I> LLPointer<typename I::Object> make_obj(const CatF& f)
{
    RawObj<typename I::Object>* r = new RawObj<typename I::Object>();
    r->assign(f);
    return LLPointer<typename I::Object>(r);
}

// Column lists (README "Records").
static const char* kItemCols = "id parent asset thumbnail type inv_type flags creator owner last_owner group base owner_mask group_mask everyone next group_owned sale_type sale_price date favorite runtime name desc";
static const char* kCatCols = "id parent type pref_type thumbnail favorite date runtime name";
static const char* kVCatCols = "id parent type pref_type thumbnail favorite date runtime name owner version";
static const char* kObjCols = "id parent type thumbnail favorite date runtime name";
static const char* kPermCols = "creator owner last_owner group base owner_mask group_mask everyone next group_owned";
static const char* kSaleCols = "type price";
static const char* kStreamCols = "state pos rest";

enum ItemCol { IC_ID, IC_PARENT, IC_ASSET, IC_THUMB, IC_TYPE, IC_INV_TYPE, IC_FLAGS, IC_CREATOR, IC_OWNER, IC_LAST, IC_GROUP,
               IC_BASE, IC_OWNER_MASK, IC_GROUP_MASK, IC_EVERYONE, IC_NEXT, IC_GROUP_OWNED, IC_SALE_TYPE, IC_SALE_PRICE, IC_DATE,
               IC_FAV, IC_RUNTIME, IC_NAME, IC_DESC };
enum PermCol { PC_CREATOR, PC_OWNER, PC_LAST, PC_GROUP, PC_BASE, PC_OWNER_MASK, PC_GROUP_MASK, PC_EVERYONE, PC_NEXT, PC_GROUP_OWNED };

template<class P> std::vector<std::string> perm_cols(const P& p)
{
    return {u(p.getCreator()), u(p.getOwner()), u(p.getLastOwner()), u(p.getGroup()),
            x8(p.getMaskBase()), x8(p.getMaskOwner()), x8(p.getMaskGroup()), x8(p.getMaskEveryone()), x8(p.getMaskNextOwner()),
            bl(p.isGroupOwned())};
}
template<class S> std::vector<std::string> sale_cols(const S& s)
{
    return {d((S32)s.getSaleType()), d(s.getSalePrice())};
}
template<class It> std::vector<std::string> item_cols(const It& it)
{
    const auto& p = it.getPermissions();
    const auto& s = it.getSaleInfo();
    return {u(it.getUUID()), u(it.getParentUUID()), u(it.getAssetUUID()), u(it.getThumbnailUUID()),
            d((S32)it.getActualType()), d((S32)it.getInventoryType()), x8(it.getFlags()),
            u(p.getCreator()), u(p.getOwner()), u(p.getLastOwner()), u(p.getGroup()),
            x8(p.getMaskBase()), x8(p.getMaskOwner()), x8(p.getMaskGroup()), x8(p.getMaskEveryone()), x8(p.getMaskNextOwner()),
            bl(p.isGroupOwned()), d((S32)s.getSaleType()), d(s.getSalePrice()), d((S64)it.getCreationDate()),
            bl(it.getIsFavorite()), esc(it.getRuntime()), esc(it.getName()), esc(it.getDescription())};
}
template<class C> std::vector<std::string> cat_cols(const C& c)
{
    return {u(c.getUUID()), u(c.getParentUUID()), d((S32)c.getActualType()), d((S32)c.getPreferredType()),
            u(c.getThumbnailUUID()), bl(c.getIsFavorite()), d((S64)c.getCreationDate()), esc(c.getRuntime()), esc(c.getName())};
}
template<class C> std::vector<std::string> vcat_cols(const C& c)
{
    std::vector<std::string> v = cat_cols(c);
    v.push_back(u(c.mOwnerID));
    v.push_back(d(c.mVersion));
    return v;
}
template<class O> std::vector<std::string> obj_cols(const O& o)
{
    return {u(o.getUUID()), u(o.getParentUUID()), d((S32)o.getActualType()), u(o.getThumbnailUUID()),
            bl(o.getIsFavorite()), d((S64)o.getCreationDate()), esc(o.getRuntime()), esc(o.getName())};
}

std::vector<std::string> stream_cols(std::istream& is, const std::string& src)
{
    std::ios::iostate st = is.rdstate();
    std::string flags;
    if (st & std::ios::eofbit) flags += "e";
    if (st & std::ios::failbit) flags += "f";
    if (st & std::ios::badbit) flags += "b";
    if (flags.empty()) flags = "good";
    std::streamoff pos = (std::streamoff)is.rdbuf()->pubseekoff(0, std::ios::cur, std::ios::in);
    std::string rest = (pos >= 0 && (size_t)pos <= src.size()) ? src.substr((size_t)pos) : std::string("?");
    return {flags, d((S64)pos), esc(rest)};
}

void emit_llsd(Sink& s, const std::string& cs, const std::string& field, const LLSD& sd)
{
    emit(s, cs, field + ".bin", {llsd_bin(sd)});
    emit(s, cs, field + ".txt", {llsd_txt(sd)});
}

//============================================================================
// The message system (C9)
//============================================================================

static std::function<void(LLMessageSystem*)>* g_handler = NULL;
static void message_handler(LLMessageSystem* msg, void**)
{
    if (g_handler) (*g_handler)(msg);
}

// LLMessageSystem::wrapBuiltTemplateData (private) gives the bytes the
// template builder built, before zero-coding. The harness reaches it through
// an explicit instantiation, which is exempt from access checking.
struct WrapBuiltTag
{
    typedef LLSD (LLMessageSystem::*type)() const;
};
WrapBuiltTag::type get_member(WrapBuiltTag);
template<typename Tag, typename Tag::type M> struct MemberAccess
{
    friend typename Tag::type get_member(Tag) { return M; }
};
template struct MemberAccess<WrapBuiltTag, &LLMessageSystem::wrapBuiltTemplateData>;

std::vector<U8> built_bytes()
{
    LLMessageSystem* msg = gMessageSystem;
    LLSD w = (msg->*get_member(WrapBuiltTag()))();
    std::vector<U8> b = w["binary-template-data"].asBinary();
    gMessageSystem->clearMessage();
    // The packet header (flags, sequence, extra-header length) is not written
    // by the builder (the sender fills it): zero it.
    for (size_t i = 0; i < b.size() && i < (size_t)LL_PACKET_ID_SIZE; ++i) b[i] = 0;
    return b;
}

// The built message from its message number on (the header is the sender's).
std::string packet_hex(const std::vector<U8>& b)
{
    return b.size() > (size_t)LL_PACKET_ID_SIZE ? hexbytes(b.data() + LL_PACKET_ID_SIZE, b.size() - LL_PACKET_ID_SIZE) : std::string();
}

void dispatch(const char* name, const std::vector<U8>& bytes, std::function<void(LLMessageSystem*)> fn)
{
    g_handler = &fn;
    LLSD message;
    message["body"]["binary-template-data"] = LLSD::Binary(bytes);
    {
        LockMessageChecker lmc(gMessageSystem);
        LLMessageSystem::dispatchTemplate(name, message, LLHTTPNode::ResponsePtr());
    }
    g_handler = NULL;
}

//============================================================================
// Runtime set-up
//============================================================================

void init_runtime()
{
    static bool done = false;
    if (done) return;
    done = true;
    LLError::setDefaultLevel(LLError::LEVEL_ERROR);
    if (!gAPRPoolp)
    {
        ll_init_apr();
    }
    // Port 0: an OS-assigned port, so that concurrent runs never share one.
    start_messaging_system(VKS_MSG_TEMPLATE_FILE, 0, 1, 0, 0, false, "notasharedsecret", NULL, false, 5.f, 100.f);
    if (!gMessageSystem)
    {
        std::cerr << "message system failed to start" << std::endl;
        abort();
    }
    gMessageSystem->setHandlerFuncFast(_PREHASH_BulkUpdateInventory, message_handler);
    gMessageSystem->setHandlerFuncFast(_PREHASH_UpdateInventoryFolder, message_handler);
    gMessageSystem->setHandlerFuncFast(_PREHASH_UpdateInventoryItem, message_handler);
    gMessageSystem->setHandlerFuncFast(_PREHASH_UpdateTaskInventory, message_handler);
    gMessageSystem->setHandlerFuncFast(_PREHASH_ObjectProperties, message_handler);
    static LLTranslationBridge::ptr_t bridge_live = std::make_shared<NullBridge>();
    static LLTranslationBridge::ptr_t bridge_oracle = std::make_shared<NullBridge>();
    ::LLSettingsType::initParamSingleton(bridge_live);
    llinventory_oracle::LLSettingsType::initParamSingleton(bridge_oracle);
    Live::set_opensim(false);
    Oracle::set_opensim(false);
}

//============================================================================
// C1: type tables
//============================================================================

std::vector<S64> c1_values()
{
    std::vector<S64> v;
    for (S64 i = -129; i <= 256; ++i) v.push_back(i);
    v.push_back(INT_MIN);
    v.push_back(INT_MAX);
    return v;
}

std::vector<std::string> c1_names()
{
    std::vector<std::string> n = {
        "", "-1", "invalid", "ensemble", "llassettype_bad_lookup", "llfoldertype_bad_lookup",
        "texture", "sound", "callcard", "landmark", "script", "clothing", "object", "notecard", "category",
        "lsltext", "lslbyte", "txtr_tga", "bodypart", "snd_wav", "img_tga", "jpeg", "animatn", "gesture",
        "simstate", "link", "link_f", "widget", "person", "mesh", "settings", "material", "gltf", "glbin",
        "root", "snapshot", "attach", "wearable", "animation", "root_inv", "trash", "lstndfnd", "favorite",
        "current", "outfit", "my_otfts", "inbox", "outbox", "basic_rt", "merchant", "stock", "version", "suitcase",
        "not", "orig", "copy", "cntn",
        "calling card", "legacy script", "note card", "folder", "lsl2 script", "lsl bytecode", "tga texture",
        "body part", "targa image", "jpeg image", "sym link", "sym folder link", "settings blob", "render material",
        "GLTF", "GLTF binary", "attachment", "root",
        "TEXTURE", "SOUND", "CALLINGCARD", "LANDMARK", "SCRIPT", "CLOTHING", "OBJECT", "NOTECARD", "CATEGORY",
        "LSL_TEXT", "LSL_BYTECODE", "TEXTURE_TGA", "BODYPART", "SOUND_WAV", "IMAGE_TGA", "IMAGE_JPEG", "ANIMATION",
        "GESTURE", "SIMSTATE", "LINK", "FOLDER_LINK", "MESH", "WIDGET", "PERSON", "SETTINGS", "MATERIAL", "GLTF_BIN",
        "UNKNOWN", "NONE", "texture_tga", "Texture", "tExture", "Sound", "Notecard", "Landmark", "NOT", "Copy",
        "texture ", " texture", "texturexx", "landmarks", "objectxxx", "lsltextxx", "unknown", "none", "null",
        "0", "6", "255", "texture\n", "tex", "glbinary", "material ", "\xc3\xa9", "folder\t",
    };
    return n;
}

template<class I> void c1_types(Sink& s)
{
    typedef typename I::AT AT;
    typedef typename I::IT IT;
    typedef typename I::FT FT;
    typedef typename I::Sale Sale;
    typedef typename I::Settings Settings;
    for (S64 v : c1_values())
    {
        const std::string cs = "c1.v." + d(v);
        const typename AT::EType at = static_cast<typename AT::EType>((int)v);
        const char* human = AT::lookupHumanReadable(at);
        emit(s, cs, "at", {cstr(AT::lookup(at)), cstr(human), esc(AT::getDesc(at)), bl(AT::lookupCanLink(at)),
                           bl(AT::lookupIsAssetFetchByIDAllowed(at)), bl(AT::lookupIsAssetIDKnowable(at)),
                           bl(AT::lookupIsLinkType(at))});
        const typename IT::EType it = static_cast<typename IT::EType>((int)v);
        emit(s, cs, "it", {esc(IT::lookup(it)), esc(IT::lookupHumanReadable(it)), d((S32)IT::defaultForAssetType(at)),
                           bl(IT::cannotRestrictPermissions(it)), bl(IT::showInWorldPermissions(it))});
        const typename FT::EType ft = static_cast<typename FT::EType>((int)v);
        emit(s, cs, "ft", {esc(FT::lookup(ft)), bl(FT::lookupIsProtectedType(ft)), bl(FT::lookupIsAutomaticType(ft)),
                           bl(FT::lookupIsSingletonType(ft)), bl(FT::lookupIsEnsembleType(ft)),
                           d((S32)FT::folderTypeToAssetType(ft)), d((S32)FT::assetTypeToFolderType(at))});
        const typename Sale::EForSale st = static_cast<typename Sale::EForSale>((int)v);
        emit(s, cs, "sale", {cstr(Sale::lookup(st))});
        const typename Settings::type_e set = static_cast<typename Settings::type_e>((int)v);
        emit(s, cs, "settings", {d((S32)Settings::getIconName(set)), esc(Settings::getDefaultName(set))});
    }
    // inventory_and_asset_types_match over (-1..=255)^2: one line per inventory
    // type, one character per asset type -1..255.
    for (S32 it = -1; it <= 255; ++it)
    {
        std::string bits;
        bits.reserve(257);
        for (S32 at = -1; at <= 255; ++at)
        {
            bits += I::types_match(it, at) ? '1' : '0';
        }
        emit(s, "c1.match." + d(it), "at-1..255", {bits});
    }
    const std::vector<std::string> names = c1_names();
    for (size_t i = 0; i < names.size(); ++i)
    {
        const std::string& n = names[i];
        emit(s, "c1.name." + d((S64)i), "lookup",
             {esc(n), d((S32)AT::lookup(n)), d((S32)AT::lookup(n.c_str())), d((S32)AT::lookupHumanReadable(n)),
              d((S32)AT::lookupHumanReadable(n.c_str())), d((S32)AT::getType(n)), d((S32)IT::lookup(n)),
              d((S32)FT::lookup(n)), d((S32)Sale::lookup(n.c_str()))});
    }
    emit(s, "c1.name.null", "lookup", {"NULL", d((S32)AT::lookup((const char*)NULL)), d((S32)AT::lookupHumanReadable((const char*)NULL))});
    emit_llsd(s, "c1.typenames", "asset", AT::getTypeNames());
    emit_llsd(s, "c1.typenames", "folder", FT::getTypeNames());
    emit(s, "c1.badlookup", "values", {esc(AT::BADLOOKUP), esc(FT::badLookup())});

    // LLSettingsType::fromInventoryFlags.
    std::vector<U32> flag_values;
    for (U32 f = 0; f < 256; ++f) flag_values.push_back(f);
    for (U32 f : {0x100u, 0x101u, 0x1ffu, 0x1f1102u, 0x7fffffffu, 0x80000002u, 0xffffffffu}) flag_values.push_back(f);
    for (U32 f : flag_values)
    {
        emit(s, "c1.flags." + x8(f), "settings_from_flags", {d((S32)Settings::fromInventoryFlags(f))});
    }

    // LLCategory over the defined domain (top level 0..6; deeper bytes free).
    typedef typename I::Cat Cat;
    for (U32 top = 0; top <= 6; ++top)
    {
        for (U32 b1 : {0u, 1u, 2u, 255u})
        {
            for (U32 b2 : {0u, 1u})
            {
                for (U32 b3 : {0u, 1u})
                {
                    const U32 value = (top << 24) | (b1 << 16) | (b2 << 8) | b3;
                    Cat c(value);
                    std::vector<std::string> sub;
                    for (U32 n : {0u, 1u, 5u, 255u})
                    {
                        sub.push_back(x8(c.getSubCategory((U8)n).getU32()));
                    }
                    emit(s, "c1.cat." + x8(value), "category",
                         {x8(c.getU32()), d(c.getSubCategoryCount()), cstr(c.lookupName()), sub[0], sub[1], sub[2], sub[3]});
                }
            }
        }
    }
    {
        Rng r(0x7631000000000001ULL);
        for (int i = 0; i < 64; ++i)
        {
            const U32 value = (U32)r.next();
            Cat c(value);
            emit(s, "c1.catu32." + x8(value), "category", {x8(c.getU32())});
        }
        Cat none;
        emit(s, "c1.cat.default", "category", {x8(none.getU32()), d(none.getSubCategoryCount()), cstr(none.lookupName())});
        emit(s, "c1.cat.none", "category", {x8(Cat::none.getU32())});
    }

    // Transaction flags.
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            emit(s, "c1.tf.pack." + d(a) + d(b), "flags", {d(I::pack_tf(a != 0, b != 0))});
    {
        std::string src, dst, own;
        for (U32 f = 0; f < 256; ++f)
        {
            src += I::tf_src((U8)f) ? '1' : '0';
            dst += I::tf_dst((U8)f) ? '1' : '0';
            own += I::tf_own((U8)f) ? '1' : '0';
        }
        emit(s, "c1.tf.is", "src0..255", {src});
        emit(s, "c1.tf.is", "dst0..255", {dst});
        emit(s, "c1.tf.is", "own0..255", {own});
    }
    {
        struct TM { S32 amount; LLUUID a, b; const char* name; S32 type; const char* desc; };
        const TM tm[] = {
            {0, ID_A(), ID_B(), "Bob", TRANS_OBJECT_SALE, ""},
            {10, NUL(), ID_B(), "Bob", TRANS_OBJECT_SALE, "desc"},
            {10, ID_A(), NUL(), "", TRANS_GROUP_CREATE, ""},
            {10, ID_A(), NUL(), "", TRANS_GROUP_JOIN, ""},
            {10, ID_A(), NUL(), "", TRANS_UPLOAD_CHARGE, ""},
            {10, ID_A(), NUL(), "", 0, "x"},
            {25, ID_A(), ID_B(), "Bob Resident", TRANS_OBJECT_SALE, "Thing"},
            {25, ID_A(), ID_B(), "Bob Resident", TRANS_OBJECT_SALE, ""},
            {25, ID_A(), ID_B(), "Bob Resident", TRANS_LAND_SALE, ""},
            {25, ID_A(), ID_B(), "Bob Resident", TRANS_LAND_PASS_SALE, ""},
            {25, ID_A(), ID_B(), "Bob Resident", TRANS_GROUP_LAND_DEED, ""},
            {-5, ID_A(), ID_B(), "Bob", 0, "neg"},
        };
        for (size_t i = 0; i < sizeof(tm) / sizeof(tm[0]); ++i)
        {
            const TM& t = tm[i];
            emit(s, "c1.tm." + d((S64)i), "messages",
                 {esc(I::msg_src(t.amount, t.a, t.b, t.name, t.type, t.desc)), esc(I::msg_dst(t.amount, t.a, t.b, t.name, t.type, t.desc))});
        }
    }

    // mask_to_string over the V M C T X alphabet and the edges.
    {
        std::vector<U32> masks;
        for (U32 i = 0; i < 32; ++i)
        {
            U32 m = 0;
            if (i & 1) m |= V_;
            if (i & 2) m |= M_;
            if (i & 4) m |= C_;
            if (i & 8) m |= T_;
            if (i & 16) m |= X_;
            masks.push_back(m);
        }
        for (size_t i = 0; i < kMaskEdgeCount; ++i) masks.push_back(kMaskEdges[i]);
        for (U32 m : masks)
        {
            emit(s, "c1.mask." + x8(m), "mask_to_string", {esc(I::mask_str(m, false)), esc(I::mask_str(m, true))});
        }
    }

    // Constants.
    emit(s, "c1.const", "perm", {x8(PERM_TRANSFER), x8(PERM_MODIFY), x8(PERM_COPY), x8(PERM_EXPORT), x8(PERM_MOVE),
                                 x8(PERM_RESERVED), x8(PERM_NONE), x8(PERM_ALL), x8(PERM_ITEM_UNRESTRICTED),
                                 d(PERM_BASE), d(PERM_OWNER), d(PERM_GROUP), d(PERM_EVERYONE), d(PERM_NEXT_OWNER)});
    emit(s, "c1.const", "counts", {d(AT::AT_COUNT), d(AT::AT_UNKNOWN), d(AT::AT_NONE), d(IT::IT_COUNT), d(IT::IT_UNKNOWN),
                                   d(IT::IT_NONE), d(FT::FT_COUNT), d(FT::FT_NONE), d(Sale::FS_COUNT), d(IT::ICONNAME_COUNT),
                                   d(IT::ICONNAME_SCRIPT_LUAU), d(IT::ICONNAME_INVALID), d(IT::ICONNAME_UNKNOWN), d(IT::ICONNAME_NONE)});
    emit(s, "c1.const", "misc", {d(I::default_price()), d(I::task_item_key()), d(I::task_asset_key()), d(I::max_inventory_buffer()),
                                 d(I::sst(0)), d(I::sst(1))});
    emit(s, "c1.const", "transaction_flags", {d(I::tf_const(0)), d(I::tf_const(1)), d(I::tf_const(2)), d(I::tf_const(3)),
                                              d(I::tf_const(4)), d(I::tf_const(5))});
    emit(s, "c1.const", "item_flags",
         {x8(LLInventoryItemFlags::II_FLAGS_SHARED_SINGLE_REFERENCE), x8(LLInventoryItemFlags::II_FLAGS_LANDMARK_VISITED),
          x8(LLInventoryItemFlags::II_FLAGS_OBJECT_SLAM_PERM), x8(LLInventoryItemFlags::II_FLAGS_OBJECT_SLAM_SALE),
          x8(LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_BASE), x8(LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_OWNER),
          x8(LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_GROUP), x8(LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_EVERYONE),
          x8(LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_NEXT_OWNER), x8(LLInventoryItemFlags::II_FLAGS_OBJECT_HAS_MULTIPLE_ITEMS),
          x8(LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK), x8(LLInventoryItemFlags::II_FLAGS_PERM_OVERWRITE_MASK)});
}

//============================================================================
// C2: normalisers
//============================================================================

std::vector<std::string> c2_inputs()
{
    std::vector<std::string> v;
    for (int b = 0; b < 256; ++b)
    {
        const std::string c(1, (char)b);
        v.push_back(c);
        v.push_back(c + "abc");
        v.push_back("abc" + c);
        v.push_back("ab" + c + "c");
    }
    const std::string pattern = "Abcdefghij klmnopqrst UVWXYZ0123 456789-_+= ";
    auto patterned = [&](size_t n) {
        std::string s;
        for (size_t i = 0; i < n; ++i) s += pattern[i % pattern.size()];
        return s;
    };
    for (size_t n = 0; n <= 70; ++n) v.push_back(patterned(n));
    for (size_t n = 120; n <= 130; ++n) v.push_back(patterned(n));
    for (size_t n = 250; n <= 260; ++n) v.push_back(patterned(n));
    const char* ws[] = {" ", "\t", "\n", "\v", "\f", "\r"};
    for (const char* a : ws)
    {
        for (const char* b : ws)
        {
            v.push_back(std::string(a) + "x y" + b);
            v.push_back(std::string(a) + b + "x" + b + a);
        }
    }
    // 63/64 bytes with spaces around the cut.
    v.push_back(std::string(62, 'a') + " b");
    v.push_back(std::string(63, 'a') + " ");
    v.push_back(" " + std::string(63, 'a'));
    v.push_back(std::string(60, 'a') + "   " + "bbbb");
    v.push_back(std::string(64, ' '));
    v.push_back(std::string(70, '|'));
    // UTF-8 and friends.
    const char* utf[] = {
        "caf\xc3\xa9", "\xc3\xa9t\xc3\xa9", "\xe2\x82\xac" "5", "\xe6\x97\xa5\xe6\x9c\xac", "\xf0\x9f\x98\x80 smile",
        "a\xf0\x9f\x8f\xb3\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x8c\x88z", "\xc3", "\xe2\x82", "\xff\xfe", "\xed\xa0\x80",
        "\xc0\xaf", "\x80", "\xbf\xbf",
    };
    for (const char* x : utf) v.push_back(x);
    v.push_back(std::string(30, '\xc3'));
    v.push_back(std::string("a\0b", 3));
    v.push_back(std::string("\0", 1));
    v.push_back(std::string("name\0", 5));
    for (size_t i = 0; i <= 10; ++i)
    {
        std::string s = "0123456789";
        s.insert(i, "|");
        v.push_back(s);
    }
    v.push_back("|");
    v.push_back("||");
    v.push_back(" | ");
    v.push_back("line1\r\nline2");
    v.push_back("line1\nline2\n");
    v.push_back("\r\n");
    v.push_back("del\x7f");
    v.push_back("\x7f");
    v.push_back("tab\tinside");
    v.push_back(std::string(63, '\xc3') + "x");
    v.push_back(std::string(70, '\x80'));
    return v;
}

template<class I> void c2_normalisers(Sink& s)
{
    typedef typename I::Object Object;
    typedef typename I::Item Item;
    typedef typename I::Category Category;
    const std::vector<std::string> in = c2_inputs();
    for (size_t i = 0; i < in.size(); ++i)
    {
        const std::string& str = in[i];
        const std::string cs = "c2." + d((S64)i);
        std::string name = str;
        Object::correctInventoryName(name);
        std::string desc = str;
        Item::correctInventoryDescription(desc);
        std::string rna = str;
        LLStringUtil::replaceNonstandardASCII(rna, ' ');
        std::string rna_pipe = rna;
        LLStringUtil::replaceChar(rna_pipe, '|', ' ');
        // rename() on an object named "prev": ignored when the corrected name is empty or equal.
        CatF prev;
        prev.name = "prev";
        LLPointer<Object> o = make_obj<I>(prev);
        o->rename(str);
        // The item constructor and setDescription.
        LLPointer<Item> it = new Item(U(0x01), U(0x02), typename I::Perm(), U(0x03), (typename I::AT::EType)6,
                                      (typename I::IT::EType)6, str, str, typename I::Sale(), 0, 0);
        ItemF pf;
        pf.desc = "prev desc";
        LLPointer<Item> it2 = make_item<I>(pf);
        it2->setDescription(str);
        LLPointer<Category> cat = new Category(U(0x01), U(0x02), (typename I::FT::EType)-1, str);
        emit(s, cs, "in", {esc(str)});
        emit(s, cs, "out", {esc(name), esc(desc), esc(rna_pipe), esc(rna), esc(o->getName()), esc(it->getName()),
                            esc(it->getDescription()), esc(it2->getDescription()), esc(cat->getName())});
    }
}

//============================================================================
// C3: permissions
//============================================================================

std::vector<PermF> c3_states()
{
    std::vector<PermF> v;
    PermF p;
    // 0: owned by A, created by C, group G, typical masks.
    p.creator = ID_C(); p.owner = ID_A(); p.last = ID_B(); p.group = ID_G();
    p.base = PERM_ALL; p.owner_mask = PERM_ALL; p.group_mask = 0; p.everyone = 0; p.next = T_ | V_;
    v.push_back(p);
    // 1: no-transfer owner mask.
    p.owner_mask = PERM_ALL & ~T_; p.base = PERM_ALL & ~T_; p.next = C_ | M_ | V_;
    v.push_back(p);
    // 2: group owned (owner null, group G).
    p = PermF(); p.creator = ID_C(); p.owner = NUL(); p.last = ID_A(); p.group = ID_G(); p.group_owned = true;
    p.base = PERM_ALL; p.owner_mask = PERM_ALL; p.group_mask = V_ | C_; p.everyone = C_; p.next = PERM_ALL;
    v.push_back(p);
    // 3: unowned, no group.
    p = PermF();
    v.push_back(p);
    // 4: owner A, last owner null, group null, no-copy base.
    p = PermF(); p.creator = ID_A(); p.owner = ID_A(); p.base = PERM_ALL & ~C_; p.owner_mask = PERM_ALL & ~C_;
    p.group_mask = 0; p.everyone = 0; p.next = 0;
    v.push_back(p);
    // 5: raw everything (unfixed masks, flag inconsistent with ids).
    p = PermF(); p.creator = ID_B(); p.owner = ID_A(); p.last = ID_A(); p.group = ID_H(); p.group_owned = true;
    p.base = 0xffffffff; p.owner_mask = 0x7fff1fff; p.group_mask = 0x80000001; p.everyone = 0x0000e000; p.next = 0;
    v.push_back(p);
    // 6: group G set, owner A, group mask with modify, everyone copy.
    p = PermF(); p.creator = ID_C(); p.owner = ID_A(); p.group = ID_G();
    p.base = PERM_ALL; p.owner_mask = PERM_ALL; p.group_mask = M_ | C_ | V_; p.everyone = C_ | V_; p.next = PERM_ALL;
    v.push_back(p);
    // 7: owner A, base no-transfer, group not owned, everyone copy.
    p.base = PERM_ALL & ~T_; p.owner_mask = PERM_ALL & ~T_;
    v.push_back(p);
    // 8: owner A whose next-owner mask has neither copy nor transfer, so that an owner
    // change with is_atomic adds transfer to the new base (and without it does not).
    p = PermF(); p.creator = ID_C(); p.owner = ID_A(); p.last = ID_B(); p.group = ID_G();
    p.base = PERM_ALL; p.owner_mask = PERM_ALL; p.group_mask = 0; p.everyone = 0; p.next = M_ | V_;
    v.push_back(p);
    return v;
}

static const char* kOpNames[] = {"T", "M", "C", "V", "X", "0", "ALL"};
static const U32 kOps[] = {PERM_TRANSFER, PERM_MODIFY, PERM_COPY, PERM_MOVE, PERM_EXPORT, 0, PERM_ALL};

template<class I> std::vector<std::string> perm_queries(const typename I::Perm& p)
{
    // allowOperationBy for requester {null, A, B, G, H} x group {null, G, H} x op (7).
    std::string allow;
    const LLUUID req[] = {NUL(), ID_A(), ID_B(), ID_G(), ID_H()};
    const LLUUID grp[] = {NUL(), ID_G(), ID_H()};
    for (const LLUUID& r : req)
        for (const LLUUID& g : grp)
            for (U32 op : kOps)
                allow += p.allowOperationBy(op, r, g) ? '1' : '0';
    std::string helpers;
    for (const LLUUID& r : req)
    {
        helpers += p.allowModifyBy(r) ? '1' : '0';
        helpers += p.allowCopyBy(r) ? '1' : '0';
        helpers += p.allowMoveBy(r) ? '1' : '0';
        helpers += p.allowModifyBy(r, ID_G()) ? '1' : '0';
        helpers += p.allowCopyBy(r, ID_G()) ? '1' : '0';
        helpers += p.allowMoveBy(r, ID_G()) ? '1' : '0';
        helpers += p.allowTransferTo(r) ? '1' : '0';
        helpers += p.allowExportBy(r) ? '1' : '0';
    }
    LLUUID owner_id = U(0xee);
    bool is_group = true;
    const bool owned = p.getOwnership(owner_id, is_group);
    return {allow, helpers, x8(p.getCRC32()), bl(owned), u(owner_id), bl(is_group), bl(p.isOwned()), bl(p.isGroupOwned())};
}

template<class I> void c3_permissions(Sink& s)
{
    typedef typename I::Perm Perm;
    // Defaults.
    {
        Perm p;
        emit(s, "c3.default", "perm", perm_cols(p));
        emit(s, "c3.default", "static", perm_cols(Perm::DEFAULT));
    }
    // fix() on structured masks.
    for (size_t bi = 0; bi < kMaskEdgeCount; ++bi)
    {
        for (size_t xi = 0; xi < kMaskEdgeCount; ++xi)
        {
            for (int go = 0; go < 2; ++go)
            {
                PermF f;
                f.base = kMaskEdges[bi];
                f.owner_mask = f.group_mask = f.everyone = f.next = kMaskEdges[xi];
                f.group_owned = go != 0;
                Perm p = make_perm<Perm>(f);
                p.fix();
                emit(s, "c3.fix." + x8(f.base) + "." + x8(f.owner_mask) + "." + d(go), "perm", perm_cols(p));
            }
        }
    }
    // initMasks(5 args) = fixFairUse + fix.
    for (size_t bi = 0; bi < kMaskEdgeCount; ++bi)
    {
        for (size_t ni = 0; ni < kMaskEdgeCount; ++ni)
        {
            Perm p;
            p.initMasks(kMaskEdges[bi], PERM_ALL, PERM_ALL, PERM_ALL, kMaskEdges[ni]);
            emit(s, "c3.initmasks." + x8(kMaskEdges[bi]) + "." + x8(kMaskEdges[ni]), "perm", perm_cols(p));
        }
    }
    // initMasks(inventory type) over -1..30 and 255, from two states.
    const std::vector<PermF> states = c3_states();
    for (S32 t = -1; t <= 30; ++t)
    {
        for (int si : {0, 5})
        {
            Perm p = make_perm<Perm>(states[si]);
            p.initMasks(static_cast<typename I::IT::EType>(t));
            emit(s, "c3.initmasks_type." + d(t) + "." + d(si), "perm", perm_cols(p));
        }
    }
    {
        Perm p = make_perm<Perm>(states[5]);
        p.initMasks(static_cast<typename I::IT::EType>(255));
        emit(s, "c3.initmasks_type.255.5", "perm", perm_cols(p));
    }
    // accumulate over all pairs of states.
    for (size_t a = 0; a < states.size(); ++a)
    {
        for (size_t b = 0; b < states.size(); ++b)
        {
            Perm p = make_perm<Perm>(states[a]);
            p.accumulate(make_perm<Perm>(states[b]));
            emit(s, "c3.accumulate." + d((S64)a) + "." + d((S64)b), "perm", perm_cols(p));
        }
    }
    const LLUUID ids[] = {NUL(), ID_A(), ID_B(), ID_G()};
    // setOwnerAndGroup over the id grid, both atomic forms, from states 0, 1, 2, 3 and 8.
    for (int si : {0, 1, 2, 3, 8})
    {
        for (const LLUUID& agent : ids)
            for (const LLUUID& owner : ids)
                for (const LLUUID& group : ids)
                    for (int atomic = 0; atomic < 2; ++atomic)
                    {
                        Perm p = make_perm<Perm>(states[si]);
                        const bool ret = p.setOwnerAndGroup(agent, owner, group, atomic != 0);
                        std::vector<std::string> cols = perm_cols(p);
                        cols.insert(cols.begin(), bl(ret));
                        emit(s, "c3.soag." + d(si) + "." + idname(agent) + "." + idname(owner) + "." + idname(group) + "." + d(atomic),
                             "ret+perm", cols);
                    }
    }
    // deedToGroup.
    for (int si = 0; si < (int)states.size(); ++si)
    {
        for (const LLUUID& agent : ids)
            for (const LLUUID& group : {NUL(), ID_G(), ID_H()})
            {
                Perm p = make_perm<Perm>(states[si]);
                const bool ret = p.deedToGroup(agent, group);
                std::vector<std::string> cols = perm_cols(p);
                cols.insert(cols.begin(), bl(ret));
                emit(s, "c3.deed." + d(si) + "." + idname(agent) + "." + idname(group), "ret+perm", cols);
            }
    }
    // set*Bits.
    const U32 bits[] = {T_, M_, C_, V_, X_, PERM_ALL};
    const LLUUID agents[] = {NUL(), ID_A(), ID_B()};
    const LLUUID groups[] = {NUL(), ID_G(), ID_H()};
    for (int si : {0, 1, 2})
    {
        for (const LLUUID& agent : agents)
            for (int set = 0; set < 2; ++set)
                for (U32 bit : bits)
                {
                    const std::string tail = d(si) + "." + idname(agent) + "." + d(set) + "." + x8(bit);
                    {
                        Perm p = make_perm<Perm>(states[si]);
                        const bool ret = p.setBaseBits(agent, set != 0, bit);
                        std::vector<std::string> cols = perm_cols(p);
                        cols.insert(cols.begin(), bl(ret));
                        emit(s, "c3.setbase." + tail, "ret+perm", cols);
                    }
                    {
                        Perm p = make_perm<Perm>(states[si]);
                        const bool ret = p.setOwnerBits(agent, set != 0, bit);
                        std::vector<std::string> cols = perm_cols(p);
                        cols.insert(cols.begin(), bl(ret));
                        emit(s, "c3.setowner." + tail, "ret+perm", cols);
                    }
                    for (const LLUUID& group : groups)
                    {
                        const std::string gt = tail + "." + idname(group);
                        {
                            Perm p = make_perm<Perm>(states[si]);
                            const bool ret = p.setGroupBits(agent, group, set != 0, bit);
                            std::vector<std::string> cols = perm_cols(p);
                            cols.insert(cols.begin(), bl(ret));
                            emit(s, "c3.setgroup." + gt, "ret+perm", cols);
                        }
                        {
                            Perm p = make_perm<Perm>(states[si]);
                            const bool ret = p.setEveryoneBits(agent, group, set != 0, bit);
                            std::vector<std::string> cols = perm_cols(p);
                            cols.insert(cols.begin(), bl(ret));
                            emit(s, "c3.seteveryone." + gt, "ret+perm", cols);
                        }
                        {
                            Perm p = make_perm<Perm>(states[si]);
                            const bool ret = p.setNextOwnerBits(agent, group, set != 0, bit);
                            std::vector<std::string> cols = perm_cols(p);
                            cols.insert(cols.begin(), bl(ret));
                            emit(s, "c3.setnext." + gt, "ret+perm", cols);
                        }
                    }
                }
    }
    // Queries, setLastOwner, yesReallySetOwner, set(), operators and the serialized forms of every state.
    for (size_t si = 0; si < states.size(); ++si)
    {
        const std::string cs = "c3.state." + d((S64)si);
        Perm p = make_perm<Perm>(states[si]);
        emit(s, cs, "in", perm_cols(p));
        emit(s, cs, "queries", perm_queries<I>(p));
        if (p.getOwner().notNull() || p.isGroupOwned())
        {
            emit(s, cs, "safe_owner", {u(p.getSafeOwner())});
        }
        {
            Perm q = p;
            q.setLastOwner(ID_H());
            emit(s, cs, "set_last_owner", perm_cols(q));
            Perm r;
            r.set(p);
            emit(s, cs, "set", perm_cols(r));
            emit(s, cs, "eq", {bl(r == p), bl(r != p), bl(q == p), bl(q != p)});
        }
        // Legacy text.
        std::ostringstream os;
        p.exportLegacyStream(os);
        emit(s, cs, "legacy", {esc(os.str())});
        {
            const std::string text = os.str().substr(os.str().find('\n') + 1); // after "\tpermissions 0"
            std::istringstream is(text);
            Perm q = make_perm<Perm>(states[0]);
            const bool ok = q.importLegacyStream(is);
            std::vector<std::string> cols = perm_cols(q);
            cols.insert(cols.begin(), bl(ok));
            emit(s, cs, "legacy.reimport", cols);
            emit(s, cs, "legacy.reimport.stream", stream_cols(is, text));
        }
        // LLSD.
        LLSD sd = I::sd_from_perm(p);
        emit_llsd(s, cs, "llsd", sd);
        emit(s, cs, "llsd.reimport", perm_cols(I::perm_from_sd(sd)));
        {
            LLSD filled;
            filled["preexisting"] = 1;
            I::fill_sd_from_perm(filled, p);
            emit(s, cs, "llsd.fill.bin", {llsd_bin(filled)});
        }
        {
            Perm q = make_perm<Perm>(states[5]);
            q.importLLSD(sd);
            emit(s, cs, "llsd.import_over5", perm_cols(q));
        }
        // The hyphenated LLSD message forms (no caller in the viewer).
        LLSD pm = p.packMessage();
        emit(s, cs, "packmessage.bin", {llsd_bin(pm)});
        Perm q = make_perm<Perm>(states[3]);
        q.unpackMessage(pm);
        emit(s, cs, "unpackmessage", perm_cols(q));
    }
    // importLLSD of non-maps and of the legacy key forms.
    {
        const LLSD non_maps[] = {LLSD(), LLSD(5), LLSD("x"), LLSD::emptyArray()};
        for (size_t i = 0; i < 4; ++i)
        {
            Perm q = make_perm<Perm>(states[5]);
            q.importLLSD(non_maps[i]);
            emit(s, "c3.importllsd.nonmap." + d((S64)i), "perm", perm_cols(q));
        }
    }
    // LLAggregatePermissions: aggregate(mask) sequences of length 0..3 over six masks.
    typedef typename I::Agg Agg;
    const U32 agg_masks[] = {0, T_, M_, C_, T_ | M_ | C_, PERM_ALL};
    auto agg_cols = [](const Agg& a) {
        return std::vector<std::string>{x2(a.getU8()), d(a.getValue(PERM_COPY)), d(a.getValue(PERM_MODIFY)),
                                        d(a.getValue(PERM_TRANSFER)), d(a.getValue(PERM_MOVE)), bl(a.isEmpty())};
    };
    {
        Agg a;
        emit(s, "c3.agg.seq", "agg", agg_cols(a));
        emit(s, "c3.agg.empty", "agg", agg_cols(Agg::empty));
    }
    for (U32 m1 : agg_masks)
    {
        Agg a1;
        a1.aggregate(m1);
        emit(s, "c3.agg.seq." + x8(m1), "agg", agg_cols(a1));
        for (U32 m2 : agg_masks)
        {
            Agg a2 = a1;
            a2.aggregate(m2);
            emit(s, "c3.agg.seq." + x8(m1) + "." + x8(m2), "agg", agg_cols(a2));
            for (U32 m3 : agg_masks)
            {
                Agg a3 = a2;
                a3.aggregate(m3);
                emit(s, "c3.agg.seq." + x8(m1) + "." + x8(m2) + "." + x8(m3), "agg", agg_cols(a3));
            }
        }
    }
}

// The aggregate-of-aggregates table needs aggregates in every U8 state: they
// come from unpackMessage, so it lives with C9.

//----------------------------------------------------------------------------
// C3 exhaustive (in process; digest only)
//----------------------------------------------------------------------------

inline U32 alpha5(U32 i)
{
    U32 m = 0;
    if (i & 1) m |= T_;
    if (i & 2) m |= M_;
    if (i & 4) m |= C_;
    if (i & 8) m |= V_;
    if (i & 16) m |= X_;
    return m;
}
inline U32 alpha4(U32 i)
{
    U32 m = 0;
    if (i & 1) m |= T_;
    if (i & 2) m |= C_;
    if (i & 4) m |= 0x00000001;
    if (i & 8) m |= 0x80000000;
    return m;
}
inline U32 alpha7(U32 i) { return alpha5(i) | ((i & 32) ? 0x00000001u : 0u) | ((i & 64) ? 0x80000000u : 0u); }

// Each exhaustive case appends its result as little-endian U32s to `out`.
template<class I> void c3x_fix(U32 i, int go, int alphabet, U8* out)
{
    typedef typename I::Perm Perm;
    Perm p;
    const U32 bitsz = alphabet == 5 ? 5 : 4;
    const U32 mask = (1u << bitsz) - 1;
    auto a = [&](U32 v) { return alphabet == 5 ? alpha5(v) : alpha4(v); };
    p.setMaskBase(a(i & mask));
    p.setMaskOwner(a((i >> bitsz) & mask));
    p.setMaskGroup(a((i >> (2 * bitsz)) & mask));
    p.setMaskEveryone(a((i >> (3 * bitsz)) & mask));
    p.setMaskNext(a((i >> (4 * bitsz)) & mask));
    p.yesReallySetOwner(NUL(), go != 0);
    p.fix();
    const U32 r[5] = {p.getMaskBase(), p.getMaskOwner(), p.getMaskGroup(), p.getMaskEveryone(), p.getMaskNextOwner()};
    memcpy(out, r, 20);
}
template<class I> void c3x_fair(U32 i, U8* out)
{
    // base and next over the 7-bit alphabet, owner/everyone/group over five values.
    static const U32 others[5] = {PERM_ALL, 0, T_ | C_, M_ | V_, 0xffffffff};
    typedef typename I::Perm Perm;
    Perm p;
    const U32 b = alpha7(i & 127), n = alpha7((i >> 7) & 127);
    const U32 o = others[(i >> 14) % 5], e = others[((i >> 14) / 5) % 5], g = others[((i >> 14) / 25) % 5];
    p.initMasks(b, o, e, g, n);
    const U32 r[5] = {p.getMaskBase(), p.getMaskOwner(), p.getMaskGroup(), p.getMaskEveryone(), p.getMaskNextOwner()};
    memcpy(out, r, 20);
}
static const U32 C3X_FAIR_CASES = 128u * 128u * 125u;

// A stream's digest in chunks of `chunk` cases, named "<stream>@<first>-<end>"
// (the chunks of a sampled stream start at its sampled cases), and in total,
// named "<stream>". Entries: name, cases, lines (records), SHA-256.
struct ChunkedDigest
{
    std::string name;
    U64 chunk;
    Sha256 total, part;
    U64 cases = 0, lines = 0, part_lines = 0, start = 0;
    std::vector<std::vector<std::string> > entries;

    ChunkedDigest(const std::string& n, U64 c) : name(n), chunk(c) {}
    void begin_case(U64 i)
    {
        if (i != 0 && i % chunk == 0) flush(i);
        cases = i + 1;
    }
    void bytes(const void* p, size_t n)
    {
        total.update(p, n);
        part.update(p, n);
    }
    void line(const std::string& l)
    {
        bytes(l.data(), l.size());
        bytes("\n", 1);
        ++lines;
        ++part_lines;
    }
    void record(const U8* p, size_t n)
    {
        bytes(p, n);
        ++lines;
        ++part_lines;
    }
    void flush(U64 end)
    {
        entries.push_back({name + "@" + d((S64)start) + "-" + d((S64)end), d((S64)(end - start)), d((S64)part_lines), part.hex()});
        part = Sha256();
        part_lines = 0;
        start = end;
    }
    void finish()
    {
        flush(cases);
        entries.insert(entries.begin(), std::vector<std::string>{name, d((S64)cases), d((S64)lines), total.hex()});
    }
};

static const U64 EXHAUSTIVE_CHUNK = 1ull << 20;

struct ExhaustiveResult
{
    std::string name;
    U64 cases = 0;
    std::vector<std::vector<std::string> > digests; // ChunkedDigest entries
    U64 diffs = 0;
    std::string first_diff;
};

static const char* kExhaustiveNames[] = {"c3x.fix.alpha5.go0", "c3x.fix.alpha4.go0", "c3x.fix.alpha5.go1", "c3x.fix.alpha4.go1",
                                         "c3x.initmasks5"};

// Runs the exhaustive sweeps. With `compare`, compares live with oracle and digests
// the live output (the test); without, digests the oracle output (the dump).
std::vector<ExhaustiveResult> run_c3_exhaustive(bool compare)
{
    std::vector<ExhaustiveResult> results;
    auto run = [&](const std::string& name, U64 n, const std::function<void(U64, U8*)>& fo,
                   const std::function<void(U64, U8*)>& fl) {
        ExhaustiveResult r;
        r.name = name;
        ChunkedDigest cd(name, EXHAUSTIVE_CHUNK);
        U8 a[20], b[20];
        for (U64 i = 0; i < n; ++i)
        {
            cd.begin_case(i);
            fo(i, a);
            if (compare)
            {
                fl(i, b);
                cd.record(b, 20);
                if (memcmp(a, b, 20) != 0)
                {
                    if (!r.diffs)
                    {
                        r.first_diff = "case " + d((S64)i) + ": expected " + hexbytes(a, 20) + " actual " + hexbytes(b, 20);
                    }
                    ++r.diffs;
                }
            }
            else
            {
                cd.record(a, 20);
            }
        }
        cd.finish();
        r.cases = n;
        r.digests = cd.entries;
        results.push_back(r);
    };
    for (int go = 0; go < 2; ++go)
    {
        run("c3x.fix.alpha5.go" + d(go), 1ull << 25,
            [go](U64 i, U8* o) { c3x_fix<Oracle>((U32)i, go, 5, o); },
            [go](U64 i, U8* o) { c3x_fix<Live>((U32)i, go, 5, o); });
        run("c3x.fix.alpha4.go" + d(go), 1ull << 20,
            [go](U64 i, U8* o) { c3x_fix<Oracle>((U32)i, go, 4, o); },
            [go](U64 i, U8* o) { c3x_fix<Live>((U32)i, go, 4, o); });
    }
    run("c3x.initmasks5", C3X_FAIR_CASES,
        [](U64 i, U8* o) { c3x_fair<Oracle>((U32)i, o); },
        [](U64 i, U8* o) { c3x_fair<Live>((U32)i, o); });
    return results;
}

//----------------------------------------------------------------------------
// C3 random
//----------------------------------------------------------------------------

LLUUID draw_id(Rng& r)
{
    switch (r.below(4))
    {
    case 0: return NUL();
    case 1: return ID_A();
    case 2: return ID_B();
    default: return ID_G();
    }
}
U32 draw_mask(Rng& r)
{
    const U64 v = r.next();
    if (v & 1) return kMaskEdges[(v >> 1) % kMaskEdgeCount];
    return (U32)(v >> 32);
}
PermF draw_perm(Rng& r)
{
    PermF f;
    f.creator = draw_id(r);
    f.owner = draw_id(r);
    f.last = draw_id(r);
    f.group = draw_id(r);
    f.base = draw_mask(r);
    f.owner_mask = draw_mask(r);
    f.group_mask = draw_mask(r);
    f.everyone = draw_mask(r);
    f.next = draw_mask(r);
    f.group_owned = r.coin();
    return f;
}
static const U32 kDrawBits[] = {T_, M_, C_, V_, X_, 0x00000001, 0x80000000, T_ | M_ | C_, PERM_ALL, 0xffffffff};

struct C3In
{
    PermF st;
    U32 op = 0;
    PermF other;
    LLUUID a1, a2, a3;
    bool flag = false;
    U32 bits = 0;
    U32 masks[5] = {0, 0, 0, 0, 0};
};
static const U32 C3_OPS = 12;
static const char* kC3OpNames[C3_OPS] = {"accumulate", "setOwnerAndGroup", "deedToGroup", "setBaseBits", "setOwnerBits",
                                         "setGroupBits", "setEveryoneBits", "setNextOwnerBits", "initMasks5", "fix",
                                         "importLLSD", "legacy"};

C3In draw_c3(Rng& r)
{
    C3In in;
    in.st = draw_perm(r);
    in.op = r.below(C3_OPS);
    switch (in.op)
    {
    case 0: in.other = draw_perm(r); break;
    case 1: in.a1 = draw_id(r); in.a2 = draw_id(r); in.a3 = draw_id(r); in.flag = r.coin(); break;
    case 2: in.a1 = draw_id(r); in.a2 = draw_id(r); break;
    case 3: case 4: in.a1 = draw_id(r); in.flag = r.coin(); in.bits = kDrawBits[r.below(10)]; break;
    case 5: case 6: case 7: in.a1 = draw_id(r); in.a2 = draw_id(r); in.flag = r.coin(); in.bits = kDrawBits[r.below(10)]; break;
    case 8: for (int k = 0; k < 5; ++k) in.masks[k] = draw_mask(r); break;
    default: break;
    }
    return in;
}

template<class I> void c3r_case(U64 i, const C3In& in, Sink& s)
{
    typedef typename I::Perm Perm;
    const std::string cs = "c3r." + d((S64)i);
    Perm p = make_perm<Perm>(in.st);
    emit(s, cs, "in", perm_cols(p));
    bool ret = true;
    std::vector<std::string> args;
    switch (in.op)
    {
    case 0:
    {
        Perm o = make_perm<Perm>(in.other);
        args = perm_cols(o);
        p.accumulate(o);
        break;
    }
    case 1: args = {idname(in.a1), idname(in.a2), idname(in.a3), bl(in.flag)}; ret = p.setOwnerAndGroup(in.a1, in.a2, in.a3, in.flag); break;
    case 2: args = {idname(in.a1), idname(in.a2)}; ret = p.deedToGroup(in.a1, in.a2); break;
    case 3: args = {idname(in.a1), bl(in.flag), x8(in.bits)}; ret = p.setBaseBits(in.a1, in.flag, in.bits); break;
    case 4: args = {idname(in.a1), bl(in.flag), x8(in.bits)}; ret = p.setOwnerBits(in.a1, in.flag, in.bits); break;
    case 5: args = {idname(in.a1), idname(in.a2), bl(in.flag), x8(in.bits)}; ret = p.setGroupBits(in.a1, in.a2, in.flag, in.bits); break;
    case 6: args = {idname(in.a1), idname(in.a2), bl(in.flag), x8(in.bits)}; ret = p.setEveryoneBits(in.a1, in.a2, in.flag, in.bits); break;
    case 7: args = {idname(in.a1), idname(in.a2), bl(in.flag), x8(in.bits)}; ret = p.setNextOwnerBits(in.a1, in.a2, in.flag, in.bits); break;
    case 8:
        args = {x8(in.masks[0]), x8(in.masks[1]), x8(in.masks[2]), x8(in.masks[3]), x8(in.masks[4])};
        p.initMasks(in.masks[0], in.masks[1], in.masks[2], in.masks[3], in.masks[4]);
        break;
    case 9: p.fix(); break;
    case 10:
    {
        LLSD sd = I::sd_from_perm(p);
        args = {llsd_bin(sd)};
        p = I::perm_from_sd(sd);
        break;
    }
    default:
    {
        std::ostringstream os;
        p.exportLegacyStream(os);
        args = {esc(os.str())};
        std::istringstream is(os.str().substr(os.str().find('\n') + 1));
        Perm q;
        ret = q.importLegacyStream(is);
        p = q;
        break;
    }
    }
    args.insert(args.begin(), kC3OpNames[in.op]);
    emit(s, cs, "op", args);
    std::vector<std::string> out = perm_cols(p);
    out.insert(out.begin(), bl(ret));
    emit(s, cs, "out", out);
    emit(s, cs, "queries", perm_queries<I>(p));
}

//============================================================================
// Guarded legacy export (a sale type outside 0..3)
//============================================================================

// LLSaleInfo::exportLegacyStream streams lookup()'s NULL for a sale type outside
// 0..3. libstdc++ sets badbit|failbit and drops every later write; the MSVC runtime
// dereferences the null pointer. The harness runs such exports under a structured
// exception handler on Windows so that the crash is observed, and records the bytes
// written before it.
struct GuardedWrite
{
    std::function<void(std::ostream&)> fn;
    std::ostringstream os;
};
static void guarded_thunk(void* p)
{
    GuardedWrite* g = (GuardedWrite*)p;
    g->fn(g->os);
}
#if LL_WINDOWS
static int run_guarded(void (*fn)(void*), void* ctx)
{
    __try
    {
        fn(ctx);
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 1;
    }
}
#else
static int run_guarded(void (*fn)(void*), void* ctx)
{
    fn(ctx);
    return 0;
}
#endif

// The bytes written and the outcome: "crash", or the stream state ("good" or e/f/b letters).
std::pair<std::string, std::string> guarded_export(const std::function<void(std::ostream&)>& fn)
{
    GuardedWrite g;
    g.fn = fn;
    const bool crashed = run_guarded(&guarded_thunk, &g) != 0;
    const std::string out = g.os.str();
    if (crashed) return {out, "crash"};
    const std::ios::iostate st = g.os.rdstate();
    std::string flags;
    if (st & std::ios::eofbit) flags += "e";
    if (st & std::ios::failbit) flags += "f";
    if (st & std::ios::badbit) flags += "b";
    return {out, flags.empty() ? std::string("good") : flags};
}

//============================================================================
// C4: sale info
//============================================================================

static const S32 kSaleTypes[] = {0, 1, 2, 3, -1, 4, 200, 255};
static const S32 kPrices[] = {INT_MIN, -1, 0, 10, INT_MAX};

template<class I> void c4_sale(Sink& s)
{
    typedef typename I::Sale Sale;
    {
        Sale def;
        emit(s, "c4.default", "sale", sale_cols(def));
        emit(s, "c4.default", "static", sale_cols(Sale::DEFAULT));
        emit(s, "c4.default", "forsale", {bl(def.isForSale())});
    }
    for (S32 t : kSaleTypes)
    {
        for (S32 p : kPrices)
        {
            const std::string cs = "c4.tp." + d(t) + "." + d(p);
            Sale raw = make_sale<Sale>(t, p);
            emit(s, cs, "raw", sale_cols(raw));
            emit(s, cs, "crc", {x8(raw.getCRC32()), bl(raw.isForSale())});
            Sale ctor(static_cast<typename Sale::EForSale>(t), p);
            emit(s, cs, "ctor", sale_cols(ctor));
            Sale setp;
            setp.setSaleType(static_cast<typename Sale::EForSale>(t));
            setp.setSalePrice(p);
            emit(s, cs, "setters", sale_cols(setp));
            // Legacy text (a sale type outside 0..3 streams a null name: runtime edge, not recorded here).
            if (t >= 0 && t <= 3)
            {
                std::ostringstream os;
                raw.exportLegacyStream(os);
                emit(s, cs, "legacy", {esc(os.str())});
                const std::string text = os.str().substr(os.str().find('\n') + 1);
                std::istringstream is(text);
                Sale q = make_sale<Sale>(1, 77);
                bool has = true;
                U32 pm = 0xa5a5a5a5;
                const bool ok = q.importLegacyStream(is, has, pm);
                std::vector<std::string> cols = sale_cols(q);
                cols.push_back(bl(ok));
                cols.push_back(bl(has));
                cols.push_back(x8(pm));
                emit(s, cs, "legacy.reimport", cols);
                emit(s, cs, "legacy.reimport.stream", stream_cols(is, text));
            }
            else
            {
                // A sale type outside 0..3: "not" since bug 1 (913990ebe3); still run guarded,
                // so that a regression to the null name is reported, not fatal on Windows.
                const auto r = guarded_export([&](std::ostream& os) { raw.exportLegacyStream(os); });
                emit(s, cs, "legacy", {esc(r.first)});
                emit(s, cs, "legacy.outcome", {r.second});
            }
            LLSD sd = raw.asLLSD();
            emit_llsd(s, cs, "llsd", sd);
            LLSD conv = raw; // operator LLSD
            emit(s, cs, "llsd.operator.bin", {llsd_bin(conv)});
            {
                Sale q = make_sale<Sale>(1, 77);
                bool has = false;
                U32 pm = 0xa5a5a5a5;
                const bool ok = q.fromLLSD(sd, has, pm);
                std::vector<std::string> cols = sale_cols(q);
                cols.push_back(bl(ok));
                cols.push_back(bl(has));
                cols.push_back(x8(pm));
                emit(s, cs, "llsd.reimport", cols);
            }
            LLSD csd = I::sd_from_sale(raw);
            emit(s, cs, "sdcreate.bin", {llsd_bin(csd)});
            emit(s, cs, "sdcreate.reimport", sale_cols(I::sale_from_sd(csd)));
            LLSD pm = raw.packMessage();
            emit(s, cs, "packmessage.bin", {llsd_bin(pm)});
            Sale q = make_sale<Sale>(1, 77);
            q.unpackMessage(pm);
            emit(s, cs, "unpackmessage", sale_cols(q));
        }
    }
    // accumulate (signed overflow of the price: runtime edge).
    const std::pair<S32, S32> acc[] = {{0, 10}, {1, 20}, {1, 1}, {2, 0}, {3, INT_MAX}, {200, 5}, {1, INT_MAX}, {0, INT_MIN}, {0, -1}};
    for (size_t a = 0; a < sizeof(acc) / sizeof(acc[0]); ++a)
    {
        for (size_t b = 0; b < sizeof(acc) / sizeof(acc[0]); ++b)
        {
            Sale x = make_sale<Sale>(acc[a].first, acc[a].second);
            x.accumulate(make_sale<Sale>(acc[b].first, acc[b].second));
            Sale y = make_sale<Sale>(acc[b].first, acc[b].second);
            emit(s, "c4.acc." + d((S64)a) + "." + d((S64)b), "sale",
                 {d((S32)x.getSaleType()), d(x.getSalePrice()), bl(x == y), bl(x != y)});
        }
    }
    // fromLLSD over value types.
    struct SV { const char* name; LLSD v; };
    LLSD::Binary b0, b3 = {1, 2, 3}, b4 = {0x80, 0, 0x40, 0x01}, b5 = {0x00, 0x00, 0xe0, 0x00, 0xff};
    const std::vector<SV> types = {
        {"absent", LLSD()}, {"undef", LLSD()}, {"str.not", "not"}, {"str.orig", "orig"}, {"str.copy", "copy"},
        {"str.cntn", "cntn"}, {"str.junk", "junk"}, {"str.empty", ""}, {"str.COPY", "COPY"}, {"int.2", 2}, {"int.-1", -1},
        {"int.255", 255}, {"int.256", 256}, {"int.258", 258}, {"real.2.7", 2.7}, {"bool.true", true},
        {"uuid", LLSD(U(0x55))}, {"bin4", LLSD(b4)}, {"map", LLSD::emptyMap()}, {"array", LLSD::emptyArray()},
    };
    const std::vector<SV> prices = {
        {"absent", LLSD()}, {"int.50", 50}, {"int.-5", -5}, {"int.max", INT_MAX}, {"int.min", INT_MIN}, {"real.12.9", 12.9},
        {"real.-3.5", -3.5}, {"real.1e12", 1e12}, {"str.42", "42"}, {"str.junk", "junk"}, {"str.-7", "-7"}, {"bool", true},
        {"bin4", LLSD(b4)},
    };
    const std::vector<SV> pms = {
        {"absent", LLSD()}, {"bin0", LLSD(b0)}, {"bin3", LLSD(b3)}, {"bin4", LLSD(b4)}, {"bin5", LLSD(b5)},
        {"int", 0x8000}, {"str", "8000"}, {"undef", LLSD()},
    };
    for (size_t ti = 0; ti < types.size(); ++ti)
    {
        for (size_t pi = 0; pi < prices.size(); ++pi)
        {
            LLSD sd = LLSD::emptyMap();
            if (ti != 0) sd["sale_type"] = types[ti].v;
            if (pi != 0) sd["sale_price"] = prices[pi].v;
            Sale q = make_sale<Sale>(2, 77);
            bool has = false;
            U32 pm = 0xa5a5a5a5;
            q.fromLLSD(sd, has, pm);
            std::vector<std::string> cols = sale_cols(q);
            cols.push_back(bl(has));
            cols.push_back(x8(pm));
            emit(s, std::string("c4.fromllsd.") + types[ti].name + "." + prices[pi].name, "sale+perm_mask", cols);
            emit(s, std::string("c4.fromllsd.") + types[ti].name + "." + prices[pi].name, "sdfrom",
                 sale_cols(I::sale_from_sd(sd)));
        }
    }
    for (size_t mi = 0; mi < pms.size(); ++mi)
    {
        LLSD sd = LLSD::emptyMap();
        sd["sale_type"] = "copy";
        sd["sale_price"] = 5;
        if (mi == 7) sd["perm_mask"] = LLSD();
        else if (mi != 0) sd["perm_mask"] = pms[mi].v;
        Sale q = make_sale<Sale>(2, 77);
        bool has = false;
        U32 pm = 0xa5a5a5a5;
        q.fromLLSD(sd, has, pm);
        std::vector<std::string> cols = sale_cols(q);
        cols.push_back(bl(has));
        cols.push_back(x8(pm));
        emit(s, std::string("c4.permmask.") + pms[mi].name, "sale+perm_mask", cols);
    }
    {
        // Non-map input.
        const LLSD non_maps[] = {LLSD(), LLSD(3), LLSD("copy"), LLSD::emptyArray()};
        for (size_t i = 0; i < 4; ++i)
        {
            Sale q = make_sale<Sale>(2, 77);
            bool has = false;
            U32 pm = 0xa5a5a5a5;
            q.fromLLSD(non_maps[i], has, pm);
            std::vector<std::string> cols = sale_cols(q);
            cols.push_back(bl(has));
            cols.push_back(x8(pm));
            emit(s, "c4.fromllsd.nonmap." + d((S64)i), "sale+perm_mask", cols);
        }
    }
}

struct C4In
{
    S32 type1 = 0, price1 = 0, type2 = 0, price2 = 0;
    U32 op = 0;
    U32 sd_kind = 0;
};
S32 draw_sale_type(Rng& r)
{
    static const S32 t[] = {0, 1, 2, 3, 0, 1, 2, 3, -1, 4, 200, 255, 127, -128};
    return t[r.below(14)];
}
S32 draw_price(Rng& r)
{
    const U64 v = r.next();
    if (v & 1) return kPrices[(v >> 1) % 5];
    if (v & 2) return (S32)((v >> 8) % 100000);
    return (S32)(U32)(v >> 32);
}
C4In draw_c4(Rng& r)
{
    C4In in;
    in.type1 = draw_sale_type(r);
    in.price1 = draw_price(r);
    in.op = r.below(4);
    in.type2 = draw_sale_type(r);
    in.price2 = draw_price(r);
    in.sd_kind = r.below(3);
    return in;
}
template<class I> void c4r_case(U64 i, const C4In& in, Sink& s)
{
    typedef typename I::Sale Sale;
    const std::string cs = "c4r." + d((S64)i);
    Sale a = make_sale<Sale>(in.type1, in.price1);
    emit(s, cs, "in", {d(in.type1), d(in.price1), d((S64)in.op), d(in.type2), d(in.price2), d((S64)in.sd_kind)});
    emit(s, cs, "crc", {x8(a.getCRC32()), bl(a.isForSale())});
    switch (in.op)
    {
    case 0:
    {
        // accumulate; a sum outside S32 is a runtime edge (signed overflow), not drawn.
        const S64 sum = (S64)in.price1 + (S64)in.price2;
        if (sum >= INT_MIN && sum <= INT_MAX)
        {
            Sale b = make_sale<Sale>(in.type2, in.price2);
            a.accumulate(b);
            emit(s, cs, "accumulate", sale_cols(a));
        }
        else
        {
            emit(s, cs, "accumulate", {"overflow"});
        }
        break;
    }
    case 1:
    {
        LLSD sd = a.asLLSD();
        emit(s, cs, "llsd.bin", {llsd_bin(sd)});
        Sale q = make_sale<Sale>(in.type2, in.price2);
        bool has = false;
        U32 pm = 0;
        q.fromLLSD(sd, has, pm);
        emit(s, cs, "llsd.reimport", sale_cols(q));
        break;
    }
    case 2:
    {
        LLSD sd = LLSD::emptyMap();
        if (in.sd_kind == 0) sd["sale_type"] = in.type2;
        if (in.sd_kind == 1) sd["sale_type"] = std::string(in.type2 == 1 ? "orig" : in.type2 == 2 ? "copy" : in.type2 == 3 ? "cntn" : "x");
        sd["sale_price"] = in.price2;
        Sale q = make_sale<Sale>(in.type1, in.price1);
        bool has = false;
        U32 pm = 0;
        q.fromLLSD(sd, has, pm);
        emit(s, cs, "fromllsd", sale_cols(q));
        break;
    }
    default:
    {
        Sale q;
        q.setSaleType(static_cast<typename Sale::EForSale>(in.type2));
        q.setSalePrice(in.price2);
        emit(s, cs, "setters", {d((S32)q.getSaleType()), d(q.getSalePrice()), bl(q == a), bl(q != a), x8(q.getCRC32())});
        break;
    }
    }
}

//============================================================================
// C5: items and categories
//============================================================================

static const S32 kAssetTypes[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 17, 18, 19, 20, 21, 22, 24, 25, 40, 45, 49,
                                  56, 57, 58, 59, 255, 26, 60, 127, -2, 50, 9};
static const S32 kInvTypes[] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 15, 17, 18, 19, 20, 22, 23, 24, 25, 26, 27, 28, -1, 255, 4, 5, 11, 29};
static const U32 kBaseMasks[] = {PERM_ALL, 0x7fffbfff, 0x7fff7fff, 0x7fffdfff, 0x7fff5fff, 0x00000000, 0x0000e000};
static const U32 kFlags[] = {0, 1, 0xff, 0x100, 0x1f1100, 0x7fffffff, 0xffffffff};
static const S64 kDates[] = {0, 1, INT_MAX, INT_MIN, 2147483648LL, 4294967301LL, -1};

ItemF base_item()
{
    ItemF f;
    f.id = U(0xa1);
    f.parent = U(0xa2);
    f.asset = U(0xa3);
    f.type = 6;
    f.inv_type = 6;
    f.flags = 0;
    f.perm.creator = ID_C();
    f.perm.owner = ID_A();
    f.perm.last = ID_B();
    f.perm.group = ID_G();
    f.perm.base = PERM_ALL;
    f.perm.owner_mask = PERM_ALL;
    f.perm.group_mask = 0;
    f.perm.everyone = 0;
    f.perm.next = T_ | V_;
    f.sale_type = 0;
    f.sale_price = 10;
    f.date = 1700000000;
    f.name = "Base item";
    f.desc = "Base description";
    return f;
}

// The "previous" item used for "absent keeps" (is_new = false) and for the
// slam-bit comparison.
ItemF prev_item()
{
    ItemF f;
    f.id = U(0x11);
    f.parent = U(0x12);
    f.asset = U(0x13);
    f.thumb = U(0x14);
    f.type = 7;
    f.inv_type = 7;
    f.flags = 0x00000101;
    f.perm.creator = U(0x21);
    f.perm.owner = U(0x22);
    f.perm.last = U(0x23);
    f.perm.group = U(0x24);
    f.perm.base = PERM_ALL;
    f.perm.owner_mask = 0x0008e000;
    f.perm.group_mask = 0x00002000;
    f.perm.everyone = 0;
    f.perm.next = 0x00082000;
    f.sale_type = 2;
    f.sale_price = 25;
    f.date = 1234567890;
    f.fav = true;
    f.runtime = "pre-runtime";
    f.name = "pre name";
    f.desc = "pre desc";
    return f;
}

std::vector<std::pair<std::string, std::string> > c5_names()
{
    return {
        {"", ""},
        {"Plain name", "Plain description"},
        {"pipe|name", "pipe|desc"},
        {"new\nline", "new\nline"},
        {"caf\xc3\xa9 \xf0\x9f\x98\x80", "\xe6\x97\xa5\xe6\x9c\xac"},
        {std::string(63, 'n'), std::string(127, 'd')},
        {std::string(64, 'n'), std::string(128, 'd')},
        {std::string(255, 'n'), std::string(300, 'd')},
        {"  leading and trailing  ", "  leading and trailing  "},
        {"tab\there", "tab\there"},
        {"cr\r", "cr\r"},
        {"del\x7f", "del\x7f"},
        {"\x01\x02ctrl", "\x1f" "ctrl"},
        {"|", "|"},
        {"name with spaces", ""},
        {"<llsd>", "</llsd>"},
    };
}

struct ItemCase
{
    std::string id;
    ItemF f;
};

std::vector<ItemCase> c5_item_cases()
{
    std::vector<ItemCase> v;
    const ItemF b = base_item();
    // Every asset type with its default inventory type, every inventory type on an object asset.
    for (S32 at : kAssetTypes)
    {
        ItemF f = b;
        f.type = at;
        f.inv_type = (at >= 0 && at < 60) ? (S32)Oracle::IT::defaultForAssetType((Oracle::AT::EType)at) : 255;
        v.push_back({"c5.at." + d(at), f});
    }
    for (S32 it : kInvTypes)
    {
        ItemF f = b;
        f.inv_type = it;
        v.push_back({"c5.it." + d(it), f});
    }
    // Masks x asset x type.
    const LLUUID assets[] = {NUL(), U(0xa3), MAGIC(), NOT_MAGIC()};
    const char* asset_names[] = {"null", "a3", "magic", "notmagic"};
    const S32 types[][2] = {{6, 6}, {7, 7}, {3, 3}, {10, 10}};
    for (U32 m : kBaseMasks)
        for (int ai = 0; ai < 4; ++ai)
            for (const auto& t : types)
            {
                ItemF f = b;
                f.perm.base = m;
                f.asset = assets[ai];
                f.type = t[0];
                f.inv_type = t[1];
                v.push_back({"c5.mask." + x8(m) + "." + asset_names[ai] + "." + d(t[0]), f});
            }
    // Thumbnail x favorite x runtime x type.
    const std::string runtimes[] = {"", "luau", std::string(60, 'r'), "lu au"};
    for (int th = 0; th < 2; ++th)
        for (int fv = 0; fv < 2; ++fv)
            for (int ri = 0; ri < 4; ++ri)
                for (S32 t : {10, 7})
                {
                    ItemF f = b;
                    f.thumb = th ? U(0xb1) : NUL();
                    f.fav = fv != 0;
                    f.runtime = runtimes[ri];
                    f.type = t;
                    f.inv_type = t;
                    v.push_back({"c5.meta." + d(th) + d(fv) + d(ri) + "." + d(t), f});
                }
    // Flags x type.
    const S32 ftypes[][2] = {{6, 6}, {5, 18}, {56, 25}};
    for (U32 fl : kFlags)
        for (const auto& t : ftypes)
        {
            ItemF f = b;
            f.flags = fl;
            f.type = t[0];
            f.inv_type = t[1];
            v.push_back({"c5.flags." + x8(fl) + "." + d(t[0]), f});
        }
    // Sale.
    for (S32 st = 0; st <= 3; ++st)
        for (S32 p : kPrices)
        {
            ItemF f = b;
            f.sale_type = st;
            f.sale_price = p;
            v.push_back({"c5.sale." + d(st) + "." + d(p), f});
        }
    for (S32 st : {-1, 4, 200, 255})
    {
        ItemF f = b;
        f.sale_type = st;
        v.push_back({"c5.sale." + d(st) + ".10", f});
    }
    // Names and descriptions.
    const auto names = c5_names();
    for (size_t i = 0; i < names.size(); ++i)
    {
        ItemF f = b;
        f.name = names[i].first;
        f.desc = names[i].second;
        v.push_back({"c5.name." + d((S64)i), f});
    }
    // Creation dates.
    for (S64 dt : kDates)
    {
        ItemF f = b;
        f.date = dt;
        v.push_back({"c5.date." + d(dt), f});
    }
    // Permission ids and group ownership.
    {
        ItemF f = b;
        f.perm.owner = NUL();
        f.perm.group_owned = true;
        v.push_back({"c5.groupowned", f});
        f = b;
        f.perm = PermF();
        v.push_back({"c5.perm.default", f});
        f = b;
        f.perm.group_mask = PERM_ALL;
        f.perm.everyone = PERM_ALL;
        f.perm.next = PERM_ALL;
        v.push_back({"c5.perm.unfixed", f});
    }
    // Links.
    {
        ItemF f = b;
        f.type = 24;
        f.inv_type = -1;
        f.asset = U(0xc5);
        v.push_back({"c5.link", f});
        f.type = 25;
        f.inv_type = 8;
        v.push_back({"c5.folderlink", f});
    }
    return v;
}

template<class I> void c5_item_record(const std::string& cs, const ItemF& f, Sink& s)
{
    typedef typename I::Item Item;
    typedef typename I::Object Object;
    LLPointer<Item> item = make_item<I>(f);
    emit(s, cs, "in", item_cols(*item));
    const bool sale_ok = f.sale_type >= 0 && f.sale_type <= 3;
    // Legacy text.
    if (sale_ok)
    {
        std::ostringstream key, nokey;
        const bool r1 = item->exportLegacyStream(key, true);
        const bool r2 = item->exportLegacyStream(nokey, false);
        emit(s, cs, "legacy", {esc(key.str())});
        // Without the asset key only the asset line differs: record that line and
        // whether the rest is byte-identical.
        {
            const std::string k = key.str(), n = nokey.str();
            auto asset_line = [](const std::string& t, size_t& a, size_t& b) {
                a = t.find("\t\tasset_id\t");
                if (a == std::string::npos) a = t.find("\t\tshadow_id\t");
                b = (a == std::string::npos) ? std::string::npos : t.find('\n', a) + 1;
            };
            size_t ka, kb, na, nb;
            asset_line(k, ka, kb);
            asset_line(n, na, nb);
            const bool rest_same = ka != std::string::npos && na != std::string::npos && k.substr(0, ka) == n.substr(0, na) &&
                                   k.substr(kb) == n.substr(nb);
            emit(s, cs, "legacy.nokey", {na == std::string::npos ? std::string("-") : esc(n.substr(na, nb - na)), bl(rest_same)});
        }
        emit(s, cs, "legacy.ret", {bl(r1), bl(r2)});
        // The readers start after the "inv_item\t0" line (as the notecard and
        // task-inventory readers call them).
        const std::string text = key.str().substr(key.str().find('\n') + 1);
        std::istringstream is(text);
        LLPointer<Item> back = make_item<I>(prev_item());
        const bool ok = back->importLegacyStream(is);
        emit(s, cs, "legacy.reimport", item_cols(*back));
        std::vector<std::string> st = stream_cols(is, text);
        st.insert(st.begin(), bl(ok));
        emit(s, cs, "legacy.reimport.stream", st);
    }
    else
    {
        // A sale type outside 0..3: written as "not" since bug 1 (913990ebe3); run
        // guarded, so that a regression to the null name is reported, not fatal on Windows.
        const auto key = guarded_export([&](std::ostream& os) { item->exportLegacyStream(os, true); });
        const auto nokey = guarded_export([&](std::ostream& os) { item->exportLegacyStream(os, false); });
        emit(s, cs, "legacy", {esc(key.first)});
        emit(s, cs, "legacy.outcome", {key.second, nokey.second});
        emit(s, cs, "legacy.nokey.prefix", {esc(nokey.first)});
        const std::string text = key.first.substr(key.first.find('\n') + 1);
        std::istringstream is(text);
        LLPointer<Item> back = make_item<I>(prev_item());
        const bool ok = back->importLegacyStream(is);
        emit(s, cs, "legacy.reimport", item_cols(*back));
        std::vector<std::string> st = stream_cols(is, text);
        st.insert(st.begin(), bl(ok));
        emit(s, cs, "legacy.reimport.stream", st);
    }
    {
        std::ostringstream obj;
        item->Object::exportLegacyStream(obj, true);
        emit(s, cs, "legacy.object", {esc(obj.str())});
    }
    // LLSD.
    const LLSD sd = item->asLLSD();
    emit_llsd(s, cs, "llsd", sd);
    {
        LLSD into;
        into["preexisting"] = 1;
        into["name"] = "overwritten";
        item->asLLSD(into);
        // asLLSD(LLSD&) writes into the map without clearing it.
        std::string keys;
        bool same = true;
        for (auto i = into.beginMap(); i != into.endMap(); ++i)
        {
            keys += (keys.empty() ? "" : ",") + i->first;
            if (i->first != "preexisting" && llsd_bin(i->second) != llsd_bin(sd[i->first])) same = false;
        }
        emit(s, cs, "llsd.into", {esc(keys), bl(same), d(into["preexisting"].asInteger())});
    }
    emit(s, cs, "crc", {x8(item->getCRC32())});
    emit(s, cs, "derived", {u(item->getLinkedUUID()), bl(item->getIsLinkType()), d(item->getInventorySubType()),
                            u(item->getCreatorUUID()), d((S32)item->getType())});
    {
        LLPointer<Item> n = new Item;
        const bool ok = n->fromLLSD(sd, true);
        std::vector<std::string> cols = item_cols(*n);
        emit(s, cs, "fromllsd.new", cols);
        emit(s, cs, "fromllsd.new.ret", {bl(ok), bl(llsd_bin(n->asLLSD()) == llsd_bin(sd))});
    }
    {
        LLPointer<Item> o = make_item<I>(prev_item());
        o->fromLLSD(sd, false);
        emit(s, cs, "fromllsd.reuse", item_cols(*o));
    }
    {
        I::set_opensim(true);
        LLPointer<Item> o = make_item<I>(prev_item());
        o->fromLLSD(sd, true);
        I::set_opensim(false);
        emit(s, cs, "fromllsd.opensim", item_cols(*o));
    }
    emit(s, cs, "sdcreate.bin", {llsd_bin(I::sd_from_item(item.get()))});
    {
        LLPointer<Item> x = make_item<I>(f);
        LLPointer<Item> old = make_item<I>(prev_item());
        x->accumulatePermissionSlamBits(*old);
        LLPointer<Item> y = make_item<I>(prev_item());
        y->accumulatePermissionSlamBits(*item);
        emit(s, cs, "slam", {x8(x->getFlags()), x8(y->getFlags())});
    }
    {
        LLPointer<Item> x = make_item<I>(f);
        x->setPermissions(make_perm<typename I::Perm>(prev_item().perm));
        emit(s, cs, "setpermissions", perm_cols(x->getPermissions()));
        LLPointer<Item> c = new Item(item.get());
        emit(s, cs, "copy", item_cols(*c));
        LLPointer<Item> z = make_item<I>(f);
        z->setInventorySubType(0x7e);
        emit(s, cs, "subtype", {x8(z->getFlags()), d(z->getInventorySubType())});
        LLPointer<Item> w = make_item<I>(prev_item());
        w->copyItem(item.get());
        emit(s, cs, "copyitem", item_cols(*w));
        // The full constructor (normalises the name and the description, initMasks).
        LLPointer<Item> k = new Item(f.id, f.parent, make_perm<typename I::Perm>(f.perm), f.asset,
                                     static_cast<typename I::AT::EType>(f.type), static_cast<typename I::IT::EType>(f.inv_type),
                                     f.name, f.desc, make_sale<typename I::Sale>(f.sale_type, f.sale_price), f.flags, (S32)f.date);
        emit(s, cs, "ctor", item_cols(*k));
    }
}

template<class I> void c5_items(Sink& s)
{
    for (const ItemCase& c : c5_item_cases())
    {
        c5_item_record<I>(c.id, c.f, s);
    }
}

// The asset type x inventory type matrix, compact.
template<class I> void c5_matrix(Sink& s)
{
    typedef typename I::Item Item;
    for (S32 at : kAssetTypes)
    {
        for (S32 it : kInvTypes)
        {
            ItemF f = base_item();
            f.type = at;
            f.inv_type = it;
            LLPointer<Item> item = make_item<I>(f);
            std::ostringstream os;
            item->exportLegacyStream(os, true);
            const std::string text = os.str();
            auto token = [&](const std::string& key) -> std::string {
                const size_t p = text.find("\t\t" + key + "\t");
                if (p == std::string::npos) return "-";
                const size_t a = p + key.size() + 3;
                return esc(text.substr(a, text.find('\n', a) - a));
            };
            std::istringstream is(text.substr(text.find('\n') + 1));
            LLPointer<Item> back = make_item<I>(prev_item());
            back->importLegacyStream(is);
            const LLSD sd = item->asLLSD();
            const LLSD& sit = sd["inv_type"];
            LLPointer<Item> n = new Item;
            n->fromLLSD(sd, true);
            emit(s, "c5m." + d(at) + "." + d(it), "matrix",
                 {token("type"), token("inv_type"), esc(sd["type"].asString()),
                  sit.isString() ? "S:" + esc(sit.asString()) : "I:" + d(sit.asInteger()),
                  x8(item->getCRC32()), d((S32)back->getActualType()), d((S32)back->getInventoryType()),
                  x8(back->getPermissions().getMaskBase()), d((S32)n->getActualType()), d((S32)n->getInventoryType()),
                  x8(n->getPermissions().getMaskBase()), x8(n->getPermissions().getMaskEveryone()),
                  u(item->getLinkedUUID())});
        }
    }
}

// Random items (C5 random stream).
ItemF draw_item(Rng& r)
{
    static const std::string runtimes[] = {"", "luau", std::string(60, 'r'), "lu au"};
    static const S64 dates[] = {0, 1, INT_MAX, INT_MIN, -1, 1700000000};
    ItemF f = base_item();
    f.type = kAssetTypes[r.below(sizeof(kAssetTypes) / sizeof(kAssetTypes[0]))];
    f.inv_type = kInvTypes[r.below(sizeof(kInvTypes) / sizeof(kInvTypes[0]))];
    switch (r.below(5))
    {
    case 0: f.asset = NUL(); break;
    case 1: f.asset = U(0xa3); break;
    case 2: f.asset = MAGIC(); break;
    case 3: f.asset = NOT_MAGIC(); break;
    default:
    {
        const U64 a = r.next(), b = r.next();
        memcpy(f.asset.mData, &a, 8);
        memcpy(f.asset.mData + 8, &b, 8);
        break;
    }
    }
    f.thumb = r.coin() ? NUL() : U(0xb1);
    f.fav = r.coin();
    f.runtime = runtimes[r.below(4)];
    f.flags = r.coin() ? kFlags[r.below(7)] : (U32)r.next();
    f.perm = draw_perm(r);
    f.sale_type = (S32)r.below(4);
    f.sale_price = draw_price(r);
    const auto names = c5_names();
    f.name = names[r.below((U32)names.size())].first;
    f.desc = names[r.below((U32)names.size())].second;
    f.date = r.coin() ? dates[r.below(6)] : (S64)(S32)(U32)r.next();
    return f;
}

//----------------------------------------------------------------------------
// Categories and objects
//----------------------------------------------------------------------------

CatF prev_cat()
{
    CatF f;
    f.id = U(0x31);
    f.parent = U(0x32);
    f.thumb = U(0x34);
    f.type = 8;
    f.pref = 5;
    f.fav = true;
    f.date = 99;
    f.runtime = "pre-rt";
    f.name = "pre folder";
    f.owner = U(0x33);
    f.version = 3;
    return f;
}

struct CatCase
{
    std::string id;
    CatF f;
};

std::vector<CatCase> c5_cat_cases()
{
    std::vector<CatCase> v;
    CatF b;
    b.id = U(0xd1);
    b.parent = U(0xd2);
    b.type = 8;
    b.name = "Folder name";
    b.owner = U(0xd3);
    b.version = 7;
    for (S32 p = -1; p <= 101; ++p)
    {
        CatF f = b;
        f.pref = p;
        v.push_back({"c5c.pref." + d(p), f});
    }
    {
        CatF f = b;
        f.pref = 127;
        v.push_back({"c5c.pref.127", f});
        f.pref = -128;
        v.push_back({"c5c.pref.-128", f});
    }
    for (S32 p : {0, 8, 46, -1, 127, 58})
    {
        for (int k = 1; k < 4; ++k)
        {
            CatF f = b;
            f.pref = p;
            f.thumb = (k & 1) ? U(0xb2) : NUL();
            f.fav = (k & 2) != 0;
            v.push_back({"c5c.meta." + d(p) + "." + d(k), f});
        }
    }
    const auto names = c5_names();
    for (size_t i = 0; i < names.size(); ++i)
    {
        CatF f = b;
        f.pref = 0;
        f.name = names[i].first;
        v.push_back({"c5c.name." + d((S64)i), f});
    }
    for (S32 t : {-1, 255, 6, 24})
    {
        CatF f = b;
        f.pref = 0;
        f.type = t;
        v.push_back({"c5c.type." + d(t), f});
    }
    {
        CatF f = b;
        f.runtime = "luau";
        f.thumb = U(0xb2);
        v.push_back({"c5c.runtime", f});
        f = b;
        f.id = NUL();
        f.parent = NUL();
        f.owner = NUL();
        f.version = -1;
        v.push_back({"c5c.nullids", f});
    }
    return v;
}

template<class I> void c5_cat_record(const std::string& cs, const CatF& f, Sink& s)
{
    typedef typename I::Category Category;
    typedef typename I::Object Object;
    LLPointer<Category> cat = make_cat<I>(f);
    emit(s, cs, "in", cat_cols(*cat));
    {
        std::ostringstream os;
        cat->exportLegacyStream(os, true);
        emit(s, cs, "legacy", {esc(os.str())});
        const std::string text = os.str().substr(os.str().find('\n') + 1);
        std::istringstream is(text);
        LLPointer<Category> back = make_cat<I>(prev_cat());
        const bool ok = back->importLegacyStream(is);
        emit(s, cs, "legacy.reimport", cat_cols(*back));
        std::vector<std::string> st = stream_cols(is, text);
        st.insert(st.begin(), bl(ok));
        emit(s, cs, "legacy.reimport.stream", st);
    }
    {
        std::ostringstream os;
        cat->Object::exportLegacyStream(os, true);
        emit(s, cs, "legacy.object", {esc(os.str())});
        const std::string text = os.str().substr(os.str().find('\n') + 1);
        std::istringstream is(text);
        LLPointer<Object> back = make_obj<I>(prev_cat());
        const bool ok = back->importLegacyStream(is);
        emit(s, cs, "legacy.object.reimport", obj_cols(*back));
        std::vector<std::string> st = stream_cols(is, text);
        st.insert(st.begin(), bl(ok));
        emit(s, cs, "legacy.object.reimport.stream", st);
    }
    const LLSD as = cat->asLLSD();
    const LLSD ais = cat->asAISCreateCatLLSD();
    LLSD ex;
    cat->exportLLSD(ex);
    LLPointer<typename I::VCat> vcat = make_vcat<I>(f);
    LLSD vex;
    vcat->exportLLSD(vex);
    emit_llsd(s, cs, "asllsd", as);
    emit_llsd(s, cs, "ais", ais);
    emit_llsd(s, cs, "export", ex);
    emit_llsd(s, cs, "vexport", vex);
    {
        LLPointer<Category> c = make_cat<I>(prev_cat());
        const bool ok = c->fromLLSD(as);
        std::vector<std::string> cols = cat_cols(*c);
        cols.push_back(bl(ok));
        emit(s, cs, "fromllsd.asllsd", cols);
        LLPointer<Category> c2 = make_cat<I>(prev_cat());
        c2->fromLLSD(ais);
        emit(s, cs, "fromllsd.ais", cat_cols(*c2));
    }
    {
        LLPointer<Category> c = new Category;
        const bool ok = c->importLLSDMap(ex);
        std::vector<std::string> cols = cat_cols(*c);
        cols.push_back(bl(ok));
        emit(s, cs, "importmap", cols);
        LLPointer<typename I::VCat> v = make_vcat<I>(CatF());
        v->importLLSDMap(vex);
        emit(s, cs, "vimportmap", vcat_cols(*v));
        LLSD again;
        v->exportLLSD(again);
        emit(s, cs, "vimportmap.identity", {bl(llsd_bin(again) == llsd_bin(vex))});
    }
    {
        const LLSD c = I::sd_from_cat(cat.get());
        emit(s, cs, "sdcreate.bin", {llsd_bin(c)});
        LLPointer<Category> back = I::cat_from_sd(c);
        emit(s, cs, "sdcreate.back", cat_cols(*back));
        LLPointer<Category> copy = new Category(cat.get());
        emit(s, cs, "copy", cat_cols(*copy));
        LLPointer<Category> ctor = new Category(f.id, f.parent, static_cast<typename I::FT::EType>(f.pref), f.name);
        emit(s, cs, "ctor", cat_cols(*ctor));
    }
}

template<class I> void c5_categories(Sink& s)
{
    for (const CatCase& c : c5_cat_cases())
    {
        c5_cat_record<I>(c.id, c.f, s);
    }
    // LLInventoryObject on its own.
    const std::string names[] = {"Object name", "", "  spaced  ", std::string(80, 'o'), "pipe|obj", "caf\xc3\xa9"};
    for (size_t i = 0; i < 6; ++i)
    {
        CatF f;
        f.id = U(0xe5);
        f.parent = U(0xe6);
        f.type = (i == 3) ? 255 : 6;
        f.name = names[i];
        f.thumb = (i == 2) ? U(0xb3) : NUL();
        const std::string cs = "c5o." + d((S64)i);
        LLPointer<typename I::Object> o = make_obj<I>(f);
        emit(s, cs, "in", obj_cols(*o));
        std::ostringstream os;
        o->exportLegacyStream(os, true);
        emit(s, cs, "legacy", {esc(os.str())});
        const std::string text = os.str().substr(os.str().find('\n') + 1);
        std::istringstream is(text);
        LLPointer<typename I::Object> back = make_obj<I>(prev_cat());
        const bool ok = back->importLegacyStream(is);
        emit(s, cs, "legacy.reimport", obj_cols(*back));
        std::vector<std::string> st = stream_cols(is, text);
        st.insert(st.begin(), bl(ok));
        emit(s, cs, "legacy.reimport.stream", st);
        LLPointer<typename I::Object> ctor = new typename I::Object(f.id, f.parent, static_cast<typename I::AT::EType>(f.type), f.name);
        emit(s, cs, "ctor", obj_cols(*ctor));
        LLPointer<typename I::Object> cp = make_obj<I>(prev_cat());
        cp->copyObject(o.get());
        emit(s, cs, "copyobject", obj_cols(*cp));
        LLPointer<typename I::Object> st2 = make_obj<I>(prev_cat());
        st2->setType(static_cast<typename I::AT::EType>(10));
        std::string rt1 = st2->getRuntime();
        st2->setType(static_cast<typename I::AT::EType>(6));
        emit(s, cs, "settype", {esc(rt1), esc(st2->getRuntime())});
    }
}

//============================================================================
// C6: malformed legacy input
//============================================================================

struct LegacyCase
{
    std::string id;
    std::string readers; // I item, O object, C category, P permissions, S sale info, N notecard
    std::string input;
    // Corrupt-input cases: the same input without the corrupt line. The readers then also
    // record the fix() invariants and that the corrupt line widened no permission.
    std::string baseline;
    S32 max_text = 0; // notecards: LLNotecard's max_text (0: LLNotecard::MAX_SIZE)
};

// The fix() invariants of a permissions record, one character each:
// owner<=base, group<=owner, everyone<=owner, next<=base, everyone without MODIFY,
// no-transfer base and not group owned => group and everyone without COPY.
template<class P> std::string perm_invariants(const P& p)
{
    const U32 b = p.getMaskBase(), o = p.getMaskOwner(), g = p.getMaskGroup(), e = p.getMaskEveryone(), n = p.getMaskNextOwner();
    std::string r;
    r += (o & ~b) ? '0' : '1';
    r += (g & ~o) ? '0' : '1';
    r += (e & ~o) ? '0' : '1';
    r += (n & ~b) ? '0' : '1';
    r += (e & PERM_MODIFY) ? '0' : '1';
    r += (!(b & PERM_TRANSFER) && !p.isGroupOwned() && ((g | e) & PERM_COPY)) ? '0' : '1';
    return r;
}

// Whether a corrupt line widened no permission against the same input without it: one
// character per mask (base, owner, group, everyone, next: no bit added) and one for
// group ownership (not newly set).
template<class P> std::string no_widening(const P& p, const P& base)
{
    std::string r;
    r += (p.getMaskBase() & ~base.getMaskBase()) ? '0' : '1';
    r += (p.getMaskOwner() & ~base.getMaskOwner()) ? '0' : '1';
    r += (p.getMaskGroup() & ~base.getMaskGroup()) ? '0' : '1';
    r += (p.getMaskEveryone() & ~base.getMaskEveryone()) ? '0' : '1';
    r += (p.getMaskNextOwner() & ~base.getMaskNextOwner()) ? '0' : '1';
    r += (p.isGroupOwned() && !base.isGroupOwned()) ? '0' : '1';
    return r;
}

// Text up to 4096 bytes escaped; longer as its length and SHA-256.
std::string long_text(const std::string& t)
{
    if (t.size() <= 4096) return esc(t);
    Sha256 sha;
    sha.update(t.data(), t.size());
    return "len=" + d((S64)t.size()) + ";sha256=" + sha.hex();
}

std::vector<std::string> std_item_lines(U8 tag = 0x61)
{
    return {
        "\t{",
        "\t\titem_id\t" + u(U(tag)),
        "\t\tparent_id\t" + u(U(tag + 1)),
        "\tpermissions 0",
        "\t{",
        "\t\tbase_mask\t7fffffff",
        "\t\towner_mask\t7fffffff",
        "\t\tgroup_mask\t00000000",
        "\t\teveryone_mask\t00000000",
        "\t\tnext_owner_mask\t00082000",
        "\t\tcreator_id\t" + u(U(tag + 2)),
        "\t\towner_id\t" + u(U(tag + 3)),
        "\t\tlast_owner_id\t" + u(U(tag + 4)),
        "\t\tgroup_id\t" + u(U(tag + 5)),
        "\t}",
        "\t\tasset_id\t" + u(U(tag + 6)),
        "\t\ttype\tnotecard",
        "\t\tinv_type\tnotecard",
        "\t\tflags\t00000000",
        "\tsale_info\t0",
        "\t{",
        "\t\tsale_type\tnot",
        "\t\tsale_price\t10",
        "\t}",
        "\t\tname\tStandard item|",
        "\t\tdesc\tStandard description|",
        "\t\tcreation_date\t1700000000",
        "\t}",
    };
}
enum StdLine { L_OPEN, L_ITEM_ID, L_PARENT, L_PERM_HDR, L_PERM_OPEN, L_BASE, L_OWNER, L_GROUP, L_EVERYONE, L_NEXT, L_CREATOR,
               L_OWNER_ID, L_LAST, L_GROUP_ID, L_PERM_CLOSE, L_ASSET, L_TYPE, L_INV_TYPE, L_FLAGS, L_SALE_HDR, L_SALE_OPEN,
               L_SALE_TYPE, L_SALE_PRICE, L_SALE_CLOSE, L_NAME, L_DESC, L_DATE, L_CLOSE, L_COUNT };

std::string join_lines(const std::vector<std::string>& v, const char* eol = "\n")
{
    std::string s;
    for (const std::string& l : v)
    {
        s += l;
        s += eol;
    }
    return s;
}

static const std::string kSuffix = "NEXT\tline\n";

std::string meta_xml(const std::string& inner) { return "<llsd><map>" + inner + "</map></llsd>"; }

std::vector<LegacyCase> c6_cases()
{
    std::vector<LegacyCase> v;
    const std::string ALL = "IOCPS";
    auto item_case = [&](const std::string& id, const std::function<void(std::vector<std::string>&)>& mut, bool suffix = true,
                         const std::string& baseline = std::string()) {
        std::vector<std::string> l = std_item_lines();
        mut(l);
        v.push_back({id, ALL, join_lines(l) + (suffix ? kSuffix : std::string()), baseline});
    };
    auto raw_case = [&](const std::string& id, const std::string& input, const std::string& readers = "IOCPS",
                        const std::string& baseline = std::string()) {
        v.push_back({id, readers, input, baseline});
    };
    const std::string std_item = join_lines(std_item_lines()) + kSuffix;
    item_case("c6.good", [](std::vector<std::string>&) {});
    item_case("c6.good.eof", [](std::vector<std::string>&) {}, false);
    {
        std::string t = join_lines(std_item_lines());
        t.pop_back();
        raw_case("c6.good.nonl", t);
    }
    {
        std::vector<std::string> l = std_item_lines();
        raw_case("c6.crlf", join_lines(l, "\r\n") + "NEXT\tline\r\n");
    }
    item_case("c6.nobrace.open", [](std::vector<std::string>& l) { l.erase(l.begin() + L_OPEN); });
    item_case("c6.nobrace.close", [](std::vector<std::string>& l) { l.pop_back(); }, false);
    item_case("c6.nobrace.close.next", [](std::vector<std::string>& l) {
        l.pop_back();
        l.push_back("\tinv_item\t0");
        l.push_back("\t{");
        l.push_back("\t\tname\tSecond|");
        l.push_back("\t}");
    });
    for (int k = 0; k < L_COUNT; ++k)
    {
        item_case("c6.blank." + d(k), [k](std::vector<std::string>& l) { l.insert(l.begin() + k + 1, ""); });
    }
    for (int k : {(int)L_OPEN, (int)L_PERM_CLOSE, (int)L_SALE_CLOSE, (int)L_DATE})
    {
        item_case("c6.ws." + d(k), [k](std::vector<std::string>& l) { l.insert(l.begin() + k + 1, "  \t  "); });
        item_case("c6.cr." + d(k), [k](std::vector<std::string>& l) { l.insert(l.begin() + k + 1, "\r"); });
    }
    item_case("c6.blank.first", [](std::vector<std::string>& l) { l.insert(l.begin(), ""); });
    item_case("c6.eof.after_perm", [](std::vector<std::string>& l) { l.resize(L_PERM_CLOSE + 1); }, false);
    item_case("c6.eof.after_perm.blank", [](std::vector<std::string>& l) { l.resize(L_PERM_CLOSE + 1); l.push_back(""); }, false);
    item_case("c6.eof.after_sale", [](std::vector<std::string>& l) { l.resize(L_SALE_CLOSE + 1); }, false);
    item_case("c6.eof.mid_perm", [](std::vector<std::string>& l) { l.resize(L_NEXT + 1); }, false);
    item_case("c6.eof.after_name", [](std::vector<std::string>& l) { l.resize(L_NAME + 1); }, false);
    // Keyword-only lines (stale value).
    item_case("c6.kw.type", [](std::vector<std::string>& l) { l[L_TYPE] = "\t\ttype"; });
    item_case("c6.kw.inv_type", [](std::vector<std::string>& l) { l[L_INV_TYPE] = "\t\tinv_type"; });
    item_case("c6.kw.flags", [](std::vector<std::string>& l) { l[L_FLAGS] = "\t\tflags"; });
    item_case("c6.kw.asset", [](std::vector<std::string>& l) { l[L_ASSET] = "\t\tasset_id"; });
    item_case("c6.kw.name", [](std::vector<std::string>& l) { l[L_NAME] = "\t\tname"; });
    item_case("c6.kw.desc", [](std::vector<std::string>& l) { l[L_DESC] = "\t\tdesc"; });
    // The stale value is the description ("Standard description"): %d fails and the
    // uninitialised local is stored (undefined behaviour).
    // The stale value is the description ("Standard description"): %d fails; 0 since bug 38.
    item_case("c6.kw.date", [](std::vector<std::string>& l) { l[L_DATE] = "\t\tcreation_date"; });
    item_case("c6.kw.item_id", [](std::vector<std::string>& l) { l[L_ITEM_ID] = "\t\titem_id"; });
    item_case("c6.kw.metadata", [](std::vector<std::string>& l) { l.insert(l.begin() + L_PERM_CLOSE + 1, "\t\tmetadata"); });
    raw_case("c6.kw.date.first", "\t{\n\t\tcreation_date\n\t}\n" + std::string(kSuffix));
    raw_case("c6.kw.date.stale", "\t{\n\t\tflags\t7\n\t\tcreation_date\n\t}\n" + std::string(kSuffix));
    // Long lines.
    for (int n : {253, 254, 255, 256, 300})
    {
        item_case("c6.long.desc." + d(n), [n](std::vector<std::string>& l) {
            l[L_DESC] = "\t\tdesc\t" + std::string(n - 8, 'x') + "|";
        });
        item_case("c6.long.unknown." + d(n), [n](std::vector<std::string>& l) {
            l.insert(l.begin() + L_PARENT + 1, "\t\tunknown\t" + std::string(n - 10, 'u'));
        });
    }
    for (int n : {16382, 16383, 16384, 16385})
    {
        item_case("c6.long.perm." + d(n), [n](std::vector<std::string>& l) {
            l.insert(l.begin() + L_NEXT + 1, "\t\tunknown_perm\t" + std::string(n - 15, 'p'));
        });
    }
    // name and desc.
    const std::pair<const char*, const char*> nd[] = {
        {"empty", "\t|"}, {"nopipe", "\tNo pipe"}, {"leadtabs", "\t\t\tLead tabs|"}, {"leadspaces", "\t   Lead spaces|"},
        {"spacesep", " Space sep|"}, {"innertab", "\tInner\ttab|"}, {"midpipe", "\tMid|pipe|"}, {"cr", "\tCR\r"},
        {"pipefirst", "\t|rest"}, {"high", "\tcaf\xc3\xa9|"}, {"tabonly", "\t"}, {"ctrl", "\t\x01\x02|"},
        {"trailingspace", "\tTrailing   |"}, {"spacetab", " \tSpace then tab|"},
    };
    for (const auto& x : nd)
    {
        const std::string vname = x.second;
        item_case(std::string("c6.name.") + x.first, [vname](std::vector<std::string>& l) { l[L_NAME] = "\t\tname" + vname; });
        item_case(std::string("c6.desc.") + x.first, [vname](std::vector<std::string>& l) { l[L_DESC] = "\t\tdesc" + vname; });
    }
    // metadata.
    const std::string thumb = "<key>thumbnail</key><map><key>asset_id</key><uuid>" + u(U(0x68)) + "</uuid></map>";
    const std::string script = "<key>script</key><map><key>runtime</key><string>luau</string></map>";
    const std::string fav = "<key>favorite</key><map><key>toggled</key><boolean>1</boolean></map>";
    const std::vector<std::pair<std::string, std::vector<std::string> > > metas = {
        {"thumb", {"\t\tmetadata\t" + meta_xml(thumb), "|"}},
        {"script", {"\t\tmetadata\t" + meta_xml(script), "|"}},
        {"fav", {"\t\tmetadata\t" + meta_xml(fav), "|"}},
        {"all", {"\t\tmetadata\t" + meta_xml(fav + script + thumb), "|"}},
        {"notllsd", {"\t\tmetadata\tjunk"}},
        {"malformed", {"\t\tmetadata\t<llsd><map><key>thumbnail</key></llsd>"}},
        {"over254", {"\t\tmetadata\t" + meta_xml(fav + "<key>script</key><map><key>runtime</key><string>" + std::string(60, 'r') +
                                                 "</string></map>" + thumb), "|"}},
        {"notation", {"\t\tmetadata\t", "{'thumbnail':{'asset_id':u" + u(U(0x68)) + "}}"}},
        {"spaceruntime", {"\t\tmetadata\t" + meta_xml("<key>script</key><map><key>runtime</key><string>lu au</string></map>"), "|"}},
        {"nopipe", {"\t\tmetadata\t" + meta_xml(thumb)}},
        {"thumbnoasset", {"\t\tmetadata\t" + meta_xml("<key>thumbnail</key><map></map>"), "|"}},
        {"favnotoggle", {"\t\tmetadata\t" + meta_xml("<key>favorite</key><map></map>"), "|"}},
        {"emptymap", {"\t\tmetadata\t<llsd><map></map></llsd>", "|"}},
    };
    for (const auto& m : metas)
    {
        const std::vector<std::string> lines = m.second;
        item_case("c6.meta." + m.first, [lines](std::vector<std::string>& l) {
            l.insert(l.begin() + L_PERM_CLOSE + 1, lines.begin(), lines.end());
        });
        item_case("c6.meta.end." + m.first, [lines](std::vector<std::string>& l) {
            l.insert(l.begin() + L_DATE + 1, lines.begin(), lines.end());
        });
    }
    // asset_id / shadow_id.
    {
        LLUUID shadow = U(0x67);
        const LLUUID magic = MAGIC();
        for (int i = 0; i < 16; ++i) shadow.mData[i] ^= magic.mData[i];
        const std::string sh = u(shadow), as = u(U(0x67)), other = u(U(0x69));
        item_case("c6.asset.shadow", [sh](std::vector<std::string>& l) { l[L_ASSET] = "\t\tshadow_id\t" + sh; });
        item_case("c6.asset.asset_then_shadow", [sh](std::vector<std::string>& l) { l.insert(l.begin() + L_ASSET + 1, "\t\tshadow_id\t" + sh); });
        item_case("c6.asset.shadow_then_asset", [sh, other](std::vector<std::string>& l) {
            l[L_ASSET] = "\t\tshadow_id\t" + sh;
            l.insert(l.begin() + L_ASSET + 1, "\t\tasset_id\t" + other);
        });
        std::string upper = as;
        LLStringUtil::toUpper(upper);
        std::string short35 = as;
        short35.erase(23, 1);
        std::string slots = as;
        slots[8] = 'x'; slots[13] = 'y'; slots[18] = 'z'; slots[23] = '!';
        const std::pair<const char*, std::string> forms[] = {
            {"upper", upper}, {"short35", short35}, {"slots", slots}, {"invalid", "zz"}, {"long37", as + "0"},
            {"short34", as.substr(0, 34)}, {"nohyphens", "0123456789abcdef0123456789abcdef"}, {"empty", ""},
        };
        for (const auto& f : forms)
        {
            const std::string val = f.second;
            item_case(std::string("c6.uuid.") + f.first, [val](std::vector<std::string>& l) { l[L_ASSET] = "\t\tasset_id\t" + val; });
        }
    }
    // Permissions inside the item.
    item_case("c6.perm.creator_mask", [](std::vector<std::string>& l) { l[L_BASE] = "\t\tcreator_mask\t00006000"; });
    item_case("c6.perm.group_owned.1", [](std::vector<std::string>& l) { l.insert(l.begin() + L_GROUP_ID + 1, "\t\tgroup_owned\t1"); });
    item_case("c6.perm.group_owned.0", [](std::vector<std::string>& l) { l.insert(l.begin() + L_GROUP_ID + 1, "\t\tgroup_owned\t0"); });
    item_case("c6.perm.group_owned.junk", [](std::vector<std::string>& l) { l.insert(l.begin() + L_GROUP_ID + 1, "\t\tgroup_owned\tjunk"); },
              true, std_item);
    item_case("c6.perm.mask.stale", [](std::vector<std::string>& l) { l[L_OWNER] = "\t\towner_mask\tzz"; }, true, std_item);
    item_case("c6.perm.mask.0x", [](std::vector<std::string>& l) { l[L_OWNER] = "\t\towner_mask\t0x0000e000"; });
    item_case("c6.perm.mask.neg", [](std::vector<std::string>& l) { l[L_OWNER] = "\t\towner_mask\t-1"; });
    item_case("c6.perm.mask.9digits", [](std::vector<std::string>& l) { l[L_OWNER] = "\t\towner_mask\t1ffffffff"; });
    item_case("c6.perm.id.invalid", [](std::vector<std::string>& l) { l[L_OWNER_ID] = "\t\towner_id\tzz"; });
    item_case("c6.perm.unknown", [](std::vector<std::string>& l) { l.insert(l.begin() + L_BASE, "\t\tunknown_perm\t1"); });
    {
        // The first mask fails to parse: the uninitialised local is stored (undefined behaviour).
        // Defined since bug 38 (d680ea60a7): the targets start at 0 and "".
        const std::string open = "\t{\n\tpermissions 0\n\t{\n", close = "\t}\n\t}\n" + std::string(kSuffix);
        raw_case("c6.perm.mask.first_fails", open + "\t\tbase_mask\tzz\n" + close, ALL, open + close);
        raw_case("c6.perm.group_owned.first_fails", open + "\t\tgroup_owned\tjunk\n" + close, ALL, open + close);
        raw_case("c6.perm.id.first_empty", open + "\t\tcreator_id\n" + close, ALL, open + close);
    }
    // Sale info inside the item.
    for (const char* pm : {"00000000", "7fff7fff", "0000e000", "ffffffff"})
    {
        const std::string val = pm;
        item_case("c6.sale.perm_mask." + val, [val](std::vector<std::string>& l) { l.insert(l.begin() + L_SALE_PRICE + 1, "\t\tperm_mask\t" + val); });
    }
    item_case("c6.sale.perm_mask.kw", [](std::vector<std::string>& l) { l.insert(l.begin() + L_SALE_PRICE + 1, "\t\tperm_mask"); });
    item_case("c6.sale.type.unknown", [](std::vector<std::string>& l) { l[L_SALE_TYPE] = "\t\tsale_type\tjunk"; });
    item_case("c6.sale.type.copy", [](std::vector<std::string>& l) { l[L_SALE_TYPE] = "\t\tsale_type\tcopy"; });
    item_case("c6.sale.price.neg", [](std::vector<std::string>& l) { l[L_SALE_PRICE] = "\t\tsale_price\t-5"; });
    item_case("c6.sale.price.junk", [](std::vector<std::string>& l) { l[L_SALE_PRICE] = "\t\tsale_price\tjunk"; });
    item_case("c6.sale.price.big", [](std::vector<std::string>& l) { l[L_SALE_PRICE] = "\t\tsale_price\t3000000000"; });
    item_case("c6.sale.missing", [](std::vector<std::string>& l) { l.erase(l.begin() + L_SALE_TYPE, l.begin() + L_SALE_PRICE + 1); });
    // flags.
    for (const char* fl : {"0x1f", "-1", "123456789", "fffffffff", "zz", "+5", "00000100junk", "0X10", "7FFFFFFF"})
    {
        const std::string val = fl;
        item_case("c6.flags." + val, [val](std::vector<std::string>& l) { l[L_FLAGS] = "\t\tflags\t" + val; });
    }
    // creation_date.
    for (const char* dt : {"2147483648", "4294967301", "-2147483649", "-5", "12abc", "99999999999999999999", "+7", "0x10"})
    {
        const std::string val = dt;
        item_case("c6.date." + val, [val](std::vector<std::string>& l) { l[L_DATE] = "\t\tcreation_date\t" + val; });
    }
    item_case("c6.date.abc", [](std::vector<std::string>& l) { l[L_DATE] = "\t\tcreation_date\tabc"; });
    // Unknown keywords, braces.
    item_case("c6.unknown.kw", [](std::vector<std::string>& l) { l.insert(l.begin() + L_PARENT + 1, "\t\tunknown_key\tvalue"); });
    item_case("c6.brace.junk", [](std::vector<std::string>& l) { l[L_CLOSE] = "\t}junk"; });
    item_case("c6.brace.trailing", [](std::vector<std::string>& l) { l[L_CLOSE] = "\t} trailing"; });
    item_case("c6.brace.inner", [](std::vector<std::string>& l) { l.insert(l.begin() + L_PARENT + 1, "\t{"); });
    item_case("c6.brace.inner.close", [](std::vector<std::string>& l) { l.insert(l.begin() + L_TYPE, "\t}"); });
    item_case("c6.brace.openvalue", [](std::vector<std::string>& l) { l.insert(l.begin() + L_PARENT + 1, "\t{ x"); });
    // Types.
    const std::pair<const char*, std::pair<const char*, const char*> > types[] = {
        {"invalid", {"invalid", "notecard"}}, {"junk", {"junk", "notecard"}}, {"invjunk", {"notecard", "junk"}},
        {"mismatch", {"landmark", "texture"}}, {"link", {"link", "texture"}}, {"linkf", {"link_f", "category"}},
        {"minus1", {"-1", "notecard"}}, {"script", {"script", "script"}}, {"callcard", {"callcard", "object"}},
        {"settings", {"settings", "settings"}}, {"bad", {"llassettype_bad_lookup", "notecard"}},
    };
    for (const auto& t : types)
    {
        const std::string ty = t.second.first, iv = t.second.second;
        item_case(std::string("c6.types.") + t.first, [ty, iv](std::vector<std::string>& l) {
            l[L_TYPE] = "\t\ttype\t" + ty;
            l[L_INV_TYPE] = "\t\tinv_type\t" + iv;
        });
    }
    item_case("c6.types.noinv", [](std::vector<std::string>& l) { l.erase(l.begin() + L_INV_TYPE); });
    // perm_mask fix-up against an owner mask that differs from the base mask (audit F1):
    // perm_mask 0 takes the owner mask; a perm_mask without copy gains transfer.
    for (const char* owner : {"0008e000", "7fffffff", "00006000"})
    {
        for (const char* pm : {"00000000", "00004000", "00080000", "00008000", "00002000", "0008e000"})
        {
            const std::string o = owner, val = pm;
            item_case("c6.permmask.o" + o + ".pm" + val, [o, val](std::vector<std::string>& l) {
                l[L_OWNER] = "\t\towner_mask\t" + o;
                l.insert(l.begin() + L_SALE_PRICE + 1, "\t\tperm_mask\t" + val);
            });
        }
    }
    // The sale_info block before the permissions block: the fix-up sees the previous
    // item's owner mask, then the permissions block re-initialises the permissions.
    for (const char* pm : {"00000000", "00004000"})
    {
        const std::string val = pm;
        item_case("c6.permmask.salefirst.pm" + val, [val](std::vector<std::string>& l) {
            l[L_OWNER] = "\t\towner_mask\t0008e000";
            l.insert(l.begin() + L_SALE_PRICE + 1, "\t\tperm_mask\t" + val);
            std::vector<std::string> sale(l.begin() + L_SALE_HDR, l.begin() + L_SALE_CLOSE + 2);
            l.erase(l.begin() + L_SALE_HDR, l.begin() + L_SALE_CLOSE + 2);
            l.insert(l.begin() + L_PERM_HDR, sale.begin(), sale.end());
        });
    }
    // A single token of 253/254/255 characters ending in '7', then a keyword-only
    // "flags" line, which takes the stale value (audit F9: the %254s width).
    for (int n : {253, 254, 255})
    {
        item_case("c6.token." + d(n), [n](std::vector<std::string>& l) {
            l.insert(l.begin() + L_DESC + 1, std::string(n - 1, 'z') + "7");
            l.insert(l.begin() + L_DESC + 2, "\t\tflags");
        });
    }
    // Spaceless XML metadata that does not start with <llsd> (audit F9).
    item_case("c6.meta.notprefix", [](std::vector<std::string>& l) {
        l.insert(l.begin() + L_PERM_CLOSE + 1, "\t\tmetadata\t<!--x--><llsd><map><key>thumbnail</key><map><key>asset_id</key><uuid>" +
                                                     u(U(0x68)) + "</uuid></map></map></llsd>");
        l.insert(l.begin() + L_PERM_CLOSE + 2, "|");
    });
    item_case("c6.meta.bareMap", [](std::vector<std::string>& l) {
        l.insert(l.begin() + L_PERM_CLOSE + 1, "\t\tmetadata\t<map><key>thumbnail</key><map><key>asset_id</key><uuid>" + u(U(0x68)) +
                                                     "</uuid></map></map>");
    });
    // LLUUID::set leniency: each hyphen slot replaced on its own, other spellings (audit D02).
    {
        const std::string as = u(U(0x67));
        for (size_t slot : {(size_t)8, (size_t)13, (size_t)18, (size_t)23})
        {
            std::string v = as;
            v[slot] = 'g';
            item_case("c6.uuid.slot" + d((S64)slot), [v](std::vector<std::string>& l) { l[L_ASSET] = "\t\tasset_id\t" + v; });
        }
        std::string mixed = as;
        for (size_t k = 0; k < mixed.size(); k += 2) mixed[k] = (char)toupper((unsigned char)mixed[k]);
        item_case("c6.uuid.mixedcase", [mixed](std::vector<std::string>& l) { l[L_ASSET] = "\t\tasset_id\t" + mixed; });
        for (size_t cut : {(size_t)8, (size_t)13, (size_t)18})
        {
            std::string v = as;
            v.erase(cut, 1);
            item_case("c6.uuid.short35.at" + d((S64)cut), [v](std::vector<std::string>& l) { l[L_ASSET] = "\t\tasset_id\t" + v; });
        }
        std::string nonhex = as;
        nonhex[0] = 'g';
        item_case("c6.uuid.nonhex", [nonhex](std::vector<std::string>& l) { l[L_ASSET] = "\t\tasset_id\t" + nonhex; });
    }
    // Objects.
    const std::string obj_good = "\t{\n\t\tobj_id\t" + u(U(0x71)) + "\n\t\tparent_id\t" + u(U(0x72)) + "\n\t\ttype\tobject\n\t\tname\tObj name|\n\t}\n";
    raw_case("c6.obj.good", obj_good + kSuffix);
    raw_case("c6.obj.name.lead", "\t{\n\t\tobj_id\t" + u(U(0x71)) + "\n\t\tname\t \t Lead|\n\t}\n" + kSuffix);
    raw_case("c6.obj.name.empty", "\t{\n\t\tobj_id\t" + u(U(0x71)) + "\n\t\tname\t|\n\t}\n" + kSuffix);
    raw_case("c6.obj.name.long", "\t{\n\t\tname\t" + std::string(80, 'o') + "|\n\t}\n" + kSuffix);
    raw_case("c6.obj.metadata", "\t{\n\t\tobj_id\t" + u(U(0x71)) + "\n\t\tmetadata\t" + meta_xml(thumb + script) + "\n|\n\t}\n" + kSuffix);
    // Categories.
    const std::string cat_good = "\t{\n\t\tcat_id\t" + u(U(0x73)) + "\n\t\tparent_id\t" + u(U(0x74)) +
                                 "\n\t\ttype\tcategory\n\t\tpref_type\ttexture\n\t\tname\tCat name|\n\t}\n";
    raw_case("c6.cat.good", cat_good + kSuffix);
    for (const char* pt : {"ensemble", "-1", "junk", "root_inv", "suitcase", "llfoldertype_bad_lookup", "outbox"})
    {
        raw_case(std::string("c6.cat.pref.") + pt, "\t{\n\t\tcat_id\t" + u(U(0x73)) + "\n\t\tpref_type\t" + pt + "\n\t}\n" + kSuffix);
    }
    raw_case("c6.cat.name.empty", "\t{\n\t\tcat_id\t" + u(U(0x73)) + "\n\t\tname\t|\n\t}\n" + kSuffix);
    raw_case("c6.cat.name.lead", "\t{\n\t\tname\t \t Lead|\n\t}\n" + kSuffix);
    raw_case("c6.cat.metadata.after", "\t{\n\t\tcat_id\t" + u(U(0x73)) + "\n\t\tname\tCat|\n\t\tmetadata\t" + meta_xml(thumb) + "\n|\n\t}\n" + kSuffix);
    raw_case("c6.cat.metadata.script", "\t{\n\t\tmetadata\t" + meta_xml(script) + "\n|\n\t}\n" + kSuffix);
    // Sale-info blocks on their own.
    raw_case("c6.saleblk.good", "\t{\n\t\tsale_type\tcopy\n\t\tsale_price\t50\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.blankfirst", "\n\n\t{\n\t\tsale_type\torig\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.blankmid", "\t{\n\t\tsale_type\torig\n\n\t\tsale_price\t3\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.cntn", "\t{\n\t\tsale_type\tcntn\n\t\tsale_price\t-1\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.permmask", "\t{\n\t\tperm_mask\t0000e000\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.permmask.junk", "\t{\n\t\tperm_mask\tzz\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.price.junk.first", "\t{\n\t\tsale_price\tjunk\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.empty", "\t{\n\t}\n" + std::string(kSuffix));
    raw_case("c6.saleblk.eof", "\t{\n\t\tsale_type\tcopy\n");
    // Permission blocks on their own.
    raw_case("c6.permblk.good", "\t{\n\t\tbase_mask\t0008e000\n\t\towner_mask\t0008e000\n\t\tgroup_mask\t0000a000\n\t\teveryone_mask\t0000c000\n"
                                "\t\tnext_owner_mask\t00000000\n\t\towner_id\t" + u(U(0x0a)) + "\n\t\tgroup_id\t" + u(U(0x06)) + "\n\t}\n" + kSuffix);
    raw_case("c6.permblk.creator_mask", "\t{\n\t\tcreator_mask\t00004000\n\t\tnext_owner_mask\t00002000\n\t}\n" + std::string(kSuffix));
    raw_case("c6.permblk.groupowned", "\t{\n\t\tgroup_id\t" + u(U(0x06)) + "\n\t\tgroup_owned\t1\n\t}\n" + kSuffix);
    raw_case("c6.permblk.groupowned.noid", "\t{\n\t\tgroup_owned\t1\n\t}\n" + std::string(kSuffix));
    raw_case("c6.permblk.ownernull.group", "\t{\n\t\tgroup_id\t" + u(U(0x06)) + "\n\t}\n" + kSuffix);
    raw_case("c6.permblk.empty", "\t{\n\t}\n" + std::string(kSuffix));
    raw_case("c6.permblk.eof", "\t{\n\t\tbase_mask\t00002000\n");
    raw_case("c6.permblk.nobrace", "\t\tbase_mask\t00002000\n\t}\n" + std::string(kSuffix));
    // Empty input.
    raw_case("c6.empty", "");
    raw_case("c6.only_close", "\t}\n" + std::string(kSuffix));
    raw_case("c6.only_blank", "\n");

    // Notecards (reader N).
    auto embedded = [](const std::vector<std::string>& item_lines, int index) {
        return "{\next char index " + d(index) + "\n\tinv_item\t0\n" + join_lines(item_lines) + "}\n";
    };
    auto notecard = [](int version, const std::vector<std::string>& items, const std::string& text, int count_override = -1000) {
        std::string s = "Linden text version " + d(version) + "\n{\nLLEmbeddedItems version 1\n{\ncount " +
                        d(count_override == -1000 ? (S64)items.size() : count_override) + "\n";
        for (const std::string& it : items) s += it;
        s += "}\nText length " + d((S64)text.size()) + "\n" + text + "}\n";
        return s;
    };
    const std::string body = "Hello notecard\nsecond line\n";
    raw_case("c6.nc.empty", notecard(2, {}, body), "N");
    raw_case("c6.nc.one", notecard(2, {embedded(std_item_lines(), 0)}, body), "N");
    raw_case("c6.nc.three", notecard(2, {embedded(std_item_lines(0x41), 0), embedded(std_item_lines(0x51), 1), embedded(std_item_lines(0x61), 2)}, body), "N");
    raw_case("c6.nc.v1", notecard(1, {embedded(std_item_lines(), 0)}, body), "N");
    raw_case("c6.nc.v3", notecard(3, {}, body), "N");
    {
        std::vector<std::string> l = std_item_lines();
        l.insert(l.begin() + L_PERM_CLOSE + 1, "");
        raw_case("c6.nc.blank_after_perm", notecard(2, {embedded(l, 0)}, body), "N");
        l = std_item_lines();
        l[L_DESC] = "\t\tdesc\t" + std::string(300, 'x') + "|";
        raw_case("c6.nc.longline", notecard(2, {embedded(l, 0)}, body), "N");
        l = std_item_lines();
        l.insert(l.begin() + L_PERM_CLOSE + 1, "\t\tmetadata\t" + meta_xml(thumb));
        l.insert(l.begin() + L_PERM_CLOSE + 2, "|");
        raw_case("c6.nc.metadata", notecard(2, {embedded(l, 0)}, body), "N");
        l = std_item_lines();
        l.pop_back();
        raw_case("c6.nc.item_noclose", notecard(2, {embedded(l, 0)}, body), "N");
    }
    {
        std::string s = notecard(2, {}, body);
        s.replace(s.find("Text length 27"), 14, "Text length 100");
        raw_case("c6.nc.textshort", s, "N");
        s = notecard(2, {}, body);
        s.replace(s.find("Text length 27"), 14, "Text length 70000");
        raw_case("c6.nc.texttoolong", s, "N");
    }
    raw_case("c6.nc.countneg", notecard(2, {}, body, -1), "N");
    raw_case("c6.nc.countmore", notecard(2, {embedded(std_item_lines(), 0)}, body, 2), "N");
    {
        std::string s = notecard(2, {embedded(std_item_lines(), 0)}, body);
        std::string crlf;
        for (char c : s)
        {
            if (c == '\n') crlf += "\r\n";
            else crlf += c;
        }
        raw_case("c6.nc.crlf", crlf, "N");
    }
    raw_case("c6.nc.garbage", "Not a notecard\n", "N");
    raw_case("c6.nc.utf8", notecard(2, {}, "caf\xc3\xa9 \xf0\x9f\x98\x80\n"), "N");
    // Text length exactly at the reader's limit and one past it (audit F8): with
    // LLNotecard's default limit (MAX_SIZE, 65536) and with a constructed limit of 64.
    for (S32 limit : {64, 0})
    {
        const S32 max = limit ? limit : 65536;
        for (S32 len : {max - 1, max, max + 1})
        {
            std::string text(len, 'x');
            text[len - 1] = '\n';
            v.push_back({"c6.nc.limit" + d(max) + ".len" + d(len), "N", notecard(2, {}, text), {}, limit});
        }
    }
    return v;
}

template<class I> void c6_run(const LegacyCase& c, Sink& s)
{
    const std::string& cs = c.id;
    for (char r : c.readers)
    {
        std::istringstream is(c.input);
        switch (r)
        {
        case 'I':
        {
            LLPointer<typename I::Item> it = make_item<I>(prev_item());
            const bool ok = it->importLegacyStream(is);
            emit(s, cs, "item", item_cols(*it));
            if (!c.baseline.empty())
            {
                std::istringstream bs(c.baseline);
                LLPointer<typename I::Item> base = make_item<I>(prev_item());
                base->importLegacyStream(bs);
                emit(s, cs, "item.invariants", {perm_invariants(it->getPermissions())});
                emit(s, cs, "item.no_widening", {no_widening(it->getPermissions(), base->getPermissions())});
            }
            std::vector<std::string> st = stream_cols(is, c.input);
            st.insert(st.begin(), bl(ok));
            emit(s, cs, "item.stream", st);
            break;
        }
        case 'O':
        {
            LLPointer<typename I::Object> o = make_obj<I>(prev_cat());
            const bool ok = o->importLegacyStream(is);
            emit(s, cs, "obj", obj_cols(*o));
            std::vector<std::string> st = stream_cols(is, c.input);
            st.insert(st.begin(), bl(ok));
            emit(s, cs, "obj.stream", st);
            break;
        }
        case 'C':
        {
            LLPointer<typename I::Category> o = make_cat<I>(prev_cat());
            const bool ok = o->importLegacyStream(is);
            emit(s, cs, "cat", cat_cols(*o));
            std::vector<std::string> st = stream_cols(is, c.input);
            st.insert(st.begin(), bl(ok));
            emit(s, cs, "cat.stream", st);
            break;
        }
        case 'P':
        {
            typename I::Perm p = make_perm<typename I::Perm>(prev_item().perm);
            const bool ok = p.importLegacyStream(is);
            std::vector<std::string> cols = perm_cols(p);
            cols.insert(cols.begin(), bl(ok));
            emit(s, cs, "perm", cols);
            if (!c.baseline.empty())
            {
                std::istringstream bs(c.baseline);
                typename I::Perm base = make_perm<typename I::Perm>(prev_item().perm);
                base.importLegacyStream(bs);
                emit(s, cs, "perm.invariants", {perm_invariants(p)});
                emit(s, cs, "perm.no_widening", {no_widening(p, base)});
            }
            emit(s, cs, "perm.stream", stream_cols(is, c.input));
            break;
        }
        case 'S':
        {
            typename I::Sale sale = make_sale<typename I::Sale>(1, 77);
            bool has = true;
            U32 pm = 0xa5a5a5a5;
            const bool ok = sale.importLegacyStream(is, has, pm);
            std::vector<std::string> cols = sale_cols(sale);
            cols.push_back(bl(has));
            cols.push_back(x8(pm));
            cols.push_back(bl(ok));
            emit(s, cs, "sale", cols);
            emit(s, cs, "sale.stream", stream_cols(is, c.input));
            break;
        }
        case 'N':
        {
            typename I::Notecard nc(c.max_text ? c.max_text : (S32)I::Notecard::MAX_SIZE);
            const bool ok = nc.importStream(is);
            emit(s, cs, "notecard", {bl(ok), d(nc.getVersion()), d(nc.getEmbeddedVersion()), d((S64)nc.getItems().size()),
                                     long_text(nc.getText())});
            for (size_t k = 0; k < nc.getItems().size(); ++k)
            {
                emit(s, cs, "notecard.item." + d((S64)k), item_cols(*nc.getItems()[k]));
            }
            emit(s, cs, "notecard.stream", stream_cols(is, c.input));
            if (ok)
            {
                std::ostringstream os;
                const bool eok = nc.exportStream(os);
                emit(s, cs, "notecard.export", {bl(eok), long_text(os.str())});
            }
            break;
        }
        default:
            break;
        }
    }
}

template<class I> void c6_legacy(Sink& s)
{
    for (const LegacyCase& c : c6_cases())
    {
        emit(s, c.id, "input", {c.readers, esc(c.input)});
        c6_run<I>(c, s);
    }
    // A notecard built and exported by the viewer's own writer (three items).
    {
        typename I::Notecard nc;
        std::vector<LLPointer<typename I::Item> > items;
        const auto cases = c5_item_cases();
        for (size_t k : {(size_t)0, (size_t)40, (size_t)120})
        {
            items.push_back(make_item<I>(cases[k % cases.size()].f));
        }
        nc.setItems(items);
        nc.setText("Text with an embedded item \xf4\x80\x80\x80 marker\n");
        std::ostringstream os;
        const bool ok = nc.exportStream(os);
        emit(s, "c6.nc.export", "notecard.export", {bl(ok), esc(os.str())});
        std::istringstream is(os.str());
        typename I::Notecard back;
        const bool rok = back.importStream(is);
        emit(s, "c6.nc.export", "notecard.reimport", {bl(rok), d(back.getVersion()), d((S64)back.getItems().size()), esc(back.getText())});
        for (size_t k = 0; k < back.getItems().size(); ++k)
        {
            emit(s, "c6.nc.export", "notecard.reimport.item." + d((S64)k), item_cols(*back.getItems()[k]));
        }
    }
}

//============================================================================
// C7, C8: LLSD input
//============================================================================

struct NamedSD
{
    std::string name;
    LLSD v;
};

std::vector<NamedSD> llsd_values()
{
    LLSD::Binary b0, b3 = {1, 2, 3}, b4 = {0x00, 0x00, 0x01, 0x01}, b5 = {0x80, 0, 0, 1, 0xff};
    LLSD perm_map;
    perm_map["base_mask"] = (LLSD::Integer)0x7fff7fff;
    perm_map["owner_id"] = U(0x77);
    LLSD sale_map;
    sale_map["sale_type"] = "copy";
    sale_map["sale_price"] = 50;
    LLSD script_map;
    script_map["script"]["runtime"] = "luau";
    LLSD arr = LLSD::emptyArray();
    arr.append(1);
    arr.append("x");
    std::vector<NamedSD> v = {
        {"undef", LLSD()}, {"bool.t", true}, {"bool.f", false},
        {"int.0", 0}, {"int.1", 1}, {"int.-1", -1}, {"int.7", 7}, {"int.255", 255}, {"int.256", 256}, {"int.300", 300},
        {"int.max", INT_MAX}, {"int.min", INT_MIN},
        {"real.0", 0.0}, {"real.1.5", 1.5}, {"real.-1", -1.0}, {"real.1e10", 1e10}, {"real.300.7", 300.7},
        {"str.empty", ""}, {"str.texture", "texture"}, {"str.lsltext", "lsltext"}, {"str.script", "script"},
        {"str.notecard", "notecard"}, {"str.invalid", "invalid"}, {"str.-1", "-1"}, {"str.junk", "junk"}, {"str.sp7", " 7"},
        {"str.0x10", "0x10"}, {"str.7", "7"}, {"str.uuid", u(U(0x75))}, {"str.pipes", "name|with|pipes\x01\xc3\xa9"},
        {"uuid.null", LLUUID::null}, {"uuid.75", U(0x75)},
        {"date", LLDate("2026-10-22T10:00:00Z")}, {"uri", LLURI("http://example.com/x")},
        {"bin0", b0}, {"bin3", b3}, {"bin4", b4}, {"bin5", b5},
        {"map.empty", LLSD::emptyMap()},
        {"map.asset_id", LLSD().with("asset_id", U(0x76))},
        {"map.toggled.t", LLSD().with("toggled", true)},
        {"map.toggled.0", LLSD().with("toggled", 0)},
        {"map.runtime", LLSD().with("runtime", "luau")},
        {"map.script", script_map},
        {"map.perm", perm_map},
        {"map.sale", sale_map},
        {"array.empty", LLSD::emptyArray()},
        {"array.1x", arr},
    };
    return v;
}

template<class I> void c7_item_llsd(Sink& s)
{
    typedef typename I::Item Item;
    const std::vector<NamedSD> values = llsd_values();
    const char* keys[] = {"item_id", "parent_id", "permissions", "thumbnail", "thumbnail_id", "favorite", "script", "metadata",
                          "asset_id", "linked_id", "shadow_id", "type", "inv_type", "flags", "sale_info", "name", "desc",
                          "created_at", "callback_id", "hash_id"};
    for (const char* key : keys)
    {
        for (const NamedSD& val : values)
        {
            LLSD sd;
            sd[key] = val.v;
            for (int is_new = 1; is_new >= 0; --is_new)
            {
                LLPointer<Item> it = make_item<I>(prev_item());
                const bool ok = it->fromLLSD(sd, is_new != 0);
                std::vector<std::string> cols = item_cols(*it);
                cols.insert(cols.begin(), bl(ok));
                emit(s, std::string("c7.") + key + "." + val.name + "." + d(is_new), "ret+item", cols);
            }
        }
    }
    // OpenSim branch (permissions survive the prologue).
    for (const char* vname : {"map.perm", "map.empty", "undef"})
    {
        for (const NamedSD& val : values)
        {
            if (val.name != vname) continue;
            LLSD sd;
            sd["permissions"] = val.v;
            sd["name"] = "os";
            for (int is_new = 1; is_new >= 0; --is_new)
            {
                I::set_opensim(true);
                LLPointer<Item> it = make_item<I>(prev_item());
                it->fromLLSD(sd, is_new != 0);
                I::set_opensim(false);
                emit(s, std::string("c7.opensim.") + vname + "." + d(is_new), "item", item_cols(*it));
            }
        }
    }
    // Whole-document non-maps.
    for (const NamedSD& val : values)
    {
        if (val.v.isMap()) continue;
        LLPointer<Item> it = make_item<I>(prev_item());
        const bool ok = it->fromLLSD(val.v, true);
        std::vector<std::string> cols = item_cols(*it);
        cols.insert(cols.begin(), bl(ok));
        emit(s, "c7.doc." + val.name, "ret+item", cols);
    }
    // asset_id / linked_id / shadow_id subsets, over two base masks.
    for (int mask = 1; mask < 8; ++mask)
    {
        LLSD sd;
        if (mask & 1) sd["asset_id"] = U(0x81);
        if (mask & 2) sd["linked_id"] = U(0x82);
        if (mask & 4) sd["shadow_id"] = U(0x83);
        for (int is_new = 1; is_new >= 0; --is_new)
        {
            LLPointer<Item> it = make_item<I>(prev_item());
            it->fromLLSD(sd, is_new != 0);
            emit(s, "c7.assetkeys." + d(mask) + "." + d(is_new), "item", item_cols(*it));
        }
    }
    // thumbnail with thumbnail_id.
    for (int k = 0; k < 4; ++k)
    {
        LLSD sd;
        sd["thumbnail"] = (k & 1) ? LLSD().with("asset_id", U(0x84)) : LLSD::emptyMap();
        if (k & 2) sd["thumbnail_id"] = U(0x85);
        LLPointer<Item> it = make_item<I>(prev_item());
        it->fromLLSD(sd, true);
        emit(s, "c7.thumbs." + d(k), "item", item_cols(*it));
    }
    // UUIDs given as Strings (LLSD String -> UUID, LLUUID::set leniency; audit D02).
    {
        const std::string as = u(U(0x81));
        std::vector<std::pair<std::string, std::string> > forms = {
            {"plain", as}, {"lead_space", " " + as}, {"trail_space", as + " "}, {"lead_tab", "\t" + as}, {"trail_nl", as + "\n"},
            {"nohyphens", "8101234567abcdef1032547698ba81"}, {"empty", ""}, {"short34", as.substr(0, 34)}, {"long37", as + "0"},
        };
        std::string upper = as;
        LLStringUtil::toUpper(upper);
        forms.push_back({"upper", upper});
        for (size_t slot : {(size_t)8, (size_t)13, (size_t)18, (size_t)23})
        {
            std::string v = as;
            v[slot] = 'g';
            forms.push_back({"slot" + d((S64)slot), v});
            v = as;
            v.erase(slot, 1);
            forms.push_back({"short35.at" + d((S64)slot), v});
        }
        for (const auto& f : forms)
        {
            LLSD sd;
            sd["asset_id"] = f.second;
            sd["item_id"] = f.second;
            LLPointer<Item> it = make_item<I>(prev_item());
            it->fromLLSD(sd, true);
            emit(s, "c7.uuidstr." + f.first, "item", item_cols(*it));
        }
    }
    // script, metadata.script, both, metadata without script.
    for (int k = 0; k < 4; ++k)
    {
        LLSD sd;
        if (k == 0 || k == 2) sd["script"]["runtime"] = "direct";
        if (k == 1 || k == 2) sd["metadata"]["script"]["runtime"] = "wrapped";
        if (k == 3) sd["metadata"]["thumbnail"]["asset_id"] = U(0x86);
        sd["type"] = "lsltext";
        for (int is_new = 1; is_new >= 0; --is_new)
        {
            LLPointer<Item> it = make_item<I>(prev_item());
            it->fromLLSD(sd, is_new != 0);
            emit(s, "c7.script." + d(k) + "." + d(is_new), "item", item_cols(*it));
        }
    }
    // sale_info with perm_mask (Binary 0/3/4/5 bytes, Integer, absent), with and without permissions.
    {
        LLSD::Binary b0, b3 = {0xff, 0xff, 0xff}, b4 = {0x00, 0x00, 0x60, 0x00}, b5 = {0x00, 0x00, 0x80, 0x00, 0x01}, bz = {0, 0, 0, 0}, bmod = {0x00, 0x00, 0x40, 0x00}, bmove = {0x00, 0x08, 0x00, 0x00}, bcopy = {0x00, 0x00, 0x80, 0x00};
        const std::vector<NamedSD> pms = {{"absent", LLSD()}, {"bin0", b0}, {"bin3", b3}, {"bin4", b4}, {"bin5", b5},
                                          {"bin4zero", bz}, {"int", 0x6000}, {"str", "6000"},
                                          {"bin4mod", bmod}, {"bin4move", bmove}, {"bin4copy", bcopy}};
        for (size_t i = 0; i < pms.size(); ++i)
        {
            for (int with_perm = 0; with_perm < 2; ++with_perm)
            {
                LLSD sd;
                sd["sale_info"]["sale_type"] = "orig";
                sd["sale_info"]["sale_price"] = 7;
                if (i != 0) sd["sale_info"]["perm_mask"] = pms[i].v;
                if (with_perm)
                {
                    sd["permissions"]["owner_mask"] = (LLSD::Integer)0x0008e000;
                    sd["permissions"]["base_mask"] = (LLSD::Integer)0x7fffffff;
                    sd["permissions"]["owner_id"] = ID_A();
                }
                LLPointer<Item> it = make_item<I>(prev_item());
                it->fromLLSD(sd, true);
                emit(s, "c7.permmask." + pms[i].name + "." + d(with_perm), "item", item_cols(*it));
            }
        }
    }
    // permissions map keys x value types.
    {
        const char* pkeys[] = {"creator_id", "owner_id", "last_owner_id", "group_id", "is_owner_group", "base_mask",
                               "owner_mask", "group_mask", "everyone_mask", "next_owner_mask"};
        const std::vector<NamedSD> pvals = {{"int.max", INT_MAX}, {"int.-1", -1}, {"int.e000", 0xe000}, {"real", 1.5e9},
                                            {"str.123", "123"}, {"str.uuid", u(U(0x87))}, {"uuid", U(0x87)}, {"bool", true},
                                            {"undef", LLSD()}};
        for (const char* pk : pkeys)
        {
            for (const NamedSD& pv : pvals)
            {
                LLSD sd;
                sd["permissions"][pk] = pv.v;
                LLPointer<Item> it = make_item<I>(prev_item());
                it->fromLLSD(sd, true);
                emit(s, std::string("c7.perm.") + pk + "." + pv.name, "item", item_cols(*it));
                typename I::Perm p = I::perm_from_sd(sd["permissions"]);
                emit(s, std::string("c7.perm.") + pk + "." + pv.name, "perm_from_sd", perm_cols(p));
            }
        }
        // Group ownership derived from ids (is_owner_group is never read).
        LLSD sd;
        sd["permissions"]["owner_id"] = LLUUID::null;
        sd["permissions"]["group_id"] = ID_G();
        sd["permissions"]["is_owner_group"] = false;
        LLPointer<Item> it = make_item<I>(prev_item());
        it->fromLLSD(sd, true);
        emit(s, "c7.perm.derived_group", "item", item_cols(*it));
    }
    // Reuse of one object across records (the legacy HTTP fetch loop).
    {
        LLPointer<Item> it = make_item<I>(prev_item());
        const auto cases = c5_item_cases();
        for (size_t k = 0; k < 6; ++k)
        {
            LLSD sd = make_item<Oracle>(cases[(k * 37) % cases.size()].f)->asLLSD();
            if (k % 2) sd.erase("name");
            if (k % 3 == 1) sd.erase("thumbnail");
            if (k == 4) sd.erase("permissions");
            it->fromLLSD(sd, true);
            emit(s, "c7.reuse." + d((S64)k), "item", item_cols(*it));
        }
    }
}

template<class I> void c8_category_llsd(Sink& s)
{
    typedef typename I::Category Category;
    const std::vector<NamedSD> values = llsd_values();
    const char* keys[] = {"category_id", "item_id", "cat_id", "parent_id", "thumbnail", "thumbnail_id", "favorite", "type",
                          "type_default", "preferred_type", "name", "version", "owner_id"};
    for (const char* key : keys)
    {
        for (const NamedSD& val : values)
        {
            LLSD sd;
            sd[key] = val.v;
            const std::string cs = std::string("c8.") + key + "." + val.name;
            LLPointer<Category> c = make_cat<I>(prev_cat());
            const bool ok = c->fromLLSD(sd);
            std::vector<std::string> cols = cat_cols(*c);
            cols.insert(cols.begin(), bl(ok));
            emit(s, cs, "fromllsd", cols);
            LLPointer<Category> m = make_cat<I>(prev_cat());
            const bool r = m->importLLSD(key, val.v);
            m->importLLSDMap(sd); // the map reader on top (same key again)
            cols = cat_cols(*m);
            cols.insert(cols.begin(), bl(r));
            emit(s, cs, "importllsd", cols);
            if (!strcmp(key, "owner_id") || !strcmp(key, "version") || !strcmp(key, "favorite") || !strcmp(key, "name") ||
                !strcmp(key, "cat_id"))
            {
                LLPointer<typename I::VCat> v = make_vcat<I>(prev_cat());
                const bool vr = v->importLLSD(key, val.v);
                cols = vcat_cols(*v);
                cols.insert(cols.begin(), bl(vr));
                emit(s, cs, "vimportllsd", cols);
            }
        }
    }
    // Combinations.
    for (int k = 0; k < 4; ++k)
    {
        LLSD sd;
        if (k & 1) sd["thumbnail"] = LLSD().with("asset_id", U(0x88));
        else sd["thumbnail"] = LLSD::emptyMap();
        if (k & 2) sd["thumbnail_id"] = U(0x89);
        LLPointer<Category> c = make_cat<I>(prev_cat());
        c->fromLLSD(sd);
        emit(s, "c8.thumbs." + d(k), "fromllsd", cat_cols(*c));
    }
    {
        const std::vector<NamedSD> tv = {{"int", 46}, {"str", "46"}, {"strname", "current"}, {"real", 46.9}};
        for (const NamedSD& a : tv)
        {
            for (const NamedSD& b : tv)
            {
                LLSD sd;
                sd["type"] = a.v;
                sd["type_default"] = b.v;
                LLPointer<Category> c = make_cat<I>(prev_cat());
                c->fromLLSD(sd);
                emit(s, "c8.typepair." + a.name + "." + b.name, "fromllsd", cat_cols(*c));
            }
        }
    }
    // One object reused across records (the legacy fetch loop and the cache loader's dialect).
    {
        LLPointer<Category> c = make_cat<I>(prev_cat());
        const auto cases = c5_cat_cases();
        for (size_t k = 0; k < 6; ++k)
        {
            LLSD sd = make_cat<Oracle>(cases[(k * 29) % cases.size()].f)->asAISCreateCatLLSD();
            if (k % 2) sd.erase("name");
            if (k % 3 == 1) sd.erase("thumbnail");
            c->fromLLSD(sd);
            emit(s, "c8.reuse." + d((S64)k), "fromllsd", cat_cols(*c));
        }
        LLPointer<typename I::VCat> v = make_vcat<I>(prev_cat());
        for (size_t k = 0; k < 6; ++k)
        {
            LLSD sd;
            make_vcat<Oracle>(cases[(k * 31) % cases.size()].f)->exportLLSD(sd);
            if (k % 2) sd.erase("version");
            if (k % 3 == 2) sd.erase("preferred_type");
            v->importLLSDMap(sd);
            emit(s, "c8.vreuse." + d((S64)k), "vimportmap", vcat_cols(*v));
        }
    }
}

//============================================================================
// C9: message blocks
//============================================================================

static LLUUID AGENT() { return U(0xf1); }
static LLUUID SESSION() { return U(0xf2); }
static LLUUID TRANSACTION() { return U(0xf3); }

template<class I> std::vector<U8> pack_update_inventory_item(typename I::VItem* item)
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_UpdateInventoryItem);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, AGENT());
    msg->addUUIDFast(_PREHASH_SessionID, SESSION());
    msg->addUUIDFast(_PREHASH_TransactionID, item->mTransactionID);
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_CallbackID, 0);
    item->packMessage(msg);
    return built_bytes();
}

template<class I> std::vector<U8> pack_update_task_inventory(typename I::VItem* item)
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_UpdateTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, AGENT());
    msg->addUUIDFast(_PREHASH_SessionID, SESSION());
    msg->nextBlockFast(_PREHASH_UpdateData);
    msg->addU32Fast(_PREHASH_LocalID, 0x01020304);
    msg->addU8Fast(_PREHASH_Key, I::task_item_key());
    msg->nextBlockFast(_PREHASH_InventoryData);
    item->packMessage(msg);
    return built_bytes();
}

template<class I> std::vector<U8> pack_bulk_update(const std::vector<typename I::Item*>& items)
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_BulkUpdateInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, AGENT());
    msg->addUUIDFast(_PREHASH_TransactionID, TRANSACTION());
    for (typename I::Item* it : items)
    {
        msg->nextBlockFast(_PREHASH_ItemData);
        msg->addU32Fast(_PREHASH_CallbackID, 7);
        it->packMessage(msg);
    }
    return built_bytes();
}

// A raw ItemData block (any byte values).
struct UdpItem
{
    LLUUID item, folder, creator, owner, group, asset;
    U32 masks[5] = {0, 0, 0, 0, 0};
    bool group_owned = false;
    S8 type = 0, inv_type = 0;
    U32 flags = 0;
    U8 sale_type = 0;
    S32 sale_price = 0;
    std::string name, desc;
    S32 date = 0;
    U32 crc = 0;
};

std::vector<U8> pack_bulk_raw(const std::vector<UdpItem>& items, const std::vector<CatF>& folders)
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_BulkUpdateInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, AGENT());
    msg->addUUIDFast(_PREHASH_TransactionID, TRANSACTION());
    for (const CatF& f : folders)
    {
        msg->nextBlockFast(_PREHASH_FolderData);
        msg->addUUIDFast(_PREHASH_FolderID, f.id);
        msg->addUUIDFast(_PREHASH_ParentID, f.parent);
        msg->addS8Fast(_PREHASH_Type, (S8)f.pref);
        msg->addStringFast(_PREHASH_Name, f.name);
    }
    for (const UdpItem& u : items)
    {
        msg->nextBlockFast(_PREHASH_ItemData);
        msg->addUUIDFast(_PREHASH_ItemID, u.item);
        msg->addU32Fast(_PREHASH_CallbackID, 0);
        msg->addUUIDFast(_PREHASH_FolderID, u.folder);
        msg->addUUIDFast(_PREHASH_CreatorID, u.creator);
        msg->addUUIDFast(_PREHASH_OwnerID, u.owner);
        msg->addUUIDFast(_PREHASH_GroupID, u.group);
        msg->addU32Fast(_PREHASH_BaseMask, u.masks[0]);
        msg->addU32Fast(_PREHASH_OwnerMask, u.masks[1]);
        msg->addU32Fast(_PREHASH_GroupMask, u.masks[2]);
        msg->addU32Fast(_PREHASH_EveryoneMask, u.masks[3]);
        msg->addU32Fast(_PREHASH_NextOwnerMask, u.masks[4]);
        msg->addBOOLFast(_PREHASH_GroupOwned, u.group_owned);
        msg->addUUIDFast(_PREHASH_AssetID, u.asset);
        msg->addS8Fast(_PREHASH_Type, u.type);
        msg->addS8Fast(_PREHASH_InvType, u.inv_type);
        msg->addU32Fast(_PREHASH_Flags, u.flags);
        msg->addU8Fast(_PREHASH_SaleType, u.sale_type);
        msg->addS32Fast(_PREHASH_SalePrice, u.sale_price);
        msg->addStringFast(_PREHASH_Name, u.name);
        msg->addStringFast(_PREHASH_Description, u.desc);
        msg->addS32Fast(_PREHASH_CreationDate, u.date);
        msg->addU32Fast(_PREHASH_CRC, u.crc);
    }
    return built_bytes();
}

UdpItem udp_from(const ItemF& f)
{
    UdpItem u;
    u.item = f.id;
    u.folder = f.parent;
    u.creator = f.perm.creator;
    u.owner = f.perm.owner;
    u.group = f.perm.group;
    u.asset = f.asset;
    u.masks[0] = f.perm.base;
    u.masks[1] = f.perm.owner_mask;
    u.masks[2] = f.perm.group_mask;
    u.masks[3] = f.perm.everyone;
    u.masks[4] = f.perm.next;
    u.group_owned = f.perm.group_owned;
    u.type = (S8)f.type;
    u.inv_type = (S8)f.inv_type;
    u.flags = f.flags;
    u.sale_type = (U8)f.sale_type;
    u.sale_price = f.sale_price;
    u.name = f.name;
    u.desc = f.desc;
    u.date = (S32)f.date;
    // The CRC the frozen copy computes for the same record (thumbnail null).
    ItemF g = f;
    g.thumb = LLUUID::null;
    u.crc = make_item<Oracle>(g)->getCRC32();
    return u;
}

std::vector<std::pair<std::string, UdpItem> > c9_udp_variants()
{
    std::vector<std::pair<std::string, UdpItem> > v;
    const ItemF b = base_item();
    UdpItem base = udp_from(b);
    v.push_back({"base", base});
    UdpItem x = base;
    x.crc += 1;
    v.push_back({"badcrc", x});
    for (S8 t : {(S8)-2, (S8)127, (S8)-128, (S8)26, (S8)24})
    {
        ItemF f = b;
        f.type = t;
        v.push_back({"type." + d(t), udp_from(f)});
    }
    for (S8 t : {(S8)100, (S8)-100, (S8)29, (S8)2, (S8)3, (S8)-1})
    {
        ItemF f = b;
        f.inv_type = t;
        v.push_back({"invtype." + d(t), udp_from(f)});
    }
    for (U8 st : {(U8)4, (U8)200, (U8)255, (U8)3})
    {
        ItemF f = b;
        f.sale_type = st;
        v.push_back({"saletype." + d(st), udp_from(f)});
    }
    for (S32 p : {-5, INT_MIN, INT_MAX})
    {
        ItemF f = b;
        f.sale_price = p;
        v.push_back({"price." + d(p), udp_from(f)});
    }
    const auto names = c5_names();
    for (size_t i = 0; i < names.size(); ++i)
    {
        ItemF f = b;
        f.name = names[i].first;
        f.desc = names[i].second;
        v.push_back({"name." + d((S64)i), udp_from(f)});
    }
    for (size_t len : {(size_t)254, (size_t)255, (size_t)256})
    {
        // The builder's Variable 1 limit (255 bytes with the NUL) at limit-1, limit, limit+1.
        ItemF f = b;
        f.name = std::string(len, 'n');
        f.desc = std::string(len, 'd');
        v.push_back({"strlen." + d((S64)len), udp_from(f)});
    }
    {
        ItemF f = b;
        f.perm.group_owned = true;
        f.perm.group_mask = PERM_ALL;
        f.perm.everyone = PERM_ALL;
        v.push_back({"rawperm", udp_from(f)});
        f = b;
        f.date = -1;
        v.push_back({"date.-1", udp_from(f)});
        f = b;
        f.flags = 0xffffffff;
        v.push_back({"flags.ffffffff", udp_from(f)});
    }
    return v;
}

template<class I> void c9_messages(Sink& s)
{
    typedef typename I::Item Item;
    // Packing every C5 record.
    for (const ItemCase& c : c5_item_cases())
    {
        LLPointer<typename I::VItem> vi = make_vitem<I>(c.f, TRANSACTION());
        emit(s, c.id, "UpdateInventoryItem", {packet_hex(pack_update_inventory_item<I>(vi.get()))});
        emit(s, c.id, "UpdateTaskInventory", {packet_hex(pack_update_task_inventory<I>(vi.get()))});
        LLPointer<Item> it = make_item<I>(c.f);
        std::vector<Item*> items = {it.get()};
        const std::vector<U8> bulk = pack_bulk_update<I>(items);
        emit(s, c.id, "BulkUpdateInventory", {packet_hex(bulk)});
        LLPointer<Item> fresh = new Item;
        LLPointer<Item> reused = make_item<I>(prev_item());
        bool r1 = false, r2 = false;
        dispatch("BulkUpdateInventory", bulk, [&](LLMessageSystem* msg) {
            r1 = fresh->unpackMessage(msg, _PREHASH_ItemData, 0);
            r2 = reused->unpackMessage(msg, _PREHASH_ItemData, 0);
        });
        std::vector<std::string> cols = item_cols(*fresh);
        cols.insert(cols.begin(), bl(r1));
        emit(s, c.id, "unpack.fresh", cols);
        cols = item_cols(*reused);
        cols.insert(cols.begin(), bl(r2));
        emit(s, c.id, "unpack.reused", cols);
    }
    // Raw blocks.
    const auto variants = c9_udp_variants();
    for (const auto& var : variants)
    {
        const std::vector<U8> bytes = pack_bulk_raw({var.second}, {});
        const std::string cs = "c9.udp." + var.first;
        emit(s, cs, "packet", {packet_hex(bytes)});
        LLPointer<Item> fresh = new Item;
        LLPointer<Item> reused = make_item<I>(prev_item());
        bool r1 = false, r2 = false;
        dispatch("BulkUpdateInventory", bytes, [&](LLMessageSystem* msg) {
            r1 = fresh->unpackMessage(msg, _PREHASH_ItemData, 0);
            r2 = reused->unpackMessage(msg, _PREHASH_ItemData, 0);
        });
        std::vector<std::string> cols = item_cols(*fresh);
        cols.insert(cols.begin(), bl(r1));
        emit(s, cs, "unpack.fresh", cols);
        cols = item_cols(*reused);
        cols.insert(cols.begin(), bl(r2));
        emit(s, cs, "unpack.reused", cols);
    }
    // Several blocks into one reused object (llinventorymodel.cpp:5523).
    {
        std::vector<UdpItem> four;
        for (size_t k = 0; k < 4; ++k) four.push_back(variants[(k * 5) % variants.size()].second);
        const std::vector<U8> bytes = pack_bulk_raw(four, {});
        emit(s, "c9.udp.multi", "packet", {packet_hex(bytes)});
        LLPointer<Item> reused = make_item<I>(prev_item());
        dispatch("BulkUpdateInventory", bytes, [&](LLMessageSystem* msg) {
            const S32 n = msg->getNumberOfBlocksFast(_PREHASH_ItemData);
            emit(s, "c9.udp.multi", "blocks", {d(n)});
            for (S32 k = 0; k < n; ++k)
            {
                const bool r = reused->unpackMessage(msg, _PREHASH_ItemData, k);
                std::vector<std::string> cols = item_cols(*reused);
                cols.insert(cols.begin(), bl(r));
                emit(s, "c9.udp.multi", "unpack." + d(k), cols);
            }
        });
    }
    // Categories: UpdateInventoryFolder from packMessage; FolderData unpack.
    for (const CatCase& c : c5_cat_cases())
    {
        // The viewer's senders pack through LLViewerInventoryCategory::packMessage (frozen copy).
        LLPointer<typename I::VCat> cat = make_vcat<I>(c.f);
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_UpdateInventoryFolder);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, AGENT());
        msg->addUUIDFast(_PREHASH_SessionID, SESSION());
        msg->nextBlockFast(_PREHASH_FolderData);
        cat->packMessage(msg);
        const std::vector<U8> bytes = built_bytes();
        emit(s, c.id, "UpdateInventoryFolder", {packet_hex(bytes)});
        LLPointer<typename I::Category> back = make_cat<I>(prev_cat());
        dispatch("UpdateInventoryFolder", bytes, [&](LLMessageSystem* m) { back->unpackMessage(m, _PREHASH_FolderData, 0); });
        emit(s, c.id, "unpack", cat_cols(*back));
    }
    {
        CatF f;
        f.id = U(0xd5);
        f.parent = U(0xd6);
        f.pref = -100;
        f.name = "raw|name\xc3\xa9\x01";
        const std::vector<U8> bytes = pack_bulk_raw({}, {f});
        emit(s, "c9.folder.raw", "packet", {packet_hex(bytes)});
        LLPointer<typename I::Category> back = make_cat<I>(prev_cat());
        dispatch("BulkUpdateInventory", bytes, [&](LLMessageSystem* m) { back->unpackMessage(m, _PREHASH_FolderData, 0); });
        emit(s, "c9.folder.raw", "unpack", cat_cols(*back));
    }
    // ObjectProperties: aggregate permissions, LLCategory, sale info.
    auto object_properties = [](U8 agg, U32 category, U8 sale_type, S32 price,
                                const std::function<void(LLMessageSystem*)>& custom) {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_ObjectProperties);
        msg->nextBlockFast(_PREHASH_ObjectData);
        msg->addUUIDFast(_PREHASH_ObjectID, U(0x91));
        msg->addUUIDFast(_PREHASH_CreatorID, U(0x92));
        msg->addUUIDFast(_PREHASH_OwnerID, U(0x93));
        msg->addUUIDFast(_PREHASH_GroupID, U(0x94));
        msg->addU64Fast(_PREHASH_CreationDate, 0);
        msg->addU32Fast(_PREHASH_BaseMask, PERM_ALL);
        msg->addU32Fast(_PREHASH_OwnerMask, PERM_ALL);
        msg->addU32Fast(_PREHASH_GroupMask, 0);
        msg->addU32Fast(_PREHASH_EveryoneMask, 0);
        msg->addU32Fast(_PREHASH_NextOwnerMask, PERM_ALL);
        msg->addS32Fast(_PREHASH_OwnershipCost, 0);
        if (!custom)
        {
            msg->addU8Fast(_PREHASH_SaleType, sale_type);
            msg->addS32Fast(_PREHASH_SalePrice, price);
            msg->addU8Fast(_PREHASH_AggregatePerms, agg);
            msg->addU32Fast(_PREHASH_Category, category);
        }
        else
        {
            custom(msg);
        }
        msg->addU8Fast(_PREHASH_AggregatePermTextures, 0);
        msg->addU8Fast(_PREHASH_AggregatePermTexturesOwner, 0);
        msg->addS16Fast(_PREHASH_InventorySerial, 0);
        msg->addUUIDFast(_PREHASH_ItemID, LLUUID::null);
        msg->addUUIDFast(_PREHASH_FolderID, LLUUID::null);
        msg->addUUIDFast(_PREHASH_FromTaskID, LLUUID::null);
        msg->addUUIDFast(_PREHASH_LastOwnerID, LLUUID::null);
        msg->addStringFast(_PREHASH_Name, "obj");
        msg->addStringFast(_PREHASH_Description, "");
        msg->addStringFast(_PREHASH_TouchName, "");
        msg->addStringFast(_PREHASH_SitName, "");
        msg->addStringFast(_PREHASH_TextureID, "");
        return built_bytes();
    };
    typedef typename I::Agg Agg;
    std::vector<Agg> agg_states(256);
    for (U32 a = 0; a < 256; ++a)
    {
        const std::vector<U8> bytes = object_properties((U8)a, 0, 0, 0, nullptr);
        Agg ag;
        dispatch("ObjectProperties", bytes, [&](LLMessageSystem* m) { ag.unpackMessage(m, _PREHASH_ObjectData, _PREHASH_AggregatePerms, 0); });
        agg_states[a] = ag;
        const std::vector<U8> repacked = object_properties(0, 0, 0, 0, [&](LLMessageSystem* m) {
            m->addU8Fast(_PREHASH_SaleType, 0);
            m->addS32Fast(_PREHASH_SalePrice, 0);
            ag.packMessage(m, _PREHASH_AggregatePerms);
            m->addU32Fast(_PREHASH_Category, 0);
        });
        U8 back = 0;
        dispatch("ObjectProperties", repacked, [&](LLMessageSystem* m) { m->getU8Fast(_PREHASH_ObjectData, _PREHASH_AggregatePerms, back, 0); });
        emit(s, "c9.agg." + x2(a), "agg", {x2(ag.getU8()), d(ag.getValue(PERM_COPY)), d(ag.getValue(PERM_MODIFY)),
                                          d(ag.getValue(PERM_TRANSFER)), bl(ag.isEmpty()), x2(back)});
    }
    for (U32 a = 0; a < 64; ++a)
    {
        std::string row;
        for (U32 b = 0; b < 64; ++b)
        {
            Agg x = agg_states[a];
            x.aggregate(agg_states[b]);
            row += x2(x.getU8());
        }
        emit(s, "c9.aggagg." + x2(a), "b0..63", {row});
    }
    for (U32 value : {0x00000000u, 0x01000000u, 0x06000000u, 0x01010000u, 0x06ff01ffu, 0xffffffffu, 0x12345678u, 0x00000001u})
    {
        typename I::Cat cat(value);
        const std::vector<U8> bytes = object_properties(0, 0, 0, 0, [&](LLMessageSystem* m) {
            m->addU8Fast(_PREHASH_SaleType, 0);
            m->addS32Fast(_PREHASH_SalePrice, 0);
            m->addU8Fast(_PREHASH_AggregatePerms, 0);
            cat.packMessage(m);
        });
        typename I::Cat c1, c2;
        dispatch("ObjectProperties", bytes, [&](LLMessageSystem* m) {
            c1.unpackMessage(m, _PREHASH_ObjectData);
            c2.unpackMultiMessage(m, _PREHASH_ObjectData, 0);
        });
        emit(s, "c9.category." + x8(value), "category", {packet_hex(bytes), x8(c1.getU32()), x8(c2.getU32())});
    }
    for (U8 st : {(U8)0, (U8)1, (U8)2, (U8)3, (U8)4, (U8)200, (U8)255})
    {
        for (S32 p : {-5, 0, 10, INT_MAX, INT_MIN})
        {
            const std::vector<U8> bytes = object_properties(0, 0, st, p, nullptr);
            typename I::Sale s1 = make_sale<typename I::Sale>(1, 77), s2 = make_sale<typename I::Sale>(1, 77);
            dispatch("ObjectProperties", bytes, [&](LLMessageSystem* m) {
                s1.unpackMessage(m, _PREHASH_ObjectData);
                s2.unpackMultiMessage(m, _PREHASH_ObjectData, 0);
            });
            std::vector<std::string> cols = sale_cols(s1);
            const std::vector<std::string> c2 = sale_cols(s2);
            cols.insert(cols.end(), c2.begin(), c2.end());
            emit(s, "c9.sale." + d(st) + "." + d(p), "sale", cols);
            // and packMessage of a raw sale.
            typename I::Sale raw = make_sale<typename I::Sale>(st, p);
            const std::vector<U8> packed = object_properties(0, 0, 0, 0, [&](LLMessageSystem* m) {
                raw.packMessage(m);
                m->addU8Fast(_PREHASH_AggregatePerms, 0);
                m->addU32Fast(_PREHASH_Category, 0);
            });
            emit(s, "c9.sale." + d(st) + "." + d(p), "packed", {packet_hex(packed)});
        }
    }
}

//============================================================================
// C10: the Linden Library cache
//============================================================================

static const char* LIBRARY_FILE = "library/ba2a564a-f0f1-4b82-9c61-b7520bfcd09f.inv.llsd.gz";

bool gunzip_file(const std::string& path, std::string& out)
{
    gzFile f = gzopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    int n;
    out.clear();
    while ((n = gzread(f, buf, sizeof(buf))) > 0)
    {
        out.append(buf, n);
    }
    gzclose(f);
    return n == 0;
}

bool load_cache(const std::string& path, LLSD& inventory, S32& version)
{
    std::string data;
    if (!gunzip_file(path, data) || data.size() < 4) return false;
    version = (S32)(((U32)(U8)data[0] << 24) | ((U32)(U8)data[1] << 16) | ((U32)(U8)data[2] << 8) | (U32)(U8)data[3]);
    std::istringstream is(data.substr(4));
    LLPointer<LLSDParser> parser = new LLSDBinaryParser();
    return parser->parse(is, inventory, LLSDSerialize::SIZE_UNLIMITED) != LLSDParser::PARSE_FAILURE;
}

struct LibraryStats
{
    U64 items = 0, items_identical = 0, cats = 0, cats_identical = 0;
};

template<class I> LibraryStats c10_library(const LLSD& inventory, Sink* s)
{
    LibraryStats st;
    const LLSD& cats = inventory["categories"];
    for (S32 k = 0; k < cats.size(); ++k)
    {
        LLPointer<typename I::VCat> v = make_vcat<I>(CatF());
        v->setVersion(-1);
        v->importLLSDMap(cats[k]);
        LLSD again;
        v->exportLLSD(again);
        const bool same = llsd_bin(again) == llsd_bin(cats[k]);
        st.cats++;
        if (same) st.cats_identical++;
        if (s)
        {
            std::vector<std::string> cols = vcat_cols(*v);
            cols.insert(cols.begin(), bl(same));
            emit(*s, "c10.cat." + d(k), "same+vcat", cols);
        }
    }
    const LLSD& items = inventory["items"];
    for (S32 k = 0; k < items.size(); ++k)
    {
        LLPointer<typename I::Item> it = new typename I::Item;
        const bool ok = it->fromLLSD(items[k]);
        const bool same = llsd_bin(it->asLLSD()) == llsd_bin(items[k]);
        st.items++;
        if (same) st.items_identical++;
        if (s)
        {
            std::vector<std::string> cols = item_cols(*it);
            cols.insert(cols.begin(), bl(same));
            cols.insert(cols.begin(), bl(ok));
            emit(*s, "c10.item." + d(k), "ret+same+item", cols);
        }
    }
    if (s)
    {
        emit(*s, "c10.summary", "counts", {d((S64)st.cats), d((S64)st.cats_identical), d((S64)st.items), d((S64)st.items_identical)});
    }
    return st;
}

//============================================================================
// Corpus registry
//============================================================================

struct Structured
{
    const char* name;
    const char* file;
    const char* what;
    void (*oracle)(Sink&);
    void (*live)(Sink&);
};

static const Structured kStructured[] = {
    {"c1", "c1_types.txt", "C1 type tables", &c1_types<Oracle>, &c1_types<Live>},
    {"c2", "c2_normalisers.txt", "C2 name and description normalisers", &c2_normalisers<Oracle>, &c2_normalisers<Live>},
    {"c3", "c3_permissions.txt", "C3 permissions, structured cases", &c3_permissions<Oracle>, &c3_permissions<Live>},
    {"c4", "c4_sale.txt", "C4 sale info, structured cases", &c4_sale<Oracle>, &c4_sale<Live>},
    {"c5", "c5_items.txt", "C5 generated items", &c5_items<Oracle>, &c5_items<Live>},
    {"c5m", "c5_matrix.txt", "C5 asset type x inventory type matrix", &c5_matrix<Oracle>, &c5_matrix<Live>},
    {"c5c", "c5_categories.txt", "C5 generated categories and objects", &c5_categories<Oracle>, &c5_categories<Live>},
    {"c6", "c6_legacy.txt", "C6 malformed legacy text through every reader", &c6_legacy<Oracle>, &c6_legacy<Live>},
    {"c7", "c7_item_llsd.txt", "C7 LLSD input of LLInventoryItem::fromLLSD", &c7_item_llsd<Oracle>, &c7_item_llsd<Live>},
    {"c8", "c8_category_llsd.txt", "C8 LLSD input of the category readers", &c8_category_llsd<Oracle>, &c8_category_llsd<Live>},
    {"c9", "c9_messages.txt", "C9 message blocks", &c9_messages<Oracle>, &c9_messages<Live>},
};

// Random streams: one step generates case i for both implementations.
struct RandomStream
{
    const char* name;
    const char* file;
    const char* what;
    U64 seed;
    U64 cases;
    U64 head;   // sample: i < head or i % every == 0
    U64 every;
};

static const RandomStream kRandom[] = {
    {"c3r", "c3_random_sample.txt", "C3 permissions, random stream", SEED_C3, C3_RANDOM_CASES, 200, 1000},
    {"c4r", "c4_random_sample.txt", "C4 sale info, random stream", SEED_C4, C4_RANDOM_CASES, 200, 1000},
    {"c5r", "c5_random_sample.txt", "C5 items, random stream", SEED_C5, C5_RANDOM_CASES, 10, 2000},
    {"c5mf", "", "C5 full records over the asset x inventory type matrix", 0, 0, 0, 0},
};

struct StreamGen
{
    Rng rng;
    std::string name;
    U64 index = 0;
    explicit StreamGen(const RandomStream& r) : rng(r.seed), name(r.name) {}
    // Generates the next case for both implementations.
    void step(VecSink* o, VecSink* l)
    {
        const U64 i = index++;
        if (name == "c3r")
        {
            const C3In in = draw_c3(rng);
            if (o) c3r_case<Oracle>(i, in, *o);
            if (l) c3r_case<Live>(i, in, *l);
        }
        else if (name == "c4r")
        {
            const C4In in = draw_c4(rng);
            if (o) c4r_case<Oracle>(i, in, *o);
            if (l) c4r_case<Live>(i, in, *l);
        }
        else if (name == "c5r")
        {
            const ItemF f = draw_item(rng);
            const std::string cs = "c5r." + d((S64)i);
            if (o) c5_item_record<Oracle>(cs, f, *o);
            if (l) c5_item_record<Live>(cs, f, *l);
        }
        else if (name == "c5mf")
        {
            const size_t na = sizeof(kAssetTypes) / sizeof(kAssetTypes[0]);
            const size_t ni = sizeof(kInvTypes) / sizeof(kInvTypes[0]);
            ItemF f = base_item();
            f.type = kAssetTypes[(i / ni) % na];
            f.inv_type = kInvTypes[i % ni];
            const std::string cs = "c5mf." + d(f.type) + "." + d(f.inv_type);
            if (o) c5_item_record<Oracle>(cs, f, *o);
            if (l) c5_item_record<Live>(cs, f, *l);
        }
    }
};

// The digest chunk of a stream: a sampled stream's sample spacing (every chunk starts
// at a sampled case); the matrix's inventory types per asset type.
U64 stream_chunk(const RandomStream& r)
{
    if (r.every) return r.every;
    return (U64)(sizeof(kInvTypes) / sizeof(kInvTypes[0]));
}

U64 stream_cases(const RandomStream& r)
{
    if (std::string(r.name) == "c5mf")
    {
        return (U64)(sizeof(kAssetTypes) / sizeof(kAssetTypes[0])) * (sizeof(kInvTypes) / sizeof(kInvTypes[0]));
    }
    return r.cases;
}

//============================================================================
// Comparison
//============================================================================

std::vector<std::string> split_tabs(const std::string& line)
{
    std::vector<std::string> out;
    size_t a = 0;
    while (true)
    {
        const size_t b = line.find('\t', a);
        out.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

std::vector<std::string> split_spaces(const char* s)
{
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}

// Column names of a record field (README "Records"), by corpus, field and width.
std::vector<std::string> columns_for(const std::string& cs, const std::string& field, size_t n)
{
    auto cols = [](const char* s) { return split_spaces(s); };
    auto pre = [](const char* first, std::vector<std::string> v) { v.insert(v.begin(), first); return v; };
    auto post = [](std::vector<std::string> v, const char* last) { v.push_back(last); return v; };
    auto ends = [&](const char* sfx) {
        const std::string x = sfx;
        return field.size() >= x.size() && field.compare(field.size() - x.size(), x.size(), x) == 0;
    };
    auto corpus = [&](const char* p) { return cs.compare(0, strlen(p), p) == 0; };
    std::vector<std::string> v;
    if (ends("stream")) v = n == 4 ? pre("ret", cols(kStreamCols)) : cols(kStreamCols);
    else if (n == 24) v = cols(kItemCols);
    else if (n == 25) v = pre("ret", cols(kItemCols));
    else if (n == 26) v = pre("ret", pre("same", cols(kItemCols)));
    else if (field == "queries") v = cols("allow helpers crc owned owner_id owner_is_group is_owned is_group_owned");
    else if (field == "agg") v = corpus("c9") ? cols("u8 copy modify transfer empty repacked") : cols("u8 copy modify transfer move empty");
    else if (field == "notecard") v = cols("ret version embedded_version items text");
    else if (field == "legacy.outcome") v = n == 2 ? cols("key nokey") : cols("outcome");
    else if (ends("invariants")) v = cols("owner_in_base group_in_owner everyone_in_owner next_in_base everyone_no_modify no_transfer_no_copy");
    else if (field == "matrix") v = cols("legacy_type legacy_inv_type llsd_type llsd_inv_type crc reimport_type reimport_inv_type reimport_base "
                                         "fromllsd_type fromllsd_inv_type fromllsd_base fromllsd_everyone linked_uuid");
    else if (corpus("c2") && field == "out") v = cols("name desc rna_pipe rna rename ctor_name ctor_desc set_desc cat_ctor");
    else if (corpus("c1") && field == "at") v = cols("type_name human desc can_link can_fetch can_know is_link");
    else if (corpus("c1") && field == "it") v = cols("name human default_for_asset cannot_restrict show_in_world");
    else if (corpus("c1") && field == "ft") v = cols("name protected automatic singleton ensemble to_asset from_asset");
    else if (corpus("c1") && field == "lookup") v = cols("name at at_cstr human human_cstr get_type it ft sale");
    else if (corpus("c1") && field == "category") v = cols("u32 subcount name sub0 sub1 sub5 sub255");
    else if (field == "sale" && corpus("c6")) v = cols("type price has_perm_mask perm_mask ret");
    else if (field == "sale" && corpus("c9")) v = cols("type price multi_type multi_price");
    else if (field == "sale" && corpus("c4.acc")) v = cols("type price eq ne");
    else if (field == "sale+perm_mask") v = cols("type price has_perm_mask perm_mask");
    else if (corpus("c4.") && n == 5) v = cols("type price ret has_perm_mask perm_mask");
    else if (corpus("c4") && n == 2 && field != "crc") v = cols(kSaleCols);
    else if (n == 10 && (corpus("c3") || corpus("c7") || field == "setpermissions" || field == "perm_from_sd")) v = cols(kPermCols);
    else if (n == 11 && (corpus("c3") || corpus("c6"))) v = pre("ret", cols(kPermCols));
    else if (n == 11) v = cols(kVCatCols);
    else if (n == 12) v = pre(corpus("c10") ? "same" : "ret", cols(kVCatCols));
    else if (n == 9) v = cols(kCatCols);
    else if (n == 10) v = (field == "fromllsd.asllsd" || field == "importmap") ? post(cols(kCatCols), "ret") : pre("ret", cols(kCatCols));
    else if (n == 8) v = cols(kObjCols);
    if (v.size() != n)
    {
        v.clear();
        for (size_t i = 0; i < n; ++i) v.push_back("v" + d((S64)i));
    }
    return v;
}

std::string clip(const std::string& s, size_t n = 200)
{
    if (s.size() <= n) return s;
    return s.substr(0, n) + "...(" + d((S64)s.size()) + " chars)";
}

// Only an expected "UB" (undefined behaviour stated in the fixture) is skipped.
bool skip_value(const std::string& expected, const std::string& /*actual*/)
{
    return expected == "UB";
}

// Compares expected and actual line lists; appends readable differences to
// report (at most max_report) and returns the number of differing lines.
U64 compare_lines(const std::vector<std::string>& expected, const std::vector<std::string>& actual, const std::string& label,
                  std::string& report, U64 max_report)
{
    U64 diffs = 0;
    const size_t n = std::max(expected.size(), actual.size());
    for (size_t i = 0; i < n; ++i)
    {
        const std::string* e = i < expected.size() ? &expected[i] : NULL;
        const std::string* a = i < actual.size() ? &actual[i] : NULL;
        if (e && a && *e == *a) continue;
        if (e && a)
        {
            const std::vector<std::string> ev = split_tabs(*e), av = split_tabs(*a);
            if (ev.size() >= 2 && av.size() >= 2 && ev[0] == av[0] && ev[1] == av[1] && ev.size() == av.size())
            {
                bool real = false;
                const std::vector<std::string> cols = columns_for(ev[0], ev[1], ev.size() - 2);
                std::string text;
                for (size_t k = 2; k < ev.size(); ++k)
                {
                    if (ev[k] == av[k] || skip_value(ev[k], av[k])) continue;
                    real = true;
                    text += label + ": case " + ev[0] + " field " + ev[1] + "." + cols[k - 2] + ": expected " + clip(ev[k]) +
                            " actual " + clip(av[k]) + "\n";
                }
                if (!real) continue;
                if (diffs < max_report) report += text;
                ++diffs;
                continue;
            }
        }
        if (diffs < max_report)
        {
            report += label + ": line " + d((S64)i + 1) + ": expected " + (e ? clip(*e) : std::string("<end>")) + "\n" +
                      std::string(label.size() + 2, ' ') + "actual   " + (a ? clip(*a) : std::string("<end>")) + "\n";
        }
        ++diffs;
        if (!e || !a) break; // a length difference: the rest is shifted
    }
    return diffs;
}

bool read_lines(const std::string& path, std::vector<std::string>& body, std::vector<std::string>* header = NULL)
{
    std::string data;
    if (path.size() > 3 && path.compare(path.size() - 3, 3, ".gz") == 0)
    {
        if (!gunzip_file(path, data)) return false;
    }
    else
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        data = ss.str();
    }
    size_t a = 0;
    while (a < data.size())
    {
        size_t b = data.find('\n', a);
        if (b == std::string::npos) b = data.size();
        std::string line = data.substr(a, b - a);
        if (!line.empty() && line[0] == '#')
        {
            if (header) header->push_back(line);
        }
        else
        {
            body.push_back(line);
        }
        a = b + 1;
    }
    return true;
}

//============================================================================
// Checks used by the parity test
//============================================================================

struct CheckResult
{
    U64 groups = 0;
    U64 cases = 0;
    U64 diffs = 0;
    std::string report;
};

static const U64 MAX_REPORT = 25;

void check_structured(const std::string& fixture_dir, CheckResult& r)
{
    for (const Structured& c : kStructured)
    {
        VecSink o, l;
        c.oracle(o);
        c.live(l);
        r.groups++;
        r.cases += o.lines.size();
        r.diffs += compare_lines(o.lines, l.lines, std::string(c.name) + " live vs oracle", r.report, MAX_REPORT);
        std::vector<std::string> body;
        if (!read_lines(fixture_dir + "/" + c.file, body))
        {
            r.report += std::string(c.name) + ": fixture " + c.file + " missing\n";
            r.diffs++;
            continue;
        }
        r.diffs += compare_lines(body, l.lines, std::string(c.name) + " live vs fixture", r.report, MAX_REPORT);
    }
}

std::map<std::string, std::string> read_digests(const std::string& fixture_dir)
{
    std::map<std::string, std::string> m;
    std::vector<std::string> body;
    read_lines(fixture_dir + "/digests.txt", body);
    for (const std::string& line : body)
    {
        const std::vector<std::string> v = split_tabs(line);
        if (v.size() >= 3) m[v[0]] = v[2] + "\t" + (v.size() > 3 ? v[3] : std::string());
    }
    return m;
}

// Compares a stream's digests with digests.txt; a chunk mismatch names its case range.
void check_digests(const std::vector<std::vector<std::string> >& entries, const std::map<std::string, std::string>& digests,
                   CheckResult& r)
{
    for (const std::vector<std::string>& e : entries)
    {
        const std::string got = e[2] + "\t" + e[3];
        auto it = digests.find(e[0]);
        std::string what = e[0] + " (whole stream)";
        const size_t at = e[0].find('@');
        if (at != std::string::npos)
        {
            std::string range = e[0].substr(at + 1);
            range.replace(range.find('-'), 1, ", ");
            what = e[0].substr(0, at) + " cases [" + range + ")";
        }
        if (it == digests.end())
        {
            r.report += what + ": no digest in digests.txt\n";
            r.diffs++;
        }
        else if (it->second != got)
        {
            if (r.diffs < MAX_REPORT) r.report += what + ": digest (lines, sha256) expected " + it->second + " actual " + got + "\n";
            r.diffs++;
        }
    }
}

void check_random(const std::string& fixture_dir, CheckResult& r)
{
    const std::map<std::string, std::string> digests = read_digests(fixture_dir);
    for (const RandomStream& rs : kRandom)
    {
        StreamGen gen(rs);
        ChunkedDigest cd(rs.name, stream_chunk(rs));
        std::vector<std::string> sample;
        const U64 n = stream_cases(rs);
        for (U64 i = 0; i < n; ++i)
        {
            cd.begin_case(i);
            VecSink o, l;
            gen.step(&o, &l);
            r.diffs += compare_lines(o.lines, l.lines, std::string(rs.name) + " live vs oracle", r.report, MAX_REPORT);
            for (const std::string& line : l.lines)
            {
                cd.line(line);
                if (rs.every && (i < rs.head || i % rs.every == 0)) sample.push_back(line);
            }
        }
        cd.finish();
        r.groups++;
        r.cases += n;
        check_digests(cd.entries, digests, r);
        if (rs.every)
        {
            std::vector<std::string> body;
            if (!read_lines(fixture_dir + "/" + rs.file, body))
            {
                r.report += std::string(rs.name) + ": fixture " + rs.file + " missing\n";
                r.diffs++;
            }
            else
            {
                r.diffs += compare_lines(body, sample, std::string(rs.name) + " live vs fixture sample", r.report, MAX_REPORT);
            }
        }
    }
}

void check_exhaustive(const std::string& fixture_dir, CheckResult& r)
{
    const std::map<std::string, std::string> digests = read_digests(fixture_dir);
    const std::vector<ExhaustiveResult> res = run_c3_exhaustive(true);
    for (const ExhaustiveResult& e : res)
    {
        r.groups++;
        r.cases += e.cases;
        if (e.diffs)
        {
            r.report += e.name + " live vs oracle: " + d((S64)e.diffs) + " differences; first: " + e.first_diff + "\n";
            r.diffs += e.diffs;
        }
        check_digests(e.digests, digests, r);
    }
}

// The fixture tree holds exactly the expected files, and digests.txt exactly the
// expected streams: a missing or an unexpected entry fails.
std::vector<std::string> expected_fixture_files()
{
    std::vector<std::string> f = {"README.md", ".gitattributes", "digests.txt", "c10_library.txt.gz", LIBRARY_FILE};
    for (const Structured& c : kStructured) f.push_back(c.file);
    for (const RandomStream& rs : kRandom)
        if (rs.every) f.push_back(rs.file);
    std::sort(f.begin(), f.end());
    return f;
}

void check_fixture_tree(const std::string& fixture_dir, CheckResult& r)
{
    namespace fs = std::filesystem;
    const std::vector<std::string> want = expected_fixture_files();
    std::vector<std::string> have;
    std::error_code ec;
    const fs::path root(fixture_dir);
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->is_regular_file()) have.push_back(it->path().lexically_relative(root).generic_string());
    }
    if (ec)
    {
        r.report += "fixture tree: cannot list " + fixture_dir + "\n";
        r.diffs++;
    }
    std::sort(have.begin(), have.end());
    r.groups++;
    r.cases += want.size();
    for (const std::string& w : want)
    {
        if (!std::binary_search(have.begin(), have.end(), w))
        {
            r.report += "fixture tree: missing " + w + "\n";
            r.diffs++;
        }
    }
    for (const std::string& h : have)
    {
        if (!std::binary_search(want.begin(), want.end(), h))
        {
            r.report += "fixture tree: unexpected file " + h + "\n";
            r.diffs++;
        }
    }
    // digests.txt holds exactly the expected keys: each stream's total and its chunks.
    std::set<std::string> keys;
    auto add_stream = [&](const std::string& name, U64 n, U64 chunk) {
        keys.insert(name);
        for (U64 a = 0; a < n; a += chunk) keys.insert(name + "@" + d((S64)a) + "-" + d((S64)std::min(n, a + chunk)));
    };
    for (const RandomStream& rs : kRandom) add_stream(rs.name, stream_cases(rs), stream_chunk(rs));
    for (const char* e : kExhaustiveNames)
    {
        const std::string name = e;
        const U64 n = name == "c3x.initmasks5" ? (U64)C3X_FAIR_CASES : (name.find("alpha5") != std::string::npos ? (1ull << 25) : (1ull << 20));
        add_stream(name, n, EXHAUSTIVE_CHUNK);
    }
    const std::map<std::string, std::string> digests = read_digests(fixture_dir);
    for (const auto& kv : digests)
    {
        if (!keys.count(kv.first))
        {
            r.report += "digests.txt: unexpected entry " + kv.first + "\n";
            r.diffs++;
        }
    }
    for (const std::string& k : keys)
    {
        if (!digests.count(k))
        {
            r.report += "digests.txt: missing " + k + "\n";
            r.diffs++;
        }
    }
}

void check_library(const std::string& fixture_dir, CheckResult& r)
{
    LLSD inventory;
    S32 version = 0;
    if (!load_cache(fixture_dir + "/" + LIBRARY_FILE, inventory, version))
    {
        r.report += std::string("c10: cannot load ") + LIBRARY_FILE + "\n";
        r.diffs++;
        return;
    }
    VecSink o, l;
    c10_library<Oracle>(inventory, &o);
    c10_library<Live>(inventory, &l);
    r.groups++;
    r.cases += l.lines.size();
    r.diffs += compare_lines(o.lines, l.lines, "c10 live vs oracle", r.report, MAX_REPORT);
    std::vector<std::string> body;
    if (!read_lines(fixture_dir + "/c10_library.txt.gz", body))
    {
        r.report += "c10: fixture c10_library.txt.gz missing\n";
        r.diffs++;
        return;
    }
    r.diffs += compare_lines(body, l.lines, "c10 live vs fixture", r.report, MAX_REPORT);
}

//============================================================================
// Dump
//============================================================================

std::string header(const std::string& what, const std::string& source, const std::string& seed)
{
    std::ostringstream os;
    os << "# vkstorm-inventory model fixture: " << what << "\n";
    os << "# generator: llinventory_tool dump (indra/llinventory/tests/llinventory_tool.cpp); values from the frozen copies in llinventory_oracle.cpp\n";
    os << "# viewer source: " << source << "\n";
    os << "# platform: " << platform_description() << "\n";
    os << "# seed: " << seed << "\n";
    os << "# format: tab-separated \"<case>\\t<field>\\t<value>...\" (README.md)\n";
    return os.str();
}

bool write_text(const std::string& path, const std::string& head, const std::vector<std::string>& lines)
{
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << head;
    for (const std::string& l : lines) out << l << "\n";
    return (bool)out;
}

bool write_gz(const std::string& path, const std::string& head, const std::vector<std::string>& lines)
{
    gzFile f = gzopen(path.c_str(), "wb9");
    if (!f) return false;
    gzwrite(f, head.data(), (unsigned)head.size());
    for (const std::string& l : lines)
    {
        gzwrite(f, l.data(), (unsigned)l.size());
        gzwrite(f, "\n", 1);
    }
    return gzclose(f) == Z_OK;
}

static const int DUMP_RECORDED = 3;

int dump(const std::string& dir, const std::string& source, const std::string& library)
{
    init_runtime();
    std::vector<std::string> digest_lines;
    for (const Structured& c : kStructured)
    {
        VecSink o;
        c.oracle(o);
        const std::string path = dir + "/" + c.file;
        if (!write_text(path, header(c.what, source, "none"), o.lines))
        {
            std::cerr << "cannot write " << path << std::endl;
            return 1;
        }
        std::cout << c.file << ": " << o.lines.size() << " lines" << std::endl;
    }
    for (const RandomStream& rs : kRandom)
    {
        StreamGen gen(rs);
        ChunkedDigest cd(rs.name, stream_chunk(rs));
        std::vector<std::string> sample;
        const U64 n = stream_cases(rs);
        for (U64 i = 0; i < n; ++i)
        {
            cd.begin_case(i);
            VecSink o;
            gen.step(&o, NULL);
            for (const std::string& line : o.lines)
            {
                cd.line(line);
                if (rs.every && (i < rs.head || i % rs.every == 0)) sample.push_back(line);
            }
        }
        cd.finish();
        for (const auto& e : cd.entries) digest_lines.push_back(e[0] + "\t" + e[1] + "\t" + e[2] + "\t" + e[3]);
        if (rs.every)
        {
            char seed[32];
            snprintf(seed, sizeof(seed), "0x%016llx", (unsigned long long)rs.seed);
            const std::string what = std::string(rs.what) + " (sample: cases i < " + d((S64)rs.head) + " and i % " + d((S64)rs.every) +
                                     " == 0 of " + d((S64)n) + "; digests in digests.txt)";
            if (!write_text(dir + "/" + rs.file, header(what, source, seed), sample))
            {
                std::cerr << "cannot write " << rs.file << std::endl;
                return 1;
            }
            std::cout << rs.file << ": " << sample.size() << " lines of " << cd.lines << std::endl;
        }
    }
    for (const ExhaustiveResult& e : run_c3_exhaustive(false))
    {
        for (const auto& x : e.digests) digest_lines.push_back(x[0] + "\t" + x[1] + "\t" + x[2] + "\t" + x[3]);
    }
    {
        std::string head = header("digests of the random streams (SHA-256 of the canonical lines, each ending in \\n) and of the "
                                  "exhaustive sweeps (SHA-256 of 20 bytes per case: the five masks as little-endian U32), "
                                  "per stream and per chunk of cases",
                                  source, "per stream, see README.md");
        head += "# columns: <stream>[@<first case>-<end case>]\\t<cases>\\t<lines (records for the exhaustive sweeps)>\\t<sha256>\n";
        if (!write_text(dir + "/digests.txt", head, digest_lines))
        {
            std::cerr << "cannot write digests.txt" << std::endl;
            return 1;
        }
    }
    {
        // C10 needs the Library cache: a missing input fails.
        LLSD inventory;
        S32 version = 0;
        if (library.empty() || !load_cache(library, inventory, version))
        {
            std::cerr << "cannot load the Library cache (--library " << library << ")" << std::endl;
            return 1;
        }
        VecSink o;
        c10_library<Oracle>(inventory, &o);
        if (!write_gz(dir + "/c10_library.txt.gz",
                      header("C10 the Linden Library cache (cache version " + d(version) + "): fromLLSD of every "
                             "item and importLLSDMap of every category, the re-serialization's byte identity "
                             "and the fields", source, "none"),
                      o.lines))
        {
            std::cerr << "cannot write c10_library.txt.gz" << std::endl;
            return 1;
        }
        std::cout << "c10_library.txt.gz: " << o.lines.size() << " lines" << std::endl;
    }
    // A recording is not a verification: the dump exits with DUMP_RECORDED (3) so that it
    // can never pass for a test run; regenerate.py expects exactly that status.
    return DUMP_RECORDED;
}

//============================================================================
// Bench
//============================================================================

typedef std::chrono::steady_clock Clock;
U64 g_bench_sink = 0;

template<class F> double time_min(int rounds, F&& f)
{
    double best = 1e30;
    for (int r = 0; r < rounds; ++r)
    {
        const auto t0 = Clock::now();
        f();
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        best = std::min(best, us);
    }
    return best;
}

ItemF bench_item(Rng& r, U64 i)
{
    static const S32 types[][2] = {{0, 0}, {0, 15}, {6, 6}, {6, 17}, {5, 18}, {13, 18}, {7, 7}, {10, 10}, {20, 19}, {21, 20},
                                   {3, 3}, {1, 1}, {24, 6}, {24, 18}, {56, 25}, {57, 26}, {49, 22}};
    ItemF f;
    f.id.mData[0] = 0x42;
    memcpy(f.id.mData + 8, &i, 8);
    const U64 a = r.next(), b = r.next();
    memcpy(f.asset.mData, &a, 8);
    memcpy(f.asset.mData + 8, &b, 8);
    f.parent = U((U8)(r.below(64)));
    const auto& t = types[r.below(sizeof(types) / sizeof(types[0]))];
    f.type = t[0];
    f.inv_type = t[1];
    f.flags = (t[0] == 5 || t[0] == 13) ? r.below(16) : 0;
    f.perm.creator = U((U8)r.below(200));
    f.perm.owner = ID_A();
    f.perm.last = U((U8)r.below(200));
    f.perm.group = r.below(4) == 0 ? ID_G() : NUL();
    f.perm.base = r.coin() ? PERM_ALL : 0x0008e000 & ~(r.coin() ? C_ : T_);
    f.perm.owner_mask = f.perm.base;
    f.perm.group_mask = 0;
    f.perm.everyone = 0;
    f.perm.next = 0x00082000 | (r.coin() ? C_ : 0);
    f.thumb = r.below(5) == 0 ? U(0xb1) : NUL();
    f.sale_type = 0;
    f.sale_price = 10;
    f.date = 1100000000 + (S64)r.below(600000000);
    std::string name = "Item ";
    const U32 len = 4 + r.below(28);
    for (U32 k = 0; k < len; ++k) name += (char)('a' + r.below(26));
    f.name = name;
    f.desc = r.below(3) == 0 ? "" : "(No Description)";
    return f;
}

template<class I> void bench_impl(const std::vector<ItemF>& population, const LLSD* library, int rounds, const char* label)
{
    typedef typename I::Item Item;
    const size_t n = population.size();
    std::vector<LLPointer<Item> > items;
    items.reserve(n);
    for (const ItemF& f : population) items.push_back(make_item<I>(f));
    std::vector<LLSD> sds;
    sds.reserve(n);
    for (auto& it : items) sds.push_back(it->asLLSD());
    std::vector<std::string> texts;
    texts.reserve(n);
    for (auto& it : items)
    {
        std::ostringstream os;
        it->exportLegacyStream(os, true);
        texts.push_back(os.str().substr(os.str().find('\n') + 1));
    }
    std::vector<typename I::Perm> perms;
    perms.reserve(n);
    for (auto& it : items) perms.push_back(it->getPermissions());
    U64& sink = g_bench_sink;
    auto report = [&](const char* op, double us, size_t count) {
        printf("%-8s %-42s %10.1f us  %8.1f ns/item  (%zu)\n", label, op, us, us * 1000.0 / (double)count, count);
    };
    if (n) // the synthetic population (empty for a private cache)
    {
    report("fromLLSD [C++] (new item each)", time_min(rounds, [&] {
        for (const LLSD& sd : sds)
        {
            LLPointer<Item> it = new Item;
            it->fromLLSD(sd);
            sink += it->getFlags();
        }
    }), n);
    report("asLLSD [C++]", time_min(rounds, [&] {
        for (auto& it : items)
        {
            LLSD sd = it->asLLSD();
            sink += sd.size();
        }
    }), n);
    report("legacy export", time_min(rounds, [&] {
        for (auto& it : items)
        {
            std::ostringstream os;
            it->exportLegacyStream(os, true);
            sink += os.tellp();
        }
    }), n);
    report("legacy import (new item each)", time_min(rounds, [&] {
        for (const std::string& t : texts)
        {
            std::istringstream is(t);
            LLPointer<Item> it = new Item;
            it->importLegacyStream(is);
            sink += it->getFlags();
        }
    }), n);
    report("getCRC32", time_min(rounds, [&] {
        U32 acc = 0;
        for (auto& it : items) acc += it->getCRC32();
        sink += acc;
    }), n);
    report("LLPermissions::fix", time_min(rounds, [&] {
        U32 acc = 0;
        for (const auto& p : perms)
        {
            typename I::Perm q = p;
            q.fix();
            acc += q.getMaskEveryone();
        }
        sink += acc;
    }), n);
    report("allowOperationBy (5 ops x 3 agents)", time_min(rounds, [&] {
        U32 acc = 0;
        const LLUUID agents[3] = {ID_A(), ID_B(), NUL()};
        for (const auto& p : perms)
            for (const LLUUID& a : agents)
                for (U32 op : {PERM_TRANSFER, PERM_MODIFY, PERM_COPY, PERM_MOVE, PERM_EXPORT})
                    acc += p.allowOperationBy(op, a, ID_G()) ? 1 : 0;
        sink += acc;
    }), n * 15);
    const LLUUID bench_agents[3] = {ID_A(), ID_B(), NUL()};
    report("allowModifyBy(agent) x 3 agents", time_min(rounds, [&] {
        U32 acc = 0;
        for (const auto& p : perms)
            for (const LLUUID& a : bench_agents)
                acc += p.allowModifyBy(a) ? 1 : 0;
        sink += acc;
    }), n * 3);
    report("allowCopyBy(agent, group) x 3", time_min(rounds, [&] {
        U32 acc = 0;
        for (const auto& p : perms)
            for (const LLUUID& a : bench_agents)
                acc += p.allowCopyBy(a, ID_G()) ? 1 : 0;
        sink += acc;
    }), n * 3);
    report("allowTransferTo x 3 agents", time_min(rounds, [&] {
        U32 acc = 0;
        for (const auto& p : perms)
            for (const LLUUID& a : bench_agents)
                acc += p.allowTransferTo(a) ? 1 : 0;
        sink += acc;
    }), n * 3);
    report("getOwnership", time_min(rounds, [&] {
        U32 acc = 0;
        for (const auto& p : perms)
        {
            LLUUID id;
            bool group = false;
            acc += p.getOwnership(id, group) ? id.mData[0] + (group ? 1 : 0) : 0;
        }
        sink += acc;
    }), n);
    report("getSafeOwner (owned)", time_min(rounds, [&] {
        U32 acc = 0;
        for (const auto& p : perms) acc += p.getSafeOwner().mData[0];
        sink += acc;
    }), n);
    report("initMasks(EType)", time_min(rounds, [&] {
        U32 acc = 0;
        for (size_t k = 0; k < n; ++k)
        {
            typename I::Perm q = perms[k];
            q.initMasks((typename I::IT::EType)population[k].inv_type);
            acc += q.getMaskBase();
        }
        sink += acc;
    }), n);
    report("LLPermissions::accumulate", time_min(rounds, [&] {
        U32 acc = 0;
        for (size_t k = 0; k < n; ++k)
        {
            typename I::Perm q = perms[k];
            q.accumulate(perms[(k + 1) % n]);
            acc += q.getMaskBase() + q.getCreator().mData[0];
        }
        sink += acc;
    }), n);
    report("setOwnerAndGroup", time_min(rounds, [&] {
        U32 acc = 0;
        for (const auto& p : perms)
        {
            typename I::Perm q = p;
            acc += q.setOwnerAndGroup(ID_A(), ID_B(), ID_G(), true) ? 1 : 0;
            acc += q.getMaskBase();
        }
        sink += acc;
    }), n);
    report("LLSaleInfo::getCRC32", time_min(rounds, [&] {
        U32 acc = 0;
        for (auto& it : items) acc += it->getSaleInfo().getCRC32();
        sink += acc;
    }), n);
    report("correctInventoryName", time_min(rounds, [&] {
        for (const ItemF& f : population)
        {
            std::string s = f.name;
            I::Object::correctInventoryName(s);
            sink += s.size();
        }
    }), n);
    std::vector<LLPointer<typename I::Category> > cats;
    cats.reserve(n);
    for (const ItemF& f : population)
    {
        CatF c;
        c.id = f.id;
        c.parent = f.parent;
        c.thumb = f.thumb;
        c.pref = (f.id.mData[8] % 4) ? -1 : 8;
        c.name = f.name;
        cats.push_back(make_cat<I>(c));
    }
    std::vector<std::string> cat_texts;
    cat_texts.reserve(n);
    for (auto& c : cats)
    {
        std::ostringstream os;
        c->exportLegacyStream(os, true);
        cat_texts.push_back(os.str().substr(os.str().find('\n') + 1));
    }
    report("category legacy export", time_min(rounds, [&] {
        for (auto& c : cats)
        {
            std::ostringstream os;
            c->exportLegacyStream(os, true);
            sink += os.tellp();
        }
    }), n);
    report("category legacy import (new each)", time_min(rounds, [&] {
        for (const std::string& t : cat_texts)
        {
            std::istringstream is(t);
            LLPointer<typename I::Category> c = new typename I::Category;
            c->importLegacyStream(is);
            sink += c->getName().size();
        }
    }), n);
    }
    if (library)
    {
        const LLSD& li = (*library)["items"];
        const LLSD& lc = (*library)["categories"];
        std::vector<LLPointer<Item> > litems;
        for (S32 k = 0; k < li.size(); ++k)
        {
            LLPointer<Item> it = new Item;
            it->fromLLSD(li[k]);
            litems.push_back(it);
        }
        report("library fromLLSD [C++]", time_min(rounds, [&] {
            for (S32 k = 0; k < li.size(); ++k)
            {
                LLPointer<Item> it = new Item;
                it->fromLLSD(li[k]);
                sink += it->getFlags();
            }
        }), (size_t)li.size());
        report("library asLLSD [C++]", time_min(rounds, [&] {
            for (auto& it : litems)
            {
                LLSD sd = it->asLLSD();
                sink += sd.size();
            }
        }), litems.size());
        report("library legacy export", time_min(rounds, [&] {
            for (auto& it : litems)
            {
                if (it->getSaleInfo().getSaleType() < 0 || it->getSaleInfo().getSaleType() > 3) continue;
                std::ostringstream os;
                it->exportLegacyStream(os, true);
                sink += os.tellp();
            }
        }), litems.size());
        report("library getCRC32", time_min(rounds, [&] {
            U32 acc = 0;
            for (auto& it : litems) acc += it->getCRC32();
            sink += acc;
        }), litems.size());
        report("library categories importLLSDMap [C++]", time_min(rounds, [&] {
            for (S32 k = 0; k < lc.size(); ++k)
            {
                LLPointer<typename I::VCat> c = make_vcat<I>(CatF());
                c->importLLSDMap(lc[k]);
                sink += c->getName().size();
            }
        }), (size_t)lc.size());
    }
}

int bench(int rounds, U64 count, const std::string& library, const std::string& cache)
{
    init_runtime();
    printf("llinventory_tool bench: %s, %d rounds (minimum reported), %llu synthetic items\n", platform_description().c_str(), rounds,
           (unsigned long long)count);
    printf("live: the viewer's classes (Rust rules); oracle: the frozen C++. Rows marked [C++] are the LLSD forms,\n"
           "C++ in both (kept C++ until R2/R3a; they still call the Rust rules inside, e.g. fix and initMasks).\n");
    Rng r(SEED_BENCH);
    std::vector<ItemF> population;
    population.reserve(count);
    for (U64 i = 0; i < count; ++i) population.push_back(bench_item(r, i));
    LLSD lib;
    S32 version = 0;
    const bool have_lib = !library.empty() && load_cache(library, lib, version);
    if (have_lib)
    {
        printf("library: %d categories, %d items (cache version %d)\n", (int)lib["categories"].size(), (int)lib["items"].size(), version);
    }
    bench_impl<Live>(population, have_lib ? &lib : NULL, rounds, "live");
    bench_impl<Oracle>(population, have_lib ? &lib : NULL, rounds, "oracle");
    if (!cache.empty())
    {
        // A private cache: counts, identity and timings only, never contents.
        LLSD inv;
        S32 v = 0;
        const auto t0 = Clock::now();
        if (!load_cache(cache, inv, v))
        {
            printf("private cache: cannot load\n");
            return 1;
        }
        const double load_us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        const LibraryStats st = c10_library<Live>(inv, NULL);
        const LibraryStats so = c10_library<Oracle>(inv, NULL);
        printf("private cache: version %d, %llu categories (%llu re-serialized identically), %llu items (%llu identically); "
               "oracle agrees: %s; gunzip+parse %.1f us\n",
               v, (unsigned long long)st.cats, (unsigned long long)st.cats_identical, (unsigned long long)st.items,
               (unsigned long long)st.items_identical,
               (st.items_identical == so.items_identical && st.cats_identical == so.cats_identical) ? "yes" : "NO", load_us);
        bench_impl<Live>(std::vector<ItemF>(), &inv, rounds, "private");
    }
    printf("(sink %llu)\n", (unsigned long long)g_bench_sink);
    return 0;
}

} // namespace vksinv

#ifndef VKS_INV_NO_MAIN
int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string library, cache, source = "unspecified";
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--library") library = args[i + 1];
        if (args[i] == "--cache") cache = args[i + 1];
        if (args[i] == "--source") source = args[i + 1];
    }
    if (args.size() >= 2 && args[0] == "dump")
    {
        return vksinv::dump(args[1], source, library);
    }
    if (args.size() >= 1 && args[0] == "bench")
    {
        const int rounds = args.size() >= 2 ? atoi(args[1].c_str()) : 3;
        U64 count = 100000;
        for (size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == "--items") count = strtoull(args[i + 1].c_str(), NULL, 10);
        }
        return vksinv::bench(rounds > 0 ? rounds : 3, count, library, cache);
    }
    if (args.size() >= 1 && args[0] == "probe")
    {
        // Runs one runtime-edge path whose outcome may be a crash; prints what happened.
        vksinv::init_runtime();
        const std::string what = args.size() >= 2 ? args[1] : "";
        if (what == "null-sale-export")
        {
            LLSaleInfo s;
            s.setSaleType((LLSaleInfo::EForSale)200);
            std::ostringstream os;
            s.exportLegacyStream(os);
            printf("null-sale-export: state=%d bytes=%s\n", (int)os.rdstate(), vksinv::esc(os.str()).c_str());
            return 0;
        }
        if (what == "ub")
        {
            for (const vksinv::LegacyCase& c : vksinv::c6_cases())
            {
                if (c.baseline.empty()) continue;
                vksinv::VecSink s;
                vksinv::c6_run<vksinv::Live>({c.id, c.readers, c.input, {}}, s);
                for (const std::string& l : s.lines) printf("%s\n", l.c_str());
            }
            return 0;
        }
        return 2;
    }
    fprintf(stderr, "usage: llinventory_tool dump <dir> [--source <commit>] [--library <cache.gz>]\n"
                    "       llinventory_tool bench [rounds] [--items N] [--library <cache.gz>] [--cache <private cache.gz>]\n"
                    "       llinventory_tool probe null-sale-export|ub\n");
    return 2;
}
#endif

/**
 * @file llsdserialize_corpus.cpp
 * @brief The LLSD values and serialized inputs shared by the serialization
 * parity test and the benchmark/fixture tool (vkstorm-rust rung two).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "llsdserialize_corpus.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#if LL_WINDOWS
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

namespace llsd_corpus
{
    namespace
    {
        std::vector<U8> pseudoRandom(size_t n, U32 seed)
        {
            std::vector<U8> v;
            v.reserve(n);
            for (size_t i = 0; i < n; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                v.push_back((U8)(seed >> 16));
            }
            return v;
        }

        std::string allBytes()
        {
            std::string s;
            for (int i = 0; i < 256; ++i)
            {
                s.push_back((char)i);
            }
            return s;
        }

        LLSD deepMap(int depth)
        {
            LLSD v = LLSD::emptyMap();
            v["end"] = 99;
            for (int i = depth - 1; i >= 0; --i)
            {
                LLSD outer = LLSD::emptyMap();
                outer["level_" + std::to_string(i)] = v;
                v = outer;
            }
            return v;
        }

        LLSD deepArray(int depth)
        {
            LLSD v = LLSD::emptyArray();
            v.append(1);
            for (int i = 0; i < depth; ++i)
            {
                LLSD outer = LLSD::emptyArray();
                outer.append(v);
                v = outer;
            }
            return v;
        }

        std::string be32(U32 v)
        {
            U32 n = htonl(v);
            return std::string(reinterpret_cast<const char*>(&n), 4);
        }

        std::string binString(const std::string& s)
        {
            return "s" + be32((U32)s.size()) + s;
        }

        std::string binKey(const std::string& s)
        {
            return "k" + be32((U32)s.size()) + s;
        }

        std::string binInt(S32 v)
        {
            return "i" + be32((U32)v);
        }

        std::string binMap(U32 count, const std::string& body, bool close = true)
        {
            return "{" + be32(count) + body + (close ? "}" : "");
        }

        std::string binArray(U32 count, const std::string& body, bool close = true)
        {
            return "[" + be32(count) + body + (close ? "]" : "");
        }

        std::string deepNotation(int depth)
        {
            std::string s = "{'deep':";
            for (int i = 0; i < depth; ++i)
            {
                s += "{'level_" + std::to_string(i) + "':";
            }
            s += "{'end':i99}";
            for (int i = 0; i <= depth; ++i)
            {
                s += "} ";
            }
            return s;
        }

        std::string deepXml(int depth)
        {
            std::string s;
            for (int i = 0; i < depth; ++i)
            {
                s += "<array>";
            }
            s += "<integer>1</integer>";
            for (int i = 0; i < depth; ++i)
            {
                s += "</array>";
            }
            return s;
        }

        std::string deepBinary(int depth)
        {
            std::string s;
            for (int i = 0; i < depth; ++i)
            {
                s += binArray(1, "", false);
            }
            s += binInt(1);
            for (int i = 0; i < depth; ++i)
            {
                s += "]";
            }
            return s;
        }
    }

    std::vector<ValueCase> values()
    {
        std::vector<ValueCase> v;
        auto add = [&](const std::string& name, const LLSD& value) { v.push_back(ValueCase{ name, value }); };

        add("undef", LLSD());
        add("true", LLSD(true));
        add("false", LLSD(false));
        add("int 0", LLSD(0));
        add("int 1", LLSD(1));
        add("int -1", LLSD(-1));
        add("int max", LLSD(2147483647));
        add("int min", LLSD((S32)-2147483647 - 1));
        add("int 123456", LLSD(123456));

        const F64 reals[] = {
            0.0, -0.0, 1.0, -1.0, 1234.5, -1234.5, 0.1, 0.5, 2.5, 1e-5, 1e-4, 0.001, 123456.7, 123456789.0,
            100000.0, 1000000.0, 1e15, 1e20, 1e21, 1.5e300, 5e-324, 2.2250738585072014e-308,
            1.7976931348623157e308, 3.14159265358979, 42.0, -34379.0438, 1198833773.1, 0.3333333333333333,
            (F64)1234.5f, (F64)0.1f,
            std::numeric_limits<F64>::infinity(), -std::numeric_limits<F64>::infinity(),
            std::numeric_limits<F64>::quiet_NaN(), -std::numeric_limits<F64>::quiet_NaN(),
        };
        for (size_t i = 0; i < sizeof(reals) / sizeof(reals[0]); ++i)
        {
            add("real " + std::to_string(i), LLSD(reals[i]));
        }

        add("uuid null", LLSD(LLUUID::null));
        add("uuid", LLSD(LLUUID("c96f9b1e-f589-4100-9774-d98643ce0bed")));
        add("uuid upper", LLSD(LLUUID("C96F9B1E-F589-4100-9774-D98643CE0BED")));
        add("uuid ff", LLSD(LLUUID("ffffffff-ffff-ffff-ffff-ffffffffffff")));

        add("string empty", LLSD(""));
        add("string a", LLSD("a"));
        add("string hello", LLSD("hello world"));
        add("string quotes", LLSD("it's a \"quoted\" 'string'"));
        add("string backslash", LLSD("back\\slash\\\\double"));
        add("string newlines", LLSD("line1\nline2\r\nline3\rline4"));
        add("string tabs", LLSD("tab\there\tand\v\f\a\b"));
        add("string xml", LLSD("<xml>&amp;&lt;\"'</xml>"));
        add("string utf8", LLSD("h\xc3\xa9llo w\xc3\xb6rld \xe2\x9c\x93 \xf0\x9f\x8e\x89"));
        add("string controls", LLSD(std::string("\x01\x02\x1f\x7f", 4)));
        add("string high bytes", LLSD(std::string("\xff\xfe\x80", 3)));
        add("string nul", LLSD(std::string("a\0b", 3)));
        add("string all bytes", LLSD(allBytes()));
        add("string long", LLSD(std::string(5000, 'x')));
        add("string spaces", LLSD("  leading and trailing  "));

        add("date epoch", LLSD(LLDate(0.0)));
        add("date fractional", LLSD(LLDate(1198833773.10)));
        add("date whole", LLSD(LLDate(1145895093.0)));
        add("date 2002", LLSD(LLDate("2002-12-07T05:07:15.00Z")));
        add("date far", LLSD(LLDate(1e10)));
        add("date tiny fraction", LLSD(LLDate(1145895093.009)));
        add("date half", LLSD(LLDate(1145895093.5)));

        add("uri empty", LLSD(LLURI("")));
        add("uri http", LLSD(LLURI("http://slurl.com/secondlife/Ambleside/57/104/26/")));
        add("uri https login", LLSD(LLURI("https://secondlife.com/login")));
        add("uri query", LLSD(LLURI("http://example.com/path?x=1&y=two words#frag")));
        add("uri odd", LLSD(LLURI("mailto:someone@example.com")));

        add("binary empty", LLSD(std::vector<U8>()));
        add("binary hello", LLSD(std::vector<U8>{ 'h', 'e', 'l', 'l', 'o' }));
        add("binary one", LLSD(std::vector<U8>{ 0 }));
        add("binary two", LLSD(std::vector<U8>{ 0xff, 0x00 }));
        add("binary random 100", LLSD(pseudoRandom(100, 1)));
        add("binary random 1000", LLSD(pseudoRandom(1000, 2)));
        {
            std::string all = allBytes();
            add("binary all bytes", LLSD(std::vector<U8>(all.begin(), all.end())));
        }

        add("map empty", LLSD::emptyMap());
        add("array empty", LLSD::emptyArray());
        {
            LLSD m = LLSD::emptyMap();
            m["name"] = "luke";
            m["age"] = 3;
            add("map simple", m);
        }
        {
            LLSD m = LLSD::emptyMap();
            m[""] = "empty key";
            m["with space"] = 1;
            m["quote'key"] = 2;
            m["<xml&key>"] = 3;
            m["\xc3\xa9"] = 4;
            m["back\\slash"] = 5;
            add("map odd keys", m);
        }
        {
            LLSD a = LLSD::emptyArray();
            a.append("ali");
            a.append(28);
            a.append(LLSD());
            a.append(1.5);
            a.append(true);
            add("array mixed", a);
        }
        {
            LLSD v;
            v["a"]["1"] = true;
            v["b"]["0"] = false;
            add("nested maps", v);
        }
        {
            LLSD v;
            v[0][0] = true;
            v[1][0] = false;
            add("nested arrays", v);
        }
        {
            LLSD v;
            v["amy"] = 23;
            v["bob"]["dogs"] = LLSD::emptyArray();
            v["bob"]["dogs"].append(LLSD::emptyMap());
            v["bob"]["dogs"][0]["name"] = "groove";
            v["bob"]["dogs"][0]["breed"] = "samoyed";
            v["bob"]["dogs"].append(LLSD::emptyMap());
            v["bob"]["dogs"][1]["name"] = "greyley";
            v["bob"]["dogs"][1]["breed"] = "chow/husky";
            v["cam"] = 1.23;
            add("nested notation", v);
        }
        {
            LLSD v = LLSD::emptyMap();
            v["agent_id"] = LLUUID("c96f9b1e-f589-4100-9774-d98643ce0bed");
            v["session_id"] = LLUUID("1e6a4e1b-2c3d-4e5f-8a9b-0c1d2e3f4a5b");
            v["first_name"] = "Test";
            v["last_name"] = "Resident";
            v["login"] = "true";
            v["seconds_since_epoch"] = 1198833773;
            v["look_at"] = "[r0.1,r0.2,r0.3]";
            v["home"] = "{'region_handle':[r256000,r256000], 'position':[r128,r128,r20]}";
            v["start_location"] = "last";
            v["circuit_code"] = 123456;
            v["sim_ip"] = "127.0.0.1";
            v["sim_port"] = 13000;
            v["seed_capability"] = LLURI("https://sim.example.com/cap/12345");
            v["inventory-skeleton"] = LLSD::emptyArray();
            for (int i = 0; i < 5; ++i)
            {
                LLSD folder = LLSD::emptyMap();
                folder["folder_id"] = LLUUID::generateNewID("folder" + std::to_string(i));
                folder["parent_id"] = LLUUID::null;
                folder["name"] = "Folder " + std::to_string(i);
                folder["type_default"] = i;
                folder["version"] = 1;
                v["inventory-skeleton"].append(folder);
            }
            add("login response", v);
        }
        {
            LLSD v = LLSD::emptyArray();
            for (int i = 0; i < 50; ++i)
            {
                v.append(i * 0.25);
            }
            add("array of reals", v);
        }
        add("deep map 60", deepMap(60));
        add("deep array 60", deepArray(60));
        {
            LLSD v = LLSD::emptyMap();
            v["binary"] = pseudoRandom(300, 3);
            v["date"] = LLDate(1198833773.10);
            v["uri"] = LLURI("http://example.com/");
            v["uuid"] = LLUUID("c96f9b1e-f589-4100-9774-d98643ce0bed");
            v["real"] = 1.5;
            v["int"] = -7;
            v["bool"] = true;
            v["undef"] = LLSD();
            v["string"] = "s";
            v["map"] = LLSD::emptyMap();
            v["array"] = LLSD::emptyArray();
            add("map all types", v);
        }
        return v;
    }

    std::vector<ValueCase> largeValues()
    {
        std::vector<ValueCase> v;
        {
            // Settings-like: a big map of small maps.
            LLSD settings = LLSD::emptyMap();
            for (int i = 0; i < 3000; ++i)
            {
                LLSD s = LLSD::emptyMap();
                s["Comment"] = "Setting number " + std::to_string(i) + " controls something in the viewer";
                s["Persist"] = 1;
                switch (i % 5)
                {
                case 0: s["Type"] = "Boolean"; s["Value"] = (i % 2 == 0); break;
                case 1: s["Type"] = "S32"; s["Value"] = i * 7; break;
                case 2: s["Type"] = "F32"; s["Value"] = i * 0.37; break;
                case 3: s["Type"] = "String"; s["Value"] = "value " + std::to_string(i); break;
                default:
                {
                    s["Type"] = "Color4";
                    LLSD c = LLSD::emptyArray();
                    c.append(0.1 * (i % 10));
                    c.append(0.2);
                    c.append(0.3);
                    c.append(1.0);
                    s["Value"] = c;
                    break;
                }
                }
                settings["Setting" + std::to_string(i)] = s;
            }
            v.push_back(ValueCase{ "settings-like map (3000 entries)", settings });
        }
        {
            // Inventory-like: an array of item maps with uuids and strings.
            LLSD inv = LLSD::emptyMap();
            inv["categories"] = LLSD::emptyArray();
            inv["items"] = LLSD::emptyArray();
            for (int i = 0; i < 20000; ++i)
            {
                LLSD item = LLSD::emptyMap();
                item["item_id"] = LLUUID::generateNewID("item" + std::to_string(i));
                item["parent_id"] = LLUUID::generateNewID("parent" + std::to_string(i % 100));
                item["asset_id"] = LLUUID::generateNewID("asset" + std::to_string(i));
                item["name"] = "Item " + std::to_string(i);
                item["desc"] = "(No Description)";
                item["type"] = i % 20;
                item["inv_type"] = i % 10;
                item["flags"] = 0;
                item["created_at"] = 1198833773 + i;
                LLSD perms = LLSD::emptyMap();
                perms["base_mask"] = 2147483647;
                perms["owner_mask"] = 2147483647;
                perms["group_mask"] = 0;
                perms["everyone_mask"] = 0;
                perms["next_owner_mask"] = 581632;
                perms["creator_id"] = LLUUID::generateNewID("creator");
                perms["owner_id"] = LLUUID::generateNewID("owner");
                item["permissions"] = perms;
                inv["items"].append(item);
            }
            v.push_back(ValueCase{ "inventory-like (20000 items)", inv });
        }
        return v;
    }

    std::vector<ParseCase> parseCases()
    {
        std::vector<ParseCase> v;
        auto add = [&](const std::string& name, Format f, const std::string& input, llssize max_bytes = -2, S32 depth = -1, bool lines = false)
        {
            ParseCase c;
            c.name = name;
            c.format = f;
            c.input = input;
            c.max_bytes = (max_bytes == -2) ? (llssize)input.size() : max_bytes;
            c.max_depth = depth;
            c.lines = lines;
            v.push_back(c);
        };
        const llssize UNLIMITED = LLSDSerialize::SIZE_UNLIMITED;
        // A runtime edge: the C++ oracle answers differently on MSVC and
        // glibc; the product behaves as stated on both.
        auto addEdge = [&](const std::string& name, Format f, const std::string& input, S32 count, const LLSD& value, bool lines = false)
        {
            ParseCase c;
            c.name = name;
            c.format = f;
            c.input = input;
            c.max_bytes = lines ? UNLIMITED : (llssize)input.size();
            c.max_depth = -1;
            c.lines = lines;
            c.runtime_edge = true;
            c.edge_count = count;
            c.edge_value = value;
            v.push_back(c);
        };

        // ---- notation: the unit test cases
        add("n malformed map", NOTATION, "{'ha ha'");
        add("n malformed array", NOTATION, "['ha ha'");
        add("n malformed string", NOTATION, "'ha ha");
        add("n noise", NOTATION, "g48ejlnfr");
        add("n undef", NOTATION, "!");
        for (const char* s : { "false", "f", "0", "F", "FALSE", "true", "t", "1", "T", "TRUE", "TR", "FAL", "True", "fAlSe", "f1", "fx", "t,", "tr" })
        {
            add(std::string("n bool ") + s, NOTATION, s);
        }
        add("n int", NOTATION, "i123");
        add("n int bare", NOTATION, "421");
        add("n real", NOTATION, "r456.7");
        add("n real bare", NOTATION, "456.7");
        add("n uuid short", NOTATION, "u123");
        add("n uuid", NOTATION, "uc96f9b1e-f589-4100-9774-d98643ce0bed");
        add("n uuid upper", NOTATION, "uC96F9B1E-F589-4100-9774-D98643CE0BED");
        add("n uuid bad hex", NOTATION, "uc96f9b1e-f589-4100-9774-d98643ce0beg");
        add("n uuid bad hyphens", NOTATION, "uc96f9b1exf589x4100x9774xd98643ce0bed");
        add("n uuid spaced", NOTATION, "u c96f9b1e-f589-4100-9774- d98643ce0bed");
        add("n string dq", NOTATION, "\"foolish\"");
        add("n string dq apos", NOTATION, "\"g'day\"");
        add("n string sq dq", NOTATION, "'have a \"nice\" day'");
        add("n string raw", NOTATION, "s(8)\"whatever\"");
        add("n string raw short", NOTATION, "s(7)\"whatever\"");
        add("n string raw long", NOTATION, "s(9)\"whatever\"");
        add("n string raw zero", NOTATION, "s(0)\"\"");
        add("n string raw hex", NOTATION, "s(0x2)\"ab\"");
        add("n string raw octal", NOTATION, "s(010)\"abcdefgh\"");
        add("n string raw junk len", NOTATION, "s(x)\"\"");
        add("n string raw mixed quotes", NOTATION, "s(3)'abc\"");
        add("n string raw no paren", NOTATION, "s3)\"abc\"");
        add("n string raw unlimited", NOTATION, "s(3)\"abc\"", UNLIMITED);
        add("n string escapes", NOTATION, "'a\\x41\\n\\t\\'\\\\\\q\\x6a'");
        add("n string hex escape bad", NOTATION, "'\\xzz'");
        add("n string truncated escape", NOTATION, "'abc\\");
        add("n string truncated hex", NOTATION, "'abc\\x4");
        add("n uri", NOTATION, "l\"http://www.google.com\"");
        add("n uri sq", NOTATION, "l'http://www.google.com'");
        add("n uri odd delim", NOTATION, "lXhttp://www.google.comX");
        add("n uri unterminated", NOTATION, "l\"http://www.google.com");
        add("n uri eof", NOTATION, "l");
        add("n date", NOTATION, "d\"2007-12-28T09:22:53.10Z\"");
        add("n date whole", NOTATION, "d\"2007-12-28T09:22:53Z\"");
        add("n date garbage", NOTATION, "d\"garbage\"");
        add("n date pre-epoch", NOTATION, "d\"1969-12-31T23:59:59Z\"");
        add("n date offset", NOTATION, "d\"2007-12-28T09:22:53+01:30\"");
        add("n date neg offset", NOTATION, "d\"2007-12-28T09:22:53-01:30\"");
        add("n date no z", NOTATION, "d\"2007-12-28T09:22:53\"");
        add("n date month 13", NOTATION, "d\"2007-13-28T09:22:53Z\"");
        add("n date day 40", NOTATION, "d\"2007-01-40T09:22:53Z\"");
        add("n binary b64", NOTATION, "b64\"YWJjMzIx\"");
        add("n binary b16", NOTATION, "b16\"616263333231\"");
        add("n binary raw", NOTATION, "b(6)\"abc321\"");
        add("n binary raw too long", NOTATION, "b(7)\"abc321\"");
        add("n binary raw way too long", NOTATION, "b(1000000)\"abc321\"");
        add("n binary raw zero", NOTATION, "b(0)\"\"");
        add("n binary raw bad close", NOTATION, "b(2)\"abX");
        add("n binary raw no close", NOTATION, "b(2)\"ab");
        add("n binary b64 empty", NOTATION, "b64\"\"");
        add("n binary b64 space", NOTATION, "b64\"YW Jj\"");
        add("n binary b64 padding", NOTATION, "b64\"YWJj\"");
        add("n binary b64 one", NOTATION, "b64\"Y\"");
        add("n binary b64 two", NOTATION, "b64\"YW\"");
        add("n binary b64 three", NOTATION, "b64\"YWJ\"");
        add("n binary b64 unterminated", NOTATION, "b64\"YWJj");
        add("n binary b16 empty", NOTATION, "b16\"\"");
        add("n binary b16 lower", NOTATION, "b16\"6162ff\"");
        add("n binary b16 bad", NOTATION, "b16\"zz\"");
        add("n binary b16 long", NOTATION, "b16\"" + std::string(600, 'A') + "\"");
        add("n binary unknown", NOTATION, "bx\"\"");
        add("n binary no quote", NOTATION, "b16");
        add("n size longer than bytes left", NOTATION, "s(5)\"hi\"");
        add("n binary size longer than bytes left", NOTATION, "b(5)\"hi\"");
        add("n simple map", NOTATION, "{'amy':i23,'bob':!,'cam':r1.23}");
        add("n nested map", NOTATION, "{'amy':i23,'bob':{'vehicle':'bicycle'},'cam':r1.23}");
        add("n simple array", NOTATION, "[i23,!,r1.23]");
        add("n nested array", NOTATION, "[i23,['bicycle'],r1.23]");
        add("n nested notation", NOTATION, "{'amy':i23, 'bob':{'dogs':[{'name':'groove', 'breed':'samoyed'},{'name':'greyley', 'breed':'chow/husky'}]}, 'cam':r1.23}");
        add("n deep 3 limit 5", NOTATION, "{'deep' : {'level_0':{'level_1':{'level_2': i99} } } }", -2, 5);
        add("n deep 10 limit 15", NOTATION, "{'deep' : {'level_0':{'level_1':{'level_2':{'level_3':{'level_4':{'level_5':{'level_6':{'level_7':{'level_8':{'level_9':i99}} } } } } } } } } }", -2, 15);
        add("n deep 10 limit 9", NOTATION, "{'deep' : {'level_0':{'level_1':{'level_2':{'level_3':{'level_4':{'level_5':{'level_6':{'level_7':{'level_8':{'level_9':i99}} } } } } } } } } }", -2, 9);
        add("n deep 10 limit 11", NOTATION, "{'deep' : {'level_0':{'level_1':{'level_2':{'level_3':{'level_4':{'level_5':{'level_6':{'level_7':{'level_8':{'level_9':i99}} } } } } } } } } }", -2, 11);
        add("n deep 10 limit 12", NOTATION, "{'deep' : {'level_0':{'level_1':{'level_2':{'level_3':{'level_4':{'level_5':{'level_6':{'level_7':{'level_8':{'level_9':i99}} } } } } } } } } }", -2, 12);
        add("n deep 50", NOTATION, deepNotation(50));
        add("n deep 50 limit 50", NOTATION, deepNotation(50), -2, 50);
        add("n deep 50 limit 52", NOTATION, deepNotation(50), -2, 52);
        add("n deep 50 limit 53", NOTATION, deepNotation(50), -2, 53);
        add("n depth 0", NOTATION, "i1", -2, 0);
        add("n depth 0 empty", NOTATION, "", -2, 0);
        add("n depth 1 map", NOTATION, "{}", -2, 1);
        add("n depth 2 map", NOTATION, "{'a':i1}", -2, 2);
        add("n depth 1 map full", NOTATION, "{'a':i1}", -2, 1);
        // ---- notation: edge cases
        add("n empty", NOTATION, "");
        add("n whitespace", NOTATION, "  \n\t ");
        add("n leading ws", NOTATION, "  \n i5");
        add("n trailing", NOTATION, "i5 trailing junk");
        add("n two values", NOTATION, "!!");
        add("n int trailing", NOTATION, "i12abc");
        add("n int empty", NOTATION, "i");
        add("n int sign only", NOTATION, "i-");
        add("n int plus", NOTATION, "i+5");
        add("n int space", NOTATION, "i 5");
        add("n int overflow", NOTATION, "i2147483648");
        add("n int min", NOTATION, "i-2147483648");
        add("n int hex", NOTATION, "i0x10");
        add("n int leading zeros", NOTATION, "i007");
        add("n real dot", NOTATION, "r.5");
        add("n real exp", NOTATION, "r1e5");
        add("n real exp caps", NOTATION, "r1E-5");
        add("n real bare e", NOTATION, "r1e");
        add("n real dot e", NOTATION, "r1.5e");
        add("n real e sign", NOTATION, "r1e+");
        add("n real inf", NOTATION, "rinf");
        add("n real nan", NOTATION, "rnan");
        add("n real overflow", NOTATION, "r1e999");
        // glibc accepts an underflowing real (value 0), MSVC rejected it: accepted.
        addEdge("n real underflow", NOTATION, "r1e-999", 1, LLSD(0.0));
        add("n real denormal", NOTATION, "r5e-324");
        add("n real sign only", NOTATION, "r-");
        add("n real empty", NOTATION, "r");
        add("n real trailing dot", NOTATION, "r1.");
        add("n real negative", NOTATION, "r-0.25");
        add("n real plus", NOTATION, "r+0.25");
        // MSVC's extraction reads hex floats, glibc's stops at the x: read.
        addEdge("n real hex", NOTATION, "r0x1p3", 1, LLSD(8.0));
        add("n real precise", NOTATION, "r0.1000000000000000055511151");
        add("n real long", NOTATION, "r123456789012345678901234567890");
        add("n map dangling key", NOTATION, "{'a'}");
        add("n map junk before key", NOTATION, "{xyz'a':i1}");
        add("n map no colon", NOTATION, "{'a'i1}");
        add("n map no comma", NOTATION, "{'a':i1 'b':i2}");
        add("n map trailing comma", NOTATION, "{'a':i1,}");
        add("n map dup keys", NOTATION, "{'a':i1,'a':i2}");
        // Nested duplicates: the discarded second 'a' holds a map whose own
        // duplicate 'c' is discarded while 'd' still belongs to it.
        add("n nested dup keys", NOTATION, "{'a':i0,'a':{'b':{'c':i1,'c':i2,'d':i3}}}");
        add("n nested dup keys container first", NOTATION, "{'a':{'b':{'c':i1,'c':i2,'d':i3}},'a':i0}");
        add("n nested dup keys arrays", NOTATION, "{'a':[i1],'a':[{'x':i1,'x':[i2,i3],'y':i4},i5],'b':{'k':'v','k':{'z':[i6,{'q':i7,'q':i8}]},'m':i9}}");
        add("n dup keys scalar after dup map", NOTATION, "{'a':i0,'a':{'b':{'c':i1}},'c':i2,'c':i3,'e':{'f':i4,'f':{'g':i5},'h':i6}}");
        add("n map two strings", NOTATION, "{'a' 'b':i1}");
        add("n map raw key", NOTATION, "{s(1)\"a\":i1}");
        add("n map empty key", NOTATION, "{'':i1}");
        add("n map key eof", NOTATION, "{'a':");
        add("n map value eof", NOTATION, "{'a':i");
        add("n map unclosed", NOTATION, "{'a':i1");
        add("n map key limit", NOTATION, "{s(3)\"abc\":i1}", 10);
        add("n map key limit unlimited", NOTATION, "{s(3)\"abc\":i1}", UNLIMITED);
        add("n array commas", NOTATION, "[,i1,,]");
        add("n array no commas", NOTATION, "[i1 i2]");
        add("n array unclosed", NOTATION, "[i1");
        add("n array bad element", NOTATION, "[i1,x]");
        add("n array eof after comma", NOTATION, "[i1,");
        add("n lone close brace", NOTATION, "}");
        add("n lone close bracket", NOTATION, "]");
        add("n lone open brace", NOTATION, "{");
        add("n lone open bracket", NOTATION, "[");
        add("n map in array", NOTATION, "[{'k':[{'x':r1.5}]}]");
        add("n pretty", NOTATION, "{\n    'a':\n    [\n        i1,\n        r2.5\n    ],\n    'b':'c'\n}\n");
        add("n high byte", NOTATION, "'\xc3\xa9'");
        add("n high byte start", NOTATION, "\xc3\xa9");
        add("n nul", NOTATION, std::string("'a\0b'", 5));
        add("n binary limit exact", NOTATION, "b(3)\"abc\"", 9);
        add("n binary limit short", NOTATION, "b(3)\"abc\"", 7);
        add("n binary limit unlimited", NOTATION, "b(3)\"abc\"", UNLIMITED);
        add("n string limit exact", NOTATION, "s(3)\"abc\"", 9);
        add("n string limit short", NOTATION, "s(3)\"abc\"", 4);

        // ---- binary: the unit test cases and edge cases
        add("b string", BINARY, binString("abc321"));
        add("b string short", BINARY, "s" + be32(7) + "abc321");
        add("b string huge", BINARY, "s" + be32(100000) + "abc321");
        add("b string huge unlimited", BINARY, "s" + be32(100000) + "abc321", UNLIMITED);
        add("b string negative", BINARY, "s" + be32(0xffffffffu) + "abc321");
        add("b string empty", BINARY, "s" + be32(0));
        add("b string truncated size", BINARY, "s\x00\x00");
        add("b binary", BINARY, "b" + be32(6) + "abc321");
        add("b binary short", BINARY, "b" + be32(7) + "abc321");
        add("b binary huge", BINARY, "b" + be32(100000) + "abc321");
        add("b binary negative", BINARY, "b" + be32(0xffffffffu) + "abc321");
        add("b binary empty", BINARY, "b" + be32(0));
        add("b binary truncated size", BINARY, "b\x00");
        add("b malformed map", BINARY, "{'ha ha'");
        add("b malformed array", BINARY, "['ha ha'");
        add("b malformed string", BINARY, "'ha ha");
        add("b noise", BINARY, "g48ejlnfr");
        add("b undef", BINARY, "!");
        add("b false", BINARY, "0");
        add("b true", BINARY, "1");
        add("b t", BINARY, "t");
        add("b f", BINARY, "f");
        add("b empty", BINARY, "");
        add("b int", BINARY, binInt(23));
        add("b int negative", BINARY, binInt(-23));
        add("b int truncated", BINARY, "i\x00\x01");
        add("b int missing", BINARY, "i");
        add("b real", BINARY, std::string("r\x3f\xf8\x00\x00\x00\x00\x00\x00", 9));
        add("b real truncated", BINARY, std::string("r\x3f\xf8\x00", 4));
        add("b uuid", BINARY, std::string("u\xc9\x6f\x9b\x1e\xf5\x89\x41\x00\x97\x74\xd9\x86\x43\xce\x0b\xed", 17));
        add("b uuid truncated", BINARY, std::string("u\xc9\x6f\x9b", 4));
        add("b date", BINARY, std::string("d\x00\x00\x00\x00\x00\x00\xf0\x3f", 9));
        add("b date truncated", BINARY, std::string("d\x00\x00\x00", 4));
        add("b uri", BINARY, "l" + be32(13) + "http://sl.com");
        add("b uri long", BINARY, "l" + be32(14) + "http://sl.com");
        add("b quoted string", BINARY, "\"amy\"");
        add("b quoted string sq", BINARY, "'amy'");
        add("b quoted string escapes", BINARY, "'a\\x41\\n'");
        add("b map", BINARY, binMap(1, binKey("amy") + binInt(23)));
        add("b map bad key size", BINARY, binMap(1, "k" + be32(1) + "amy" + binInt(23), false));
        add("b map unterminated", BINARY, binMap(1, binKey("amy") + binInt(23), false));
        add("b map too long", BINARY, binMap(0, binKey("amy") + binInt(23)));
        add("b map too short", BINARY, binMap(2, binKey("amy") + binInt(23)));
        add("b map empty", BINARY, binMap(0, ""));
        add("b map negative size", BINARY, binMap(0xffffffffu, ""));
        add("b map quoted keys", BINARY, binMap(2, "'amy'" + binInt(1) + "\"bob\"" + binInt(2)));
        add("b map unknown key tag", BINARY, binMap(1, "x" + binInt(1)));
        add("b map dup keys", BINARY, binMap(2, binKey("a") + binInt(1) + binKey("a") + binInt(2)));
        add("b nested dup keys", BINARY, binMap(2, binKey("a") + binInt(0) + binKey("a") + binMap(1, binKey("b") + binMap(3, binKey("c") + binInt(1) + binKey("c") + binInt(2) + binKey("d") + binInt(3)))));
        add("b nested dup keys container first", BINARY, binMap(2, binKey("a") + binMap(1, binKey("b") + binMap(3, binKey("c") + binInt(1) + binKey("c") + binInt(2) + binKey("d") + binInt(3))) + binKey("a") + binInt(0)));
        add("b nested dup keys arrays", BINARY, binMap(3, binKey("a") + binArray(1, binInt(1)) + binKey("a") + binArray(2, binMap(3, binKey("x") + binInt(1) + binKey("x") + binArray(2, binInt(2) + binInt(3)) + binKey("y") + binInt(4)) + binInt(5)) + binKey("b") + binMap(3, binKey("k") + binString("v") + binKey("k") + binMap(1, binKey("z") + binArray(2, binInt(6) + binMap(2, binKey("q") + binInt(7) + binKey("q") + binInt(8)))) + binKey("m") + binInt(9))));
        add("b map value fails", BINARY, binMap(1, binKey("a") + "g"));
        add("b map value eof", BINARY, binMap(1, binKey("a")));
        add("b map nested", BINARY, binMap(1, binKey("a") + binMap(1, binKey("b") + binArray(2, binInt(1) + "!"))));
        add("b array", BINARY, binArray(2, "\"amy\"" + binInt(23)));
        add("b array size short", BINARY, binArray(1, "\"amy\"" + binInt(23)));
        add("b array unterminated", BINARY, binArray(2, "\"amy\"" + binInt(23), false));
        add("b array too short", BINARY, binArray(3, "\"amy\"" + binInt(23)));
        add("b array empty", BINARY, binArray(0, ""));
        add("b array negative size", BINARY, binArray(0xffffffffu, ""));
        add("b array bad element", BINARY, binArray(1, "g"));
        add("b array eof element", BINARY, binArray(1, ""));
        add("b deep 100", BINARY, deepBinary(100));
        add("b deep 100 limit 96", BINARY, deepBinary(100), -2, 96);
        add("b deep 90 limit 96", BINARY, deepBinary(90), -2, 96);
        add("b deep 95 limit 96", BINARY, deepBinary(95), -2, 96);
        add("b deep 96 limit 96", BINARY, deepBinary(96), -2, 96);
        add("b depth 0", BINARY, "!", -2, 0);
        add("b depth 0 empty", BINARY, "", -2, 0);
        add("b trailing", BINARY, binInt(5) + "trailing");
        add("b two values", BINARY, "!!");
        add("b string limit", BINARY, binString("abc"), 7);
        add("b string limit unlimited", BINARY, binString("abc"), UNLIMITED);
        add("b binary limit", BINARY, "b" + be32(3) + "abc", 7);

        // ---- XML: the unit test cases
        add("x malformed", XML, "<llsd><string>ha ha</string>");
        add("x not llsd", XML, "<html><body><p>ha ha</p></body></html>");
        add("x value without llsd", XML, "<string>ha ha</string>");
        add("x key without llsd", XML, "<key>ha ha</key>");
        add("x unknown type", XML, "<llsd><map><key>amy</key><integer>23</integer><key>bob</key><bigint>99999999999999999</bigint><key>cam</key><real>1.23</real></map></llsd>");
        add("x map with html", XML, "<llsd><map><key>amy</key><integer>23</integer><html><body>ha ha</body></html><key>cam</key><real>1.23</real></map></llsd>");
        add("x map value for key", XML, "<llsd><map><key>amy</key><integer>23</integer><string>ha ha</string><key>cam</key><real>1.23</real></map></llsd>");
        add("x map of html", XML, "<llsd><map><key>amy</key><integer>23</integer><key>bob</key><map><html><body>ha ha</body></html></map><key>cam</key><real>1.23</real></map></llsd>");
        add("x array of html", XML, "<llsd><array><integer>23</integer><html><body>ha ha</body></html><real>1.23</real></array></llsd>");
        add("x array map of html", XML, "<llsd><array><integer>23</integer><map><html><body>ha ha</body></html></map><real>1.23</real></array></llsd>");
        add("x binary hello", XML, "<llsd><binary encoding=\"base64\">aGVsbG8=</binary></llsd>\n");
        add("x binary lines", XML, "<llsd><binary encoding=\"base64\">Nnw2fGFzZGZoYXBweWJveHw2MGU0NGVjNS0zMDVjLTQzYzItOWExOS1iNGI4OWIxYWUyYTZ8NjBl\nNDRlYzUtMzA1Yy00M2MyLTlhMTktYjRiODliMWFlMmE2fDYwZTQ0ZWM1LTMwNWMtNDNjMi05YTE5\nLWI0Yjg5YjFhZTJhNnwwMDAwMDAwMC0wMDAwLTAwMDAtMDAwMC0wMDAwMDAwMDAwMDB8N2ZmZmZm\nZmZ8N2ZmZmZmZmZ8MHwwfDgyMDAwfDQ1MGZlMzk0LTI5MDQtYzlhZC0yMTRjLWEwN2ViN2ZlZWMy\nOXwoTm8gRGVzY3JpcHRpb24pfDB8MTB8MA==</binary></llsd>\n");
        add("x deep", XML, "<llsd><map><key>deep</key><map><key>level_0</key><map><key>level_1</key><map><key>level_2</key><map><key>level_3</key><map><key>level_4</key><map><key>level_5</key><real>42.0</real></map></map></map></map></map></map></map></llsd>");
        // ---- XML: edge cases
        for (int lines = 0; lines < 2; ++lines)
        {
            std::string sfx = lines ? " (lines)" : "";
            bool l = lines != 0;
            add("x empty" + sfx, XML, "", UNLIMITED, -1, l);
            add("x whitespace" + sfx, XML, " \n ", UNLIMITED, -1, l);
            add("x declaration" + sfx, XML, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<llsd><integer>1</integer></llsd>\n", UNLIMITED, -1, l);
            add("x declaration late" + sfx, XML, "<llsd><?xml version=\"1.0\"?><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x declaration after space" + sfx, XML, " <?xml version=\"1.0\"?><llsd><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x bom" + sfx, XML, "\xef\xbb\xbf<llsd><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x comment" + sfx, XML, "<!-- c --><llsd><!-- inner --><integer>1<!-- x -->2</integer></llsd>", UNLIMITED, -1, l);
            add("x comment double dash" + sfx, XML, "<llsd><!-- a -- b --><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x pi" + sfx, XML, "<?pi target?><llsd><?x y?><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x cdata" + sfx, XML, "<llsd><string><![CDATA[<a&b>]]></string></llsd>", UNLIMITED, -1, l);
            add("x cdata outside" + sfx, XML, "<![CDATA[x]]><llsd><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x doctype" + sfx, XML, "<!DOCTYPE llsd [ <!ELEMENT llsd ANY> ]><llsd><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x entities" + sfx, XML, "<llsd><string>&lt;&gt;&amp;&apos;&quot;&#65;&#x42;&#x1F600;</string></llsd>", UNLIMITED, -1, l);
            add("x entity unknown" + sfx, XML, "<llsd><string>&nbsp;</string></llsd>", UNLIMITED, -1, l);
            add("x entity bare amp" + sfx, XML, "<llsd><string>a & b</string></llsd>", UNLIMITED, -1, l);
            add("x entity zero" + sfx, XML, "<llsd><string>&#0;</string></llsd>", UNLIMITED, -1, l);
            add("x entity surrogate" + sfx, XML, "<llsd><string>&#xD800;</string></llsd>", UNLIMITED, -1, l);
            add("x entity cr" + sfx, XML, "<llsd><string>a&#13;b</string></llsd>", UNLIMITED, -1, l);
            add("x crlf" + sfx, XML, "<llsd><string>a\r\nb\rc\nd</string></llsd>", UNLIMITED, -1, l);
            add("x key entity" + sfx, XML, "<llsd><map><key>a&amp;b</key><integer>1</integer></map></llsd>", UNLIMITED, -1, l);
            add("x binary base16" + sfx, XML, "<llsd><array><binary encoding=\"base16\">6162</binary><integer>1</integer></array></llsd>", UNLIMITED, -1, l);
            add("x binary sq attr" + sfx, XML, "<llsd><binary encoding='base64'>YWJj</binary></llsd>", UNLIMITED, -1, l);
            add("x binary no attr" + sfx, XML, "<llsd><binary>YWJj</binary></llsd>", UNLIMITED, -1, l);
            add("x binary ws" + sfx, XML, "<llsd><binary>Y W\nJ\tj\r</binary></llsd>", UNLIMITED, -1, l);
            add("x binary empty" + sfx, XML, "<llsd><binary /></llsd>", UNLIMITED, -1, l);
            add("x binary bad" + sfx, XML, "<llsd><binary>!!!</binary></llsd>", UNLIMITED, -1, l);
            add("x binary partial" + sfx, XML, "<llsd><binary>YWJ</binary></llsd>", UNLIMITED, -1, l);
            add("x dup attr" + sfx, XML, "<llsd><binary encoding=\"base64\" encoding=\"base64\">YWJj</binary></llsd>", UNLIMITED, -1, l);
            add("x attr no value" + sfx, XML, "<llsd><binary encoding>YWJj</binary></llsd>", UNLIMITED, -1, l);
            add("x attr unquoted" + sfx, XML, "<llsd><binary encoding=base64>YWJj</binary></llsd>", UNLIMITED, -1, l);
            add("x attr lt" + sfx, XML, "<llsd><binary encoding=\"<\">YWJj</binary></llsd>", UNLIMITED, -1, l);
            add("x attr other" + sfx, XML, "<llsd><integer a=\"1\" b='2'>5</integer></llsd>", UNLIMITED, -1, l);
            add("x attr ws" + sfx, XML, "<llsd><integer a = \"1\" >5</integer></llsd>", UNLIMITED, -1, l);
            add("x attr crlf" + sfx, XML, "<llsd><binary encoding=\"base\r\n64\">YWJj</binary></llsd>", UNLIMITED, -1, l);
            add("x unclosed" + sfx, XML, "<llsd><map><key>a</key><integer>1</integer></map>", UNLIMITED, -1, l);
            add("x mismatched" + sfx, XML, "<llsd><map><key>a</key><integer>1</integer></array></llsd>", UNLIMITED, -1, l);
            add("x text outside" + sfx, XML, "junk<llsd><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x text after" + sfx, XML, "<llsd><integer>1</integer></llsd>junk", UNLIMITED, -1, l);
            add("x text after newline" + sfx, XML, "<llsd><integer>1</integer></llsd>\njunk", UNLIMITED, -1, l);
            add("x no newline" + sfx, XML, "<llsd><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x trailing newlines" + sfx, XML, "<llsd><integer>1</integer></llsd>\n\n\n", UNLIMITED, -1, l);
            add("x integer spaces" + sfx, XML, "<llsd><integer> 12 </integer></llsd>", UNLIMITED, -1, l);
            add("x integer trailing" + sfx, XML, "<llsd><integer>12abc</integer></llsd>", UNLIMITED, -1, l);
            add("x integer alpha" + sfx, XML, "<llsd><integer>abc</integer></llsd>", UNLIMITED, -1, l);
            add("x integer real" + sfx, XML, "<llsd><integer>1.5</integer></llsd>", UNLIMITED, -1, l);
            add("x integer dot" + sfx, XML, "<llsd><integer>.5</integer></llsd>", UNLIMITED, -1, l);
            add("x integer exp" + sfx, XML, "<llsd><integer>1e3</integer></llsd>", UNLIMITED, -1, l);
            add("x integer empty" + sfx, XML, "<llsd><integer></integer></llsd>", UNLIMITED, -1, l);
            add("x integer self closed" + sfx, XML, "<llsd><integer/></llsd>", UNLIMITED, -1, l);
            add("x integer overflow" + sfx, XML, "<llsd><integer>2147483648</integer></llsd>", UNLIMITED, -1, l);
            add("x integer big overflow" + sfx, XML, "<llsd><integer>99999999999999999999</integer></llsd>", UNLIMITED, -1, l);
            add("x integer overflow 32" + sfx, XML, "<llsd><integer>4294967296</integer></llsd>", UNLIMITED, -1, l);
            add("x integer overflow 33" + sfx, XML, "<llsd><integer>8589934593</integer></llsd>", UNLIMITED, -1, l);
            add("x integer overflow 64" + sfx, XML, "<llsd><integer>18446744073709551616</integer></llsd>", UNLIMITED, -1, l);
            add("x integer overflow 64 minus" + sfx, XML, "<llsd><integer>-18446744073709551616</integer></llsd>", UNLIMITED, -1, l);
            add("x integer negative overflow" + sfx, XML, "<llsd><integer>-2147483649</integer></llsd>", UNLIMITED, -1, l);
            add("x integer negative overflow 33" + sfx, XML, "<llsd><integer>-8589934593</integer></llsd>", UNLIMITED, -1, l);
            add("x integer negative" + sfx, XML, "<llsd><integer>-42</integer></llsd>", UNLIMITED, -1, l);
            add("x integer plus" + sfx, XML, "<llsd><integer>+42</integer></llsd>", UNLIMITED, -1, l);
            add("x integer hex" + sfx, XML, "<llsd><integer>0x10</integer></llsd>", UNLIMITED, -1, l);
            add("x real" + sfx, XML, "<llsd><real>1.5</real></llsd>", UNLIMITED, -1, l);
            add("x real trailing space" + sfx, XML, "<llsd><real>1.5 </real></llsd>", UNLIMITED, -1, l);
            add("x real leading space" + sfx, XML, "<llsd><real> 1.5</real></llsd>", UNLIMITED, -1, l);
            add("x real inf" + sfx, XML, "<llsd><real>inf</real></llsd>", UNLIMITED, -1, l);
            add("x real nan" + sfx, XML, "<llsd><real>nan</real></llsd>", UNLIMITED, -1, l);
            // MSVC stores infinity for an overflowing extraction, glibc DBL_MAX: infinity.
            addEdge("x real overflow" + sfx, XML, "<llsd><real>1e999</real></llsd>", 1, LLSD(std::numeric_limits<F64>::infinity()), l);
            add("x real underflow" + sfx, XML, "<llsd><real>1e-999</real></llsd>", UNLIMITED, -1, l);
            add("x real neg zero" + sfx, XML, "<llsd><real>-0</real></llsd>", UNLIMITED, -1, l);
            add("x real exp" + sfx, XML, "<llsd><real>2.5e3</real></llsd>", UNLIMITED, -1, l);
            add("x real bare e" + sfx, XML, "<llsd><real>2.5e</real></llsd>", UNLIMITED, -1, l);
            add("x real empty" + sfx, XML, "<llsd><real/></llsd>", UNLIMITED, -1, l);
            add("x real precise" + sfx, XML, "<llsd><real>0.1000000000000000055511151</real></llsd>", UNLIMITED, -1, l);
            add("x real trailing junk" + sfx, XML, "<llsd><real>1.5x</real></llsd>", UNLIMITED, -1, l);
            add("x boolean true" + sfx, XML, "<llsd><boolean>true</boolean></llsd>", UNLIMITED, -1, l);
            add("x boolean 1" + sfx, XML, "<llsd><boolean>1</boolean></llsd>", UNLIMITED, -1, l);
            add("x boolean spaced" + sfx, XML, "<llsd><boolean> true</boolean></llsd>", UNLIMITED, -1, l);
            add("x boolean caps" + sfx, XML, "<llsd><boolean>TRUE</boolean></llsd>", UNLIMITED, -1, l);
            add("x boolean 0" + sfx, XML, "<llsd><boolean>0</boolean></llsd>", UNLIMITED, -1, l);
            add("x boolean empty" + sfx, XML, "<llsd><boolean/></llsd>", UNLIMITED, -1, l);
            add("x uuid" + sfx, XML, "<llsd><uuid>c96f9b1e-f589-4100-9774-d98643ce0bed</uuid></llsd>", UNLIMITED, -1, l);
            add("x uuid broken 35" + sfx, XML, "<llsd><uuid>c96f9b1e-f589-4100-9774d98643ce0bed</uuid></llsd>", UNLIMITED, -1, l);
            add("x uuid bad" + sfx, XML, "<llsd><uuid>not-a-uuid</uuid></llsd>", UNLIMITED, -1, l);
            add("x uuid bad hex" + sfx, XML, "<llsd><uuid>c96f9b1e-f589-4100-9774-d98643ce0beg</uuid></llsd>", UNLIMITED, -1, l);
            add("x uuid spaced" + sfx, XML, "<llsd><uuid> c96f9b1e-f589-4100-9774-d98643ce0bed</uuid></llsd>", UNLIMITED, -1, l);
            add("x uuid empty" + sfx, XML, "<llsd><uuid /></llsd>", UNLIMITED, -1, l);
            add("x date" + sfx, XML, "<llsd><date>2006-04-24T16:11:33Z</date></llsd>", UNLIMITED, -1, l);
            add("x date fraction" + sfx, XML, "<llsd><date>2007-12-28T09:22:53.10Z</date></llsd>", UNLIMITED, -1, l);
            add("x date garbage" + sfx, XML, "<llsd><date>garbage</date></llsd>", UNLIMITED, -1, l);
            add("x date empty" + sfx, XML, "<llsd><date/></llsd>", UNLIMITED, -1, l);
            add("x date spaced" + sfx, XML, "<llsd><date> 2006-04-24T16:11:33Z</date></llsd>", UNLIMITED, -1, l);
            add("x uri" + sfx, XML, "<llsd><uri>http://example.com/a?b=c&amp;d=e</uri></llsd>", UNLIMITED, -1, l);
            add("x uri empty" + sfx, XML, "<llsd><uri/></llsd>", UNLIMITED, -1, l);
            add("x string empty" + sfx, XML, "<llsd><string/></llsd>", UNLIMITED, -1, l);
            add("x string ws" + sfx, XML, "<llsd><string>  a  </string></llsd>", UNLIMITED, -1, l);
            add("x string utf8" + sfx, XML, "<llsd><string>h\xc3\xa9llo \xe2\x9c\x93 \xf0\x9f\x8e\x89</string></llsd>", UNLIMITED, -1, l);
            add("x string control" + sfx, XML, std::string("<llsd><string>a\x01b</string></llsd>"), UNLIMITED, -1, l);
            add("x string tab" + sfx, XML, "<llsd><string>a\tb</string></llsd>", UNLIMITED, -1, l);
            add("x string bad utf8" + sfx, XML, "<llsd><string>a\xffz</string></llsd>", UNLIMITED, -1, l);
            add("x string overlong" + sfx, XML, "<llsd><string>a\xc0\x80z</string></llsd>", UNLIMITED, -1, l);
            add("x string surrogate" + sfx, XML, "<llsd><string>a\xed\xa0\x80z</string></llsd>", UNLIMITED, -1, l);
            add("x string fffe" + sfx, XML, "<llsd><string>a\xef\xbf\xbez</string></llsd>", UNLIMITED, -1, l);
            add("x string fffd" + sfx, XML, "<llsd><string>a\xef\xbf\xbdz</string></llsd>", UNLIMITED, -1, l);
            add("x string nonchar fdd0" + sfx, XML, "<llsd><string>a\xef\xb7\x90z</string></llsd>", UNLIMITED, -1, l);
            add("x string max" + sfx, XML, "<llsd><string>a\xf4\x8f\xbf\xbfz</string></llsd>", UNLIMITED, -1, l);
            add("x string beyond max" + sfx, XML, "<llsd><string>a\xf4\x90\x80\x80z</string></llsd>", UNLIMITED, -1, l);
            add("x string cdend" + sfx, XML, "<llsd><string>a]]>b</string></llsd>", UNLIMITED, -1, l);
            add("x string brackets" + sfx, XML, "<llsd><string>a]]b]</string></llsd>", UNLIMITED, -1, l);
            add("x string gt" + sfx, XML, "<llsd><string>a>b</string></llsd>", UNLIMITED, -1, l);
            add("x string lt" + sfx, XML, "<llsd><string>a<b</string></llsd>", UNLIMITED, -1, l);
            add("x nested llsd" + sfx, XML, "<llsd><array><llsd><integer>1</integer></llsd><integer>2</integer></array></llsd>", UNLIMITED, -1, l);
            add("x two roots" + sfx, XML, "<llsd><integer>1</integer><string>x</string></llsd>", UNLIMITED, -1, l);
            add("x llsd empty" + sfx, XML, "<llsd></llsd>", UNLIMITED, -1, l);
            add("x llsd self closed" + sfx, XML, "<llsd/>", UNLIMITED, -1, l);
            add("x llsd whitespace" + sfx, XML, "<llsd>\n  \n</llsd>", UNLIMITED, -1, l);
            add("x key in array" + sfx, XML, "<llsd><array><key>x</key><integer>1</integer></array></llsd>", UNLIMITED, -1, l);
            add("x key at root" + sfx, XML, "<llsd><key>x</key><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x empty key" + sfx, XML, "<llsd><map><key></key><integer>1</integer><key>b</key><integer>2</integer></map></llsd>", UNLIMITED, -1, l);
            add("x dangling key" + sfx, XML, "<llsd><map><key>a</key><integer>1</integer><key>b</key></map></llsd>", UNLIMITED, -1, l);
            add("x dangling key inner" + sfx, XML, "<llsd><map><key>a</key><map><key>dangling</key></map><integer>5</integer></map></llsd>", UNLIMITED, -1, l);
            add("x key key" + sfx, XML, "<llsd><map><key>a</key><key>b</key><integer>1</integer></map></llsd>", UNLIMITED, -1, l);
            add("x dup keys" + sfx, XML, "<llsd><map><key>a</key><integer>1</integer><key>a</key><integer>2</integer></map></llsd>", UNLIMITED, -1, l);
            add("x nested dup keys" + sfx, XML, "<llsd><map><key>a</key><integer>0</integer><key>a</key><map><key>b</key><map><key>c</key><integer>1</integer><key>c</key><integer>2</integer><key>d</key><integer>3</integer></map></map></map></llsd>", UNLIMITED, -1, l);
            add("x nested dup keys container first" + sfx, XML, "<llsd><map><key>a</key><map><key>b</key><map><key>c</key><integer>1</integer><key>c</key><integer>2</integer><key>d</key><integer>3</integer></map></map><key>a</key><integer>0</integer></map></llsd>", UNLIMITED, -1, l);
            add("x map text" + sfx, XML, "<llsd><map>junk<key>a</key>junk<integer>1</integer>junk</map></llsd>", UNLIMITED, -1, l);
            add("x scalar nested" + sfx, XML, "<llsd><integer><integer>5</integer>6</integer></llsd>", UNLIMITED, -1, l);
            add("x unknown root value" + sfx, XML, "<llsd><thing>x</thing></llsd>", UNLIMITED, -1, l);
            add("x unknown nested" + sfx, XML, "<llsd><thing><integer>1</integer></thing></llsd>", UNLIMITED, -1, l);
            add("x map empty" + sfx, XML, "<llsd><map/></llsd>", UNLIMITED, -1, l);
            add("x array empty" + sfx, XML, "<llsd><array /></llsd>", UNLIMITED, -1, l);
            add("x deep 100" + sfx, XML, "<llsd>" + deepXml(100) + "</llsd>", UNLIMITED, -1, l);
            add("x pretty" + sfx, XML, "<llsd>\n    <map>\n        <key>a</key>\n        <array>\n            <integer>1</integer>\n            <real>2.5</real>\n        </array>\n        <key>b</key>\n        <string>c</string>\n    </map>\n</llsd>\n", UNLIMITED, -1, l);
            add("x tag space" + sfx, XML, "<llsd>< integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x tag trailing space" + sfx, XML, "<llsd><integer >1</integer ></llsd>", UNLIMITED, -1, l);
            add("x tag bad name" + sfx, XML, "<llsd><1abc>1</1abc></llsd>", UNLIMITED, -1, l);
            add("x tag caps" + sfx, XML, "<LLSD><INTEGER>1</INTEGER></LLSD>", UNLIMITED, -1, l);
            add("x extra close" + sfx, XML, "<llsd><integer>1</integer></llsd></llsd>", UNLIMITED, -1, l);
            add("x long line" + sfx, XML, "<llsd><string>" + std::string(3000, 'x') + "</string></llsd>", UNLIMITED, -1, l);
            add("x multi line" + sfx, XML, "<llsd>\n<string>\nmulti\nline\n</string>\n</llsd>\n", UNLIMITED, -1, l);
            add("x crlf lines" + sfx, XML, "<llsd>\r\n<string>a</string>\r\n</llsd>\r\n", UNLIMITED, -1, l);
            add("x old style" + sfx, XML, "<? LLSD/XML ?>\n<llsd><integer>1</integer></llsd>", UNLIMITED, -1, l);
            add("x second doc" + sfx, XML, "<llsd><integer>1</integer></llsd>\n<llsd><integer>2</integer></llsd>\n", UNLIMITED, -1, l);
            add("x second doc same line" + sfx, XML, "<llsd><integer>1</integer></llsd><llsd><integer>2</integer></llsd>\n", UNLIMITED, -1, l);
        }
        return v;
    }

    bool equalBits(const LLSD& a, const LLSD& b)
    {
        if (a.type() != b.type())
        {
            return false;
        }
        switch (a.type())
        {
        case LLSD::TypeUndefined:
            return true;
        case LLSD::TypeBoolean:
            return a.asBoolean() == b.asBoolean();
        case LLSD::TypeInteger:
            return a.asInteger() == b.asInteger();
        case LLSD::TypeReal:
        case LLSD::TypeDate:
        {
            F64 x = a.asReal();
            F64 y = b.asReal();
            return memcmp(&x, &y, sizeof(x)) == 0;
        }
        case LLSD::TypeString:
            return a.asStringRef() == b.asStringRef();
        case LLSD::TypeUUID:
            return a.asUUID() == b.asUUID();
        case LLSD::TypeURI:
            return a.asString() == b.asString();
        case LLSD::TypeBinary:
            return a.asBinary() == b.asBinary();
        case LLSD::TypeMap:
        {
            if (a.size() != b.size())
            {
                return false;
            }
            LLSD::map_const_iterator ia = a.beginMap();
            LLSD::map_const_iterator ib = b.beginMap();
            for (; ia != a.endMap(); ++ia, ++ib)
            {
                if (ia->first != ib->first || !equalBits(ia->second, ib->second))
                {
                    return false;
                }
            }
            return true;
        }
        case LLSD::TypeArray:
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (!equalBits(a[i], b[i]))
                {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
        }
    }

    std::string describe(const LLSD& v)
    {
        std::ostringstream out;
        LLSDSerialize::toNotation(v, out);
        std::string s = out.str();
        if (s.size() > 300)
        {
            s = s.substr(0, 300) + "...";
        }
        return s;
    }

    std::string readFile(const std::string& path)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
        {
            return std::string();
        }
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
}

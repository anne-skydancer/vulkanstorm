/**
 * @file llaisadapter_parity_test.cpp
 * @brief The AIS reply adapter corpus (inventory rung, recorder R3a, the
 *        HTTP part): what the coroutine adapter makes of an AIS reply.
 *
 * AIS commands reach the server through LLCoreHttpUtil::HttpCoroutineAdapter
 * (postAndSuspend, getAndSuspend, ...), whose LLSD handler turns the HTTP
 * response into the LLSD the coroutine receives: the body parsed as LLSD
 * XML (or moved under "content" when it is not a map), the http_result
 * status block, the error body, the 499 that replaces a success status
 * when an application/llsd+xml body does not parse, and the scheduling of
 * the parse (bodies over 64 KB parse on the "General" work queue). With the
 * AIS depth cap (an approved fix), a body nested AIS_MAX_REPLY_DEPTH
 * containers deep counts as unparsable.
 *
 * This test feeds the real handler canned HttpResponses, a matrix of status
 * x body x content type x size, and records for each the LLSD posted to the
 * coroutine's pump and where the parse ran. It replays
 * tests/fixtures/inventory/ais/adapter (binary LLSD, case by case); with
 * VKS_AISADAPTER_DUMP=<dir> it writes the fixtures there instead and
 * fails. The R3a update corpus (llaisupdate_parity) starts where this one
 * ends: its scripted results are what this adapter produces.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llcorehttputil.h"
#include "bufferarray.h"
#include "httpheaders.h"
#include "httpresponse.h"
#include "llerrorcontrol.h"
#include "llevents.h"
#include "llsdserialize.h"
#include "workqueue.h"

#include "../test/lltut.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifndef VKS_AISADAPTER_FIXTURE_DIR
#error VKS_AISADAPTER_FIXTURE_DIR must name tests/fixtures/inventory/ais/adapter
#endif

namespace
{
    // The depth cap AIS sets on its adapter (llaisapi.cpp AIS_MAX_REPLY_DEPTH).
    const S32 AIS_MAX_REPLY_DEPTH = 256;
    // HttpCoroHandler::onCompleted's MAX_BODY_SIZE_THRESHOLD.
    const size_t WORKER_THRESHOLD = 65536;
    // Strings and binaries longer than this are recorded as length and hash.
    const size_t LONG_VALUE = 256;
    const char* const URL = "https://ais.example/cap/category/c0000000-0000-4000-8000-000000000001/children?depth=50";

    std::string fnv1a64(const std::string& s)
    {
        U64 h = 0xcbf29ce484222325ULL;
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 0x100000001b3ULL;
        }
        std::ostringstream o;
        o << std::hex << std::setw(16) << std::setfill('0') << h;
        return o.str();
    }

    // The recorded form of a result: long strings and binaries shortened.
    LLSD shorten(const LLSD& sd)
    {
        switch (sd.type())
        {
        case LLSD::TypeString:
            if (sd.asStringRef().size() > LONG_VALUE)
            {
                const std::string& s = sd.asStringRef();
                return LLSD("<" + std::to_string(s.size()) + " bytes, fnv1a64 " + fnv1a64(s) + ">");
            }
            return sd;
        case LLSD::TypeBinary:
            if (sd.asBinary().size() > LONG_VALUE)
            {
                const LLSD::Binary& b = sd.asBinary();
                std::string s(b.begin(), b.end());
                return LLSD("<binary " + std::to_string(s.size()) + " bytes, fnv1a64 " + fnv1a64(s) + ">");
            }
            return sd;
        case LLSD::TypeMap:
        {
            LLSD out = LLSD::emptyMap();
            for (LLSD::map_const_iterator it = sd.beginMap(); it != sd.endMap(); ++it)
            {
                out[it->first] = shorten(it->second);
            }
            return out;
        }
        case LLSD::TypeArray:
        {
            LLSD out = LLSD::emptyArray();
            for (LLSD::array_const_iterator it = sd.beginArray(); it != sd.endArray(); ++it)
            {
                out.append(shorten(*it));
            }
            return out;
        }
        default:
            return sd;
        }
    }

    std::string to_binary(const LLSD& sd)
    {
        std::ostringstream o;
        LLSDSerialize::toBinary(sd, o);
        return o.str();
    }

    std::string to_notation(const LLSD& sd)
    {
        std::ostringstream o;
        LLSDSerialize::toNotation(sd, o);
        return o.str();
    }

    //------------------------------------------------------------------------
    // The matrix
    //------------------------------------------------------------------------

    struct Status
    {
        const char* name;
        LLCore::HttpStatus status;
    };

    std::vector<Status> statuses()
    {
        using LLCore::HttpStatus;
        return {
            { "200", HttpStatus(200) },
            { "201", HttpStatus(201) },
            { "204", HttpStatus(204) },
            { "206", HttpStatus(206) },
            { "299", HttpStatus(299) },
            { "300", HttpStatus(300) },
            { "400", HttpStatus(400, "Bad Request") },
            { "403", HttpStatus(403) },
            { "404", HttpStatus(404, "Not Found") },
            { "410", HttpStatus(410) },
            { "499", HttpStatus(499) },
            { "500", HttpStatus(500, "Internal Server Error") },
            { "503", HttpStatus(503) },
            // curl: operation timed out (CURLE_OPERATION_TIMEDOUT)
            { "curl28", HttpStatus(HttpStatus::EXT_CURL_EASY, 28) },
            { "canceled", HttpStatus(HttpStatus::LLCORE, LLCore::HE_OP_CANCELED) },
            // a reply to a request already canceled: nothing is posted
            { "notfound", HttpStatus(HttpStatus::LLCORE, LLCore::HE_HANDLE_NOT_FOUND) },
        };
    }

    // `levels` nested containers (maps and arrays alternating) around one
    // integer.
    std::string nested(size_t levels)
    {
        std::string s = "<?xml version=\"1.0\" ?><llsd>";
        for (size_t i = 0; i < levels; ++i)
        {
            s += (i % 2 == 0) ? "<map><key>k</key>" : "<array>";
        }
        s += "<integer>7</integer>";
        for (size_t i = levels; i-- > 0;)
        {
            s += (i % 2 == 0) ? "</map>" : "</array>";
        }
        s += "</llsd>";
        return s;
    }

    struct Body
    {
        std::string name;
        bool present;     // false: the response has no body buffer at all
        std::string bytes;
    };

    std::vector<Body> bodies()
    {
        const std::string head = "<?xml version=\"1.0\" ?><llsd>";
        std::vector<Body> b = {
            { "absent", false, "" },
            { "empty", true, "" },
            { "map", true, head + "<map><key>category_id</key><uuid>c0000000-0000-4000-8000-000000000001</uuid>"
                                   "<key>version</key><integer>7</integer></map></llsd>" },
            { "map_item_id", true, head + "<map><key>item_id</key><uuid>a0000000-0000-4000-8000-000000000001</uuid>"
                                          "<key>error</key><string>gone</string></map></llsd>" },
            { "map_http_result", true, head + "<map><key>http_result</key><map><key>status</key><integer>1</integer>"
                                              "</map><key>x</key><integer>2</integer></map></llsd>" },
            { "array", true, head + "<array><integer>1</integer><string>two</string></array></llsd>" },
            { "string", true, head + "<string>just text</string></llsd>" },
            { "undef", true, head + "<undef /></llsd>" },
            { "unterminated", true, head + "<map><key>a</key><integer>1</integer>" },
            { "text", true, "Service Unavailable" },
            { "json", true, "{\"category_id\":\"c0000000-0000-4000-8000-000000000001\"}" },
            { "deep255", true, nested(AIS_MAX_REPLY_DEPTH - 1) },
            { "deep256", true, nested(AIS_MAX_REPLY_DEPTH) },
            { "deep100000", true, nested(100000) },
        };
        return b;
    }

    // Large variants (over the worker threshold) of some bodies: padded in a
    // way that keeps their shape.
    std::vector<Body> large_bodies()
    {
        const std::string head = "<?xml version=\"1.0\" ?><llsd>";
        const std::string pad(WORKER_THRESHOLD, 'p');
        return {
            { "large_map", true, head + "<map><key>category_id</key><uuid>c0000000-0000-4000-8000-000000000001</uuid>"
                                         "<key>pad</key><string>" + pad + "</string></map></llsd>" },
            { "large_array", true, head + "<array><string>" + pad + "</string></array></llsd>" },
            { "large_unterminated", true, head + "<map><key>pad</key><string>" + pad + "</string>" },
            { "large_deep256", true, nested(AIS_MAX_REPLY_DEPTH) + std::string(WORKER_THRESHOLD, ' ') },
        };
    }

    struct ContentType
    {
        const char* name;
        const char* header; // the raw header line, or null for none
    };

    std::vector<ContentType> content_types()
    {
        return {
            { "none", nullptr },
            { "llsd_xml", "Content-Type: application/llsd+xml" },
            { "llsd_xml_charset", "Content-Type: application/llsd+xml; charset=utf-8" },
            { "llsd_xml_upper", "Content-Type: APPLICATION/LLSD+XML" },
            { "xml", "Content-Type: application/xml" },
            { "text", "Content-Type: text/plain" },
        };
    }

    //------------------------------------------------------------------------
    // One case through the handler
    //------------------------------------------------------------------------

    struct Queues
    {
        // HttpCoroHandler::onCompleted posts a large success body from
        // "mainloop" to "General" and the reply back; the test runs both.
        LL::WorkQueue mainloop{ "mainloop" };
        LL::WorkQueue general{ "General" };
    };

    LLSD run_case(Queues& q, const std::string& id, const Status& st, const Body& body, const ContentType& ct)
    {
        LLEventStream pump("vks_aisadapter_" + id, true);
        bool posted = false;
        LLSD result;
        LLTempBoundListener conn = pump.listen("capture", [&](const LLSD& e)
        {
            posted = true;
            result = e;
            return false;
        });

        LLCoreHttpUtil::HttpCoroHandler::ptr_t handler = LLCoreHttpUtil::createLLSDHandler(pump, AIS_MAX_REPLY_DEPTH);

        LLCore::HttpResponse* response = new LLCore::HttpResponse();
        response->setStatus(st.status);
        response->setRequestURL(URL);
        response->setRequestMethod("GET");
        if (body.present)
        {
            LLCore::BufferArray* ba = new LLCore::BufferArray();
            if (!body.bytes.empty())
            {
                ba->append(body.bytes.data(), body.bytes.size());
            }
            response->setBody(ba);
            ba->release();
        }
        LLCore::HttpHeaders::ptr_t headers = std::make_shared<LLCore::HttpHeaders>();
        if (ct.header)
        {
            headers->appendNormal(ct.header, strlen(ct.header));
        }
        const char* other = "X-Vks-Test: 1";
        headers->appendNormal(other, strlen(other));
        const char* flag = "X-Vks-Flag:";
        headers->appendNormal(flag, strlen(flag));
        response->setHeaders(headers);

        handler->onCompleted(LLCORE_HTTP_HANDLE_INVALID, response);
        std::string path = posted ? "main" : "none";
        if (!posted)
        {
            q.general.runPending();
            q.mainloop.runPending();
            if (posted)
            {
                path = "worker";
            }
        }
        response->release();

        LLSD rec = LLSD::emptyMap();
        rec["id"] = id;
        rec["status_type"] = (LLSD::Integer)st.status.getType();
        rec["status_status"] = (LLSD::Integer)st.status.getStatus();
        rec["body_size"] = (LLSD::Integer)body.bytes.size();
        rec["path"] = path;
        if (posted)
        {
            rec["result"] = shorten(result);
        }
        return rec;
    }

    LLSD run_matrix()
    {
        Queues q;
        LLSD cases = LLSD::emptyArray();
        std::vector<Body> all = bodies();
        for (const Body& b : large_bodies())
        {
            all.push_back(b);
        }
        for (const Status& st : statuses())
        {
            for (const Body& body : all)
            {
                for (const ContentType& ct : content_types())
                {
                    std::string id = std::string(st.name) + "/" + body.name + "/" + ct.name;
                    if (getenv("VKS_AISADAPTER_TRACE"))
                    {
                        std::cerr << "case " << id << std::endl;
                    }
                    cases.append(run_case(q, id, st, body, ct));
                }
            }
        }
        return cases;
    }

    std::string read_file(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream o;
        o << in.rdbuf();
        return o.str();
    }
}

namespace tut
{
    struct aisadapter_data
    {
    };
    typedef test_group<aisadapter_data> aisadapter_group;
    typedef aisadapter_group::object aisadapter_object;
    tut::aisadapter_group aisadapter("llaisadapter_parity");

    template<> template<>
    void aisadapter_object::test<1>()
    {
        set_test_name("adapter: status x body x content type x size");
        // The handler's parse failures log a warning each; keep the run quiet.
        LLError::setDefaultLevel(LLError::LEVEL_ERROR);

        const LLSD cases = run_matrix();

        const char* dump = getenv("VKS_AISADAPTER_DUMP");
        if (dump && *dump)
        {
            const std::string dir(dump);
            std::ofstream bin(dir + "/expected.llsd", std::ios::binary);
            bin << to_binary(cases);
            std::ofstream txt(dir + "/expected.txt", std::ios::binary);
            const char* commit = getenv("VKS_AISADAPTER_COMMIT");
            txt << "# The AIS reply adapter corpus (R3a): one case per line, notation.\n";
            txt << "# viewer commit " << (commit && *commit ? commit : "unknown") << "\n";
#if defined(_MSC_VER)
            txt << "# platform MSVC " << _MSC_VER << "\n";
#elif defined(__GNUC__)
            txt << "# platform GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "\n";
#else
            txt << "# platform other\n";
#endif
            txt << "# cases " << cases.size() << "\n";
            for (LLSD::array_const_iterator it = cases.beginArray(); it != cases.endArray(); ++it)
            {
                txt << to_notation(*it) << "\n";
            }
            fail("dump mode: wrote " + std::to_string(cases.size()) + " cases to " + dir + " (dump mode never passes)");
        }

        const std::string dir(VKS_AISADAPTER_FIXTURE_DIR);
        const std::string bytes = read_file(dir + "/expected.llsd");
        ensure("fixture tests/fixtures/inventory/ais/adapter/expected.llsd is missing", !bytes.empty());
        LLSD expected;
        std::istringstream in(bytes);
        ensure("expected.llsd does not parse", LLSDSerialize::fromBinary(expected, in, bytes.size()) > 0);

        std::ostringstream report;
        size_t differ = 0;
        const size_t n = std::max<size_t>(expected.size(), cases.size());
        for (size_t i = 0; i < n; ++i)
        {
            const LLSD& want = expected[(S32)i];
            const LLSD& got = cases[(S32)i];
            if (to_binary(want) != to_binary(got))
            {
                if (++differ <= 20)
                {
                    report << "\ncase " << i << " (" << (got.has("id") ? got["id"].asString() : want["id"].asString())
                           << "): expected " << to_notation(want) << "\n  actual " << to_notation(got);
                }
            }
        }
        if (differ)
        {
            fail(std::to_string(differ) + " of " + std::to_string(n) + " cases differ:" + report.str());
        }
        std::cout << "  llaisadapter_parity: " << cases.size() << " cases, 0 differences" << std::endl;
    }
}

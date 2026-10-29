/**
 * @file llinventory_parity_test.cpp
 * @brief The inventory data model (indra/llinventory, llcommon/llassettype)
 * against its oracle (vkstorm-rust rung seven, recorder R1).
 *
 * Two comparisons, both with zero differences expected:
 * 1. the live classes against the frozen copies in llinventory_oracle.cpp,
 *    in process, over every corpus (C1-C9 structured cases, the C3/C4/C5
 *    random streams, the C3 exhaustive permission sweeps, the C10 Library
 *    cache);
 * 2. the live classes against the fixtures recorded before any port commit
 *    (tests/fixtures: the structured corpora line for line, the random
 *    streams by chunked digests and committed sample, the exhaustive sweeps
 *    by chunked digests, the Library cache field by field), and the
 *    fixture tree itself (a missing or an unexpected file fails).
 * The build also runs check_r1_copies.py after this test (CMakeLists.txt).
 * A failure names the corpus, the case, the field (and column) and the
 * expected and actual values. The corpora, the dump and the bench are in
 * llinventory_tool.cpp, compiled into this test with VKS_INV_NO_MAIN.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "../test/lltut.h"

#include <iostream>
#include <string>

#ifndef VKS_INV_FIXTURE_DIR
#error VKS_INV_FIXTURE_DIR must name indra/llinventory/tests/fixtures
#endif

// Defined in llinventory_tool.cpp.
namespace vksinv
{
struct CheckResult
{
    U64 groups = 0;
    U64 cases = 0;
    U64 diffs = 0;
    std::string report;
};
void init_runtime();
void check_structured(const std::string& fixture_dir, CheckResult& r);
void check_random(const std::string& fixture_dir, CheckResult& r);
void check_exhaustive(const std::string& fixture_dir, CheckResult& r);
void check_library(const std::string& fixture_dir, CheckResult& r);
void check_fixture_tree(const std::string& fixture_dir, CheckResult& r);
}

namespace tut
{
struct inventory_parity_data
{
    inventory_parity_data() { vksinv::init_runtime(); }

    void run(const char* what, void (*fn)(const std::string&, vksinv::CheckResult&))
    {
        vksinv::CheckResult r;
        fn(VKS_INV_FIXTURE_DIR, r);
        std::cout << "llinventory_parity " << what << ": " << r.groups << " groups, " << r.cases << " cases, " << r.diffs
                  << " differences" << std::endl;
        if (r.diffs)
        {
            std::cout << r.report << std::flush;
        }
        ensure_equals(std::string(what) + " differences:\n" + r.report, r.diffs, (U64)0);
    }
};
typedef test_group<inventory_parity_data> inventory_parity_group;
typedef inventory_parity_group::object inventory_parity_object;
tut::inventory_parity_group inventory_parity("llinventory_parity");

template<> template<> void inventory_parity_object::test<1>()
{
    set_test_name("structured corpora C1-C9");
    run("structured", &vksinv::check_structured);
}

template<> template<> void inventory_parity_object::test<2>()
{
    set_test_name("random streams C3, C4, C5");
    run("random", &vksinv::check_random);
}

template<> template<> void inventory_parity_object::test<3>()
{
    set_test_name("exhaustive permission sweeps C3");
    run("exhaustive", &vksinv::check_exhaustive);
}

template<> template<> void inventory_parity_object::test<4>()
{
    set_test_name("Linden Library cache C10");
    run("library", &vksinv::check_library);
}

template<> template<> void inventory_parity_object::test<5>()
{
    set_test_name("fixture tree: no missing and no unexpected files or digests");
    run("fixture tree", &vksinv::check_fixture_tree);
}
}

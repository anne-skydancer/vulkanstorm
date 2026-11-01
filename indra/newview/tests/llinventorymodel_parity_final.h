/**
 * @file llinventorymodel_parity_final.h
 * @brief The closing checks of llinventorymodel_parity_test.cpp (included
 *        once, last).
 *
 * - Every corpus test ran (no partial run passes unless
 *   VKS_INVMODEL_PARTIAL=1, a developer flag).
 * - The fixture tree holds exactly the files the corpora read or wrote, the
 *   READMEs and .gitattributes, and the other platform's own records
 *   (c4_validation_qa.<other library>.txt; c5_write_safety.linux.txt on
 *   Windows): a missing file fails in its corpus, an unexpected one here.
 * - tests/check_r2_copies.py passes (the frozen copies against the live
 *   source).
 * - Record mode (VKS_INVMODEL_DUMP) never passes: it writes DUMP_COMPLETE
 *   into the dump directory and then fails, so a dump cannot be mistaken
 *   for a replay.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

namespace tut
{
    template<> template<> void invmodel_object::test<20>()
    {
        set_test_name("closing checks: every corpus ran, fixture tree, frozen copies, record mode");
        const std::set<int> all = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        bool partial = !getenv_str("VKS_INVMODEL_PARTIAL").empty();
        std::string missing_tests;
        for (int n : all) if (!env().ran.count(n)) missing_tests += " " + std::to_string(n);
        ensure("corpus tests that did not run:" + missing_tests + " (set VKS_INVMODEL_PARTIAL=1 for a developer run)",
               partial || missing_tests.empty());

        // The fixture tree.
        std::set<std::string> allowed = env().touched;
        for (const char* a : { "cache/README.md", "cache/.gitattributes", "msgs/README.md", "msgs/.gitattributes" }) allowed.insert(a);
        // The other standard library's records: its QA-mode file and its
        // <area>/<library>/ tree (see platform_rel()).
        allowed.insert(std::string("cache/c4_validation_qa.") + other_stdlib_name() + ".txt");
        std::string root = env().dump() ? env().out : env().fixtures;
        std::string unexpected;
        for (const char* area : { "cache", "msgs" })
        {
            std::string dir = join(root, area);
            if (!fs::is_directory(dir)) continue;
            const std::string other_tree = std::string(area) + "/" + other_stdlib_name() + "/";
            for (const auto& e : fs::recursive_directory_iterator(dir))
            {
                if (!e.is_regular_file()) continue;
                std::string rel = std::string(area) + "/" + fs::relative(e.path(), dir).generic_string();
                if (rel.rfind(other_tree, 0) == 0) continue;
                if (!allowed.count(rel)) unexpected += "\n  " + rel;
            }
        }
        if (!partial) ensure("unexpected files in the fixture tree " + root + ":" + unexpected, unexpected.empty());

        // The frozen copies.
        std::string cmd = std::string("\"") + VKS_INVMODEL_PYTHON + "\" \"" + VKS_INVMODEL_SOURCE_ROOT
                          + "/indra/newview/tests/check_r2_copies.py\" \"" + VKS_INVMODEL_SOURCE_ROOT + "\"";
#if LL_WINDOWS
        cmd = "\"" + cmd + "\"";  // cmd.exe strips one pair of outer quotes
#endif
        fflush(stdout);
        int rc = std::system(cmd.c_str());
        ensure_equals("check_r2_copies.py exit status", rc, 0);

        fs::remove_all(env().work);
        if (env().dump())
        {
            std::string list;
            for (const std::string& rel : env().touched) list += rel + "\n";
            write_file(join(env().out, "DUMP_COMPLETE"), list);
            ensure("record mode: fixtures written to " + env().out + "; record mode never passes, replay to compare", false);
        }
    }
}

/**
 * @file llinventorymodel_parity_c5.h
 * @brief Corpus C5 of llinventorymodel_parity_test.cpp: cache write safety
 *        (included once, after the C4 corpus).
 *
 * Every case first saves a cache at logout (the "old" .gz), changes the
 * model so a new save would differ, injects one fault and saves again. It
 * records whether the old .gz is still byte-identical, the decompressed
 * content of the .gz afterwards, the cache and temp directories (random temp
 * names shown as <uuid>.tmp) and the journal.
 *
 * - save_ok, stale_gz_t_file: the save goes through "<cache>.gz.t" and a
 *   rename; a stale .gz.t is replaced.
 * - gz_t_is_directory: "<cache>.gz.t" cannot be created, so gzip fails and
 *   the old .gz stays (a direct write of the .gz would replace it).
 * - temp_dir_missing: saveToFile cannot open its temp file; nothing is
 *   gzipped; the old .gz stays.
 * - mid_write_file_size_limit (Linux only, cache/c5_write_safety.linux.txt):
 *   RLIMIT_FSIZE makes saveToFile fail part-way through its temp file; the
 *   save check removes the partial file and keeps the old .gz (ignoring the
 *   result would gzip the partial file over it). Windows has no portable
 *   way to fail a file write part-way, so this case is recorded on Linux
 *   only and the Windows replay does not run it.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#if !LL_WINDOWS
#include <csignal>
#include <sys/resource.h>
#endif

namespace
{
    enum class Fault { NONE, STALE_GZ_T, GZ_T_DIRECTORY, TEMP_DIR_MISSING, FILE_SIZE_LIMIT };

    Case write_case(const std::string& id, Fault fault)
    {
        Case c;
        c.id = id;
        trace("c5 " + id);
        std::string dir = join(env().work, "c5");
        fresh(dir);
        World w = standard_world();
        populate(w, dir);
        vks_inv_frozen::save_inventory_at_logout(false);
        clear_journal();
        std::string gz = LLInventoryModel::getInvCacheAddres(AGENT) + ".gz";
        std::string scratch = join(env().work, "c5scratch");
        std::string old_gz = read_file(gz);
        std::string old_plain = gunzip_bytes(gz, scratch);
        c.lines.push_back("old size=" + std::to_string(old_plain.size()) + " sha256=" + sha256_hex(old_plain));
        // A change a new save would carry.
        gInventory.updateItem(item(I(1200), F(SF_OBJECTS), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Added before the second save", A(1200)));
        gInventory.getCategory(F(SF_OBJECTS))->setDescendentCount(gInventory.getCategory(F(SF_OBJECTS))->getViewerDescendentCount());
        gInventory.notifyObservers();
        clear_journal();

        std::string temp = gDirUtilp->getTempDir();
        switch (fault)
        {
        case Fault::STALE_GZ_T: write_file(gz + ".t", "stale partial gzip from an interrupted save"); break;
        case Fault::GZ_T_DIRECTORY: fs::create_directories(gz + ".t"); break;
        case Fault::TEMP_DIR_MISSING: set_temp_dir(join(env().work, "no-such-temp-dir")); break;
        default: break;
        }
        files(c.lines, dir, "before_save");
#if !LL_WINDOWS
        struct rlimit saved;
        if (fault == Fault::FILE_SIZE_LIMIT)
        {
            signal(SIGXFSZ, SIG_IGN);
            getrlimit(RLIMIT_FSIZE, &saved);
            struct rlimit lim = saved;
            lim.rlim_cur = 4096;
            setrlimit(RLIMIT_FSIZE, &lim);
        }
#endif
        if (fault == Fault::STALE_GZ_T || fault == Fault::NONE)
        {
            vks_inv_frozen::save_inventory_at_logout(false);  // the logout path
        }
        else
        {
            gInventory.cache(w.agent_root, AGENT);
        }
#if !LL_WINDOWS
        if (fault == Fault::FILE_SIZE_LIMIT)
        {
            setrlimit(RLIMIT_FSIZE, &saved);
            signal(SIGXFSZ, SIG_DFL);
        }
#endif
        if (fault == Fault::TEMP_DIR_MISSING) set_temp_dir(temp);
        c.lines.push_back("step save");
        take(c.lines);
        std::string new_gz = read_file(gz);
        std::string new_plain = gunzip_bytes(gz, scratch);
        c.lines.push_back(std::string("old_gz_byte_identical=") + (new_gz == old_gz ? "1" : "0"));
        c.lines.push_back("after size=" + std::to_string(new_plain.size()) + " sha256=" + sha256_hex(new_plain));
        c.lines.push_back(std::string("gz_t_present=") + (fs::exists(gz + ".t") ? (fs::is_directory(gz + ".t") ? "directory" : "file") : "0"));
        files(c.lines, dir, "after_save");
        files(c.lines, temp, "temp");
        // Clear what a failed save leaves behind, for the next case.
        fs::remove_all(temp);
        fs::create_directories(temp);
        return c;
    }
}

namespace tut
{
    template<> template<> void invmodel_object::test<10>()
    {
        set_test_name("C5 cache write safety");
        env().ran.insert(10);
        Cases cases;
        cases.push_back(write_case("save_ok", Fault::NONE));
        cases.push_back(write_case("stale_gz_t_file", Fault::STALE_GZ_T));
        cases.push_back(write_case("gz_t_is_directory", Fault::GZ_T_DIRECTORY));
        cases.push_back(write_case("temp_dir_missing", Fault::TEMP_DIR_MISSING));
        corpus("cache/c5_write_safety.txt", "C5 cache write safety", "none", cases);
#if !LL_WINDOWS
        Cases linux_cases;
        linux_cases.push_back(write_case("mid_write_file_size_limit", Fault::FILE_SIZE_LIMIT));
        corpus("cache/c5_write_safety.linux.txt", "C5 cache write safety, Linux only (RLIMIT_FSIZE)", "none", linux_cases);
#endif
    }
}

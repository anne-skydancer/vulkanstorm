/**
 * @file llinventorymodel_parity_logs.h
 * @brief The log lines of the login load (loadSkeleton) against the C++
 *        before the port (included once, after the C2 corpus).
 *
 * <Vulkanstorm> R2 log parity. Each case logs in (the library's skeleton,
 * then the agent's) over cache files built from C2's base caches, and
 * records every line the two loadSkeleton calls log (level, tags and text;
 * not the time, location or function), once with debug lines enabled and
 * once at INFO. The cache directory in a line is "<cache>/"; the load time
 * of the summary line is "<t>". The cases are links to links (a getter that
 * follows one logs "Warning: Accessing link to link" from
 * LLViewerInventoryItem::getLinkedItem, in the cache decode's item filter,
 * the first pass and the retry, and in the debug lines' getName() and
 * getAssetUUID()), a null-id link and a link deferred after an item that
 * addItem logs (the order of the debug lines among addItem's own).
 *
 * The record, cache/c2_logs.txt, was made from the llinventorymodel.cpp
 * before the port (vkstorm-rust at 3846816a16) built into this test; the
 * loads read their files in the order given and walk no unordered map, so
 * the record is shared by every standard library.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "llerrorcontrol.h"

#include <regex>

namespace
{
    // Every line logged while it is installed: level and tags, no time,
    // location or function.
    class LogCapture : public LLError::Recorder
    {
    public:
        LogCapture()
        {
            showTime(false);
            showLevel(true);
            showTags(true);
            showLocation(false);
            showFunctionName(false);
            showMultiline(false);
        }
        void recordMessage(LLError::ELevel, const std::string& message) override
        {
            mLines.push_back(message);
        }
        std::vector<std::string> mLines;
    };

    std::string scrub_log_line(std::string line, const std::string& cache_dir)
    {
        for (size_t at = line.find(cache_dir); at != std::string::npos; at = line.find(cache_dir, at))
        {
            line.replace(at, cache_dir.size(), "<cache>");
        }
        std::replace(line.begin(), line.end(), '\\', '/');
        static const std::regex cache("<cache>/+");
        static const std::regex seconds("after [-+.0-9eE]+s? seconds");
        line = std::regex_replace(line, cache, "<cache>/");
        return std::regex_replace(line, seconds, "after <t> seconds");
    }

    struct LogCase
    {
        std::string id;
        std::string note;
        std::map<std::string, std::string> files;
    };

    std::vector<LogCase> make_log_cases()
    {
        std::vector<LogCase> cases;
        std::string scratch = join(env().work, "c2logs_gz");
        Base base = make_base(standard_world());
        auto gz = [&](const CacheBody& b) { return gzip_bytes(b.plain(), scratch); };
        auto with = [&](const std::string& id, const std::string& note, const CacheBody& agent, const CacheBody& library)
        {
            LogCase c;
            c.id = id;
            c.note = note;
            c.files[gz_name(AGENT)] = gz(agent);
            c.files[gz_name(LIB_OWNER)] = gz(library);
            return c;
        };
        auto link_sd = [&](const LLUUID& id, const LLUUID& parent, const LLUUID& target, const std::string& name)
        {
            LLSD l = *CacheBody(base.agent).item(I(33));
            l["item_id"] = id;
            l["parent_id"] = parent;
            l["asset_id"] = target;
            l["name"] = name;
            return l;
        };

        cases.push_back(with("baseline", "both caches current", base.agent, base.library));
        {
            CacheBody a = base.agent;
            a.items().append(link_sd(I(900), F(SF_OBJECTS), I(33), "Link to a COF link"));
            cases.push_back(with("link_to_link_after_target", "a link to a link, after its target in the file: followed in the first pass and in the retry; still broken", a, base.library));
        }
        {
            CacheBody a = base.agent;
            LLSD arr = LLSD::emptyArray();
            arr.append(link_sd(I(901), F(SF_OBJECTS), I(33), "Link to a COF link, first"));
            for (LLSD::array_const_iterator it = a.items().beginArray(); it != a.items().endArray(); ++it) arr.append(*it);
            a.items() = arr;
            cases.push_back(with("link_to_link_before_target", "a link to a link, before its target in the file: deferred without following; followed in the retry", a, base.library));
        }
        {
            CacheBody l = base.library;
            LLSD lib_link = *l.item(LI(1));
            lib_link["item_id"] = LI(9);
            lib_link["type"] = "link";
            lib_link.erase("shadow_id"); // a no-modify item caches its asset id encoded
            lib_link["asset_id"] = LI(1);
            lib_link["name"] = "Library link";
            l.items().append(lib_link);
            CacheBody a = base.agent;
            a.items().append(link_sd(I(902), F(SF_COF), LI(9), "Link to a Library link"));
            a.items().append(link_sd(LLUUID::null, F(SF_COF), LI(9), "Null-id link to a Library link"));
            cases.push_back(with("link_to_library_link", "an agent link to a Library link (in the model before the agent's load: followed by the decode's item filter too) and a null-id link to it (its debug line's getName() follows it)", a, l));
        }
        {
            CacheBody a = base.agent;
            LLSD odd = *a.item(I(10));
            odd["item_id"] = I(904);
            odd["type"] = 200;
            odd["name"] = "Unknown type 200";
            LLSD arr = LLSD::emptyArray();
            arr.append(odd);
            arr.append(link_sd(I(903), F(SF_OBJECTS), pid(TAG_ITEM, 0x9903), "Link to nothing"));
            for (LLSD::array_const_iterator it = a.items().beginArray(); it != a.items().endArray(); ++it) arr.append(*it);
            a.items() = arr;
            cases.push_back(with("deferred_after_add_line", "an item addItem logs (unknown asset type 200), then a link deferred as broken: their lines in that order", a, base.library));
        }
        return cases;
    }

    Case run_log_case(const LogCase& c, bool debug)
    {
        Case out;
        out.id = c.id + (debug ? "/debug" : "/info");
        trace("logs " + out.id);
        std::string dir = join(env().work, "c2logs");
        fresh(dir);
        set_cache_dir(dir);
        for (const auto& kv : c.files) write_file(join(dir, kv.first), kv.second);
        out.lines.push_back("note " + c.note);
        const World w = standard_world();
        gInventory.setRootFolderID(w.agent_root);
        gInventory.setLibraryRootFolderID(w.lib_root);
        gInventory.setLibraryOwnerID(LIB_OWNER);
        const LLSD lib_skel = skeleton(w.library);
        const LLSD agent_skel = skeleton(w.agent);

        LLError::SettingsStoragePtr saved = LLError::saveAndResetSettings();
        LLError::setDefaultLevel(debug ? LLError::LEVEL_DEBUG : LLError::LEVEL_INFO);
        std::shared_ptr<LogCapture> capture = std::make_shared<LogCapture>();
        LLError::addRecorder(capture);
        auto step = [&](const std::string& name, bool rv)
        {
            out.lines.push_back("step " + name + " rv=" + (rv ? "1" : "0"));
            for (const std::string& l : capture->mLines) out.lines.push_back("  log" + scrub_log_line(l, dir));
            capture->mLines.clear();
        };
        bool rv = gInventory.loadSkeleton(lib_skel, LIB_OWNER);
        step("load_library", rv);
        rv = gInventory.loadSkeleton(agent_skel, AGENT);
        step("load_agent", rv);
        LLError::removeRecorder(capture);
        LLError::restoreSettings(saved);
        clear_journal();
        return out;
    }
}

namespace tut
{
    // R2 log parity: the lines of the login load (see the file comment).
    template<> template<> void invmodel_object::test<11>()
    {
        env().ran.insert(11);
        set_test_name("C2 logs of the login load");
        Cases cases;
        for (const LogCase& c : make_log_cases())
        {
            cases.push_back(run_log_case(c, true));
            cases.push_back(run_log_case(c, false));
        }
        corpus("cache/c2_logs.txt", "C2 log lines of the login load (level, tags, text)", "none", cases);
    }
}

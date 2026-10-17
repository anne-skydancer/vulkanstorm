"""Exercise actual utility retirement code and Boost subscription lifetime without a viewer."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def method_body(path, signature):
    source = (ROOT / path).read_text(encoding="utf-8-sig")
    start = source.index("{", source.index(signature))
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"Unbalanced method: {signature}")


class NativeUtilityLifecycleTest(unittest.TestCase):
    def test_subscription_destruction_and_account_retirement(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "A C++ compiler is required")
        header = (ROOT / "indra/newview/llfloatersettingscolor.h").read_text(encoding="utf-8-sig")
        source = (ROOT / "indra/newview/llfloatersettingscolor.cpp").read_text(encoding="utf-8-sig")
        member = re.search(r"boost::signals2::scoped_connection mHideDefaultsConnection;", header)
        self.assertIsNotNone(member, "The color-settings callback must have an owning connection")
        connect = re.search(
            r'mHideDefaultsConnection = gSavedSettings.getControl\("ColorSettingsHideDefault"\)'
            r'->getCommitSignal\(\)->connect\([\s\S]*?;', source)
        self.assertIsNotNone(connect)
        stream = method_body("indra/newview/fsfloaterstreamtitle.cpp", "void FSStreamTitleManager::resetAccount()")
        mention = method_body("indra/newview/llfloaterchatmentionpicker.cpp", "void LLFloaterChatMentionPicker::resetAccount()")
        fixture = r'''#include <cassert>
#include <string>
#include <vector>
#include <boost/signals2.hpp>
#include <boost/bind/bind.hpp>
struct Setting {
    boost::signals2::signal<void()> changed;
    auto* getCommitSignal() { return &changed; }
};
struct Settings {
    Setting setting;
    Setting* getControl(const char*) { return &setting; }
} gSavedSettings;
int updates = 0;
struct LLFloaterSettingsColor {
    MEMBER
    void updateList(bool) { ++updates; }
    void postBuild() { CONNECT }
};
struct FSStreamTitleManager {
    std::string mCurrentStreamTitle;
    std::vector<std::string> mStreamTitleHistory;
    boost::signals2::signal<void(const std::vector<std::string>&)> mHistoryUpdateSignal;
    boost::signals2::signal<void(std::string_view)> mUpdateSignal;
    void resetAccount() STREAM
};
struct UUID {
    bool null = false;
    void setNull() { null = true; }
};
struct LLChatMentionHelper {
    inline static bool exists = false;
    inline static int instances = 0;
    int hidden = 0;
    static bool instanceExists() { return exists; }
    static LLChatMentionHelper& instance() {
        ++instances;
        static LLChatMentionHelper helper;
        return helper;
    }
    void hideHelper() { ++hidden; }
};
struct LLFloaterChatMentionPicker {
    inline static UUID sSessionID;
    inline static void* sParticipantSource = reinterpret_cast<void*>(1);
    static void resetAccount() MENTION
};
int main() {
    // A surviving settings service must not call a destroyed or previous window.
    {
        LLFloaterSettingsColor color;
        color.postBuild();
        gSavedSettings.setting.changed();
        assert(updates == 1);
    }
    gSavedSettings.setting.changed();
    assert(updates == 1 && gSavedSettings.setting.changed.num_slots() == 0);
    {
        LLFloaterSettingsColor color;
        color.postBuild();
        gSavedSettings.setting.changed();
        assert(updates == 2);
    }
    gSavedSettings.setting.changed();
    assert(updates == 2 && gSavedSettings.setting.changed.num_slots() == 0);

    FSStreamTitleManager manager;
    manager.mCurrentStreamTitle = "Previous account";
    manager.mStreamTitleHistory = {"Previous account's stream"};
    int history_updates = 0, title_updates = 0;
    manager.mHistoryUpdateSignal.connect([&](const auto& values) {
        assert(values.empty() && manager.mCurrentStreamTitle.empty());
        ++history_updates;
    });
    manager.mUpdateSignal.connect([&](auto value) {
        assert(value.empty() && manager.mStreamTitleHistory.empty());
        ++title_updates;
    });
    manager.resetAccount();
    assert(history_updates == 1 && title_updates == 1);

    // Shutdown without a constructed helper cannot initialize new UI services.
    LLFloaterChatMentionPicker::resetAccount();
    assert(LLFloaterChatMentionPicker::sSessionID.null);
    assert(!LLFloaterChatMentionPicker::sParticipantSource);
    assert(LLChatMentionHelper::instances == 0);
    LLChatMentionHelper::exists = true;
    LLFloaterChatMentionPicker::resetAccount();
    assert(LLChatMentionHelper::instance().hidden == 1);
}
'''
        fixture = fixture.replace("MEMBER", member[0]).replace("CONNECT", connect[0])
        fixture = fixture.replace("STREAM", stream).replace("MENTION", mention)
        # Use the viewer's installed Boost when present, otherwise the compiler's
        # normal system headers. This test does not download or substitute Boost.
        candidates = sorted(ROOT.glob("build*/packages/include"))
        if os.environ.get("BOOST_ROOT"):
            candidates.extend([Path(os.environ["BOOST_ROOT"]), Path(os.environ["BOOST_ROOT"]) / "include"])
        include_flags = []
        for candidate in candidates:
            if (candidate / "boost/signals2.hpp").is_file():
                include_flags = ["-I", str(candidate)]
                break
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "utility_lifecycle.cpp"
            executable = Path(directory) / "utility_lifecycle.exe"
            path.write_text(fixture, encoding="utf-8")
            built = subprocess.run(
                [compiler, "-std=c++17", *include_flags, str(path), "-o", str(executable)],
                capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

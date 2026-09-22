"""Run the production stream-start/idle/stop methods with controlled startup state."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class LoginMusicGateTest(unittest.TestCase):
    def test_deferred_playback_and_cancellation(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if not compiler:
            self.skipTest("requires clang++ or a GCC-compatible compiler in CXX")
        root = Path(__file__).resolve().parents[2]
        source = (root / "indra/newview/llvieweraudio.cpp").read_text(encoding="utf-8")
        methods = []
        for name in ("startInternetStreamWithAutoFade", "onIdleUpdate",
                     "stopInternetStreamWithAutoFade"):
            match = re.search(r"^(?:void|bool) LLViewerAudio::" + name +
                              r"\([^\n]*\)\n\{.*?^\}", source, re.M | re.S)
            self.assertIsNotNone(match, name)
            methods.append(match.group())
        harness = r'''
#include <cassert>
#include <sstream>
#include <string>
#include <vector>
#define LL_DEBUGS(...) std::ostringstream()
#define LL_WARNS(...) std::ostringstream()
#define LL_ENDL ""
constexpr int STATE_STARTED = 10;
constexpr int FMODEX_STREAM_BUFFER_SIZE = 7000, FMODEX_DECODE_BUFFER_SIZE = 1000;
struct LLStringUtil { inline static const std::string null; };
struct LLStartUp { inline static int state = 0; static int getStartupState() { return state; } };
struct Window { bool progress = true; bool getShowProgress() { return progress; } } window;
Window* gViewerWindow = &window;
struct Settings { bool fade = true; bool getBOOL(const char*) { return fade; } } gSavedSettings;
std::vector<std::string> events;
void audio_update_volume(bool) { events.push_back("volume"); }
struct LLStreamingAudioInterface {
    enum class AudioFadeResult { Pending, Failed, Cancelled };
    AudioFadeResult getAudioFadeResult() { return AudioFadeResult::Pending; }
    bool supportsAdjustableBufferSizes() { return false; }
    void setBufferSizes(int, int) {}
    bool hasAudioFade() { return true; }
};
struct Engine {
    LLStreamingAudioInterface stream;
    std::string url;
    void startInternetStream(const std::string& s) { url = s; if (!s.empty()) events.push_back("play:" + s); }
    void stopInternetStream() { url.clear(); }
    const std::string& getInternetStreamURL() { return url; }
    LLStreamingAudioInterface* getStreamingAudioImpl() { return &stream; }
} engine;
Engine* gAudiop = &engine;
struct Timer { void reset() {} void setTimerExpirySec(float) {} };
struct LLViewerAudio {
    enum EFadeState { FADE_IDLE, FADE_IN, FADE_OUT };
    bool mDone = true, mBackendFade = false, mStreamStartDeferred = false;
    bool listening = false;
    float mFadeTime = 1;
    std::string mNextStreamURI;
    EFadeState mFadeState = FADE_IDLE;
    Timer stream_fade_timer;
    void registerIdleListener() { listening = true; }
    void deregisterIdleListener() { listening = false; }
    void startFading() { if (mDone) { events.push_back("fade"); mDone = false; mBackendFade = true; } }
    float getFadeVolume() { return 1; }
    void startInternetStreamWithAutoFade(const std::string&);
    void stopInternetStreamWithAutoFade();
    bool onIdleUpdate();
};
'''
        scenarios = r'''
int main() {
    for (bool fade : {false, true}) {
        gSavedSettings.fade = fade;
        engine.url.clear(); events.clear(); window.progress = true; LLStartUp::state = 0;
        LLViewerAudio audio;
        audio.startInternetStreamWithAutoFade("first");
        assert(events.empty() && audio.listening);
        assert(!audio.onIdleUpdate() && events.empty());
        audio.startInternetStreamWithAutoFade("latest");
        LLStartUp::state = STATE_STARTED;
        assert(!audio.onIdleUpdate() && events.empty()); // progress still covers world
        window.progress = false;
        audio.onIdleUpdate();
        assert(events.front() == "volume" && events.back() == "play:latest");
        assert(events.size() == (fade ? 3 : 2)); // fade begins only after release
        const auto count = events.size(); audio.onIdleUpdate(); assert(events.size() == count);

        for (bool empty_request : {false, true}) {
            engine.url.clear(); events.clear(); window.progress = true; LLStartUp::state = 0;
            LLViewerAudio cancelled;
            cancelled.startInternetStreamWithAutoFade("cancel-me");
            if (empty_request) cancelled.startInternetStreamWithAutoFade("");
            else cancelled.stopInternetStreamWithAutoFade();
            window.progress = false; LLStartUp::state = STATE_STARTED;
            cancelled.onIdleUpdate();
            assert(engine.url.empty());
            for (const auto& event : events) assert(event.find("play:") != 0);
        }
        events.clear(); engine.url.clear();
        LLViewerAudio normal;
        normal.startInternetStreamWithAutoFade("after-login");
        assert(engine.url == "after-login"); // no delay once ready
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / "login_music.cpp"
            binary = Path(temporary) / "login_music.exe"
            cpp.write_text(harness + "\n".join(methods) + scenarios, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()

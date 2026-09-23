"""Run the production stream-start/idle/stop methods with controlled startup state."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class LoginMusicGateTest(unittest.TestCase):
    def test_early_buffering_and_sample_timed_login_fade(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if not compiler:
            self.skipTest("requires clang++ or a GCC-compatible compiler in CXX")
        root = Path(__file__).resolve().parents[2]
        source = (root / "indra/newview/llvieweraudio.cpp").read_text(encoding="utf-8")
        methods = []
        for name in ("startInternetStreamWithAutoFade", "onIdleUpdate",
                     "stopInternetStreamWithAutoFade", "startFading", "getFadeVolume"):
            match = re.search(r"^(?:void|bool|F32) LLViewerAudio::" + name +
                              r"\([^\n]*\)\n\{.*?^\}", source, re.M | re.S)
            self.assertIsNotNone(match, name)
            methods.append(match.group())
        harness = r'''
#include <cassert>
#include <cmath>
#include <algorithm>
using F32 = float;
template<class T> T llmax(T a,T b) { return std::max(a,b); }
template<class T> T llclamp(T v,T a,T b) { return std::clamp(v,a,b); }
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
struct Settings { bool fade = true; bool getBOOL(const char*) { return fade; } float getF32(const char* name) { return std::string(name) == "FSAudioMusicFadeIn" ? 3.f : 2.f; } } gSavedSettings;
std::vector<std::string> events;
void audio_update_volume(bool) { events.push_back("volume"); }
struct LLStreamingAudioInterface {
    enum class AudioFadeResult { Pending, Complete, Failed, Cancelled };
    AudioFadeResult result = AudioFadeResult::Pending;
    float target = 1, duration = -1;
    bool capable = true;
    int commands = 0;
    bool beginAudioFade(float t, float d) {
        if (!capable) return false;
        target = t; duration = d; ++commands; result = AudioFadeResult::Pending;
        return true;
    }
    AudioFadeResult getAudioFadeResult() { return result; }
    bool isAudioFadeComplete() { return result == AudioFadeResult::Complete; }
    bool supportsAdjustableBufferSizes() { return false; }
    void setBufferSizes(int, int) {}
    bool hasAudioFade() { return capable; }
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
struct Timer {
    float elapsed = 0, expiry = 1;
    void reset() { elapsed = 0; }
    void setTimerExpirySec(float value) { expiry = value; }
    bool hasExpired() { return elapsed >= expiry; }
    float getElapsedTimeF32() { return elapsed; }
};
struct LLViewerAudio {
    enum EFadeState { FADE_IDLE, FADE_IN, FADE_OUT };
    bool mDone = true, mBackendFade = false, mLoginFadePending = false;
    bool listening = false;
    float mFadeTime = 1;
    std::string mNextStreamURI;
    EFadeState mFadeState = FADE_IDLE;
    Timer stream_fade_timer;
    void registerIdleListener() { listening = true; }
    void deregisterIdleListener() { listening = false; }
    inline static LLViewerAudio* instance = nullptr;
    LLViewerAudio() { instance = this; }
    static LLViewerAudio* getInstance() { return instance; }
    EFadeState getFadeState() { return mFadeState; }
    void startFading();
    float getFadeVolume();
    void startInternetStreamWithAutoFade(const std::string&);
    void stopInternetStreamWithAutoFade();
    bool onIdleUpdate();
};
'''
        scenarios = r'''
int main() {
    for (bool capable : {true, false}) {
        engine = Engine{}; engine.stream.capable = capable; events.clear();
        LLStartUp::state = 0; window.progress = true; gSavedSettings.fade = true;
        LLViewerAudio audio;
        audio.startInternetStreamWithAutoFade("first");
        assert(engine.url == "first"); // connects before login completes
        assert(audio.listening && audio.mLoginFadePending);
        assert(audio.getFadeVolume() == 0.f);
        if (capable) {
            assert(engine.stream.target == 0.f && engine.stream.duration == 0.f);
            engine.stream.result = LLStreamingAudioInterface::AudioFadeResult::Complete;
        }
        audio.stream_fade_timer.elapsed = 100; // a long login must not consume the fade
        assert(!audio.onIdleUpdate() && !audio.mDone);
        audio.startInternetStreamWithAutoFade("latest");
        assert(engine.url == "latest"); // buffers the latest URL while held
        const auto events_before = events.size();
        const auto commands_before = engine.stream.commands;
        LLStartUp::state = STATE_STARTED;
        audio.onIdleUpdate(); // progress is STILL VISIBLE
        assert(!audio.mLoginFadePending && !audio.mDone);
        assert(events.size() == events_before); // no second connection/buffering wait
        if (capable) {
            assert(audio.mBackendFade);
            assert(engine.stream.target == 1.f && engine.stream.duration == 3.f);
            assert(engine.stream.commands == commands_before + 1);
            audio.stream_fade_timer.elapsed = 100;
            assert(audio.getFadeVolume() == 1.f && !audio.mDone); // backend owns completion
            audio.onIdleUpdate();
            assert(engine.stream.commands == commands_before + 1);
            engine.stream.result = LLStreamingAudioInterface::AudioFadeResult::Complete;
        } else {
            assert(!audio.mBackendFade);
            audio.stream_fade_timer.elapsed = 3;
        }
        assert(audio.onIdleUpdate() && audio.mFadeState == LLViewerAudio::FADE_IDLE);
    }
    for (bool empty_request : {false, true}) {
        engine = Engine{}; LLStartUp::state = 0;
        LLViewerAudio audio;
        audio.startInternetStreamWithAutoFade("cancel-me");
        if (empty_request) audio.startInternetStreamWithAutoFade("");
        else audio.stopInternetStreamWithAutoFade();
        assert(!audio.mLoginFadePending);
        engine.stream.result = LLStreamingAudioInterface::AudioFadeResult::Complete;
        LLStartUp::state = STATE_STARTED; audio.onIdleUpdate();
        assert(engine.url.empty());
    }
    engine = Engine{}; LLStartUp::state = 0;
    LLViewerAudio failed;
    failed.startInternetStreamWithAutoFade("failure");
    engine.stream.result = LLStreamingAudioInterface::AudioFadeResult::Failed;
    assert(failed.onIdleUpdate() && !failed.mLoginFadePending && engine.url.empty());

    engine = Engine{}; LLStartUp::state = STATE_STARTED;
    LLViewerAudio normal;
    normal.startInternetStreamWithAutoFade("normal");
    assert(engine.stream.target == 1 && engine.stream.duration == 3 && normal.mBackendFade);
    normal.stopInternetStreamWithAutoFade();
    assert(engine.stream.target == 0 && engine.stream.duration == 2 && normal.mBackendFade);

    engine = Engine{}; LLStartUp::state = 0; gSavedSettings.fade = false;
    LLViewerAudio no_fade;
    no_fade.startInternetStreamWithAutoFade("no-fade");
    assert(engine.url == "no-fade" && engine.stream.commands == 0);
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

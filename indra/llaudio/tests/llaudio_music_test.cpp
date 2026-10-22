/**
 * @file llaudio_music_test.cpp
 * @brief INTEGRATION_TEST_llaudio_music: parcel music end to end through the
 *        viewer's streaming slot (LLStreamingAudio_VKS) on the Rust engine's
 *        null device, against the local radio server
 *        (indra/newview/tests/vksaudio_mock_stream_server.py, which runs this
 *        test and passes its port in LL_TEST_PORT).
 *
 * The test is the device: it renders the mixer 1024 frames at a time, sets the
 * device clock to what it has rendered less a fixed latency, and calls the
 * streaming interface's update() as the viewer's idle loop does. It waits for
 * decoded audio between blocks (as a device's period does), so the output is
 * the decoded stream without underruns and can be measured.
 *
 * Checked: every format decodes (MP3, MP2, Ogg Vorbis, AAC-LC, HE-AAC v1 and
 * v2, Opus, FLAC) with the expected tones on both channels; no rate change
 * (frames at the device rate equal ceil(source frames x device rate / source
 * rate) over the whole stream, the first second included); fades are
 * sample-timed (the envelope starts at exactly 0, rises linearly, and the fade
 * completes only once the device clock has passed its final sample); ICY
 * metadata over an "ICY 200 OK" status line and Ogg comments reach the
 * metadata signal; playlists play; a dropped live stream reconnects; video and
 * web pages are refused (state failed, a pending fade fails); the speaker fill
 * of stereo music at 5.1; a second parcel URL during the login hold keeps the
 * hold and fades in from zero afterwards, and a start while a fade-in runs
 * carries the fade instead of cancelling it.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llaudioengine_vks.h"
#include "llstreamingaudio_vks.h"
#include "lldir.h"
#include "llsd.h"
#include "vkstorm_ffi.h"

#include "lltut.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <vector>

struct LLVKSAudioTestAccess
{
    static bool init(LLAudioEngine_VKS& engine, unsigned int rate, unsigned int channels)
    {
        engine.LLAudioEngine::init(nullptr, "llaudio music");
        return engine.initNull(rate, channels);
    }
};

namespace
{
    const unsigned RATE = 48000;
    const unsigned BLOCK = 1024;
    // The device clock lags what was rendered by this much (a device buffer)
    const U64 LATENCY = 2048;

    std::string env(const char* name)
    {
        const char* v = getenv(name);
        return v ? v : "";
    }

    std::string base_url()
    {
        const std::string port = env("LL_TEST_PORT");
        return port.empty() ? std::string() : "http://127.0.0.1:" + port;
    }

    void take_string(void* user, const char* s, size_t len)
    {
        static_cast<std::string*>(user)->assign(s, len);
    }

    // Goertzel power of `freq` in one channel of interleaved frames.
    double tone_power(const std::vector<float>& pcm, unsigned channels, unsigned channel,
                      size_t first, size_t count, double freq)
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / RATE;
        const double coeff = 2.0 * cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = first; i < first + count; ++i)
        {
            const double s0 = pcm[i * channels + channel] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        return power / (count * count / 4.0);
    }

    double rms(const std::vector<float>& pcm, unsigned channels, unsigned channel, size_t first, size_t count)
    {
        double sum = 0.0;
        for (size_t i = first; i < first + count; ++i)
        {
            const double v = pcm[i * channels + channel];
            sum += v * v;
        }
        return count ? sqrt(sum / count) : 0.0;
    }

    struct Session
    {
        LLAudioEngine_VKS* engine = nullptr;
        LLStreamingAudio_VKS* stream = nullptr;
        VksAudio* audio = nullptr;
        unsigned channels = 2;
        std::vector<float> out;   // everything rendered
        U64 rendered = 0;
        std::vector<LLSD> metadata;
        boost::signals2::connection meta_connection;

        bool open(unsigned ch)
        {
            channels = ch;
            engine = new LLAudioEngine_VKS();
            gAudiop = engine;
            if (!LLVKSAudioTestAccess::init(*engine, RATE, ch))
            {
                return false;
            }
            audio = LLAudioEngine_VKS::getAudio();
            // The FFmpeg libraries are staged next to the test executable on
            // Windows and in the prebuilt library directory on Linux; the
            // streaming impl's own vks_music_create is then a no-op
            std::string ffmpeg = env("VKS_FFMPEG_DIR");
            if (ffmpeg.empty())
            {
                ffmpeg = gDirUtilp->getExecutableDir();
            }
            vks_music_create(audio, nullptr, "Vulkanstorm-test", ffmpeg.c_str());
            stream = (LLStreamingAudio_VKS*)engine->createDefaultStreamingAudioImpl();
            engine->setStreamingAudioImpl(stream);
            meta_connection = stream->setMetadataUpdateCallback([this](const LLSD& md) { metadata.push_back(md); });
            stream->setGain(1.f);
            return true;
        }

        void close()
        {
            meta_connection.disconnect();
            if (engine)
            {
                engine->shutdown();
                delete engine;
            }
            engine = nullptr;
            stream = nullptr;
            gAudiop = nullptr;
        }

        ~Session() { close(); }

        S32 state(std::string* reason = nullptr)
        {
            std::string r;
            const S32 s = vks_music_state(audio, take_string, &r);
            if (reason) *reason = r;
            return s;
        }

        VksMusicStats stats(std::string* codec = nullptr)
        {
            VksMusicStats st{};
            std::string c;
            vks_music_stats(audio, &st, take_string, &c);
            if (codec) *codec = c;
            return st;
        }

        // Renders one block, first waiting for a block of decoded audio while
        // the decoder is still making progress (as a device period would
        // pass), so the output carries no underruns. Waiting ends when a
        // block is buffered, the stream is over, or nothing new has been
        // decoded for 500 ms (the end of a finite stream short of a block).
        void step(bool wait_for_audio = true)
        {
            if (wait_for_audio)
            {
                U64 last_pushed = stats().pushed_frames;
                auto last_progress = std::chrono::steady_clock::now();
                for (;;)
                {
                    const S32 s = state();
                    if (s == 0 || s == 4 || s == 5 || s == 6 || s == 7)
                    {
                        break;
                    }
                    const VksMusicStats st = stats();
                    if (st.buffered_frames >= BLOCK)
                    {
                        break;
                    }
                    const auto now = std::chrono::steady_clock::now();
                    if (st.pushed_frames != last_pushed)
                    {
                        last_pushed = st.pushed_frames;
                        last_progress = now;
                    }
                    else if (now - last_progress > std::chrono::milliseconds(st.pushed_frames ? 500 : 15000))
                    {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            const size_t at = out.size();
            out.resize(at + BLOCK * channels);
            vks_audio_render_null(audio, out.data() + at, BLOCK);
            rendered += BLOCK;
            vks_audio_set_null_clock(audio, rendered > LATENCY ? rendered - LATENCY : 0);
            stream->update();
            if (!env("VKS_MUSIC_TRACE").empty())
            {
                const VksMusicStats st = stats();
                LL_INFOS() << "rendered " << rendered << " state " << state() << " buffered " << st.buffered_frames
                           << " pushed " << st.pushed_frames << " pulled " << st.pulled_frames << LL_ENDL;
            }
        }

        // Plays `url` to its end (or `max_seconds` of output).
        void play_to_end(const std::string& url, double max_seconds)
        {
            stream->start(url);
            const U64 limit = (U64)(max_seconds * RATE);
            while (rendered < limit)
            {
                step();
                const S32 s = state();
                if (s == 5 || s == 6)
                {
                    // Let the last decoded frames play out
                    for (int i = 0; i < 8; ++i) step(false);
                    break;
                }
            }
        }

        // The first frame with audible output.
        size_t first_sound() const
        {
            for (size_t i = 0; i < out.size(); ++i)
            {
                if (out[i] != 0.f) return i / channels;
            }
            return out.size() / channels;
        }
    };

    void check_format(const std::string& base, const std::string& file, const char* codec_prefix,
                      double left_hz, double right_hz)
    {
        Session s;
        tut::ensure("engine on the null device", s.open(2));
        s.play_to_end(base + "/live/" + file + "?length=1", 30.0);
        std::string codec, reason;
        const S32 st = s.state(&reason);
        const VksMusicStats stats = s.stats(&codec);
        std::ostringstream what;
        what << file << " (" << codec << ", state " << st << " " << reason << ")";
        tut::ensure(what.str() + ": played to its end", st == 5);
        tut::ensure(what.str() + ": codec", codec.find(codec_prefix) == 0);
        // No rate change: every decoded segment produced exactly the frames
        // its rate conversion predicts, and the bus took them all
        tut::ensure(what.str() + ": decoded something", stats.source_frames > RATE / 2);
        tut::ensure_equals(what.str() + ": frames at the device rate", stats.device_frames, stats.expected_device_frames);
        tut::ensure_equals(what.str() + ": the bus took every decoded frame", stats.bus_input_frames, stats.pushed_frames);
        // The tones, over one second from the first sound
        const size_t first = s.first_sound();
        const size_t frames = s.out.size() / s.channels;
        tut::ensure(what.str() + ": a second of sound", frames > first + RATE);
        const size_t win = RATE / 2;
        const double l_on = tone_power(s.out, 2, 0, first + RATE / 4, win, left_hz);
        const double l_off = tone_power(s.out, 2, 0, first + RATE / 4, win, right_hz);
        const double r_on = tone_power(s.out, 2, 1, first + RATE / 4, win, right_hz);
        const double r_off = tone_power(s.out, 2, 1, first + RATE / 4, win, left_hz);
        what << " L " << l_on << "/" << l_off << " R " << r_on << "/" << r_off;
        tut::ensure(what.str() + ": left tone", l_on > 100.0 * l_off && l_on > 1e-3);
        if (right_hz > 0.0)
        {
            tut::ensure(what.str() + ": right tone", r_on > 10.0 * r_off && r_on > 1e-4);
        }
    }
}

namespace tut
{
    struct music_data
    {
        std::string url;
        music_data() : url(base_url()) {}
    };
    typedef test_group<music_data> music_group;
    typedef music_group::object music_object;
    music_group music_test_group("llaudio_music");

    template<> template<>
    void music_object::test<1>()
    {
        set_test_name("every format decodes with no rate change");
        ensure("LL_TEST_PORT (run under vksaudio_mock_stream_server.py)", !url.empty());
        // tone.* fixtures: 440 Hz left at 0.5, 660 Hz right at 0.25
        check_format(url, "tone.mp3", "mp3", 440.0, 660.0);
        check_format(url, "tone.mp2", "mp", 440.0, 660.0);
        check_format(url, "tone.ogg", "vorbis", 440.0, 660.0);
        check_format(url, "tone_lc.aac", "aac", 440.0, 660.0);
        // HE-AAC v1: 9 kHz on the right, above the core's band (SBR only);
        // v2: parametric stereo from a mono core
        check_format(url, "tone_he1.aac", "he-aac", 440.0, 9000.0);
        check_format(url, "tone_he2.aac", "he-aac-v2", 440.0, 0.0);
        check_format(url, "tone.opus", "opus", 440.0, 660.0);
        check_format(url, "tone.flac", "flac", 440.0, 660.0);
    }

    template<> template<>
    void music_object::test<2>()
    {
        set_test_name("sample-timed fade in and out, completion on the device clock");
        ensure("LL_TEST_PORT", !url.empty());
        Session s;
        ensure("engine", s.open(2));
        // As llvieweraudio does: the fade-in is requested before the stream
        // starts and attaches to it; the envelope starts at exactly zero
        const F32 FADE_IN = 1.0f;
        ensure("fade accepted before the start", s.stream->beginAudioFade(1.f, FADE_IN));
        ensure("pending before the start",
               s.stream->getAudioFadeResult() == LLStreamingAudioInterface::AudioFadeResult::Pending);
        s.stream->start(url + "/live/tone.ogg?loops=4&length=1");
        bool complete = false;
        U64 complete_clock = 0, pending_clock = 0;
        while (s.rendered < 20 * RATE && !complete)
        {
            s.step();
            const auto r = s.stream->getAudioFadeResult();
            ensure("no failure while fading in", r != LLStreamingAudioInterface::AudioFadeResult::Failed);
            if (r == LLStreamingAudioInterface::AudioFadeResult::Complete)
            {
                complete = true;
                complete_clock = s.rendered - LATENCY;
            }
            else
            {
                pending_clock = s.rendered - LATENCY;
            }
        }
        ensure("fade-in completed", complete);
        const size_t first = s.first_sound();
        // The envelope reaches 1 at the frame FADE_IN x rate after the first
        // decoded frame (whose own gain is 0): the fence
        const U64 fence = first + (U64)(FADE_IN * RATE) - 1;
        std::ostringstream w;
        w << "first sound " << first << " fence " << fence << " clock at completion " << complete_clock
          << " last pending clock " << pending_clock;
        ensure(w.str() + ": not complete before the clock passed the final sample", complete_clock >= fence);
        ensure(w.str() + ": complete as soon as it did", pending_clock < fence + 2);
        // Linear rise: the left channel's level in 50 ms windows follows the
        // envelope (the tone is 0.5 at full gain)
        for (int k = 1; k < 19; ++k)
        {
            const size_t at = first + k * RATE / 20;
            const double level = rms(s.out, 2, 0, at, RATE / 100) / (0.5 / sqrt(2.0));
            const double expect = (k * RATE / 20 + RATE / 200) / (double)(FADE_IN * RATE);
            std::ostringstream m;
            m << "envelope at " << k * 50 << " ms: " << level << " vs " << expect;
            ensure(m.str(), fabs(level - expect) < 0.03);
        }
        ensure("starts at exactly zero", first > 0 && s.out[first * 2 - 2] == 0.f && s.out[first * 2 - 1] == 0.f);

        // Fade out: complete only after the final faded sample played, then silence
        const F32 FADE_OUT = 0.5f;
        ensure("fade-out accepted", s.stream->beginAudioFade(0.f, FADE_OUT));
        const size_t out_start = s.out.size() / 2;
        complete = false;
        while (s.rendered < 40 * RATE && !complete)
        {
            s.step();
            complete = s.stream->getAudioFadeResult() == LLStreamingAudioInterface::AudioFadeResult::Complete;
            if (complete) complete_clock = s.rendered - LATENCY;
        }
        ensure("fade-out completed", complete);
        ensure("fade-out not complete early", complete_clock >= out_start + (U64)(FADE_OUT * RATE) - BLOCK);
        const size_t tail = s.out.size() / 2 - BLOCK;
        ensure("silent after the fade-out", rms(s.out, 2, 0, tail, BLOCK) == 0.0);
        s.stream->stop();
        ensure_equals("stopped", s.stream->isPlaying(), 0);
    }

    template<> template<>
    void music_object::test<3>()
    {
        set_test_name("ICY 200 OK with interleaved titles, Ogg comments, playlists");
        ensure("LL_TEST_PORT", !url.empty());
        {
            Session s;
            ensure("engine", s.open(2));
            s.play_to_end(url + "/live/tone.mp3?status=icy&icy=1&metaint=2000&every=2&repeat=1&length=1&title=Icy%20Artist%20-%20Icy%20Title", 30.0);
            ensure_equals("played", s.state(), 5);
            bool seen = false;
            for (const LLSD& md : s.metadata)
            {
                if (md.has("TITLE") && md["TITLE"].asString() == "Icy Title" && md["ARTIST"].asString() == "Icy Artist")
                    seen = true;
            }
            ensure("ICY artist and title reached the metadata signal", seen);
            s.stream->stop();
            ensure("metadata cleared on stop", s.metadata.back().size() == 0);
        }
        {
            Session s;
            ensure("engine", s.open(2));
            s.play_to_end(url + "/live/chain.ogg?length=1", 30.0);
            int titles = 0;
            for (const LLSD& md : s.metadata)
            {
                const std::string t = md.has("TITLE") ? md["TITLE"].asString() : "";
                if (t == "First Title" || t == "Second Title" || t == "Third Title") ++titles;
            }
            ensure_equals("each chained link's comments", titles, 3);
            std::string codec;
            const VksMusicStats st = s.stats(&codec);
            ensure_equals("chained links at the device rate", st.device_frames, st.expected_device_frames);
        }
        {
            Session s;
            ensure("engine", s.open(2));
            s.play_to_end(url + "/playlist.pls?e=/live/tone.mp3%3Flength%3D1", 30.0);
            ensure_equals("PLS entry played", s.state(), 5);
        }
        {
            Session s;
            ensure("engine", s.open(2));
            s.play_to_end(url + "/playlist.m3u?e=/live/tone.flac%3Flength%3D1", 30.0);
            ensure_equals("M3U entry played", s.state(), 5);
        }
    }

    template<> template<>
    void music_object::test<4>()
    {
        set_test_name("video and web pages are refused, a dropped stream reconnects");
        ensure("LL_TEST_PORT", !url.empty());
        for (const char* path : {"/media/video.mp4", "/media/video.ogv", "/media/page.html"})
        {
            Session s;
            ensure("engine", s.open(2));
            ensure("fade", s.stream->beginAudioFade(1.f, 3.f));
            s.stream->start(url + path);
            std::string reason;
            for (int i = 0; i < 400 && s.state(&reason) != 6; ++i) s.step();
            ensure(std::string(path) + " refused", s.state(&reason) == 6);
            ensure(std::string(path) + " not playing", s.stream->isPlaying() == 0);
            ensure(std::string(path) + " fade failed",
                   s.stream->getAudioFadeResult() == LLStreamingAudioInterface::AudioFadeResult::Failed);
            ensure(std::string(path) + " silent", s.first_sound() == s.out.size() / 2);
        }
        {
            Session s;
            ensure("engine", s.open(2));
            s.play_to_end(url + "/live/tone.mp3?loops=2&drop=80000&key=reconnect&lengthafter=1", 60.0);
            const VksMusicStats st = s.stats();
            ensure("reconnected", st.reconnects >= 1);
            ensure_equals("played to the end after the reconnect", s.state(), 5);
        }
    }

    template<> template<>
    void music_object::test<5>()
    {
        set_test_name("speaker fill of stereo music at 5.1");
        ensure("LL_TEST_PORT", !url.empty());
        Session s;
        ensure("engine at 6 channels", s.open(6));
        s.stream->setMusicSpatialLayout(51);
        s.play_to_end(url + "/live/tone.ogg?length=1", 30.0);
        const size_t first = s.first_sound();
        const size_t at = first + RATE / 4, n = RATE / 2;
        // 5.1 order: FL FR FC LFE BL BR
        const double fl = rms(s.out, 6, 0, at, n), fr = rms(s.out, 6, 1, at, n);
        const double fc = rms(s.out, 6, 2, at, n), lfe = rms(s.out, 6, 3, at, n);
        const double bl = rms(s.out, 6, 4, at, n), br = rms(s.out, 6, 5, at, n);
        std::ostringstream w;
        w << "FL " << fl << " FR " << fr << " FC " << fc << " LFE " << lfe << " BL " << bl << " BR " << br;
        ensure(w.str() + ": mains at 0.7071", fabs(fl - 0.70710678 * 0.5 / sqrt(2.0)) < 0.01 && fabs(bl - fl) < 1e-6 && fabs(br - fr) < 1e-6);
        ensure(w.str() + ": LFE silent", lfe == 0.0);
        ensure(w.str() + ": centre", fc > 0.05);
    }

    template<> template<>
    void music_object::test<6>()
    {
        set_test_name("a replacement stream keeps the login hold, and no start cancels a live transition");
        ensure("LL_TEST_PORT", !url.empty());
        Session s;
        ensure("engine", s.open(2));
        typedef LLStreamingAudioInterface::AudioFadeResult Result;
        auto ok = [&](const char* when)
        {
            const Result r = s.stream->getAudioFadeResult();
            ensure(std::string(when) + ": not cancelled or failed", r == Result::Pending || r == Result::Complete);
        };
        // As LLViewerAudio::startInternetStreamWithAutoFade does before
        // STATE_STARTED: startFading() holds at zero, then the stream starts
        ensure("hold before the first stream", s.stream->beginAudioFade(0.f, 0.f));
        s.stream->start(url + "/live/tone.ogg?loops=8&length=1&key=first");
        // Until a second of it has been mixed (the player buffers 3 s first)
        for (int i = 0; i < 4000 && s.stats().pulled_frames < RATE; ++i)
        {
            s.step();
            ok("first stream held");
        }
        ensure("the first stream plays", s.stats().pulled_frames >= RATE);
        ensure("held at zero", s.first_sound() == s.out.size() / 2);

        // Another parcel URL during the login hold: startFading() again (the
        // old stream still has a URL, so this is a live transition), then the
        // replacement starts
        ensure("hold before the replacement", s.stream->beginAudioFade(0.f, 0.f));
        s.stream->start(url + "/live/tone.mp3?loops=8&length=1&key=second");
        ensure_equals("replacement URL", s.stream->getURL(), url + "/live/tone.mp3?loops=8&length=1&key=second");
        const U64 pulled_before = s.stats().pulled_frames;
        for (int i = 0; i < 4000 && s.stats().pulled_frames < pulled_before + RATE; ++i)
        {
            s.step();
            ok("replacement during the login hold");
        }
        ensure("the replacement decodes", s.stats().pulled_frames >= pulled_before + RATE);
        ensure("the replacement is held at zero", s.first_sound() == s.out.size() / 2);
        ensure_equals("the replacement is playing", s.stream->isPlaying(), 1);

        // STATE_STARTED: the real fade-in of the replacement, from zero
        const size_t start_frame = s.out.size() / 2;
        ensure("fade-in accepted", s.stream->beginAudioFade(1.f, 0.5f));
        bool complete = false;
        const auto fade_give_up = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!complete && std::chrono::steady_clock::now() < fade_give_up)
        {
            s.step();
            ok("fade-in of the replacement");
            complete = s.stream->getAudioFadeResult() == Result::Complete;
        }
        ensure("the replacement faded in", complete);
        const size_t first = s.first_sound();
        ensure("the fade-in starts from zero", first >= start_frame && first < start_frame + 64);
        const double early = rms(s.out, 2, 0, first, RATE / 100);
        const double full = rms(s.out, 2, 0, s.out.size() / 2 - RATE / 10, RATE / 20);
        ensure("the fade-in rises", early < 0.1 * full && full > 0.3);

        // General case: a start while a fade-in is still running carries it
        ensure("second fade-in", s.stream->beginAudioFade(0.f, 0.f));
        s.step();
        ensure("fade-in accepted", s.stream->beginAudioFade(1.f, 1.0f));
        s.step();
        ensure("still fading in", s.stream->getAudioFadeResult() == Result::Pending);
        s.stream->start(url + "/live/tone.flac?loops=8&length=1&key=third");
        complete = false;
        // Bounded by wall time, not by blocks: the new stream buffers 3 s
        // before it plays (silent blocks meanwhile), however fast the test
        // renders them
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!complete && std::chrono::steady_clock::now() < give_up)
        {
            s.step();
            ok("start during a fade-in");
            complete = s.stream->getAudioFadeResult() == Result::Complete;
        }
        ensure("the carried fade-in completed", complete);
        s.stream->stop();
    }
}

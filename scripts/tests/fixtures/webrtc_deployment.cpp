// Device-free fixture. Production methods are inserted by the test launcher.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
using namespace std::chrono_literals;

struct TaskQueue
{
    std::deque<std::function<void()>> tasks;
    int delayed = 0;
    template<class F> void PostTask(F task) { tasks.emplace_back(task); }
    // Delays are not simulated: a delayed task runs in posting order.
    template<class F, class D> void PostDelayedTask(F task, D) { ++delayed; tasks.emplace_back(task); }
    void step() { auto task = std::move(tasks.front()); tasks.pop_front(); task(); }
};
namespace webrtc { struct TimeDelta { static int Millis(int value) { return value; } }; }
static const int AUDIO_DEVICE_RETRY_DELAY = webrtc::TimeDelta::Millis(100);
struct Device
{
    int initialized = 0, terminated = 0, stops = 0;
    // Initialized but not running: the macOS ADM mid-transition state.
    bool transitioning = false;
    bool Playing() const { return !transitioning; }
    bool Recording() const { return !transitioning; }
    bool RecordingIsInitialized() const { return true; }
    bool PlayoutIsInitialized() const { return true; }
    void StopPlayout() { ++stops; }
    void ForceStopRecording() { ++stops; }
    int Init() { ++initialized; return 0; }
    int ForceTerminate() { ++terminated; return 0; }
};
struct Connection
{
    int mute_resets = 0, receiver_updates = 0;
    void enableSenderTracks(bool) {}
    void enableReceiverTracks(bool) { ++receiver_updates; }
    void resetMute() { ++mute_resets; }
};
struct LogSink { void OnLogMessage(const std::string&) {} };
struct NullLog { template<class T> NullLog& operator<<(const T&) { return *this; } };
#define RTC_LOG(severity) NullLog{}
// Deterministic timed-lock outcomes exercise std::unique_lock's actual failure
// and exception paths without sleeping or relying on OS thread scheduling.
struct DeviceMutex
{
    int timeouts = 0;
    bool throw_next = false;
    template<class Rep, class Period>
    bool try_lock_for(std::chrono::duration<Rep, Period>)
    {
        if (throw_next)
        {
            throw_next = false;
            throw std::system_error(std::make_error_code(std::errc::operation_not_permitted));
        }
        if (timeouts) { --timeouts; return false; }
        return true;
    }
    void unlock() {}
} gAudioDeviceMutex;
// llwebrtc locks through this pointer (the viewer injects its mutex).
DeviceMutex* sAudioDeviceMutex = &gAudioDeviceMutex;
bool gWebRTCUpdateDevices = false;

struct LLWebRTCImpl
{
    TaskQueue worker, signaling;
    TaskQueue* mWorkerThread = &worker;
    TaskQueue* mSignalingThread = &signaling;
    Device module;
    Device* mDeviceModule = &module;
    LogSink log;
    LogSink* mLogSink = &log;
    bool mVoiceEnabled = false, mTuningMode = false;
    std::atomic<int> mDevicesDeploying{0};
    std::atomic<bool> mDevicesDeployingNeedsReset{false};
    int mDeployBackoffs = 0;
    std::shared_ptr<std::atomic<bool>> mDeployRetriesAlive = std::make_shared<std::atomic<bool>>(true);
    std::vector<std::unique_ptr<Connection>> mPeerConnections;
    int recordings = 0, playouts = 0;
    bool fail_recording = false;
    void workerStartRecording()
    {
        if (fail_recording)
        {
            fail_recording = false;
            throw std::runtime_error("Device operation failed");
        }
        ++recordings;
    }
    void workerStartPlayout() { ++playouts; }
    void setVoiceEnabled(bool);
    void deployDevices(bool);
    void workerDeployDevices(bool);
    void drain()
    {
        unsigned budget = 100;
        while (!worker.tasks.empty() || !signaling.tasks.empty())
        {
            assert(budget-- > 0);
            if (!worker.tasks.empty()) worker.step();
            if (!signaling.tasks.empty()) signaling.step();
            assert(mDevicesDeploying >= 0);
        }
        assert(mDevicesDeploying == 0);
    }
};

// PRODUCTION_METHODS

int main()
{
    LLWebRTCImpl voice;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        voice.setVoiceEnabled(true);
        voice.drain();
        assert(voice.recordings == cycle + 1);
        voice.setVoiceEnabled(false);
        voice.drain();
    }

    LLWebRTCImpl pending;
    pending.setVoiceEnabled(true);
    pending.worker.step(); // Activation overlaps a device-change/reset request.
    assert(pending.mDevicesDeploying == 1);
    pending.deployDevices(true);
    pending.deployDevices(false);
    pending.drain();
    assert(pending.module.terminated == 1);
    assert(pending.recordings == 3 && pending.playouts == 3);
    assert(!pending.mDevicesDeployingNeedsReset);

    LLWebRTCImpl absent;
    absent.mDeviceModule = nullptr;
    absent.setVoiceEnabled(true);
    absent.drain();
    assert(absent.recordings == 0);

    LLWebRTCImpl vanished;
    vanished.deployDevices(false);
    vanished.mDeviceModule = nullptr; // Disappears after scheduling the attempt.
    vanished.drain();
    vanished.mDeviceModule = &vanished.module;
    vanished.deployDevices(false);
    vanished.drain();
    assert(vanished.recordings == 1);

    // A busy shared device mutex retries the same request after a delay and
    // applies it, instead of dropping the device change until the next one.
    LLWebRTCImpl busy;
    busy.mPeerConnections.emplace_back(std::make_unique<Connection>());
    gAudioDeviceMutex.timeouts = 3;
    busy.deployDevices(true);
    busy.drain();
    assert(busy.worker.delayed == 3);
    assert(busy.module.terminated == 1 && busy.recordings == 1 && busy.playouts == 1);
    assert(!busy.mDevicesDeployingNeedsReset);
    assert(busy.mPeerConnections[0]->mute_resets == 1);
    assert(busy.mPeerConnections[0]->receiver_updates == 1);

    // Shutdown while a busy retry is pending: the retry runs after terminate()
    // and must not deploy or post completion (the threads have moved away).
    LLWebRTCImpl closing;
    gAudioDeviceMutex.timeouts = 1;
    closing.deployDevices(false);
    closing.worker.step();
    assert(closing.worker.delayed == 1 && closing.worker.tasks.size() == 1);
    closing.mDeployRetriesAlive->store(false); // what terminate() does first
    closing.mSignalingThread = nullptr;        // moved to the shutdown thread
    closing.worker.step();
    assert(closing.worker.tasks.empty() && closing.recordings == 0);

    for (int failure = 1; failure < 3; ++failure)
    {
        LLWebRTCImpl recover;
        recover.mPeerConnections.emplace_back(std::make_unique<Connection>());
        if (failure == 1) gAudioDeviceMutex.throw_next = true;
        if (failure == 2) recover.fail_recording = true;
        recover.deployDevices(true);
        recover.drain(); // A failed attempt must not strand the count.
        assert(recover.mDevicesDeployingNeedsReset);
        assert(recover.mPeerConnections[0]->mute_resets == 0);
        assert(recover.mPeerConnections[0]->receiver_updates == 0);
        const int terminations = recover.module.terminated;
        recover.deployDevices(false);
        recover.drain();
        assert(recover.module.terminated == terminations + 1);
        assert(recover.recordings == 1 && !recover.mDevicesDeployingNeedsReset);
        assert(recover.mPeerConnections[0]->mute_resets == 1);
        assert(recover.mPeerConnections[0]->receiver_updates == 1);
    }

    LLWebRTCImpl queued;
    gAudioDeviceMutex.timeouts = 1;
    queued.deployDevices(true);
    queued.deployDevices(false);
    queued.drain(); // The busy reset retries itself, then the queued request runs.
    assert(queued.worker.delayed == 1);
    assert(queued.module.terminated == 1 && queued.recordings == 2);
    assert(!queued.mDevicesDeployingNeedsReset);

#if WEBRTC_MAC
    // Mid-transition: back off without retiring the request, then deploy once
    // the module settles. The retry keeps a reset that arrived meanwhile.
    LLWebRTCImpl settling;
    settling.mPeerConnections.emplace_back(std::make_unique<Connection>());
    settling.module.transitioning = true;
    settling.deployDevices(false);
    for (int backoff = 1; backoff <= 3; ++backoff)
    {
        settling.worker.step();
        assert(settling.worker.delayed == backoff && settling.mDeployBackoffs == backoff);
        assert(settling.mDevicesDeploying == 1 && settling.signaling.tasks.empty());
    }
    settling.deployDevices(true); // Queued behind the deferred request.
    settling.module.transitioning = false;
    settling.drain();
    assert(settling.mDeployBackoffs == 0 && settling.recordings >= 1);
    assert(settling.module.terminated == 1 && !settling.mDevicesDeployingNeedsReset);
    assert(settling.mPeerConnections[0]->mute_resets >= 1);

    // Still mid-transition after 20 back-offs: force-stop and deploy anyway.
    LLWebRTCImpl stuck;
    stuck.module.transitioning = true;
    stuck.deployDevices(false);
    stuck.drain();
    assert(stuck.worker.delayed == 20 && stuck.mDeployBackoffs == 0);
    assert(stuck.module.stops == 2 && stuck.recordings == 1 && stuck.playouts == 1);

    // Shutdown during a back-off: the delayed retry runs after terminate() and
    // must not deploy or post completion (the threads have moved away).
    LLWebRTCImpl quitting;
    quitting.module.transitioning = true;
    quitting.deployDevices(false);
    quitting.worker.step();
    assert(quitting.worker.delayed == 1 && quitting.worker.tasks.size() == 1);
    quitting.mDeployRetriesAlive->store(false); // what terminate() does first
    quitting.mSignalingThread = nullptr;        // moved to the shutdown thread
    quitting.worker.step();
    assert(quitting.worker.tasks.empty() && quitting.worker.delayed == 1 && quitting.recordings == 0);
#endif
}

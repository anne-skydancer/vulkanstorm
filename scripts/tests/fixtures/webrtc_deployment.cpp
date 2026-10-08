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
    template<class F> void PostTask(F task) { tasks.emplace_back(task); }
    void step() { auto task = std::move(tasks.front()); tasks.pop_front(); task(); }
};
struct Device
{
    int initialized = 0, terminated = 0;
    bool Playing() const { return false; }
    bool Recording() const { return false; }
    bool RecordingIsInitialized() const { return true; }
    bool PlayoutIsInitialized() const { return true; }
    void StopPlayout() {}
    void ForceStopRecording() {}
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

    for (int failure = 0; failure < 3; ++failure)
    {
        LLWebRTCImpl recover;
        recover.mPeerConnections.emplace_back(std::make_unique<Connection>());
        if (failure == 0) gAudioDeviceMutex.timeouts = 1;
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
    queued.drain(); // The queued request retries the reset after lock failure.
    assert(queued.module.terminated == 1 && queued.recordings == 1);
    assert(!queued.mDevicesDeployingNeedsReset);
}

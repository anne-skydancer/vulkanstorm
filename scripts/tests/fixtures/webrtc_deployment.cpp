// Device-free fixture. Production methods are inserted by the test launcher.
#include <atomic>
#include <cassert>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
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
    void enableSenderTracks(bool) {}
    void enableReceiverTracks(bool) {}
    void resetMute() {}
};
struct LogSink { void OnLogMessage(const std::string&) {} };
struct NullLog { template<class T> NullLog& operator<<(const T&) { return *this; } };
#define RTC_LOG(severity) NullLog{}
std::timed_mutex gAudioDeviceMutex;
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
    void workerStartRecording() { ++recordings; }
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
}

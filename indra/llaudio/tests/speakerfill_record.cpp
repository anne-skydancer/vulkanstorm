/**
 * @file speakerfill_record.cpp
 * @brief Records tests/fixtures/speakerfill_matrices.txt: what today's VLC
 *        speaker fill (indra/media_plugins/libvlc: the "speakerfill" audio
 *        filter, its "speakerfill_output" WASAPI output module and
 *        llvlcspeakerfillconfig.cpp) does, as the reference for the speaker
 *        fill the Rust engine's music bus takes over (vkstorm-rust rung six).
 *
 * Based on indra/media_plugins/libvlc/tests/vlc_speakerfill_test.cpp: it
 * loads the two built modules (libspeakerfill_plugin.dll, MSVC, and
 * libspeakerfill_output_plugin.dll, llvm-mingw over the pinned VLC 3.0.21
 * sources), drives them with VLC objects from the packaged libvlccore and a
 * fake WASAPI device, and prints instead of asserting:
 *
 *  - fill: the filter's output frame for a unit impulse on the left and on
 *    the right input channel, for a stereo, a 5.1 and a mono source and each
 *    target mask the filter can be given, in VLC's channel order and mapped
 *    to the device's WAVEFORMATEXTENSIBLE order the output module writes;
 *  - negotiate: the device layout the output module negotiates and the mask
 *    it publishes to the filter, for each device channel mask, each
 *    FSMusicSpatialSound layout (0/21/41/51/71, 99 invalid) and each device
 *    condition (speakers, headphones, failures, surround rejected);
 *  - gain: the cubic volume curve setMusicGain publishes, the filter's
 *    per-sample gain ramp (rate/20 samples) sampled along its length, and
 *    the fade envelope a music-fade-command produces (3 s in, 2 s out), with
 *    the sample at which completion is published and its playback fence.
 *
 * Not part of the viewer build (the VLC music patching is removed by the
 * port); built once by hand against the pre-port tree:
 *
 *   cl /nologo /EHsc /std:c++17 /O2 /wd4191 /DWIN32_LEAN_AND_MEAN
 *      /I build-vc170-64/packages/include/vlc/plugins /I build-vc170-64/packages/include
 *      /I build-vc170-64/media_plugins/libvlc/speakerfill-source
 *      indra/llaudio/tests/speakerfill_record.cpp
 *      indra/media_plugins/libvlc/llvlcspeakerfillconfig.cpp
 *      /link build-vc170-64/packages/lib/release/libvlc.lib
 *      build-vc170-64/packages/lib/release/libvlccore.lib ole32.lib
 *   speakerfill_record <libspeakerfill_plugin.dll> <libspeakerfill_output_plugin.dll> <out.txt>
 *   (libvlc.dll, libvlccore.dll on PATH; VLC_PLUGIN_PATH at the two modules' directory)
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include <winsock2.h>
#include <basetsd.h>
typedef SSIZE_T ssize_t;
static inline int poll(struct pollfd* descriptors, unsigned count, int timeout)
{
    return WSAPoll(descriptors, count, timeout);
}
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_aout.h>
#include <vlc_block.h>
#include <vlc_modules.h>
#include <vlc/vlc.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#pragma warning(push)
#pragma warning(disable: 4244)
#include "audio_output/mmdevice.h"
#pragma warning(pop)
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../../media_plugins/libvlc/llvlcspeakerfillconfig.h"

static FILE* out = stdout;

static void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Descriptor
{
    void* open = nullptr;
    void* close = nullptr;
    int score = -1;
};

static int Describe(void* opaque, void*, int property, ...)
{
    auto& descriptor = *static_cast<Descriptor*>(opaque);
    va_list arguments;
    va_start(arguments, property);
    if (property == VLC_MODULE_CREATE) *va_arg(arguments, module_t**) = reinterpret_cast<module_t*>(opaque);
    if (property == VLC_MODULE_CB_OPEN) { va_arg(arguments, const char*); descriptor.open = va_arg(arguments, void*); }
    if (property == VLC_MODULE_CB_CLOSE) { va_arg(arguments, const char*); descriptor.close = va_arg(arguments, void*); }
    if (property == VLC_MODULE_SCORE) descriptor.score = va_arg(arguments, int);
    va_end(arguments);
    return 0;
}

static Descriptor load(const char* filename, HMODULE& module)
{
    module = LoadLibraryA(filename);
    require(module != nullptr, "load VLC module DLL");
    using Entry = int (*)(vlc_set_cb, void*);
    const auto entry = reinterpret_cast<Entry>(GetProcAddress(module, "vlc_entry__3_0_0f"));
    require(entry != nullptr, "VLC 3 module ABI entry");
    Descriptor descriptor;
    require(entry(Describe, &descriptor) == 0 && descriptor.open, "module descriptor");
    return descriptor;
}

// ---- the fake device of vlc_speakerfill_test.cpp ----

struct FakeClock final : IAudioClock
{
    UINT64 position = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetFrequency(UINT64* frequency) override { *frequency = 48000; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPosition(UINT64* value, UINT64* counter) override
    { *value = position; if (counter) *counter = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCharacteristics(DWORD* value) override { *value = 0; return S_OK; }
};

struct FakeRenderer final : IAudioRenderClient
{
    std::vector<BYTE> buffer;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT32 frames, BYTE** output) override
    { buffer.resize(static_cast<size_t>(frames) * 8 * sizeof(float)); *output = buffer.data(); return S_OK; }
    HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32, DWORD) override { return S_OK; }
};

struct FakeClient final : IAudioClient
{
    FakeClock clock;
    FakeRenderer renderer;
    WAVEFORMATEXTENSIBLE mix{};
    HRESULT mixResult = S_OK;
    HRESULT initializeResult = S_OK;
    bool rejectSurround = false;
    unsigned channels = 0;
    DWORD mask = 0;
    unsigned refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME, REFERENCE_TIME,
        const WAVEFORMATEX* format, LPCGUID) override
    {
        channels = format->nChannels;
        mask = format->wFormatTag == WAVE_FORMAT_EXTENSIBLE ?
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->dwChannelMask : 0;
        return initializeResult;
    }
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32* frames) override { *frames = 2048; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME* latency) override { *latency = 100000; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32* frames) override { *frames = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsFormatSupported(AUDCLNT_SHAREMODE, const WAVEFORMATEX* format, WAVEFORMATEX** closest) override
    { *closest = nullptr; return rejectSurround && format->nChannels > 2 ? AUDCLNT_E_UNSUPPORTED_FORMAT : S_OK; }
    HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX** format) override
    {
        if (FAILED(mixResult)) return mixResult;
        *format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(mix)));
        require(*format != nullptr, "fake mix allocation");
        memcpy(*format, &mix, sizeof(mix));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME* normal, REFERENCE_TIME* minimum) override
    { *normal = 100000; *minimum = 30000; return S_OK; }
    HRESULT STDMETHODCALLTYPE Start() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetService(REFIID id, void** output) override
    {
        if (id == __uuidof(IAudioClock)) { *output = &clock; return S_OK; }
        if (id == __uuidof(IAudioRenderClient)) { *output = &renderer; return S_OK; }
        return E_NOINTERFACE;
    }
};

struct FakeProperties final : IPropertyStore
{
    ULONG form = Speakers;
    HRESULT result = S_OK;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetCount(DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetAt(DWORD, PROPERTYKEY*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY, PROPVARIANT* value) override
    { value->vt = VT_UI4; value->ulVal = form; return result; }
    HRESULT STDMETHODCALLTYPE SetValue(REFPROPERTYKEY, REFPROPVARIANT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit() override { return E_NOTIMPL; }
};

struct FakeDevice final : IMMDevice
{
    FakeClient client;
    FakeProperties properties;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE Activate(REFIID, DWORD, PROPVARIANT*, void** output) override
    { *output = &client; client.AddRef(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD, IPropertyStore** output) override
    { *output = &properties; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetState(DWORD*) override { return E_NOTIMPL; }
};

// ---- names ----

static std::string vlcNames(uint32_t mask)
{
    static const struct { uint32_t bit; const char* name; } names[] = {
        {AOUT_CHAN_LEFT, "L"}, {AOUT_CHAN_RIGHT, "R"}, {AOUT_CHAN_MIDDLELEFT, "ML"}, {AOUT_CHAN_MIDDLERIGHT, "MR"},
        {AOUT_CHAN_REARLEFT, "RL"}, {AOUT_CHAN_REARRIGHT, "RR"}, {AOUT_CHAN_REARCENTER, "RC"},
        {AOUT_CHAN_CENTER, "C"}, {AOUT_CHAN_LFE, "LFE"}};
    std::string s;
    for (unsigned i = 0; pi_vlc_chan_order_wg4[i]; ++i)
        for (const auto& n : names)
            if (n.bit == pi_vlc_chan_order_wg4[i] && (mask & n.bit)) s += (s.empty() ? "" : ",") + std::string(n.name);
    return s.empty() ? "-" : s;
}

// The output module's mapping (speakerfill_output.c chans_in): VLC order
// L R ML MR RL RR RC C LFE <-> FL FR SL SR BL BR BC FC LFE; the device
// receives WAVEFORMATEXTENSIBLE order (ascending SPEAKER_* bits).
static const DWORD kSpeakerOf[] = {SPEAKER_FRONT_LEFT, SPEAKER_FRONT_RIGHT, SPEAKER_SIDE_LEFT, SPEAKER_SIDE_RIGHT,
    SPEAKER_BACK_LEFT, SPEAKER_BACK_RIGHT, SPEAKER_BACK_CENTER, SPEAKER_FRONT_CENTER, SPEAKER_LOW_FREQUENCY};

static DWORD speakerMask(uint32_t vlc)
{
    DWORD m = 0;
    for (unsigned i = 0; pi_vlc_chan_order_wg4[i]; ++i) if (vlc & pi_vlc_chan_order_wg4[i]) m |= kSpeakerOf[i];
    return m;
}

static std::string speakerNames(DWORD mask)
{
    static const struct { DWORD bit; const char* name; } names[] = {
        {SPEAKER_FRONT_LEFT, "FL"}, {SPEAKER_FRONT_RIGHT, "FR"}, {SPEAKER_FRONT_CENTER, "FC"}, {SPEAKER_LOW_FREQUENCY, "LFE"},
        {SPEAKER_BACK_LEFT, "BL"}, {SPEAKER_BACK_RIGHT, "BR"}, {SPEAKER_FRONT_LEFT_OF_CENTER, "FLC"},
        {SPEAKER_FRONT_RIGHT_OF_CENTER, "FRC"}, {SPEAKER_BACK_CENTER, "BC"}, {SPEAKER_SIDE_LEFT, "SL"}, {SPEAKER_SIDE_RIGHT, "SR"}};
    std::string s;
    for (const auto& n : names) if (mask & n.bit) s += (s.empty() ? "" : ",") + std::string(n.name);
    return s.empty() ? "-" : s;
}

static std::string num(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

// ---- fill matrices ----

static void fill(const Descriptor& filterDescriptor)
{
    using Open = int (*)(vlc_object_t*);
    using Close = void (*)(vlc_object_t*);
    const auto open = reinterpret_cast<Open>(filterDescriptor.open);
    const auto close = reinterpret_cast<Close>(filterDescriptor.close);
    const struct { const char* name; uint32_t mask; } targets[] = {
        {"none", 0}, {"2.0", AOUT_CHANS_2_0}, {"4.0", AOUT_CHANS_4_0}, {"2.1", AOUT_CHANS_2_0 | AOUT_CHAN_LFE},
        {"4.1rear", AOUT_CHANS_4_0 | AOUT_CHAN_LFE},
        {"4.1side", AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_MIDDLELEFT | AOUT_CHAN_MIDDLERIGHT | AOUT_CHAN_LFE},
        {"4.0side", AOUT_CHAN_LEFT | AOUT_CHAN_RIGHT | AOUT_CHAN_MIDDLELEFT | AOUT_CHAN_MIDDLERIGHT},
        {"5.1rear", AOUT_CHANS_5_1}, {"5.1side", AOUT_CHANS_5_0_MIDDLE | AOUT_CHAN_LFE}, {"7.1", AOUT_CHANS_7_1},
        {"5.0rear(unsupported)", AOUT_CHANS_5_0}, {"3.0(unsupported)", AOUT_CHANS_3_0}};
    const struct { const char* name; uint32_t mask; } sources[] = {
        {"stereo", AOUT_CHANS_2_0}, {"5.1", AOUT_CHANS_5_1}, {"mono", AOUT_CHAN_CENTER}};
    std::fprintf(out, "\n# fill <source> <target mask> <impulse on input channel> -> bypass|fill, output channels in VLC order\n"
                      "#   and the same frame in the device's WAVEFORMATEXTENSIBLE order (speakerfill-smooth-gain off)\n");
    for (const auto& source : sources)
    for (const auto& target : targets)
    {
        auto* filter = static_cast<filter_t*>(vlc_object_create(static_cast<vlc_object_t*>(nullptr), sizeof(filter_t)));
        require(filter != nullptr, "filter object");
        var_Create(filter, "speakerfill-smooth-gain", VLC_VAR_BOOL);
        var_SetBool(filter, "speakerfill-smooth-gain", false);
        var_Create(filter, "speakerfill-mask", VLC_VAR_INTEGER);
        var_SetInteger(filter, "speakerfill-mask", target.mask);
        filter->fmt_in.audio.i_format = VLC_CODEC_FL32;
        filter->fmt_in.audio.channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
        filter->fmt_in.audio.i_rate = 48000;
        filter->fmt_in.audio.i_physical_channels = source.mask;
        aout_FormatPrepare(&filter->fmt_in.audio);
        require(open(VLC_OBJECT(filter)) == VLC_SUCCESS, "open filter");
        const unsigned inChannels = filter->fmt_in.audio.i_channels;
        const unsigned outChannels = filter->fmt_out.audio.i_channels;
        const uint32_t outMask = filter->fmt_out.audio.i_physical_channels;
        for (unsigned impulse = 0; impulse < inChannels; ++impulse)
        {
            block_t* input = block_Alloc(4 * inChannels * sizeof(float));
            require(input != nullptr, "block");
            input->i_nb_samples = 4;
            auto* samples = reinterpret_cast<float*>(input->p_buffer);
            for (unsigned s = 0; s < 4 * inChannels; ++s) samples[s] = (s % inChannels == impulse) ? 1.f : 0.f;
            block_t* output = filter->pf_audio_filter(filter, input);
            require(output != nullptr, "filter output");
            const bool bypass = output == input;
            const auto* o = reinterpret_cast<const float*>(output->p_buffer);
            std::string vlcFrame, deviceFrame;
            std::vector<std::pair<DWORD, float>> byBit;
            unsigned column = 0;
            for (unsigned i = 0; pi_vlc_chan_order_wg4[i]; ++i)
            {
                if (!(outMask & pi_vlc_chan_order_wg4[i])) continue;
                vlcFrame += (vlcFrame.empty() ? "" : " ") + num(o[column]);
                byBit.push_back({kSpeakerOf[i], o[column]});
                ++column;
            }
            for (DWORD bit = 1; bit; bit <<= 1)
                for (const auto& b : byBit) if (b.first == bit) deviceFrame += (deviceFrame.empty() ? "" : " ") + num(b.second);
            // The other three frames must equal the first (no state).
            for (unsigned f = 1; f < 4; ++f) for (unsigned c = 0; c < outChannels; ++c)
                require(o[f * outChannels + c] == o[c], "stateless fill");
            std::string inName = vlcNames(source.mask);
            {
                // name of the impulse channel in VLC order
                unsigned seen = 0;
                for (unsigned i = 0; pi_vlc_chan_order_wg4[i]; ++i)
                    if (source.mask & pi_vlc_chan_order_wg4[i]) { if (seen++ == impulse) { inName = vlcNames(pi_vlc_chan_order_wg4[i]); break; } }
            }
            std::fprintf(out, "fill %s %s in=%s -> %s vlc[%s]=%s device[%s]=%s\n", source.name, target.name, inName.c_str(),
                bypass ? "bypass" : "fill", vlcNames(outMask).c_str(), vlcFrame.c_str(),
                speakerNames(speakerMask(outMask)).c_str(), deviceFrame.c_str());
            block_Release(output);
        }
        close(VLC_OBJECT(filter));
        vlc_object_release(filter);
    }
}

// ---- output negotiation ----

static void negotiate(const Descriptor& descriptor)
{
    using Start = HRESULT (*)(aout_stream_t*, audio_sample_format_t*, const GUID*);
    using Stop = HRESULT (*)(aout_stream_t*);
    const auto start = reinterpret_cast<Start>(descriptor.open);
    const auto stop = reinterpret_cast<Stop>(descriptor.close);
    const DWORD masks[] = {SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT,
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY,
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_LOW_FREQUENCY,
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
        SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT | SPEAKER_LOW_FREQUENCY,
        KSAUDIO_SPEAKER_5POINT1, KSAUDIO_SPEAKER_5POINT1_SURROUND, KSAUDIO_SPEAKER_7POINT1_SURROUND};
    const char* scenarios[] = {"speakers", "headphones", "mixformat-fails", "source-5.1", "formfactor-fails",
                               "initialize-fails", "surround-rejected"};
    std::fprintf(out, "\n# negotiate <device condition> <device mask> layout=<FSMusicSpatialSound> -> <result> device=<channels>[<mask>]"
                      " published speakerfill-mask (VLC / device names); stereo 44100 Hz FL32 source unless noted\n");
    for (unsigned scenario = 0; scenario < 7; ++scenario)
    for (const auto mask : masks)
    for (const int layout : {0, 21, 41, 51, 71, 99})
    {
        FakeDevice device;
        if (scenario == 1) device.properties.form = Headphones;
        if (scenario == 2) device.client.mixResult = E_FAIL;
        if (scenario == 4) device.properties.result = E_FAIL;
        if (scenario == 5) device.client.initializeResult = E_FAIL;
        if (scenario == 6) device.client.rejectSurround = true;
        auto& mix = device.client.mix;
        mix.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        mix.Format.nSamplesPerSec = 48000;
        for (DWORD bits = mask; bits; bits >>= 1) mix.Format.nChannels += bits & 1;
        mix.Format.wBitsPerSample = 32;
        mix.Format.nBlockAlign = mix.Format.nChannels * 4;
        mix.Format.nAvgBytesPerSec = mix.Format.nBlockAlign * 48000;
        mix.Format.cbSize = sizeof(mix) - sizeof(mix.Format);
        mix.Samples.wValidBitsPerSample = 32;
        mix.dwChannelMask = mask;
        mix.SubFormat = {3, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
        auto* parent = static_cast<vlc_object_t*>(vlc_object_create(static_cast<vlc_object_t*>(nullptr), sizeof(vlc_object_t)));
        require(parent != nullptr, "output parent");
        parent->obj.flags |= OBJECT_FLAGS_QUIET;
        var_Create(parent, "speakerfill-layout", VLC_VAR_INTEGER);
        var_SetInteger(parent, "speakerfill-layout", layout);
        auto* stream = static_cast<aout_stream_t*>(vlc_object_create(parent, sizeof(aout_stream_t)));
        require(stream != nullptr, "stream object");
        stream->owner.device = &device;
        stream->owner.activate = [](void* opaque, REFIID identifier, PROPVARIANT* parameters, void** output)
        { return static_cast<FakeDevice*>(opaque)->Activate(identifier, CLSCTX_ALL, parameters, output); };
        audio_sample_format_t format{};
        format.channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
        format.i_format = VLC_CODEC_FL32;
        format.i_rate = 44100;
        format.i_physical_channels = scenario == 3 ? AOUT_CHANS_5_1 : AOUT_CHANS_2_0;
        aout_FormatPrepare(&format);
        const auto result = start(stream, &format, nullptr);
        const uint32_t published = static_cast<uint32_t>(var_GetInteger(parent, "speakerfill-mask"));
        std::fprintf(out, "negotiate %s %s layout=%d -> %s device=%u[%s] stream=%s published=%s/%s\n", scenarios[scenario],
            speakerNames(mask).c_str(), layout, SUCCEEDED(result) ? "ok" : "failed", device.client.channels,
            speakerNames(device.client.mask).c_str(), SUCCEEDED(result) ? vlcNames(format.i_physical_channels).c_str() : "-",
            vlcNames(published).c_str(), speakerNames(speakerMask(published)).c_str());
        if (SUCCEEDED(result)) require(SUCCEEDED(stop(stream)), "stop");
        vlc_object_release(stream);
        vlc_object_release(parent);
    }
}

// ---- gain curve, gain ramp and fade envelope ----

static filter_t* smoothFilter(const Descriptor& d, unsigned rate, float gain, float fadeInitial, bool fades)
{
    using Open = int (*)(vlc_object_t*);
    auto* filter = static_cast<filter_t*>(vlc_object_create(static_cast<vlc_object_t*>(nullptr), sizeof(filter_t)));
    require(filter != nullptr, "smooth filter");
    var_Create(filter, "speakerfill-mask", VLC_VAR_INTEGER);
    var_SetInteger(filter, "speakerfill-mask", 0);
    if (fades)
    {
        for (const char* name : {"music-fade-command", "music-fade-fence", "music-fade-epoch", "music-fade-completed", "music-clock-epoch"})
        { var_Create(filter, name, VLC_VAR_INTEGER); var_SetInteger(filter, name, 0); }
        var_Create(filter, "music-fade-initial", VLC_VAR_FLOAT);
        var_SetFloat(filter, "music-fade-initial", fadeInitial);
        var_SetInteger(filter, "music-clock-epoch", 1);
    }
    var_Create(filter, "speakerfill-smooth-gain", VLC_VAR_BOOL);
    var_SetBool(filter, "speakerfill-smooth-gain", true);
    var_Create(filter, "speakerfill-gain", VLC_VAR_FLOAT);
    var_SetFloat(filter, "speakerfill-gain", gain);
    filter->fmt_in.audio.i_format = VLC_CODEC_FL32;
    filter->fmt_in.audio.channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
    filter->fmt_in.audio.i_rate = rate;
    filter->fmt_in.audio.i_physical_channels = AOUT_CHANS_2_0;
    aout_FormatPrepare(&filter->fmt_in.audio);
    require(reinterpret_cast<Open>(d.open)(VLC_OBJECT(filter)) == VLC_SUCCESS, "open smooth filter");
    return filter;
}

// Runs `frames` frames of ones through the filter in blocks of `partition`
// and returns the left channel (the per-sample gain), noting the frame after
// which music-fade-completed was first published.
static std::vector<float> runOnes(filter_t* filter, unsigned frames, unsigned partition, unsigned rate, long long& completedAt, long long& fence)
{
    std::vector<float> gains;
    completedAt = -1;
    for (unsigned position = 0; position < frames;)
    {
        const unsigned n = partition < frames - position ? partition : frames - position;
        auto* block = block_Alloc(n * 2 * sizeof(float));
        require(block != nullptr, "ones block");
        block->i_nb_samples = n;
        block->i_pts = 1000000 + static_cast<int64_t>(position) * 1000000 / rate;
        auto* s = reinterpret_cast<float*>(block->p_buffer);
        for (unsigned i = 0; i < n * 2; ++i) s[i] = 1.f;
        block = filter->pf_audio_filter(filter, block);
        require(block != nullptr, "ones output");
        s = reinterpret_cast<float*>(block->p_buffer);
        for (unsigned i = 0; i < n; ++i) gains.push_back(s[i * 2]);
        position += n;
        if (completedAt < 0 && var_Type(filter, "music-fade-completed") == VLC_VAR_INTEGER &&
            var_GetInteger(filter, "music-fade-completed") != 0)
        {
            completedAt = position;
            fence = var_GetInteger(filter, "music-fade-fence");
        }
        block_Release(block);
    }
    return gains;
}

static void sampleCurve(const char* label, const std::vector<float>& g, unsigned points)
{
    std::fprintf(out, "%s samples=%zu", label, g.size());
    const size_t n = g.size();
    for (unsigned p = 0; p <= points; ++p)
    {
        const size_t i = p == points ? n - 1 : (size_t)p * (n - 1) / points;
        std::fprintf(out, " [%zu]=%s", i, num(g[i]).c_str());
    }
    std::fprintf(out, "\n");
}

static void gains(const Descriptor& d, libvlc_instance_t* instance)
{
    using Close = void (*)(vlc_object_t*);
    const auto close = reinterpret_cast<Close>(d.close);
    std::fprintf(out, "\n# gain curve: setMusicGain(volume) publishes speakerfill-gain (llvlcspeakerfillconfig.cpp)\n");
    auto* media = libvlc_media_new_location(instance, "https://example.invalid/test");
    auto* player = libvlc_media_player_new_from_media(media);
    require(player != nullptr, "media player");
    require(configureMusicGain(player, 21, 1.f), "configure music gain");
    auto* owner = reinterpret_cast<vlc_object_t*>(player);
    for (const float volume : {0.f, 0.05f, 0.1f, 0.105f, 0.125f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f})
    {
        require(setMusicGain(player, volume), "setMusicGain");
        std::fprintf(out, "curve volume=%s -> gain=%s\n", num(volume).c_str(), num(var_GetFloat(owner, "speakerfill-gain")).c_str());
    }
    require(setMusicGain(player, .5f, true), "setMusicGain muted");
    const std::string mutedGain = num(var_GetFloat(owner, "speakerfill-gain"));
    std::fprintf(out, "curve muted: setMusicGain(0.5, muted) -> gain=%s music-hard-mute=%d (the filter writes 0 while hard-muted)\n",
        mutedGain.c_str(), (int)var_GetBool(owner, "music-hard-mute"));
    libvlc_media_player_release(player);
    libvlc_media_release(media);

    std::fprintf(out, "\n# ramp: per-sample gain on a constant 1.0 input after speakerfill-gain changes; the filter ramps\n"
                      "#   linearly over rate/20 samples from the current gain, partition-independent (blocks of 37 frames)\n");
    for (const unsigned rate : {44100u, 48000u})
    {
        filter_t* filter = smoothFilter(d, rate, 1.f, 1.f, false);
        long long completed, fence;
        var_SetFloat(filter, "speakerfill-gain", .25f);
        auto g = runOnes(filter, rate / 20 + 10, 37, rate, completed, fence);
        sampleCurve(("ramp rate=" + std::to_string(rate) + " 1->0.25").c_str(), g, 20);
        var_SetFloat(filter, "speakerfill-gain", 1.f);
        g = runOnes(filter, rate / 40, 37, rate, completed, fence);
        var_SetFloat(filter, "speakerfill-gain", 0.f);
        auto g2 = runOnes(filter, rate / 20 + 10, 37, rate, completed, fence);
        sampleCurve(("ramp rate=" + std::to_string(rate) + " 0.25->1 retargeted to 0 at " + std::to_string(rate / 40)).c_str(), g, 10);
        sampleCurve(("ramp rate=" + std::to_string(rate) + " ...->0").c_str(), g2, 20);
        close(VLC_OBJECT(filter));
        vlc_object_release(filter);
        filter = smoothFilter(d, rate, .125f, 1.f, false);
        g = runOnes(filter, 4, 4, rate, completed, fence);
        std::fprintf(out, "ramp rate=%u open at gain 0.125: first sample %s (no ramp from 1 at startup)\n", rate, num(g[0]).c_str());
        close(VLC_OBJECT(filter));
        vlc_object_release(filter);
    }

    std::fprintf(out, "\n# fade: music-fade-command = serial<<32 | round(seconds*1000)<<16 | round(target*65535); envelope from\n"
                      "#   the current transition to the target over ms*rate/1000 samples, linear; speakerfill-gain 1 (no ramp);\n"
                      "#   completion published after the block holding the final sample, fence = pts + (frame+1)*1e6/rate\n");
    for (const unsigned rate : {44100u, 48000u})
    for (const auto& fade : {std::make_pair(3.f, 1.f), std::make_pair(2.f, 0.f)})
    {
        const float initial = fade.second == 1.f ? 0.f : 1.f;
        filter_t* filter = smoothFilter(d, rate, 1.f, initial, true);
        const auto serial = 7u;
        const auto command = (static_cast<uint64_t>(serial) << 32) | (static_cast<uint64_t>(std::llround(fade.first * 1000.)) << 16) |
            static_cast<uint64_t>(std::llround(fade.second * 65535.));
        var_SetInteger(filter, "music-fade-command", static_cast<int64_t>(command));
        long long completed = -1, fence = 0;
        const unsigned frames = static_cast<unsigned>(fade.first * rate) + 1000;
        auto g = runOnes(filter, frames, 512, rate, completed, fence);
        char label[160];
        std::snprintf(label, sizeof(label), "fade rate=%u %s %.1fs initial=%s target=%s", rate, fade.second ? "in" : "out",
            fade.first, num(initial).c_str(), num(fade.second).c_str());
        sampleCurve(label, std::vector<float>(g.begin(), g.begin() + static_cast<size_t>(fade.first * rate) + 1), 30);
        std::fprintf(out, "%s duration_samples=%u completed_after_frame=%lld fence_us=%lld (block pts 1000000 + position*1e6/rate, blocks of 512)\n",
            label, static_cast<unsigned>(std::llround(fade.first * 1000.)) * rate / 1000, completed, fence);
        close(VLC_OBJECT(filter));
        vlc_object_release(filter);
    }
}

int main(int argc, char** argv)
{
    require(argc == 4, "usage: speakerfill_record <filter dll> <output dll> <out.txt>");
    HMODULE filterModule = nullptr, outputModule = nullptr;
    const auto filter = load(argv[1], filterModule);
    const auto output = load(argv[2], outputModule);
    out = std::fopen(argv[3], "wb");
    require(out != nullptr, "output file");
    const char* arguments[] = {"--no-plugins-cache", "--no-audio", "--no-video", "--quiet"};
    libvlc_instance_t* instance = libvlc_new(4, arguments);
    require(instance != nullptr, "libvlc");
    std::fprintf(out, "# Speaker fill reference: today's VLC speakerfill filter and speakerfill_output module, recorded before the\n"
                      "# Rust port by indra/llaudio/tests/speakerfill_record.cpp (built from vlc_speakerfill_test.cpp's harness) with\n"
                      "# libvlc %s, the filter built by MSVC (vlc_speakerfill target), the output by llvm-mingw (vlc_speakerfill_output).\n"
                      "# VLC channel order: L R ML MR RL RR RC C LFE; the output module maps it to FL FR SL SR BL BR BC FC LFE and\n"
                      "# writes WAVEFORMATEXTENSIBLE order (FL FR FC LFE BL BR BC SL SR). Values are %%.9g floats.\n",
                 libvlc_get_version());
    fill(filter);
    negotiate(output);
    gains(filter, instance);
    std::fclose(out);
    libvlc_release(instance);
    FreeLibrary(outputModule);
    FreeLibrary(filterModule);
    std::puts("recorded");
    return 0;
}

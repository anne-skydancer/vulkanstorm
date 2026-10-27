# 07 — Media, audio, voice and plugins

This chapter covers everything in the viewer that produces or carries sound,
video or web content: the audio engine (`indra/llaudio`), the out-of-process
plugin host (`indra/llplugin`) and its plugins (`indra/media_plugins`: CEF,
VLC, GStreamer), the viewer-side media layer (parcel media, shared
media/MOAP, `LLMediaCtrl`), voice (`indra/llwebrtc`, WebRTC and Vivox
clients), gestures and sound triggering, crash reporting and the login and
update components. Audio is the only part already ported to Rust: since rung
six the default `LLAudioEngine` backend (`LLAudioEngine_VKS`) is a thin C++
adapter over `vkstorm-audio` through 51 `vks_audio_*`/`vks_music_*` C
functions, and parcel music plays in-process through that engine instead of
the VLC plugin. Most of the remaining code wraps large third-party engines
(Chromium via Dullahan, libVLC, GStreamer, libwebrtc, Vivox SLVoice); porting
those wrappers buys little, while the plugin IPC host and the viewer-side
policy layers are plausible later candidates.

Paths in tables without a leading directory are relative to `indra/`;
`doc/`, `scripts/` and `.github/` paths are relative to the repository root. Line numbers
refer to branch `codex/rust-inventory-model` as surveyed (2026-10-27). Rust
crate paths refer to the sibling checkout of `vkstorm-core-rust` at
`1b183c7`, which is the commit embedded in the pinned package version
`vkstorm-ffi 0.1.0.1b183c77e7.full` (`autobuild.xml`).

## Scope and size

Counts are C/C++ source and header lines (`.cpp .h .hpp .c .mm .m`) of
tracked files, measured with

```sh
cd indra
f=$(git ls-files <pathspec...> | grep -E '\.(cpp|h|hpp|c|mm|m)$'); echo "$f" | grep -c . ; cat $f | wc -l
```

### Libraries and executables

| Directory / group | Files | Lines | Built in this tree | Origin |
|---|---:|---:|---|---|
| `llaudio/` library (top level only) | 27 | 8,710 | always | LL upstream engine; FS output-device selection and stream metadata; Vulkanstorm `*_vks.*` |
| of which `llaudio/*_vks.*` | 6 | 1,470 | `USE_VKS_AUDIO` (default) | Vulkanstorm (rung six) |
| of which `llaudio/*fmodstudio*` | 6 | 1,981 | `USE_FMODSTUDIO` | LL upstream + FS |
| of which `llaudio/*openal*` | 4 | 847 | `USE_OPENAL` | LL upstream |
| `llaudio/tests/` (code) | 6 | 3,349 | `LL_TESTS` + VKS audio | Vulkanstorm |
| `llaudio/tests/fixtures/` | (data) | — | — | Vulkanstorm (recorded from the C++/SoLoud and VLC before the port) |
| `llplugin/` library (top level) | 16 | 6,555 | always | LL upstream (FS touches in `llpluginprocessparent`, `llpluginclassmedia`) |
| `llplugin/slplugin/` (SLPlugin executable) | 3 | 500 | always | LL upstream |
| `media_plugins/base/` | 2 | 333 | `ENABLE_MEDIA_PLUGINS` (default ON) | LL upstream |
| `media_plugins/cef/` | 7 | 2,363 | Windows, macOS, Linux | LL upstream (Dullahan) |
| `media_plugins/libvlc/` | 1 | 799 | Windows, macOS | LL upstream + Vulkanstorm lifecycle fixes |
| `media_plugins/gstreamer10/` | 4 | 1,298 | Linux | FS (Linux media) |
| `media_plugins/gstreamer010/` | 7 | 2,305 | **not built** (no `add_subdirectory`) | legacy FS |
| `media_plugins/example/` | 1 | 395 | **not built** (commented out) | LL upstream |
| `llwebrtc/` (shared library `llwebrtc.dll`/`.so`) | 3 | 2,831 | always | LL upstream + FS fixes (17 `<FS` markers) |
| `viewer_components/login/` (`lllogin`) | 3 | 850 | always | LL upstream |
| `llcrashlogger/` | 6 | 1,309 | only if `NOT USE_BUGSPLAT`; linked by nothing (see Risks) | LL upstream |
| `linux_crash_logger/` | 2 | 191 | Linux | FS (BugSplat minidump uploader) |
| `win_crash_logger/` | 2 | 209 | **not built** (no `CMakeLists.txt`) | legacy |
| `mac_crash_logger/` | 0 | 0 | only a stray `.DS_Store` is tracked | — |

`viewer_components/` contains only `login/` (plus a `Resources/` README);
there is no `updater` component in this tree. Updating is handled in
`indra/newview` (`llvelopack.cpp`, `llvvmquery.cpp`) and by the
`viewer-manager` package (below).

### Viewer-side groups in `indra/newview`

Pathspecs used (each with the `.cpp`/`.h` filter above):

| Group | Pathspecs | Files | Lines |
|---|---|---:|---:|
| Media | `llviewermedia*` `llmediactrl*` `llviewerparcelmedia*` `llmediadataclient*` `llviewermediafocus*` `llpanelprimmediacontrols*` `llpanelnearbymedia*` `llpanelmediasettings*` `llfloatermediasettings*` `llpanellandmedia*` `floatermedialists*` | 31 | 16,868 |
| Voice | `llvoice*` `llspeakers*` `fsfloatervoicecontrols*` `llpanelvoice*` `llfloatervoice*` `fsnearbychatvoicemonitor*` `llfloaterchatvoicevolume*` | 27 | 21,512 |
| Audio / sound UI | `llvieweraudio*` `llaudiosourcevo*` `lldeferredsounds*` `llpreviewsound*` `llfloatersounddevices*` `NACLfloaterexploresounds*` `fsfloaterstreamtitle*` `streamtitledisplay*` `llpanellandaudio*` `fspanelpreferenceuisounds*` | 20 | 3,848 |
| Gestures | `llgesturemgr*` `llviewergesture*` `llpreviewgesture*` `llfloatergesture*` `llgesturelistener*` `lldelayedgestureerror*` | 12 | 5,609 |
| Login / update | `lllogininstance*` `llpanellogin*` `fspanellogin*` `llloginhandler*` `llpanelloginlistener*` `llvelopack*` `llvvmquery*` | 14 | 6,049 |

Largest single files: `newview/llvoicevivox.cpp` 8,118; `newview/llviewermedia.cpp`
4,248; `newview/llvoicewebrtc.cpp` 3,486; `llaudio/llaudioengine.cpp` 2,086;
`llwebrtc/llwebrtc.cpp` 1,829; `newview/llviewerparcelmedia.cpp` 1,692;
`llplugin/llpluginclassmedia.cpp` 1,657.

Total for the chapter: about 32,000 lines in the libraries and executables
above (including the unbuilt directories and the llaudio tests) plus about 53,900 in the
`newview` groups.

### Third-party packages (`autobuild.xml`)

| Package (autobuild name) | Version string | Platforms in `autobuild.xml` | Used by | CMake |
|---|---|---|---|---|
| `dullahan` (CEF wrapper) | `1.26.0.202510161627_139.0.40_g465474a_chromium-139.0.7258.139` (CEF 139.0.40, Chromium 139) | windows64, darwin64, linux64 | `media_plugin_cef` | `cmake/CEFPlugin.cmake` |
| `vlc-bin` | `3.0.21.11968962952` | windows64, darwin64 (no Linux) | `media_plugin_libvlc` | `cmake/LibVLCPlugin.cmake` |
| `gstreamer10` | 1.6.3 (2016 build from `3p.firestormviewer.org`) | linux64 | `media_plugin_gstreamer10` (staged as `libmedia_plugin_gstreamer.so`, `newview/viewer_manifest.py:2369`) | `cmake/GStreamer10Plugin.cmake` |
| `webrtc` | `m137.7151.04.23.22004231636` | windows64, darwin64, linux64 | `llwebrtc` | `cmake/WebRTC.cmake` |
| `slvoice` (Vivox) | `4.10.0000.32327.5fc3fe7c.5942f08` | windows64, darwin64, linux64 | `SLVoice` daemon for `LLVivoxVoiceClient` | `cmake/ViewerMiscLibs.cmake:20`, `cmake/Copy3rdPartyLibs.cmake` |
| `ffmpeg` (3p-ffmpeg, decoder-only LGPL) | `9.0.2.39711573873` | windows64, linux64 | loaded at run time by `vkstorm-audio` (HE-AAC, Opus) | `cmake/VKSAudio.cmake:24` |
| `ogg_vorbis` | `1.3.5-1.3.7.11968798109` (libogg 1.3.5, libvorbis 1.3.7) | all three | `LLVorbisEncode` (uploads); parity test oracle | `cmake/Audio.cmake` (`ll::vorbis`) |
| `openal` (openal-soft) | `1.24.2-r1` | all three | `LLAudioEngine_OpenAL` (`--openal`) | `cmake/OPENAL.cmake` |
| `fmodstudio` | version key `2.02.20`; URL is a local `file:///opt/firestorm/fmodstudio-2.03.12-…` | all three | `LLAudioEngine_FMODSTUDIO` (`--fmodstudio`, proprietary) | `cmake/FMODSTUDIO.cmake` |
| `vkstorm-ffi` | `0.1.0.1b183c77e7.full` | windows64, linux64 | `llaudio`, `llcommon` (and so every executable, including SLPlugin) | `cmake/Rust.cmake` |
| `bugsplat` / `breakpad` | `5.0.1.0-71fc41e` / — | windows64, darwin64 / linux | crash reporting when `USE_BUGSPLAT` | `cmake/bugsplat.cmake` |
| `velopack` | `40232ef.23500976684` | windows64, darwin64 | updater/installer when `USE_VELOPACK` | `cmake/Velopack.cmake` |
| `viewer-manager` | `3.0-f14b5ec` | all three | Python launcher/update checker (`SLVersionChecker`) | `cmake/ViewerManager.cmake` |
| `websocketpp` | `0.8.2.24525603568` | common | not in this chapter's code; listed because voice-adjacent searches hit it | — |

The `soloud` package is no longer referenced (`grep -ci soloud autobuild.xml`
prints 0); remaining "SoLoud" strings in `indra/llaudio` are comments, test
names and the reported driver name.

## Components

### Audio engine (`indra/llaudio`)

**Responsibility.** Plays sound effects, UI sounds, attached/looped object
sounds, wind and parcel music; decodes sound assets (Ogg Vorbis) into a
cache of WAV-like `.dsf` files; encodes uploads to Vorbis.

**Key classes.**

| Class | Location | Role | Origin |
|---|---|---|---|
| `LLAudioEngine` | `llaudio/llaudioengine.h:74`, `.cpp` (`init` 113, `idle` 239, `getFreeChannel` 589, `preloadSound` 658, `triggerSound` 801/839, `startInternetStream` 170) | Backend-independent application logic: 30 channels (`LL_MAX_AUDIO_CHANNELS`, `llaudioengine.h:57`), sources, priorities, queued and synced sounds, buffers, asset transfers, sound history | LL; FS additions (15 `<FS` markers in the `.cpp`) |
| `LLAudioSource`, `LLAudioData`, `LLAudioChannel`, `LLAudioBuffer` | `llaudioengine.h:301, 424, 466, 507` | Source (object or UI), decoded data, a mixer slot, a backend buffer | LL |
| `LLListener` | `llaudio/lllistener.h:32` | Listener position/orientation, Doppler/rolloff | LL |
| `LLStreamingAudioInterface` | `llaudio/llstreamingaudio.h:35` | Music slot. Fade API (`hasAudioFade`, `beginAudioFade`, `AudioFadeResult`), `setAudioHardMute`, `setMusicSpatialLayout` are Vulkanstorm; metadata signal is FS ("Firestorm additions", line 66) | LL + FS + Vulkanstorm |
| `LLAudioDecodeMgr`, `LLVorbisDecodeState` | `llaudio/llaudiodecodemgr.h:40`; `.cpp` `initDecode` 141, `decodeSection` 243, `finishDecode` 271, `startMoreDecodes` 372, `processQueue` 584 | Decode queue; work posted from `mainloop` to the `General` work queue; `.dsf` written via `LLLFSThread` | LL; decode core now Rust |
| `LLVorbisEncode` | `llaudio/llvorbisencode.h` | Upload encoding (libvorbisenc) | LL |
| `LLWindGen` | `llaudio/llwindgen.h:33` | Wind synthesis template; now used only as reference (the Rust mixer has its own port) | LL |
| `LLAudioEngine_VKS`, `LLAudioChannelVKS`, `LLAudioBufferVKS` | `llaudio/llaudioengine_vks.h:38, 93, 121` | Rust backend adapter | Vulkanstorm |
| `LLListener_VKS` | `llaudio/lllistener_vks.h:30` | Listener → `vks_audio_set_listener` | Vulkanstorm |
| `LLStreamingAudio_VKS` | `llaudio/llstreamingaudio_vks.h:41` | Parcel music through the Rust player | Vulkanstorm |
| `LLAudioEngine_OpenAL`, `LLAudioEngine_FMODSTUDIO`, `LLStreamingAudio_FMODSTUDIO` | `llaudioengine_openal.h:37`, `llaudioengine_fmodstudio.h:47`, `llstreamingaudio_fmodstudio.h:46` | Compile-time alternatives | LL + FS |
| `LLListener_DS3D` | `llaudio/lllistener_ds3d.h:37` | Header only, not in `CMakeLists.txt` | legacy |

**Backend selection.** `indra/cmake/VKSAudio.cmake` sets `USE_VKS_AUDIO`
ON unless OpenAL or FMOD Studio is requested, fails configuration if two are
selected (line 14), and fails on any platform other than Windows x64 or
Linux x64 (lines 18-21). `newview/llstartup.cpp:1122` constructs
`LLAudioEngine_VKS` under `#ifdef LL_VKS_AUDIO`, else FMOD, else OpenAL.
`scripts/configure_firestorm.sh:88,138,495` maps `--vksaudio`. CI Release
passes `-DUSE_VKS_AUDIO:BOOL=ON` (`.github/workflows/build.yaml:162`).

**Thread model.**
- Main thread: `LLAudioEngine::idle()` (`llaudioengine.cpp:239`) updates
  sources and channels; every channel parameter change is an FFI call.
- Device thread: owned by `vkstorm-audio` (WASAPI on Windows; PulseAudio
  then ALSA on Linux, loaded at run time; null device for tests) — per
  `doc/rust_port.md` rung six "The boundary". The C++ side polls device
  state in `LLAudioEngine_VKS::idle()` (`llaudioengine_vks.cpp:308`,
  `vks_audio_poll` at 328).
- Decode: `LLAudioDecodeMgr::Impl::startMoreDecodes` posts to the `General`
  work queue; the Rust decode call `vks_audio_decode_asset` runs there
  (`llaudiodecodemgr.cpp:178`).
- Music: the Rust stream player connects, demuxes and decodes on its own
  threads (comment, `llstreamingaudio_vks.h:36-40`).
- Device mutex: `gAudioDeviceMutex` (a `std::timed_mutex`, FS FIRE-36022)
  is taken with a 1 s timeout around init, device enumeration and device
  switch (`llaudioengine_vks.cpp:99, 142, 188, 293`), and by `llwebrtc`
  around its device changes (`llwebrtc/llwebrtc.cpp:487, 676, 883`). See
  Risks on whether the two modules share the same object.

**Ownership.** `gAudiop` (global, created in `llstartup.cpp:1117-1145`)
owns the engine; `LLAudioEngine_VKS::sAudio` (static `VksAudio*`) is the
Rust engine object; buffers are reference-counted `VksAudioWav` released by
`vks_audio_wav_free` (`llaudioengine_vks.cpp:686, 693`).

**Streaming music selection.** `llstartup.cpp:1156-1168`: if
`UseMediaPluginsForStreamingAudio` is false (default), the engine's
`createDefaultStreamingAudioImpl()` is used (`LLStreamingAudio_VKS`,
`llaudioengine_vks.cpp:273`); otherwise, or if the engine supplies none
(OpenAL), `LLStreamingAudio_MediaPlugins`
(`newview/llviewermedia_streamingaudio.cpp`) drives a hidden media plugin
instance (VLC on Windows/macOS, GStreamer on Linux).

### Viewer audio policy (`indra/newview`)

| Class / function | Location | Role | Origin |
|---|---|---|---|
| `LLViewerAudio` | `newview/llvieweraudio.h:42`; `.cpp` `startInternetStreamWithAutoFade` 89, `onIdleUpdate` 183, `stopInternetStreamWithAutoFade` 289, `startFading` 318, teleport hooks 410-457 | Music fade policy: FSFadeAudioStream, the login hold at zero until `STATE_STARTED` (114, 206, 220, 343), backend fade when the slot implements it (346, 372), frame-timed fade otherwise | LL + FS (11 markers) + Vulkanstorm fade completion |
| `init_audio`, `audio_update_volume`, `audio_update_listener`, `audio_update_wind` | `llvieweraudio.cpp:475, 550, 672, 703` | UI sound preload, volume/mute mixing, listener and wind per frame | LL |
| `LLAudioSourceVO` | `newview/llaudiosourcevo.h:36` | Object-attached sound sources (cutoff, mutes) | LL |
| `process_sound_trigger`, `process_preload_sound`, `process_attached_sound`, `process_attached_sound_gain_change` | `newview/llviewermessage.cpp:4857, 4974, 5035, 5092`; registered `llstartup.cpp:3071-3074` | UDP message handlers that start sounds | LL |
| `send_sound_trigger` | `llviewermessage.cpp:618` | Sends `SoundTrigger` (gestures, inventory "play") | LL |
| `LLDeferredSounds` | `newview/lldeferredsounds.h:33` | Queues UI sounds until audio is ready | FS |
| `FSStreamTitleManager`, `FSFloaterStreamTitle`, `StreamTitleDisplay` | `newview/fsfloaterstreamtitle.h:41, 98`; `streamtitledisplay.h:35` | Consume the music metadata signal (`fsfloaterstreamtitle.cpp:58`, `streamtitledisplay.cpp:61`) | FS |
| `NACLfloaterexploresounds`, `fspanelpreferenceuisounds`, `floatermedialists` | `newview/` | Sound explorer, UI sound prefs, media/radio lists | FS |

`gAudiop` is referenced 220 times in 25 `newview/*.cpp` files
(`grep -rn gAudiop --include=*.cpp newview | wc -l`).

### Gestures

`LLGestureMgr` (`newview/llgesturemgr.h:51`) plays gestures
(`playGesture` at `llgesturemgr.cpp:552`, `runStep` at 1070). A `STEP_SOUND`
step calls `send_sound_trigger(sound_id, volume)` (`llgesturemgr.cpp:1108`),
so gesture sounds go to the simulator and come back as `SoundTrigger`, i.e.
through `process_sound_trigger` and `LLAudioEngine::triggerSound`. Gesture
assets are loaded through the inventory/asset system (`LLGestureMgr`
inherits `LLInventoryFetchItemsObserver`). `LLViewerGesture`/
`LLViewerGestureList` (`llviewergesture.h:37, 60`) are the legacy
chat-triggered gestures. All LL upstream.

### Plugin host (`indra/llplugin`)

**Responsibility.** Runs each media plugin in its own `SLPlugin` process,
exchanges LLSD messages with it, and shares pixel buffers through shared
memory.

**Process model.**

```
viewer (main thread)                          SLPlugin process (one per media instance)
LLViewerMediaImpl                             slplugin.cpp main(): LLPluginProcessChild loop
  └ LLPluginClassMedia ─────┐                   └ LLPluginInstance: apr_dso_load(plugin .dll/.so)
      └ LLPluginProcessParent│ TCP loopback        └ LLPluginInitEntryPoint  (media_plugin_base.cpp:194)
          LLPluginMessagePipe ├─ NUL-delimited ─────  MediaPluginBase subclass (CEF / VLC / GStreamer)
          LLPluginSharedMemory└─ named shm (pixels)
```

| Class | Location | Notes |
|---|---|---|
| `LLPluginClassMedia` | `llplugin/llpluginclassmedia.h:38`; `.cpp` `init` 70, `idle` 169, `getBitsData` 301, `setSize` 311, `receivePluginMessage` 1030 | The media API the viewer uses: navigation, input, size negotiation, volume, cookies, priorities (`PRIORITY_UNLOADED`…`PRIORITY_HIGH`) |
| `LLPluginClassMediaOwner` | `llpluginclassmediaowner.h:38` | Event callback interface (`EMediaEvent`, `EMediaStatus`) |
| `LLPluginProcessParent` | `llpluginprocessparent.h:56`; `.cpp` `create` 144, `init` 276, `idle` 338 (state machine 400-720), `sendMessage` 774, `poll` 948, `receiveMessage` 1101 | Launch, connect, heartbeat, lockup detection, shared-memory add/remove |
| `LLPluginProcessParentPollThread` | `llpluginprocessparent.cpp:58` | Optional read thread (`PluginUseReadThread`, default true, `newview/app_settings/settings.xml:9946`) polling all plugin sockets with one `apr_pollset` |
| `LLPluginMessagePipe` | `llpluginmessagepipe.h:66`; `.cpp` `MESSAGE_DELIMITER '\0'` at 36, `pumpOutput` 169 | Byte stream over `LLSocket` (APR), mutex-protected output queue |
| `LLPluginMessage` | `llpluginmessage.h:36`; `generate` 356 (`LLSDSerialize::toPrettyXML`), `parse` 372 (`LLSDSerialize::fromXML`) | Message = LLSD map with class, name, params; message classes in `llpluginmessageclasses.h:38-51` (`internal`, `base`, `media`, `media_browser`, `media_time`) |
| `LLPluginSharedMemory` | `llpluginsharedmemory.h:37`; POSIX `shm_open`/`mmap` at `.cpp:250-375`, Win32 `CreateFileMapping` at 379+ | Named segment per texture; parent creates, child maps (`shm_add`/`shm_remove`, `llpluginprocessparent.cpp:1163-1233`) |
| `LLPluginProcessChild` | `llpluginprocesschild.h:41`; state machine `.cpp:121-180`, sends `hello` at 140 | Child side; heartbeat |
| `LLPluginInstance` | `llplugininstance.h:50`; `apr_dso_load` at `.cpp:98`, entry point `LLPluginInitEntryPoint` (47) | Loads the plugin library inside SLPlugin; host↔plugin calls pass serialized message strings through function pointers |
| SLPlugin | `llplugin/slplugin/slplugin.cpp:134/136` | `main(launcher_port)`; loop `plugin->idle()` / `plugin->sleep()` (220-244); macOS Obj-C glue in `slplugin-objc.mm` |

**State machine and timeouts.** Parent states `STATE_UNINITIALIZED` →
`INITIALIZED` → `LISTENING` (bound loopback port, 400-510) → `LAUNCHED`
(process created on the `General` work queue with a main-thread callback,
527-560) → `CONNECTED` → `HELLO` → `LOADING` → `RUNNING` → `GOODBYE`/
`EXITING`/`CLEANUP`/`DONE` with `LAUNCH_FAILURE` and `ERROR` paths.
Launch timeout 60 s and lockup (heartbeat) timeout 15 s
(`llpluginprocessparent.cpp:106-107`, check at 1317).

**Rust touchpoint.** Every plugin message is LLSD XML. `LLSDXMLParser::doParse`
(`indra/llcommon/llsdserialize.cpp:865`) parses through the Rust LLSD rung
(`parseWithRust(VKS_LLSD_XML, …)` at 876), and `llcommon` links
`vkstorm_ffi` (`indra/llcommon/CMakeLists.txt:322`). SLPlugin links
`llcommon` (`llplugin/slplugin/CMakeLists.txt`), so the plugin child process
already runs Rust code for message parsing on both ends.

**Origin.** LL upstream (2008-2010 design). FS changes are small
(`<FS` markers: 4 in `llpluginprocessparent.cpp`, 2 in
`llpluginclassmedia.cpp`). Rung six removed the Vulkanstorm-only
`media_audio`/`media_music` message protocol from `llpluginclassmedia` and
`llpluginmessageclasses.h` (see Rust port status).

### Media plugins (`indra/media_plugins`)

| Plugin | Main file | 3p engine | Platforms built (`media_plugins/CMakeLists.txt`) | Notes |
|---|---|---|---|---|
| base | `base/media_plugin_base.cpp` (`staticReceiveMessage` 107, `LLPluginInitEntryPoint` 194); `media_plugin_base.h:36` | — | all | `MediaPluginBase`: message dispatch, shared-memory bookkeeping |
| CEF | `cef/media_plugin_cef.cpp` (1,266 lines; `dullahan` object at 127, created 168) | Dullahan/CEF 139 | Windows, macOS, Linux | Web browser for MOAP, profiles, search, help, login page. `VolumeCatcher` (`cef/volume_catcher.h:36`): Windows `waveOutSetVolume` on the process (`windows_volume_catcher.cpp`), Linux PulseAudio introspection (`linux_volume_catcher.cpp:44-46`), macOS |
| libVLC | `libvlc/media_plugin_libvlc.cpp` (`libvlc_new` 203, video callbacks 435, `libvlc_audio_set_volume` 508, `init_media_plugin` 789) | VLC 3.0.21 | Windows, macOS | Video and, with `UseMediaPluginsForStreamingAudio`, music. VLC plays its audio to its own output |
| GStreamer 1.0 | `gstreamer10/media_plugin_gstreamer10.cpp` (980 lines) | GStreamer 1.6.3 (symbols loaded via `llmediaimplgstreamer_syms*`) | Linux | Linux video/music; named `media_plugin_gstreamer` in `skins/default/xui/en/mime_types_linux.xml` |
| GStreamer 0.10 | `gstreamer010/` | — | not built | dead code |
| example | `example/media_plugin_example.cpp` | — | not built | referenced once by `mime_types.xml` |

MIME routing: `newview/skins/default/xui/en/mime_types.xml` maps 23 types to
`media_plugin_cef`, 16 to `media_plugin_libvlc`; `mime_types_linux.xml` maps
20 to CEF and 17 to `media_plugin_gstreamer`
(`grep -oiE "media_plugin_[a-z0-9]+" <file> | sort | uniq -c`).
`LLViewerMediaImpl::newSourceFromMediaType` (`llviewermedia.cpp:1796`)
resolves the launcher (`gDirUtilp->getLLPluginLauncher()`, 1846) and plugin
file and calls `LLPluginClassMedia::init` (1904).

### Viewer media layer (`indra/newview`)

| Class | Location | Role | Origin |
|---|---|---|---|
| `LLViewerMedia` (singleton) | `llviewermedia.h:74`; `initSingleton` 237, `setVolume` 486, `updateMedia` 661 | Owns all `LLViewerMediaImpl`s; per-frame priority assignment (743-850) by distance, visibility, focus and the max-instances limits; the spare browser instance (1553) | LL |
| `LLViewerMediaImpl` | `llviewermedia.h:189`; `initializeMedia` 1722, `newSourceFromMediaType` 1796, `initializePlugin` 1933, `updateVolume` 2206, `navigateTo` 2610, `update` 2932, `doMediaTexUpdate` 3105, `updateMediaImage` 3148, `handleMediaEvent` 3393 | One media instance: MIME discovery, plugin lifetime, texture upload from shared memory | LL (5 FS markers) |
| `LLViewerMediaTexture` | `newview/llviewertexture.*` (chapter on textures) | GL texture fed by `doMediaTexUpdate`; may run on the image thread when `LLImageGLThread::sEnabledMedia` (`llviewermedia.cpp:3023`) | LL |
| `LLMediaCtrl` | `llmediactrl.h:42`; `navigateTo` 669, `draw` 837, `handleMediaEvent` 1044 | UI widget embedding a browser (`LLViewerMediaObserver`) | LL |
| `LLViewerParcelMedia` | `llviewerparcelmedia.h:41`; `update` 99, `play` 188, `stop` 260, `processParcelMediaCommandMessage` 372, `processParcelMediaUpdate` 508 | Parcel media and parcel music URL changes; calls `LLViewerAudio::startInternetStreamWithAutoFade` (1188-1311) | LL + FS |
| `LLViewerParcelMediaAutoPlay` | `llviewerparcelmediaautoplay.h:34` | Auto-play policy timer | LL |
| `LLMediaDataClient`, `LLObjectMediaDataClient`, `LLObjectMediaNavigateClient` | `llmediadataclient.h:78, 305, 386`; caps `ObjectMedia` (`.cpp:668`), `ObjectMediaNavigate` (976) | Shared media (MOAP) fetch/update queues | LL |
| `LLViewerMediaFocus`, `LLPanelPrimMediaControls` | `llviewermediafocus.h:40`, `llpanelprimmediacontrols.h:44` | Focus/zoom and on-face controls for MOAP | LL |
| `LLPanelNearbyMedia`, media settings floaters/panels | `llpanelnearbymedia.*`, `llfloatermediasettings.*`, `llpanelmediasettings*.*`, `llpanellandmedia.*`, `llpanellandaudio.*` | UI | LL |
| `LLStreamingAudio_MediaPlugins` | `llviewermedia_streamingaudio.h:38`; `initializeMedia` `.cpp:174`, `updateMetadata` 190 | Music through a hidden plugin instance; stock upstream path again after rung six (no fade API) | LL + FS metadata |

**Threads.** All media control runs on the main thread (`LLViewerMedia::updateMedia`
as an idle callback). Media texture updates either run on the main thread or
are posted to the GL image thread (`llviewermedia.cpp:3023-3050`);
`doMediaTexUpdate` holds `mLock` so the plugin cannot be torn down during an
upload (3108). Plugin socket reads run on the plugin poll thread when
enabled.

### Voice (`indra/llwebrtc`, `indra/newview`)

**Module selection.** `LLVoiceClient` (`newview/llvoiceclient.h:368`,
`LLParamSingleton`) fronts two `LLVoiceModuleInterface` implementations
(`llvoiceclient.h:151`). `getVoiceModule` (`llvoiceclient.cpp:125`) returns
`LLVivoxVoiceClient` for `voice_server_type` "vivox" or empty and
`LLWebRTCVoiceClient` for "webrtc"; both are initialised (176-177) and device
calls fan out to both (e.g. 409-416). The spatial and non-spatial modules can
differ (`setSpatialVoiceModule` 259, `setNonSpatialVoiceModule` 278).

| Component | Location | Model | Origin |
|---|---|---|---|
| `llwebrtc` shared library | `llwebrtc/llwebrtc.h` (exports `init` 292, `terminate` 295, `getDeviceInterface` 298, `newPeerConnection` 301, `freePeerConnection` 302; `LLSYMEXPORT` 50-57) | Wraps Google libwebrtc m137. Interfaces: `LLWebRTCDeviceInterface` (132), `LLWebRTCAudioInterface` (180), `LLWebRTCDataInterface` (197), `LLWebRTCSignalingObserver` (219), `LLWebRTCPeerConnectionInterface` (258) | LL (2024-25); FS device-removal fixes |
| `LLWebRTCImpl` | `llwebrtc/llwebrtc_impl.h:419`; threads created `llwebrtc.cpp:296-302` | Three libwebrtc threads: network (with socket server), worker, signaling; tasks posted with `PostTask`/`BlockingCall` (`llwebrtc_impl.h:474-511`) | LL |
| `LLWebRTCAudioDeviceModule`, `LLWebRTCAudioTransport`, `LLCustomProcessor` | `llwebrtc_impl.h:180, 124, 388` | Viewer-owned audio device module wrapping libwebrtc's, for device selection, tuning and level metering | LL |
| `LLWebRTCVoiceClient` | `newview/llvoicewebrtc.h:62`; `init` `.cpp:276`, `initWebRTC` 282, `voiceConnectionCoro` 504 (launched 1754), `OnDevicesChanged` 750, `updatePosition` 1096, `sendPositionUpdate` 1227 | Coroutine-driven state machine; main-loop work queue (289) | LL (11 FS markers) |
| `LLVoiceWebRTCConnection`, `…SpatialConnection`, `…AdHocConnection` | `llvoicewebrtc.h:580, 724, 744` | One peer connection per region/parcel/ad-hoc channel; SDP/ICE via the `ProvisionVoiceAccountRequest` capability (`.cpp:2738`) over `LLCoreHttpUtil::HttpCoroutineAdapter` (2539, 2754); position, gain (`"ug"`) and mute (`"m"`) as `boost::json` over the data channel (1235-1262, 2685-2696) | LL |
| `LLVivoxVoiceClient` | `newview/llvoicevivox.h:69`; `.cpp` 8,118 lines | Launches the `SLVoice` daemon with `LLProcess` (`sGatewayPtr`, 257; exe path 1001) and speaks Vivox's XML protocol over a local TCP socket (`LLVivoxProtocolParser : LLIOPipe`, `.h:943`; expat) on `LLPumpIO` (285, 405). Linux can run `SLVoice.exe` under Wine (537) | LL + FS (28 markers) |
| `LLVoiceChannel*` | `newview/llvoicechannel.h:37, 140, 167, 180` | Proximal, group, P2P channel UI/state | LL |
| `LLSpeakerMgr` family, `LLVoiceVisualizer` | `newview/llspeakers.h:225-350`, `llvoicevisualizer.h` | Speaker lists, lip-sync/indicator | LL |

**Packaging.** The Vivox client is compiled unconditionally
(`newview/CMakeLists.txt:870`), the `slvoice` package is always installed
(`cmake/ViewerMiscLibs.cmake:20`), and the Windows manifest always stages
`SLVoice.exe`, `vivoxsdk_x64.dll`, `ortp_x64.dll`
(`newview/viewer_manifest.py:769-775`; `newview/CMakeLists.txt:2335-2345`).
`llwebrtc.dll` is staged at `newview/CMakeLists.txt:2316`.

### Crash reporting

| Part | Location | State |
|---|---|---|
| BugSplat (Windows/macOS) / Breakpad (Linux) | `cmake/bugsplat.cmake`; `newview/bugsplatattributes.cpp`, `llappviewer*.cpp` | `USE_BUGSPLAT` requires `INSTALL_PROPRIETARY` and `BUGSPLAT_DB`; CI Release sets `-DUSE_BUGSPLAT:BOOL=OFF` and an empty DB (`.github/workflows/build.yaml:165-168`) |
| `linux-crash-logger` | `linux_crash_logger/linux_crash_logger.cpp` | FLTK prompt + libcurl upload of a minidump to `<db>.bugsplat.com`; launched from `newview/llappviewerlinux.cpp:221, 451`; staged as `linux-crash-logger.bin` (`viewer_manifest.py:2348`). `llcrashlogger` link commented out in its CMake |
| `llcrashlogger` library | `llcrashlogger/llcrashlogger.h:57` (`LLCrashLogger : LLApp`), `llcrashlock.h:41`, `llcrashlookup.h:28` | Built when BugSplat is off (`indra/CMakeLists.txt:154-156`) but no target links it; only its own sources include its headers |
| `win_crash_logger/`, `mac_crash_logger/` | — | Not built |

### Login and update (`indra/viewer_components`, `indra/newview`)

| Part | Location | Notes | Origin |
|---|---|---|---|
| `LLLogin` | `viewer_components/login/lllogin.h:36`; `Impl::connect` `.cpp:123`, `loginCoro` 135 | Coroutine posting the login request to the `LLXMLRPCTransaction` event pump and reporting progress on its own pump; unit test `login/tests/lllogin_test.cpp` | LL |
| `LLLoginInstance` | `newview/lllogininstance.h`; owns `LLLogin` (`.cpp:83`) | ToS/critical-message handling, login parameters | LL + FS |
| `LLPanelLogin`, `FSPanelLogin` | `newview/llpanellogin.cpp`, `fspanellogin.cpp` | Login UI; the login page itself is a `LLMediaCtrl` (CEF) | LL / FS |
| Velopack updater | `newview/llvelopack.cpp` (1,248 lines), `llvelopack.h`; `cmake/Velopack.cmake` (`USE_VELOPACK`, default OFF) | LL's 2025 updater path; off in CI (`build.yaml:166`) | LL |
| `llvvmquery.cpp` | `newview/llvvmquery.cpp` (189 lines) | Viewer version manager query | LL |
| `viewer-manager` package | `cmake/ViewerManager.cmake:2` | LL's Python launcher/update checker | LL |

Login is covered in more depth by the startup/network chapters; it is listed
here because the task brief places `viewer_components` in this chapter.

## Coupling

| From | To | Mechanism | Port relevance |
|---|---|---|---|
| `llaudio` | `vkstorm_ffi` (Rust) | 51 distinct `vks_audio_*`/`vks_music_*` functions called from `llaudio/*.cpp` | The rung-six boundary |
| `llaudio` | asset system (`llmessage`, `llfilesystem`) | `LLAudioData` requests via `gAssetStorage`; decode reads the cache with `LLFileSystem` and writes `.dsf` via `LLLFSThread` (`llaudiodecodemgr.cpp`) | Couples to rung five (asset pipeline); the parity adapter cannot construct `LLAssetStorage` without a live `LLMessageSystem` (comment in `tests/llaudio_parity_decode_adapter.cpp:12-17`) |
| `llaudio` | `llcommon` work queues | `mainloop`/`General` `LL::WorkQueue` | Shared threading infrastructure |
| `llaudio` ↔ `llwebrtc` | `gAudioDeviceMutex` | 1 s timed lock around device changes | Cross-module synchronisation (see Risks) |
| `newview` (≈25 files) | `gAudiop` | Global pointer, 220 uses | Any further audio port keeps `LLAudioEngine`'s interface |
| UDP messages | `llaudio` | `SoundTrigger`, `PreloadSound`, `AttachedSound`, `AttachedSoundGainChange` handlers in `llviewermessage.cpp` | Message template rung |
| Gestures | sound via simulator | `send_sound_trigger` | No direct audio coupling |
| `llviewerparcelmedia`, `llviewermedia`, `llstatusbar`, `llpanelnearbymedia`, `chatbar_as_cmdline` | `LLViewerAudio` | `startInternetStreamWithAutoFade` (call sites listed by `grep -n startInternetStreamWithAutoFade newview/*.cpp`) | Music policy entry points |
| Music metadata consumers (FS) | `LLStreamingAudioInterface` | `boost::signals2` signal | Callback from Rust is turned into LLSD on the main thread (`llstreamingaudio_vks.cpp:240-259`) |
| `llplugin` | `llcommon` (APR sockets, `LLProcess`, `LLSD`), `llmessage` (`LLSocket`), `llxml` | Direct | APR is the transport; LLSD XML parse is already Rust |
| `media_plugins` | `llplugin` (`LLPluginInstance`, `LLPluginMessage`), `llcommon` | Static link into each plugin library | A Rust host must keep this message protocol or rebuild every plugin |
| `llviewermedia` | textures/GL (`LLViewerMediaTexture`, `LLImageGL`, `LLImageGLThread`) | Shared-memory pixels → GL subimage upload | Couples media to the texture and renderer rungs |
| `llviewermedia` | UI (`LLMediaCtrl`, floaters), cookies (`setCookie`, OpenID at 1278-1389), MIME types (`LLMIMETypes`) | Direct | Large UI surface |
| `llmediadataclient` | capabilities (`ObjectMedia`, `ObjectMediaNavigate`), `LLViewerObject`/`LLVOVolume` | HTTP via `LLCoreHttpUtil` | Object store rung |
| `llvoicewebrtc` | `llwebrtc` (DLL C++ interface), `LLCoreHttpUtil`, capabilities, `LLVOAvatar`, `boost::json`, `LLCoros` | Direct | The `llwebrtc` boundary is already a narrow exported C++ interface |
| `llvoicevivox` | `LLPumpIO`, `LLIOPipe`, expat, `LLProcess` | Direct | Legacy I/O stack |
| `llvoiceclient` | both voice modules, `LLSpeakerMgr`, `LLVoiceChannel`, UI | Direct, broad | — |
| `lllogin` | `LLEventPumps`, `LLCoros`, `LLXMLRPCTransaction` | Event-pump API | Narrow |

## Rust port status

### Audio rung (rung six) — done

Authoritative description: `doc/rust_port.md`, "Rung six: the audio engine"
(lines 1505-1878). Summary of what is in this tree:

**Build wiring.** `indra/cmake/VKSAudio.cmake` (`USE_VKS_AUDIO`,
`LL_VKS_AUDIO=1`, `use_prebuilt_binary(ffmpeg)`); `indra/llaudio/CMakeLists.txt`
adds the `*_vks.*` sources and links `vkstorm_ffi` and `ll::vorbis`
unconditionally (the Rust decoder is used by every backend; libvorbis stays
for `LLVorbisEncode` and the oracle). `indra/llaudio/tests/audio_backend_selection.cmake`
checks the selection rules. Rust sources: `vkstorm-core-rust/crates/vkstorm-audio`
(`src/{asset.rs, engine.rs, music.rs, wav.rs, mixer/, device/, stream/}`),
`crates/vkstorm-vorbis`, C ABI `crates/vkstorm-ffi/src/audio.rs`
(48 `vks_audio_*`/`vks_music_*` functions; 28,902 lines of Rust in the two
audio crates, `find crates/vkstorm-audio crates/vkstorm-vorbis -name '*.rs' | xargs cat | wc -l`).

**Call sites** (from
`grep -rnoE "vks_(audio|music)_[a-z_0-9]+" indra/llaudio`):

| C++ site | Rust functions |
|---|---|
| `LLAudioEngine_VKS::init` / `initNull` (`llaudioengine_vks.cpp:94, 123`) | `vks_audio_create` (103), `vks_audio_create_null` (126), `vks_audio_last_error` |
| `backendStarted`, `setInternalGain` (87, 364) | `vks_audio_set_global_volume` (90, 368) |
| `getDevices`, `setDevice` (138, 161) | `vks_audio_devices` (147), `vks_audio_set_device` (208) |
| `getDriverName` (235) | `vks_audio_engine_info` (243), `vks_audio_engine_version` (247), `vks_audio_format` (253) |
| `shutdown` (287) | `vks_audio_destroy` (298) |
| `idle` (308) | `vks_audio_poll` (328) |
| `initWind`, `cleanupWind`, `updateWind` (372, 387, 396) | `vks_audio_wind_init` (381), `vks_audio_wind_cleanup` (391), `vks_audio_wind_targets` (413) |
| declicked stop helper (≈420-427) | `vks_audio_voice_fade_volume`, `vks_audio_voice_schedule_stop` |
| `LLAudioChannelVKS::applyVoiceParams`, `startVoicePaused`, `play`, `playSynced`, `isPlaying`, `updateLoop`, `update3DPosition` (459-670) | `vks_audio_play` (500), `vks_audio_play_3d` (508), `vks_audio_voice_set_{volume,looping,pause,3d,doppler_factor,inverse_distance_rolloff,inaudible_behavior}`, `vks_audio_voice_{is_valid,stream_position,seek,loop_count}`, `vks_audio_wav_length`, `vks_audio_update_3d` (524) |
| `LLAudioBufferVKS` (678-723) | `vks_audio_wav_load` (701), `vks_audio_wav_free` (686, 693), `vks_audio_wav_sample_count` (723) |
| `LLListener_VKS::commitDeferredChanges` (`lllistener_vks.cpp:42`) | `vks_audio_set_listener`, `vks_audio_update_3d` |
| `LLStreamingAudio_VKS` (`llstreamingaudio_vks.cpp:101-395`) | `vks_music_create`, `_start`, `_stop`, `_pause`, `_is_playing`, `_state`, `_set_volume`, `_set_muted`, `_set_envelope`, `_transition`, `_transition_state`, `_set_spatial_layout`, `_poll_metadata` (240) |
| `LLVorbisDecodeState` (`llaudiodecodemgr.cpp:178-234`) | `vks_audio_decode_asset` (178), `vks_audio_bytes_free` (188, 194), `vks_audio_asset_vendor` (234) |
| Tests only | `vks_audio_render_null`, `vks_audio_set_null_clock`, `vks_audio_voice_count`, `vks_music_stats` |

Exported but not called by the viewer (`comm -23` of the ABI list against
the viewer's uses): `vks_audio_frames_played`, `vks_audio_global_volume`,
`vks_audio_restart`, `vks_music_current_metadata`, `vks_music_url`.

**Decoders.** Sound assets: Vorbis in pure Rust (`vkstorm-vorbis`),
bit-identical to libvorbisfile per platform. Parcel music: MP3, MP2, AAC-LC,
FLAC via symphonia 0.6.1; Vorbis via `vkstorm-vorbis`; HE-AAC v1/v2 and Opus
via FFmpeg 9.0.2 (decoder-only LGPL: `aac` and `opus` decoders, libavutil,
libswresample; `avcodec-63` loaded at run time from the viewer directory on
Windows or `lib/` on Linux; no demuxers — Ogg and ADTS are demuxed in Rust).
HTTP for streams is libcurl through `vkstorm-http`. All per `doc/rust_port.md`
rung six "Scope" and "FFmpeg".

**What stayed C++.** `LLAudioEngine` and its source/channel/priority logic,
the `*_vks.*` adapters, `LLVorbisEncode`, the decode work-queue plumbing,
`llvieweraudio.cpp`'s fade policy, every `newview` consumer. OpenAL and FMOD
Studio backends are unchanged compile-time options.

**What was removed** (doc/rust_port.md 1814-1848; `doc/vlc_audio_*.md`,
`doc/soloud_audio.md`):
- SoLoud (package, `USE_SOLOUD`, `LLAudioEngine_SoLoud`, the headless
  `llsoloud_adapter_test`; `scripts/validate_soloud.sh` became
  `scripts/validate_audio.sh`).
- The VLC PCM bridge and WASAPI endpoint code (`llvlcaudio.*`,
  `llvlcaudiobridge.*`, `llvlcwasapi.h`, `llvlcendpoint.h`,
  `prepare_miniaudio.cmake`, `LL_VLC_PCM_AUDIO`).
- The VLC speaker-fill filter and output module (`vlc_speakerfill.cpp`,
  `prepare_speakerfill.cmake`, `llvlcspeakerfillconfig.*`, the MinGW build).
- The Vulkanstorm `media_audio`/`media_music` plugin protocol in
  `llpluginclassmedia`, `llpluginmessageclasses.h`,
  `media_plugin_libvlc.cpp` and `llviewermedia.cpp` (object/nonpositional
  roles, spatial projection, `LL_VLC_AUDIO_PROTOCOL_TESTS`,
  `llviewermedia_audio_test.cpp`).
- Fade/mute/metadata extensions in `LLStreamingAudio_MediaPlugins`.

Commit trail (`git log --oneline -- indra/llaudio indra/media_plugins indra/llplugin …`):
`25ac720b5b`, `b0898eeb15`, `efea51f085` (record corpora), `f726c7185a`,
`98e10be654`, `aaccc064c2`, `203689437a` (removals), `6ac866262b` (Rust
asset decode), `31767dc796` (Rust engine default), `bd4f21a117` (music
through Rust), `b199713085` (tests), later fixes `fe06e2f42b`, `33c2b36728`,
`26289317e5`.

**Kept in the VLC plugin** (general fixes, Vulkanstorm): callbacks store
atomics only, single `stopPlayer()` detaching events and releasing
player/media, null-safe `resetVLC()`, `STATUS_ERROR` on error paths,
FreeBSD `--aout=oss` (`media_plugin_libvlc.cpp:191`).

### Other parts of this chapter — not started

No `vks_*` calls exist in `llplugin`, `media_plugins`, `llwebrtc`,
`viewer_components`, the crash loggers, or the `newview` media/voice files
(`grep -rln "vks_" indra/llplugin indra/media_plugins indra/llwebrtc indra/viewer_components indra/newview/llviewermedia*.cpp indra/newview/llvoice*.cpp`
returns nothing). The only Rust these components execute is the LLSD XML
parse in plugin messages (see "Rust touchpoint" above) and whatever HTTP
and LLSD rungs their HTTP calls use. `doc/rust_port.md`'s rung order lists
no media, plugin or voice rung.

## Port assessment

| Part | Recommendation | Reasoning | Boundary | Prerequisites |
|---|---|---|---|---|
| `LLAudioEngine` application logic (sources, priorities, queued/synced sounds, transfers) | Defer | Rung six judged it application logic over `LLUUID`/asset system with no data transformation (doc/rust_port.md, keep-or-port table). Porting it means porting `LLAudioSource` consumers in `newview` (≈25 files) | `LLAudioEngine` interface stays | Asset pipeline and object store in Rust |
| `LLVorbisEncode` | Keep C++ | No Rust encoder reproduces libvorbisenc's psychoacoustic model (rung six table) | — | — |
| OpenAL / FMOD backends | Keep as-is, or retire | They are compile-time options; macOS cannot build the default engine (`VKSAudio.cmake:19-21`), so OpenAL or FMOD is the only macOS path | — | Decide macOS support (Inference: a CoreAudio device in `vkstorm-audio` would be needed to retire them) |
| `llvieweraudio` fade policy | Keep C++ for now | Small state machine coupled to startup state, settings, teleport events; the sample-timed part is already Rust | `LLStreamingAudioInterface` fade API | — |
| Plugin IPC host (`LLPluginProcessParent`, `LLPluginMessagePipe`, `LLPluginSharedMemory`, `LLPluginProcessChild`, SLPlugin) | Candidate (medium value) | ≈7,000 lines of self-contained LL code with a clear wire protocol: loopback TCP, NUL-delimited LLSD XML messages, named shared memory, heartbeat, a documented state machine. It is security-relevant (it parses data from a process that renders untrusted web content) and its parser is already Rust. Inference: a Rust host could be built and tested against the current C++ plugins without touching them, because the wire format is fixed | Keep `LLPluginClassMedia`'s C++ interface as the wrapper; port `LLPluginProcessParent` + pipe + shm behind a C ABI; keep message names and LLSD shapes identical | An oracle: record message transcripts and state transitions from the C++ host (no such corpus exists today); APR replacement for sockets/polling on the Rust side; a decision on whether the child side (SLPlugin, linked into every plugin) moves too |
| `LLPluginClassMedia` (media API, 1,657 lines) | Defer; keep as wrapper | Mostly message construction and event dispatch; its value is its interface, which `newview` and every plugin depend on | Stays C++ | — |
| CEF plugin | Keep | A wrapper around Chromium/Dullahan (Chromium is tens of millions of lines). Porting `media_plugin_cef.cpp` (1,266 lines) gains nothing; the risk lives in CEF itself | Plugin protocol | — |
| libVLC plugin, GStreamer plugin | Keep | Wrappers around VLC 3.0.21 and GStreamer 1.6.3; video decode is the engines' job. Music no longer needs them by default | Plugin protocol | — |
| Volume catchers (CEF) | Keep | Platform glue (`waveOutSetVolume`, PulseAudio) | — | — |
| `gstreamer010/`, `example/`, `win_crash_logger/`, `mac_crash_logger/`, `llcrashlogger` | Delete rather than port (Inference) | Not built or not linked (see Scope and Risks) | — | Confirm no packaging step references them |
| Viewer media layer (`llviewermedia`, `llviewerparcelmedia`, `llmediadataclient`, MOAP UI) | Defer | ≈16,900 lines of policy tightly coupled to UI, GL textures, objects, capabilities and cookies. Candidate sub-pieces once their dependencies are ported: `LLMediaDataClient` queues (pure request scheduling over caps; has a disabled unit test), the priority/instance-limit computation in `LLViewerMedia::updateMedia` (743-850) | `LLViewerMediaImpl` and `LLViewerMedia` interfaces | Object store, texture and UI rungs |
| `llwebrtc` | Keep | A wrapper around libwebrtc (m137), which is far larger than the wrapper and has no Rust replacement of comparable maturity (Inference). It already presents a narrow exported interface (`llwebrtc.h:292-302`) | Existing DLL interface | — |
| `LLWebRTCVoiceClient` (3,486 + 762 lines) | Defer; possible later candidate | Coroutine state machine over capabilities, ICE/SDP exchange and a JSON data-channel protocol. Inference: the data-channel JSON (position, gain, mute, participant updates) is a pure, testable codec that could be ported in isolation | `LLVoiceModuleInterface` | HTTP/coroutine story on the Rust side; recorded data-channel transcripts |
| `LLVivoxVoiceClient` (8,118 + 1,099 lines) | Keep frozen; do not port | Legacy protocol to a proprietary daemon; Second Life uses WebRTC; `doc/rust_port.md:45-47` says Vivox is for OpenSim only | — | Decide whether Release keeps shipping it (see Risks) |
| `LLVoiceClient`, channels, speakers | Defer | UI/application glue | — | — |
| Gestures | Defer | Inventory/animation/chat coupling; sound path already goes through the simulator | — | Inventory rung (in progress on this branch) |
| `lllogin` | Low value | 507 lines (`.cpp`+`.h`) of coroutine glue over the XML-RPC transaction; the transport is already ported (HTTP rung) | Event pump API | — |
| Crash reporting | Keep (3p) | BugSplat/Breakpad are external; `linux-crash-logger` is a 191-line uploader | — | — |
| Updater (Velopack, viewer-manager) | Keep (3p) | Off in CI Release; Vulkanstorm ships Inno Setup (`doc/rust_port.md:44`) | — | — |

**Summary.** Of the roughly 86,000 lines in this chapter, the large
third-party wrappers (CEF, VLC, GStreamer, libwebrtc, Vivox) and the
UI-heavy viewer media and voice layers are poor port targets. The plugin
IPC host is the clearest self-contained candidate; the WebRTC data-channel
codec and `LLMediaDataClient`'s queues are smaller candidates after their
dependencies move.

## Existing tests and oracles

| Test / oracle | Location | What it covers | In CI |
|---|---|---|---|
| `INTEGRATION_TEST_llaudio_parity` | `indra/llaudio/tests/llaudio_parity_test.cpp` (3 TUT tests at 1685, 1787, 1858), adapters `llaudio_parity_vks_adapter.cpp`, `llaudio_parity_decode_adapter.cpp` (which `#include`s `../llaudiodecodemgr.cpp`), `llaudio_parity_adapter.h` | Mixed-scene corpus (`fixtures/llaudio_mix_{2,6,8}ch.vksa`, `llaudio_parity_scenes.txt`, `llaudio_mix_observables.txt`), decode corpus (`fixtures/decode/`, `llaudio_decode_parity.txt`, 58 assets), speaker-fill matrices (`fixtures/speakerfill_matrices.txt`) | Yes (`.github/workflows/build.yaml:222`) |
| `INTEGRATION_TEST_llaudio_music` | `indra/llaudio/tests/llaudio_music_test.cpp` (6 TUT tests, 309-500); server `indra/newview/tests/vksaudio_mock_stream_server.py`; fixtures `indra/newview/tests/fixtures/music/` (made by `make_fixtures.sh`) | Every music format, fades on the device clock, ICY, chained Ogg, PLS/M3U, reconnect, refusals (`video.mp4`, `video.ogv`, `page.html`), 5.1 fill | Yes (same line) |
| Backend selection | `indra/llaudio/tests/audio_backend_selection.cmake` | `USE_VKS_AUDIO`/OpenAL/FMOD rules | No; run by `scripts/validate_audio.sh:10` (`cmake -P`) |
| Scene generator | `indra/llaudio/tests/fixtures/make_llaudio_parity_scenes.py` | Writes the scene script | — |
| Speaker-fill recorder | `indra/llaudio/tests/speakerfill_record.cpp` | Recorded `speakerfill_matrices.txt` from the removed VLC modules; not referenced by any `CMakeLists.txt` | No (cannot be rebuilt: the modules it loads are removed) |
| Rust-side audio tests | `vkstorm-core-rust/crates/vkstorm-audio/tests/` (`engine_adapter.rs`, `mixer_parity.rs`, `asset_decode.rs`, `wav_reader.rs`, `speakerfill_record.rs`, `speakerfill_vlc.rs`, `stream_formats.rs`, `stream_behaviour.rs`, `stream_decode_cpu.rs`, `device_callback.rs`, `device_hardware.rs`, `engine_null.rs`, `mixer_behaviour.rs`); oracles `tools/soloud-oracle`, `tools/vorbis-oracle` | SoLoud oracle scenes, WAV and Vorbis parity | vkstorm-core-rust CI |
| Login music gate | `scripts/perf/test_login_music_gate.py` | `llvieweraudio` login hold policy | Not in the focused list (Inference from `build.yaml:216-226`) |
| Audio validation script | `scripts/validate_audio.sh` | Manual/local validation | No |
| `lllogin` unit test | `indra/viewer_components/login/tests/lllogin_test.cpp` (343 lines) | `LLLogin` coroutine against a fake XML-RPC pump | Built with `LL_TESTS` (`LL_ADD_PROJECT_UNIT_TESTS`) |
| `lllogininstance` unit test | `indra/newview/tests/lllogininstance_test.cpp` | `LLLoginInstance` | Built with `LL_TESTS` (`newview/CMakeLists.txt:3034`) |
| `llmediadataclient` unit test | `indra/newview/tests/llmediadataclient_test.cpp` | MOAP queues | **Disabled** (`newview/CMakeLists.txt:3033` commented out) |
| Plugin host, media plugins, `llwebrtc`, voice clients, gestures, crash loggers | — | — | No tests in the tree (`git ls-files indra/llplugin | grep -i test` is empty) |

Oracle model note: unlike rungs that keep a frozen C++ copy linked into the
test, the audio rung's oracles are **recorded corpora** (the SoLoud and VLC
code is gone) plus, on Linux, libvorbisfile linked in the same process for
the decode comparison (doc/rust_port.md, "Parity corpora"). The only
C++-side SoLoud reproduction is `vkstorm-core-rust/tools/soloud-oracle`.

## Risks and open questions

1. **Vivox is still built and shipped in Release.** `doc/rust_port.md:45-47`
   states that Vivox is not part of the Release. In this tree
   `llvoicevivox.cpp` is compiled unconditionally (`newview/CMakeLists.txt:870`),
   `use_prebuilt_binary(slvoice)` is unconditional
   (`cmake/ViewerMiscLibs.cmake:20`), `getVoiceModule` returns the Vivox
   client for an empty server type (`llvoiceclient.cpp:127`), and the
   Windows manifest always stages `SLVoice.exe`, `vivoxsdk_x64.dll` and
   `ortp_x64.dll` (`viewer_manifest.py:769-775`). No `OPENSIM` guard was
   found around any of these. Either the doc describes intent not yet
   implemented, or the exclusion lives somewhere this survey did not find.
2. **The audio/voice device mutex is probably two mutexes on Windows.**
   `gAudioDeviceMutex` is an `inline` variable defined in
   `indra/llcommon/inlinemutexs.h:38` (used by `llaudio`) and defined again
   in `indra/llwebrtc/llwebrtc.cpp:51`, and `llwebrtc` is a `SHARED`
   library (`llwebrtc/CMakeLists.txt:25`). Inference: an inline variable
   is only merged within one linked image, so on Windows `llwebrtc.dll` and
   the viewer executable each have their own mutex and the FS FIRE-36022
   synchronisation does not cross the DLL boundary; on Linux it depends on
   symbol visibility. This predates the Rust port (FS code) but the rung-six
   doc lists "the 1 s device mutex shared with voice" as kept behaviour.
   Needs a check (e.g. compare the addresses at run time).
3. **macOS cannot use the default audio engine.** `USE_VKS_AUDIO` defaults
   ON on every platform but `VKSAudio.cmake:18-21` fails configuration on
   anything but Windows/Linux x64; `vkstorm-ffi` and `ffmpeg` packages exist
   only for windows64/linux64. A macOS build must pass `--openal` or
   `--fmodstudio`.
4. **Linux media plugins are old.** `gstreamer10` is a 2016 1.6.3 package
   from `3p.firestormviewer.org`; there is no VLC on Linux. Music is
   unaffected by default (Rust), but video and
   `UseMediaPluginsForStreamingAudio` on Linux depend on it.
5. **Dead code.** `media_plugins/gstreamer010/` (2,305 lines),
   `media_plugins/example/`, `win_crash_logger/`, `mac_crash_logger/`
   (`.DS_Store` only), `llaudio/lllistener_ds3d.h`, and the `llcrashlogger`
   library (built when BugSplat is off — which is CI's Release configuration
   — but linked by nothing). `llcrashlogger` therefore costs build time in
   every CI Release build. Open question: remove, or document why they are
   kept.
6. **No tests for the plugin host or voice.** Any port of `llplugin` or of
   the WebRTC client would first need recorded transcripts (message
   sequences, state transitions, shared-memory negotiation, data-channel
   JSON). None exist.
7. **Speaker-fill oracle cannot be re-recorded.** `speakerfill_record.cpp`
   depends on VLC modules removed in `aaccc064c2`; the recorded
   `speakerfill_matrices.txt` is now the only reference. Re-recording would
   require checking out the pre-removal history.
8. **Stale comments and paths.** `doc/rust_port.md` rung six names
   `indra/rust/vkstorm-ffi/src/audio.rs` and `indra/rust/include/vkstorm_ffi.h`;
   `indra/rust/` no longer exists (the doc's build section, lines 37-42,
   explains the move to vkstorm-core-rust). The decode adapter's header
   comment (`tests/llaudio_parity_decode_adapter.cpp:17-18, 45`) still says
   decode reads "through libvorbisfile callbacks", which describes the
   pre-port path; the code it includes now calls `vks_audio_decode_asset`.
9. **Unused ABI.** Five exported functions (`vks_audio_frames_played`,
   `vks_audio_global_volume`, `vks_audio_restart`,
   `vks_music_current_metadata`, `vks_music_url`) have no viewer caller.
   Open question: test-only, future, or removable.
10. **FMOD package entry is inconsistent.** The version key says `2.02.20`
    while the darwin URL names `fmodstudio-2.03.12` at a local `file:///`
    path; FMOD builds are not reproducible from `autobuild.xml` alone
    (proprietary, by design upstream in FS).
11. **Plugin message parsing crosses into Rust in an untrusted context.**
    SLPlugin hosts CEF, which renders arbitrary web content, and the
    viewer parses that process's messages with the Rust LLSD XML parser.
    This is likely an improvement, but the LLSD rung's corpora were not
    built from plugin traffic (Inference from `doc/rust_port.md` rung two;
    not verified in detail here).
12. **Physical audio acceptance is manual.** Per `doc/soloud_audio.md`,
    listening on speakers/headphones, unplug/replug and default-device moves
    are not automated; the automated checks use the null device.
13. **Upstream merges.** `llwebrtc`, `llvoicewebrtc` and `llviewermedia`
    change often upstream (LL 2024-26) and carry FS patches; any port of
    them would make upstream merges harder. The audio rung already diverges
    from upstream `llaudio` in the `*_vks.*` files and in
    `llaudiodecodemgr.cpp`.

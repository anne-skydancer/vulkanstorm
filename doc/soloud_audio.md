# SoLoud audio backend (replaced)

SoLoud is no longer part of the viewer. Since vkstorm-rust rung six the
default audio backend is the Vulkanstorm audio engine (`vkstorm-audio`,
Rust), which ports the parts of SoLoud e82fd32c1f the viewer used (with the
3p-soloud package's bounded mixing and device-selection patches) operation
for operation, and reproduces SoLoud's output bit for bit on the recorded
scenes. See `doc/rust_port.md`, "Rung six: the audio engine", for the
scope, the parity corpora and the measurements.

What changed for builders:

- `USE_SOLOUD` is gone; `USE_VKS_AUDIO` (`indra/cmake/VKSAudio.cmake`)
  selects the Rust engine and is the default unless OpenAL or FMOD Studio is
  selected. The configure script's `--soloud` is now `--vksaudio`;
  `--openal` and `--fmodstudio` are unchanged, and the last named backend
  option wins as before. A conflicting cached selection still fails
  configuration explicitly.
- The `soloud` autobuild package is no longer used. The engine needs the
  decoder-only LGPL `ffmpeg` package (3p-ffmpeg) for HE-AAC and Opus parcel
  music; it is loaded at run time and staged next to the viewer.
- `scripts/validate_soloud.sh` is `scripts/validate_audio.sh`; the headless
  `llsoloud_adapter_test` is gone. Its cases run against the Rust engine in
  vkstorm-audio's `tests/engine_adapter.rs`, and the viewer's
  `INTEGRATION_TEST_llaudio_parity` drives the C++ engine classes against
  the corpora recorded from SoLoud before the port.

What users keep: the device selection by endpoint identity (the saved device
UUIDs are derived as before, so a selected device stays selected), the
device list refresh, the restart and restore-previous rules, the declicked
stops, synced starts, the 64-voice limit, the 7.1 order, the attenuation,
rolloff and Doppler. Two SoLoud defects are gone: more than 64 voices that
keep ticking while inaudible crashed it, and after a device switch it mixed
with denormals.

Physical acceptance (listening on speakers and headphones, unplug and replug,
default-device moves) is still a manual step; the automated checks run on a
null device and open real endpoints briefly.

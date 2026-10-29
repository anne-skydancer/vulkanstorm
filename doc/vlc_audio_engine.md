# Plugin-owned VLC PCM audio engine (removed)

The retired plugin-side PCM engine documented here (VLC `amem` callbacks
into a private miniaudio/WASAPI render loop with an endpoint-consumption
fence: `llvlcaudio`, `llvlcaudiobridge`, `llvlcwasapi.h`, `llvlcendpoint.h`,
`prepare_miniaudio.cmake`, `LL_VLC_PCM_AUDIO`, `LL_VLC_AUDIO_BRIDGE_TESTS`)
was compiled out before vkstorm-rust rung six and has now been removed; its
history is in git.

Its problem, that the plugin process owned the device so the viewer could not
know when a faded sample had been heard, is gone: the audio engine
(`vkstorm-audio`) owns the output device and completes a music fade when its
final sample has passed the device clock. See `doc/rust_port.md`, rung six.

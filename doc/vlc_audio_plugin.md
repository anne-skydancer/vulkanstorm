# VLC plugin music audio (removed)

The VLC-side music work documented here (the `speakerfill` audio filter and
the patched WASAPI output module built with MinGW, `prepare_speakerfill.cmake`,
`llvlcspeakerfillconfig`, the native fade envelope and its completion fence,
the `media_music` capability) has been removed in vkstorm-rust rung six; its
history is in git. The VLC media plugin is the stock upstream plugin plus
general lifecycle and thread-safety fixes, listed in `doc/rust_port.md`,
rung six, "What was removed from the VLC plugin, and what was kept".

The speaker fill of stereo parcel music now lives in the audio engine's
music bus, on Windows and Linux: stereo to all mains at 0.7071, the centre
at 0.3536 x (L+R), LFE silent, native multichannel passed through, gated by
the device's channel mask, the headphone form factor and FSMusicSpatialSound
(0, 21, 41, 51, 71; rear pair if the device has it, else side pair). The
setting applies immediately. The filter's recorded matrices, negotiation
decisions, gain curve and envelopes (`indra/llaudio/tests/fixtures/
speakerfill_matrices.txt`) are the oracle of vkstorm-audio's
`tests/speakerfill_record.rs`, and the cases of the removed
`vlc_speakerfill_test.cpp` are transcribed in `tests/speakerfill_vlc.rs`.

# VLC music path (stock)

The viewer-owned VLC audio protocol that used to live here (`media_audio`
and `media_music` messages, generation and serial tokens, sample-timed
fades and completion acknowledgements, the object/nonpositional roles, the
spatial projection, `LL_VLC_AUDIO_PROTOCOL_TESTS`) has been removed in
vkstorm-rust rung six. Its history is in git (before the commit "Restore the
upstream viewer side of media-plugin audio").

Parcel music now plays through the audio engine's own streaming slot
(`LLStreamingAudio_VKS`, `createDefaultStreamingAudioImpl`): decoded in
process by `vkstorm-audio` and mixed on the device clock, with the fade
policy described in `doc/rust_port.md`, rung six.

`UseMediaPluginsForStreamingAudio` (default off) still selects the media
plugin path for music. That path is the stock upstream one again:
`LLStreamingAudio_MediaPlugins` drives the VLC plugin with `set_volume`,
does not implement `hasAudioFade`/`beginAudioFade`, and so
`llvieweraudio.cpp` uses its frame-timed fade for it (with the login hold
at zero before STATE_STARTED). Metadata comes from the plugin as upstream
delivers it; there is no status-json side channel on this path.

Video media audio stays on VLC's own output, unchanged.

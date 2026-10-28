# Graphics settings persistence

Native OpenGL and Mesa/Zink share the viewer's OpenGL renderer. Switching
between their driver implementations on the same adapter should preserve the
user's graphics settings. Startup normalizes known AMD/NVIDIA adapter strings
for this comparison while retaining the raw GPU description for diagnostics.

The saved `LastGraphicsRendererFamily` identifies the active renderer as OpenGL
or Vulkan. Crossing that boundary still applies recommended settings. Hardware
changes, safe mode, feature-table changes, and explicit hardware reprobes retain
their reset behavior. Ordinary Vulkan relogs no longer unconditionally apply
recommended settings when deferred GPU classification completes.

Run `python scripts/perf/test_graphics_identity.py` to compile and exercise the
production identity helper for AMD/NVIDIA driver switches, renderer-family
changes, legacy settings, and different or unknown adapters.

Runtime validation: customize graphics settings, relog on the same renderer,
then switch OpenGL to Zink and back. Values should survive each restart on the
same adapter. Switching to Vulkan or back to OpenGL should apply recommended
settings once; another relog should preserve subsequent customization.

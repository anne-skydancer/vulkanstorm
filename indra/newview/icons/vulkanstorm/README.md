# Vulkanstorm application artwork

The blue VK, lightning and ring artwork was supplied by the project owner in
`vulkanstorm-blue-icons.zip` on 2026-10-03. `vulkanstorm-blue.png` is the
unmodified transparent master. `firestorm_icon.ico` is the supplied recommended
Windows ICO; `vulkanstorm-blue-legacy.ico` is the supplied uncompressed fallback.

PNG sizes, the 256-pixel BMP and macOS ICNS are format derivatives of that master.
The inherited `firestorm_*` resource filenames preserve platform integration
paths. All viewer channels and grid variants now select this directory.

Windows application and installer resources, Linux package and notification
icons, macOS bundles, login and skin startup logos use this artwork. The Windows
resource compiler must accept the recommended ICO; if it rejects compressed
frames, use the supplied legacy ICO without changing the design.

# Vulkanstorm defaults and branding restoration

The pre-reset settings were recovered from archived development commit
`7c2c201134905184971e82fb313da462fc880f77` in
`H:\vulkanstorm\archive-2026-10-02-clean-base\git-metadata`.
Only values of supported current settings were copied; no archived renderer
implementation or history was merged into the active branch.

Restored values cover chat persistence and formatting, chat and typing animations,
camera position and focus offsets, cache size, keyboard movement behavior, RLVa,
OpenGL core context and texture threading, and camera texture boost. The Modern
skin and its blue theme remain the Vulkanstorm default. The archive's obsolete
Firestorm/grey skin selection and languages without current translations were
not restored.

The Modern skin included eight flat tab textures and two text-field highlight
textures without named registry entries. Its widget definitions requested those
names, so the viewer reported missing local images. Registering the assets makes
the existing blue selection artwork available. The unsupported `text_pad_top`
folder-view attribute was also removed.

A further audit of Modern and inherited default XUI, registered texture files,
color aliases and literal C++ UI-image requests found two more missing texture
names (`Refresh_Over` and `Combobox_Over`), three missing list-view colors, three
misspelled color references and four unsupported `none` image references. These
now resolve to existing artwork and palette entries, or use an empty image name
to disable the image. List-view aliases are refreshed after the Modern palette
loads so selection uses its blue color. Static checks cannot cover dynamically
constructed names or replace runtime inspection of every window.
The scan includes all translation overlays; duplicate tooltip attributes in the
Russian texture panel were removed so that overlay parses successfully.

Graphics preference tabs use the labels `Rendering 1` and `Rendering 2`,
including the Italian override, while retaining their internal control names.

The default login layout adapts Kokua's sidebar structure from local source
commit `20493b5e73`, preserving Vulkanstorm's username removal, password visibility,
grid selection, viewer modes and saved locations. All eight translation overlays
follow the new hierarchy and retain their translated content. Grid-provided
splash pages and explicit URL overrides are preserved; the inherited Firestorm
default splash is replaced by a bundled Vulkanstorm welcome page.

The project owner's supplied blue VK icon master and both Windows ICO variants
are in `indra/newview/icons/vulkanstorm`. All channels share this icon directory.
Derived PNG, BMP and ICNS resources cover the viewer executable, installers,
macOS bundles, Linux desktop and notification icons, login and startup logos.
macOS disk-image backgrounds also carry Vulkanstorm branding. Some internal
resource filenames retain their inherited Firestorm names for compatibility.

Qualification encountered unrelated archived shallow refs in an otherwise
complete source branch. The version check now rejects only shallow boundaries
reachable from HEAD. Its test suite exercises both a truncated source branch and
a complete source branch with an unrelated shallow ref.

Windows development qualification uses the canonical Autobuild RelWithDebInfo
variables, Release dependencies and runtime staging, with installer generation
disabled. Linux and macOS packaging paths and image formats are checked on
Windows; builds and physical runtime acceptance on those platforms remain
separate requirements.

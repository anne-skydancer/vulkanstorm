# Vulkanstorm tools licensing notice

This branch holds Skin Studio (`tools/skin-editor`), a standalone tool for
making Vulkanstorm viewer skins and toolbar layouts. It contains no viewer
code. Since 2026-11-08 Vulkanstorm, and so the tools, is licensed under the
**GNU General Public License, version 3 or (at your option) any later
version** (`GPL-3.0-or-later`; the text is in [`LICENSE`](LICENSE), verbatim
from <https://www.gnu.org/licenses/gpl-3.0.txt>). The LGPL-2.1 option of the
dual licence of 2026-11-03 is dropped.

| Group | SPDX expression | Where it lives |
|---|---|---|
| Skin Studio code, written for Vulkanstorm | `GPL-3.0-or-later` | `tools/skin-editor/*.py`, `*.js`, `*.html`, `*.css`, `desktop/` (the Rust app, its packager and bridge), `tests/` |
| Skin library copied from the viewer (Linden Lab and Firestorm XUI, themes, labels, `commands.xml`) | LGPL-2.1, headers unchanged; part of a GPL-3.0-or-later whole | `tools/skin-editor/library/` (see its README for the source commit). Their headers are unchanged |
| Artwork in the skin library | Its own terms | Linden Lab viewer artwork: CC BY-SA 3.0, trademarks excluded (`tools/skin-editor/licenses/LICENSE-logos.txt`); other skin artwork: its authors' terms |
| Third-party components | Their own licences | The Rust crates of the desktop app and the bundled Python runtime; the desktop bundle ships their notices in `licenses/` |

The LGPL-2.1 text that the Linden Lab and Firestorm headers refer to is kept
in [`LICENSE-LGPL-2.1`](LICENSE-LGPL-2.1). Copies of both texts ship in
`tools/skin-editor/licenses/` (`GPL-3.0-license.txt`, `LGPL-2.1-license.txt`),
which the desktop bundle includes, beside the statement `LICENSE`.

## Linden Lab and Firestorm library

Linden Lab and Firestorm code stays LGPL-2.1, with its headers unchanged, and is combined into Vulkanstorm, which as a whole is licensed GPL-3.0-or-later (LGPL-2.1 is compatible with inclusion in a GPL-3.0 work). The LGPL-2.1 text their headers refer to is `LICENSE-LGPL-2.1`.

## Compatibility

Skin Studio's browser mode needs only Python's standard library and a
browser. The desktop app's crates.io dependencies (`desktop/Cargo.lock`,
all platforms) are under MIT, Apache-2.0, Zlib, ISC, BSD-3-Clause,
Unlicense, CC0/MIT-0, Unicode-3.0 and MPL-2.0 (cssparser, selectors,
dtoa-short and option-ext, through wry). MPL-2.0 is GPL-compatible
through its section 3.3 (Secondary Licenses). On Linux the app uses the
system WebKitGTK (LGPL); on Windows it uses Microsoft's WebView2 runtime, a
separate system component. The bundled Python runtime is under the PSF
licence (GPL-compatible), with the libraries python-build-standalone builds
in (OpenSSL 3, SQLite, libffi, Tcl/Tk and others under permissive licences;
the bundle ships their notices). Re-check them when the runtime is updated. None of these is proprietary or
GPL-2.0-only, so the source and the desktop bundle can be distributed under GPL-3.0-or-later.

## Other people's GPL code

Code available only under the GPL (for example the Aurora Viewer,
GPL-3.0-or-later) can be used where its authors permit. Aurora is still a
design reference first.

## Contributions

Contributions are accepted under `GPL-3.0-or-later`; changes to the copied
skin library keep the Linden Lab and Firestorm headers. See
[`CONTRIBUTING.md`](CONTRIBUTING.md).

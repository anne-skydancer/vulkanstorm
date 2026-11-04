# Vulkanstorm tools licensing notice

This branch holds Skin Studio (`tools/skin-editor`), a standalone tool for
making Vulkanstorm viewer skins and toolbar layouts. It contains no viewer
code. Since 2026-11-03 Vulkanstorm is offered under the GNU General Public
License, version 3 or later, **as well as** the GNU Lesser General Public
License, version 2.1; the tools follow it. The recipient chooses.

| Group | SPDX expression | Where it lives |
|---|---|---|
| Skin Studio code, written for Vulkanstorm | `LGPL-2.1-or-later OR GPL-3.0-or-later` | `tools/skin-editor/*.py`, `*.js`, `*.html`, `*.css`, `desktop/` (the Rust app, its packager and bridge), `tests/`; the source files carry the SPDX header |
| Skin library copied from the viewer (Linden Lab and Firestorm XUI, themes, labels, `commands.xml`) | `LGPL-2.1-only OR GPL-3.0-or-later` | `tools/skin-editor/library/` (see its README for the source commit) |
| Artwork in the skin library | Its own terms | Linden Lab viewer artwork: CC BY-SA 3.0, trademarks excluded (`tools/skin-editor/licenses/LICENSE-logos.txt`); other skin artwork: its authors' terms |
| Third-party components | Their own licences | The Rust crates of the desktop app and the bundled Python runtime; the desktop bundle ships their notices in `licenses/` |

The licence texts are [`LICENSE-LGPL-2.1`](LICENSE-LGPL-2.1) and
[`LICENSE-GPL-3.0`](LICENSE-GPL-3.0) (verbatim from
<https://www.gnu.org/licenses/gpl-3.0.txt>). Copies ship in
`tools/skin-editor/licenses/` (`LGPL-license.txt`, `GPL-3.0-license.txt`),
which the desktop bundle includes, beside the dual-licence statement
`LICENSE`.

## Why GPL-3.0 can be offered for the Linden Lab and Firestorm library

Linden Lab and Firestorm license their viewer files under LGPL-2.1 only.
Section 3 of LGPL-2.1:

> 3. You may opt to apply the terms of the ordinary GNU General Public
> License instead of this License to a given copy of the Library.  To do
> this, you must alter all the notices that refer to this License, so
> that they refer to the ordinary GNU General Public License, version 2,
> instead of to this License.  (If a newer version than version 2 of the
> ordinary GNU General Public License has appeared, then you can specify
> that version instead if you wish.)  Do not make any other change in
> these notices.
>
> Once this change is made in a given copy, it is irreversible for
> that copy, so the ordinary GNU General Public License applies to all
> subsequent copies and derivative works made from that copy.

This branch keeps the LGPL-2.1-only notices of that material, so every
recipient keeps both options; the GPL-3.0-or-later option is the section 3
conversion each recipient may make for their own copy.

## How to choose

- **LGPL-2.1**: use the tools as they are.
- **GPL-3.0 or later**: Skin Studio's own code is already offered under
  GPL-3.0-or-later. For the skin library, make the section 3 change in your
  copy; it cannot be undone for that copy and the copies made from it.

Artwork and third-party components stay under their own terms either way.

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
the bundle ships their notices). Re-check them when the runtime is updated. None of these is GPL-only or proprietary
and linked, so both options hold for the source and for the desktop bundle.

## GPL-only code

There is none on this branch, and there must be none: code available only
under the GPL (for example the Aurora Viewer, GPL-3.0-or-later only) would
remove the LGPL option. Aurora is a design reference only; its code is never
copied.

## Contributions

Contributions are accepted under `LGPL-2.1-or-later OR GPL-3.0-or-later`
for Skin Studio's code, and under `LGPL-2.1-only OR GPL-3.0-or-later` for
changes to the copied skin library. See [`CONTRIBUTING.md`](CONTRIBUTING.md).

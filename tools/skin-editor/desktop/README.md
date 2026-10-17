# Skin Studio desktop

A small Rust host for the existing Skin Studio editor. Each window starts its own
bundled Python server on an OS-assigned loopback port, waits for readiness and
owns that process for the lifetime of the app. Normal window closure stops it;
Windows Job Object kill-on-close and Linux parent-death signals also stop it if
the app is terminated unexpectedly. It does not attach to or stop other Skin
Studio servers. Unexpected server exits are reported in a native error dialog.

## Use a standalone bundle

Extract the **entire** platform archive to a writable local directory (prefer a
short path on Windows), then run
`skin-studio.exe` (Windows) or `./skin-studio` (Linux). Do not move the executable
away from its adjacent `bundle` directory. No Python installation, source checkout,
browser tab or internet connection is needed at runtime.

- Windows x86-64: requires the Microsoft Edge WebView2 Evergreen Runtime, normally
  present on supported Windows 10/11 systems. If absent, install it from Microsoft.
- Linux x86-64: requires GTK 3, WebKitGTK 4.1 and a desktop file-dialog portal.
  On Ubuntu 22.04/24.04:
  `sudo apt install libgtk-3-0 libwebkit2gtk-4.1-0 xdg-desktop-portal xdg-desktop-portal-gtk`.
  Other distributions use different package names. The Linux archive is not
  an AppImage and does not bundle these system GUI libraries.

**Save project** and **Export skin ZIP** use native Save As dialogs. Cancelling
does not mark work saved; errors are displayed in the editor. Writes are staged
in the destination directory before replacing a file. **Open project** uses the
webview's native file chooser. Editing during a save does not mark newer changes
saved. Closing with unsaved changes asks for confirmation. Save before leaving:
there is no automatic recovery or persistence.

Navigation is restricted to the local editor. Preview links do not open external
websites. Native file access is limited to the user-selected save destination;
the editor never installs a skin or changes viewer/account settings.

## Build a standalone archive

Build **on each target OS** with stable Rust/Cargo and Python 3.12+ for packaging.
The bundled server runs the separately pinned Python runtime, not build-host Python.
Node.js 24 is used only for the JavaScript bridge tests.
Windows also needs MSVC C++ build tools. Linux build dependencies on Ubuntu:

```sh
sudo apt install build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev \
  libxdo-dev xdg-desktop-portal xdg-desktop-portal-gtk
```

From this directory:

```sh
cargo test --locked
node --test bridge.test.cjs
python package.py
```

The script downloads a SHA-256-pinned Python Build Standalone runtime from
`astral-sh/python-build-standalone`, builds the Rust executable in release mode,
copies the editor plus the complete skin library and toolbar command registry,
and verifies the staged backend before creating a platform archive under `dist/`.
Use `--runtime-archive <path>` for an already downloaded matching archive.
Use `--output <directory>` to retain multiple builds; existing outputs are never
silently replaced. Build dependencies/runtime archives need network access on
first use. The generated Cargo lockfile must remain committed with the source.

The archive includes editor/Rust sources, the Rust dependency lockfile and
dependency licence notices. The bundled Python distribution retains its licence
files, with Python Build Standalone dependency notices copied from a separately
SHA-256-pinned upstream source archive. Skin artwork is copied without modification;
original artwork notices and trademark restrictions are under `licenses/`.
Exporting or redistributing skin
assets does not grant additional rights to trademarks.

The standalone desktop CI workflow builds separate Windows and Linux archives.
It does not build or alter the viewer, create an installer, publish a release or
move the viewer's `latest` tag.

## Verification

```sh
cargo fmt --check
cargo clippy --locked --all-targets -- -D warnings
cargo test --locked
```

Run the bundled executable with `--check` to validate bundled Python startup,
catalog/project loading, native save/export backend paths and server shutdown,
without opening a window. This is not GUI qualification: native dialogs, webview
file opening, unsaved-close confirmation and rendering must also be tested on
both desktops.

For all editor regression commands and export caveats, see
[the editor guide](../README.md).

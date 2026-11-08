# Vulkanstorm tools

Independent, standalone tools for Vulkanstorm viewers. This branch contains no
viewer application code, viewer build system, or viewer development hooks. It is not
a viewer release or development branch.

## Skin Studio

[Skin Studio](tools/skin-editor/README.md) creates viewer skins and toolbar layouts.
It includes an interactive colour preview, project save/open, and native skin ZIP
export. It never installs assets or changes viewer/account settings.

- [Desktop app](tools/skin-editor/desktop/README.md): a Rust window with its own
  bundled local server. Closing or terminating the app stops its server.
- Browser mode: Python 3.10+ and a modern browser, with no packages to install.

From this repository's root:

```powershell
python tools\skin-editor\editor.py
```

The skin library and command definitions are included as
[tool data](tools/skin-editor/library/README.md); no viewer checkout or build is
required. Exports still need testing in the intended viewer, since preferences,
viewer versions, and custom UI definitions can affect their appearance.

## Tests

```powershell
python -m unittest discover -s tools\skin-editor\tests -p "test_*.py" -v
node --check tools\skin-editor\editor.js
node --test tools\skin-editor\desktop\bridge.test.cjs
```

Desktop build and Rust validation commands are in the
[desktop guide](tools/skin-editor/desktop/README.md). The dedicated CI workflow
builds only Skin Studio, not the viewer.

## Licensing

Licensed under the [GNU General Public License, version 3 or later](LICENSE)
(`GPL-3.0-or-later`, decided 2026-11-08; the LGPL-2.1 option is dropped).
Skin Studio's code is `GPL-3.0-or-later`; the skin library copied from the
viewer (Linden Lab and Firestorm, LGPL-2.1 with unchanged headers) is used under
the GPL as section 3 of LGPL-2.1 allows. Included skin artwork and other
upstream assets retain their original terms and trademark restrictions; see
[the bundled notices](tools/skin-editor/licenses/). [NOTICE.md](NOTICE.md)
explains the groups; contributions are accepted under the same terms
([CONTRIBUTING.md](CONTRIBUTING.md)).

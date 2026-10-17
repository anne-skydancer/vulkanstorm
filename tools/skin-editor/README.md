# Vulkanstorm Skin Studio

A local browser-based editor for the main **logged-in viewer UI**, not the login
screen. It edits semantic skin colours and left/right/bottom toolbar command
placement, order, alignment, display mode, and layout style. It does not edit
arbitrary textures, icons, XUI panels, or toolbar dimensions. An optional flat
selected-tab background is generated as a native texture and widget override.

## Run

For a standalone Windows/Linux app with its own window and bundled Python, see
[Skin Studio desktop](desktop/README.md). The browser workflow below remains
available without Rust or GUI libraries.

Requires Python 3.10+ and a modern browser. No packages, viewer rebuild, or network
service are required. From the repository root:

```powershell
python tools\skin-editor\editor.py
```

Open `http://127.0.0.1:8765`. Stop the server with Ctrl+C. Use `--port 8766` if the
default port is occupied, or `--root C:\path\to\checkout` to read another compatible
viewer source checkout. The server binds only to loopback, requires the printed
host, rejects cross-origin API requests, and never writes viewer files.

## Author

1. Choose a source skin and theme, then **Start from selection**.
2. Enter a new skin name and a new lowercase folder identifier.
3. Add, remove, reorder, or move commands between toolbar edges. Each command can
   occur only once. Display supports icons, text, or both; layout supports normal,
   equalized, or fill sizing.
4. In **Colour workshop**, choose a meaning-based group: links and messages,
   chat and mentions, avatar name tags, floater windows and tabs, menus and
   inventory, or text fields and accents. Click a preview sample to select its
   text role, Shift-click for its background, or Alt-click for its border.
   Samples also support keyboard focus and Enter/Space. The advanced palette
   remains available for every raw colour name.
5. Edit RGB with the picker and alpha with the fourth numeric field, or edit all
   four components numerically (R, G, B, A from left to right, range 0 to 1).
   **Reset** restores the colour to its value when the project was loaded.
   Choose **As loaded** to compare against that snapshot without discarding edits.
   Change the world backdrop to see transparency against daylight, night or grey.
6. For a selected floater-tab background, enable **Custom selected tab background**.
   This intentionally replaces selected tab artwork with a flat colour. Turn it
   off to retain source artwork. Tab attention flash and selected-tab text have
   separate controls. Click the example tabs to switch between chat and inventory.
7. **Save project** downloads editable JSON; **Open project** validates and restores
   it. Save before closing: there is no automatic persistence.
8. **Export skin ZIP** downloads native assets and installation instructions.

The preview is schematic, with abbreviated labels standing in for icons. It is
not the viewer's renderer and has no live viewer connection. It demonstrates
URLs, chat, script errors, alerts, mentions, self/friend overhead names, selected
inventory rows, menus, fields and floater/tab states. It does not reproduce
original tab artwork, precise fonts, glow, flashing animation, viewer opacity
preferences or every use of every colour. Final qualification must be done in
the viewer. Fill and equalize are only approximated in the preview.

### What each colour changes

- `HTMLLinkColor`: viewer URL text, not content inside embedded web pages.
- `ScriptErrorColor`: script errors, not a universal error-message colour.
  Alert/caution text and backgrounds have separate roles.
- `UserChatColor` / `FriendsChatColor`: your/friends' chat and IM text, not
  exclusively the name portion of a message.
- `NameTagSelf` / `NameTagFriend`: overhead avatar names, separate from chat.
  Friend tags require `NameTagShowFriends`. Contact sets, individual avatar
  colours, restrictions and other preferences may take priority.
- `ButtonFlashBgColor`: the default tab attention flash, not selected artwork.
  Per-floater settings can override it. Button label and image tints are shared
  with ordinary buttons; the editor labels those shared effects explicitly.
- Selected tab artwork has no dedicated palette colour. The optional custom tab
  setting exports a 16x16 RGBA PNG and updates all first/middle/last selected
  top/bottom/left tab images in the native `tab_container` widget template.
  Existing unselected images and other widget settings are preserved. The
  viewer multiplies that image by `ButtonImageColor`, reflected in the preview.
  Floaters specifying their own tab images can override these widget defaults.
  No viewer code or shared button artwork is modified.

Source asset warnings identify malformed colors, unresolved references, and stale
or duplicate commands. Invalid colors retain inherited values if available;
unresolvable new colors are omitted. Unknown commands are excluded, matching the
viewer's command lookup rejection. No shipped source assets are repaired.

## Export and install

The ZIP contains:

- `skins/<new_folder>/`: the source skin's assets, overlaid with its selected
  theme, plus generated `colors.xml` and `toolbars.xml`. If custom tabs are
  enabled, also a selected-tab PNG and `xui/en/widgets/tab_container.xml`.
- `skin-registration.xml`: **one registration entry**, not a replacement registry.
- `project.json`: the editable project.
- `INSTALL.txt`: installation and precedence caveats.

Theme assets are flattened into a single new skin; other themes are not copied.
Colors are resolved in default/skin/theme load order and exported as independent
RGBA values, not aliases. Changing one exported color does not automatically
recolor its former aliases. Assets not provided by the source skin still inherit
from the viewer's default skin. Use the same viewer version for authoring and testing.
This is a local authoring export, not a standalone redistributable asset license;
the source assets' existing license terms still apply.

Close the viewer and back up your settings. Copy the new skin directory into the
viewer installation's `skins` directory, then merge the `<map>` inside the
registration fragment's `<array>` into the existing `skins/skins.xml` array.
Do **not** replace the registry with the fragment. Select the new skin under
Preferences > Skins and restart.

An existing account's `toolbars.xml` takes precedence over skin toolbar defaults.
To apply your layout to an existing account, close the viewer, back up that
account's toolbar file, and replace it with the exported skin's `toolbars.xml`.
Account directories vary by platform, grid, and account; the editor deliberately
does not guess or write to them. User-settings `colors.xml` can also override
skin colors. Roll back by restoring the backups and selecting the previous skin.

## Validate

From the repository root:

```powershell
python -m unittest discover -s scripts\tests -p test_skin_editor.py -v
node --check tools\skin-editor\editor.js
```

The regression suite covers every registered theme, palette layering, source
warnings, native XML/ZIP round trips, theme asset flattening, invalid projects,
and loopback HTTP endpoints. Browser interaction and actual viewer rendering
require separate checks.

Tests also check that every preview role exists in every registered theme,
semantic colour values survive export independently, old projects remain
valid, custom tab XML preserves unrelated settings, and generated PNG pixels,
alpha and checksums match the requested colour.

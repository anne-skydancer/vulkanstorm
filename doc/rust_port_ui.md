# vkstorm-rust: the UI rung and user customization

Evaluation of whether the incremental Rust port (see `rust_port.md`) can keep
the viewer's user interface customizable exactly as it is today: skins, XUI
layouts, color and texture tables, toolbars, fonts, UI scale, per-user
overrides, localization and notifications. Paths are relative to the viewer
checkout. Counts come from `grep` over the tree on 2026-10-20; no build or
measurement was run.

## 1. Inventory of customization surfaces

Almost the entire UI is data-driven. The C++ side supplies widget types, named
callbacks and the layout engine; everything a skin author or user sees is in
XML files resolved through a skin search path.

### 1.1 Skin search path and fallback

`LLDir::setSkinFolder` (`indra/llfilesystem/lldir.cpp:1086`) builds the
ordered search list: `skins/default`, `skins/<skin>`,
`skins/<skin>/themes/<theme>`, then the per-user `<OS user app dir>/skins/default`
and `<OS user app dir>/skins/<skin>` (`getOSUserAppDir()` plus `skins`,
`lldir.cpp:1139`; this is a sibling of `user_settings`, not inside it).
`LLDir::findSkinnedFilenames` (`lldir.cpp:813`) resolves `(subdir, filename)`
to the list of existing files in that order, adding `xui/en` then
`xui/<language>` for localized subdirs (`textures` is unlocalized).
`CURRENT_SKIN` returns up to two files, not one: the most specific
base-language file and, independently, the most specific current-language
file, in that order (`lldir.cpp:913`). The two can come from different skins,
so a user can override only the localization of a file, or only its English
layout, without touching the other. `ALL_SKINS` returns every match for
overlaying (colors, textures, notifications).
Skins and themes are declared in `indra/newview/skins/skins.xml` (LLSD: name,
folder, themes) and read by the preferences skin panel
(`llfloaterpreference.cpp:5293`). Selection is the `SkinCurrent` and
`SkinCurrentTheme` settings (`llappviewer.cpp:3383`), applied before `LLUI`
exists; the reset path sets `"default"` (`llfloaterpreference.cpp:3481`).
Shipped skins: `default` (Vulkanstorm palette, see `doc/branding_defaults.md`),
`firestorm`, `ansastorm`, `ansastorm_modern`, `metaharper`, `starlight`,
`starlightcui`, `vintage`; the last has its own `settings.xml` and
`toolbars.xml`.

### 1.2 Surfaces and how each binds to code

| Surface | Files | Loader | Binding to C++ |
|---|---|---|---|
| Widget layouts (floaters, panels, menus) | `skins/default/xui/en/*.xml`, 689 files; skins override by same name | `LLUICtrlFactory::createFromFile` -> `getLayeredXMLNode` (`lluictrlfactory.cpp`, default constraint `CURRENT_SKIN`, `lluictrlfactory.h:194`): the single most specific English file replaces every less specific English file whole (a skin that removes a control removes it), then the independently selected localization file is overlaid by node name (`LLXMLNode::updateNode`, `llxmlnode.cpp:573`) | XML tag -> `LLDefaultChildRegistry::Register<T>("tag")` (99 tags registered in tree, 427 distinct tags appear in XML counting dotted sub-blocks); attributes -> `T::Params` param block via `LLXUIParser::readXUI`; `name=` -> `getChild<T>("name")` (5144 call sites in `newview`) |
| Widget defaults | `xui/en/widgets/*.xml`, 110 files | `LLUICtrlFactory::loadWidgetTemplate`, filled into the per-type prototype (`ParamDefaults`) and inherited down the Params hierarchy | one template per registered tag, by `LLWidgetNameRegistry` |
| Floater registry | `llviewerfloaterreg.cpp`, 231 `LLFloaterReg::add` | name -> XML filename + build functor | `Floater.Show`, `Floater.Toggle` callbacks take the name as `parameter=` |
| Callbacks | `function="X.Y"` in XML, 1309 distinct uses | `LLUICtrl::CommitCallbackRegistry` / `EnableCallbackRegistry` (`lluictrl.h:300`), scoped registrars per floater; `view_listener_t::addMenu/addEnable` in `llviewermenu.cpp` (373) | 789 distinct commit names, 70 enable names, registered by string |
| Settings bindings | `control_name=`, `enabled_control=`, `visibility_control=` (1310 uses) | `LLUICtrl::initFromParams` (`lluictrl.cpp:126`) looks the name up in the `LLUI` settings map (`config`, `floater`, `account`, `ignores`, `llappviewer.cpp:1025`) | `LLControlGroup` (`indra/llxml/llcontrol.cpp`), schema in `app_settings/settings.xml` (12319 keys), `settings_files.xml` groups; `CurrentSkin` group loads a skin's `settings.xml` non-persistently |
| Colors | `colors.xml` per skin and theme, overlaid `ALL_SKINS`; user palette `user_settings/colors.xml` | `LLUIColorTable::loadFromSettings` (`lluicolortable.cpp:258`), `saveUserSettings` | XUI color attributes resolve names at parse time (`readUIColorValue`); `Pref.applyUIColor`/`Pref.getUIColor` (53 uses) |
| Textures | `textures/textures.xml` per skin, overlaid (954 declarations in default; scale and clip rects for nine-slice) | `LLUIImageList` (`llviewertexturelist.cpp:1994`) | `image_*=` attributes by name; `LLUI::getUIImage` |
| Toolbars | `app_settings/commands.xml` (70 commands: icon, `label_ref`, `tooltip_ref`, `execute_function`, `is_running_function`), default `skins/default/toolbars.xml`, saved `<account>/toolbars.xml` | `LLCommandManager::load` (`llcommandmanager.cpp:165`, `LLSimpleXUIParser`), `LLToolBarView::loadToolbars` (`lltoolbarview.cpp:227`: account file, then `LL_PATH_TOP_SKIN`, then default skin) | command functions are commit-registry names |
| Floater positions, sizes and visibility | `save_rect`, `save_visibility`, `positioning` attributes; `floater_rect_<name>` (saved size), `floater_pos_<name>_x/_y`, visibility and dock controls, all in the `account` group (`LLFloater::getControlGroup`, `llfloater.cpp:644`) | `LLFloaterReg::declareRectControl`/`declarePosControl`/`declareVisibilityControl` (`llfloaterreg.cpp:431`), `LLFloater::applyRectControl`/`storeRectControl` | persisted in the per-account settings file, not the global `settings.xml` |
| Fonts and UI scale | `xui/en/fonts.xml` (skinnable), 11 selectable `fonts/fonts_*.xml` via `FSFontSettingsFile`, user `user_settings/fonts/`; `UIScaleFactor` | `LLFontGL::initClass` (`llfontgl.cpp:870`), `LLFontRegistry`; `LLUI::setScaleFactor` (`llviewerwindow.cpp:2097`) | XUI coordinates are logical units scaled at draw |
| Strings and localization | `strings.xml`, `language_settings.xml`, `teleport_strings.xml`, `rlva_strings.xml`; `xui/<lang>/` overlays (13 languages) | `LLTransUtil::parseStrings`; overlay merge in `getLayeredXMLNode` | `LLTrans::getString`, `[PLACEHOLDER]` substitution |
| Notifications | `notifications.xml` (1337 templates, forms, `$yestext` form templates), `notification_visibility.xml`, `ignorable_dialogs.xml` | `LLNotifications::loadTemplates` (`llnotifications.cpp:1587`, `ALL_SKINS`), `LLSimpleXUIParser` | template name -> `LLNotificationsUtil::add` |
| Key bindings | `app_settings/key_bindings.xml`, user copy | `LLKeyConflictHandler` (`llkeyconflict.cpp`) | control names |
| Other user files | `quick_preferences.xml`, `client_list_v2.xml`, `beams`, `windlight`, `grids.user.xml`, `avatar_render_settings.xml`, `emoji_floater_state.xml` | per-file LLSD loaders | listed from `LL_PATH_USER_SETTINGS` / `LL_PATH_PER_SL_ACCOUNT` uses |
| Login page | `panel_fs_nui_login.xml`, `panel_fs_login.xml`, `panel_login.xml` with a `<web_browser>` child; URL from `LoginPage` setting / grid | `FSPanelLogin::loadLoginPage` (`fspanellogin.cpp:911`) | CEF plugin texture inside `LLMediaCtrl` (26 XUI files use `<web_browser>`) |
| Markdown chat | `parse_markdown`, `markdown_emote` text params; `LLMarkdown` (`indra/llui/llmarkdown.cpp`) | set in `llchathistory.cpp:1181`, `fschathistory.cpp:1301` | chat history widgets; styles from `colors.xml` |
| Script editor (LSL, Lua) | `panel_script_ed.xml` (compile targets `lsl-luau`, `luau`), `keywords_lsl_default.xml`, `keywords_lua_default.xml`, `floater_script_ed_prefs.xml` (syntax colors are UI colors) | `LLSyntaxIdLSL` (`llsyntaxid.cpp:50`), `LLKeywords` | `script_editor` widget, `LLTextEditor` segments |

Hard-coded in C++: the widget types and their behaviour (102k lines in
`indra/llui`, 301 floater/panel classes in `newview`), layout semantics
(`rect`, `follows`, `left_delta`/`bottom_delta`, `layout_stack`, tab groups),
the parse rules of `LLInitParam` (`indra/llcommon/llinitparam.h`: `Optional`,
`Mandatory`, `Multiple<T, AtLeast<n>>`, `Alternative`, `Flag`, `Deprecated`,
provided-ness tracking, `fillFrom` defaults), the callback and widget
registries, and the 2D draw path (`LLView::draw` tree walk emitting batched
quads through `gGL`, `llrender2dutils.cpp`, glyph cache in
`LLFontVertexBuffer`). Menus are XML too: `menu_viewer.xml` has 1776 items
across 155 `menu_*.xml` files bound by `function=` names
(`llviewermenu.cpp:589` onward). Some pieces are built from code with
`LLUICtrlFactory::create<T>(params)`, for example the popup menu
(`llviewermenu.cpp:577`) and `LLButton`'s checkbox panel, but they still go
through the same Params and widget templates.

## 2. What a port must preserve

The contract with users and skin authors is the file formats and their
resolution rules, not any C++ API. A port keeps every surface if it preserves:

1. The XUI schema and semantics: tag names, attribute names including dotted
   sub-blocks (`<combo_box.item>`, `<button.commit_callback function=...>`),
   attribute-or-child-element equivalence through the `name_stack`, string
   enum values (`TypeValuesHelper`), `Multiple` cardinality, deprecated
   attributes silently ignored, unknown attributes warned not fatal,
   `from_xui` layout (`applyXUILayout`: deltas relative to the previous
   sibling, `follows`), widget template defaults by inheritance chain, and
   the default-value diffing used by `writeXUI` (the XUI exporter and
   `llrngwriter.cpp` schema writer are developer tools and may lag).
2. The search order of section 1.1 including the per-user skin directories
   under the OS user app dir, `ALL_SKINS` overlaying for colors, textures and
   notifications, and for widget files the two-file rule: whole-file
   replacement of the English layout by the most specific skin, then the
   independently chosen localization overlaid by node name.
3. `skins.xml`, `SkinCurrent`/`SkinCurrentTheme`, and the skin `settings.xml`
   group.
4. `colors.xml` name references resolved late enough that user palette
   overrides and theme overlays apply; `user_settings/colors.xml` round-trip.
5. `textures.xml` names with scale and clip rects (nine-slice) and the
   `ALL_SKINS` overlay.
6. `commands.xml` as the toolbar command registry with its named functions,
   and the three-level `toolbars.xml` lookup (account, top skin, default).
7. `floater_rect_*` (saved size), `floater_pos_*`, visibility and dock
   controls in the per-account `account` settings group, plus `positioning`;
   a port must keep them in that store so existing per-account layouts carry
   over.
8. `strings.xml` and `[PLACEHOLDER]` substitution, `notifications.xml` form
   templates and `$yestext` style expansion, `ignorable_dialogs.xml`.
9. `fonts.xml` and `fonts_*.xml` selection with user `fonts/` fallback,
   `UIScaleFactor` as a logical-unit scale applied at draw time.
10. The registry-by-string model for callbacks and settings
    (`function=`, `parameter=`, `control_name=`), since skins reference these
    names directly.

Everything in this list is a data contract; none of it depends on the host
language. The risk is in reproducing semantics (item 1) exactly. The parsing
half is testable by export: parse every XUI file in every shipped skin with
both parsers, export with `writeXUI`, and compare, as the J2C rung compared
decodes. That proves parameter serialization only: the builder exports the
params before `applyXUILayout` and widget construction (`lluictrlfactory.h:243`),
so layout and behaviour need their own C++-oracle checks (section 3a).

## 3. Options for the UI rung

The project rule added for this evaluation: a part stays C++ only when porting
it would be detrimental to performance or when Rust is not tooled for the job.
Evidence on both for the UI layer:

Performance. The in-tree instrumentation is the Tracy zones `HUD`, `UI 3D`
and `UI 2D` in `render_ui` (`llviewerdisplay.cpp:1598`) and the `XUI Parsing`
block timer (`llxuiparser.cpp:678`). No numbers are recorded in `doc/`, and
this evaluation ran no build. Structurally the per-frame UI cost is a CPU tree
walk over visible widgets, text layout, and batched quads; parsing is paid once
per floater build, not per frame. Nothing in that path is faster in C++ than in
Rust, and nothing in it is a hot GPU path. The performance argument against
Rust does not apply to the UI itself. It does apply to the boundary: a widget
tree split across C++ and Rust with per-widget virtual calls, string-keyed
`getChild` lookups and callbacks crossing a C ABI would add overhead and, more
importantly, maintenance cost on 5144 lookup sites. The tree should live on one
side at a time, at floater granularity or larger.

Tooling. XML parsing (`quick-xml`, `roxmltree`), LLSD, derive macros for
param blocks, and localization string tables are well covered in Rust. Text
shaping and layout: the viewer uses FreeType only (`llfontfreetype.cpp`, no
HarfBuzz) with an OT-SVG hook for color emoji (`llfontfreetypesvg.cpp`); Rust's
`cosmic-text`/`parley` (rustybuzz shaping, fontdb fallback, swash
rasterization, `resvg` for SVG glyphs) are at least equivalent to that stack.
Spell checking is Hunspell (`llspellcheck.cpp:33`); Rust has Hunspell bindings
and a pure-Rust `spellbook`. The embedded browser is out of process already
(`indra/media_plugins/cef`); the UI side (`LLMediaCtrl`) only shows a texture
and forwards input, so no Rust CEF binding is needed for the UI rung. What Rust
does not have is a retained-mode widget set with XUI semantics: `egui` is
immediate mode, `iced`, `xilem`/`masonry` and `slint` each bring their own
layout model and DSL, and none understands `follows`, `layout_stack`, floater
docking, tear-off menus, inventory drag and drop or nine-slice skin textures.
That gap is the same for every option: the widget set must be written either
way. Rust is tooled for the job; it is not tooled for skipping the job.

### 3a. Faithful port of LLUI to Rust, XUI stays the layout language

Shape of the port: `LLXMLNode` and the layered merge become a Rust XML tree
with the same `updateNode` rules; `LLInitParam` becomes a `#[derive(XuiBlock)]`
on widget `Params` structs where each field is a typed `Provided<T>` (value plus
provided flag, since `isProvided` drives layout and export), with attribute
name, enum name tables, cardinality, deprecation, block inheritance and
`fill_from` template merging generated by the macro; plain `serde` is not
enough because of the name stack, provided-ness and template filling. The
widget registry is a `HashMap<&str, fn(&XmlNode, Option<WidgetId>) ->
WidgetId>` keyed by tag with per-type child registries (menus accept menu items
only). Callback registries stay string keyed, with scoped registrars. Widgets
form an arena-allocated tree with a `Widget` trait for draw and event
dispatch; `getChild` by name stays, returning typed handles. The draw path
emits the same quad batches into the existing `LLRender` through the renderer
rung's C ABI, so `colors.xml`, `textures.xml`, fonts and UI scale are consumed
unchanged.

Surfaces: all survive, because the file formats and resolution rules are
reimplemented one to one and verified against the C++ oracle. Breakage risk is
semantic drift in the parser, the layout and the widget behaviour. The
export-and-diff test catches the parser half. The rest needs oracle checks per
widget stage, recorded from the C++ build: constructed geometry after
`applyXUILayout` and `follows` resizing at several window sizes, rendered
output of representative floaters, input and focus traversal, callback
dispatch by name, and persistence round trips of the `account` controls.

Effort: `indra/llui` is 102k lines; `newview` holds 301 floater and panel
classes, 287 param blocks (97 files in `newview`, 73 in `llui`), 789 named
commit callbacks and 373 menu listeners. The text subsystem alone
(`lltextbase`, `lltexteditor`, `llkeywords`, `llmarkdown`, `llurlentry`,
`llmenugl`, `llscrolllistctrl`, `llfolderview`, `llpreviewscript`,
`llmediactrl`) is about 29k lines. Order of magnitude: engineer-years (three
to five), spread over several releases, each release moving a set of floaters
and deleting their C++.

Justification for C++ under the two-ground rule: not justified on performance
or tooling once the renderer rung exists; the port is deferred by sequencing
and size only.

### 3b. New Rust toolkit with an XUI compatibility layer

A compatibility layer would have to parse XUI (same work as 3a), then map 427
tags onto a foreign widget model while reproducing absolute rects, `follows`,
sibling deltas, floater persistence, focus and tab order, modal dialogs,
docking, tear-off menus and skin textures. On `egui` this is not achievable
without rebuilding a retained layer on top of it; on `iced`/`xilem`/`slint`
it means a custom layout container per XUI tag, which is LLUI again inside a
framework that also wants to own the window, input and GL context, while the
viewer draws UI between 3D passes with shared `LLRender` state
(`render_ui`). Surfaces that break or degrade in practice: skin textures and
nine-slice chrome (toolkits draw their own), colors beyond what the layer
maps, widget templates, floater positions semantics, any skin that relies on
pixel layout of overridden floaters, and third-party skins in the per-user
`skins` directory. Effort is 3a plus the adapter plus the toolkit's
learning curve, with a worse result for skin authors. Not justified on either
ground; rejected.

### 3c. Keep LLUI in C++ longest, port around it

This is the current plan: the UI is the rung after the renderer. Rungs below
it (textures, inventory, object store, renderer) expose C ABIs that `llui`
and the `newview` floaters call through thin C++ wrappers, as `LLImageJ2COJ`
does today. All surfaces survive untouched. The costs are that the UI's
behaviour stays in C++ for the longest, that the `newview` floater classes
remain the largest body of C++ coupling to every ported subsystem (each rung's
C ABI must serve the UI's query patterns), and that there is no Rust-side test
coverage of XUI. Under the two-ground rule this is justified as sequencing,
not as a permanent exemption: nothing in the UI is performance-critical in a
way that favours C++, and the tooling gap is the widget set itself, which
does not shrink by waiting.

| Option | XUI, skins, colors, textures | Toolbars, floater positions, notifications, strings | Fonts, UI scale, CEF, markdown, script editor | Effort | C++ justified by perf/tooling |
|---|---|---|---|---|---|
| 3a port LLUI, keep XUI | kept (oracle-verified) | kept | kept (Rust text stack equal or better; CEF stays out of process) | engineer-years, multi-release | no |
| 3b new toolkit + XUI layer | degraded or broken for skins | partially kept | fonts re-done; editor widgets rewritten | 3a plus adapter | no |
| 3c keep LLUI in C++ | kept | kept | kept | none now; cost moves to every rung's C ABI | sequencing only |

## 4. Recommendation

For vkstorm-rust: keep option 3c as the plan of record until the renderer rung
lands, then execute the UI rung as option 3a. Do not adopt a Rust UI toolkit;
XUI remains the layout language and the skin directory contract is unchanged.
No feature is cut.

Within 3a, order the work so that each step has a C++ oracle and a byte-level
or structural equality test, as the J2C rung did:

1. Data layer first, usable before the renderer rung: layered XML loading and
   `findSkinnedFilenames`, the `XuiBlock` derive and parser, `colors.xml`,
   `textures.xml` declarations, `strings.xml`, `notifications.xml`,
   `commands.xml`, `toolbars.xml`, `fonts.xml`, `settings.xml` schema. Test:
   parse every XUI file in every shipped skin and every language with both
   parsers, export with `writeXUI`, diff. This also gives
   `scripts/tests` a skin validation tool that does not exist today.
2. Text shaping and glyph cache behind a C ABI (replacing
   `llfontfreetype`/`llfontbitmapcache`), verified by glyph-metric and
   atlas comparison; keeps `fonts_*.xml` and user fonts.
3. The widget tree, moved at floater granularity. A Rust floater is hosted as
   a leaf in the C++ `LLView` root until the root itself moves; the C++ class
   of a moved floater is deleted in the same change. Named callbacks and
   settings bindings cross the boundary by string, as they already do inside
   C++.

Keep the two-ground rule explicit in the rung record: the UI stays C++ for
sequencing, and the record should say so, with the Tracy `UI 2D` numbers
captured before the rung starts so the port is held to them.

Implication for vkstorm-aleph: a ground-up viewer that wants users' existing
skins must also treat XUI, `colors.xml`, `textures.xml`, `toolbars.xml`,
`notifications.xml` and the skin search order as its input contract, or accept
a one-time skin break with no migration path for third-party and user skins.
The data-layer crates from step 1 are the shareable common base; widget
behaviour is not. This note records the implication only and does not design
Aleph's UI.

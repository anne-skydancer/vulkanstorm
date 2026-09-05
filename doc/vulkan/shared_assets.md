# Shared UI Assets — Strategy for the GL and Vulkan Backends

Status: **current** (2026-09-05). Governing architecture:
[phase3_v2_ui_plan.md](phase3_v2_ui_plan.md) — especially §0 (results, not
logic) and §3 (fully-parallel renderers, GL reference byte-stable).

This document records which UI assets the two render backends (OpenGL via
`llrender`/`LLUIImageList`, native Vulkan via `llvulkan`/`LLVKUIImage`)
consume, what is shared between them, what is per-backend, and the module
rules that keep the sharing honest.

## 1. Runtime model: why sharing is about code health

The two backends are **session-exclusive**: `LLWindow::getSkipGLContext()`
selects one at startup and when Vulkan runs there is *no* GL context at all.
Nothing is ever live in both backends simultaneously, so CPU-side sharing is
**not** about saving memory or sharing GPU objects — it is about having one
source of truth for declarations, colors, fonts, and skin files so the two
backends cannot silently diverge. Sharing is limited to **CPU-side data and
parsing**; all GPU-side work (texture uploads, glyph atlases, draw calls)
stays per-backend, per the fully-parallel rule in phase3_v2_ui_plan.md §3.

## 2. Asset inventory used by both backends

| Asset class | Source of truth | Notes |
|---|---|---|
| UI chrome images | `skins/*/textures/textures.xml` + image files (PNG/TGA) | name → file, preload, use_mips, clip/scale pixel rects, scale_type |
| UI colors | `skins/*/colors.xml` | `LLUIColorTable` (llui) |
| Fonts | `fonts.xml` + FreeType faces | `LLFontRegistry` (llui); raster via `LLFontGL` (GL) or `getVkFaceInfo` handoff (VK) |
| Cursors | `skins/*/textures` cursor files | window-system cursors, per backend at the OS seam |
| Media frames | plugin pipeline (CEF etc.) | CPU pixel buffers in `LLViewerMedia`; uploaded per backend |
| World textures surfaced in UI | fetch/decode pipeline | minimap, world map, texture pickers, profile/snapshot previews |

## 3. What is shared (CPU-side, GL-free)

- **`LLUIImageDecls` (llui, NEW)** — `llui/lluiimagedecls.{h,cpp}`. The single
  parse of `textures.xml`. It captures the union of the fields both backends
  used to read independently (`name`, `file_name`, `preload`, `use_mips`,
  `clip`, `scale`, `scale_type`) and merges all skins' files in
  `LLDir::ALL_SKINS` order (generic → specific) with the exact semantics the
  GL path produced via `LLInitParam::Block::overwriteFrom`: a more specific
  skin's declaration overwrites only the fields it provides. It also offers
  `resolvePaths()`, which resolves each declaration's file through the
  `CURRENT_SKIN` search path (permitting PNG-only theme overrides).
  Consumers: `LLUIImageList::initFromFile()` (GL) and `LLVKUIImage::init()`
  (Vulkan) — the duplicated XML walks in both were deleted.
- **`LLUIColorTable` (llui)** — already shared; both backends read colors
  from it.
- **`LLFontRegistry` (llui) + the `LLFontGL::getVkFaceInfo` handoff** — font
  selection/descriptor data is shared; the Vulkan text path receives FreeType
  face info from the registry without executing GL.
- **`LLViewerMedia` CPU pixel buffers** — media plugin frames are produced
  backend-neutrally on the CPU; each backend uploads them itself.

## 4. What stays per-backend (GPU-side)

- **Texture uploads.** GL: `LLViewerTextureManager`/`LLViewerFetchedTexture`
  with discard levels, POT padding, and the `onUIImageLoaded` callback that
  normalizes clip/scale rects against decoded dimensions. Vulkan: llimage
  decode → RGBA8 at exact content dims → `LLVKContext::createTexture2D` in
  `LLVKUIImage::init`. No GL texture object, state, or callback is ever
  visible to the Vulkan path.
- **Glyph atlases.** GL rasterizes into `LLImageGL`-backed atlases; Vulkan
  builds its own RGBA8 (white + alpha coverage) pages per phase3_v2_ui_plan
  §3 component 5.
- **Draw code.** GL binds to `llrender` (untouched reference); Vulkan binds
  to the `llvkrender`/LLVKUI2D sink. The shared registry supplies *data*;
  each backend turns it into its own geometry (the 9-slice decomposition is
  ported, not shared).

## 5. Module and boundary rules

1. **llui is the GL-free shared layer.** `lluiimagedecls` uses only llxml,
   llfilesystem, llmath, and llcommon. It must never include llrender or GL
   headers; its `EScaleStyle` enum deliberately mirrors — rather than
   includes — `LLUIImage::EScaleStyle` (llrender) and
   `LLVKUIImage::ScaleStyle` (llvulkan), and each consumer maps to its own
   backend enum at the call site.
2. **llvulkan must not execute GL.** It consumes the shared *declarations*
   but performs its own decode + upload. This does not violate the
   fully-parallel rule of phase3_v2_ui_plan.md §3: that rule forbids sharing
   render logic/GL state, not GL-free source data — the same category as the
   already-shared `LLUIColorTable` and `LLFontRegistry`.
3. **The GL reference stays byte-stable.** `LLUIImageList::initFromFile()`
   keeps its exact observable behavior: same merged declarations, same
   two-pass preload ordering (`preload` images first, then the rest), same
   `decodeAllImages` calls, same per-image `preloadUIImage` arguments
   (pixel rects unnormalized; normalization still happens in
   `onUIImageLoaded` against decoded dimensions). Only the XML walk moved.
4. **Failure contract is preserved.** The registry returns `false` exactly
   when the GL parser did: no `textures.xml` found, first (most generic)
   file unparseable, or missing `version` attribute.

## 6. Dynamic / world-texture bridge for Vulkan

World-derived images in the UI (minimap, world map, texture pickers, media
frames) are produced by the fetch/decode/media pipeline as **CPU pixels**.
newview-side hooks hand those pixels to Vulkan through
`LLVKUIImage::updateDynamic(key, pixels, w, h, components, bgra,
source_serial)`, which uploads/updates a `LLVKContext::Texture2D`;
`drawDynamic` emits the quad with the GL media UV rules. The fetch pipeline
itself (LLViewerFetchedTexture decode, LLViewerMedia surfaces) stays
GL-free up to the pixel buffer; only the final `LLImageGL` upload step is
replaced on the Vulkan path. This keeps the world-data path shared at the
CPU level while GPU residency stays per-backend.

## 7. Migration notes

- `LLUIImageList::initFromFile` (indra/newview/llviewertexturelist.cpp) now
  calls `LLUIImageDecls::load()` and iterates `LLUIImageDecls::getDecls()`.
  The local `LLInitParam` blocks (`UIImageDeclaration(s)`) and the
  `TypeValues<LLUIImage::EScaleStyle>` specialization were removed.
- `LLVKUIImage::init` (indra/llvulkan/llvkuiimage.cpp) now calls
  `LLUIImageDecls::load()` + `resolvePaths()` and iterates the registry; its
  hand-rolled parse/merge was deleted. Its public API
  (`init/ready/getSize/draw/drawBorder/drawSolid/updateDynamic/drawDynamic/
  shutdown`) is unchanged; llvkuirender.cpp and llviewerdisplay.cpp are
  unaffected.

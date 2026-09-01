# Phase 3b v2: An independent Vulkan UI pipe

Supersedes the reverted 3b (run-the-GL-tree-on-disabled-GL) approach. That
approach patched gGL to survive a missing GL context — wrong direction: it
couples the backends through gGL's shared mutable state and diverges
invisibly. This design builds an **independent** Vulkan 2D pipe that arrives at
the same pixels, verified byte-exact against the GL reference via the harness.

Grounded in agent analysis of the GL UI pipe (structure, state model, text).
See /memories/repo/ui-draw-path.md and the design notes below.

## Principle: same output, independent implementation

The GL pipe is: view tree → widget draw() → funnel primitives (gl_rect_2d,
gl_draw_scaled_image*, LLFontGL::render) → gGL immediate-mode batcher → flush()
→ GL. The Vulkan pipe reproduces the OUTPUT of those primitives — it does not
run gGL. It implements the same minimal primitive set against the same ortho
projection and UI transform, with the same draw order and state discipline.

The viewer's widget draw() methods bake GL in; we cannot reuse them. So the
Vulkan pipe has its OWN draw of the view tree: it walks LLView and asks each
widget for its geometry through a backend-neutral emission interface, then
rasterizes through the (already proven) LLVKContext 2D pipeline.

## The minimal primitive set (from the analysis)

A Vulkan 2D pipe that renders the login screen needs exactly:

1. Solid triangle rect (BT_ALPHA) — panels, edit backgrounds, selection.
2. Line list/strip — outlines, focus borders, checkbox ticks, text underline.
3. Textured quad (uv + tint) — LLUIImage icons/buttons, media surface.
4. 9-slice textured batch (degenerates to #3) — bordered images.
5. Glyph quads with per-atlas-page texture switch — all text.
6. Blend modes: BT_ALPHA (default), BT_REPLACE, BT_ADD_WITH_ALPHA (glow).
   BT_MULT* are 3D-only, not needed for UI.
7. Scissor (LLLocalClipRect) — line editors / combo lists on the login panel.
8. Strict painter-order (no depth test; submission order is z-order).

All of these are already proven byte-exact by the 3a scene battery except lines
(scissor + line topology) and the 9-slice/glyph batching (which are the same
textured-quad path with more verts / a texture switch).

## The state discipline (correctness-critical)

The GL pipe flushes the pending batch BEFORE any state change (blend, texture,
scissor, shader, matrix). Each batch therefore executes under the state that
was current while it accumulated. The Vulkan pipe MUST reproduce this
flush-before-mutate discipline: a draw batch is emitted whenever blend mode,
texture, scissor, or transform changes, in submission order.

Baked at emit time: position (UI offset/scale applied), per-vertex color,
per-vertex uv. Read at flush time: blend factors, bound texture, scissor rect,
program variant, MVP/texture matrix.

## Coordinate system

- Ortho `ortho(0, w, 0, h, -1, 1)` — GL bottom-left origin, y-up. The existing
  Vulkan pipe's negative-height viewport already reconciles this to the
  top-left UI convention. Reuse it.
- UI transform: a per-frame stack of (offset, scale). A widget's local rect
  becomes screen pixels as (local + Σoffset) × Πscale. The Vulkan pipe carries
  the same stack (fed by the view traversal) and applies it at emit time,
  matching LLRender::vertex3f.

## Text (Piece: fonts)

FreeType rasterization is SHARED (CPU) — the Vulkan path uses the same
LLFontFreetype + LLFontBitmapCache to produce glyph atlases, then uploads each
atlas as a Vulkan texture (the proven createTexture2D path) and draws glyph
quads through the batcher with per-atlas texture switches.

Byte-exact parity requirements (from the text analysis): same FT_Set_Char_Size
(dpi = 96 × scale), same hinting flags (default FT_LOAD_FORCE_AUTOHINT),
FT_RENDER_MODE_NORMAL (grayscale coverage, NOT LCD), MONO→8-bit expansion,
vertical row flip on upload, LA atlas pre-cleared (255,0) with coverage in
alpha, FT_KERNING_UNFITTED + the lsb/rsb ±1px threshold, ll_round(cur_x) after
every advance (integral pen positions), glyph origins pixel-snapped. Parity
means NOT applying the dead italic slant.

## The split point (agent-informed): Seam A extended to the state surface

The view tree's widget draw() methods call a small set of funnel primitives.
The split is at those funnels PLUS the state functions they depend on — a
backend-dispatch layer. Widgets run unchanged on both backends; on Vulkan the
funnels dispatch to a Vulkan sink instead of gGL. gGL is never entered on the
Vulkan path, so the backends stay independent (no shared gGL state, no null-GL
faults).

Geometry emission points (the funnels): gl_rect_2d family, gl_drop_shadow,
gl_draw_scaled_rotated_image, the 9-slice batch, LLFontGL::render/drawGlyph,
LLUIImage inlines. Plus the state surface they read: setSceneBlendType/blendFunc,
texunit-0 bind/unbind, LLScreenClipRect, the UI matrix stack, color4f, and the
gUIProgram/gSolidColorProgram bind. On Vulkan these update pending batcher state
with flush-before-mutate; on GL they behave exactly as today.

LLRender::flush()'s contract (read-state-at-flush, bake-transform-at-emit) is
the written SPECIFICATION for the Vulkan batcher — reused as a spec, not shared
as an implementation.

Frame entry: LLViewerWindow::draw()'s GL preamble (gUIProgram.bind, ortho) is
replaced by the Vulkan frame setup on the Vulkan path; the view-tree traversal
(LLView::drawChildren) runs unchanged and its funnel calls dispatch to Vulkan.

Rejected seams (from the analysis): flush()-reroute shares gGL mutable state
(fatal coupling); backend-virtual widget draw() is ~237 override sites;
a frame-level separate walker duplicates per-widget appearance logic (drift).

Trickiest correctness issue: interleaved texture switches inside a font batch —
the atlas page re-binds mid-string and flushes first, so the Vulkan batcher must
split draws at exactly the same points with the same accumulated state.

## What stays the same

- LLVKContext 2D pipeline, texture upload, readback, and the harness are
  unchanged and reused (3a).
- The GL path is untouched when RenderBackend=OpenGL; the two backends are
  independent at the process level (mutually exclusive, per the decision).
- Selector still requires restart; hidden when no ICD.

## Order of work

1. Emission interface + the view-tree walk producing solid rects + textured
   quads for the login panel (no text yet). Diff a real login frame (panels)
   GL vs Vulkan.
2. Scissor + lines.
3. Fonts (atlas upload + glyph quads) → full login UI.
4. Media surface (login_html) as a textured quad.
5. Full-frame byte-exact diff of the login screen GL vs Vulkan (tol 0 opaque /
   tol 1 alpha).

## Hard edges to watch

- The UI transform must be read at EMIT time per vertex (matching
  LLRender::vertex3f), not at flush — positions are baked per-vertex.
- Texture atlas re-upload per glyph insertion is the GL behavior; the Vulkan
  path can do dirty-rect updates but must produce identical content.
- sRGB: UI textures are mostly authored sRGB; the swapchain is UNORM. The 3a
  scenes were linear-content; real UI textures may need an sRGB read. Verify
  against the GL reference (GL defaults to linear read of an UNORM texture).

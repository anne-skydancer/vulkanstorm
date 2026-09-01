# Phase 3c — Widget-tree transpose plan (login screen → native Vulkan UI)

Status: PLAN (2026-09-01). Precedes implementation. Builds on Phase 3b v2
(commit e45510c145: gl_rect_2d funnel + frame wiring, byte-exact verified).

Goal: run the REAL login-screen widget tree (`mRootView->draw()`) through the
Vulkan 2D pipe (LLVKUI2D sink) with byte-exact parity vs the GL reference, with
zero GL calls on the Vulkan path (no GL context exists; raw GL = null-PFN crash).

---

## 1. Widget-tree inventory (FS login screen)

Login panel: `FSPanelLogin` (indra/newview/fspanellogin.cpp:209/213) loads
`panel_fs_nui_login.xml` (default; legacy setting → `panel_fs_login.xml`).
Widgets in `skins/default/xui/en/panel_fs_nui_login.xml`:

| Widget (class) | Emitted primitives | Funnel status |
|---|---|---|
| panel root (LLPanel::draw, llpanel.cpp:201) | bg: LLUIImage::draw OR gl_rect_2d; FS login calls setBackgroundVisible(false) | rect funneled; image = M2 |
| web_browser (LLMediaCtrl, llmediactrl.cpp:837) | textured quad from media surface + LLViewerDynamicTexture update | M4 (media) |
| icon (LLIconCtrl) | LLUIImage::draw → 9-slice textured | M2 |
| combo_box ×3 (LLComboBox) | LLButton parts (images/rects), embedded line-editor text, drop_down_button image | rect M1 / image M2 / text M3 |
| check_box ×2 (LLCheckBoxCtrl) | LLUIImage checkmark, label text, focus/box rect+lines | rect M1 / image M2 / text M3 |
| button ×4 (LLButton::draw, llbutton.cpp:740) | images (llbutton.cpp:945/956), glow drawSolid BT_ADD_WITH_ALPHA, no-image pink gl_rect_2d (:970), label text (~:1086) | rect M1 / image+glow M2 / text M3 |
| line_editor (LLLineEditor) | bg rect (no-color gl_rect_2d after gGL.color4f), border lines, text, cursor line, LLLocalClipRect scissor, selection rects | M1 (rect+scissor) / text M3 |
| layout_stack / layout_panel | containers only, no emission | safe |
| text_box (LLTextBox) | LLFontGL text only | M3 |

Traversal (`LLView::drawChildren`, llview.cpp:1290-1339) is 100% CPU-safe:
reverse painter's order, `LLUI::pushMatrix/translate` are passthroughs to the
gGL UI stacks (`mUIOffset`/`mUIScale` std::vectors, llrender.cpp:1298-1347 —
pure CPU, no GL). Only leaf emissions and LLRender state calls can crash.

## 2. Crash surface — why naive dispatch fails

`gGL.init(true)` is SKIPPED on the Vulkan path (llviewerwindow.cpp:2125-2136),
so `LLRender::mBuffer` is null and the striders (`mVerticesp`/`mTexcoordsp`/
`mColorsp`, llrender.cpp:905-924) are INVALID. Therefore:

| Reachable call | Failure | Required interception |
|---|---|---|
| `gGL.color4f/4fv/3f/3fv/4ub/4ubv` (llrender.cpp:1954-1990) | writes `mColorsp[mCount]` → crash | Layer 1: intercept in `LLRender::color4ub` (single funnel all variants reach) |
| `gGL.begin/end/vertex*/vertexBatchPreTransformed` (llrender.cpp:1573-1935) | strider writes → crash | must be fully funneled; add fail-fast guard |
| `gGL.flush()` (llrender.cpp:1609) | assert + mBuffer, but ONLY when `mCount>0`; no-op at 0 | safe iff all emission funneled (mCount stays 0) |
| `gGL.setSceneBlendType/blendFunc` (llrender.cpp:1407-1440) | `glBlendFunc` null PFN | Layer 1: intercept → sink.setBlend |
| `LLTexUnit::bind/unbind/enable` (unit 0) | GL calls; `bind(LLImageGL*)` can trigger `createGLTexture` (v1 crash c) | Layer 2: intercept → sink.setTexture (white for unbind) |
| `LLScreenClipRect::updateScissorRegion` (lllocalcliprect.cpp:88-104) | `glScissor` null PFN | Layer 3: intercept → sink.setScissor |
| `LLGLState::setEnabled`/dtor (llgl.cpp:2564-2650) | `glEnable/glDisable` null PFN | Layer 4: update sStateMap, skip GL call |
| `gUIProgram.bind/unbind`, `gSolidColorProgram` | `glUseProgram` null PFN (v1 crash b) | gate in LLViewerWindow::draw preamble |
| `stop_glerror()`→`assert_glerror` (llgl.cpp:2427) | no-op unless `gDebugGL` | SAFE in release |
| matrix ops (`ortho/translatef/pushMatrix/scaleUI`…) | CPU glm/vector; call `flush()` which no-ops at mCount==0 | SAFE once emission funneled |

`LLUI::setLineWidth(1.f)` at llviewerwindow.cpp:2980 — verify implementation;
gate if it calls glLineWidth.

## 3. CRITICAL correctness fix — funnel transform math

`LLRender::vertex3f` (llrender.cpp:1838-1839) bakes: **out = (in + mUIOffset)
\* mUIScale** (offset added BEFORE scale). The sink's `rect()` does exactly
`(left + mOffX) * mScaleX` — matching. Therefore the funnel hook must pass the
stacks RAW: `sink.setTransform(getUITranslation().x/y, getUIScale().x/y)`.

The current `vk_funnel_rect` (llviewerdisplay.cpp) multiplies by
`sUIGLScaleFactor` — WRONG for the general path (that factor is only pre-applied
inside `gl_rect_2d_offset_local`, llrender2dutils.cpp:100-115). Milestone-1
didn't catch this because the test panel called `sink.rect()` directly with the
identity transform. **Fix in M1.**

`gl_rect_2d_offset_local` resets the offset (`loadUIIdentity`) then calls plain
`gl_rect_2d` with pre-scaled coords — correct automatically if the funnel reads
the CURRENT stacks at call time.

## 4. Key unification — `vertexBatchPreTransformed`

Both fonts and images converge on
`LLRender::vertexBatchPreTransformed(verts, uvs, colors, count)`
(llrender.cpp:1894):
- Fonts: LLFontGL::render batches 30 glyphs, flushes + `bind(font_image)` per
  atlas-page switch (llfontgl.cpp:326-340, 379, 417), pixel-snapped
  (`ll_round(cur_render_x...)`, :384-389), BT_ALPHA (:222), underline via LINES
  (:429-436).
- Images: `gl_draw_scaled_rotated_image` degree==0 path pre-transforms CPU-side.

Interception plan: route the 3-array overload to a new sink primitive
`texturedBatchPreTransformed(pos, uv, color, count)` (no transform — already
screen-space; emit under the CURRENT pending texture/blend). The
`LLTexUnit::bind(LLImageGL*)` hook supplies texture identity → Vulkan texture
cache key. Sink's flush-before-mutate on setTexture preserves the exact
page-switch ordering fonts depend on.

`gGL.begin(TRIANGLES)` wrappers around it are safe (set mMode; flush no-ops at
mCount==0). The strider writes inside `vertexBatchPreTransformed` are the only
hazard — intercepted before they run.

## 5. Gating surface in LLViewerWindow::draw (llviewerwindow.cpp:2973-3203)

| Section (:line) | Action on Vulkan path |
|---|---|
| `LLView::sIsDrawing` (:2976) | keep |
| `LLUI::setLineWidth(1.f)` (:2980) | verify; gate if glLineWidth |
| `gGL.matrixMode/loadIdentity` (:2983-2986) | safe (CPU) |
| `sDirtyRect` (:2992) | keep |
| DisplayTimecode block (:2997-3013) | GATE (font render; default off) |
| `gUIProgram.bind` + `gGL.color4f(1,1,1,1)` (:3018-3019) | gate bind; color via Layer 1 |
| `gGL.pushMatrix`/`LLUI::pushMatrix`/`scaleUI` (:3021-3032) | safe (CPU; flush no-ops) |
| zoom block (:3034-3049) | safe (CPU glm) |
| `LLToolMgr ...->draw()` (:3052) | GATE for login milestones |
| exodus mouselook HUD (:3055-3144) | GATE (unreachable at login; gate anyway) |
| `drawMouselookInstructions` (:3147-3151) | GATE |
| **`mRootView->draw()` (:3155)** | **THE TREE — runs, funneled** |
| sDebugRects (:3157-3160) | GATE (default off) |
| top_ctrl (:3163-3174) | keep — widget, funnels; matrix ops CPU |
| overlay title (:3177-3186) | GATE (font; gShowOverlayTitle off) |
| `gUIProgram.unbind` (:3193) | gate |

display_startup (llviewerdisplay.cpp:419+): the Vulkan early-return already
skips its GL body (`gPipeline.updateGL`, `glClear`, `setup2DRender`,
`gViewerWindow->draw`, `gGL.flush`, `LLVertexBuffer::unbind`,
`LLViewerDynamicTexture::updateAllInstances`). vulkan_ui_frame will call
`gViewerWindow->draw()` directly. NOTE: the dynamic-texture update (media
surface refresh for login_html) is currently never run on the Vulkan path —
re-home it in M4.

## 6. Scissor conversion

GL: `glScissor(x=floor(mLeft*scale), y=floor(mBottom*scale), w, h)` —
BOTTOM-left origin (lllocalcliprect.cpp:97-103). The LLRect is in the same
y-up screen space as the ortho. Vulkan scissor is TOP-left device pixels:
`vk_y = deviceH - (floor(mBottom*scale) + h)`. The sink setScissor hook must do
this conversion. Clip-rect stack semantics (intersect-with-parent,
lllocalcliprect.cpp:60-73) are CPU — keep; only updateScissorRegion is
intercepted. Constructor's `mScissorState.setEnabled` → Layer 4 no-op.

## 7. Texture identity (cache keys)

- UI images: `LLViewerFetchedTexture` (BOOST_UI, pinned setNoDelete, deduped by
  name in LLUIImageList::mUIImages). Cache key: texture UUID (`getID()`); the
  funnel receives `LLTexture*` — resolve through the cache (design:
  /memories/repo/ui-texture-cache.md — CPU format expansion L/LA→RGBA8, UNORM,
  LINEAR+clamp UI / NEAREST fonts, deferred destroy).
- Font atlases: `LLFontBitmapCache` pages (`getImageGL(type, page)`,
  llfontgl.cpp:337). Key: (font, glyph-type, page) or the LLImageGL pointer.
  Upload from the bitmap-cache CPU data; NEAREST; LA→RGBA8(L,L,L,A).
- OPEN QUESTION to verify at M1/M3: whether XUI construction / text metrics
  (`getWidth`→`getGlyphInfo`→rasterize→cache insert) touch GL before first
  render on the Vulkan path. Current build boots fine, so construction appears
  GL-free; confirm before gating fonts at render() only — if metrics can
  rasterize, gate the LLFontBitmapCache→LLImageGL upload instead.

## 8. Milestones (each byte-exact verifiable; gates toggled on BOTH backends)

Gate mechanism: env-driven funnel gates (e.g. `VULKANSTORM_UI_GATE=text,images,media`)
applied at the funnel entry points so the GL reference runs the SAME gated tree —
every milestone diffs gated-Vulkan vs gated-GL at tol 0. Final milestone runs
ungated vs ungated.

- **M1 — Real tree, solid chrome only.**
  Fix hook transform (§3); Layer 1 color interception (`color4ub` → sink
  current color; `blendFunc` → sink.setBlend); route the no-color
  `gl_rect_2d` overloads (use tracked color; read transform at call time);
  Layer 4 LLGLState skip; draw() gating per §5; force-gate text/images/media
  to no-ops at the funnel entries. Line primitive: route non-filled
  gl_rect_2d / gl_line_2d → sink.lineStrip.
  Verify: login window chrome (panels, button/box rects, borders) renders;
  gated-Vulkan vs gated-GL diff tol 0. No crash on construction/metrics.

- **M2 — Images.** Texture cache (pinned UI textures, CPU format expansion);
  Layer 2 texunit interception; `vertexBatchPreTransformed` →
  sink.texturedBatchPreTransformed; 9-slice via the same batch path (54
  pre-transformed verts); BT_ADD_WITH_ALPHA glow (blend pipeline exists).
  Ungate images. Diff tol 0 (gates: text,media).

- **M3 — Text.** Font-atlas upload to the Vulkan cache (from LLFontBitmapCache
  CPU data; full-page upload per insert mirrors GL's setImage shortcut);
  per-page bind → sink.setTexture; ungate LLFontGL::render. Watch: underline
  LINES + drop-shadow extra quads ride the same batch. Diff tol 0
  (gates: media).

- **M4 — Media surface + full login.** Re-home
  `LLViewerDynamicTexture::updateAllInstances` for the Vulkan frame; media
  texture upload path → cache; ungate LLMediaCtrl::draw. Ungated Vulkan vs
  ungated GL full-frame diff tol 0 (alpha-mode tol 1 where blending involved,
  per accepted policy). **Login screen fully native.**

- **M5 — Remainder/paranoia sweep.** gl_drop_shadow gradient tris
  (llrender2dutils.cpp:165 — per-vertex alpha; expressible as a textured
  ramp quad or per-vertex-alpha tris — decide by diffing), checkerboard,
  gl_segmented_rect_2d_tex (rounded rects), top_ctrl/menus/tooltips,
  combo dropdowns (floating views), scroll lists. Post-login chrome is a
  separate phase (3d) — out of scope here.

## 9. Risks / non-funnelables

- `flush()` safety invariant depends on mCount==0 — any missed emission path
  breaks it. Mitigation: in debug builds, LL_ERRS in begin/vertex* when the
  Vulkan UI hook is active.
- Blend modes beyond the sink's 4 (BT_MULT/BT_MULT_ALPHA/BT_MULT_X2): not used
  by the login UI per the draw-path map; LL_ERRS if hit (sink assert).
- `sBufferDataList` (display-list recording, llrender.cpp:1664-1672): off by
  default; assert if active on Vulkan path.
- LLFontVertexBuffer (font display-list cache): bakes GL VBOs — MUST be
  disabled/bypassed on the Vulkan path (it would record into null GL). Find
  its enable flag during M3.
- RenderUIBuffer (UI-into-FBO dirty-rect path): default off; assert off.
- LLMediaCtrl is the only login widget needing a per-frame texture UPDATE
  path — isolated to M4.

# Stochastic transparency as a replacement for PPLL

Date: 2026-11-04. Branch `codex/oit-stochastic-replace` (from `vkstorm-devel` at the merge
of PR #305). Scope: study and offline bench only; no viewer C++ or shader changes.

Question (owner, 2026-11-04): could depth-sampled stochastic transparency (Enderton,
Sintorn, Shirley, Luebke, I3D 2010) **replace** PPLL, the viewer's order-independent
transparency method (`RenderAlphaSortMethod 1`, PPLL slots: a fixed 8-slot per-pixel image
array with 8-byte nodes and an order-independent tail), rather than sit beside it as a third
method? The first feasibility study answered the other question (a third method) and
recommended against replacement. This report re-derives the answer for replacement, with a
second bench round aimed at the ideas that could close the performance gap.

All numbers are from `scripts/perf/bench_alpha_oit.py` on this branch unless a source is
cited. Hardware: RTX 5070 Ti (48 MB L2), Windows 11, NVIDIA 621.01 and Mesa 26.3.0-devel
Zink (Vulkan on the same NVIDIA GPU). The bench measures GPU time only, with a trivial
fragment shader and full-screen layers; it does not measure shading cost, draw-call cost or
skinning (section 5 covers the CPU side from the code).

---

## 0. Verdict

**No-go for stochastic transparency as a replacement for PPLL.** No stochastic variant
measured meets the quality bar where PPLL is exact (8 layers per pixel: PPLL 0.73 RMSE /
1 step max; stochastic 6.1 / 21 at S = 8, 9.0 / 21 at S = 4, 3.8 / 17 at half resolution,
which then fails on thin foliage with a max error of 120). The only variant that reaches GPU
parity with PPLL slots on 8 layers is the noisiest (S = 4), and only with a 16-bit depth that
collapses distant layers (19.4 RMSE on the `deep` scene). Every stochastic variant except the
one-pass basic method needs one more geometry pass over the alpha set than PPLL (+100 % of
the non-rigged alpha draws with depth of field off, the default; +50 % of the rigged ones,
and of the non-rigged ones with depth of field on), and the one-pass method is both slower (3.7x at 8 layers) and noisier
(11.5 / 65) than the others.

**Go-with-conditions only for a hybrid that keeps PPLL** and replaces its tail: PPLL slots
stay exact up to 8 layers (bit-identical in the bench) and only pixels past the slots use the
depth-based stochastic estimate. Past the slots it lowers RMSE substantially (full 32:
12.2 -> 7.9 at S = 4; foliage 32: 11.2 -> 2.7) but raises the worst-pixel error (full 32:
max 18 -> 42 steps), and with the depth format a viewer can use (D24; D16 merges distant
layers) it costs 1.86x PPLL's GPU time at 8 layers and 2.13x at 32 at 1440p, plus one
depth-only geometry pass. **The cheaper lever for the same problem is more PPLL slots**:
16 slots cost the same GPU time as 8 up to 32 layers (1.04x), make `foliage 32` exact and
cut full 32 to 6.8 / 9, at 156 B/px instead of 92. The hybrid only wins where pixels go well
past 16 layers (`foliage 64`: 2.7 / 26 against 11.4 / 50 for 16 slots) or where memory for
16 slots is not available. The conditions are therefore an in-world survey showing overflow
past what a raised slot count fixes, and a CPU frame-time gate in a crowded scene (section 9).

Key results, NVIDIA 621.01, 2560x1440 (GPU ms, ratio to PPLL slots in the same process;
RMSE / max error in 8-bit steps against the exact composite). "Passes" = geometry passes
over the alpha set (PPLL: 1). "Sum" = GPU time summed over the 11-scene set, 1440p / 1080p.

| Variant | Passes | B/px | full 8 | deep 8 | full 32 | foliage 32 | Sum 1440 / 1080 |
|---|---|---|---|---|---|---|---|
| PPLL slots, 8 (today) | 1 | 92 | 0.76, 0.73 / 1 | 0.76, 0.73 / 1 | 3.57, 12.2 / 18 | 8.21, 11.2 / 57 | 1.00 / 1.00 |
| PPLL slots, 16 (comparator) | 1 | 156 | 0.79 (1.04x), 0.73 / 1 | 0.79 (1.04x), 0.73 / 1 | 3.71 (1.04x), 6.8 / 9 | 11.43 (1.39x), 0.4 / 1 | 1.40 / 1.49 |
| stoch, S = 8, D32F | 3 | 42 | 3.10 (4.08x), 6.1 / 21 | 1.95 (2.56x), 6.0 / 21 | 8.77 (2.46x), 5.8 / 35 | 4.97 (0.61x), 1.9 / 21 | 1.39 / 1.26 |
| stoch+mrt+d24+s4 | 2 | 26 | 1.21 (1.56x), 9.0 / 21 | 1.20 (1.54x), 8.8 / 21 | 3.28 (0.94x), 8.0 / 42 | 2.96 (0.35x), 2.8 / 34 | 0.67 / 0.59 |
| stoch+mrt+d16+s4 (fastest) | 2 | 18 | 0.75 (0.98x), 9.0 / 21 | 0.69 (0.91x), **19.4** / 24 | 2.18 (0.61x), 8.0 / 42 | 2.04 (0.25x), 2.8 / 34 | 0.45 / 0.53 |
| stoch1 (one pass) | 1 | 100 | 2.89 (3.73x), 11.5 / 65 | 2.88 (3.69x), 11.3 / 55 | 5.62 (1.57x), 8.4 / 43 | 3.18 (0.39x), 2.8 / 35 | 1.14 / 1.33 |
| hybrid+d24+s4 | 2 | 108 | 1.43 (1.86x), 0.73 / 1 | 1.43 (1.85x), 0.73 / 1 | 7.51 (2.13x), 7.9 / 42 | 10.85 (1.29x), 2.7 / 34 | 1.56 / 1.41 |
| hybrid+d16+s4 | 2 | 100 | 1.04 (1.37x), 0.73 / 1 | 1.02 (1.34x), 0.73 / 1 | 5.41 (1.51x), 7.9 / 42 | 9.79 (1.19x), 2.7 / 34 | 1.27 / 1.32 |

The 16-slot row is PPLL with `OIT_BENCH_SLOTS=16` (ratios against the 8-slot PPLL of the
`stoch` process); it exceeds the default 512 MB `RenderAlphaOITMemoryMB` at 1440p (549 MiB).

---

## 1. What "replaces PPLL" has to mean

PPLL today is one shaded capture pass plus one full-screen resolve; exact (to the 8-bit
node quantisation) for up to 8 alpha layers per pixel; an order-independent tail beyond.
A replacement has to be at least as good on everything PPLL is used for, or the viewer gets
worse for the common case. The criteria, with the numbers this study holds each variant to:

| # | Criterion | Bar | Why |
|---|---|---|---|
| R1 | GPU time | <= 1.0x PPLL slots summed over the 11-scene set at 1080p and 1440p, and <= 1.25x on each 8-layer scene (full, bright, deep, occl) | a replacement that costs more on the common case is a regression; overflow scenes may trade |
| R2 | CPU / draw calls | no more geometry passes over the captured alpha set than PPLL (today 1 non-rigged and 2 rigged with depth of field off, the default; section 5), or <= +15 % frame time in a CPU-bound crowded scene | the alpha pass is per-draw CPU work in `LLDrawPoolAlpha::renderAlpha` |
| R3 | Image quality, no TAA | up to 8 layers: RMSE <= 1 step and max <= 2 steps (PPLL: 0.73 / 1); past 8 layers: RMSE below PPLL's tail (12.2 at full 32) | the viewer has no temporal accumulation; per-frame noise is the final image |
| R4 | Memory | <= 92 B/px (PPLL slots) | PPLL's budget (`RenderAlphaOITMemoryMB`) already covers 1440p |
| R5 | Coverage | every path PPLL captures today, with no new exclusion (section 6) | parity |
| R6 | Portability | OpenGL 4.4 on NVIDIA, AMD, Intel and Mesa without new extensions; a Vulkan-line port with OpenGL as the reference | the viewer's support matrix |

R3 is the decisive one. It is not met by any pure stochastic variant at any sample count the
GPU budget allows, so R1 and R2 only decide between the hybrid and nothing.

Scorecard (NVIDIA; R1 sums and worst 8-layer ratio at 1440p / 1080p; R3 at 8 layers is the
worst of the four 8-layer scenes; R5 and R6 are met by every row except as noted in sections
6 and 8):

| Variant | R1 sum | R1 worst 8-layer | R2 passes (PPLL 1) | R3 at 8 layers | R3 past 8 (full 32 / foliage 32 RMSE) | R4 B/px |
|---|---|---|---|---|---|---|
| stoch (S = 8, D32F) | 1.39x / 1.26x fail | 4.08x / 3.09x fail | 3 fail | 6.10 / 21 fail | 5.8 / 1.9 pass | 42 pass |
| stoch+mrt+d24+s4 | 0.67x / 0.59x pass | 1.57x / 1.25x fail | 2 fail | 8.95 / 21 fail | 8.0 / 2.8 pass | 26 pass |
| stoch+mrt+d16+s4 | 0.45x / 0.53x pass | 1.08x / 1.04x pass | 2 fail | 19.36 / 24 (deep) fail | 8.0 / 2.8 pass | 18 pass |
| stoch+half+mrt | 1.07x / 1.24x fail | 2.07x / 2.05x fail | 2 fail | 3.84 / 19 fail | 3.7 / 10.2 (max 120) fail | 19 pass |
| stoch1 (one pass) | 1.14x / 1.33x fail | 4.02x / 3.96x fail | 1 pass | 11.51 / 65 fail | 8.4 / 2.8 pass | 100 fail |
| hybrid+d24+s4 | 1.56x / 1.41x fail | 1.86x / 1.54x fail | 2 fail | 0.85 / 1 pass | 7.9 / 2.7 pass (max 42 vs 18) | 108 fail |
| hybrid+d16+s4 | 1.27x / 1.32x fail | 1.45x / 1.52x fail | 2 fail | 0.85 / 1 pass | 7.9 / 2.7 pass (max 42 vs 18) | 100 fail |
| PPLL, 16 slots (not stochastic) | 1.40x / 1.49x fail | 1.04x / 1.00x pass | 1 pass | 0.85 / 1 pass | 6.8 / 0.4 pass | 156 fail |

The 16-slot sum is dominated by the two foliage scenes (1.39x and 1.69x at 1440p); every
full-screen scene up to 32 layers is 0.87-1.10x.

---

## 2. Bench round 2: what was added

`bench_alpha_oit.py` takes implementation names with `+` options (module docstring); the
existing `ppll`, `slots`, `slots16`, `direct` and `stoch` paths and their checks are
unchanged. New:

- **Multisample depth format**: `d24`, `d16` (default 32F). The visibility test allows one
  unit of the format.
- **`mrt`**: total alpha as a second render target of the accumulation pass: pass T is gone,
  leaving two geometry passes (D depth-only, A shaded) instead of three.
- **`half`**: the stochastic depth pass at half resolution in each axis, seeded with the
  farthest opaque depth of each 2x2 (stored in an R32F image), and a depth-aware upsample in
  pass A: bilinear weights over the four half-resolution neighbours, dropping a neighbour
  whose opaque seed is nearer than the fragment (`oit_bench_scenes.upsample_weights`).
- **`bn`**: blue-noise thresholds (32x32 void-and-cluster, shifted per layer) instead of a
  per-pixel hash for the stratified count R = floor(a S + xi). The mask choice stays hashed.
- **`s4` / `s8`**: per-variant sample count; `OIT_BENCH_SLOTS` sets the slot count of
  `slots` and `hybrid` (default 8), used for the 16-slot comparator.
- **`stoch1`**: basic stochastic transparency with alpha correction, **one geometry pass**
  like PPLL: S-sample RGBA16F colour + S-sample depth with the stratified mask, exact total
  alpha by an image atomic, resolve = mean of the covered samples scaled to total alpha.
- **`hybrid`**: PPLL slots as the core. Pass D as `stoch`; the PPLL capture, in which a
  fragment past the 8th slot adds `vis * a * rgb` and `vis * a` (vis from the S-sample
  depth) and its unweighted `-log2(1 - a)` to the tail; the resolve is PPLL's exact sort
  where a pixel has 8 or fewer fragments and, on overflowing pixels, the depth-based
  estimator over the slot nodes plus the tail, scaled to the exact total alpha.
- Two scenes: **`deep`** (8 layers 5 cm apart at 40 m under a perspective projection with
  near 0.1 m, far 256 m: distinct in 24 bits, not in 16) and **`occl`** (8 layers over 8 px
  opaque bars, for the half-resolution upsample at silhouettes).
- **brmse**: RMSE after a 3x3 binomial filter of image and reference, a proxy for the noise
  seen at viewing distance (dither cancels, bias stays).
- **Measured memory** of each multisample target (`GL_NVX_gpu_memory_info`, available
  video memory before and after allocate + clear).
- Checks: total alpha of every stochastic variant against the exact `1 - prod(1 - a)`
  (all pass, max 0.25/255), and the hybrid's read-back image equal to PPLL slots' value for
  value on every scene without overflow in the window, at least one such scene required
  (pass on NVIDIA and Zink, max difference 0).
- Unit tests (`scripts/tests/test_oit_bench_scenes.py`, 27 tests, unittest): the new
  scenes, the blue-noise map, the upsample weights, the blurred error, and Python models
  of `stoch1` and `hybrid` (the hybrid is exact within the slots and equals the depth-based
  estimate past them).

Method: one process per variant (each with `slots` as its baseline), so that besides PPLL's
slot arrays only that variant's multisample targets and the start-up sample-count probe's
full-resolution D32F targets (S = 4, 8 and 16, kept allocated; 58 and 116 MiB measured, about
230 MiB for 16, at 1440p)
are resident, not all sixteen variants' targets; 30 frames, median, after 5 warm-up frames;
RMSE over a 256x256 window against the exact CPU composite, 8-bit units of linear colour. A
first run with all 16 variants in one process showed outliers (plain `stoch` full 8 at
1440p: 5.88 ms against 3.10 ms in isolation, every pass slower including the composite);
three repeated runs of a 5-variant subset agreed within 1 %.

---

## 3. Results

GPU total in ms per frame (ratio to PPLL slots in the same process), 30-frame median. S = 8
unless the variant says s4. "Geometry passes" counts passes over the alpha set (PPLL: one
capture). "Sum" is the ratio of the summed totals over the 11 scenes. Errors are in 3.4.

### 3.1 NVIDIA 621.01, 2560x1440

| variant | geometry passes | full 2 | full 8 | bright 8 | deep 8 | occl 8 | full 16 | full 32 | sparse 24 | foliage 32 | foliage 64 | sum |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PPLL slots (8) | 1 | 0.19 | 0.76 | 0.77 | 0.76 | 0.59 | 1.69 | 3.57 | 0.67 | 8.21 | 12.33 | 29.64 |
| stoch | 3 | 0.41 (2.18x) | 3.10 (4.08x) | 3.12 (4.07x) | 1.95 (2.56x) | 2.25 (3.81x) | 6.53 (3.87x) | 8.77 (2.46x) | 1.55 (2.32x) | 4.97 (0.61x) | 8.32 (0.68x) | 1.39x |
| stoch+mrt | 2 | 0.41 (2.16x) | 3.13 (4.14x) | 3.14 (4.14x) | 1.94 (2.54x) | 2.25 (3.80x) | 6.62 (3.92x) | 8.87 (2.49x) | 1.44 (2.16x) | 4.78 (0.58x) | 7.83 (0.63x) | 1.37x |
| stoch+d24 | 3 | 0.41 (2.19x) | 2.61 (3.41x) | 2.63 (3.45x) | 2.59 (3.39x) | 1.91 (3.23x) | 5.33 (3.15x) | 7.42 (2.09x) | 1.44 (2.16x) | 5.27 (0.64x) | 8.66 (0.70x) | 1.30x |
| stoch+d16 | 3 | 0.37 (1.99x) | 1.71 (2.24x) | 1.72 (2.26x) | 1.24 (1.62x) | 1.33 (2.25x) | 3.43 (2.03x) | 4.47 (1.25x) | 1.35 (2.03x) | 3.80 (0.46x) | 6.55 (0.53x) | 0.88x |
| stoch+mrt+d16 | 2 | 0.37 (1.99x) | 1.68 (2.21x) | 1.70 (2.22x) | 1.19 (1.56x) | 1.33 (2.24x) | 3.35 (1.98x) | 4.30 (1.21x) | 1.24 (1.86x) | 3.50 (0.43x) | 5.97 (0.48x) | 0.84x |
| stoch+s4 | 3 | 0.22 (1.16x) | 1.33 (1.74x) | 1.34 (1.76x) | 1.03 (1.35x) | 1.02 (1.72x) | 2.95 (1.75x) | 3.46 (0.97x) | 1.09 (1.63x) | 2.96 (0.36x) | 5.07 (0.41x) | 0.70x |
| stoch+bn | 3 | 0.41 (2.18x) | 3.11 (4.08x) | 3.12 (4.10x) | 1.96 (2.56x) | 2.25 (3.81x) | 6.54 (3.87x) | 8.76 (2.46x) | 1.56 (2.33x) | 4.97 (0.61x) | 8.31 (0.67x) | 1.39x |
| stoch+bn+s4 | 3 | 0.22 (1.16x) | 1.33 (1.77x) | 1.34 (1.78x) | 1.03 (1.37x) | 1.02 (1.72x) | 2.95 (1.75x) | 3.47 (0.97x) | 1.09 (1.63x) | 2.96 (0.36x) | 5.10 (0.41x) | 0.70x |
| stoch+mrt+d24+s4 | 2 | 0.23 (1.17x) | 1.21 (1.56x) | 1.21 (1.57x) | 1.20 (1.54x) | 0.93 (1.54x) | 2.57 (1.53x) | 3.28 (0.94x) | 1.04 (1.55x) | 2.96 (0.35x) | 5.11 (0.41x) | 0.67x |
| stoch+mrt+d16+s4 | 2 | 0.25 (1.34x) | 0.75 (0.98x) | 0.74 (0.98x) | 0.69 (0.91x) | 0.64 (1.08x) | 1.38 (0.82x) | 2.18 (0.61x) | 0.85 (1.27x) | 2.04 (0.25x) | 3.77 (0.31x) | 0.45x |
| stoch+mrt+d16+bn+s4 | 2 | 0.25 (1.34x) | 0.75 (0.99x) | 0.75 (0.98x) | 0.70 (0.91x) | 0.64 (1.09x) | 1.39 (0.82x) | 2.19 (0.61x) | 0.88 (1.31x) | 2.06 (0.25x) | 3.78 (0.31x) | 0.46x |
| stoch+half+mrt | 2 | 0.40 (2.11x) | 1.47 (1.93x) | 1.46 (1.91x) | 1.46 (1.93x) | 1.23 (2.07x) | 2.89 (1.71x) | 5.41 (1.51x) | 1.83 (2.74x) | 5.12 (0.62x) | 10.13 (0.82x) | 1.07x |
| stoch1 | 1 | 0.94 (5.00x) | 2.89 (3.73x) | 2.78 (3.56x) | 2.88 (3.69x) | 2.39 (4.02x) | 5.61 (3.30x) | 5.62 (1.57x) | 2.31 (3.48x) | 3.18 (0.39x) | 4.82 (0.39x) | 1.14x |
| stoch1+bn | 1 | 0.94 (5.02x) | 2.89 (3.79x) | 2.78 (3.63x) | 2.88 (3.79x) | 2.39 (4.03x) | 5.60 (3.31x) | 5.62 (1.57x) | 2.33 (3.49x) | 3.19 (0.39x) | 4.83 (0.39x) | 1.14x |
| hybrid | 2 | 0.44 (2.34x) | 2.53 (3.32x) | 2.51 (3.28x) | 2.09 (2.74x) | 1.88 (3.16x) | 8.17 (4.84x) | 12.11 (3.40x) | 2.03 (3.03x) | 12.60 (1.53x) | 19.25 (1.56x) | 2.16x |
| hybrid+d24 | 2 | 0.44 (2.27x) | 2.22 (2.89x) | 2.21 (2.87x) | 2.22 (2.89x) | 1.68 (2.78x) | 7.15 (4.26x) | 10.93 (3.13x) | 1.96 (2.92x) | 12.84 (1.53x) | 19.69 (1.59x) | 2.08x |
| hybrid+d16 | 2 | 0.39 (2.04x) | 1.91 (2.50x) | 1.90 (2.48x) | 1.50 (1.95x) | 1.47 (2.48x) | 5.86 (3.46x) | 8.73 (2.45x) | 1.83 (2.74x) | 11.44 (1.39x) | 16.76 (1.36x) | 1.75x |
| hybrid+d24+s4 | 2 | 0.28 (1.47x) | 1.43 (1.86x) | 1.43 (1.85x) | 1.43 (1.85x) | 1.11 (1.83x) | 4.93 (2.94x) | 7.51 (2.13x) | 1.63 (2.43x) | 10.85 (1.29x) | 15.72 (1.27x) | 1.57x |
| hybrid+d16+s4 | 2 | 0.31 (1.63x) | 1.04 (1.37x) | 1.05 (1.37x) | 1.02 (1.34x) | 0.86 (1.45x) | 3.47 (2.05x) | 5.41 (1.51x) | 1.38 (2.07x) | 9.79 (1.19x) | 13.23 (1.07x) | 1.27x |

### 3.2 NVIDIA 621.01, 1920x1080

| variant | geometry passes | full 2 | full 8 | bright 8 | deep 8 | occl 8 | full 16 | full 32 | sparse 24 | foliage 32 | foliage 64 | sum |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PPLL slots (8) | 1 | 0.11 | 0.44 | 0.44 | 0.44 | 0.35 | 0.92 | 1.88 | 0.38 | 4.05 | 5.50 | 14.56 |
| stoch | 3 | 0.16 (1.55x) | 1.35 (3.10x) | 1.35 (3.08x) | 0.95 (2.17x) | 0.95 (2.73x) | 3.03 (3.29x) | 3.50 (1.86x) | 0.88 (2.31x) | 2.21 (0.54x) | 3.81 (0.69x) | 1.26x |
| stoch+mrt | 2 | 0.17 (1.55x) | 1.33 (3.03x) | 1.33 (3.02x) | 0.94 (2.13x) | 0.97 (2.79x) | 2.98 (3.24x) | 3.41 (1.83x) | 0.85 (2.24x) | 2.08 (0.51x) | 3.55 (0.65x) | 1.22x |
| stoch+d24 | 3 | 0.16 (1.54x) | 1.10 (2.51x) | 1.11 (2.52x) | 1.10 (2.51x) | 0.82 (2.39x) | 2.43 (2.65x) | 3.04 (1.62x) | 0.81 (2.14x) | 2.33 (0.58x) | 3.98 (0.73x) | 1.17x |
| stoch+d16 | 3 | 0.18 (1.74x) | 0.60 (1.37x) | 0.60 (1.37x) | 0.56 (1.28x) | 0.52 (1.49x) | 1.13 (1.23x) | 1.70 (0.91x) | 0.76 (2.01x) | 1.52 (0.38x) | 2.79 (0.51x) | 0.72x |
| stoch+mrt+d16 | 2 | 0.19 (1.75x) | 0.59 (1.33x) | 0.59 (1.33x) | 0.55 (1.25x) | 0.51 (1.46x) | 1.11 (1.20x) | 1.64 (0.87x) | 0.73 (1.92x) | 1.38 (0.34x) | 2.53 (0.46x) | 0.68x |
| stoch+s4 | 3 | 0.13 (1.22x) | 0.46 (1.06x) | 0.47 (1.08x) | 0.45 (1.04x) | 0.39 (1.15x) | 0.91 (0.99x) | 1.34 (0.72x) | 0.61 (1.62x) | 1.18 (0.29x) | 2.16 (0.39x) | 0.56x |
| stoch+bn | 3 | 0.17 (1.56x) | 1.35 (3.09x) | 1.35 (3.08x) | 0.95 (2.16x) | 0.95 (2.73x) | 3.02 (3.28x) | 3.50 (1.86x) | 0.88 (2.30x) | 2.21 (0.55x) | 3.81 (0.69x) | 1.26x |
| stoch+bn+s4 | 3 | 0.13 (1.20x) | 0.46 (1.07x) | 0.47 (1.08x) | 0.45 (1.05x) | 0.39 (1.15x) | 0.91 (0.99x) | 1.34 (0.71x) | 0.61 (1.62x) | 1.18 (0.29x) | 2.17 (0.40x) | 0.56x |
| stoch+mrt+d24+s4 | 2 | 0.13 (1.27x) | 0.46 (1.05x) | 0.45 (1.03x) | 0.47 (1.09x) | 0.43 (1.25x) | 0.88 (0.96x) | 1.37 (0.74x) | 0.62 (1.64x) | 1.21 (0.30x) | 2.33 (0.42x) | 0.59x |
| stoch+mrt+d16+s4 | 2 | 0.14 (1.38x) | 0.42 (0.96x) | 0.42 (0.96x) | 0.41 (0.93x) | 0.36 (1.05x) | 0.78 (0.86x) | 1.25 (0.67x) | 0.52 (1.37x) | 1.17 (0.29x) | 2.17 (0.39x) | 0.53x |
| stoch+mrt+d16+bn+s4 | 2 | 0.14 (1.38x) | 0.42 (0.96x) | 0.42 (0.96x) | 0.41 (0.93x) | 0.36 (1.05x) | 0.79 (0.86x) | 1.25 (0.67x) | 0.52 (1.36x) | 1.17 (0.29x) | 2.18 (0.40x) | 0.53x |
| stoch+half+mrt | 2 | 0.26 (2.51x) | 0.83 (1.91x) | 0.83 (1.92x) | 0.83 (1.91x) | 0.70 (2.05x) | 1.64 (1.80x) | 3.06 (1.63x) | 1.06 (2.80x) | 2.90 (0.72x) | 5.68 (1.03x) | 1.23x |
| stoch1 | 1 | 0.53 (5.00x) | 1.64 (3.78x) | 1.57 (3.63x) | 1.64 (3.77x) | 1.36 (3.96x) | 3.16 (3.46x) | 3.20 (1.72x) | 1.34 (3.54x) | 1.83 (0.46x) | 2.79 (0.51x) | 1.32x |
| stoch1+bn | 1 | 0.53 (5.18x) | 1.71 (3.95x) | 1.57 (3.63x) | 1.64 (3.77x) | 1.35 (3.99x) | 3.16 (3.47x) | 3.20 (1.72x) | 1.39 (3.67x) | 1.83 (0.46x) | 2.79 (0.51x) | 1.33x |
| hybrid | 2 | 0.21 (2.00x) | 1.35 (3.12x) | 1.34 (3.10x) | 1.10 (2.53x) | 0.99 (2.89x) | 4.47 (4.89x) | 6.43 (3.47x) | 1.07 (2.82x) | 6.32 (1.57x) | 8.97 (1.64x) | 2.23x |
| hybrid+d24 | 2 | 0.21 (1.94x) | 1.17 (2.66x) | 1.16 (2.65x) | 1.17 (2.66x) | 0.88 (2.53x) | 3.82 (4.15x) | 5.70 (3.06x) | 1.00 (2.64x) | 6.51 (1.60x) | 9.43 (1.71x) | 2.14x |
| hybrid+d16 | 2 | 0.23 (2.19x) | 0.78 (1.79x) | 0.78 (1.80x) | 0.75 (1.73x) | 0.65 (1.90x) | 2.32 (2.53x) | 3.42 (1.85x) | 0.91 (2.40x) | 5.44 (1.35x) | 7.20 (1.32x) | 1.55x |
| hybrid+d24+s4 | 2 | 0.19 (1.87x) | 0.64 (1.48x) | 0.64 (1.47x) | 0.65 (1.48x) | 0.53 (1.54x) | 1.99 (2.17x) | 2.98 (1.61x) | 0.79 (2.11x) | 5.14 (1.27x) | 6.67 (1.22x) | 1.40x |
| hybrid+d16+s4 | 2 | 0.20 (1.90x) | 0.63 (1.45x) | 0.63 (1.45x) | 0.62 (1.42x) | 0.52 (1.52x) | 1.86 (2.03x) | 2.74 (1.46x) | 0.67 (1.77x) | 4.94 (1.22x) | 6.26 (1.13x) | 1.32x |

### 3.3 Mesa Zink (Vulkan on the same GPU), 2560x1440

Mesa 26.3.0-devel Zink, Vulkan 1.4 on the same RTX 5070 Ti; GL_MAX_DEPTH_TEXTURE_SAMPLES = 8.
Ratios are against the PPLL run in the same process; the PPLL row shown is from the `stoch`
process. Zink's PPLL time on `foliage 64` was bimodal across processes (12.2-12.3 ms in five,
4.7-4.9 ms in two, including the one shown), and the hybrid rows there are as erratic, so
that column and the sums are indicative only.
All checks passed on Zink, including the hybrid's bit-identity with PPLL below 9 layers.

| variant | geometry passes | full 2 | full 8 | bright 8 | deep 8 | occl 8 | full 16 | full 32 | sparse 24 | foliage 32 | foliage 64 | sum |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PPLL slots (8) | 1 | 0.21 | 0.86 | 0.86 | 0.86 | 0.72 | 1.77 | 3.74 | 0.72 | 8.03 | 4.69 | 22.58 |
| stoch | 3 | 1.18 (5.48x) | 4.05 (4.71x) | 4.05 (4.69x) | 3.09 (3.59x) | 1.57 (2.17x) | 7.55 (4.25x) | 9.88 (2.64x) | 2.01 (2.77x) | 5.28 (0.66x) | 5.51 (1.17x) | 1.99x |
| stoch+mrt+d16 | 2 | 0.59 (2.74x) | 1.88 (2.18x) | 1.88 (2.18x) | 1.03 (1.19x) | 0.97 (1.33x) | 3.53 (1.99x) | 4.44 (1.19x) | 1.24 (1.70x) | 3.50 (0.44x) | 5.87 (0.48x) | 1.12x |
| stoch+mrt+d16+bn+s4 | 2 | 0.26 (1.19x) | 0.74 (0.86x) | 0.74 (0.85x) | 0.70 (0.81x) | 0.90 (1.17x) | 1.36 (0.76x) | 2.12 (0.57x) | 0.80 (1.11x) | 1.99 (0.25x) | 3.66 (0.30x) | 0.59x |
| stoch+half+mrt | 2 | 0.41 (1.90x) | 1.33 (1.54x) | 1.32 (1.53x) | 1.36 (1.57x) | 1.16 (1.60x) | 2.55 (1.44x) | 4.62 (1.24x) | 1.77 (2.44x) | 3.50 (0.44x) | 6.82 (0.56x) | 1.11x |
| stoch1 | 1 | 1.31 (6.14x) | 3.41 (3.96x) | 3.28 (3.81x) | 3.21 (3.72x) | 5.85 (8.06x) | 6.08 (3.46x) | 6.05 (1.62x) | 2.43 (3.35x) | 3.42 (0.42x) | 5.44 (1.11x) | 1.83x |
| hybrid+d24+s4 | 2 | 0.53 (2.45x) | 1.93 (2.24x) | 1.92 (2.22x) | 1.33 (1.53x) | 1.14 (1.55x) | 3.94 (2.22x) | 6.22 (1.67x) | 1.35 (1.86x) | 9.62 (1.19x) | 7.94 (0.65x) | 1.60x |
| hybrid+d16+s4 | 2 | 0.31 (1.46x) | 1.16 (1.35x) | 1.16 (1.35x) | 1.17 (1.36x) | 0.99 (1.33x) | 2.23 (1.29x) | 4.14 (1.15x) | 1.07 (1.49x) | 8.43 (1.03x) | 5.00 (0.41x) | 1.14x |

### 3.4 Quality past and within the slots (the deciding numbers)

RMSE / max / brmse in 8-bit steps, 256x256 window (identical at 1080p and 1440p and on
Zink, where measured). `mrt`, `d24` and `bn` do not change the error except where noted:
`d16` changes only `deep 8`; `bn` changes only the brmse of `bright` and `foliage` (section
4). B/px is format arithmetic, with the multisample target measured at 1440p in brackets.

| Variant | B/px | full 2 | full 8 | bright 8 | deep 8 | occl 8 | full 16 | full 32 | sparse 24 | foliage 32 | foliage 64 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| PPLL slots (8) | 92 | 0.70 / 1 / 0.70 | 0.73 / 1 / 0.73 | 0.85 / 1 / 0.85 | 0.73 / 1 / 0.73 | 0.69 / 1 / 0.69 | 7.55 / 11 / 7.55 | 12.19 / 18 / 12.19 | 11.63 / 18 / 11.51 | 11.19 / 57 / 5.14 | 14.59 / 54 / 6.57 |
| PPLL slots (16; RMSE / max only) | 156 | 0.70 / 1 | 0.73 / 1 | 0.85 / 1 | 0.73 / 1 | 0.69 / 1 | 0.79 / 1 | 6.81 / 9 | 8.85 / 11 | 0.37 / 1 | 11.41 / 50 |
| stoch S = 8 (also mrt, d24, bn) | 42 (116 MiB) | 0.09 / 0 / 0.09 | 6.10 / 21 / 2.31 | 1.06 / 5 / 0.40 | 5.96 / 21 / 2.25 | 6.02 / 21 / 2.29 | 5.66 / 30 / 2.12 | 5.76 / 35 / 2.22 | 6.72 / 30 / 2.55 | 1.92 / 21 / 0.72 | 1.84 / 21 / 0.69 |
| stoch+d16 S = 8 | 26 (58 MiB) | as stoch | as stoch | as stoch | **19.36 / 24 / 19.35** | as stoch | as stoch | as stoch | as stoch | as stoch | as stoch |
| stoch S = 4 (also mrt, d24) | 26 (58 MiB) | 0.09 / 0 / 0.09 | 8.95 / 21 / 3.42 | 1.69 / 9 / 0.66 | 8.82 / 21 / 3.44 | 8.83 / 21 / 3.36 | 7.98 / 32 / 3.07 | 8.00 / 42 / 3.20 | 9.59 / 37 / 3.72 | 2.76 / 34 / 1.04 | 2.65 / 26 / 1.00 |
| stoch+mrt+d16+s4 | 18 (29 MiB) | as S = 4 | as S = 4 | as S = 4 | **19.36 / 24 / 19.35** | as S = 4 | as S = 4 | as S = 4 | as S = 4 | as S = 4 | as S = 4 |
| stoch+half+mrt | 19 (30 MiB) | 0.09 / 0 / 0.09 | 3.84 / 17 / 3.06 | 0.64 / 4 / 0.51 | 3.70 / 19 / 2.92 | 3.81 / 16 / 3.03 | 3.68 / 22 / 2.92 | 3.68 / 21 / 2.92 | 6.63 / 34 / 4.37 | **10.17 / 120 / 4.98** | **9.77 / 154 / 4.37** |
| stoch1 | 100 (346 MiB) | 3.77 / 14 / 1.53 | 11.51 / 65 / 4.48 | 3.42 / 22 / 1.32 | 11.25 / 55 / 4.23 | 11.31 / 65 / 4.46 | 10.57 / 48 / 3.99 | 8.44 / 43 / 3.23 | 10.74 / 53 / 4.09 | 2.75 / 35 / 1.04 | 2.82 / 31 / 1.06 |
| hybrid S = 8 | 124 (D32F, D24) / 108 (D16) | 0.70 / 1 / 0.70 | 0.73 / 1 / 0.73 | 0.85 / 1 / 0.85 | 0.73 / 1 / 0.73 | 0.69 / 1 / 0.69 | 5.65 / 30 / 2.15 | 5.73 / 34 / 2.27 | 6.77 / 30 / 2.66 | 1.88 / 22 / 0.76 | 1.87 / 20 / 0.73 |
| hybrid S = 4 | 108 (D24) / 100 (D16) | 0.70 / 1 / 0.70 | 0.73 / 1 / 0.73 | 0.85 / 1 / 0.85 | 0.73 / 1 / 0.73 | 0.69 / 1 / 0.69 | 7.94 / 32 / 3.06 | 7.94 / 42 / 3.17 | 9.63 / 37 / 3.84 | 2.67 / 34 / 1.06 | 2.68 / 26 / 1.03 |

The S = 16 column of the plain `stoch` table (two 8-sample iterations on NVIDIA): full 8
4.40 / 21, full 32 4.21 / 27; the `stoch` printout has all three sample counts per scene.

Images for judging the noise by eye: `doc/oit/png/{full8,bright8,foliage32,full32}_1440p.png`,
each seven 256x256 tiles of the RMSE window (sRGB), left to right: exact composite, PPLL
slots, stoch S = 8, `stoch+mrt+d16+s4`, `stoch+half+mrt`, `stoch1`, `hybrid+d16+s4` (the
D24 hybrid's image is identical). On `full 8` the PPLL and hybrid tiles match the exact one,
and every stochastic tile shows an even grain over the whole surface, coarsest for `stoch1`.

---

## 4. What closes the gap, and what does not

Pass D (the stochastic depth pass) is the cost, and its cost is set by whether the
S-sample depth target fits the GPU's L2 cache (48 MB on the RTX 5070 Ti):

Pass D (opaque seed + stochastic depth) and pass A at full 8, NVIDIA 621.01; target size is
format arithmetic in MB (10^6 bytes; the measured MiB agree).

| Resolution | Variant | MS depth target | Pass D, 8 layers | per layer | Pass A, 8 layers |
|---|---|---|---|---|---|
| 1440p | stoch (D32F, S = 8) | 118 MB | 1.78 ms | 0.22 ms | 1.14 ms |
| 1440p | stoch+d24 | 118 MB | 1.46 ms | 0.18 ms | 0.96 ms |
| 1440p | stoch+d16 | 59 MB | 1.15 ms | 0.14 ms | 0.37 ms |
| 1440p | stoch+s4 (D32F) | 59 MB | 0.83 ms | 0.10 ms | 0.32 ms |
| 1440p | stoch+mrt+d16+s4 | 29 MB | 0.32 ms | 0.04 ms | 0.36 ms |
| 1440p | stoch+half+mrt (D32F, S = 8) | 29 MB | 0.23 ms | 0.03 ms | 1.17 ms |
| 1080p | stoch (D32F, S = 8) | 66 MB | 0.89 ms | 0.11 ms | 0.36 ms |
| 1080p | stoch+d24 | 66 MB | 0.71 ms | 0.09 ms | 0.29 ms |
| 1080p | stoch+d16 | 33 MB | 0.32 ms | 0.04 ms | 0.18 ms |
| 1080p | stoch+s4 (D32F) | 33 MB | 0.23 ms | 0.03 ms | 0.14 ms |
| 1080p | stoch+mrt+d16+s4 | 17 MB | 0.19 ms | 0.02 ms | 0.19 ms |

Below about 33 MB the pass costs 0.02-0.04 ms per full-screen layer; at 59 MB 0.10-0.14 ms,
at 66-118 MB 0.09-0.22 ms. At 4K (8.3 Mpx) the S = 4 D16 target is 66 MB and would leave the
L2 again; on GPUs with a smaller L2 (most AMD and Intel parts, older NVIDIA) the cliff comes
earlier. This was not measured.

- **16-bit depth: closes most of the GPU gap, and fails on distance.** D16 halves the target
  (S = 8 at 1440p: 58 MiB measured, against 116 MiB for D32F and D24, which the driver
  stores at 4 bytes) and cuts pass D by about a third and pass A by two thirds at 1440p. At
  1080p S = 8 D16 is 33 MB and fits the L2: 0.60 ms against 1.35 ms for D32F at full 8. But
  window depth from a perspective projection has about 0.24 m of resolution at 40 m in 16
  bits; the `deep` scene (layers 5 cm apart at 40 m) goes from 5.96 to 19.4 RMSE: layers
  merge and occlude each other. A 16-bit **linear** eye depth (about 4 mm everywhere over
  256 m) would avoid that but needs `gl_FragDepth` in pass D and was not measured.
- **D24**: no memory saving (stored as 4 bytes), 16-19 % faster than D32F at full 8 but slower
  on `deep` (2.59 against 1.95 ms at 1440p); not a lever by itself. It is the format a viewer
  would need instead of D16.
- **S = 4**: halves the target, reaches parity on overflow-heavy scenes and about 1.7x on 8
  layers, but raises RMSE at 8 layers from 6.1 to 9.0. Further from R3, not closer.
- **Folding total alpha into pass A (`mrt`)**: removes pass T (0.14 ms at 8 layers, 0.47 ms
  at 32 at 1440p) and, more importantly, one CPU traversal: three geometry passes become
  two. Necessary for any stochastic route; not sufficient. Image atomics would also work
  (`stoch1` uses one) but are not needed when A writes a second target.
- **The combination** `stoch+mrt+d16+s4` (18 B/px; `+bn` measures the same) is the only pure
  stochastic variant at or below PPLL's GPU time on 8 layers (0.91-0.98x at 1440p, 0.97-1.04x
  at 1080p) and 0.25-0.61x on overflow scenes; 0.45x summed over the scene set at 1440p and
  0.53x at 1080p. Its quality is 8.95 / 21 RMSE / max at 8 layers and 19.4 on `deep`. With
  D24 instead (`stoch+mrt+d24+s4`, the depth a viewer could use), 1.56x at 8 layers at 1440p
  and 1.05x at 1080p.
- **More PPLL slots** (not stochastic, measured as the comparator): 16 slots cost 1.00-1.04x
  of 8 slots on every scene up to 32 full-screen layers (full 16 is faster, 0.87x, because
  no fragment reaches the tail's atomics), 1.39x on `foliage 32` (now exact, 0.37 / 1) and
  1.69x on `foliage 64` (11.4 / 50). Memory 156 B/px (549 MiB at 1440p, above the default
  512 MB budget).
- **Half-resolution depth with depth-aware upsample**: pass D drops to 0.23 ms at 8 layers,
  but pass A then fetches 4 x S samples and becomes the cost (1.17 ms): 1.9x PPLL. The
  four-neighbour average lowers per-pixel RMSE on broad layers (3.84 vs 6.10) but the error
  becomes blotchy (brmse 3.06 vs 2.31), and thin geometry fails: `foliage 32` RMSE 10.2 /
  max 120, because the half-resolution pass sees coverage at one pixel of each 2x2. The
  opaque-silhouette test (`occl`) is handled by the seed test (3.81 RMSE, no worse than
  `full`). Not a route.
- **Blue-noise thresholds**: no change where alpha is on the k/S grid (`full`, alpha 0.5 at
  S = 4 or 8: the count is exact and all the noise comes from which samples overlap between
  layers), small gains where it is not (`bright 8` brmse 0.66 -> 0.59 at S = 4, `foliage 32`
  1.04 -> 0.94). The dominant noise is the inter-layer mask overlap, which a threshold
  dither does not touch; the stratification that does touch it (Laine and Karras 2011) is,
  in the authors' words, "most probably not feasible to implement in shaders".
- **One-pass basic stochastic transparency (`stoch1`)**: the only stochastic form with
  PPLL's pass count (R2 met), but the S-sample RGBA16F colour target makes it 3.7x PPLL at 8
  layers and 1.6x at 32, at 100 B/px, with the worst quality measured (11.5 / 65 at full 8;
  3.8 / 14 even at two layers, where the depth-based method is exact). Fails R1, R3, R4.
- **Hybrid (PPLL slots + stochastic overflow)**: identical to PPLL within the slots and a
  lower RMSE past them (though a higher worst-pixel error: full 32 max 42 against 18), and at
  `d16+s4` costs 1.37x PPLL at 8 layers and 1.51x at 32 at 1440p. It fails R1 and R2 as a
  replacement, but it is not one: it is PPLL with a different tail. With D16 the overflow
  pixels inherit the 16-bit merge problem on distant layers (not exercised: `deep` has 8
  layers, no overflow); a viewer prototype should use D24, which costs more:
  `hybrid+d24+s4` is 1.86x PPLL at 8 layers and 2.13x at 32 at 1440p (1.48x and 1.61x at
  1080p), because the 58 MB S = 4 D24 target leaves the L2. Against 16 PPLL slots it wins
  only on `foliage 64` (section 0).

What would close the quality gap is temporal accumulation (every shipping use of
stochastic or hashed transparency found relies on TAA; Wyman 2016 Fig. 9; Brüll, Kern and
Grosch 2025). The viewer has none, and adding TAA is a renderer-wide project with its own
OpenGL-reference implications for the Vulkan line; it is not in scope and nothing here
assumes it.

---

## 5. CPU and draw calls

There is no alpha draw counter in the viewer and no measurement of per-draw CPU cost or of
alpha draw counts in the repository (searched `doc/`, `scripts/perf/`); **neither was
measured in this round** (no viewer changes). What the code fixes:

Traversals of the alpha pool per frame, main view, post-water, PPLL on
(`LLDrawPoolAlpha::renderPostDeferred`, `lldrawpoolalpha.cpp` 202-279):

| Side | PPLL today | draws per captured draw |
|---|---|---|
| rigged | capture; depth-only replay (full lit programs, for glow depth); residual (queues emissives, draws only particles/custom-blend) | 2 |
| non-rigged | capture; residual (as above); DoF depth (`gDeferredFullbrightAlphaMaskProgram` or the full lit programs) only when `RenderDepthOfField` is on (`lldrawpoolalpha.cpp` 281; default off, `settings.xml` 11918) | 1, or 2 with DoF |

Legacy sorted alpha is 1 (rigged) and 1 or 2 (non-rigged, forward, + DoF). Under stochastic
transparency the capture becomes D + A (with `mrt`) or T + D + A (without):

| Side | PPLL | D + A (`mrt`) or hybrid (D + capture) | T + D + A |
|---|---|---|---|
| non-rigged, DoF off (default) | 1 | 2 (**+100 %**) | 3 (+200 %) |
| non-rigged, DoF on | 2 | 3 (+50 %) | 4 (+100 %) |
| rigged | 2 | 3 (+50 %) | 4 (+100 %) |

Only `stoch1` keeps PPLL's counts.

Per draw, `renderAlpha` (lines 750-1141) repeats for every pass: the group walk and water
test, `applyModelMatrix`, shader selection and, on every program change,
`bindDeferredShaderFast` (light, shadow-map and reflection-probe binds); the per-draw
material uniforms; `TexSetup` (diffuse, and normal/specular for materials); `blendFunc`;
`setBuffer`; `drawRange`; and for rigged draws `uploadMatrixPalette` when the avatar, mesh
or program changes. Draws are ordered by distance, not by program, so program changes are
frequent. A depth-only pass D could use a camera-view alpha-only program (the shadow pass's
`gDeferredShadowAlphaMaskProgram`, `gDeferredShadowFullbrightAlphaMaskProgram` and
`gDeferredShadowGLTFAlphaBlendProgram`, with skinned variants, are the model; they are
built for the light projection and would need camera variants) and skip the light, shadow,
probe, normal and specular binds; it cannot skip the model matrix, the diffuse bind (alpha
comes from the texture), the texture matrix, the palette upload, the buffer bind or the draw
itself, and since order does not matter in D it could be sorted by program and texture,
which the shared loop cannot do today.

So the extra pass costs between "the draw-call submission floor" (alpha-only program, sorted)
and "a second full alpha pass" (today's loop). The study's Stage 2 gate (+15 % frame time in
a CPU-bound crowded scene) is the measurement that decides it; it needs a draw counter in
`renderAlpha` (e.g. in the `RenderAlphaOITStats` log) and the existing `LLGLSLShader`
profiling (`sProfileEnabled`, per-program binds and GPU time). Two existing traversals
already redraw (part of) the alpha set with full lit programs only to write depth (the
rigged replay, and DoF when it is on): giving those an alpha-only program is an independent saving that would pay for
part of an extra pass, whatever is decided here.

---

## 6. Coverage of the alpha paths PPLL covers

From `lldrawpoolalpha.cpp` and the shader manager; "captured" means it goes through the
PPLL capture today.

| Path | PPLL today | Under a stochastic replacement | Under the hybrid |
|---|---|---|---|
| Simple (Blinn-Phong) alpha, `alphaF.glsl` | captured | D + A, alpha-only program for D | D + capture |
| Fullbright alpha, `fullbrightF.glsl` | captured | same | same |
| Materials (normal/spec, blend), `materialF.glsl` | captured | same | same |
| PBR alpha blend (object faces), `pbralphaF.glsl` | captured | same | same |
| Rigged variants of the above | captured, then depth-only replay | D + A with skinning in both | D + capture, skinning in both |
| glTF scene assets (`drawGLTFScene`, `pbrmetallicroughnessF.glsl`) | excluded (drawn before the capture with depth writes) | excluded | excluded (2026-11-11, `stochastic-transparency.md`: the shipped engine draws the blended assets in its passes with alpha-only variants, like the faces) |
| Emissive / glow | residual replay into `screen.a` | unchanged | unchanged |
| Particles, HUD particles | residual, legacy sorted | unchanged (stochastic is at its best on smoke; a later option) | unchanged |
| Custom blend functions | residual | unchanged | unchanged |
| Pre-water alpha pool | legacy | unchanged | unchanged |
| Water, reflection probes, mirrors/hero probes, cube snapshots, impostors, HUD | legacy (not eligible) | unchanged | unchanged |
| Selection highlight (`renderHighlights`) | after the resolve; relies on its state restore | same state obligations | same |
| DoF depth | separate traversal at alpha 0.33 | unchanged (cannot share pass D's multisample target) | unchanged |
| Alpha-mask (cutout) | opaque/deferred passes | unchanged | unchanged |

No path is lost; none is gained either, since everything that PPLL leaves on the legacy
path (particles above all) stays there in a first integration. Stochastic transparency's
one structural advantage, no slot limit, matters only where pixels overflow 8 slots.

---

## 7. Memory

PPLL slots: 8 x 8 B + 7 x 4 B meta = 92 B/px (323 MiB at 1440p, format arithmetic matching
the allocation). Stochastic variants: 18-42 B/px (measured multisample targets at 1440p: 116
MiB D32F/D24 S = 8, 58 MiB D16 S = 8 or D32F S = 4, 29 MiB D16 S = 4, 30 MiB half-res D32F
S = 8). `stoch1`: 100 B/px (346 MiB measured). Hybrid: PPLL + the multisample depth,
100-124 B/px. Memory favours stochastic transparency but is not PPLL's problem: the default
512 MB budget covers 8 slots at 1440p; it would not cover 16 slots (156 B/px, 549 MiB).

---

## 8. Vulkan line

On the Vulkan line the OpenGL results are the reference and the renderer is chosen at build
time. Stochastic transparency is portable to Vulkan (sample
mask output and multisample depth sampling are core; `sampledImageDepthSampleCounts` must
include the chosen S, and Zink on the same GPU ran every variant correctly with
`GL_MAX_DEPTH_TEXTURE_SAMPLES` = 8, so S = 16 there is two iterations). But a replacement
would have to be qualified twice, against an OpenGL result that is noisier than PPLL's;
the hybrid would add one multisample depth target and one depth-only pass to the PPLL design
the Vulkan line already plans. `doc/vulkan/component-designs.md` D12 still describes the
linked-list PPLL (16-byte nodes, SSBO allocator) and should be updated to PPLL slots
whatever is decided here.

---

## 9. Plan, for the conditional go (hybrid; PPLL stays)

Stage 0, survey (viewer branch, development-only instrumentation, not for release): extend
`RenderAlphaOITStats` with the alpha draw count per traversal and run the installed-viewer
scenes: avatar crowd with hair, club with layered glass and particles, a build with many
alpha windows, a forest sim, an open sim. **Gate**: pixels past 8 slots >= 5 % of covered
pixels in at least two scenes (excluding particle-only overflow, which PPLL does not
capture), and, re-run with `RenderAlphaOITNodesPerPixel` = 16 (and the memory budget raised
to hold it), pixels past 16 slots still >= 2 %. If overflow is common but stops below 16
layers, the change is more slots (a settings and budget change, bench: 1.04x GPU up to 32
layers), not this hybrid. Otherwise stop.

Stage 1, viewer prototype (feature branch, `RelWithDebInfo`), simple and fullbright alpha
only, main view only, behind a setting off by default: pass D with a new camera-view
alpha-only program (D24, S = 4; D16 only if linear depth is measured), the visibility-
weighted tail in `oit_append`, the hybrid resolve. Offscreen tests in the style of
`test_ppll_capture.py`: bit-identical to PPLL where no pixel overflows (as the bench
checks), stochastic estimate past the slots. **Gates**: identical images below 9 layers;
frame time <= +10 % against PPLL in the Stage 0 crowded scene on NVIDIA, and measured on
Mesa (radeonsi or Zink) and Intel if available; the extra pass's draw count reported.

Stage 2: materials, PBR alpha, rigged (skinned alpha-only program); D pass sorted by
program and texture; the DoF and rigged depth replays moved to alpha-only programs (an
independent saving). **Gate**: CPU frame time in the crowded scene within +10 % of
today's PPLL; overflow RMSE in a captured frame below PPLL's tail.

Stage 3: owner's in-world check (installed viewer, after release, per the usual rule) on
the overflow scenes from Stage 0. Then the Vulkan line follows OpenGL.

No stage replaces PPLL; the setting would switch PPLL's tail, not the method.

---

## 10. Literature

- Enderton, Sintorn, Shirley, Luebke, "Stochastic Transparency", I3D 2010:
  https://www.nvidia.com/docs/IO/88888/StochasticTransparency_I3D2010.pdf (depth-based
  variant with alpha correction, as emulated; 2.6-4.1x their one-pass baseline).
- Laine, Karras, "Stratified Sampling for Stochastic Transparency", EGSR 2011:
  https://users.aalto.fi/~laines9/publications/laine2011egsr_paper.pdf. RMSE (8-bit) motor
  model, stratified vs Enderton: 29.5 vs 36.3 at 4 spp, 17.9 vs 25.3 at 8, 10.3 vs 17.2 at 16;
  the gain nearly vanishes with dozens of layers; "most probably not feasible to implement
  it in shaders".
- Wyman, "Exploring and Expanding the Continuum of OIT Algorithms" (SLAB), HPG 2016:
  https://research.nvidia.com/sites/default/files/pubs/2016-06_Exploring-and-Expanding/hpg16_oitContinuum.pdf.
  SLAB keeps k layers with coverage masks and discards the furthest; with k = S it matches
  S-sample stochastic transparency. Needs `NV_shader_atomic_int64` and
  `NV_fragment_shader_interlock`. Titan X, 1440p, every polygon transparent: SLAB 8.1 / 15.6 /
  48.1 / 89.2 ms, 8-sample stochastic transparency 8.2 / 21.3 / 86.6 / 162.0, hybrid
  transparency 8.7 / 24.6 / 91.8 / 175.9 (pine / car / blacksmith / foliage). Temporal
  filtering removes "much of the noise" (Fig. 9); no no-TAA error figure.
- Maule, Comba, Torchelsen, Bastos, "Hybrid Transparency", I3D 2013:
  https://dl.acm.org/doi/10.1145/2448196.2448212. Exact core of the nearest k fragments plus
  an accumulated tail: the design PPLL slots already follow (with arrival order rather than
  nearest k). **Not read in full** (publisher and author pages refused access); facts here
  are from the abstract and from Wyman 2016.
- Wyman, McGuire, "Hashed Alpha Testing", I3D 2017:
  https://research.nvidia.com/sites/default/files/pubs/2017-02_Hashed-Alpha-Testing/Wyman2017Hashed.pdf.
  Object-space hashed thresholds are stable under motion without TAA ("roughly equivalent"
  to alpha testing), 0.1-0.3 ms at 1080p; stochastic alpha testing is one-sample stochastic
  transparency; it does not address layered blending. Relevant to the shadow-pass spin-off,
  not to replacing PPLL.
- Münstermann, Krumpen, Klein, Peters, "Moment-Based Order-Independent Transparency", i3D
  2018: https://cg.ivd.kit.edu/publications/2018/mboit/Muenstermann2018-MBOIT.pdf. Two
  additive geometry passes, no interlock, noise-free but biased (dark foliage, blurred
  intersections), 80-224 bits/px plus colour; 1.2-2.3 ms over unsorted blending for 4 power
  moments at 16 bits (GTX 1080 Ti, 1080p). The main non-stochastic competitor: same pass
  count as stochastic transparency with `mrt`, no noise, but not exact for few layers either;
  it ranks with the stochastic variants, below PPLL, for this viewer.
- Brüll, Kern, Grosch, "Spatio-Temporal Dithering for Order-Independent Transparency on
  Ray Tracing Hardware", EGSR 2025:
  https://diglib.eg.org/bitstreams/1850ae15-9d38-4522-a389-e6cfe8e990ed/download. Explicitly
  temporal (DLSS/TAA); cites Salvi's 2016 stochastic transparency + TAA as "not satisfactory".
- No published use of stochastic transparency without temporal accumulation, with spatial
  denoising or blue-noise masks, was found.
- Interlock support (for SLAB, multi-layer alpha blending, adaptive transparency): Mesa lists
  `GL_ARB_fragment_shader_interlock` for Zink and iris only, not radeonsi
  (https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/features.txt); AMD's Windows
  OpenGL driver was not verified. This rules out the interlock-based methods as a
  cross-vendor OpenGL replacement.

---

## 11. Not measured

- Per-draw CPU cost and alpha draw counts in a crowded scene (no viewer changes in this
  round; section 5).
- AMD and Intel GPUs (only NVIDIA, directly and through Zink).
- Real shading cost: the bench's fragment shader is trivial. Real per-fragment shading adds
  about the same cost to PPLL's capture, to pass A, to `stoch1`'s pass and to the hybrid's
  capture (each shades every fragment once), so in a shading-bound frame every ratio above
  moves toward 1x, in both directions. This does not change the verdict, which rests on
  image quality (R3) and pass count (R2); it would narrow the hybrid's GPU premium.
- Sloped geometry: the bench's layers are flat, so the per-sample depth equals the pixel
  centre depth; real triangles need a bias in the visibility test, especially at half
  resolution.
- 16-bit linear depth, SLAB, moment-based OIT and a TAA-based route (out of scope or needing
  interlock).
- The S = 16 rows: NVIDIA 621.01 runs S = 16 as two 8-sample iterations (the texture-fetched
  `gl_SampleMask` quirk), Zink reports 8 as its maximum.

The contact sheets in `doc/oit/png/` are tiles of the per-variant PNGs the bench writes with:

```
OIT_BENCH_IMPLS=slots,<variant> OIT_BENCH_W=2560 OIT_BENCH_H=1440 \
OIT_BENCH_PNG_DIR=<dir> OIT_BENCH_PNG_SCENE='bright,8;full,8;foliage,32;occl,8;full,32' \
python scripts/perf/bench_alpha_oit.py
```

with `OIT_BENCH_SCENES` as in section 3 and, for Zink,
`GALLIUM_DRIVER=zink ... --opengl <Mesa opengl32.dll>`.

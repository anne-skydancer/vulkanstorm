# Renderer direction (decisions of 2026-10-29)

Owner decisions of 2026-10-29 on OpenGL, Mesa/Zink and Vulkan. Where they
conflict with the design study in this folder (see [README.md](README.md)),
these decisions apply.

## Mesa/Zink is withdrawn

With NVIDIA GPUs, Mesa/Zink lets the desktop show through the viewer: the UI
looks broken and the 3D world renders wrong. The same build renders correctly
with AMD GPUs. Chasing why Mesa behaves differently on NVIDIA is not worth
the effort, so Zink is withdrawn and OpenGL is the only renderer.

- **What was changed:** Preferences > Graphics > Hardware Settings loses its
  "Renderer" label and combo, and they are commented out, not deleted. The
  panel's selector code is commented out with them.
- **Saved Zink settings:** a saved `RenderBackend` of Zink, in any letter
  case, is reset to OpenGL at startup, before `selectGLBackend()`.
- **What stays in the tree:** the Zink code, the Mesa package and its
  staging, unused.
- **Archive:** `vkstorm-release` before the change is tagged
  `archive/zink/2026-10-29`.
- **Lines:** release (#238, #242), devel (#239), canary (#240), aleph
  (#241). The Rust line gets it with its forward-port.
- **The selector:** it stays commented out for now.

## The Vulkan lines are archived

All Vulkan work so far is archived. This includes the DiligentCore renderer,
which never qualified. The branches were renamed, so their history is kept:

| Archive | Was | Tip |
|---|---|---|
| `vkstorm-vulkan-rhi-archive` | DiligentCore line, with its code survey (#237) | `123615c38b` |
| `vkstorm-vulkan-direct-archive` | raw Vulkan line | `b9918ba552` |
| `vkstorm-vulkan-base-archive` | shared base of the two | `6a93034fae` |
| `vkstorm-vulkan-archive` | the line before the Firestorm 7.2.5 rebuild | unchanged |

## Order of work

1. **Finish the Rust port** (`vkstorm-rust`).
2. **Then** create a new `vkstorm-vulkan` from the tip of `vkstorm-release`.
3. **On it, write a new renderer.**

## How the new renderer is approached

- **No wholesale modernization.** That means no unproven or experimental
  rendering techniques. Proven techniques are in, such as those Aurora
  Viewer uses: GPU-driven rendering with GPU-resident material tables,
  multi-draw indirect and Hi-Z occlusion culling, and clustered lights.
- **Learn from Aurora Viewer's renderer.** Study how it works, especially
  for performance and image quality, and apply that to Vulkanstorm's own
  renderer, following Aurora's path.
- **Design only, no code.** Aurora Viewer is GPL-3. It is used as a design
  reference only: no code is copied. Vulkanstorm stays LGPL-2.1.
- **Graphics API: not decided.** The candidates are wgpu, DiligentCore or
  something else, and the owner leans towards wgpu. This supersedes the
  study's preference for DiligentCore.

## OpenGL or Vulkan is chosen when the viewer is built

Vulkan remains selectable, but the choice is made when the viewer is built,
not while it runs.

- **A build option:** a new Autobuild/CMake option compiles either an OpenGL
  viewer or a Vulkan viewer. One binary never contains both renderers.
- **Proposed form:** `-DUSE_OPENGL:BOOL=TRUE|FALSE` with
  `-DUSE_VULKAN:BOOL=FALSE|TRUE`, where exactly one of the two is on.
  Configuration fails if both or neither are on. A single option with two
  values is an alternative still open.
- **Why:** simplicity. There is no runtime switch between renderers inside
  one viewer.
- **What it supersedes in the study:** keeping GL/Zink "separately
  selectable" next to the new renderer, and restart-based switching between
  backends.

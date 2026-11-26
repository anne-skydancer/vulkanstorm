<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# The C++ cleanup on release-clean

`release-clean` was cut from the tip of `vkstorm-release` (537be74479) on 2026-11-25. It is a work branch: the base of
the clean C++ code that the later Rust rewrite starts from. Pushes to it build and publish nothing; pull requests into it
get the full CI.
No `<Vulkanstorm>` markers and no upstream merges. `release-clean` is the clean base of the Rust port, not a product line:
cleanup commits do not carry per-line `<Vulkanstorm>` markers (the commit messages say what changed and why), and the branch
takes no merges from upstream Firestorm or Linden Lab. Linden Lab and Firestorm licence headers stay unchanged. Markers
that earlier hotfixes left in the code are kept as they are.

## One branch per subsystem, one at a time

Each subsystem gets its own branch `clean/<subsystem>`, cut from the tip of `release-clean`, and lands in `release-clean`
through its pull request before the next branch is cut. The subsystems depend on one another in one direction (everything
uses `llcommon`, most use `llmath`, `newview` uses all), so a later branch always starts from cleaned foundations and no
two cleanup branches change the same interfaces at once.

## Order (bottom-up)

1. Foundations: `llcommon`, then `llmath`.
2. Data and I/O: `llfilesystem`, `llimage` with the J2C back ends (`llimagej2coj`, `llimagej2cneo`), `llxml`,
   `llinventory`, `llprimitive`.
3. Network: `llcorehttp`, then `llmessage`.
4. Platform and rendering: `llwindow`, then `llrender`.
5. Characters and audio: `llcharacter`, `llappearance`, `llaudio`.
6. The UI toolkit: `llui`.
7. `newview` in slices, one branch per area (`clean/newview-<area>`): the render pipeline and draw pools; objects, prims,
   mesh and physics; avatars and animation; textures and images; light, shadows, sky and water; inventory and assets;
   chat, IM, voice and groups; agent, world, regions, network and login; the app shell (startup, settings, menus);
   floaters and panels; audio, media and web; RLV/RLVa; the Firestorm and contributor features.
8. The rest: `llplugin` and `media_plugins`, the crash loggers, `viewer_components`, `llwebrtc`, `llmeshoptimizer`,
   `llphysicsextensionsos`, `llkdu`, the test framework.

## Inventory (`vkstorm-devel` tip, 2026-11-25)

1,604 `.cpp` files, 178 of them tests: `newview` 922, `llcommon` 148, `llui` 112, `llmessage` 85, `llmath` 51, `test` 31,
`llcorehttp` 29, `llrender` 28, `media_plugins` 22, `llinventory` 22, `llcharacter` 21, `llprimitive` 20, `llwindow` 19,
`llappearance` 16, `llimage` 14, `llaudio` 12, `llfilesystem` 10, `llplugin` 8, the rest 32.

# Vulkanstorm tools conventions

- `vkstorm-tools` holds standalone tools (Skin Studio, `tools/skin-editor`), no viewer code. Feature work goes on `codex/` branches made from `vkstorm-tools` and returns through a PR. Merges flow one way: never merge a base line (`vkstorm-*`) into a feature or PR branch, including GitHub's "Update branch".
- Licence: dual (`NOTICE.md`). Skin Studio code is `LGPL-2.1-or-later OR GPL-3.0-or-later`; the skin library copied from the viewer is `LGPL-2.1-only OR GPL-3.0-or-later`; artwork and third-party components keep their own terms. GPL-only code on this line: none; keep it that way (Aurora code excluded). New source files carry the dual SPDX header; a new dependency must be compatible with both LGPL-2.1 and GPL-3.0.

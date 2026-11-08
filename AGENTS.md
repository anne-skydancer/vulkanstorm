# Vulkanstorm tools conventions

- `vkstorm-tools` holds standalone tools (Skin Studio, `tools/skin-editor`), no viewer code. Feature work goes on `codex/` branches made from `vkstorm-tools` and returns through a PR. Merges flow one way: never merge a base line (`vkstorm-*`) into a feature or PR branch, including GitHub's "Update branch".
- Licence: `GPL-3.0-or-later` (`NOTICE.md`; decided 2026-11-08, the LGPL-2.1 option is dropped). Skin Studio code carries `SPDX-License-Identifier: GPL-3.0-or-later`; the skin library copied from the viewer keeps Linden Lab and Firestorm headers (LGPL-2.1, used under the GPL); artwork and third-party components keep their own terms. A new dependency must be compatible with GPL-3.0.

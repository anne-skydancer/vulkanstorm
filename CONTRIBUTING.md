# Contributing to the Vulkanstorm tools

## Licence of contributions

The tools are licensed under GPL-3.0-or-later (see [NOTICE.md](NOTICE.md)).
By contributing you agree that your contribution is licensed under
`GPL-3.0-or-later`. Changes to the skin library copied from the viewer
(`tools/skin-editor/library/`) keep Linden Lab's and Firestorm's headers. Start
new source files with

```
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) <year>, <your name> (Vulkanstorm)
```

in the file's comment syntax. Artwork keeps its authors' terms and must come
with a licence that allows redistribution; add its notice under
`tools/skin-editor/licenses/`. A new dependency must be compatible with
GPL-3.0 (no proprietary, GPL-2.0-only or extra-restriction licences).

## Workflow

Changes reach `vkstorm-tools` through a pull request from a `codex/` branch
made from `vkstorm-tools`. Merges flow one way: never merge a base line
(`vkstorm-*`) into a feature branch. Run the tests listed in the
[README](README.md#tests) before opening the pull request.

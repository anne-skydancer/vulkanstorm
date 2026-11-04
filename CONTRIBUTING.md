# Contributing to the Vulkanstorm tools

## Licence of contributions

The tools are dual-licensed under LGPL-2.1 and GPL-3.0-or-later (see
[NOTICE.md](NOTICE.md)). By contributing you agree that your contribution is
licensed:

- under `LGPL-2.1-or-later OR GPL-3.0-or-later` when it is Skin Studio code
  (anything outside `tools/skin-editor/library/`). Start new source files with

  ```
  SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
  Copyright (C) <year>, <your name> (Vulkanstorm)
  ```

  in the file's comment syntax;
- under `LGPL-2.1-only OR GPL-3.0-or-later` when it changes the skin library
  copied from the viewer (`tools/skin-editor/library/`).

Do not contribute code that is available only under the GPL, such as code
from the Aurora Viewer (GPL-3.0-or-later only): it would remove the LGPL
option. Artwork keeps its authors' terms and must come with a licence that
allows redistribution; add its notice under `tools/skin-editor/licenses/`.
A new dependency must be compatible with both LGPL-2.1 and GPL-3.0.

## Workflow

Changes reach `vkstorm-tools` through a pull request from a `codex/` branch
made from `vkstorm-tools`. Merges flow one way: never merge a base line
(`vkstorm-*`) into a feature branch. Run the tests listed in the
[README](README.md#tests) before opening the pull request.

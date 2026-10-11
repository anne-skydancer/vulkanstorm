# Vulkan branch cleanup audit

Verified 11 October 2026. Scope is earlier Vulkan development branches; release, canary and unrelated feature branches are excluded. No branch or worktree was deleted.

The three post-reset branch tips below are ancestors of `vkstorm-vulkan`, whose integrated history is published to `origin`. Both earlier Vulkan worktrees (`vulkan-first-deliverable` and its nested `vulkan-progress-catalog`) have clean tracked and untracked status. Their detached checkout commits are also covered by the integrated history.

| Branch | Tip | Disposition |
| --- | --- | --- |
| `codex/vulkan-first-deliverable` | `1984d02d6d5417875eeda6241371b938a1cb8b1e` | Merged into `vkstorm-vulkan` |
| `codex/vulkan-pipeline-audit-design` | `00ff74c6702d5ac07acd6e5167c5a5966d2c71dd` | Merged into `vkstorm-vulkan` |
| `codex/vulkan-restore-progress-panels` | `70622d8dd1c461369dbbd74c48a8821bb18a8a00` | Merged into `vkstorm-vulkan` |

The twelve retired pre-reset branches are **preserved, not merged** into the current renderer. Their exact tips are present in the existing October 2 bundle, whose SHA-256 was reverified, and in the remote archive tags listed below. Reintroducing their legacy master-based implementation would contradict the clean reset and the current native Diligent design. Archive retention must not be confused with current implementation acceptance. Deleting these old branch names retains their history through the archive tags and bundle.

| Retired branch | Remote archive tag |
| --- | --- |
| `agents/vulkan-ui-quality-audit-corrections` | `archive/vulkan-pre-reset/2026-10-02/agents/vulkan-ui-quality-audit-corrections` |
| `agents/vulkanstorm-codebase-survey` | `archive/vulkan-pre-reset/2026-10-02/agents/vulkanstorm-codebase-survey` |
| `codex/ci-shader-tools` | `archive/vulkan-pre-reset/2026-10-02/codex/ci-shader-tools` |
| `codex/native-browser-naming` | `archive/vulkan-pre-reset/2026-10-02/codex/native-browser-naming` |
| `codex/vulkan-device-name` | `archive/vulkan-pre-reset/2026-10-02/codex/vulkan-device-name` |
| `codex/vulkan-preferences-textbox` | `archive/vulkan-pre-reset/2026-10-02/codex/vulkan-preferences-textbox` |
| `codex/vulkan-sdk-metadata` | `archive/vulkan-pre-reset/2026-10-02/codex/vulkan-sdk-metadata` |
| `codex/vulkan-sdk-version-file` | `archive/vulkan-pre-reset/2026-10-02/codex/vulkan-sdk-version-file` |
| `codex/vulkan-stats-types` | `archive/vulkan-pre-reset/2026-10-02/codex/vulkan-stats-types` |
| `native-session-owner` | `archive/vulkan-pre-reset/2026-10-02/native-session-owner` |
| `native-sl-login` | `archive/vulkan-pre-reset/2026-10-02/native-sl-login` |
| `vulkanui` | `archive/vulkan-pre-reset/2026-10-02/vulkanui` |

Archive: `H:\vulkanstorm\archive-2026-10-02-clean-base\vulkanstorm-all-refs.bundle`

Verified SHA-256: `252b627b5a27413287316dd984b493b93fe8a3851f869fcbf1e9ad7f28a3b0e1`

Keep the active `vkstorm-vulkan` checkout and its canonical `build-vc170-64` RelWithDebInfo runtime stage. Preserve any executable still being tested until that test finishes. No unique source work remains in the two earlier post-reset Vulkan worktrees.

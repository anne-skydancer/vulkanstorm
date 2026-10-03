# Archived viewer defaults audit

Audited 2026-10-03 against archived `vkstorm-devel` commit
`7c2c201134905184971e82fb313da462fc880f77` in
`H:\vulkanstorm\archive-2026-10-02-clean-base\git-metadata`.
The reset branches were compared independently: release `4eba3a13ff`,
devel `f9955139a5`, and canary `9772a5b8f8`.

## Omissions corrected by this follow-up

- Phoenix, Text, and V3 mode overrides: letter keys move the avatar (`1`),
  matching the archive rather than opening chat (`0`). Hybrid already matched.
- `cloud.xml`: start and end fallback colors restored to archived teal,
  matching the existing global FSCloudColorStart/End defaults.
- Windows and Linux hardware feature tables: multithreaded texture processing
  enabled in the base rule, and core profile enabled in the Intel rule.
  Both complete tables now match the archive byte for byte. The macOS table
  already matched. These are hardware policy defaults, not just preferences.
- Alternate AnsaStorm skin: archived light-red value and alert/error aliases;
  Classic Brown age/distance aliases. Canary already had these values.

## Explicit owner defaults

The owner additionally requested local chat logging and auto-close OOC on.
`LogNearbyChat` and `AutoCloseOOC` were **off in the archive too**; this change
sets both to `1` as a new intentional choice.

| Control | Final default | Archive default |
| --- | --- | --- |
| RLVa (`RestrainedLove`) | on | on |
| Privacy: local chat logs (`LogNearbyChat`) | on | off |
| Nearby, IM, transcript timestamps | on | on |
| Seconds in chat timestamps | on | on |
| Log timestamp (`LogTimestamp`) | on | on |
| Auto-close OOC (`AutoCloseOOC`) | on | off |

All settings and mode catalogs were searched for overrides of these controls;
none were found. Existing saved user/account preferences take precedence over
factory defaults. RLVa requires a viewer restart when changed.

## Coverage and remaining intentional differences

The audit parsed every archived LLSD settings catalog and compared every
shared `Value`, rather than matching file text. It also compared tracked
app-settings assets, camera/environment presets, GPU/feature tables, skin
settings, palettes and the skin registry. Shader implementations, XUI layouts
and image assets are outside a factory-default-value equivalence claim.

Before these corrections, release/devel had 2,389 shared main settings,
2,385 matching; canary had 2,418 shared main settings, 2,415 matching.
All differences in those shared main values were skin/theme selection,
the stock grid-feed URL, and available languages. The account catalog had
136 shared values on release/devel and 137 on canary, all matching.
Crash behavior (4), Hybrid (36), skin-specific settings (1 each) all matched.
Phoenix (47), Text (62), and V3 (27) differed only in the corrected movement
override. Canary's Modern catalog (19) already matched completely.

Remaining deliberate differences:

- AnsaStorm Modern is the selected skin, with the approved Kokua palette and
  branded login/icons. Registry entries match the archive; Modern is listed
  first. Reverting these would undo the owner's newer choices.
- The stock grid list uses HTTPS, with narrowly scoped certificate checks.
  Downloads remain enabled; custom grid URLs retain their existing behavior.
- Release/devel omit Portuguese from the enabled-language list because they
  do not ship that translation. Canary retains its newer language support.
- Release/devel have 36 archived main settings and `FSInventoryCustomTabs`
  absent, with no remaining source references. Their Modern mode catalog is
  also absent. Canary lacks only seven removed native-renderer settings:
  `RenderAlphaOITProfile`, `RenderInterleavedAlpha`,
  `RenderAlphaDepthPeelLayers`, `RenderAlphaDepthPeelTimeBudgetMS`,
  `RenderAlphaDepthPeelAvailable`, `RenderVulkanDebug`, `RenderVulkanSelfTest`.
  Adding orphan entries would not restore removed functionality. Other absent
  release/devel settings belong to newer upstream features available in
  canary; their shared canary values match the archive.
- Current source-only settings (`FSMeshUploadUseGLODAsDefault`, and on
  release/devel `TextureBiasUnimportantFactor`) retain their current defaults.
- Non-default skin palettes on release/devel lack newer upstream-only color
  keys. Shared values match after the alternate-skin correction; the selected
  Modern palette keeps the approved Kokua values. Canary retains its existing
  newer color keys. This does not claim UI equivalence to removed features.

## Qualification limits

Archive/value comparisons and XML parsing establish shipped defaults.
The Windows development build must stage the changed catalogs, cloud asset
and hardware table alongside its runtime dependencies. These checks do not
establish runtime behavior on every GPU, especially the Intel profile rule,
and do not substitute for Linux/macOS CI or first-login UI testing.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# In-world light check over LEAP

Dev tooling, nothing in the viewer. `light_check.py` is a LEAP plugin: the viewer starts it with `--leap`, you log
in by hand, and it drives the logged-in viewer. For each sky of a scenario it takes a noon snapshot and then sweeps
the sun through the horizon (+10 to -10 degrees, 0.5 degree steps, 0.1 degree steps within 1.5 degrees of the
horizon: 65 steps) with a snapshot per step. `analyze_light_check.py` then reports luminance per region and step,
flags pops and writes a contact sheet per sky. The first scenario is the in-world check of PR #447 (L3 + AT1);
the same scenario is meant for the windows-latest 82080 baseline and for the next light stages.

Needs Python 3.9+ (written for 3.13, tests pass on 3.15). Pillow converts the snapshots to PNG and numpy/Pillow run
the analysis; both are installed on this machine (Pillow 13.0.0, numpy 2.5.4). Without Pillow the run still works but
leaves BMP files (see below), and the analysis cannot run. Nothing is installed by the scripts.

## Launch (PowerShell)

Close the running viewer first (the script only starts with a new viewer: LEAP is read once at startup). Quote
rules of `--leap`: the command is split on spaces, `'` and `"` group, and **a backslash is an escape character**, so
write every path with forward slashes and put each path in single quotes.

```powershell
$exe     = 'C:\Users\annes\source\repos\vulkanstorm\work\l3at1\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe'
$scripts = 'C:/Users/annes/source/repos/vulkanstorm/work/leap-check/scripts/inworld'   # after the merge: the main checkout's scripts/inworld
$py      = (Get-Command python).Source -replace '\\','/'
$out     = 'C:/Users/annes/source/repos/vulkanstorm/.captures/l3at1-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$leap    = "'$py' '$scripts/light_check.py' --scenario '$scripts/scenarios/l3_at1_handover.json' --out '$out'"
Set-Location (Split-Path $exe)
& $exe --leap $leap
```

Log in by hand. The script waits for `STATE_STARTED` (it logs the state changes), settles 20 s, waits for the
inventory to load, and starts. Stay logged in, in an open spot with sky, ground and clouds in view (the camera is
placed at your avatar, 2.5 m up, looking along the sun's azimuth). Do not touch the viewer during the run
(six skies are about 400 snapshots; the time depends on your snapshot speed, expect 10 to 20 minutes). When it ends it
restores your settings and environment. To stop early: create an empty file `STOP` in the run folder (it restores
and exits).

Check first with the **dry run** (changes nothing; open floaters are closed again):

```powershell
$leap = "'$py' '$scripts/light_check.py' --dry-run --out '$out'"
& $exe --leap $leap
```

It checks that the viewer has every LEAP operation the script uses, that the debug settings exist, that the
Phototools and Personal Lighting floaters open and contain the widgets used (and the text editors inside the
sliders), that the inventory yields the scenario's skies and that the sky combo box accepts them. Read the
`LIGHTCHECK PASS/FAIL` lines in the viewer log (stderr of the plugin) or `dry_run.json`. `--dry-run --no-login-wait`
checks the API and settings part at the login screen.

Other modes: `--list-skies` (prints and saves every settings item of the inventory with its asset id, to pick sky names
for a scenario), `--restore RUNDIR` (re-applies `RUNDIR/restore.json` in a fresh viewer, if a run was cut short),
`--only pbr_default,classic_default`, `--step-limit 6` (quick test), `--set KEY=VALUE` (a debug setting for this
run only, restored afterwards, repeatable; e.g. `--set RenderAtmospherePhysicalLight=false` for the A/B run of #447).

Analysis (offline, any time):

```powershell
python "$scripts\analyze_light_check.py" C:\Users\annes\source\repos\vulkanstorm\.captures\l3at1-<date-time>
# compare the noon rows of two runs, e.g. the 82080 baseline (A) against #447 (B):
python "$scripts\analyze_light_check.py" <run B> --compare <run A>
```

## What a run does

1. Waits for login, records your original state (sky/water/day cycle combo values of Phototools, floaters, the
   settings it will change), writes `restore.json`.
2. Sets `FSEnvironmentManualTransitionTime` to 0 (instant sky changes) and the scenario's `knobs`/`settings`
   (`null` leaves a knob alone and only records it). A knob that does not exist in the viewer (the 82080 baseline has
   none of the #447 Sky-tab settings) is skipped with a warning, so the same scenario runs on both.
3. Pauses the clouds (Phototools "Pause Clouds"), so the cloud region does not change by drift between steps.
4. For each sky: selects it, opens Personal Lighting (which captures it as the live sky), applies the sky's `controls`
   (sliders by name, e.g. `probe_ambiance` 1 makes it a PBR sky, 0 classic) and moon placement, reads the sun
   azimuth, points the camera along it, takes the noon snapshot (sun at 66 degrees) and the sweep.
5. Restores the camera, the sky (the Phototools sky combo value it found, else "Revert to Region Default"), closes the
   floaters it opened, un-pauses the clouds and puts every changed setting back. Also on an error, a stop file, or
   Python exiting normally; not possible if the viewer itself has gone (then `restore.json` has the old values).

How things are done (LEAP has no environment API, and its UI API cannot set a slider):

| Need | Route |
|---|---|
| Select a sky | inventory lookup by name (`LLInventory.collectDescendantsIf`, type `settings`, my inventory and the library root) gives the asset id; `UI.setSelectedByValue` moves the Phototools `WLPresetsCombo` highlight (it does not apply), then a click on `WLNextPreset` and `WLPrevPreset` (`LLWindow.mouseDown/mouseUp`) ends on the same sky and applies it. Fallback: the Phototools quick-environment button named in the scenario (`Noon`, `Sunset`) |
| PBR or classic | Personal Lighting `probe_ambiance` (reflection probe ambiance 1 = PBR sky, 0 = classic; `doc/light/light-reference-numbers.md` explains why no shipped sky is PBR) |
| Sun elevation, moon position | Personal Lighting `sun_elevation`, `moon_azimuth`, `moon_elevation`: the slider's text field is focused, emptied with Backspace, filled with `LLWindow.pasteText` and committed with Enter (`LLWindow.keyDown path=...`); the value is read back with `UI.getValue` and must match to 0.006 degrees, else the step fails |
| Sky without a moon | the fixed sky editor (`env_fixed_environmentent_sky`, key `new`) has `moon_brightness`; Personal Lighting has not. The sky `no_moon` therefore edits a new sky (Default values) there. Optional and the least certain part |
| Fixed camera | `LLAgent.setCameraParams` (the follow cam, `camera_locked` and `focus_locked`, no lag): position = your avatar +2.5 m, focus 200 m away along the sun's azimuth, pitched up 10 degrees so the horizon sits at about 65 % of the frame height (sky, clouds and some ground in view). `setFollowCamActive false` and `removeCameraParams` at the end |
| Snapshot | `LLViewerWindow.saveSnapshot` (no UI, no HUD, window size unless the scenario gives a size). The viewer's LEAP operation passes no format, so the file is **BMP data whatever its name**: the script saves `.bmp` and converts to PNG with Pillow, deleting the BMP |
| FPS | `LLStats.getPerfData` (mean of the last 30 frames of `frametime.fps`), in the manifest per step; the wait per step is the larger of `per_step_s` and `min_frames` / fps |
| Login | `listen` on the `StartupState` pump, `LLStartUp.postStartupState` for the current state, wait for the last entry of `getStateTable` |

No `UI.call` is used: a wrong function name there makes the viewer abort (`LL_ERRS`); every click is a mouse event on a
widget path found at run time with `LLWindow.getPaths`.

## Skies of the #447 scenario (`scenarios/l3_at1_handover.json`)

| id | Sky (inventory name) | Setup | Why |
|---|---|---|---|
| `pbr_default` | Default | probe ambiance 1 | the viewer's default sky as a PBR sky: the reference of `light-reference-numbers.md` (row 2), sun 66 degrees; owner check 1 |
| `pbr_sunset_crescent_high` | Sunset | probe 1, moon 55 degrees up, 90 degrees from the sun | owner checks 1 and 3: sunset with the moon high |
| `pbr_sunset_moon_opposite` | [TOR] SUNSET - Bangko 1 | probe 1, moon 12 degrees up, opposite the sun | owner check 1: moon rising opposite the sun; Bangko 1 is the test's reddest low-sun sky |
| `classic_default` | Default | probe ambiance 0 | owner check 2: classic sky, noon must match windows-latest, reddening and haze |
| `yellow_stars_01` | Phototools- Yellow Stars 01 | as is, optional | owner check: negative distance multiplier, the sky where the cloud sun changes most (1.22); skipped if not in the inventory |
| `no_moon` | new sky in the fixed editor | `moon_brightness` 0, optional | owner check 4: no direct light at night |

These names are from `llatmosphericlight_test.cpp` (shipped Windlight skies) and the viewer's quick environments; I could
not look into your inventory. If a name is not found the script uses the sky's `fallback_button` and warns in `run.json`
(`warnings`); pick better names with `--list-skies` and edit the scenario. Each sky entry takes `names`,
`fallback_button`, `controls` (any Personal Lighting slider by its XUI name), `azimuth_deg`, `moon`
(`azimuth_offset_deg` from the sun, `elevation_deg`), `editor` (`personal`/`fixed`), `noon`/`sweep` (booleans),
`optional`.

## Output (run folder, git-ignored under `.captures/`)

- `snapshots/<sky>_<step>_<e+05.00>.png` (step 0 is the noon snapshot, `e+66.00`)
- `manifest.csv`: sky, step, kind (`noon`/`sweep`), requested and read-back sun elevation, sun azimuth, snapshot path,
  UTC timestamp, fps, frame ms, the settings in force. Written row by row, so an aborted run keeps its rows.
- `run.json`: scenario, knob values, field of view, per-sky status/selected sky/signature (sun azimuth, ambiance, cloud
  coverage, haze, gamma as read from Personal Lighting, to see that the right sky took), camera, regions, warnings,
  your original environment. `scenario.json`, `restore.json`, `dry_run.json` as applicable.
- `viewer_log_excerpt.txt`: the viewer log from the start of the script to the end, best effort (newest `*.log` under
  `%APPDATA%\*\logs` or `%LOCALAPPDATA%\*\logs` touched in the last 10 minutes, or `--viewer-log PATH`). The script's own
  lines (`LIGHTCHECK ...`) are in the viewer log anyway (the viewer logs the plugin's stderr).
- After the analysis: `report.txt`, `analysis.json`, `analysis_<sky>.csv`, `contact_<sky>.png`.

Analysis regions are fractions of the frame computed from the camera pitch and `CameraAngle` (`run.json` > `regions`):
ground below the horizon, the sky band just above it, a cloud region high in the frame, and the whole frame. A change
between two steps is a **pop** when it is above `--pop-floor` (0.01 luma) and its rate per degree is more than
`--pop-factor` (4) times the median rate of the neighbouring steps (the step size changes from 0.5 to 0.1 degrees, so rates,
not raw differences, are compared). The mean absolute difference of a 64-column luma thumbnail between steps is the same
test for the whole frame. A shadow that fades quickly near the horizon is real; look at the contact sheet of anything flagged.

## For the baseline and the next stages

Run the same scenario in the windows-latest 82080 viewer (the knobs of #447 are skipped there); compare noon with
`--compare`. For a new stage copy the scenario, change `knobs` / `settings` / `skies`; `--set` covers one-off A/B runs. The
sweep, camera, settle times and sky list are all scenario fields (see `DEFAULTS` in `light_check.py`).

## Tests

```powershell
python -m unittest discover -s scripts/inworld/tests -v
```

Covers the LLSD notation/binary codecs and framing, the session (requests, errors, timeouts, events), the sweep
geometry, the whole run and the dry run against a fake viewer (`tests/fake_viewer.py`) that models the operations as
the viewer's source defines them, restoration on success, a stop file and a failing sky, missing knobs, and the analysis
(synthetic images with an injected pop; no false pop at the 0.5/0.1 degree step change).

**Not tested against the real viewer** (the fake only reproduces what the source says): the real widget paths and
class names, whether `Backspace`/`Enter` key names and the focus handling edit the slider text field as intended,
the combo Next/Prev trick, `setCameraParams` with locked camera and focus, the fixed-editor `no_moon` path and its tab
click, the Phototools sky names, `STATE_STARTED` handling, `LLStats` field names, the log excerpt lookup, how long a
snapshot takes, and the quoting of the `--leap` string. The dry run is the quick check of most of these.

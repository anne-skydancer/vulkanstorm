<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# In-world light check over LEAP

Dev tooling, nothing in the viewer. `light_check.py` is a LEAP plugin: the viewer starts it with `--leap`, you log
in by hand, and it drives the logged-in viewer. For each sky of a scenario it takes a noon snapshot and then sweeps
the sun through the horizon (+10 to -10 degrees, 0.5 degree steps, 0.1 degree steps within 1.5 degrees of the
horizon: 65 steps) with a snapshot per step. `analyze_light_check.py` then reports luminance per region and step,
flags pops and writes a contact sheet per sky. The first scenario is the in-world check of PR #447 (L3 + AT1);
the same scenario is meant for the windows-latest 82080 baseline and for the next light stages.

Needs Python 3.9+ (written for 3.13; the tests pass on 3.15). Pillow converts the snapshots to PNG and numpy/Pillow
run the analysis. Without Pillow a run still works but leaves BMP files (see below) and the analysis cannot run.
The scripts install nothing.

## Launch (PowerShell)

Close the running viewer first: LEAP is read once at startup.

**Quoting rules.** On Windows the viewer tokenises its raw command line itself (`LLCommandLineParser::parseCommandLineString`,
`boost::escaped_list_separator`: the quote characters `"` and `'` toggle and are dropped, nothing can pass a quote through),
so `--leap` cannot carry a quoted LLSD notation string: the value arrives without its quotes and is split at spaces.
What works is giving **`--leap` twice**: with more than one value the setting `LeapCommand` becomes an array of plain
strings (no notation parsing), and each string is then split on spaces by `LLLeap::create`. The second entry is a
throw-away plugin (`light_check.py --noop`: it takes the handshake and exits 0). Consequences: **paths must contain no
spaces** (quotes do not protect them) and are written with forward slashes. Use the real Python interpreter, not the
Microsoft Store alias that `Get-Command python` may return (`py -c "import sys;print(sys.executable)"` prints the right one).
This was derived from the source and simulated in `tests/viewer_cmdline.py` (a port of the two tokenisers and of the
array branch); it has **not** been run in a viewer, hence step 1 below.

```powershell
$exe     = 'C:\Users\annes\source\repos\vulkanstorm\work\l3at1\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe'
$py      = 'C:/Users/annes/AppData/Local/Python/pythoncore-3.15-64/python.exe'      # py -c "import sys;print(sys.executable)"
$scripts = 'C:/Users/annes/source/repos/vulkanstorm/work/leap-check/scripts/inworld'  # after the merge: the main checkout's scripts/inworld
$stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$noop    = "$py $scripts/light_check.py --noop"
Set-Location (Split-Path $exe)
```

**Step 1, at the login screen (do not log in).** Confirms the launch line and the API side, changes nothing:

```powershell
$dry0 = "$py $scripts/light_check.py --dry-run --no-login-wait --out C:/Users/annes/source/repos/vulkanstorm/.captures/dry0-$stamp"
& $exe --leap $dry0 --leap $noop
```

In the viewer log look for two `processing --leap "..."` lines (one per entry, each the whole command) and the
`LIGHTCHECK PASS/WARN/FAIL` lines, or read `dry_run.json` in that folder. If there is only one `processing --leap` line, or
the command is broken at a space, the line did not reach the viewer intact: stop. The `--noop` entry ends at once with exit
code 0; if the viewer logs a launch warning for it, that is noise.

**Step 2, in-world dry run** (log in, wait for it; a new folder per launch). Read-only: it checks that the viewer has every
LEAP operation the script uses, that the debug settings exist, that Phototools opens with the widgets used (present and
available), that the original environment can be restored, and that the inventory yields the scenario's skies (a required
sky that would need the fallback button FAILS; an optional one warns). It does **not** open Personal Lighting or the fixed
sky editor: opening Personal Lighting freezes the current sky into the local environment (`captureCurrentEnvironment`),
which closing does not undo. Those are checked only when Personal Lighting is open already; `--deep` opens them and
restores the environment afterwards as a run does.

```powershell
$dry = "$py $scripts/light_check.py --dry-run --out C:/Users/annes/source/repos/vulkanstorm/.captures/dry-$stamp"   # add --deep for Personal Lighting and the fixed editor
& $exe --leap $dry --leap $noop
```

**Step 3, the run** (a new folder per launch):

```powershell
$run = "$py $scripts/light_check.py --scenario $scripts/scenarios/l3_at1_handover.json --out C:/Users/annes/source/repos/vulkanstorm/.captures/l3at1-$stamp"
& $exe --leap $run --leap $noop
```

Log in by hand. The script waits for `STATE_STARTED` (it logs the state changes), settles 20 s, waits for your
inventory to load, and starts. Stay logged in, in an open spot with sky, ground and clouds in view (the camera is
placed at your avatar, 2.5 m up, looking along the sun's azimuth). Do not use the viewer during the run: six skies
are about 400 snapshots, expect 10 to 20 minutes. At the end it restores your settings and environment. To stop
early create an empty file `STOP` in the run folder (it restores and exits). A relative `--out` is resolved to an
absolute path (the viewer's working directory is not yours); a stale `STOP` in a reused folder is deleted at the start.
Keep Personal Lighting and the fixed sky editor closed: with one open the run refuses to start (changing nothing), because an
edit in progress has no saved asset to put back. A locally edited sky left in place is replaced by the shared environment at the end.

Other options: `--list-skies` (prints and saves every settings item of the inventory with its asset id, to pick sky
names), `--restore RUNDIR` (re-applies the settings and releases the camera recorded in `RUNDIR/restore.json` in a
fresh viewer, if a run was cut short; it does not touch the environment and creates no folder), `--only
pbr_default,classic_default`, `--step-limit 6` (quick test), `--set KEY=VALUE` (a debug setting for this run only,
restored afterwards, repeatable; e.g. `--set RenderAtmospherePhysicalLight=false` for the A/B run of #447),
`--sky-filter TEXT` (with `--list-skies`: only names containing TEXT), `--noop` (the throw-away second plugin), `--max-wait-s N` (how long to wait for login; default the scenario's `login_timeout_s`, 3600), `--viewer-log PATH`
(the viewer log for the excerpt; default: the newest `*.log` under `%APPDATA%\*\logs` or `%LOCALAPPDATA%\*\logs`
touched in the last 10 minutes), `--scenario FILE`.

Exit codes: 0 ok, 1 error (a refused start included, see "What a run does") or dry-run failure, 2 stopped by `STOP`, 3 the pipe from the viewer reached end of file (the viewer exited or closed it), 5 protocol error (the bytes from the viewer cannot be parsed: the connection is lost but the viewer may still be running), 4 a required sky failed
(a final `LIGHTCHECK FAILED SKIES ...` line names them).

Analysis (offline, any time):

```powershell
python "$scripts/analyze_light_check.py" C:/Users/annes/source/repos/vulkanstorm/.captures/l3at1-<stamp>
# the noon rows against a baseline run (delta = new minus baseline); a baseline without analysis.json is analysed first:
python "$scripts/analyze_light_check.py" <new run> --compare <baseline run>
```

## What a run does

1. Checks the LEAP operations it needs, waits for login, then records your original state (the Phototools sky,
   water and day cycle combo values, the floaters, the settings it will change) and writes `restore.json`. If the
   original environment cannot be restored faithfully (the sky combo shows a legacy name or nothing, Personal Lighting or the
   fixed sky editor is open, a sky over a day cycle) it **refuses to start** and changes nothing, saying what to do.
   Restorable: the shared environment (the combos read Region Default / Region Default / Region Default or / `__None__`, as
   after Phototools' button or "Use Shared Environment"), restored with "Revert to Region Default"; a sky (day cycle combo
   `__None__`); a day cycle. A locally edited sky without a saved asset reads like the shared environment, so the
   combos cannot tell them apart: the run logs a warning at the start that it will be replaced by the shared environment
   at the end (an open editor, the likely sign of such an edit, is refused).
2. Sets `FSEnvironmentManualTransitionTime` to 0 (instant sky changes) and the scenario's `knobs`/`settings`
   (`null` leaves a knob alone and only records it). A knob that does not exist in the viewer (the 82080 baseline has
   none of the #447 Sky-tab settings) is skipped with a warning, so the same scenario runs on both.
3. Pauses the clouds (Phototools "Pause Clouds"), so the cloud region does not drift between steps.
4. For each sky: selects it, opens Personal Lighting (which captures it as the live sky), checks that the sky took
   (its Personal Lighting values must equal the previous sky's if the scenario names the same sky, else differ; on a
   mismatch the quick-environment button is tried), applies the sky's `controls` and moon placement, reads the sun
   azimuth, points the camera along it, takes the noon snapshot (sun at 66 degrees) and the sweep.
5. Restores the camera, the environment (Region Default button, or re-selecting the original sky and water, or the
   day cycle; it then compares the combo values with the original and warns on a difference), un-pauses the clouds if they are paused, closes the floaters it opened and
   puts every changed setting back. Also on an error or a stop file. Not possible if the viewer itself has gone
   (then `restore.json` has the old settings). Known limit: after a sky selection the water stays a copy of the
   region's water if the original water was Region Default.

How things are done (LEAP has no environment API, and its UI API cannot set a slider):

| Need | Route |
|---|---|
| Select a sky | inventory lookup by name (`LLInventory.collectDescendantsIf`, type `settings`, your inventory and the library root) gives the asset id; `UI.setSelectedByValue` moves the Phototools `WLPresetsCombo` highlight (it does not apply; the viewer's reply is a bare boolean without reqid, so the script fires it and waits with a ping instead of waiting for a reply; a refusal (false) is therefore not reported, and callers read the combo back with `UI.getValue`), then `WLNextPreset` and `WLPrevPreset` end on the same sky and apply it. Fallback: the quick-environment button named in the scenario (`Noon`, `Sunset`) |
| Click a button | `LLFloaterReg.clickButton` (calls the button's commit); mouse events on the widget path if refused; a mouse event the widget does not handle is an error |
| PBR or classic | Personal Lighting `probe_ambiance` (1 = PBR sky, 0 = classic; `doc/light/light-reference-numbers.md` explains why no shipped sky is PBR) |
| Sun elevation, moon position | Personal Lighting `sun_elevation`, `moon_azimuth`, `moon_elevation`: the slider's text field is focused, emptied with Backspace, filled with `LLWindow.pasteText` and committed with Enter (`LLWindow.keyDown path=...`); the value is read back with `UI.getValue` and must match to 0.006 degrees, else the step fails |
| Sky without a moon | the fixed sky editor (`env_fixed_environmentent_sky`, key `new`) has `moon_brightness`; Personal Lighting has not. The editor starts from a **clone of the current sky**, so the script selects Default first, opens the editor, clicks its Sun & Moon tab (`htab_panel_settings_sky_hbodies`, or `htab_panel_fs_settings_sky_hbodies` of the compact editor; scenario field `tab_buttons`) and sets `moon_brightness` 0. Optional, the least certain part; refuses if the editor is already open |
| Fixed camera | `LLAgent.setCameraParams` (follow cam, `camera_locked` and `focus_locked`, no lag): position = your avatar +2.5 m, focus 200 m away along the sun's azimuth, pitched up 10 degrees so the horizon sits at about 65 % of the frame height. `setFollowCamActive false` and `removeCameraParams` at the end |
| Snapshot | `LLViewerWindow.saveSnapshot` (no UI, no HUD, window size unless the scenario gives a size). The viewer's LEAP operation passes no format, so the file is **BMP data whatever its name**: the script saves `.bmp` and converts to PNG with Pillow, deleting the BMP |
| FPS | off by default (scenario `record_fps`, then `LLStats.getPerfData`, mean of the last 30 frames of `frametime.fps` per step; the wait per step is the larger of `per_step_s` and `min_frames` / fps). `getPerfData` replies with tens of KB, see "Big replies"; with it off the manifest's fps column is empty |
| Login | `listen` on the `StartupState` pump, `LLStartUp.postStartupState` for the current state, wait for the last entry of `getStateTable` |

No `UI.call` is used (a wrong function name there makes the viewer abort), and every fire-and-forget operation is
sent with a reply pump, because the viewer's dispatcher throws on a rejected operation that has none; errors in those
replies surface at the next round trip.

## Big replies (a viewer limitation the script avoids)

The first in-world dry run lost the connection on the reply to `LLWindow.getPaths` for the whole Phototools floater (about
100 KB). The serializer is not at fault: the binary LLSD the viewer writes (`llsdserialize.cpp`) matches the decoder
(string, key, binary and URI lengths are byte counts, the `size:` prefix of `llleap.cpp` counts bytes of the serialized
buffer, and every marker the formatter writes is handled; the only markers are `{ } [ ] ! 0 1 i r u s d l b k`).
`llprocess.cpp:189-199` documents the likely cause: on Windows the viewer's pipe writer sends 4 KB chunks and a chunk can
be written twice (`apr_file_write` reports EAGAIN with 0 bytes written although the chunk went out). A repeated chunk in the
middle of a frame shifts everything after it: the decoder meets string bytes where it expects a type marker (the `b'o'`
and `b'w'` of the log came from path names such as `phototools` and `View`) and the next length prefix is garbage. This
is inferred from the source and reproduced offline with a damaged frame (`tests/test_inworld.py`); the actual bytes of the
failed frame were not captured: on a protocol error the script now writes the whole bad frame to `bad_frame.bin` in the run
folder and the last 64 bytes of the previous good frame plus the diagnosis to `bad_frame.txt`, and says "repeated 4 KB
block at offset N" if the frame contains a chunk written twice. The script keeps replies small:
- A widget is found with `LLWindow.getPaths` on `//floater//name`, which lists only that widget's subtree. `getInfo` is not
  used on combo boxes (`LLComboBox::addInfo` lists every item: a big reply with a large sky collection) nor on a whole
  floater; it is used only on buttons, sliders and text fields, and cached. Combo values are read with `UI.getValue`.
- Inventory searches filter by name, ask for 25 items and again for 100 and 400 only if the reply was full and had no exact
  name (the viewer's filter is a substring match: "Default" and "Sunset" sit inside dozens of names), search the inventory root (it contains the settings folder) and then the library, and prefer an exact name, case-sensitive, then case-insensitive.
- FPS (`LLStats`) is off unless `record_fps`; `LLLeap.getAPIs` is not used.

A frame that cannot be parsed ends the connection as a **protocol error** (exit 5, hex dump in the log, not a claim that the
viewer exited). `ProtocolError` and `ViewerGone` are not `LeapError`s, so no handler of a failed operation can swallow a dead
connection and carry on (an earlier version turned a dead connection into FAIL rows and exit 1); a dry run that loses the
connection writes the rows it had. Only end of file is reported as the pipe closing (exit 3).

Replies that can still be large: `LLLeap.getAPI` (the operation list of an API with the operations' descriptions; LLWindow
and LLViewerControl are the largest, a few KB), `LLStartUp.getStateTable` (about 1 KB), a `--list-skies` without
`--sky-filter` (25 items, 5 to 8 KB), and `LLWindow.getPaths` under a slider or a floater widget with many children.

## Skies of the #447 scenario (`scenarios/l3_at1_handover.json`)

| id | Sky (inventory name) | Setup | Why |
|---|---|---|---|
| `pbr_default` | Default | probe ambiance 1 | the default sky as a PBR sky: the reference of `light-reference-numbers.md` (row 2), sun 66 degrees; owner check 1 |
| `pbr_sunset_crescent_high` | Sunset | probe 1, moon 55 degrees up, 90 degrees from the sun | owner checks 1 and 3: sunset with the moon high |
| `pbr_sunset_moon_opposite` | [TOR] SUNSET - Bangko 2 (Bangko 1 as second candidate) | probe 1, moon 12 degrees up, opposite the sun | owner check 1: moon rising opposite the sun; the test's reddest low-sun sky |
| `classic_default` | Default | probe ambiance 0 | owner check 2: classic sky, noon must match windows-latest, reddening and haze |
| `yellow_stars_01` | Phototools- Yellow Stars 01 | as is, optional | owner check: negative distance multiplier, where the cloud sun changes most (1.22); skipped if not in the inventory |
| `no_moon` | Default, then the fixed editor | `moon_brightness` 0, optional | owner check 4: no direct light at night |

The names come from `llatmosphericlight_test.cpp` (shipped Windlight skies) and the viewer's quick environments, not
from your inventory. If a name is not found the script uses the sky's `fallback_button` and warns in `run.json`
(`warnings`) — the data is then of a different sky, so pick better names with `--list-skies` and edit the scenario.
Each sky entry takes `names`, `fallback_button`, `controls` (any slider of the editor by its XUI name),
`azimuth_deg`, `moon` (`azimuth_offset_deg` from the sun, `elevation_deg`), `editor` (`personal`/`fixed`),
`tab_buttons`, `noon`/`sweep` (booleans), `optional`.

## Output (run folder, git-ignored under `.captures/`)

- `snapshots/<sky>_<step>_<e+05.00>.png` (step 0 is the noon snapshot, `e+66.00`)
- `manifest.csv`: sky, step, kind (`noon`/`sweep`), requested and read-back sun elevation, sun azimuth, snapshot path,
  UTC timestamp, fps, frame ms, the settings in force. Written row by row, so an aborted run keeps its rows.
- `run.json`: scenario, knob values, field of view, per-sky status / selected sky / signature (the Personal Lighting
  values, to see that the right sky took), camera, regions, warnings, your original environment.
  `scenario.json`, `restore.json`, `dry_run.json` as applicable. JSON files are written atomically.
- `viewer_log_excerpt.txt`: the viewer log from the start of the script to its end (best effort; see `--viewer-log`).
  The script's own lines (`LIGHTCHECK ...`) are in the viewer log anyway.
- After the analysis: `report.txt`, `analysis.json`, `analysis_<sky>.csv`, `contact_<sky>.png`.

Analysis regions are fractions of the frame computed from the camera pitch and `CameraAngle` (`run.json` > `regions`):
ground below the horizon, the sky band just above it, a cloud region high in the frame, and the whole frame. A change
between two steps is a **pop** when it is above `--pop-floor` (0.01 luma) and its rate per degree is more than
`--pop-factor` (4) times the median rate of the neighbouring steps (the step size changes from 0.5 to 0.1 degrees, so rates,
not raw differences, are compared). The mean absolute difference of a 64-column luma thumbnail between steps is the same
test for the whole frame. The thresholds are untuned; look at the contact sheet of anything flagged.

## For the baseline and the next stages

Run the same scenario in the windows-latest 82080 viewer (the #447 knobs are skipped there) and compare with
`--compare <baseline>`. For a new stage copy the scenario and change `knobs` / `settings` / `skies`; `--set` covers one-off
A/B runs. Sweep, camera, settle times and sky list are all scenario fields (`DEFAULTS` in `light_check.py`).

## Tests

```powershell
python -m unittest discover -s scripts/inworld/tests -v
```

Covers the LLSD notation/binary codecs and framing; the session (requests, errors, timeouts, the handshake timeout,
events, errors of fire-and-forget operations); the sweep and camera geometry; whole runs and dry runs against a fake
viewer (`tests/fake_viewer.py`), which implements the operations as the viewer's source suggests: restoration on
success, with a stop file after the camera was set, on a failing sky (exit 4), of an original sky and of a day cycle,
refusal of an environment that cannot be restored (Personal Lighting or the fixed editor open, a sky over a day cycle, a legacy name, nothing selected; both shared forms accepted), missing knobs, a viewer missing an
operation, the read-only dry run and the restored `--deep` dry run; `--restore`; the script as a real child process
over pipes; and the analysis (synthetic images with an injected pop, no false pop at the 0.5/0.1 degree step change,
`--compare` direction).

**Not tested against the real viewer** (the fake only reproduces what was read in the source): the real widget paths
and class names, whether the `End`/`Backsp`/`Enter` key names and the focus handling edit the slider text field,
`clickButton` on the Phototools buttons, the combo Next/Prev commit, `setCameraParams` with locked camera and focus,
the fixed-editor `no_moon` path and its tab click, the Phototools sky names, `STATE_STARTED` handling, `LLStats` field
names, the log excerpt lookup, snapshot timing, and the `--leap` launch line itself (simulated only). The dry run (and `--deep`) is the quick check
of most of these.

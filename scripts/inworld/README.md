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

**Do not touch the desktop during a run.** Do not minimize the viewer, cover it, or let a notification resize the 3D view: the
viewer does not render while it is minimized, and a snapshot then reads back the cleared buffer (a black frame; the first
in-world run had one at -0.6 degree). On Windows the script waits while the viewer window is minimized and takes a black
frame, or one with a run of all-zero rows (5 % of the height or more), again up to three times, logging each retake. The
analysis reports any frame that is still bad as bad data (`bad_frame` in the CSV, left out of the series and the pop search).

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
| Sky without a moon | the fixed sky editor (`env_fixed_environmentent_sky`, key `new`) has `moon_brightness`; Personal Lighting has not. The editor starts from a **clone of the current sky**, so the script selects Default first, opens the editor, clicks its Sun & Moon tab (`htab_panel_settings_sky_hbodies`, or `htab_panel_fs_settings_sky_hbodies` of the compact editor; scenario field `tab_buttons`) and sets `moon_brightness` 0. Widget paths use the view name `Fixed Environment` (`Fixed%20Environment`, as the floater XML names it, not the `env_fixed_environmentent_sky` registry name); the water editor's view has the same name and is found first, so the script refuses while it is open, and if it was opened earlier in the viewer session the tab lookup fails and says to restart the viewer. Optional, the least certain part; refuses if the sky or water editor is already open |
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

# In-world OpenGL submission capture (`perf_check.py`)

Dev tooling for the OpenGL modernisation (stage G0, `doc/gl/instrumentation.md`). It needs a **development viewer
built with `-DVKS_GL_STATS=ON`**: that build has the LEAP API `GLSubmitStats`. Release and the other lines do not,
and `perf_check.py` says so and stops. It uses the same launch lines and quoting rules as `light_check.py` above
(`--leap` twice, paths without spaces, forward slashes); only the script and its arguments change:

```powershell
$run = "$py $scripts/perf_check.py --scenario $scripts/scenarios/gl_submit_g0.json --out C:/Users/annes/source/repos/vulkanstorm/.captures/glsubmit-$stamp"
& $exe --leap $run --leap $noop
```

Log in by hand, stand where the scene is worth measuring and leave the viewer alone (no minimizing, no UI use). The run
takes about ten minutes for the shipped scenario. `--dry-run` (at the login screen with `--no-login-wait`, or in-world)
only checks that the API, its operations and the settings exist.

What a run does:

1. `selfBench`: the cost of the hooks in this binary (ns per draw's worth of hooks, not collecting and collecting;
   ns per pass scope with and without GPU timers).
2. The proof: the G-buffer is compared with **one bit flipped in the second readback**. The check must say DIFFERENT. If it
   does not, the run fails (exit 4): an IDENTICAL from a check that cannot see a one-unit difference means nothing. It must be
   exactly the injection: `color0` flagged injected with one differing pixel and no other attachment differing (a real difference
   would otherwise hide the proof). A compare that no frame took (stale results), or a G-buffer that did not run in a preset, also fails the run.
   Reflection probe faces are never compared; each result lists, per attachment, its distinct pixel values (an attachment with one is an empty pass).
3. For each preset (`current` = where you stand, then three headings from your avatar, 2.5 m up): the camera is placed;
   the frame rate is measured for 5 s with nothing collecting; then a window of 120 frames is collected (counters per view
   and pass, CPU ms, GPU ms) and written to `window_<preset>.json`; a Frame Profile is written (`profile_<preset>.json`, with a
   `submit` section); then the identical-image check runs on every pass the frame has (G-buffer, sun cascades, spot shadows,
   post-deferred), each rendered twice in the same frame and compared bit for bit (`compare_<preset>.json`). Both renders use
   today's path, so **any DIFFERENT is a bug of the harness or of the pass** (exit 4), not a rendering change.
4. Restores your settings and camera (also on error; `--restore RUNDIR` repeats it in a fresh viewer).

Keep `--out` under a `.captures` folder (the default is `.captures` in the repository): the viewer writes the window files itself
and refuses any `.json` path outside its log folder or a `.captures` folder (no relative paths, no `..`, no UNC paths).

Output: `perf.json` (everything, with the hook bench and the overhead per preset), `manifest.csv` (one row per preset),
`window_*.json`, `compare_*.json`, `profile_*.json`, `scenario.json`, `restore.json`, `viewer_log_excerpt.txt`.

```powershell
python "$scripts/analyze_perf_check.py" C:/Users/annes/source/repos/vulkanstorm/.captures/glsubmit-<stamp>
# against another run (a candidate stage against the baseline); delta = new minus baseline
python "$scripts/analyze_perf_check.py" <new run> --compare <baseline run>
```

`report.txt` has, per preset: the per-frame totals, what each draw costs the driver (vertex buffer changes, attribute
respecifications, `glBindTexture`, uniform uploads, state toggles per draw), the redundant share of unfiltered texture
binds, the mergeable run lengths, the views, the passes by GPU ms, and the identical-image results.

By hand, without the script (the same API from any LEAP client, or the debug settings): `RenderGLSubmitStats` on collects
every frame; `RenderGLSubmitCompare` set to a pass (-1 all) for one frame logs `GLSubmitCompare` lines and answers
`GLSubmitStats.compareResults`; Develop > Render > Frame Profile writes the profile with the `submit` section.

## The census session (`gl_census.json`)

Phase 2b's step 1 (`doc/gl/instrumentation.md`, "The census session"): the same launch line with
`--scenario $scripts/scenarios/gl_census.json` and `--out .../.captures/glcensus-$stamp`, on a devel build with
`-DVKS_GL_STATS=ON`. Log in, wear a HUD, stand in the crowd (Rooftops; then once more at home or a dense-mesh place), near a
mirror if there is one, and leave the viewer alone (about 10 minutes). The tier is Ultra, as in mc1b.

It proves the identical-image check on the G-buffer (1), a probe face (13), the HUD (14) and an impostor (15): one flipped bit
must be caught in each that ran; a pass that did not run in its 120 armed frames (no HUD worn, no impostor regenerated) is
recorded as not proven, with a warning. Then, for `still` (sun shadows, all shadows, few non-impostors, mirrors on) and
`moving` (turning at 10 deg/s; sun, all): the frame rate with nothing collecting, a timed window (`window_<run>.json`), every
compare armed for up to 120 frames (`compare_<run>.json`), a census window (`census_window_<run>.json`) and the resident
copy census (`census_resident_<run>.json`). `perf.json` has, per run, the census files and the resident headline (faces, the
instancing ratio of faces of 64 vertices and more), and `proofs` the proof of each pass.

# Sorted alpha against stochastic in a crowd (`alpha_crowd.json`)

Stage 0 of the legacy-alpha work: does `RenderAlphaSortMethod=1` (stochastic, S=8) cost more than `0` (sorted) when many avatars
with rigged mesh are in view, and what do the sorted alpha passes cost in CPU, GPU, draws, state changes, uploads and samples.
Same build and launch as the section above (`-DVKS_GL_STATS=ON` viewer, `--leap` given twice, paths without spaces).

**The numbers are from one machine: an RTX 5070 Ti.** Milliseconds are an upper-end view and say little about a weak GPU. The
hardware-independent counters of **method 0** (draws, shader binds, palette uploads, uniform bytes, and the alpha samples
from `GL_SAMPLES_PASSED`: samples, not fragments) are the gate for weak GPUs. Sorted alpha is the transparency path of a user who chooses it
(the viewer needs OpenGL 4.6 since the floor, so no GPU is left without Stochastic), so a later stage must not raise those counters in a crowd.
Method 1's samples (multisample targets, several passes) are not comparable to method 0's. An active sample query can perturb
early-Z, so the frame rate measured with nothing collecting (`fps_stats_off`, the "frame ms" column) is the clean figure.
That frame rate is the median of 3 probes of 5 s per method (the methods alternate between probes); the report shows the spread of the
probes per window (`fps spread %`), and the verdict is `inconclusive` when the difference is no larger than that noise. `perf.json`
records the viewer window size and whether it is maximised (`window_geometry`, checked again at the end of the run); keep it the same
between runs you compare. Alpha sample counts above width x height x 8 x draws are dropped with a warning (they are a measurement
defect, not a finding).

The scenario takes four camera presets (the current view and three headings), and for each preset one 120-frame window with
method 0 and one with method 1 (`variants`; the order alternates from preset to preset), GPU timers on, no image compare.
`perf_check.py` restores `RenderAlphaSortMethod` and the other settings at the end, also on Ctrl-C. `RenderAlphaSortMethod` is
persisted, so after a crash or a killed viewer run `perf_check.py --restore <run folder>` (the same `--leap` line with `--restore`)
in a fresh viewer. Stand where the crowd is and leave the viewer alone. **Turn vsync and any frame limiter off first**
(Preferences > Graphics): the run records `RenderVSyncEnable`, `FramePerSecondLimit` and `FSLimitFramerate` in `perf.json` and warns
when one is on, because a capped frame rate hides the difference.

Scenes, one run each:

- **S1, local OpenSim with ~40 NPCs.** For the region setup, NPC spawner and LSL kit see `opensim-bench/README.md` (built for particles, but it
  gives a local region with 30 NPCs). Dress the NPCs in rigged mesh hair and clothing for the alpha-crowd scene.
- **C, a real crowded venue** with 30+ mesh avatars. **T, a heavy-transparency scene** (glass, foliage, particles).
- **Low tier** (`alpha_crowd_lowtier.json`): the same scene with the Low list of `featuretable.txt` for the settings that matter in a
  crowd (3 non-impostor avatars, complexity cap 35000, LODs, far clip 64, no shadows, reflections or SSAO, 512 px textures). A crowd
  that is all full avatars is the worst case for alpha; this tier does not show it. Make the viewer window **1920x1080** (windowed, not
  minimised) before the run, because `perf_check.py` cannot set the size. `RenderMaxTextureResolution` only applies to textures fetched
  afterwards: set it before login, or relog after changing it. On a GPU that has OpenGL 4 the low-tier method 1 still runs
  stochastic; the figure that matters on the low tier is method 0. Run the low tier at least in C.

```powershell
$exe     = 'C:\Users\annes\source\repos\vulkanstorm\work\alpha-crowd\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe'
$py      = 'C:/Users/annes/AppData/Local/Python/pythoncore-3.15-64/python.exe'
$scripts = 'C:/Users/annes/source/repos/vulkanstorm/work/alpha-crowd/scripts/inworld'
$caps    = 'C:/Users/annes/source/repos/vulkanstorm/.captures'
$stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$noop    = "$py $scripts/perf_check.py --noop"
$scene   = 'C'          # S1, C or T: names the run folder
$tier    = ''           # '' for the default tier, '_lowtier' for the low tier (1920x1080 window)
$sc      = "$scripts/scenarios/alpha_crowd$tier.json"
$run     = "$py $scripts/perf_check.py --scenario $sc --out $caps/alpha-$scene$tier-$stamp"
Set-Location (Split-Path $exe)
& $exe --leap $run --leap $noop
```

Log in by hand, go to the scene, wait for the crowd to rez. A run takes roughly 5 to 8 minutes (per preset: 3 frame rate probes of 5 s per method, alternating, then one 120-frame window per method). Then, for each run folder:

```powershell
python "$scripts/analyze_perf_check.py" "$caps/alpha-C-<stamp>"
```

`report.txt` has, per preset and method, the frame time (from the frame rate with nothing collecting), the main view's CPU and GPU ms
and, for the `ALPHA` passes, CPU ms, GPU ms, draws, shader binds, palette uploads, uniform bytes and samples; the medians over the
presets; and the verdict. **Rule:** per preset the delta of method 1 minus method 0; stochastic is slower in a crowd when the median
frame time delta is more than 3 % of the median sorted frame time and the median alpha passes' CPU time delta is **at least half** of it.
The result is one of `not_slower`, `slower_explained` (by the alpha pass CPU time), `slower_not_explained` (a GPU-side or other cost: the
report always shows the main view and alpha pass GPU deltas) or `inconclusive` (the presets disagree in sign). It warns when method 1
shows no alpha draws or the same as method 0, which means stochastic probably was not in use. The question is answered by S1 and C both.
Keep the run folders: later stages are gated by re-running this scenario and comparing (`--compare <baseline run>`).

# Second Life session for the multicore baseline (`mc1_sl.json`, `mc1_sl_launch.ps1`)

`perf_check.py` can run the whole stage 1 session (`doc/perf/multicore-baseline-2026-11.md`) on Second Life while you only log in and teleport once.
The measuring build (Tracy on) has no `GLSubmitStats` API, so the scenario runs with `--no-glstats`: the plugin then takes frame rates
from the viewer log's `FrameCounters: frames=N` line (once a second), and the data come from the Tracy captures.

**GPU time (multicore stage 1b).** A Tracy build records GPU zones in every capture (`Frame`, `generateSunShadow`, `sun cascade 0..3`,
`projector tiles` with a `projector tile` or `projector layer` per redraw, `renderGeomDeferred`, `renderDeferredLighting`,
`renderFinalize`, `swap` and the finer ones); `tracy-profiler.exe` shows them, but `tracy-csvexport` 0.11.1 exports no GPU zone. For numbers,
configure the measuring build with `-DVKS_GL_STATS=ON` as well and start the session with `-GpuWindow`
(`perf_check.py --no-glstats --gpu-window --frames N`): the flow stays the one above (log frame rate, warm-up, one Tracy capture per
window, projector stats), and after each capture has ended one `GLSubmitStats` window of N frames (600 from the launcher) with GPU
timers is collected and written to `window_<preset>.json`: GPU ms per view (`frame`, `main`, `sun cascade 0..3`, `projector tiles`)
and pass, read with `analyze_perf_check.py`. The window runs after the capture so neither the capture nor the log frame rate holds
the cost of its timers. On a viewer without the API the switch does nothing (and `--dry-run` reports it as a failure).
A window whose requests fail is recorded (`window_failures`, with the error) and the session goes on. `--gpu-window` without
`--no-glstats` is refused.

`mc1b_rooftops` is the stage 1b session (`doc/shadows/sun-shadow-cost-study.md`, section 9, on its study branch), about 30 minutes:

| View | Runs (in this order) |
|---|---|
| `RT-still`, the camera as it is | `sun`, `none`, `all` (`RenderShadowDetail` 1, 0, 2), `splits2` (sun with `RenderShadowSplits` 2) |
| `RT-moving`, turning at 10 deg/s | `all`, `none`, `sun` |
| `RT-fixed`, still at heading 0 | `sun` (the pair of the next two), `night` (World > Environment > Midnight), `high` (the High tier, sun) |

It uses four scenario fields that are new with it:

- `soft_settings` (scenario and variant): settings applied like `settings`, except that one the viewer lacks or does not take
  (a clamped value) is recorded in `perf.json` (`soft_settings_not_taken`) and the run goes on. The scenario's are the feature
  table's Ultra list, the `high` variant's are the values in which the High list differs, so the tier does not depend on what the
  scratch profile picked. A variant's `soft_settings` go back to the scenario's values when the next variant starts, also
  those whose value did not take (the viewer may hold a clamped one).
- a preset's `variants`: the ids of the variants this view is measured with (default: all of them; an empty list is refused).
  Every view applies its first variant afresh, with its warm-up, also when the previous view ended on the same one.
- a variant's `environment`: one of `sunrise`, `noon`, `legacy noon`, `sunset`, `midnight` (the World > Environment menu),
  selected with `UI.call World.EnvSettings`; the shared environment comes back with the next variant and at the end.
  **The script cannot read which environment is selected** (no LEAP API and no setting holds it): it assumes the shared
  environment at the start, which the hand-off pop-up asks for, and it goes back to the shared one, not to a sky you had
  picked. `FSRepeatedEnvTogglesShared` is off for the run, so selecting a sky never toggles back. After the switch and its
  warm-up the dark gate's decision light must have changed: if it did not, the run is listed in `environment_failures` and the
  session ends "with problems"; without the readout the switch is only a warning (`environment_check.verified` null).
  Each run records its `environment`; `restore.json` holds it for `--restore`.
- `settle.warmup_capture_s`: after the warm-up of a run, a throw-away Tracy capture of that many seconds (whole seconds, at
  least 1), deleted at once with its log, so the first seconds of a profiler connection are in no measured capture. When it
  cannot start, the run goes on.

Every run of the log flow records the dark gate's readout (`DirectionalShadows.stats`, when the viewer has the API: asked once)
at its start and its end (`directional_shadows`, `directional_shadows_end`) and the frames the cascades were rendered and
skipped in between (`directional_delta`), which says whether the night run rendered its cascades or was skipped as too dark.

New options and scenario fields (all optional; the older scenarios are unchanged):

- `--tracy-capture <tracy-capture.exe>`: for each measurement window (scenario `window.seconds`, default 60) and for the teleport
  series, `tracy-capture -a 127.0.0.1 -o <run>/<id>.tracy -s <seconds> -f` is started just before the window and waited for. One capture at a
  time; its stdout/stderr go to `<id>.capture.log` (stdout is the LEAP channel); `perf.json` `captures` lists each one with its UTC start, exit
  code and file size; a failed capture makes the run exit 4. Abort or Ctrl-C stops a running capture with a hard kill: that capture's file is not kept.
- Preset `"motion": {"orbit_deg_per_s": 10}` (needs a `camera`): during the window the camera turns around the avatar by `point_camera` calls (about 10 Hz).
- `"owner_teleport": {...}`: after login and settle the plugin records the login region (the first `Entering region [Name]` line of the viewer log, which is the agent's own region: the viewer logs neighbours
  too; `--start-region` overrides) and position, shows a local pop-up (`LLNotifications.requestAdd`, not nearby chat) and waits for you to teleport by hand: the region
  cell (floor(global / 256), assuming 256 m regions: walking over a border counts as arrival) changes and the position holds still for 3 polls (the pop-up repeats every 3 minutes). The busy region is the first
  `Entering region` line after the teleport began (`--busy-region NAME` overrides; the teleports stop with an error when it is unknown). Then it settles (`after_teleport_s`) and measures there. At the end
  it restores the settings and shows "Session done: you can quit.", or "Session ended with problems: see perf.json" on a failed or stopped run. `AFKTimeout` is set to 0 for the run.
- `"teleports": {"within": 5, "second": 5, ...}`: N teleports within the region (start + offset, start, ...) and N between the busy region and
  the login region (`--tp-region NAME [--tp-pos X,Y,Z]` overrides the login region), a return to the busy region when N is odd (so 12 teleports for 5 + 5, kind `return`). Teleports use the `LLTeleportHandler` `teleport` operation (`LLAgent.requestTeleport` opens a confirmation dialog). A failed teleport is recorded (`failed`, `teleport_failures`) and the series goes on. Each is timed from the request
  to the arrival (the agent's global position moves; `LLAgent` has no arrival event) and to the last movement; `perf.json` `teleports.results`. One
  continuous capture (`MC1-teleports`, `capture_s`) covers the series. `--leap` splits at spaces: write a space in a region name as `%20`.
- The frame rate comes from `FrameCounters` lines of at least 1 s each, taken as 1 s: it reads low by up to one frame time per line (about 2.5 % at 20 fps, under 1 % at 60 fps). The plugin's camera and teleport polling (about 20 LEAP calls a second while moving) is a known small overhead in the measured windows.
- `"fps_probe": {"seconds": 60, ...}`: the frame rate with no capture connected (`fps_without_profiler` in `perf.json`).

One command for the session (scratch profile `.captures\mc1-profile-<stamp>`, new for every launch, which keeps credentials if you tick "remember": delete it afterwards; `-DryRun` uses a fresh profile under `.fixture-tmp`, run folder `.captures\mc1-sl-<stamp>`):

```powershell
.\scripts\inworld\mc1_sl_launch.ps1                  # -TpRegion NAME overrides the second region; -StartRegion/-BusyRegion/-Scenario below; -DryRun is the login-screen check
```

Log in by hand (any region); when the pop-up asks, teleport to the busy region and leave the viewer alone.

Unattended runs (a remembered login in one fixed local profile; the scripts never read, type, copy or log the credentials):

```powershell
.\scripts\inworld\mc1_sl_launch.ps1 -SetupLogin     # once, by the owner
.\scripts\inworld\mc1_sl_launch.ps1 -AutoLogin -Scenario mc1b_rooftops -StartRegion "Home" -BusyRegion "Busy" -GpuWindow
.\scripts\inworld\mc1_sl_launch.ps1 -DeleteProfile  # when the series ends
```

- The profile is one folder outside the repo, `%LOCALAPPDATA%\Vulkanstorm-autologin` (local, not roaming). Its credential store
  (`Roaming\Vulkanstorm_x64\user_settings\bin_conf.dat`) holds a password-equivalent. **Never copy, zip, sync or share that folder.**
- `-SetupLogin` writes a marker file and starts the viewer with that profile at the login screen; no scenario runs. Log in by hand with
  "remember me" ticked, go to the exact measuring spot in the busy region (for Rooftops the "TT Rooftops dark" spot) with World >
  Environment on Shared Environment, and **quit there**: autologin logs in at the last location.
- `-AutoLogin` passes the viewer's `--autologin`, `perf_check.py --auto-handoff` and `--max-wait-s 300`, and needs `-StartRegion` (the login
  region) and `-BusyRegion`. It refuses a folder without the marker, a junction or symlink, and a profile whose credential store is missing
  or empty (existence and size only; the content is never read, and a present file does not prove the login works: a login that does not
  finish within 300 s ends the run cleanly with a log line and the problem pop-up).
- The hand-off needs no click: logged in at the busy region (names compare case-insensitively), that position is the spot and nothing
  teleports. Elsewhere it teleports to `-BusyPos X,Y,Z` with the teleport series' own code (`teleport_once`), accepts a first teleport that
  only arrived late, fails on a landing more than 5 m off, and without `-BusyPos` ends the run with a message to redo `-SetupLogin` at the
  spot. It never waits for a human. The manual flow (no `-AutoLogin`) is unchanged.
- After `perf_check` ends (success or failure) the launcher quits the viewer with a window-close request, waits up to 120 s and, if it does
  not exit, says so loudly and leaves it (nothing is killed). It copies the viewer's own `Vulkanstorm.log` (only that file) into the run folder.
- Be logged out of Second Life everywhere else before a run: a second login of the same account kicks the first.
- `-DeleteProfile` refuses while the viewer runs and without the marker, deletes the credential files (`bin_conf.dat`, `password.dat`) first
  and checks they are gone, then the folder. Anything left is listed in red and the exit code is 2. Run it when the series ends.
- `VKS_AUTOLOGIN_PROFILE` and `VKS_AUTOLOGIN_PROCESS` (folder and process name) exist for `tests\test_launcher.py` only.

Launcher parameters: `-StartRegion NAME` (the region you log in to: the viewer does not log it at login; `--start-region` in `perf_check.py`),
`-BusyRegion NAME` (the region you teleport to, used when the viewer log does not give it; `--busy-region`), `-TpRegion NAME` (the second
region of the teleports instead of the login region) and `-Scenario NAME` from `scripts\inworld\scenarios`:

- `mc1_sl` (default): still, moving, teleport series (12 teleports, 30 s pauses, one 900 s capture) and the fps probe, about 15 minutes.
- `mc1_sl_teleports`: the teleport series only (6 teleports between the busy and the home region, one 420 s capture). It needs `-StartRegion`
  or `-TpRegion`; the launcher refuses to start without one.
- `mc1_sl_teleports_manual`: one 600 s capture while you teleport by hand between the busy and the home region. Busy regions such as Risk
  refuse automated in-region teleports (`NoGoodTPDestination`), which is why this scenario exists; it runs no series, so it needs no region name.

- `mc1_rooftops` / `mc1_rooftops_moving`: the Rooftops shadow-cost split (still and moving, or the moving view alone), each view measured with the
  `RenderShadowDetail` variants none / sun / all. Every variant of a moving preset turns the camera for its whole capture. After each variant switch
  the script waits until the frame rate has settled (the last `settle.warmup_stable_windows` 5 one-second FrameCounters windows each within `warmup_tolerance` 15 % of their median, between `warmup_min_s` 10 s and
  `warmup_max_s` 45 s; the log is read once a second) and logs the wait. `perf.json` holds, per preset, `camera_log` (once a second: the camera the script commanded; the viewer has no
  read-back), `warmup`, and `projector_stats` (plus `projector_stats_end` and `projector_delta`). A viewer without the `ProjectorShadows` API (it is
  compiled only with `-DVKS_GL_STATS=ON`, so the Tracy measuring build has none) records `{"available": false, ...}` and says so once in the log.

Measuring by hand (outside the scripted session): an unfocused viewer throttles itself (`BackgroundYieldTime`), so set `BackgroundYieldTime` to 0, and
switch the frame cap off (`FSLimitFramerate` false), or the numbers read low. The scenarios already set the frame limiter off. Not covered by the offline
tests: the real LEAP notification, `LLAgent.getPosition` timing across a real teleport, and `tracy-capture` itself.

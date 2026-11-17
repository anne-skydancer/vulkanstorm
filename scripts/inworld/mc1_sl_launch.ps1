# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
<#
Starts the multicore stage 1 measuring viewer for the Second Life session (doc/perf/multicore-baseline-2026-11.md):
a scratch profile (.captures\mc1-profile-<stamp>, new for every launch; it keeps your credentials if you tick "remember": delete it afterwards. -DryRun uses a fresh folder under .fixture-tmp that the caller deletes), the perf_check.py LEAP plugin with scenarios\mc1_sl.json
and --tracy-capture, the throw-away noop plugin entry, and the run folder .captures\mc1-sl-<stamp>.

  .\mc1_sl_launch.ps1                     (the session: log in by hand at home; a pop-up asks you to teleport)
  .\mc1_sl_launch.ps1 -TpRegion "Name"    (only to override the second region of the teleports: default is the login region)
  .\mc1_sl_launch.ps1 -StartRegion "Home" -BusyRegion "Busy"
                                          (the names of the region you log in to and the one you teleport to; the viewer does not
                                           log the login region, and the busy name is the fallback when the log has none; a space is
                                           escaped by the script)
  .\mc1_sl_launch.ps1 -Scenario mc1_sl_teleports         (teleports only, one capture; needs -StartRegion or -TpRegion)
  .\mc1_sl_launch.ps1 -Scenario mc1_sl_teleports_manual  (one 600 s capture while you teleport by hand, for busy regions that refuse
                                                          automated teleports; no series, so no region name needed)
  .\mc1_sl_launch.ps1 -Scenario mc1_rooftops_moving -StartRegion "Home" -BusyRegion "Busy"
                                          (Rooftops: only the moving view, shadow variants none / sun / all, each after a warm-up)
  .\mc1_sl_launch.ps1 -Scenario mc1_rooftops -StartRegion "Home" -BusyRegion "Busy" -GpuWindow
                                          (stage 1b, GPU time: the still and the moving view, three shadow variants each. Every run
                                           is a 60 s Tracy capture, which holds the GPU zones, and then, on a build configured with
                                           -DVKS_GL_STATS=ON, one GLSubmitStats window of -GpuWindowFrames frames with GPU timers:
                                           window_<preset>.json, GPU ms per view and pass. Without that API the switch does nothing)
  .\mc1_sl_launch.ps1 -Scenario mc1b_rooftops -StartRegion "Home" -BusyRegion "Busy" -GpuWindow
                                          (stage 1b with the sun-shadow cost study's runs, about 30 minutes: Ultra tier set by the
                                           script; still: sun, none, all, two splits; turning: all, none, sun; fixed heading: sun,
                                           midnight, High tier. Keep World > Environment on Shared Environment: the script cannot
                                           read the selected sky and goes back to the shared one. See scenarios\mc1b_rooftops.json)
  -Scenario mc1_sl (the default) is the full session: still, moving, teleports, fps probe.
  .\mc1_sl_launch.ps1 -DryRun             (at the login screen: --dry-run --no-login-wait, do not log in)

Unattended runs (a persistent local profile that logs in by itself; the credentials are never read, typed, copied or logged by these scripts):
  1. One-time setup:  .\mc1_sl_launch.ps1 -Profile .captures\mc1-autologin -SetupLogin
     The viewer starts with that profile at the login screen and no scenario runs. Log in by hand with "remember me" ticked, then quit.
  2. Each run:        .\mc1_sl_launch.ps1 -Profile .captures\mc1-autologin -AutoLogin -Scenario mc1b_rooftops -StartRegion "Home" -BusyRegion "Busy" -GpuWindow
     -AutoLogin passes the viewer's --autologin (log in as the last saved user). It needs -Profile and refuses to start when the profile
     has no stored login yet (it only checks that the viewer's bin_conf.dat exists in the profile, never what is in it).
  Be logged out of Second Life everywhere else first: a second login of the same account kicks the first.
  3. When the series ends:  .\mc1_sl_launch.ps1 -Profile .captures\mc1-autologin -DeleteProfile
     It copies the profile's viewer logs (not chat or conversation logs) into the newest .captures\mc1-sl-* run folder (viewer-logs\),
     then deletes the profile folder, saved login included. Nothing else is started.
  -Profile must be a folder under the repo's .captures (gitignored); any other path is refused.

Paths must contain no spaces (--leap is split at spaces); see README.md ("Launch").
#>
param(
    [string]$TpRegion = '',
    [string]$StartRegion = '',   # the region you log in to (the viewer does not log its name at login)
    [string]$BusyRegion = '',    # the region you teleport to when the pop-up asks
    [string]$Scenario = 'mc1_sl', # mc1_sl (captures, teleports, fps) or mc1_sl_teleports (teleports only)
    [string]$Exe = 'C:\Users\annes\source\repos\vulkanstorm\work\mc-stage1\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe',
    [switch]$GpuWindow,          # after each capture: a GPU-timed GLSubmitStats window (a -DVKS_GL_STATS=ON build)
    [ValidateRange(1, 100000)][int]$GpuWindowFrames = 600, # frames in that window (the viewer takes 1..100000)
    [string]$Profile = '',       # a persistent profile folder under .captures, used instead of a fresh .captures\mc1-profile-<stamp>
    [switch]$AutoLogin,          # pass --autologin (needs -Profile with a remembered login)
    [switch]$SetupLogin,         # one-time: start the viewer with -Profile at the login screen, no scenario; log in by hand, "remember me", quit
    [switch]$DeleteProfile,      # save the profile's logs (no chat) into the newest mc1-sl-* run folder, then delete the profile; starts nothing
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$repo = 'C:\Users\annes\source\repos\vulkanstorm'

# -Profile: only a folder under the repo's gitignored .captures (resolved to a full path first, so .. cannot escape)
$capturesRoot = [IO.Path]::GetFullPath((Join-Path $repo '.captures')).TrimEnd('\') + '\'
if ($Profile) {
    $profFull = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Profile)) { $Profile } else { Join-Path $repo $Profile })).TrimEnd('\')
    if (-not ($profFull + '\').StartsWith($capturesRoot, [StringComparison]::OrdinalIgnoreCase) -or ($profFull + '\') -eq $capturesRoot) {
        throw "-Profile must be a folder under $capturesRoot (gitignored), got: $Profile"
    }
}
if (($AutoLogin -or $SetupLogin -or $DeleteProfile) -and -not $Profile) { throw '-AutoLogin, -SetupLogin and -DeleteProfile need -Profile <folder under .captures>' }
if ($Profile -and $DryRun) { throw '-Profile cannot be combined with -DryRun (the dry run always uses a fresh folder)' }
if ($AutoLogin -and $SetupLogin) { throw '-AutoLogin and -SetupLogin exclude each other' }
if ($DeleteProfile -and ($AutoLogin -or $SetupLogin)) { throw '-DeleteProfile runs alone' }

# the viewer's stored login is bin_conf.dat in its user_settings folder; only its presence is tested, never its content
function Test-StoredLogin([string]$dir) {
    if (-not (Test-Path -LiteralPath $dir)) { return $false }
    return [bool](Get-ChildItem -LiteralPath $dir -Recurse -File -Filter 'bin_conf.dat' -ErrorAction SilentlyContinue | Where-Object { $_.Length -gt 0 } | Select-Object -First 1)
}

if ($DeleteProfile) {
    if (-not (Test-Path -LiteralPath $profFull)) { throw "no such profile folder: $profFull" }
    $run = Get-ChildItem -LiteralPath (Join-Path $repo '.captures') -Directory -Filter 'mc1-sl-*' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $run) { throw 'no .captures\mc1-sl-* run folder to save the logs into; nothing deleted' }
    $dest = Join-Path $run.FullName 'viewer-logs'
    New-Item -ItemType Directory -Force $dest | Out-Null
    # viewer logs only: *.log / *.old files, never chat or conversation logs (chat.txt, conversation.log, anything with chat in its name)
    $n = 0
    foreach ($f in @(Get-ChildItem -LiteralPath $profFull -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.log', '.old' -and $_.Name -notmatch 'chat|conversation' })) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dest $f.Name) -Force; $n++
    }
    Remove-Item -LiteralPath $profFull -Recurse -Force
    Write-Host "Saved $n log file(s) into $dest; deleted the profile $profFull"
    return
}
if ($AutoLogin -and -not (Test-StoredLogin $profFull)) {
    throw "profile $profFull has no remembered login yet. One-time setup: .\mc1_sl_launch.ps1 -Profile $Profile -SetupLogin (log in by hand with 'remember me' ticked, then quit), then run again with -AutoLogin."
}

# the scenario first: a wrong name or a teleport series that cannot know its home region must fail before anything starts
$scFile = Join-Path $PSScriptRoot "scenarios\$Scenario.json"
if (-not (Test-Path $scFile)) { throw "no such scenario: $scFile" }
$scJson = Get-Content -Raw $scFile | ConvertFrom-Json
if (-not $DryRun -and -not $SetupLogin -and $scJson.teleports -and [int]$scJson.teleports.second -gt 0 -and -not $StartRegion -and -not $TpRegion) {
    throw "scenario $Scenario teleports between two regions: give -StartRegion NAME (the region you log in to) or -TpRegion NAME, the viewer does not log the login region"
}

$scripts = ($PSScriptRoot -replace '\\', '/')
$tracy   = Join-Path (Split-Path $Exe | Split-Path | Split-Path) 'packages\bin\tracy-capture.exe'
foreach ($f in $(if ($SetupLogin) { @($Exe) } else { @($Exe, $tracy) })) { if (-not (Test-Path $f)) { throw "missing: $f" } }

# The user's real folders, read from Windows (not from $env:, which an earlier run in this window may have pointed at a
# scratch profile); every change to $env: below is undone before this script returns.
$realLocal   = [Environment]::GetFolderPath('LocalApplicationData')
$realRoaming = [Environment]::GetFolderPath('ApplicationData')
$saved = @{ APPDATA = $env:APPDATA; LOCALAPPDATA = $env:LOCALAPPDATA }

# the real interpreter, not the Microsoft Store alias: the installed one first, then py with the real folders
$py = Join-Path $realLocal 'Python\pythoncore-3.15-64\python.exe'
if (-not (Test-Path $py)) {
    $env:LOCALAPPDATA = $realLocal; $env:APPDATA = $realRoaming
    try { $py = (& py -c "import sys;print(sys.executable)" 2>$null | Select-Object -Last 1) } catch { $py = $null }
    $env:APPDATA = $saved.APPDATA; $env:LOCALAPPDATA = $saved.LOCALAPPDATA
}
if (-not $py -or -not (Test-Path $py) -or ($py -notmatch 'pythonw?\.exe$')) { throw "no Python interpreter found (got: $py)" }
$py = ($py -replace '\\', '/')

# scratch profile; the dry run gets a fresh one every time (it must never find saved credentials)
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$prof = if ($Profile) { $profFull } elseif ($DryRun) { Join-Path $repo ".fixture-tmp\mc1-dry-$stamp" } else { Join-Path $repo ".captures\mc1-profile-$stamp" }
New-Item -ItemType Directory -Force "$prof\Roaming", "$prof\Local" | Out-Null
$env:APPDATA = "$prof\Roaming"
$env:LOCALAPPDATA = "$prof\Local"
$pushed = $false
try {   # from here every path puts this window's folders back
    $out   = (Join-Path $repo ".captures\mc1-sl-$stamp") -replace '\\', '/'
    $plugin = "$py $scripts/perf_check.py --scenario $scripts/scenarios/$Scenario.json --out $out --no-glstats --tracy-capture " + ($tracy -replace '\\', '/')
    if ($TpRegion) { $plugin += ' --tp-region ' + [uri]::EscapeDataString($TpRegion) }   # a space is %20
    if ($StartRegion) { $plugin += ' --start-region ' + [uri]::EscapeDataString($StartRegion) }
    if ($BusyRegion)  { $plugin += ' --busy-region ' + [uri]::EscapeDataString($BusyRegion) }
    if ($GpuWindow) { $plugin += " --gpu-window --frames $GpuWindowFrames" }
    if ($DryRun)   { $plugin += ' --dry-run --no-login-wait' }
    $noop = "$py $scripts/perf_check.py --noop"

    Write-Host "Run folder (captures, perf.json): $out"
    Write-Host "Scratch profile: $prof"
    $minutes = switch ($Scenario) { 'mc1_sl' { '15' } 'mc1_sl_teleports' { '10' } 'mc1_sl_teleports_manual' { '15' } 'mc1_rooftops' { '15' } 'mc1_rooftops_moving' { '6 (9 at most)' } 'mc1b_rooftops' { '30 with -GpuWindow, 25 without' } default { 'several' } }
    if ($GpuWindow -and $Scenario -ne 'mc1b_rooftops') { $minutes += ', plus up to half a minute per run for its GPU window' }
    if ($AutoLogin) { Write-Host 'Autologin as the last saved user: be logged out of Second Life elsewhere (a second login kicks the first). Leave the viewer alone until "Session done: you can quit".' }
    if (-not $DryRun -and -not $SetupLogin -and -not $AutoLogin) {
        Write-Host 'Log in by hand at home. A pop-up then asks you to teleport to the busy region; do that, then leave the viewer alone'
        Write-Host ('(no minimising, no UI use) until the pop-up "Session done: you can quit" appears (about ' + $minutes + ' minutes).')
    }
    Push-Location (Split-Path $Exe); $pushed = $true
    if ($SetupLogin) {
        Write-Host "Setup: log in by hand with 'remember me' ticked, then quit the viewer. No scenario runs. Profile: $prof"
        & $Exe
    } elseif ($AutoLogin) {
        & $Exe --autologin --leap $plugin --leap $noop
    } else {
        & $Exe --leap $plugin --leap $noop
    }
} finally {
    # the viewer keeps its scratch environment; this PowerShell window gets its own folders back
    $env:APPDATA = $saved.APPDATA; $env:LOCALAPPDATA = $saved.LOCALAPPDATA
    if ($pushed) { Pop-Location }
}

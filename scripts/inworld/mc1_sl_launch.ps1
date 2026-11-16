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
  -Scenario mc1_sl (the default) is the full session: still, moving, teleports, fps probe.
  .\mc1_sl_launch.ps1 -DryRun             (at the login screen: --dry-run --no-login-wait, do not log in)

Paths must contain no spaces (--leap is split at spaces); see README.md ("Launch").
#>
param(
    [string]$TpRegion = '',
    [string]$StartRegion = '',   # the region you log in to (the viewer does not log its name at login)
    [string]$BusyRegion = '',    # the region you teleport to when the pop-up asks
    [string]$Scenario = 'mc1_sl', # mc1_sl (captures, teleports, fps) or mc1_sl_teleports (teleports only)
    [string]$Exe = 'C:\Users\annes\source\repos\vulkanstorm\work\mc-stage1\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe',
    [switch]$GpuWindow,          # after each capture: a GPU-timed GLSubmitStats window (a -DVKS_GL_STATS=ON build)
    [int]$GpuWindowFrames = 600, # frames in that window
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

# the scenario first: a wrong name or a teleport series that cannot know its home region must fail before anything starts
$scFile = Join-Path $PSScriptRoot "scenarios\$Scenario.json"
if (-not (Test-Path $scFile)) { throw "no such scenario: $scFile" }
$scJson = Get-Content -Raw $scFile | ConvertFrom-Json
if (-not $DryRun -and $scJson.teleports -and [int]$scJson.teleports.second -gt 0 -and -not $StartRegion -and -not $TpRegion) {
    throw "scenario $Scenario teleports between two regions: give -StartRegion NAME (the region you log in to) or -TpRegion NAME, the viewer does not log the login region"
}

$repo    = 'C:\Users\annes\source\repos\vulkanstorm'
$scripts = ($PSScriptRoot -replace '\\', '/')
$tracy   = Join-Path (Split-Path $Exe | Split-Path | Split-Path) 'packages\bin\tracy-capture.exe'
foreach ($f in @($Exe, $tracy)) { if (-not (Test-Path $f)) { throw "missing: $f" } }

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
$prof = if ($DryRun) { Join-Path $repo ".fixture-tmp\mc1-dry-$stamp" } else { Join-Path $repo ".captures\mc1-profile-$stamp" }
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
    $minutes = switch ($Scenario) { 'mc1_sl' { '15' } 'mc1_sl_teleports' { '10' } 'mc1_sl_teleports_manual' { '15' } 'mc1_rooftops' { '15' } 'mc1_rooftops_moving' { '6 (9 at most)' } default { 'several' } }
    if ($GpuWindow) { $minutes += ', plus up to half a minute for each GPU window' }
    if (-not $DryRun) {
        Write-Host 'Log in by hand at home. A pop-up then asks you to teleport to the busy region; do that, then leave the viewer alone'
        Write-Host ('(no minimising, no UI use) until the pop-up "Session done: you can quit" appears (about ' + $minutes + ' minutes).')
    }
    Push-Location (Split-Path $Exe); $pushed = $true
    & $Exe --leap $plugin --leap $noop
} finally {
    # the viewer keeps its scratch environment; this PowerShell window gets its own folders back
    $env:APPDATA = $saved.APPDATA; $env:LOCALAPPDATA = $saved.LOCALAPPDATA
    if ($pushed) { Pop-Location }
}

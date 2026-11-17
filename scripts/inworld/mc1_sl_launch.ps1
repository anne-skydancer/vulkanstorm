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

Unattended runs (the viewer logs in by itself from a remembered login; these scripts never read, type, copy or log the credentials).
The profile is ONE fixed folder outside the repo, $env:LOCALAPPDATA\Vulkanstorm-autologin (local, not roaming). Its credential store
(bin_conf.dat) holds a password-equivalent: never copy, zip, sync or share that folder. Only the owner creates it (-SetupLogin).
  1. One-time setup:  .\mc1_sl_launch.ps1 -SetupLogin
     The viewer starts with the profile at the login screen; no scenario runs. Log in by hand with "remember me" ticked, go to the exact
     measuring spot in the busy region (World > Environment on Shared Environment), and quit THERE: autologin logs in at the last location.
  2. Each run:        .\mc1_sl_launch.ps1 -AutoLogin -Scenario mc1b_rooftops -StartRegion "Home" -BusyRegion "Busy" -GpuWindow
     -AutoLogin passes the viewer's --autologin (log in as the last saved user), perf_check's --auto-handoff and --max-wait-s 300, and
     needs -StartRegion (the login region) and -BusyRegion. Logged in at the busy region (case-insensitive): that position is the spot.
     Elsewhere: it teleports to -BusyPos X,Y,Z when given, else the run ends with a message to redo the setup. It quits the viewer when
     perf_check is done (CloseMainWindow, waits 120 s, never kills) and copies Vulkanstorm.log into the run folder. It refuses to start
     when the profile has no marker or no credential store (bin_conf.dat present and not empty; its content is never read).
  Be logged out of Second Life everywhere else first: a second login kicks the first.
  3. When the series ends:  .\mc1_sl_launch.ps1 -DeleteProfile
     Refuses while the viewer runs; deletes the credential files first and checks they are gone, then the folder. Leftovers are listed
     and the exit code is 2. Nothing else is started.
  VKS_AUTOLOGIN_PROFILE and VKS_AUTOLOGIN_PROCESS override the folder and the process name (tests only).

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
    [string]$BusyPos = '',       # -AutoLogin: X,Y,Z to teleport to in -BusyRegion when the login is not already there
    [switch]$AutoLogin,          # unattended: --autologin from the fixed local profile; quits the viewer afterwards
    [switch]$SetupLogin,         # one-time: the viewer with the fixed profile at the login screen, no scenario; log in, go to the spot, quit there
    [switch]$DeleteProfile,      # delete the credential files, then the fixed profile folder; starts nothing
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$repo = 'C:\Users\annes\source\repos\vulkanstorm'

if (($AutoLogin -or $SetupLogin) -and $DryRun) { throw '-AutoLogin and -SetupLogin cannot be combined with -DryRun' }
if (($AutoLogin -and $SetupLogin) -or ($DeleteProfile -and ($AutoLogin -or $SetupLogin))) { throw '-AutoLogin, -SetupLogin and -DeleteProfile exclude each other' }
if ($BusyPos -and $BusyPos -notmatch '^-?\d+(\.\d+)?,-?\d+(\.\d+)?,-?\d+(\.\d+)?$') { throw '-BusyPos takes X,Y,Z numbers without spaces, for example 128,128,30' }

# The user's real folders, read from Windows (not from $env:, which an earlier run in this window may have pointed at a
# scratch profile); every change to $env: below is undone before this script returns.
$realLocal   = [Environment]::GetFolderPath('LocalApplicationData')
$realRoaming = [Environment]::GetFolderPath('ApplicationData')
$saved = @{ APPDATA = $env:APPDATA; LOCALAPPDATA = $env:LOCALAPPDATA }

# The one autologin profile: local (not roaming), outside the repo. Its bin_conf.dat is password-equivalent.
$profFull   = if ($env:VKS_AUTOLOGIN_PROFILE) { [IO.Path]::GetFullPath($env:VKS_AUTOLOGIN_PROFILE).TrimEnd('\') } else { Join-Path $realLocal 'Vulkanstorm-autologin' }
$marker     = Join-Path $profFull '.vulkanstorm-autologin-profile'
$viewerProc = if ($env:VKS_AUTOLOGIN_PROCESS) { $env:VKS_AUTOLOGIN_PROCESS } else { 'vulkanstorm-bin' }
$credFile   = Join-Path $profFull 'Roaming\Vulkanstorm_x64\user_settings\bin_conf.dat'   # the viewer's credential store (llsechandler_basic.cpp)
$credNames  = @('bin_conf.dat', 'password.dat')                                           # and its legacy password file (llappviewer.cpp)

function Test-ReparseTree([string]$dir) {   # the folder itself or anything in it is a junction or symlink
    if ((Get-Item -LiteralPath $dir -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { return $true }
    return [bool](Get-ChildItem -LiteralPath $dir -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue | Select-Object -First 1)
}
function Assert-OurProfile {
    if (-not (Test-Path -LiteralPath $profFull)) { throw "no autologin profile at ${profFull}: run .\mc1_sl_launch.ps1 -SetupLogin first" }
    if (-not (Test-Path -LiteralPath $marker)) { throw "$profFull has no .vulkanstorm-autologin-profile marker: it was not made by -SetupLogin, refusing to use or delete it" }
    if (Test-ReparseTree $profFull) { throw "$profFull is or contains a junction or symlink: refusing" }
}

if ($DeleteProfile) {
    Assert-OurProfile
    if (Get-Process -Name $viewerProc -ErrorAction SilentlyContinue) { throw "$viewerProc is running: quit the viewer first, nothing deleted" }
    # the credential files first, checked on their own, so a locked cache file cannot keep them
    foreach ($f in @(Get-ChildItem -LiteralPath $profFull -Recurse -Force -File -ErrorAction SilentlyContinue | Where-Object { $credNames -contains $_.Name })) {
        Remove-Item -LiteralPath $f.FullName -Force -ErrorAction SilentlyContinue
    }
    $creds = @(Get-ChildItem -LiteralPath $profFull -Recurse -Force -File -ErrorAction SilentlyContinue | Where-Object { $credNames -contains $_.Name })
    Remove-Item -LiteralPath $profFull -Recurse -Force -ErrorAction SilentlyContinue
    $left = @(if (Test-Path -LiteralPath $profFull) { Get-ChildItem -LiteralPath $profFull -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName } })
    if ($creds.Count -or $left.Count -or (Test-Path -LiteralPath $profFull)) {
        foreach ($c in $creds) { Write-Host "CREDENTIAL FILE STILL PRESENT: $($c.FullName)" -ForegroundColor Red }
        foreach ($l in $left) { Write-Host "LEFTOVER: $l" -ForegroundColor Red }
        Write-Host "Profile $profFull was NOT fully deleted: close what holds those files and run -DeleteProfile again." -ForegroundColor Red
        exit 2
    }
    Write-Host "Deleted the autologin profile $profFull (credential files first, then the folder)."
    return
}
if ($SetupLogin) {
    if (Test-Path -LiteralPath $profFull) {
        if (Test-ReparseTree $profFull) { throw "$profFull is or contains a junction or symlink: refusing" }
        if (-not (Test-Path -LiteralPath $marker) -and (Get-ChildItem -LiteralPath $profFull -Force | Select-Object -First 1)) { throw "$profFull exists, is not empty and has no marker: not ours, refusing" }
    }
    New-Item -ItemType Directory -Force (Join-Path $profFull 'Roaming'), (Join-Path $profFull 'Local') | Out-Null
    Set-Content -LiteralPath $marker -Value 'Vulkanstorm autologin profile (mc1_sl_launch.ps1 -SetupLogin). Holds a remembered login: never copy, zip or sync this folder. Delete with -DeleteProfile.'
}
if ($AutoLogin) {
    Assert-OurProfile
    if (-not (Test-Path -LiteralPath $credFile) -or (Get-Item -LiteralPath $credFile).Length -le 0) {
        throw "the autologin profile has no credential store yet ($credFile missing or empty). One-time setup: .\mc1_sl_launch.ps1 -SetupLogin (log in by hand with 'remember me' ticked, go to the measuring spot, quit there), then run again with -AutoLogin."
    }
}

# the scenario first: a wrong name or a teleport series that cannot know its home region must fail before anything starts
$scFile = Join-Path $PSScriptRoot "scenarios\$Scenario.json"
if (-not (Test-Path $scFile)) { throw "no such scenario: $scFile" }
$scJson = Get-Content -Raw $scFile | ConvertFrom-Json
if ($AutoLogin -and $scJson.owner_teleport -and -not ($BusyRegion -and $StartRegion)) { throw '-AutoLogin needs -StartRegion NAME (the login region) and -BusyRegion NAME: nobody is there to click the hand-off' }
if (-not $DryRun -and -not $SetupLogin -and $scJson.teleports -and [int]$scJson.teleports.second -gt 0 -and -not $StartRegion -and -not $TpRegion) {
    throw "scenario $Scenario teleports between two regions: give -StartRegion NAME (the region you log in to) or -TpRegion NAME, the viewer does not log the login region"
}

$scripts = ($PSScriptRoot -replace '\\', '/')
$tracy   = Join-Path (Split-Path $Exe | Split-Path | Split-Path) 'packages\bin\tracy-capture.exe'
foreach ($f in $(if ($SetupLogin) { @($Exe) } else { @($Exe, $tracy) })) { if (-not (Test-Path $f)) { throw "missing: $f" } }

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
$prof = if ($AutoLogin -or $SetupLogin) { $profFull } elseif ($DryRun) { Join-Path $repo ".fixture-tmp\mc1-dry-$stamp" } else { Join-Path $repo ".captures\mc1-profile-$stamp" }
New-Item -ItemType Directory -Force "$prof\Roaming", "$prof\Local" | Out-Null
$env:APPDATA = "$prof\Roaming"
$env:LOCALAPPDATA = "$prof\Local"
$pushed = $false
$rc = 0
try {   # from here every path puts this window's folders back
    $out   = (Join-Path $repo ".captures\mc1-sl-$stamp") -replace '\\', '/'
    $plugin = "$py $scripts/perf_check.py --scenario $scripts/scenarios/$Scenario.json --out $out --no-glstats --tracy-capture " + ($tracy -replace '\\', '/')
    if ($TpRegion) { $plugin += ' --tp-region ' + [uri]::EscapeDataString($TpRegion) }   # a space is %20
    if ($StartRegion) { $plugin += ' --start-region ' + [uri]::EscapeDataString($StartRegion) }
    if ($BusyRegion)  { $plugin += ' --busy-region ' + [uri]::EscapeDataString($BusyRegion) }
    if ($GpuWindow) { $plugin += " --gpu-window --frames $GpuWindowFrames" }
    if ($AutoLogin) {   # nobody clicks: the login is the measuring spot (or --busy-pos); a login that does not finish ends the run
        $plugin += ' --auto-handoff --max-wait-s 300'
        if ($BusyPos) { $plugin += ' --busy-pos ' + $BusyPos }
    }
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
        Write-Host "Setup: log in by hand with 'remember me' ticked, go to the exact measuring spot in the busy region (World > Environment on Shared Environment) and quit THERE. No scenario runs. Profile: $prof"
        & $Exe
    } elseif ($AutoLogin) {
        $vp = Start-Process -FilePath $Exe -ArgumentList ('--autologin --leap "' + $plugin + '" --leap "' + $noop + '"') -WorkingDirectory (Split-Path $Exe) -PassThru
        # wait for this run's perf_check (it ends on success, failure or a login timeout), then quit the viewer the polite way
        $seen = $false; $t0 = Get-Date
        while (-not $vp.HasExited) {
            Start-Sleep -Seconds 2
            $pc = Get-CimInstance Win32_Process -Filter "Name like 'python%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -like '*perf_check.py*' -and $_.CommandLine -like "*$out*" }
            if ($pc) { $seen = $true } elseif ($seen) { break }
            elseif (((Get-Date) - $t0).TotalSeconds -gt 240) { Write-Host 'perf_check never started (the viewer did not run its plugin); quitting the viewer.' -ForegroundColor Red; $rc = 1; break }
        }
        if (-not $vp.HasExited) {
            $vp.Refresh()
            [void]$vp.CloseMainWindow()
            if (-not $vp.WaitForExit(120000)) {
                Write-Host "THE VIEWER DID NOT EXIT within 120 s of the quit request (pid $($vp.Id)). It is left running: quit it by hand; nothing was killed." -ForegroundColor Red
                $rc = 3
            }
        }
        # only the viewer's own log is saved with the run (an allow-list; no chat or conversation logs)
        $vlogFile = Join-Path $prof 'Roaming\Vulkanstorm_x64\logs\Vulkanstorm.log'
        if (Test-Path -LiteralPath $vlogFile) {
            New-Item -ItemType Directory -Force $out | Out-Null
            Copy-Item -LiteralPath $vlogFile -Destination (Join-Path $out 'Vulkanstorm.log') -Force
        }
    } else {
        & $Exe --leap $plugin --leap $noop
    }
} finally {
    # the viewer keeps its scratch environment; this PowerShell window gets its own folders back
    $env:APPDATA = $saved.APPDATA; $env:LOCALAPPDATA = $saved.LOCALAPPDATA
    if ($pushed) { Pop-Location }
}
if ($rc) { exit $rc }

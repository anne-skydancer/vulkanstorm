# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
<#
Starts the multicore stage 1 measuring viewer for the Second Life session (doc/perf/multicore-baseline-2026-11.md):
a scratch profile (.captures\mc1-profile, no saved credentials), the perf_check.py LEAP plugin with scenarios\mc1_sl.json
and --tracy-capture, the throw-away noop plugin entry, and the run folder .captures\mc1-sl-<stamp>.

  .\mc1_sl_launch.ps1                     (the session: log in by hand at home; a pop-up asks you to teleport)
  .\mc1_sl_launch.ps1 -TpRegion "Name"    (only to override the second region of the teleports: default is the login region)
  .\mc1_sl_launch.ps1 -DryRun             (at the login screen: --dry-run --no-login-wait, do not log in)

Paths must contain no spaces (--leap is split at spaces); see README.md ("Launch").
#>
param(
    [string]$TpRegion = '',
    [string]$Exe = 'C:\Users\annes\source\repos\vulkanstorm\work\mc-stage1\build-vc180-64\newview\RelWithDebInfo\vulkanstorm-bin.exe',
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$repo    = 'C:\Users\annes\source\repos\vulkanstorm'
$scripts = ($PSScriptRoot -replace '\\', '/')
$tracy   = Join-Path (Split-Path $Exe | Split-Path | Split-Path) 'packages\bin\tracy-capture.exe'
foreach ($f in @($Exe, $tracy)) { if (-not (Test-Path $f)) { throw "missing: $f" } }

# the real interpreter, not the Microsoft Store alias
$py = (& py -c "import sys;print(sys.executable)" 2>$null)
if (-not $py) { $py = 'C:/Users/annes/AppData/Local/Python/pythoncore-3.15-64/python.exe' }
$py = ($py -replace '\\', '/')

# scratch profile: no saved credentials, the viewer cannot log in by itself
$prof = Join-Path $repo '.captures\mc1-profile'
New-Item -ItemType Directory -Force "$prof\Roaming", "$prof\Local" | Out-Null
$env:APPDATA = "$prof\Roaming"
$env:LOCALAPPDATA = "$prof\Local"

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out   = (Join-Path $repo ".captures\mc1-sl-$stamp") -replace '\\', '/'
$plugin = "$py $scripts/perf_check.py --scenario $scripts/scenarios/mc1_sl.json --out $out --no-glstats --tracy-capture " + ($tracy -replace '\\', '/')
if ($TpRegion) { $plugin += ' --tp-region ' + [uri]::EscapeDataString($TpRegion) }   # a space is %20
if ($DryRun)   { $plugin += ' --dry-run --no-login-wait' }
$noop = "$py $scripts/perf_check.py --noop"

Write-Host "Run folder (captures, perf.json): $out"
Write-Host "Scratch profile: $prof"
if (-not $DryRun) {
    Write-Host 'Log in by hand at home. A pop-up then asks you to teleport to the busy region; do that, then leave the viewer alone'
    Write-Host '(no minimising, no UI use) until the pop-up "Session done: you can quit" appears (about 15 minutes).'
}
Set-Location (Split-Path $Exe)
& $Exe --leap $plugin --leap $noop

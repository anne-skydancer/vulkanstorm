# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
<#
MEASURING BUILD ONLY (draft PR "Measuring build (never merge): impostor PBR proof"; nothing of it goes to release).

Runs the downloaded CI artifact (windows-qualification-viewer) for the impostor proof: do impostored avatars get their
real PBR materials and a single blend, before and after the fix?

  .\impostor_proof_launch.ps1 -Artifact C:\path\to\the\unzipped\artifact
  .\impostor_proof_launch.ps1 -Exe C:\path\to\vulkanstorm-bin.exe
  .\impostor_proof_launch.ps1 -Artifact ... -Mode TwoSessions     (before and after as two separate logins)

What it does
  - A fresh scratch profile for each session (APPDATA and LOCALAPPDATA point into .captures\impostor-proof-<stamp>), no flags
    except the viewer's own --set for three settings: RenderAvatarMaxNonImpostors = 1 (almost everybody becomes an impostor),
    RenderImpostorProofLog = 1 (the logging), RenderImpostorFullPBR = 0 (before) or 1 (after).
  - You log in BY HAND at a crowded place. There is no autologin. Do NOT tick "Remember me": the scratch profile is deleted
    when the viewer closes (the proof logs and PNGs are copied out first), so no credentials stay behind either way.
  - Be logged out of Second Life everywhere else first: a second login kicks the first.

Mode Single (the default): ONE session. It starts with RenderImpostorFullPBR = FALSE (before the fix).
  1. Log in and walk to a crowded place. Stand still (or turn slowly) for 2 minutes. Impostors regenerate as people move.
  2. Open Advanced > Debug Settings, search RenderImpostorFullPBR, set it to TRUE. Every impostor regenerates at once.
     (The Advanced menu hidden? Ctrl+Alt+D shows it; Ctrl+Alt+Shift+S opens Debug Settings directly.)
  3. Stand 2 more minutes. Then quit the viewer normally (File > Quit).
  The log tags every generation with the value in force (before or after), so one session is enough.
Mode TwoSessions: the same two steps as two logins, with the setting fixed to FALSE and then to TRUE (it asks you to press
  Enter before the second). Use it when the in-viewer toggle does not work for you.

Afterwards it prints the evidence folder and the command for the report:
  python scripts\inworld\impostor_proof_report.py <evidence folder>
#>
param(
    [string]$Artifact = '',     # the unzipped windows-qualification-viewer artifact (searched for the viewer exe)
    [string]$Exe = '',          # or the viewer exe itself
    [ValidateSet('Single', 'TwoSessions')][string]$Mode = 'Single',
    [int]$Minutes = 2,          # only the text on screen
    [string]$Root = ''          # evidence root; default <repo>\.captures\impostor-proof-<stamp>
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Exe) {
    if (-not $Artifact) { throw 'Give -Artifact <the unzipped artifact folder> or -Exe <viewer exe>' }
    if (-not (Test-Path -LiteralPath $Artifact)) { throw "No such folder: $Artifact" }
    $found = @(Get-ChildItem -LiteralPath $Artifact -Recurse -Filter '*-bin.exe' -File)
    if ($found.Count -eq 0) { throw "No *-bin.exe under $Artifact" }
    # the staged viewer (packaged) runs directly; otherwise the first one
    $staged = @($found | Where-Object { $_.FullName -match '\\packaged\\' })
    $Exe = if ($staged.Count -gt 0) { $staged[0].FullName } else { $found[0].FullName }
}
if (-not (Test-Path -LiteralPath $Exe)) { throw "No such viewer: $Exe" }
Write-Host "Viewer: $Exe"

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $Root) { $Root = Join-Path $repo ".captures\impostor-proof-$stamp" }
New-Item -ItemType Directory -Force -Path $Root | Out-Null

# the user's real folders, restored at the end
$saved = @{ APPDATA = $env:APPDATA; LOCALAPPDATA = $env:LOCALAPPDATA }

function Invoke-ProofSession {
    param([string]$Name, [bool]$FullPBR)
    $prof = Join-Path $Root "profile-$Name"
    New-Item -ItemType Directory -Force -Path "$prof\Roaming", "$prof\Local" | Out-Null
    $env:APPDATA = "$prof\Roaming"
    $env:LOCALAPPDATA = "$prof\Local"
    $flag = if ($FullPBR) { 1 } else { 0 }
    $viewerArgs = "--set RenderAvatarMaxNonImpostors 1 --set RenderImpostorProofLog 1 --set RenderImpostorFullPBR $flag"
    Write-Host ''
    Write-Host "=== Session '$Name': RenderImpostorFullPBR starts as $(if ($FullPBR) { 'TRUE (after the fix)' } else { 'FALSE (before the fix)' }) ==="
    if ($Mode -eq 'Single') {
        Write-Host "1. Log in by hand (do not tick Remember me) and go to a crowded place."
        Write-Host "2. Stand still $Minutes minutes."
        Write-Host "3. Advanced > Debug Settings: set RenderImpostorFullPBR to TRUE. Stand $Minutes more minutes."
        Write-Host "4. Quit the viewer normally (File > Quit)."
    } else {
        Write-Host "1. Log in by hand (do not tick Remember me) and go to the same crowded place."
        Write-Host "2. Stand still $Minutes minutes."
        Write-Host "3. Quit the viewer normally (File > Quit)."
    }
    $p = Start-Process -FilePath $Exe -ArgumentList $viewerArgs -WorkingDirectory (Split-Path $Exe) -PassThru
    $p.WaitForExit()
    # if the exe respawned itself, wait for every process of that name too (the profile is deleted below)
    $exeName = [IO.Path]::GetFileNameWithoutExtension($Exe)
    while (Get-Process -Name $exeName -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
    $env:APPDATA = $saved.APPDATA
    $env:LOCALAPPDATA = $saved.LOCALAPPDATA

    # copy the proof out, then delete the scratch profile (it may hold a remembered login)
    $dest = Join-Path $Root "session-$Name"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $proof = @(Get-ChildItem -LiteralPath $prof -Recurse -Directory -Filter 'impostor_proof' -ErrorAction SilentlyContinue)
    foreach ($d in $proof) { Copy-Item -LiteralPath $d.FullName -Destination $dest -Recurse -Force }
    # only the proof lines of the viewer log: the full log names the account
    foreach ($log in @(Get-ChildItem -LiteralPath $prof -Recurse -File -Filter '*.log' -ErrorAction SilentlyContinue)) {
        $lines = @(Select-String -LiteralPath $log.FullName -Pattern 'ImpostorProof' | ForEach-Object { $_.Line })
        if ($lines.Count) { Set-Content -LiteralPath (Join-Path $dest ('proof-' + $log.Name)) -Value $lines }
    }
    Remove-Item -LiteralPath $prof -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $prof) { Write-Warning "The scratch profile could not be deleted: $prof (it may hold a remembered login: delete it by hand)" }
    if ($proof.Count -eq 0) { Write-Warning "Session '$Name' wrote no impostor_proof folder: no impostor was generated, or the build lacks the logging." }
}

try {
    if ($Mode -eq 'Single') {
        Invoke-ProofSession -Name 'single' -FullPBR $false
    } else {
        Invoke-ProofSession -Name 'before' -FullPBR $false
        Read-Host 'Press Enter to start the second session (after the fix)' | Out-Null
        Invoke-ProofSession -Name 'after' -FullPBR $true
    }
} finally {
    $env:APPDATA = $saved.APPDATA
    $env:LOCALAPPDATA = $saved.LOCALAPPDATA
}

Write-Host ''
Write-Host "Evidence: $Root"
Write-Host "Report:   python `"$repo\scripts\inworld\impostor_proof_report.py`" `"$Root`""

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
#
# CDN texture fetch through llcorehttp (http_texture_load): wall time, bytes,
# errors, and the most TCP connections the process held at once.
# Bounded: each run is killed after 120 s.
param(
    [Parameter(Mandatory)][string]$Tool,
    [Parameter(Mandatory)][string]$Uuids,
    [string]$Label = 'run',
    [int]$Runs = 5,
    [string[]]$Configs = @('-c 8 -p 5', '-c 8 -p 0')
)
$ErrorActionPreference = 'Stop'
foreach ($cfg in $Configs) {
    for ($i = 1; $i -le $Runs; $i++) {
        $outF = [System.IO.Path]::GetTempFileName()
        # Start-Process joins the list into one command line: quote the path
        $args = @('-w') + ($cfg -split ' ') + @('-u', 'http://asset-cdn.glb.agni.lindenlab.com/?texture_id=%s', ('"{0}"' -f $Uuids))
        $p = Start-Process -FilePath $Tool -ArgumentList $args -RedirectStandardOutput $outF -RedirectStandardError "$outF.err" -PassThru -NoNewWindow
        $max = 0; $t0 = Get-Date
        while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt 120) {
            $n = @(Get-NetTCPConnection -OwningProcess $p.Id -State Established -ErrorAction SilentlyContinue | Where-Object RemotePort -eq 80).Count
            if ($n -gt $max) { $max = $n }
            Start-Sleep -Milliseconds 50
        }
        if (-not $p.HasExited) { $p.Kill(); 'killed after 120 s' }
        $txt = (Get-Content $outF -Raw) + (Get-Content "$outF.err" -Raw)
        $succ = if ($txt -match 'Successes:\s+(\d+)') { $Matches[1] } else { '?' }
        $bytes = if ($txt -match 'Byte count:\s+(\d+)') { $Matches[1] } else { '?' }
        $errs = if ($txt -match 'HTTP errors:\s+(\d+)') { $Matches[1] } else { '?' }
        $wall = if ($txt -match 'Wall Time:\s+(\d+)') { [math]::Round([double]$Matches[1] / 1e6, 2) } else { '?' }
        '{0} [{1}] run {2}: {3} ok, {4} bytes, {5} HTTP errors, {6} s, max {7} connections' -f $Label, $cfg, $i, $succ, $bytes, $errs, $wall, $max
        Remove-Item $outF, "$outF.err" -ErrorAction SilentlyContinue
    }
}

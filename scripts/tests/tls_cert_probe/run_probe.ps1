# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
#
# TLS certificate probe: how the viewer's login transport reports certificate
# failures, without logging in. It generates a throw-away PKI, serves five
# local HTTPS endpoints (valid, expired, untrusted CA, wrong host,
# self-signed), starts the staged viewer with an empty scratch profile whose
# CA.pem trusts the test CA, and lets a LEAP plugin send one XML-RPC request
# (no credentials) to each endpoint through the LLXMLRPCTransaction pump.
# The result JSON names the branch llstartup takes for each reply.
#
#   pwsh -File run_probe.ps1 -Exe <staged viewer exe> -OpenSsl <openssl.exe> -WorkDir <empty dir>
#
# Bounded: servers exit after 200 s, the viewer is closed after the result
# (or 180 s) and killed 30 s later. The real profile is never opened; the
# SHA-256 of every bin_conf.dat under %APPDATA% is compared before and after.
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$OpenSsl,
    [Parameter(Mandatory)][string]$WorkDir,
    [string]$Python = (Get-Command python -ErrorAction Stop).Source
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$pki = Join-Path $WorkDir 'pki'
$profileDir = Join-Path $WorkDir 'profile'
$out = Join-Path $WorkDir 'result.json'
$sess = Join-Path $WorkDir 'probe_session.xml'

# --- throw-away PKI (OpenSSL 3.4+ for -not_before/-not_after) ---
Remove-Item -Recurse -Force $pki -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $pki | Out-Null
Set-Content (Join-Path $pki 'min.cnf') "[req]`ndistinguished_name=dn`n[dn]`n"
$env:OPENSSL_CONF = Join-Path $pki 'min.cnf'
function Ossl { & $OpenSsl @args 2>$null; if ($LASTEXITCODE) { throw "openssl $($args[0]) failed" } }
Push-Location $pki
try {
    'basicConstraints=critical,CA:FALSE', 'keyUsage=critical,digitalSignature,keyEncipherment',
    'extendedKeyUsage=serverAuth', 'subjectKeyIdentifier=hash', 'authorityKeyIdentifier=keyid' |
        Set-Content leaf-common.ext
    (Get-Content leaf-common.ext) + 'subjectAltName=DNS:localhost' | Set-Content leaf.ext
    (Get-Content leaf-common.ext) + 'subjectAltName=DNS:not-localhost.invalid' | Set-Content leaf-wronghost.ext
    foreach ($ca in 'trusted-ca', 'untrusted-ca') {
        Ossl req -x509 -newkey rsa:2048 -nodes -keyout "$ca.key" -out "$ca.crt" -days 30 -subj "/CN=VKS TLS probe $ca" `
            -addext 'basicConstraints=critical,CA:TRUE' -addext 'keyUsage=critical,keyCertSign,cRLSign'
    }
    $leaves = @(
        @('valid', 'trusted-ca', 'leaf.ext', @('-days', '30')),
        @('expired', 'trusted-ca', 'leaf.ext', @('-not_before', '20240101000000Z', '-not_after', '20250101000000Z')),
        @('untrusted', 'untrusted-ca', 'leaf.ext', @('-days', '30')),
        @('wronghost', 'trusted-ca', 'leaf-wronghost.ext', @('-days', '30')))
    foreach ($l in $leaves) {
        $n = $l[0]; $ca = $l[1]
        Ossl req -newkey rsa:2048 -nodes -keyout "$n.key" -out "$n.csr" -subj '/CN=localhost'
        Ossl x509 -req -in "$n.csr" -CA "$ca.crt" -CAkey "$ca.key" -CAcreateserial -out "$n.crt" -extfile $l[2] @($l[3])
        (Get-Content "$n.crt") + (Get-Content "$ca.crt") | Set-Content "$n-chain.pem"
    }
    Ossl req -x509 -newkey rsa:2048 -nodes -keyout selfsigned.key -out selfsigned.crt -days 30 -subj '/CN=localhost' `
        -addext 'subjectAltName=DNS:localhost' -addext 'extendedKeyUsage=serverAuth'
    Copy-Item selfsigned.crt selfsigned-chain.pem
} finally { Pop-Location; Remove-Item Env:OPENSSL_CONF }

# --- scratch profile: empty, trusting only the test CA beyond the bundle ---
$stores = Get-ChildItem $env:APPDATA -Recurse -Filter bin_conf.dat -Depth 3 -ErrorAction SilentlyContinue
$before = $stores | ForEach-Object { "$($_.FullName) $((Get-FileHash $_.FullName).Hash)" }
Remove-Item -Recurse -Force $profileDir -ErrorAction SilentlyContinue
$us = Join-Path $profileDir 'Roaming\Vulkanstorm_x64\user_settings'
New-Item -ItemType Directory -Force $us, (Join-Path $profileDir 'Local') | Out-Null
Copy-Item (Join-Path $pki 'trusted-ca.crt') (Join-Path $us 'CA.pem')
Remove-Item $out -ErrorAction SilentlyContinue

# --- LEAP plugin through a session settings file (the command line would
# need LLSD notation quoting); the viewer joins the name to app_settings ---
$fwd = { param($p) $p.Replace([char]92, [char]47) }
$leap = "$(& $fwd $Python) -I $(& $fwd (Join-Path $here 'xmlrpc_cert_probe.py')) $(& $fwd $out)"
@"
<?xml version="1.0" ?>
<llsd><map><key>LeapCommand</key><map>
<key>Comment</key><string>TLS certificate probe</string><key>Persist</key><integer>0</integer>
<key>Type</key><string>LLSD</string><key>Value</key><array><string>$leap</string></array>
</map></map></llsd>
"@ | Set-Content -Encoding utf8NoBOM $sess
$sessArg = [System.IO.Path]::GetRelativePath((Join-Path (Split-Path $Exe) 'app_settings'), $sess)

$srv = Start-Process -FilePath $Python -ArgumentList '-I', (Join-Path $here 'tls_servers.py'), $pki, '200' `
    -RedirectStandardError (Join-Path $WorkDir 'servers.log') -RedirectStandardOutput (Join-Path $WorkDir 'servers.out') `
    -PassThru -NoNewWindow
Start-Sleep -Seconds 2
try {
    $psi = [System.Diagnostics.ProcessStartInfo]::new($Exe, "--sessionsettings `"$sessArg`"")
    $psi.WorkingDirectory = Split-Path $Exe
    $psi.UseShellExecute = $false
    $psi.Environment['APPDATA'] = Join-Path $profileDir 'Roaming'
    $psi.Environment['LOCALAPPDATA'] = Join-Path $profileDir 'Local'
    $t0 = Get-Date
    $viewer = [System.Diagnostics.Process]::Start($psi)
    while (-not (Test-Path $out) -and ((Get-Date) - $t0).TotalSeconds -lt 180 -and -not $viewer.HasExited) { Start-Sleep 2 }
    Start-Sleep 3
    if (-not $viewer.HasExited) { $null = $viewer.CloseMainWindow(); if (-not $viewer.WaitForExit(30000)) { $viewer.Kill($true) } }
} finally {
    if (-not $srv.HasExited) { Stop-Process -Id $srv.Id -Force }
}
$after = $stores | ForEach-Object { "$($_.FullName) $((Get-FileHash $_.FullName).Hash)" }
"real credential stores unchanged: $($null -eq (Compare-Object @($before) @($after))) ($(@($before).Count) checked)"
"viewer log: $(Join-Path $profileDir 'Roaming\Vulkanstorm_x64\logs\Vulkanstorm.log')"
if (Test-Path $out) {
    (Get-Content $out | ConvertFrom-Json).PSObject.Properties | ForEach-Object {
        '{0,-10} {1,-9} {2,-21} cert={3,-5} {4,-12} -> {5}' -f $_.Name, $_.Value.status, $_.Value.errorcode,
            $_.Value.has_certificate, $_.Value.validation_failure, $_.Value.llstartup }
} else { 'no result: see the viewer log'; exit 1 }

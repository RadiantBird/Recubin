# AvatarBatch NaN injection check: starts a Host (with injection) and a Client on this PC.
# Usage: powershell -ExecutionPolicy Bypass -File TestCases\NetworkTest\run_avatar_batch_nan.ps1 [-Port 27015] [-HostOnly] [-ClientOnly] [-HostAddress 127.0.0.1] [-NoInject]
# Run 'python build.py build' first (build.py syncs RecubinEngine.exe into this folder).
# Keep this file ASCII-only: Windows PowerShell 5.1 misreads BOM-less UTF-8.
param(
    [int]$Port = 27015,
    [string]$HostAddress = "127.0.0.1",
    [switch]$HostOnly,
    [switch]$ClientOnly,
    [switch]$NoInject  # baseline run without NaN injection (for comparing other logs)
)

$dir = $PSScriptRoot
$exe = Join-Path $dir "RecubinEngine.exe"
$scene = "assets/scenes/AvatarBatchNaN.yaml"

if (-not (Test-Path $exe)) {
    Write-Error "RecubinEngine.exe not found in $dir. Run 'python build.py build' first."
    exit 1
}
if (-not (Test-Path (Join-Path $dir $scene))) {
    Write-Error "Scene not found: $scene"
    exit 1
}

if (-not $ClientOnly) {
    $hostArgs = @("--direct-host", "$Port")
    if (-not $NoInject) { $hostArgs += "--debug-inject-nan-avatar-batch" }
    $hostArgs += @("--scene", $scene, "--window-title", "AvatarBatchNaN-Host")
    Write-Host "[run] Host   : $($hostArgs -join ' ')"
    Start-Process -FilePath $exe -WorkingDirectory $dir -ArgumentList $hostArgs
    if (-not $HostOnly) { Start-Sleep -Seconds 3 }
}
if (-not $HostOnly) {
    Write-Host "[run] Client : --direct-connect ${HostAddress}:$Port"
    Start-Process -FilePath $exe -WorkingDirectory $dir -ArgumentList @(
        "--direct-connect", "${HostAddress}:$Port",
        "--scene", $scene,
        "--window-title", "AvatarBatchNaN-Client")
}
Write-Host "[run] Watch each console window. See AvatarBatchNaN_README.txt for the expected log lines."

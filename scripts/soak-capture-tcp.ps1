# TCP capture soak: edge --transport tcp → capture --mode tcp → inspect gaps=0.
param(
    [int]$Seconds = 5,
    [int]$Rate = 400
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Edge = Join-Path $Bin "zcmesh_edge.exe"
$Cap = Join-Path $Bin "zcmesh_capture.exe"
$Inspect = Join-Path $Bin "zcmesh_inspect.exe"
$Zcm = Join-Path $Root "tcp-cap-soak.zcm"

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$capErr = Join-Path $Root "tcp-cap-err.txt"
$edgeErr = Join-Path $Root "tcp-cap-edge-err.txt"
Remove-Item $Zcm, $capErr, $edgeErr -ErrorAction SilentlyContinue

Write-Host "Capture TCP :9950 for ${Seconds}s"
$cap = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9950", "--out", $Zcm, "--seconds", "$Seconds", "--mode", "tcp") `
    -RedirectStandardError $capErr -PassThru
Start-Sleep -Seconds 1

Write-Host "Edge TCP -> :9950 rate=$Rate"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9950", "--transport", "tcp",
                    "--rate", "$Rate", "--batch", "16", "--duration", "$($Seconds - 1)") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $cap.Id -Timeout ($Seconds + 8) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $cap)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

if (-not (Test-Path $Zcm) -or (Get-Item $Zcm).Length -lt 40) {
    Write-Host "TCP_CAP_FAIL: no capture"
    Get-Content $capErr, $edgeErr -ErrorAction SilentlyContinue
    exit 1
}

& $Inspect $Zcm --expect-gaps-max 0 --expect-last-hop-min-pct 99
if ($LASTEXITCODE -ne 0) {
    Write-Host "TCP_CAP_FAIL: inspect"
    exit 1
}
Write-Host "TCP_CAP_OK"
exit 0

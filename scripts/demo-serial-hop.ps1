# Serial 2-hop mesh: edge → hop0 → hop1(--final) → capture → inspect hop stamps.
param(
    [int]$Seconds = 6,
    [int]$Rate = 400
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Edge = Join-Path $Bin "zcmesh_edge.exe"
$Hop = Join-Path $Bin "zcmesh_hop.exe"
$Cap = Join-Path $Bin "zcmesh_capture.exe"
$Inspect = Join-Path $Bin "zcmesh_inspect.exe"
$Zcm = Join-Path $Root "serial-hop.zcm"

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$capErr = Join-Path $Root "serial-cap-err.txt"
$hop0Err = Join-Path $Root "serial-hop0-err.txt"
$hop1Err = Join-Path $Root "serial-hop1-err.txt"
$edgeErr = Join-Path $Root "serial-edge-err.txt"
Remove-Item $Zcm, $capErr, $hop0Err, $hop1Err, $edgeErr -ErrorAction SilentlyContinue

Write-Host "Capture :9940 for ${Seconds}s"
$cap = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9940", "--out", $Zcm, "--seconds", "$Seconds", "--mode", "udp") `
    -RedirectStandardError $capErr -PassThru
Start-Sleep -Seconds 1

Write-Host "Hop1 (final) :9939 -> :9940"
$hop1 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9939", "--forward", "127.0.0.1:9940", "--final") `
    -RedirectStandardError $hop1Err -PassThru

Write-Host "Hop0 :9938 -> :9939"
$hop0 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9938", "--forward", "127.0.0.1:9939") `
    -RedirectStandardError $hop0Err -PassThru

Write-Host "Edge mesh single hop :9938 rate=$Rate"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9940", "--transport", "mesh",
                    "--hop", "127.0.0.1:9938", "--rate", "$Rate", "--batch", "8",
                    "--duration", "$($Seconds - 1)") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $cap.Id -Timeout ($Seconds + 8) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $hop0, $hop1, $cap)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

if (-not (Test-Path $Zcm) -or (Get-Item $Zcm).Length -lt 40) {
    Write-Host "SERIAL_HOP_FAIL: no capture"
    Get-Content $capErr, $edgeErr -ErrorAction SilentlyContinue
    exit 1
}

Write-Host "Inspect: hop_idx=2 exclusive, last_hop >= 99%, gaps=0"
& $Inspect $Zcm --expect-gaps-max 0 --expect-hop-idx 2 --expect-last-hop-min-pct 99
if ($LASTEXITCODE -ne 0) {
    Write-Host "SERIAL_HOP_FAIL: inspect"
    Get-Content $hop0Err, $hop1Err, $edgeErr -ErrorAction SilentlyContinue | Select-Object -Last 20
    exit 1
}
Write-Host "SERIAL_HOP_OK"
exit 0

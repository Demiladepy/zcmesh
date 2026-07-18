# C++-only TCP→mesh failover: capture is UDP-only (no TCP), hop --final, edge auto.
# Asserts mesh_failover>0 and frames landed in .zcm. No Java.
param(
    [int]$Seconds = 5,
    [int]$Rate = 400
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Edge = Join-Path $Bin "zcmesh_edge.exe"
$Hop = Join-Path $Bin "zcmesh_hop.exe"
$Cap = Join-Path $Bin "zcmesh_capture.exe"
$Zcm = Join-Path $Root "failover-cpp.zcm"

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$capErr = Join-Path $Root "failover-cpp-cap-err.txt"
$hopErr = Join-Path $Root "failover-cpp-hop-err.txt"
$edgeErr = Join-Path $Root "failover-cpp-edge-err.txt"
Remove-Item $Zcm, $capErr, $hopErr, $edgeErr -ErrorAction SilentlyContinue

Write-Host "Capture UDP-only on :9920 (no TCP => forces auto mesh failover)"
$cap = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9920", "--out", $Zcm, "--seconds", "$Seconds", "--mode", "udp") `
    -RedirectStandardError $capErr -PassThru
Start-Sleep -Seconds 1

Write-Host "Hop 9919 -> 9920 --final"
$hop = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9919", "--forward", "127.0.0.1:9920", "--final") `
    -RedirectStandardError $hopErr -PassThru

Write-Host "Edge transport=auto --hop 9919"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9920", "--transport", "auto",
                    "--hop", "127.0.0.1:9919", "--rate", "$Rate", "--batch", "8",
                    "--duration", "$($Seconds - 1)", "--print-stats-sec", "1") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $cap.Id -Timeout ($Seconds + 8) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $hop, $cap)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
Start-Sleep -Milliseconds 300

$elog = Get-Content $edgeErr -Raw -ErrorAction SilentlyContinue
$clog = Get-Content $capErr -Raw -ErrorAction SilentlyContinue
Write-Host "==== edge (tail) ===="
Get-Content $edgeErr -ErrorAction SilentlyContinue | Select-Object -Last 5
Write-Host "==== capture ===="
Get-Content $capErr -ErrorAction SilentlyContinue | Select-Object -Last 2

$failoverOk = $elog -match "mesh_failover=[1-9]"
$capOk = (Test-Path $Zcm) -and ((Get-Item $Zcm).Length -gt 40) -and ($clog -match "captured ok=[1-9]")
if ($failoverOk -and $capOk) {
    Write-Host "FAILOVER_CPP_OK"
    exit 0
}
Write-Host "FAILOVER_CPP_FAIL failover=$failoverOk cap=$capOk"
exit 1

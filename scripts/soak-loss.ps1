# Loss path: hop --loss-pct → capture .zcm → inspect --expect-gaps-min
param(
    [int]$LossPct = 10,
    [int]$Seconds = 6,
    [int]$Rate = 400,
    [int]$ExpectGapsMin = 5
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Edge = Join-Path $Bin "zcmesh_edge.exe"
$Hop = Join-Path $Bin "zcmesh_hop.exe"
$Cap = Join-Path $Bin "zcmesh_capture.exe"
$Inspect = Join-Path $Bin "zcmesh_inspect.exe"
$Zcm = Join-Path $Root "loss-soak.zcm"

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

Remove-Item $Zcm -ErrorAction SilentlyContinue
$capErr = Join-Path $Root "loss-cap-err.txt"
$hopErr = Join-Path $Root "loss-hop-err.txt"
$edgeErr = Join-Path $Root "loss-edge-err.txt"
Remove-Item $capErr, $hopErr, $edgeErr -ErrorAction SilentlyContinue

Write-Host "Capture on :9911 for ${Seconds}s"
$cap = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9911", "--out", $Zcm, "--seconds", "$Seconds") `
    -RedirectStandardError $capErr -PassThru
Start-Sleep -Seconds 1

Write-Host "Hop 9910 -> 9911 loss_pct=$LossPct --final"
$hop = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9910", "--forward", "127.0.0.1:9911",
                    "--loss-pct", "$LossPct", "--final") `
    -RedirectStandardError $hopErr -PassThru

Write-Host "Edge mesh --hop 127.0.0.1:9910 rate=$Rate"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9911", "--transport", "mesh",
                    "--hop", "127.0.0.1:9910", "--rate", "$Rate", "--batch", "8",
                    "--duration", "$($Seconds - 1)") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $cap.Id -Timeout ($Seconds + 8) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $hop, $cap)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

if (-not (Test-Path $Zcm) -or (Get-Item $Zcm).Length -lt 40) {
    Write-Host "LOSS_SOAK_FAIL: no capture"
    Get-Content $capErr -ErrorAction SilentlyContinue
    exit 1
}

Write-Host "Inspect with --expect-gaps-min $ExpectGapsMin"
& $Inspect $Zcm --expect-gaps-min $ExpectGapsMin
if ($LASTEXITCODE -ne 0) {
    Write-Host "LOSS_SOAK_FAIL: inspect gaps"
    exit 1
}
Write-Host "LOSS_SOAK_OK"
exit 0

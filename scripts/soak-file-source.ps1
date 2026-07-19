# Edge --file → capture → inspect --expect-raws (real sample values on the wire).
param(
    [int]$Rate = 200,
    [int]$CapSeconds = 4
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Edge = Join-Path $Bin "zcmesh_edge.exe"
$Cap = Join-Path $Bin "zcmesh_capture.exe"
$Inspect = Join-Path $Bin "zcmesh_inspect.exe"
$Samples = Join-Path $Root "fixtures\samples.txt"
$Zcm = Join-Path $Root "file-source.zcm"

if (-not (Test-Path $Edge)) {
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$capErr = Join-Path $Root "file-cap-err.txt"
$edgeErr = Join-Path $Root "file-edge-err.txt"
Remove-Item $Zcm, $capErr, $edgeErr -ErrorAction SilentlyContinue

$raws = ((Get-Content $Samples) | Where-Object { $_ -match '^-?\d+$' }) -join ','
$count = ($raws -split ',').Count

Write-Host "Capture UDP :9960 (${CapSeconds}s); edge --file ($count samples)"
$cap = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9960", "--out", $Zcm, "--seconds", "$CapSeconds", "--mode", "udp") `
    -RedirectStandardError $capErr -PassThru
Start-Sleep -Seconds 1

$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9960", "--transport", "udp",
                    "--file", $Samples, "--rate", "$Rate", "--batch", "1",
                    "--duration", "$($CapSeconds - 1)") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $cap.Id -Timeout ($CapSeconds + 6) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $cap)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

if (-not (Test-Path $Zcm) -or (Get-Item $Zcm).Length -lt 40) {
    Write-Host "FILE_SOURCE_FAIL: no capture"
    Get-Content $capErr, $edgeErr -ErrorAction SilentlyContinue
    exit 1
}

& $Inspect $Zcm --expect-gaps-max 0 --expect-frames-min $count --expect-raws $raws
if ($LASTEXITCODE -ne 0) {
    Write-Host "FILE_SOURCE_FAIL: inspect"
    exit 1
}
Write-Host "FILE_SOURCE_OK"
exit 0

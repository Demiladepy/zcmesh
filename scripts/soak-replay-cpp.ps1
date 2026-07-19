# C++-only capture → replay → recapture roundtrip (no Java).
param(
    [int]$Seconds = 4,
    [int]$Rate = 400
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Edge = Join-Path $Bin "zcmesh_edge.exe"
$Cap = Join-Path $Bin "zcmesh_capture.exe"
$Replay = Join-Path $Bin "zcmesh_replay.exe"
$Inspect = Join-Path $Bin "zcmesh_inspect.exe"
$Zcm1 = Join-Path $Root "replay-src.zcm"
$Zcm2 = Join-Path $Root "replay-dst.zcm"

if (-not (Test-Path $Edge)) {
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$err1 = Join-Path $Root "replay-cpp-cap1-err.txt"
$errE = Join-Path $Root "replay-cpp-edge-err.txt"
$err2 = Join-Path $Root "replay-cpp-cap2-err.txt"
$errR = Join-Path $Root "replay-cpp-replay-err.txt"
Remove-Item $Zcm1, $Zcm2, $err1, $errE, $err2, $errR -ErrorAction SilentlyContinue

Write-Host "Phase1: capture UDP :9980"
$cap1 = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9980", "--out", $Zcm1, "--seconds", "$Seconds", "--mode", "udp") `
    -RedirectStandardError $err1 -PassThru
Start-Sleep -Seconds 1
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9980", "--transport", "udp",
                    "--rate", "$Rate", "--batch", "8", "--duration", "$($Seconds - 1)") `
    -RedirectStandardError $errE -PassThru
Wait-Process -Id $cap1.Id -Timeout ($Seconds + 6) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $cap1)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

if (-not (Test-Path $Zcm1) -or (Get-Item $Zcm1).Length -lt 40) {
    Write-Host "REPLAY_CPP_FAIL: src capture"
    exit 1
}
& $Inspect $Zcm1 --expect-gaps-max 0 --expect-frames-min 50
if ($LASTEXITCODE -ne 0) { Write-Host "REPLAY_CPP_FAIL: src inspect"; exit 1 }

Write-Host "Phase2: replay TCP -> capture :9981"
$cap2 = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9981", "--out", $Zcm2, "--seconds", "$($Seconds + 2)", "--mode", "tcp") `
    -RedirectStandardError $err2 -PassThru
Start-Sleep -Seconds 1
$rep = Start-Process -FilePath $Replay `
    -ArgumentList @("--in", $Zcm1, "--target", "127.0.0.1:9981", "--transport", "tcp", "--rate", "800") `
    -RedirectStandardError $errR -PassThru -Wait
Wait-Process -Id $cap2.Id -Timeout ($Seconds + 8) -ErrorAction SilentlyContinue
if (-not $cap2.HasExited) { Stop-Process -Id $cap2.Id -Force -ErrorAction SilentlyContinue }
if ($rep.ExitCode -ne 0) {
    Write-Host "REPLAY_CPP_FAIL: replay exit=$($rep.ExitCode)"
    Get-Content $errR -ErrorAction SilentlyContinue
    exit 1
}

if (-not (Test-Path $Zcm2) -or (Get-Item $Zcm2).Length -lt 40) {
    Write-Host "REPLAY_CPP_FAIL: dst capture"
    Get-Content $err2, $errR -ErrorAction SilentlyContinue
    exit 1
}
& $Inspect $Zcm2 --expect-gaps-max 0 --expect-frames-min 50
if ($LASTEXITCODE -ne 0) { Write-Host "REPLAY_CPP_FAIL: dst inspect"; exit 1 }

Write-Host "REPLAY_CPP_OK"
exit 0

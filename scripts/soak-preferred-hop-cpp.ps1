# C++-only preferred-hop recovery: capture sink + 2 hops + edge --control + zcmesh_ctl.
# No Java. Asserts demote hop=1 then remotion hop=0.
param(
    [int]$Rate = 400,
    [int]$PhaseSec = 3
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Edge = Join-Path $Bin "zcmesh_edge.exe"
$Hop = Join-Path $Bin "zcmesh_hop.exe"
$Ctl = Join-Path $Bin "zcmesh_ctl.exe"
$Cap = Join-Path $Bin "zcmesh_capture.exe"
$Zcm = Join-Path $Root "prefhop-cpp.zcm"

if (-not (Test-Path $Edge) -or -not (Test-Path $Ctl)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$capErr = Join-Path $Root "prefhop-cpp-cap-err.txt"
$hop0Err = Join-Path $Root "prefhop-cpp-hop0-err.txt"
$hop1Err = Join-Path $Root "prefhop-cpp-hop1-err.txt"
$edgeErr = Join-Path $Root "prefhop-cpp-edge-err.txt"
Remove-Item $Zcm, $capErr, $hop0Err, $hop1Err, $edgeErr -ErrorAction SilentlyContinue

$capSec = $PhaseSec * 3 + 3
Write-Host "Capture UDP on :9930 for ${capSec}s"
$cap = Start-Process -FilePath $Cap `
    -ArgumentList @("--listen", "127.0.0.1:9930", "--out", $Zcm, "--seconds", "$capSec", "--mode", "udp") `
    -RedirectStandardError $capErr -PassThru
Start-Sleep -Seconds 1

Write-Host "Hop0 :9928 and Hop1 :9929 -> capture"
$hop0 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9928", "--forward", "127.0.0.1:9930", "--final") `
    -RedirectStandardError $hop0Err -PassThru
$hop1 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9929", "--forward", "127.0.0.1:9930", "--final") `
    -RedirectStandardError $hop1Err -PassThru

$edgeDur = $PhaseSec * 3 + 1
Write-Host "Edge mesh --control :9897"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9930", "--transport", "mesh",
                    "--hop", "127.0.0.1:9928", "--hop", "127.0.0.1:9929",
                    "--control", "127.0.0.1:9897",
                    "--rate", "$Rate", "--batch", "4", "--duration", "$edgeDur",
                    "--print-stats-sec", "1") `
    -RedirectStandardError $edgeErr -PassThru

Write-Host "Phase1: hop0 preferred (${PhaseSec}s)"
Start-Sleep -Seconds $PhaseSec

Write-Host "Phase2: ctl SET_SKIP 1"
& $Ctl --target 127.0.0.1:9897 --node-id 1 --skip 1
Start-Sleep -Seconds $PhaseSec

Write-Host "Phase3: ctl CLEAR"
& $Ctl --target 127.0.0.1:9897 --node-id 1 --clear
Start-Sleep -Seconds ([Math]::Max($PhaseSec, 2))

Wait-Process -Id $cap.Id -Timeout ($PhaseSec + 8) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $hop0, $hop1, $cap)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
Start-Sleep -Milliseconds 300

$elog = Get-Content $edgeErr -Raw -ErrorAction SilentlyContinue
Write-Host "==== edge (tail) ===="
Get-Content $edgeErr -ErrorAction SilentlyContinue | Select-Object -Last 12

$hopLines = [regex]::Matches($elog, "hop=(\d+)\s+route_fail=\d+\s+hop_ok=(\d+)/(\d+)/(\d+)")
$sawDemote = $false
$sawRecover = $false
$seenHop1 = $false
foreach ($m in $hopLines) {
    $h = [int]$m.Groups[1].Value
    $ok0 = [int64]$m.Groups[2].Value
    $ok1 = [int64]$m.Groups[3].Value
    if ($h -eq 1 -and $ok1 -gt 0) {
        $seenHop1 = $true
        $sawDemote = $true
    }
    if ($seenHop1 -and $h -eq 0 -and $ok0 -gt 0) {
        $sawRecover = $true
    }
}
$ctrlOk = ($elog -match "ctrl SET_SKIP") -and ($elog -match "ctrl CLEAR")
$capOk = (Test-Path $Zcm) -and ((Get-Item $Zcm).Length -gt 40)

Write-Host "==== demote=$sawDemote recover=$sawRecover ctrl=$ctrlOk cap=$capOk ===="
if ($sawDemote -and $sawRecover -and $ctrlOk -and $capOk) {
    Write-Host "PREFHOP_CPP_OK"
    exit 0
}
Write-Host "PREFHOP_CPP_FAIL"
exit 1

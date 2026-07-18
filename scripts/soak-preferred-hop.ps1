# Prove preferred-hop recovery via UDP mesh control (zcmesh_ctl → edge --control).
param(
    [int]$Rate = 400,
    [int]$PhaseSec = 3
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Edge = Join-Path $Root "core-engine\build\zcmesh_edge.exe"
$Hop = Join-Path $Root "core-engine\build\zcmesh_hop.exe"
$Ctl = Join-Path $Root "core-engine\build\zcmesh_ctl.exe"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot" }
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

if (-not (Test-Path $Edge) -or -not (Test-Path $Ctl)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$smokeOut = Join-Path $Root "prefhop-smoke-out.txt"
$smokeErr = Join-Path $Root "prefhop-smoke-err.txt"
$hop0Err = Join-Path $Root "prefhop-hop0-err.txt"
$hop1Err = Join-Path $Root "prefhop-hop1-err.txt"
$edgeErr = Join-Path $Root "prefhop-edge-err.txt"
Remove-Item $smokeOut, $smokeErr, $hop0Err, $hop1Err, $edgeErr -ErrorAction SilentlyContinue

$totalTimeout = ($PhaseSec * 3) + 30
Write-Host "Starting UDP-only operator on :9900"
$smoke = Start-Process -FilePath (Join-Path $Root "operator-node\gradlew.bat") `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=30",
                    "-PsmokeTimeout=$totalTimeout", "-PsmokeUdpOnly=true") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru

$deadline = (Get-Date).AddSeconds(90)
$ready = $false
while ((Get-Date) -lt $deadline) {
    $err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
    if ($err -match "UDP-only listening on 9900") { $ready = $true; break }
    if ($err -match "BUILD FAILED") { break }
    Start-Sleep -Milliseconds 300
}
if (-not $ready) {
    Write-Host "Operator failed to start"
    Get-Content $smokeErr -ErrorAction SilentlyContinue
    exit 1
}

Write-Host "Starting hop0 :9901 and hop1 :9902 -> operator"
$hop0 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9901", "--forward", "127.0.0.1:9900", "--final") `
    -RedirectStandardError $hop0Err -PassThru
$hop1 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9902", "--forward", "127.0.0.1:9900", "--final") `
    -RedirectStandardError $hop1Err -PassThru

$edgeDuration = $PhaseSec * 3 + 2
Write-Host "Edge mesh --control :9898 for ${edgeDuration}s"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9900", "--transport", "mesh",
                    "--hop", "127.0.0.1:9901", "--hop", "127.0.0.1:9902",
                    "--control", "127.0.0.1:9898",
                    "--rate", "$Rate", "--batch", "4", "--duration", "$edgeDuration",
                    "--print-stats-sec", "1") `
    -RedirectStandardError $edgeErr -PassThru

Write-Host "Phase1: preferred hop0 (${PhaseSec}s)"
Start-Sleep -Seconds $PhaseSec

Write-Host "Phase2: ctl SET_SKIP mask=1 (expect hop=1)"
& $Ctl --target 127.0.0.1:9898 --node-id 1 --skip 1
Start-Sleep -Seconds $PhaseSec

Write-Host "Phase3: ctl CLEAR (expect remotion hop=0)"
& $Ctl --target 127.0.0.1:9898 --node-id 1 --clear
Start-Sleep -Seconds ([Math]::Max($PhaseSec, 2))

Wait-Process -Id $edge.Id -Timeout ($PhaseSec + 10) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $hop0, $hop1, $smoke)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
Start-Sleep -Milliseconds 400

$elog = Get-Content $edgeErr -Raw -ErrorAction SilentlyContinue
Write-Host "==== edge stats (tail) ===="
Get-Content $edgeErr -ErrorAction SilentlyContinue | Select-Object -Last 14

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
$ctrlOk = $elog -match "ctrl SET_SKIP" -and $elog -match "ctrl CLEAR"

$smokeOk = (Get-Content $smokeOut -Raw -ErrorAction SilentlyContinue) -match "SMOKE_OK"
Write-Host "==== result demote=$sawDemote recover=$sawRecover ctrl=$ctrlOk smoke=$smokeOk ===="
if ($sawDemote -and $sawRecover -and $ctrlOk -and $smokeOk) {
    Write-Host "PREFHOP_OK"
    exit 0
}
Write-Host "PREFHOP_FAIL"
exit 1

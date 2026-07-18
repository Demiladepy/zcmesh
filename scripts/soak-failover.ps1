# Prove auto TCP→mesh failover: operator UDP-only, hop --final, edge transport=auto.
param(
    [int]$Seconds = 8,
    [int]$Rate = 400,
    [int]$MinFrames = 50
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Edge = Join-Path $Root "core-engine\build\zcmesh_edge.exe"
$Hop = Join-Path $Root "core-engine\build\zcmesh_hop.exe"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot" }
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$smokeOut = Join-Path $Root "failover-smoke-out.txt"
$smokeErr = Join-Path $Root "failover-smoke-err.txt"
$hopErr = Join-Path $Root "failover-hop-err.txt"
$edgeErr = Join-Path $Root "failover-edge-err.txt"
Remove-Item $smokeOut, $smokeErr, $hopErr, $edgeErr -ErrorAction SilentlyContinue

Write-Host "Starting UDP-only operator on :9900 (TCP denied; forces mesh failover)"
$smoke = Start-Process -FilePath (Join-Path $Root "operator-node\gradlew.bat") `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=$MinFrames",
                    "-PsmokeTimeout=$($Seconds + 20)", "-PsmokeUdpOnly=true") `
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

Write-Host "Starting hop 9901 -> 9900 --final"
$hop = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9901", "--forward", "127.0.0.1:9900", "--final") `
    -RedirectStandardError $hopErr -PassThru

Write-Host "Starting edge transport=auto --hop 127.0.0.1:9901"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9900", "--transport", "auto",
                    "--hop", "127.0.0.1:9901", "--rate", "$Rate", "--batch", "8",
                    "--duration", "$Seconds", "--print-stats-sec", "2") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $edge.Id -Timeout ($Seconds + 15) -ErrorAction SilentlyContinue
if (-not $edge.HasExited) { Stop-Process -Id $edge.Id -Force -ErrorAction SilentlyContinue }
if (-not $smoke.HasExited) { Stop-Process -Id $smoke.Id -Force -ErrorAction SilentlyContinue }
if (-not $hop.HasExited) { Stop-Process -Id $hop.Id -Force -ErrorAction SilentlyContinue }

Start-Sleep -Milliseconds 400

$out = Get-Content $smokeOut -Raw -ErrorAction SilentlyContinue
$elog = Get-Content $edgeErr -Raw -ErrorAction SilentlyContinue
Write-Host "==== result ===="
Get-Content $smokeOut -ErrorAction SilentlyContinue | Select-String "SMOKE"
Get-Content $edgeErr -ErrorAction SilentlyContinue | Select-Object -Last 6

$smokeOk = $out -match "SMOKE_OK"
$failoverOk = $elog -match "mesh_failover=[1-9]"
if ($smokeOk -and $failoverOk) {
    Write-Host "FAILOVER_OK"
    exit 0
}
Write-Host "FAILOVER_FAIL smoke=$smokeOk failover_counter=$failoverOk"
exit 1

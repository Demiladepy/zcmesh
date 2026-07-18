# Local mesh path: operator UDP <- hop(9901) <- edge(--transport udp)
# Optional: -LossPct 5 to inject deterministic drops (operator shows gaps).
param(
    [int]$LossPct = 0,
    [int]$Rate = 500,
    [int]$Seconds = 12
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Edge = Join-Path $Root "core-engine\build\zcmesh_edge.exe"
$Hop = Join-Path $Root "core-engine\build\zcmesh_hop.exe"
$JavaHome = $env:JAVA_HOME
if (-not $JavaHome -or -not (Test-Path $JavaHome)) {
    $JavaHome = "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot"
}
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$smokeOut = Join-Path $Root "demo-smoke-out.txt"
$smokeErr = Join-Path $Root "demo-smoke-err.txt"
$hopErr = Join-Path $Root "demo-hop-err.txt"
$edgeErr = Join-Path $Root "demo-edge-err.txt"
Remove-Item $smokeOut, $smokeErr, $hopErr, $edgeErr -ErrorAction SilentlyContinue

Write-Host "Starting headless operator on :9900"
$smoke = Start-Process -FilePath (Join-Path $Root "operator-node\gradlew.bat") `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=100", "-PsmokeTimeout=$Seconds") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru

$deadline = (Get-Date).AddSeconds(90)
$ready = $false
while ((Get-Date) -lt $deadline) {
    $err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
    if ($err -match "listening on 9900") { $ready = $true; break }
    if ($err -match "BUILD FAILED") { break }
    Start-Sleep -Milliseconds 300
}
if (-not $ready) {
    Write-Host "Operator failed to start"
    Get-Content $smokeErr -ErrorAction SilentlyContinue
    exit 1
}

Write-Host "Starting hop 9901 -> 9900 (loss_pct=$LossPct)"
$hopArgs = @("--listen", "127.0.0.1:9901", "--forward", "127.0.0.1:9900", "--loss-pct", "$LossPct")
$hop = Start-Process -FilePath $Hop -ArgumentList $hopArgs -RedirectStandardError $hopErr -PassThru

Write-Host "Starting edge transport=udp rate=$Rate"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9900", "--transport", "udp", "--rate", "$Rate", "--batch", "8") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $smoke.Id -Timeout ($Seconds + 30) -ErrorAction SilentlyContinue
if (-not $smoke.HasExited) { Stop-Process -Id $smoke.Id -Force -ErrorAction SilentlyContinue }
if (-not $hop.HasExited) { Stop-Process -Id $hop.Id -Force -ErrorAction SilentlyContinue }
if (-not $edge.HasExited) { Stop-Process -Id $edge.Id -Force -ErrorAction SilentlyContinue }

Write-Host "==== result ===="
Get-Content $smokeOut -ErrorAction SilentlyContinue | Select-String "SMOKE"
Get-Content $hopErr -ErrorAction SilentlyContinue | Select-Object -Last 2
exit $(if ((Get-Content $smokeOut -Raw -ErrorAction SilentlyContinue) -match "SMOKE_OK") { 0 } else { 1 })

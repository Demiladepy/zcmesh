# CI / local: Java operator e2e — edge TCP → HeadlessOperator SMOKE_OK.
param(
    [int]$Seconds = 8,
    [int]$Rate = 500,
    [int]$MinFrames = 100
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Edge = Join-Path $Root "core-engine\build\zcmesh_edge.exe"
$Gradlew = Join-Path $Root "operator-node\gradlew.bat"

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$smokeOut = Join-Path $Root "e2e-smoke-out.txt"
$smokeErr = Join-Path $Root "e2e-smoke-err.txt"
$edgeErr = Join-Path $Root "e2e-edge-err.txt"
Remove-Item $smokeOut, $smokeErr, $edgeErr -ErrorAction SilentlyContinue

Write-Host "Headless operator :9900 (minFrames=$MinFrames)"
$smoke = Start-Process -FilePath $Gradlew `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=$MinFrames",
                    "-PsmokeTimeout=$($Seconds + 25)") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru

$deadline = (Get-Date).AddSeconds(120)
$ready = $false
while ((Get-Date) -lt $deadline) {
    $err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
    if ($err -match "listening on 9900") { $ready = $true; break }
    if ($err -match "BUILD FAILED") { break }
    Start-Sleep -Milliseconds 300
}
if (-not $ready) {
    Write-Host "E2E_FAIL: operator"
    Get-Content $smokeErr -ErrorAction SilentlyContinue
    exit 1
}

Write-Host "Edge TCP rate=$Rate for ${Seconds}s"
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9900", "--transport", "tcp",
                    "--rate", "$Rate", "--batch", "32", "--duration", "$Seconds") `
    -RedirectStandardError $edgeErr -PassThru

Wait-Process -Id $smoke.Id -Timeout ($Seconds + 35) -ErrorAction SilentlyContinue
foreach ($p in @($edge, $smoke)) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host "==== e2e ===="
Get-Content $smokeOut -ErrorAction SilentlyContinue | Select-String "SMOKE"
if ((Get-Content $smokeOut -Raw -ErrorAction SilentlyContinue) -match "SMOKE_OK") {
    Write-Host "E2E_OK"
    exit 0
}
Write-Host "E2E_FAIL"
Get-Content $smokeOut, $smokeErr, $edgeErr -ErrorAction SilentlyContinue
exit 1

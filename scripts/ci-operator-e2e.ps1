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

# Keep operator alive for the full edge window so mid-flight stats scrape works.
$stayUp = [Math]::Max($MinFrames, $Rate * $Seconds)
Write-Host "Headless operator :9900 (minFrames=$stayUp)"
$smoke = Start-Process -FilePath $Gradlew `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=$stayUp",
                    "-PsmokeTimeout=$($Seconds + 25)") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru

$deadline = (Get-Date).AddSeconds(120)
$ready = $false
while ((Get-Date) -lt $deadline) {
    $err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
    if ($err -match "StatsServer listening on 9909") { $ready = $true; break }
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

# Scrape mid-flight while operator is still up.
Start-Sleep -Seconds 2
Write-Host "Scrape stats :9909"
$Classes = Join-Path $Root "operator-node\build\classes\java\main"
$statsOk = $false
$statsOut = ""
$minStats = [Math]::Max(10, [int]($Rate))
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
for ($i = 0; $i -lt 8; $i++) {
    $statsOut = cmd /c "java -cp `"$Classes`" zcmesh.net.StatsScrape 127.0.0.1 9909 $minStats 2>&1"
    if ($LASTEXITCODE -eq 0 -and ($statsOut -join "`n") -match "STATS_OK") {
        $statsOk = $true
        break
    }
    Start-Sleep -Milliseconds 500
}
$ErrorActionPreference = $prevEap
Write-Host ($statsOut -join "`n")
if (-not $statsOk) {
    Write-Host "E2E_FAIL: stats"
    Get-Content $smokeErr -ErrorAction SilentlyContinue | Select-Object -Last 20
    foreach ($p in @($edge, $smoke)) {
        if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    }
    exit 1
}

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

# Multi-edge high-Hz stress: 3 TCP edges → operator; assert nodes=3 + SMOKE_OK.
param(
    [int]$Seconds = 8,
    [int]$Rate = 2000,
    [int]$Nodes = 3
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Edge = Join-Path $Root "core-engine\build\zcmesh_edge.exe"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot" }
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

if (-not (Test-Path $Edge)) {
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

$smokeOut = Join-Path $Root "stress-smoke-out.txt"
$smokeErr = Join-Path $Root "stress-smoke-err.txt"
Remove-Item $smokeOut, $smokeErr -ErrorAction SilentlyContinue

# Aim for ~half of offered frames so CI isn't flaky under load.
$minFrames = [Math]::Max(500, [int]($Rate * $Nodes * $Seconds * 0.25))
Write-Host "Stress: $Nodes edges @ ${Rate}Hz for ${Seconds}s (minFrames=$minFrames)"

$smoke = Start-Process -FilePath (Join-Path $Root "operator-node\gradlew.bat") `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=$minFrames",
                    "-PsmokeTimeout=$($Seconds + 40)") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru

$deadline = (Get-Date).AddSeconds(120)
while ((Get-Date) -lt $deadline) {
    $err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
    if ($err -match "listening on 9900") { break }
    if ($err -match "BUILD FAILED") { Get-Content $smokeErr; exit 1 }
    Start-Sleep -Milliseconds 300
}

$edges = @()
for ($id = 1; $id -le $Nodes; $id++) {
    $errFile = Join-Path $Root "stress-edge-$id-err.txt"
    Remove-Item $errFile -ErrorAction SilentlyContinue
    $edges += Start-Process -FilePath $Edge `
        -ArgumentList @("--operator", "127.0.0.1:9900", "--node-id", "$id", "--rate", "$Rate",
                        "--batch", "64", "--transport", "tcp", "--duration", "$Seconds") `
        -RedirectStandardError $errFile -PassThru
}

Wait-Process -Id $smoke.Id -Timeout ($Seconds + 50) -ErrorAction SilentlyContinue
foreach ($p in @($smoke) + $edges) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host "==== stress result ===="
Get-Content $smokeOut -ErrorAction SilentlyContinue | Select-String "SMOKE"
$raw = Get-Content $smokeOut -Raw -ErrorAction SilentlyContinue
$ok = ($raw -match "SMOKE_OK") -and ($raw -match "nodes=$Nodes")
if (-not $ok) {
    Write-Host "STRESS_FAIL (expected nodes=$Nodes)"
    Get-Content $smokeOut, $smokeErr -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "STRESS_OK"
exit 0

# Multi-node TCP uplink: 3 edges -> operator
param(
    [int]$Seconds = 15,
    [int]$Rate = 300
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

$smokeOut = Join-Path $Root "multi-smoke-out.txt"
$smokeErr = Join-Path $Root "multi-smoke-err.txt"
Remove-Item $smokeOut, $smokeErr -ErrorAction SilentlyContinue

$minFrames = [Math]::Max(150, $Rate * 3)
$smoke = Start-Process -FilePath (Join-Path $Root "operator-node\gradlew.bat") `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=$minFrames", "-PsmokeTimeout=$($Seconds + 20)") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru

$deadline = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $deadline) {
    $err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
    if ($err -match "listening on 9900") { break }
    if ($err -match "BUILD FAILED") { Get-Content $smokeErr; exit 1 }
    Start-Sleep -Milliseconds 300
}

$edges = @()
foreach ($id in 1, 2, 3) {
    $errFile = Join-Path $Root "multi-edge-$id-err.txt"
    Remove-Item $errFile -ErrorAction SilentlyContinue
    $edges += Start-Process -FilePath $Edge `
        -ArgumentList @("--operator", "127.0.0.1:9900", "--node-id", "$id", "--rate", "$Rate",
                        "--batch", "8", "--transport", "tcp", "--duration", "$Seconds") `
        -RedirectStandardError $errFile -PassThru
}

Wait-Process -Id $smoke.Id -Timeout ($Seconds + 40) -ErrorAction SilentlyContinue
foreach ($p in @($smoke) + $edges) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host "==== multi-node result ===="
Get-Content $smokeOut -ErrorAction SilentlyContinue | Select-String "SMOKE"
$ok = (Get-Content $smokeOut -Raw -ErrorAction SilentlyContinue) -match "SMOKE_OK" -and `
      (Get-Content $smokeOut -Raw) -match "nodes=3"
if (-not $ok) {
    Write-Host "FAIL (expected nodes=3)"
    Get-Content $smokeOut, $smokeErr -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "MULTI_OK"
exit 0

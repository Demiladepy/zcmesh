# Capture UDP frames to .zcm then replay over TCP to operator.
param([int]$Seconds = 6, [int]$Rate = 400)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root "core-engine\build"
$Zcm = Join-Path $Root "capture-demo.zcm"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot" }
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

$cap = Start-Process -FilePath (Join-Path $Bin "zcmesh_capture.exe") `
    -ArgumentList @("--listen", "127.0.0.1:9910", "--out", $Zcm, "--seconds", "$Seconds") `
    -RedirectStandardError (Join-Path $Root "cap-err.txt") -PassThru
Start-Sleep -Seconds 1
$edge = Start-Process -FilePath (Join-Path $Bin "zcmesh_edge.exe") `
    -ArgumentList @("--operator", "127.0.0.1:9910", "--transport", "udp", "--rate", "$Rate",
                    "--batch", "4", "--duration", "$($Seconds - 1)") `
    -RedirectStandardError (Join-Path $Root "cap-edge-err.txt") -PassThru
Wait-Process -Id $cap.Id -Timeout ($Seconds + 5) -ErrorAction SilentlyContinue
if (-not $edge.HasExited) { Stop-Process -Id $edge.Id -Force -ErrorAction SilentlyContinue }
if (-not $cap.HasExited) { Stop-Process -Id $cap.Id -Force -ErrorAction SilentlyContinue }

if (-not (Test-Path $Zcm) -or (Get-Item $Zcm).Length -lt 40) {
    Write-Host "CAPTURE_FAIL"
    Get-Content (Join-Path $Root "cap-err.txt") -ErrorAction SilentlyContinue
    exit 1
}

& (Join-Path $Bin "zcmesh_inspect.exe") $Zcm
if ($LASTEXITCODE -ne 0) {
    Write-Host "INSPECT_FAIL"
    exit 1
}

$smokeOut = Join-Path $Root "replay-smoke-out.txt"
$smokeErr = Join-Path $Root "replay-smoke-err.txt"
Remove-Item $smokeOut, $smokeErr -ErrorAction SilentlyContinue
$smoke = Start-Process -FilePath (Join-Path $Root "operator-node\gradlew.bat") `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=50", "-PsmokeTimeout=30") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru
$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    if ((Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue) -match "listening on 9900") { break }
    Start-Sleep -Milliseconds 300
}

& (Join-Path $Bin "zcmesh_replay.exe") --in $Zcm --target 127.0.0.1:9900 --transport tcp --pace capture
Wait-Process -Id $smoke.Id -Timeout 35 -ErrorAction SilentlyContinue
if (-not $smoke.HasExited) { Stop-Process -Id $smoke.Id -Force -ErrorAction SilentlyContinue }

Get-Content $smokeOut | Select-String "SMOKE"
exit $(if ((Get-Content $smokeOut -Raw) -match "SMOKE_OK") { 0 } else { 1 })

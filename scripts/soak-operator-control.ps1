# Preferred-hop recovery driven by Java ControlClient (not zcmesh_ctl).
param(
    [int]$Rate = 400,
    [int]$PhaseSec = 3
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Edge = Join-Path $Root "core-engine\build\zcmesh_edge.exe"
$Hop = Join-Path $Root "core-engine\build\zcmesh_hop.exe"
$OpDir = Join-Path $Root "operator-node"
$Gradlew = Join-Path $OpDir "gradlew.bat"
$Classes = Join-Path $OpDir "build\classes\java\main"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot" }
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

function Invoke-ControlCli([string[]]$CliArgs) {
    & java -cp $Classes zcmesh.net.ControlCli @CliArgs
    if ($LASTEXITCODE -ne 0) {
        throw "ControlCli failed: $($CliArgs -join ' ')"
    }
}

if (-not (Test-Path $Edge)) {
    Write-Host "Building core-engine..."
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j
    Pop-Location
}

Write-Host "Compiling operator classes (ControlCli)..."
Push-Location $OpDir
& $Gradlew classes --no-daemon -q
if ($LASTEXITCODE -ne 0) { Pop-Location; exit 1 }
Pop-Location
if (-not (Test-Path (Join-Path $Classes "zcmesh\net\ControlCli.class"))) {
    Write-Host "ControlCli.class missing"
    exit 1
}

$smokeOut = Join-Path $Root "opctl-smoke-out.txt"
$smokeErr = Join-Path $Root "opctl-smoke-err.txt"
$hop0Err = Join-Path $Root "opctl-hop0-err.txt"
$hop1Err = Join-Path $Root "opctl-hop1-err.txt"
$edgeErr = Join-Path $Root "opctl-edge-err.txt"
Remove-Item $smokeOut, $smokeErr, $hop0Err, $hop1Err, $edgeErr -ErrorAction SilentlyContinue

$totalTimeout = ($PhaseSec * 3) + 30
Write-Host "UDP-only operator :9900"
$smoke = Start-Process -FilePath $Gradlew `
    -WorkingDirectory $OpDir `
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
    Write-Host "Operator failed"
    Get-Content $smokeErr -ErrorAction SilentlyContinue
    exit 1
}

$hop0 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9901", "--forward", "127.0.0.1:9900", "--final") `
    -RedirectStandardError $hop0Err -PassThru
$hop1 = Start-Process -FilePath $Hop `
    -ArgumentList @("--listen", "127.0.0.1:9902", "--forward", "127.0.0.1:9900", "--final") `
    -RedirectStandardError $hop1Err -PassThru

$edgeDuration = $PhaseSec * 3 + 2
$edge = Start-Process -FilePath $Edge `
    -ArgumentList @("--operator", "127.0.0.1:9900", "--transport", "mesh",
                    "--hop", "127.0.0.1:9901", "--hop", "127.0.0.1:9902",
                    "--control", "127.0.0.1:9898",
                    "--rate", "$Rate", "--batch", "4", "--duration", "$edgeDuration",
                    "--print-stats-sec", "1") `
    -RedirectStandardError $edgeErr -PassThru

Write-Host "Phase1: preferred hop0 (${PhaseSec}s)"
Start-Sleep -Seconds $PhaseSec

Write-Host "Phase2: Java ControlCli SET_SKIP"
Invoke-ControlCli @("127.0.0.1:9898", "--node-id", "1", "--skip", "1")
Start-Sleep -Seconds $PhaseSec

Write-Host "Phase3: Java ControlCli CLEAR"
Invoke-ControlCli @("127.0.0.1:9898", "--node-id", "1", "--clear")
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

Write-Host "==== demote=$sawDemote recover=$sawRecover ctrl=$ctrlOk smoke=$smokeOk ===="
if ($sawDemote -and $sawRecover -and $ctrlOk -and $smokeOk) {
    Write-Host "OPCTL_OK"
    exit 0
}
Write-Host "OPCTL_FAIL"
exit 1

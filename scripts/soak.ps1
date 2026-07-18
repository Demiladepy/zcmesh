# Multi-edge soak: 3 nodes x N seconds, then scrape stats :9909
param(
    [int]$Seconds = 20,
    [int]$Rate = 400
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Edge = Join-Path $Root "core-engine\build\zcmesh_edge.exe"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Microsoft\jdk-17.0.19.10-hotspot" }
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

$minFrames = 999999
$smokeOut = Join-Path $Root "soak-out.txt"
$smokeErr = Join-Path $Root "soak-err.txt"
Remove-Item $smokeOut, $smokeErr -ErrorAction SilentlyContinue

$smoke = Start-Process -FilePath (Join-Path $Root "operator-node\gradlew.bat") `
    -WorkingDirectory (Join-Path $Root "operator-node") `
    -ArgumentList @("smoke", "--no-daemon", "-PsmokeFrames=$minFrames", "-PsmokeTimeout=$($Seconds + 15)") `
    -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr -PassThru

$deadline = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $deadline) {
    $err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
    if ($err -match "StatsServer listening on 9909") { break }
    if ($err -match "BUILD FAILED") { Get-Content $smokeErr; exit 1 }
    Start-Sleep -Milliseconds 300
}

$edges = @()
foreach ($id in 1, 2, 3) {
    $edges += Start-Process -FilePath $Edge `
        -ArgumentList @("--operator","127.0.0.1:9900","--node-id","$id","--rate","$Rate",
                        "--batch","16","--transport","tcp","--duration","$Seconds") `
        -RedirectStandardError (Join-Path $Root "soak-edge-$id.txt") -PassThru
}

Start-Sleep -Seconds ([Math]::Min(4, [Math]::Max(2, $Seconds / 3)))
try {
    $c = New-Object Net.Sockets.TcpClient("127.0.0.1", 9909)
    $s = $c.GetStream()
    $b = New-Object byte[] 2048
    $n = $s.Read($b, 0, 2048)
    $stats = [Text.Encoding]::ASCII.GetString($b, 0, $n)
    $c.Close()
    Write-Host "==== stats :9909 ===="
    Write-Host $stats
    if ($stats -notmatch "frames_ok=") { throw "bad stats payload" }
    if ($stats -notmatch "frames_ok=([1-9][0-9]*)") { throw "frames_ok missing or zero" }
    $framesOk = [int64]$Matches[1]
    if ($framesOk -lt 50) { throw "frames_ok=$framesOk too low" }
    if ($stats -notmatch "nodes=(\d+)") { throw "nodes missing" }
    if ([int]$Matches[1] -lt 2) { throw "expected >=2 nodes, got $($Matches[1])" }
} catch {
    Write-Host "stats scrape failed: $_"
    foreach ($p in @($smoke) + $edges) {
        if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    }
    exit 1
}

Wait-Process -Id $smoke.Id -Timeout ($Seconds + 20) -ErrorAction SilentlyContinue
foreach ($p in @($smoke) + $edges) {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host "==== soak result ===="
Write-Host "SOAK_OK frames_ok scrape passed"
exit 0

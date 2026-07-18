# Run encode microbench (builds core if needed).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Bench = Join-Path $Root "core-engine\build\zcmesh_bench.exe"
if (-not (Test-Path $Bench)) {
    Push-Location (Join-Path $Root "core-engine")
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release | Out-Null
    cmake --build build -j --target zcmesh_bench
    Pop-Location
}
$iters = if ($args.Count -ge 1) { $args[0] } else { "500000" }
& $Bench $iters

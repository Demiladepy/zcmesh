# ZCMesh — Zero-Copy Binary Telemetry Engine

Production-oriented edge telemetry: C++ arena-backed packing + mesh fallback routing, Java NIO/JavaFX operator gateway. Fixed **24-byte** little-endian frames. No JSON, no Protobuf, no cloud.

## Layout

```
zcmesh/
  shared/wire_frame.h     # canonical wire layout
  core-engine/            # CMake C++17 (zcmesh_core + zcmesh_edge)
  operator-node/          # Gradle Java 17 + JavaFX operator
```

## Wire frame (24 bytes, LE)

| Offset | Field | Type |
|--------|-------|------|
| 0 | magic `0x5A43` | u16 |
| 2 | version `1` | u8 |
| 3 | flags | u8 |
| 4 | seq | u32 |
| 8 | timestamp_lo | u32 |
| 12 | node_id | u16 |
| 14 | sensor_type | u8 |
| 15 | reserved | u8 |
| 16 | raw_value | i32 |
| 20 | checksum (CRC32 of first 20 bytes) | u32 |

## Build — C++ edge

Requirements: CMake 3.16+, C++17 compiler (MSVC or clang/gcc), Winsock on Windows.

```powershell
cd core-engine
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Binary: `core-engine/build/zcmesh_edge.exe` (MinGW) or `core-engine/build/Release/zcmesh_edge.exe` (MSVC).

Also built: `zcmesh_bench`, `zcmesh_hop`, `zcmesh_capture`, `zcmesh_replay`, `zcmesh_test_frame` (`ctest` in build dir).

See [ARCHITECTURE.md](ARCHITECTURE.md) and [BENCHMARK.md](BENCHMARK.md).
## Build — Java operator

Requirements: JDK 17+, network for first Gradle dependency resolve.

```powershell
cd operator-node
.\gradlew.bat run
```

Listen port defaults to `9900`. Override: `.\gradlew.bat run --args="9900"`.

## Run end-to-end

Terminal 1 — operator:

```powershell
cd operator-node
.\gradlew.bat run
```

Terminal 2 — edge (after operator is up):

```powershell
cd core-engine\build
.\zcmesh_edge.exe --operator 127.0.0.1:9900 --node-id 1 --rate 1000 --batch 32
```

Headless smoke (no JavaFX):

```powershell
# terminal A
cd operator-node
.\gradlew.bat smoke

# terminal B (once "listening on 9900")
cd ..\core-engine\build
.\zcmesh_edge.exe --operator 127.0.0.1:9900 --rate 500
```

Optional value sources: `--stdin` or `--file path` (one `int32` per line). Default stream is a hardware-clocked 50 Hz sine for soak/bench.

## Notes

- Arena is a single contiguous block (`VirtualAlloc` on Windows); hot path never calls the heap allocator.
- TCP uplink to the operator is preferred; UDP mesh hops `9901`/`9902` then operator UDP are deterministic fallbacks.
- Operator decodes with direct `ByteBuffer` absolute reads only.

## Benchmark (binary vs JSON snprintf)

```powershell
cd core-engine\build
.\zcmesh_bench.exe 500000
```

Reports bytes/frame and ns/op for arena binary pack vs a typical JSON telemetry line.

Example (local): `24 B` vs `~67 B` payload, **~94%** encode CPU reduction.
## Mesh hop relay

```powershell
.\zcmesh_hop.exe --listen 127.0.0.1:9901 --forward 127.0.0.1:9900
# optional loss injection for gap demo:
.\zcmesh_hop.exe --listen 127.0.0.1:9901 --forward 127.0.0.1:9900 --loss-pct 5
```

Force UDP direct to operator:

```powershell
.\zcmesh_edge.exe --transport udp --operator 127.0.0.1:9900 --rate 500 --batch 8
```

Force UDP mesh hops (`9901` → `9902` → operator):

```powershell
.\zcmesh_edge.exe --transport mesh --rate 500
```

One-shot local mesh demo:

```powershell
.\scripts\demo.ps1
.\scripts\demo.ps1 -LossPct 5
.\scripts\demo-multinode.ps1
.\scripts\demo-replay.ps1
```

Cross-language CRC golden: `ctest` / `zcmesh_test_frame` and `.\gradlew.bat golden`.

## Capture / replay

```powershell
.\zcmesh_capture.exe --listen 127.0.0.1:9910 --out run.zcm --seconds 8
.\zcmesh_edge.exe --transport udp --operator 127.0.0.1:9910 --duration 7
.\zcmesh_replay.exe --in run.zcm --target 127.0.0.1:9900 --transport tcp --rate 500
```

Edge finite run: `--duration SEC` (0 = forever).

See [BENCHMARK.md](BENCHMARK.md) for measured encode vs JSON numbers.

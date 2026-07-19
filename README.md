# ZCMesh — Zero-Copy Binary Telemetry Engine

C++ arena-backed packing + mesh fallback routing, Java NIO/JavaFX operator. Fixed **24-byte** LE frames. No JSON, no Protobuf, no cloud.

```
zcmesh/
  shared/           # wire_frame.h, zcm_file.h, golden_frame.hex
  core-engine/      # CMake C++17
  operator-node/    # Gradle Java 17 + JavaFX
  scripts/          # demo / soak / bench
```

## Wire frame (24 B, LE)

| Off | Field | Type |
|-----|-------|------|
| 0 | magic `0x5A43` | u16 |
| 2 | version `1` | u8 |
| 3 | flags | u8 |
| 4 | seq | u32 |
| 8 | timestamp_lo | u32 |
| 12 | node_id | u16 |
| 14 | sensor_type | u8 |
| 15 | reserved | u8 |
| 16 | raw_value | i32 |
| 20 | checksum CRC32 of `[0,20)` | u32 |

`.zcm` capture: 16-byte header (`ZCM1`) + `frame_count` × 24-byte frames (`shared/zcm_file.h`).

## Build

```powershell
cd core-engine
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

cd ..\operator-node
.\gradlew.bat classes golden
```

Binaries: `zcmesh_edge`, `zcmesh_bench`, `zcmesh_hop`, `zcmesh_ctl`, `zcmesh_capture`, `zcmesh_replay`, `zcmesh_inspect`, tests.

## Run

```powershell
# operator (UI) — telemetry :9900, stats :9909
cd operator-node
.\gradlew.bat run

# edge
cd ..\core-engine\build
.\zcmesh_edge.exe --operator 127.0.0.1:9900 --rate 1000 --batch 32
```

Headless smoke: `.\gradlew.bat smoke` then start edge. Stats scrape (while operator up): TCP connect to `127.0.0.1:9909`.

Transport: `auto` (TCP + mesh fallback with unsent-only failover), `tcp`, `udp` (direct), `mesh` (hops with preferred-path probe). Options: `--duration SEC`, `--drop-pct N`, `--batch N`, `--hop host:port` (repeatable, max 3), `--print-stats-sec N`.

## Scripts

```powershell
.\scripts\bench.ps1
.\scripts\demo.ps1
.\scripts\demo.ps1 -LossPct 5
.\scripts\demo-multinode.ps1
.\scripts\demo-replay.ps1
.\scripts\soak.ps1
.\scripts\soak-failover.ps1
.\scripts\soak-failover-cpp.ps1
.\scripts\soak-loss.ps1
.\scripts\soak-preferred-hop.ps1
.\scripts\soak-preferred-hop-cpp.ps1
```

## Benchmark

```powershell
.\core-engine\build\zcmesh_bench.exe 500000
```

Typical: **24 B** vs ~**67 B** JSON line, **~93%** encode CPU reduction.

## Design notes

- Arena (`VirtualAlloc`) — no hot-path heap
- Adaptive TCP batch `flush_at` + reconnect backoff
- Auto transport: mesh only the unsent TCP suffix; operator TCP magic-scan resync
- Mesh preferred-hop probe (~500 ms) + `hop_ok` / failover counters in `--print-stats-sec`
- Hop stamps `reserved` (hop index) and `--final` sets `LAST_HOP` + CRC rewrite
- `soak-failover.ps1`: UDP-only operator forces `auto` → mesh; asserts `mesh_failover>0`
- `soak-loss.ps1`: hop `--loss-pct` → capture → `inspect --expect-gaps-min`
- `soak-preferred-hop.ps1`: `zcmesh_ctl` SET_SKIP/CLEAR via edge `--control`
- `soak-preferred-hop-cpp.ps1` / `.sh`: same without Java (CI-gated)
- Java `ControlClient` / `MeshControl` mirrors C++ control plane
- CI gates `soak-loss` + failover-cpp + preferred-hop-cpp (Win/Linux)
- Capture `--mode udp|tcp|both` streams frames to disk (header rewritten on exit)
- `OperatorRuntime.control()` / `setHopSkip` / `clearHopSkip` for mesh control
- Inspect: per-node seq gaps, hop index / LAST_HOP, SUMMARY line
- Replay: `--pace capture` (default) follows `timestamp_lo` deltas; `--rate` for fixed Hz
- Operator: NIO TCP+UDP, SPSC ring, seq gaps, `.zcm` record (`record=path.zcm`)
- Future UI binds to `OperatorRuntime` / `OperatorSnapshot` / `NodeState` (no receive rewrite)

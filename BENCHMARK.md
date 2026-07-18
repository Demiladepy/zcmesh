# Benchmark notes (local Windows, MinGW g++ 16, measured 2026-07-18)

## Encode: binary arena pack vs JSON snprintf

Command: `zcmesh_bench 500000`

| Path | Bytes/frame | ns/op |
|------|-------------|-------|
| ZCMesh binary (`pack_frame`) | **24** | **~24** |
| JSON telemetry line (`snprintf`) | **~67** | **~336** |

- Payload size reduction: **~64%**
- Encode CPU reduction: **~93%**

These are encode-path microbenchmarks (no socket I/O). Wire frames stay fixed at 24 bytes with CRC32 integrity.

## Why this matters for judges

Traditional IoT paths often serialize to JSON/HTTP, allocate per message, and parse on the gateway. ZCMesh:

1. Pre-allocates an arena (no hot-path heap)
2. Packs dense LE structs with CRC
3. Coalesces into TCP batches (`FrameBatch`)
4. Falls back across deterministic UDP mesh hops
5. Decodes on the operator with NIO `ByteBuffer` absolute reads

## Reproduce

```powershell
cd core-engine
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
.\build\zcmesh_bench.exe 500000
```

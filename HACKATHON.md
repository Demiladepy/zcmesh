# ZCMesh — Hackathon Judge Brief

**Zero-Copy Binary Telemetry Engine & Local Mesh-Routing Proxy**

No AI wrappers. No mock dashboards. No JSON/HTTP telemetry path.

## What it is

A production-shaped edge stack for high-frequency sensor streams on constrained links:

1. **C++ core** — arena allocator, 24-byte CRC frames, adaptive TCP batching, deterministic UDP mesh fallback
2. **Java operator** — NIO TCP+UDP decode, SPSC ring, seq-gap + ring-drop counters, live stats port

## Killer demo (3 minutes)

### 1) Throughput / size punchline

```powershell
cd core-engine\build
.\zcmesh_bench.exe 500000
```

Expect ~**24 B** vs ~**67 B** JSON, **~93%** encode CPU reduction ([BENCHMARK.md](BENCHMARK.md)).

### 2) Live multi-node uplink

```powershell
.\scripts\demo-multinode.ps1
```

Operator reports `nodes=3` with rising seq.

### 3) Lossy mesh + gap counters

```powershell
.\scripts\demo.ps1 -LossPct 5
```

Hop injects deterministic drops; operator `gaps=` climbs.

### 4) Live stats scrape (no cloud)

With operator running:

```powershell
$c = New-Object Net.Sockets.TcpClient('127.0.0.1',9909)
$s = $c.GetStream(); $b = New-Object byte[] 2048
$n = $s.Read($b,0,2048); [Text.Encoding]::ASCII.GetString($b,0,$n)
$c.Close()
```

### 5) Capture → inspect → replay

```powershell
.\scripts\demo-replay.ps1
.\core-engine\build\zcmesh_inspect.exe capture-demo.zcm
```

## Architecture one-liner

`SensorSample → arena FrameBatch → TCP_NODELAY (adaptive flush_at) → [fail] UDP mesh hops → NIO ByteBuffer → FrameRing → UI/stats/.zcm`

See [ARCHITECTURE.md](ARCHITECTURE.md) and [PROTOCOL.md](PROTOCOL.md).

## Why this wins "Most Impressive Tech"

- Real memory discipline (arena, no hot-path heap)
- Real networking (non-blocking sockets, backpressure, reconnect backoff)
- Measurable wins vs JSON encoding
- Cross-language golden CRC lockstep
- Judge-reproducible scripts under `scripts/`

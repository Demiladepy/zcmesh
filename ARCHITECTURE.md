# ZCMesh Architecture

## Problem

Resource-constrained edge nodes must stream high-frequency sensor telemetry over unstable, low-bandwidth links without heap fragmentation, schema frameworks, or cloud dependency.

## Design constraints

| Constraint | Choice |
|------------|--------|
| Memory | Single arena at startup; hot path never `malloc`/`new` |
| Wire format | Fixed 24-byte LE packed struct + CRC32 |
| Serialization | Direct field writes / `ByteBuffer` absolute gets |
| Transport | TCP batch uplink preferred; UDP mesh fallback |
| Operator | Java NIO selector (TCP + UDP), SPSC frame ring |

## Data path

```
SensorSample
    -> pack_frame_into (arena FrameBatch slot)
    -> flush_tcp (one write of N*24 bytes)
    -> [on failure] MeshRouter UDP hops (9901 -> 9902 -> operator)
    -> FrameReceiver (NIO)
    -> CRC verify + seq gap track
    -> FrameRing (SPSC)
    -> Operator UI / smoke consumer
```

## C++ (`core-engine`)

- **Arena** — `VirtualAlloc` slab, bump pointer, fail-hard on exhaustion.
- **FrameBatch** — contiguous `zcmesh_wire_frame[N]` in arena; coalesce into one TCP send; resume partial sends.
- **MeshRouter** — fixed route table in arena; deterministic hop fallback; no heap maps on hot path.
- **zcmesh_hop** — standalone UDP relay with CRC verify before forward.
- **zcmesh_bench** — binary pack vs JSON `snprintf` size/CPU.

## Java (`operator-node`)

- **WireFrame** — layout mirror of `shared/wire_frame.h`.
- **FrameReceiver** — dual bind TCP+UDP port; direct buffers; stream reassembly.
- **FrameRing** — power-of-two SPSC; drops oldest under backpressure.
- **TelemetryPipeline** — CRC reject + per-node sequence gap counters.

## Non-goals

Cloud APIs, JSON/HTTP telemetry, Protobuf/FlatBuffers, AI wrappers, decorative dashboards.

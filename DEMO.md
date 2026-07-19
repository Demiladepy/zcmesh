# Judge demo script (≈3 minutes)

ZCMesh is a **24-byte LE binary telemetry pipe** with C++ edge mesh routing and a Java NIO operator — no JSON, no Protobuf, no cloud mock.

## One-liner pitch

Resource-constrained sensors stream high-rate frames over unstable links; the edge packs into a fixed wire frame, falls back across mesh hops, and the operator verifies CRC / gaps / hop stamps live.

## Live path (pick one)

### A — Mesh hop (shows routing)

```powershell
.\scripts\demo.ps1 -Seconds 12 -Rate 500
```

Expect `SMOKE_OK`. Optional loss: `.\scripts\demo.ps1 -LossPct 5`.

### B — Failover (TCP dies → mesh)

```powershell
.\scripts\soak-failover.ps1
```

Expect `mesh_failover>0` and `SMOKE_OK`.

### C — Preferred hop recovery (control plane)

```powershell
.\scripts\soak-preferred-hop.ps1
```

`zcmesh_ctl` SET_SKIP / CLEAR demotes then recovers the preferred hop.

### D — Serial 2-hop stamps (offline proof)

```powershell
.\scripts\demo-serial-hop.ps1
```

Capture → inspect: every frame `hop_idx=2`, `last_hop=100%`.

### E — File-sourced samples (not synthetic sine)

```powershell
.\scripts\soak-file-source.ps1
```

`fixtures/samples.txt` ints appear on the wire (`inspect --expect-raws`).

## Numbers to quote

```powershell
.\core-engine\build\zcmesh_bench.exe 500000
```

Measured (Release, 200k iters): **24 B**/frame vs **~65 B** JSON · **~93%** encode CPU reduction · **~63%** payload size cut.

## What judges should *not* expect

- Fancy dashboard / charts (UI is a thin live table over real counters)
- Cloud APIs or simulated telemetry
- TLS (local industrial mesh by design)

## Repo map

| Path | Role |
|------|------|
| `shared/wire_frame.h` | 24-byte contract |
| `core-engine/` | edge, hop, capture, replay, inspect |
| `operator-node/` | NIO + JavaFX operator |
| `scripts/` | demos + CI soaks |

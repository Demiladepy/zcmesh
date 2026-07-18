# ZCMesh Wire & File Protocols

## Telemetry frame (24 bytes, little-endian)

Canonical header: [`shared/wire_frame.h`](shared/wire_frame.h)

| Off | Size | Field |
|-----|------|-------|
| 0 | 2 | `magic` = `0x5A43` |
| 2 | 1 | `version` = `1` |
| 3 | 1 | `flags` (bit0 nibble-pack, bit1 last-hop) |
| 4 | 4 | `seq` |
| 8 | 4 | `timestamp_lo` (low 32 bits of monotonic ns) |
| 12 | 2 | `node_id` |
| 14 | 1 | `sensor_type` |
| 15 | 1 | `reserved` |
| 16 | 4 | `raw_value` (i32) |
| 20 | 4 | `checksum` CRC32 of bytes `[0,20)` |

Golden vector: [`shared/golden_frame.hex`](shared/golden_frame.hex) — verified by C++ `zcmesh_test_frame` and Java `WireFrameGolden`.

## Capture file (`.zcm`)

Canonical header: [`shared/zcm_file.h`](shared/zcm_file.h)

```
magic u32 = 0x314D435A ('ZCM1')
version u16 = 1
reserved u16 = 0
frame_count u64
frames[frame_count] each 24 bytes
```

Tools: `zcmesh_capture`, `zcmesh_replay`, `zcmesh_inspect`, Java `ZcmWriter`.

## Operator stats side-channel

TCP port `telemetry_port + 9` (default **9909**).

Connect, read one ASCII snapshot, connection closes:

```
zcmesh_stats 1
frames_ok=...
crc_fail=...
gaps=...
nodes=...
bytes=...
queued=...
last_seq=...
ia_ewma_ns=...
ring_drops=...
```

```powershell
# while operator is running:
bash -c "echo | nc 127.0.0.1 9909"
# or PowerShell:
$c = New-Object Net.Sockets.TcpClient('127.0.0.1',9909); $s=$c.GetStream(); $b=New-Object byte[] 1024; $n=$s.Read($b,0,1024); [Text.Encoding]::ASCII.GetString($b,0,$n); $c.Close()
```

## Transport modes (edge)

| Mode | Behavior |
|------|----------|
| `auto` | Adaptive TCP batch → UDP mesh hops on failure |
| `tcp` | TCP only, exponential reconnect backoff |
| `udp` | UDP datagrams direct to `--operator` |
| `mesh` | UDP via hops `9901 → 9902 → operator` |

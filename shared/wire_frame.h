#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Little-endian packed telemetry frame (24 bytes).
 *
 *  magic         u16  @0
 *  version       u8   @2
 *  flags         u8   @3
 *  seq           u32  @4
 *  timestamp_lo  u32  @8   low 32 bits of monotonic ns
 *  node_id       u16  @12
 *  sensor_type   u8   @14
 *  reserved      u8   @15  hop index (0 at edge; incremented per mesh hop)
 *  raw_value     i32  @16
 *  checksum      u32  @20  CRC32 of first 20 bytes
 */

#define ZCMESH_WIRE_MAGIC       0x5A43u
#define ZCMESH_WIRE_VERSION     1u
#define ZCMESH_WIRE_FRAME_SIZE  24u

#define ZCMESH_FLAG_NIBBLE_PACK 0x01u
#define ZCMESH_FLAG_LAST_HOP    0x02u  /* set only on the final hop (or direct uplink) */

#define ZCMESH_OFF_MAGIC        0u
#define ZCMESH_OFF_VERSION      2u
#define ZCMESH_OFF_FLAGS        3u
#define ZCMESH_OFF_SEQ          4u
#define ZCMESH_OFF_TIMESTAMP_LO 8u
#define ZCMESH_OFF_NODE_ID      12u
#define ZCMESH_OFF_SENSOR_TYPE  14u
#define ZCMESH_OFF_RESERVED     15u
#define ZCMESH_OFF_RAW_VALUE    16u
#define ZCMESH_OFF_CHECKSUM     20u

#define ZCMESH_CRC_SEED         0xFFFFFFFFu
#define ZCMESH_CRC_PAYLOAD_LEN  20u

enum zcmesh_sensor_type {
    ZCMESH_SENSOR_VOLTAGE  = 0,
    ZCMESH_SENSOR_CURRENT  = 1,
    ZCMESH_SENSOR_TEMP     = 2,
    ZCMESH_SENSOR_PRESSURE = 3,
    ZCMESH_SENSOR_GENERIC  = 255
};

#pragma pack(push, 1)
typedef struct zcmesh_wire_frame {
    uint16_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint32_t seq;
    uint32_t timestamp_lo;
    uint16_t node_id;
    uint8_t  sensor_type;
    uint8_t  reserved;
    int32_t  raw_value;
    uint32_t checksum;
} zcmesh_wire_frame;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(zcmesh_wire_frame) == ZCMESH_WIRE_FRAME_SIZE,
              "zcmesh_wire_frame must be exactly 24 bytes");
#endif

#ifdef __cplusplus
}
#endif

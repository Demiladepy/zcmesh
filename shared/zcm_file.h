#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On-disk capture format (.zcm): little-endian.
 *   magic       u32  'ZCM1' = 0x314D435A
 *   version     u16  1
 *   reserved    u16  0
 *   frame_count u64
 *   frames[frame_count]  each ZCMESH_WIRE_FRAME_SIZE bytes
 */

#define ZCMESH_ZCM_MAGIC   0x314D435Au
#define ZCMESH_ZCM_VERSION 1u

#pragma pack(push, 1)
typedef struct zcmesh_zcm_header {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint64_t frame_count;
} zcmesh_zcm_header;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(zcmesh_zcm_header) == 16, "zcm header must be 16 bytes");
#endif

#ifdef __cplusplus
}
#endif

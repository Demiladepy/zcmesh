#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tiny LE mesh control datagram (8 bytes) → edge --control port.
 *
 *  magic   u16  @0  0x434D ('MC')
 *  version u8   @2  1
 *  opcode  u8   @3  see below
 *  node_id u16  @4
 *  mask    u8   @6  hop skip bitmask (opcode SET_SKIP)
 *  reserved u8  @7
 */

#define ZCMESH_CTRL_MAGIC   0x434Du
#define ZCMESH_CTRL_VERSION 1u
#define ZCMESH_CTRL_SIZE    8u

#define ZCMESH_CTRL_OP_SET_SKIP 1u /* set hop_skip_mask for node_id */
#define ZCMESH_CTRL_OP_CLEAR    2u /* clear hop_skip_mask (mask ignored) */

#pragma pack(push, 1)
typedef struct zcmesh_ctrl_msg {
    uint16_t magic;
    uint8_t  version;
    uint8_t  opcode;
    uint16_t node_id;
    uint8_t  mask;
    uint8_t  reserved;
} zcmesh_ctrl_msg;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(zcmesh_ctrl_msg) == ZCMESH_CTRL_SIZE,
              "zcmesh_ctrl_msg must be 8 bytes");
#endif

#ifdef __cplusplus
}
#endif

#pragma once

#include "arena.hpp"

#include <cstdint>

#include "wire_frame.h"

namespace zcmesh {

struct SensorSample {
    uint32_t seq;
    uint64_t timestamp_ns;
    uint16_t node_id;
    uint8_t  sensor_type;
    uint8_t  flags;
    uint8_t  reserved; /* hop index; 0 at edge */
    int32_t  raw_value;
};

uint32_t crc32(const void* data, std::size_t len, uint32_t seed = ZCMESH_CRC_SEED) noexcept;

/* Pack into caller-owned storage (hot path: reuse arena slab / batch buffer). */
void pack_frame_into(zcmesh_wire_frame* out, const SensorSample& sample) noexcept;

/* Pack one sample into an arena-backed frame. Returns pointer into arena. */
zcmesh_wire_frame* pack_frame(Arena& arena, const SensorSample& sample);

bool verify_frame(const zcmesh_wire_frame& frame) noexcept;

/* Increment hop index; set/clear LAST_HOP; recompute CRC. */
void apply_hop_stamp(zcmesh_wire_frame* frame, bool final_hop) noexcept;

std::size_t nibble_pack_i32(const int32_t* values, std::size_t count, uint8_t* out16) noexcept;

} // namespace zcmesh

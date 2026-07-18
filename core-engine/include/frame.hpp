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
    int32_t  raw_value;
};

uint32_t crc32(const void* data, std::size_t len, uint32_t seed = ZCMESH_CRC_SEED) noexcept;

/* Pack one sample into an arena-backed frame. Returns pointer into arena. */
zcmesh_wire_frame* pack_frame(Arena& arena, const SensorSample& sample);

/* Verify magic/version/CRC. Returns false on failure. */
bool verify_frame(const zcmesh_wire_frame& frame) noexcept;

/* Optional nibble-pack of up to 8 int32 values into 16 bytes (flags bit0). */
std::size_t nibble_pack_i32(const int32_t* values, std::size_t count, uint8_t* out16) noexcept;

} // namespace zcmesh

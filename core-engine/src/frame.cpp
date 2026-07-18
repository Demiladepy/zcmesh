#include "frame.hpp"

#include <cstring>

namespace zcmesh {

namespace {

uint32_t crc_table[256];
bool crc_table_ready = false;

void init_crc_table() noexcept {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc_table[i] = c;
    }
    crc_table_ready = true;
}

} // namespace

uint32_t crc32(const void* data, std::size_t len, uint32_t seed) noexcept {
    if (!crc_table_ready) {
        init_crc_table();
    }
    uint32_t c = seed;
    const auto* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        c = crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

zcmesh_wire_frame* pack_frame(Arena& arena, const SensorSample& sample) {
    auto* frame = arena.alloc<zcmesh_wire_frame>();
    frame->magic = ZCMESH_WIRE_MAGIC;
    frame->version = ZCMESH_WIRE_VERSION;
    frame->flags = sample.flags;
    frame->seq = sample.seq;
    frame->timestamp_lo = static_cast<uint32_t>(sample.timestamp_ns & 0xFFFFFFFFu);
    frame->node_id = sample.node_id;
    frame->sensor_type = sample.sensor_type;
    frame->reserved = 0;
    frame->raw_value = sample.raw_value;
    frame->checksum = 0;
    frame->checksum = crc32(frame, ZCMESH_CRC_PAYLOAD_LEN);
    return frame;
}

bool verify_frame(const zcmesh_wire_frame& frame) noexcept {
    if (frame.magic != ZCMESH_WIRE_MAGIC || frame.version != ZCMESH_WIRE_VERSION) {
        return false;
    }
    const uint32_t expect = crc32(&frame, ZCMESH_CRC_PAYLOAD_LEN);
    return expect == frame.checksum;
}

std::size_t nibble_pack_i32(const int32_t* values, std::size_t count, uint8_t* out16) noexcept {
    if (!values || !out16 || count == 0 || count > 8) {
        return 0;
    }
    std::memset(out16, 0, 16);
    for (std::size_t i = 0; i < count; ++i) {
        /* Store low 16 bits of each sample as two bytes (dense short path). */
        const uint16_t v = static_cast<uint16_t>(values[i] & 0xFFFF);
        out16[i * 2] = static_cast<uint8_t>(v & 0xFF);
        out16[i * 2 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }
    return count * 2;
}

} // namespace zcmesh

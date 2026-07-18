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

void pack_frame_into(zcmesh_wire_frame* out, const SensorSample& sample) noexcept {
    out->magic = ZCMESH_WIRE_MAGIC;
    out->version = ZCMESH_WIRE_VERSION;
    out->flags = sample.flags;
    out->seq = sample.seq;
    out->timestamp_lo = static_cast<uint32_t>(sample.timestamp_ns & 0xFFFFFFFFu);
    out->node_id = sample.node_id;
    out->sensor_type = sample.sensor_type;
    out->reserved = sample.reserved;
    out->raw_value = sample.raw_value;
    out->checksum = 0;
    out->checksum = crc32(out, ZCMESH_CRC_PAYLOAD_LEN);
}

zcmesh_wire_frame* pack_frame(Arena& arena, const SensorSample& sample) {
    auto* frame = arena.alloc<zcmesh_wire_frame>();
    pack_frame_into(frame, sample);
    return frame;
}

bool verify_frame(const zcmesh_wire_frame& frame) noexcept {
    if (frame.magic != ZCMESH_WIRE_MAGIC || frame.version != ZCMESH_WIRE_VERSION) {
        return false;
    }
    const uint32_t expect = crc32(&frame, ZCMESH_CRC_PAYLOAD_LEN);
    return expect == frame.checksum;
}

void apply_hop_stamp(zcmesh_wire_frame* frame, bool final_hop) noexcept {
    if (!frame) {
        return;
    }
    if (frame->reserved < 255u) {
        ++frame->reserved;
    }
    if (final_hop) {
        frame->flags = static_cast<uint8_t>(frame->flags | ZCMESH_FLAG_LAST_HOP);
    } else {
        frame->flags = static_cast<uint8_t>(frame->flags & ~ZCMESH_FLAG_LAST_HOP);
    }
    frame->checksum = 0;
    frame->checksum = crc32(frame, ZCMESH_CRC_PAYLOAD_LEN);
}

std::size_t nibble_pack_i32(const int32_t* values, std::size_t count, uint8_t* out16) noexcept {
    if (!values || !out16 || count == 0 || count > 8) {
        return 0;
    }
    std::memset(out16, 0, 16);
    for (std::size_t i = 0; i < count; ++i) {
        const uint16_t v = static_cast<uint16_t>(values[i] & 0xFFFF);
        out16[i * 2] = static_cast<uint8_t>(v & 0xFF);
        out16[i * 2 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }
    return count * 2;
}

} // namespace zcmesh

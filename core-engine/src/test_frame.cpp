#include "arena.hpp"
#include "frame.hpp"

#include <cstdio>
#include <cstring>

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fails;
    }
}

} // namespace

int main() {
    expect(sizeof(zcmesh_wire_frame) == 24, "sizeof wire frame == 24");

    zcmesh::SensorSample s{};
    s.seq = 42;
    s.timestamp_ns = 0x1122334455667788ull;
    s.node_id = 7;
    s.sensor_type = ZCMESH_SENSOR_TEMP;
    s.flags = ZCMESH_FLAG_LAST_HOP;
    s.raw_value = -9001;

    zcmesh_wire_frame stack{};
    zcmesh::pack_frame_into(&stack, s);
    expect(stack.magic == ZCMESH_WIRE_MAGIC, "magic");
    expect(stack.version == ZCMESH_WIRE_VERSION, "version");
    expect(stack.seq == 42, "seq");
    expect(stack.timestamp_lo == 0x55667788u, "timestamp_lo low32");
    expect(stack.node_id == 7, "node_id");
    expect(stack.sensor_type == ZCMESH_SENSOR_TEMP, "sensor");
    expect(stack.raw_value == -9001, "raw");
    expect(zcmesh::verify_frame(stack), "crc ok");

    zcmesh_wire_frame corrupt = stack;
    corrupt.raw_value ^= 1;
    expect(!zcmesh::verify_frame(corrupt), "crc detects mutation");

    zcmesh::Arena arena(4096);
    zcmesh_wire_frame* heapish = zcmesh::pack_frame(arena, s);
    expect(heapish != nullptr, "arena pack");
    expect(zcmesh::verify_frame(*heapish), "arena frame crc");
    expect(arena.used() >= ZCMESH_WIRE_FRAME_SIZE, "arena used");

    uint8_t packed[16];
    int32_t vals[4] = {1, 2, 3, 4};
    expect(zcmesh::nibble_pack_i32(vals, 4, packed) == 8, "nibble pack len");

    if (g_fails == 0) {
        std::printf("zcmesh_test_frame: OK\n");
        return 0;
    }
    std::printf("zcmesh_test_frame: %d failure(s)\n", g_fails);
    return 1;
}

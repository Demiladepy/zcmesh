#include "arena.hpp"
#include "frame.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

uint64_t now_ns() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count());
}

/* Typical edge JSON telemetry line (no schema framework — snprintf only). */
std::size_t encode_json(char* out, std::size_t cap, const zcmesh::SensorSample& s) {
    const int n = std::snprintf(
        out, cap,
        "{\"ts\":%llu,\"node\":%u,\"type\":%u,\"value\":%d,\"seq\":%u}",
        static_cast<unsigned long long>(s.timestamp_ns),
        static_cast<unsigned>(s.node_id),
        static_cast<unsigned>(s.sensor_type),
        s.raw_value,
        s.seq);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iters = (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 500000ull;

    zcmesh::Arena arena(64u * 1024u * 1024u);
    char json_buf[256];

    zcmesh::SensorSample sample{};
    sample.node_id = 1;
    sample.sensor_type = ZCMESH_SENSOR_VOLTAGE;
    sample.flags = 0;
    sample.raw_value = 1234;

    volatile uint32_t sink = 0;
    volatile std::size_t json_bytes = 0;

    const uint64_t t0 = now_ns();
    for (std::size_t i = 0; i < iters; ++i) {
        if (arena.remaining() < ZCMESH_WIRE_FRAME_SIZE * 8) {
            arena.reset();
        }
        sample.seq = static_cast<uint32_t>(i);
        sample.timestamp_ns = t0 + i;
        sample.raw_value = static_cast<int32_t>(i & 0xFFFF);
        zcmesh_wire_frame* f = zcmesh::pack_frame(arena, sample);
        sink ^= f->checksum;
    }
    const uint64_t t1 = now_ns();

    for (std::size_t i = 0; i < iters; ++i) {
        sample.seq = static_cast<uint32_t>(i);
        sample.timestamp_ns = t0 + i;
        sample.raw_value = static_cast<int32_t>(i & 0xFFFF);
        const std::size_t n = encode_json(json_buf, sizeof(json_buf), sample);
        json_bytes = n;
        sink ^= static_cast<uint32_t>(json_buf[0]) ^ static_cast<uint32_t>(n);
    }
    const uint64_t t2 = now_ns();

    const double bin_ns = static_cast<double>(t1 - t0) / static_cast<double>(iters);
    const double json_ns = static_cast<double>(t2 - t1) / static_cast<double>(iters);
    const double payload_ratio =
        (json_bytes > 0) ? (100.0 * (1.0 - (static_cast<double>(ZCMESH_WIRE_FRAME_SIZE) / static_cast<double>(json_bytes))))
                         : 0.0;
    const double cpu_ratio = (json_ns > 0.0) ? (100.0 * (1.0 - (bin_ns / json_ns))) : 0.0;

    std::printf("zcmesh bench iters=%zu sink=%u\n", iters, sink);
    std::printf("binary:  %u B/frame  %.2f ns/op\n", ZCMESH_WIRE_FRAME_SIZE, bin_ns);
    std::printf("json:    %zu B/frame  %.2f ns/op\n", static_cast<std::size_t>(json_bytes), json_ns);
    std::printf("payload_size_reduction: %.1f%%\n", payload_ratio);
    std::printf("encode_cpu_reduction:   %.1f%%\n", cpu_ratio);
    return 0;
}

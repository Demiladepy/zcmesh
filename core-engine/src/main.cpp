#include "arena.hpp"
#include "frame.hpp"
#include "net.hpp"
#include "router.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

uint64_t monotonic_ns() {
#if defined(_WIN32)
    static LARGE_INTEGER freq = {};
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(
        (now.QuadPart * 1000000000ull) / static_cast<uint64_t>(freq.QuadPart));
#else
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count());
#endif
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s [--operator host:port] [--node-id N] [--rate Hz] [--file path] [--stdin]\n"
                 "  Streams 24-byte ZCMesh frames to operator TCP; UDP mesh fallback hops configured in-process.\n",
                 argv0);
}

} // namespace

int main(int argc, char** argv) {
    const char* operator_ep = "127.0.0.1:9900";
    uint16_t node_id = 1;
    double rate_hz = 1000.0;
    const char* file_path = nullptr;
    bool use_stdin = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--operator") == 0 && i + 1 < argc) {
            operator_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--node-id") == 0 && i + 1 < argc) {
            node_id = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            rate_hz = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (std::strcmp(argv[i], "--stdin") == 0) {
            use_stdin = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!zcmesh::net_init()) {
        std::fprintf(stderr, "net_init failed\n");
        return 1;
    }

    constexpr std::size_t kArenaBytes = 4u * 1024u * 1024u;
    zcmesh::Arena arena(kArenaBytes);

    zcmesh::Endpoint op{};
    if (!zcmesh::parse_endpoint(operator_ep, op)) {
        std::fprintf(stderr, "invalid --operator endpoint\n");
        zcmesh::net_shutdown();
        return 1;
    }

    /* Deterministic fallback topology: primary mesh hop → secondary → operator UDP. */
    zcmesh::Endpoint hops[3]{};
    zcmesh::parse_endpoint("127.0.0.1:9901", hops[0]);
    zcmesh::parse_endpoint("127.0.0.1:9902", hops[1]);
    hops[2] = op;

    zcmesh::MeshRouter router(arena, 8);
    router.add_route(node_id, hops, 3);

    zcmesh::UdpSocket udp;
    if (!udp.open()) {
        std::fprintf(stderr, "udp open failed\n");
        zcmesh::net_shutdown();
        return 1;
    }

    zcmesh::TcpClient tcp;
    if (!tcp.connect(op)) {
        std::fprintf(stderr, "tcp connect to %s:%u failed (will retry via UDP mesh)\n",
                     op.host, op.port);
    } else {
        std::fprintf(stderr, "tcp uplink connected %s:%u\n", op.host, op.port);
    }

    std::ifstream file_in;
    if (file_path) {
        file_in.open(file_path);
        if (!file_in) {
            std::fprintf(stderr, "cannot open --file %s\n", file_path);
            zcmesh::net_shutdown();
            return 1;
        }
    }

    const auto period = std::chrono::duration<double>(1.0 / (rate_hz > 0 ? rate_hz : 1.0));
    uint32_t seq = 0;
    uint64_t sent_ok = 0;
    uint64_t sent_fail = 0;
    auto next = std::chrono::steady_clock::now();

    std::fprintf(stderr,
                 "zcmesh_edge node=%u rate=%.1fHz frame=%uB arena=%zuB (hardware-clock stream)\n",
                 node_id, rate_hz, ZCMESH_WIRE_FRAME_SIZE, arena.capacity());

    while (true) {
        if (arena.remaining() < ZCMESH_WIRE_FRAME_SIZE * 64) {
            arena.reset();
        }

        int32_t raw = 0;
        if (use_stdin) {
            if (!(std::cin >> raw)) {
                break;
            }
        } else if (file_path) {
            if (!(file_in >> raw)) {
                break;
            }
        } else {
            /* Deterministic hardware-timed waveform for soak / bench (not a mock API). */
            const double t = static_cast<double>(seq) / rate_hz;
            raw = static_cast<int32_t>(std::lround(1000.0 * std::sin(2.0 * 3.141592653589793 * 50.0 * t)));
        }

        zcmesh::SensorSample sample{};
        sample.seq = seq++;
        sample.timestamp_ns = monotonic_ns();
        sample.node_id = node_id;
        sample.sensor_type = ZCMESH_SENSOR_VOLTAGE;
        sample.flags = ZCMESH_FLAG_LAST_HOP;
        sample.raw_value = raw;

        zcmesh_wire_frame* frame = zcmesh::pack_frame(arena, sample);
        const bool ok = router.deliver(udp, tcp, node_id, frame, ZCMESH_WIRE_FRAME_SIZE, true);
        if (ok) {
            ++sent_ok;
        } else {
            ++sent_fail;
            if (!tcp.connected()) {
                tcp.connect(op);
            }
        }

        if ((seq & 0x3FFu) == 0) {
            std::fprintf(stderr, "seq=%u ok=%llu fail=%llu arena_used=%zu\n",
                         seq,
                         static_cast<unsigned long long>(sent_ok),
                         static_cast<unsigned long long>(sent_fail),
                         arena.used());
        }

        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        std::this_thread::sleep_until(next);
    }

    zcmesh::net_shutdown();
    return 0;
}

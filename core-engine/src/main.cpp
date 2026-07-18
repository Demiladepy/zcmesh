#include "arena.hpp"
#include "batch.hpp"
#include "frame.hpp"
#include "net.hpp"
#include "router.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

enum class Transport { Auto, Tcp, Udp, Mesh };

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
                 "Usage: %s [--operator host:port] [--node-id N] [--rate Hz] [--batch N]\n"
                 "          [--transport auto|tcp|udp|mesh] [--duration SEC] [--file path] [--stdin]\n"
                 "  auto: TCP then mesh fallback; tcp: TCP only; udp: UDP to --operator; mesh: UDP hops only.\n",
                 argv0);
}

bool flush_udp_direct(zcmesh::UdpSocket& udp, const zcmesh::Endpoint& ep, zcmesh::FrameBatch& batch) {
    const std::size_t n = batch.count();
    const auto* frames = static_cast<const zcmesh_wire_frame*>(batch.data());
    std::size_t forwarded = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (udp.send_to(ep, &frames[i], ZCMESH_WIRE_FRAME_SIZE)) {
            ++forwarded;
        }
    }
    batch.clear();
    return forwarded == n;
}

bool flush_udp_mesh(zcmesh::MeshRouter& router, zcmesh::UdpSocket& udp, uint16_t node_id,
                    zcmesh::FrameBatch& batch) {
    const std::size_t n = batch.count();
    const auto* frames = static_cast<const zcmesh_wire_frame*>(batch.data());
    std::size_t forwarded = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (router.forward_udp(udp, node_id, &frames[i], ZCMESH_WIRE_FRAME_SIZE)) {
            ++forwarded;
        }
    }
    batch.clear();
    return forwarded == n;
}

} // namespace

int main(int argc, char** argv) {
    const char* operator_ep = "127.0.0.1:9900";
    uint16_t node_id = 1;
    double rate_hz = 1000.0;
    std::size_t batch_size = 32;
    const char* file_path = nullptr;
    bool use_stdin = false;
    Transport transport = Transport::Auto;
    double duration_sec = 0.0; /* 0 = run forever */

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--operator") == 0 && i + 1 < argc) {
            operator_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--node-id") == 0 && i + 1 < argc) {
            node_id = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            rate_hz = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--batch") == 0 && i + 1 < argc) {
            batch_size = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
            if (batch_size == 0) {
                batch_size = 1;
            }
        } else if (std::strcmp(argv[i], "--transport") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "tcp") == 0) {
                transport = Transport::Tcp;
            } else if (std::strcmp(argv[i], "udp") == 0) {
                transport = Transport::Udp;
            } else if (std::strcmp(argv[i], "mesh") == 0) {
                transport = Transport::Mesh;
            } else if (std::strcmp(argv[i], "auto") == 0) {
                transport = Transport::Auto;
            } else {
                usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_sec = std::atof(argv[++i]);
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

    zcmesh::Endpoint hops[3]{};
    zcmesh::parse_endpoint("127.0.0.1:9901", hops[0]);
    zcmesh::parse_endpoint("127.0.0.1:9902", hops[1]);
    hops[2] = op;

    zcmesh::MeshRouter router(arena, 8);
    router.add_route(node_id, hops, 3);

    zcmesh::FrameBatch batch(arena, batch_size);

    zcmesh::UdpSocket udp;
    if (!udp.open()) {
        std::fprintf(stderr, "udp open failed\n");
        zcmesh::net_shutdown();
        return 1;
    }

    zcmesh::TcpClient tcp;
    if (transport != Transport::Udp && transport != Transport::Mesh) {
        if (!tcp.connect(op)) {
            std::fprintf(stderr, "tcp connect to %s:%u failed\n", op.host, op.port);
            if (transport == Transport::Tcp) {
                zcmesh::net_shutdown();
                return 1;
            }
        } else {
            std::fprintf(stderr, "tcp uplink connected %s:%u (TCP_NODELAY)\n", op.host, op.port);
        }
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
    const auto start = next;
    const bool timed = duration_sec > 0.0;

    const char* tname = transport == Transport::Tcp ? "tcp"
                      : transport == Transport::Udp ? "udp"
                      : transport == Transport::Mesh ? "mesh" : "auto";
    std::fprintf(stderr,
                 "zcmesh_edge node=%u rate=%.1fHz batch=%zu transport=%s duration=%.1fs frame=%uB\n",
                 node_id, rate_hz, batch_size, tname, duration_sec, ZCMESH_WIRE_FRAME_SIZE);

    while (true) {
        if (timed) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= duration_sec) {
                break;
            }
        }

        /* Drain any back-pressured batch before accepting new samples. */
        if (batch.full() || batch.has_partial_send()) {
            const std::size_t n = batch.count();
            bool ok = false;
            if (transport == Transport::Udp) {
                ok = flush_udp_direct(udp, op, batch);
            } else if (transport == Transport::Mesh) {
                ok = flush_udp_mesh(router, udp, node_id, batch);
            } else if (transport == Transport::Tcp) {
                if (!tcp.connected()) {
                    tcp.connect(op);
                }
                ok = tcp.connected() && batch.flush_tcp_retry(tcp, 128);
            } else {
                if (tcp.connected() && batch.flush_tcp_retry(tcp, 64)) {
                    ok = true;
                } else {
                    ok = flush_udp_mesh(router, udp, node_id, batch);
                    if (!tcp.connected()) {
                        tcp.connect(op);
                    }
                }
            }
            if (ok) {
                sent_ok += n;
            } else if (batch.empty()) {
                sent_ok += n; /* partial accounting: emptied via mesh */
            } else {
                /* Still full — yield and retry next tick without dropping. */
                ++sent_fail;
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
                std::this_thread::sleep_until(next);
                continue;
            }
        }

        int32_t raw = 0;
        uint8_t sensor = ZCMESH_SENSOR_VOLTAGE;
        if (use_stdin) {
            if (!(std::cin >> raw)) {
                break;
            }
        } else if (file_path) {
            if (!(file_in >> raw)) {
                break;
            }
        } else {
            const double t = static_cast<double>(seq) / rate_hz;
            const int lane = static_cast<int>(seq % 3u);
            if (lane == 0) {
                sensor = ZCMESH_SENSOR_VOLTAGE;
                raw = static_cast<int32_t>(std::lround(1000.0 * std::sin(2.0 * 3.141592653589793 * 50.0 * t)));
            } else if (lane == 1) {
                sensor = ZCMESH_SENSOR_CURRENT;
                raw = static_cast<int32_t>(std::lround(500.0 * std::sin(2.0 * 3.141592653589793 * 50.0 * t + 1.0)));
            } else {
                sensor = ZCMESH_SENSOR_TEMP;
                raw = static_cast<int32_t>(250 + std::lround(20.0 * std::sin(2.0 * 3.141592653589793 * 0.1 * t)));
            }
        }

        zcmesh::SensorSample sample{};
        sample.seq = seq++;
        sample.timestamp_ns = monotonic_ns();
        sample.node_id = node_id;
        sample.sensor_type = sensor;
        sample.flags = ZCMESH_FLAG_LAST_HOP;
        sample.raw_value = raw;

        if (!batch.push(sample)) {
            /* Should not happen after drain; spin once. */
            continue;
        }

        if (batch.full()) {
            const std::size_t n = batch.count();
            bool ok = false;
            if (transport == Transport::Udp) {
                ok = flush_udp_direct(udp, op, batch);
            } else if (transport == Transport::Mesh) {
                ok = flush_udp_mesh(router, udp, node_id, batch);
            } else if (transport == Transport::Tcp) {
                if (!tcp.connected()) {
                    tcp.connect(op);
                }
                ok = tcp.connected() && batch.flush_tcp_retry(tcp, 128);
            } else {
                if (tcp.connected() && batch.flush_tcp_retry(tcp, 64)) {
                    ok = true;
                } else {
                    ok = flush_udp_mesh(router, udp, node_id, batch);
                    if (!tcp.connected()) {
                        tcp.connect(op);
                    }
                }
            }
            if (ok) {
                sent_ok += n;
            } else if (!batch.empty()) {
                /* Keep frames; next loop drains. */
            } else {
                sent_fail += n;
            }
        }

        if ((seq & 0x3FFu) == 0) {
            std::fprintf(stderr, "seq=%u ok=%llu fail=%llu\n",
                         seq,
                         static_cast<unsigned long long>(sent_ok),
                         static_cast<unsigned long long>(sent_fail));
        }

        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        std::this_thread::sleep_until(next);
    }

    if (!batch.empty()) {
        if (transport == Transport::Udp) {
            flush_udp_direct(udp, op, batch);
        } else if (transport == Transport::Mesh) {
            flush_udp_mesh(router, udp, node_id, batch);
        } else if (!(tcp.connected() && batch.flush_tcp_retry(tcp, 256))) {
            flush_udp_mesh(router, udp, node_id, batch);
        }
    }

    std::fprintf(stderr, "zcmesh_edge done seq=%u ok=%llu fail=%llu\n",
                 seq,
                 static_cast<unsigned long long>(sent_ok),
                 static_cast<unsigned long long>(sent_fail));
    zcmesh::net_shutdown();
    return 0;
}

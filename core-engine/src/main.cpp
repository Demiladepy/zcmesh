#include "arena.hpp"
#include "batch.hpp"
#include "frame.hpp"
#include "mesh_control.h"
#include "net.hpp"
#include "router.hpp"

#include <algorithm>
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
                 "          [--transport auto|tcp|udp|mesh] [--duration SEC] [--drop-pct N]\n"
                 "          [--print-stats-sec N] [--hop host:port]... [--control host:port]\n"
                 "          [--hop-skip-file path] [--file path] [--stdin]\n"
                 "  Adaptive TCP batching + exponential reconnect. Repeat --hop to set mesh path.\n"
                 "  --control: UDP mesh control (SET_SKIP/CLEAR). --hop-skip-file kept for soaks.\n",
                 argv0);
}

void poll_control(zcmesh::UdpSocket& ctrl, zcmesh::MeshRouter& router) {
    uint8_t buf[64];
    for (;;) {
        const int n = ctrl.recv_nb(buf, sizeof(buf));
        if (n == 0) {
            return;
        }
        if (n < 0) {
            return;
        }
        if (n < static_cast<int>(ZCMESH_CTRL_SIZE)) {
            continue;
        }
        zcmesh_ctrl_msg msg{};
        std::memcpy(&msg, buf, ZCMESH_CTRL_SIZE);
        if (msg.magic != ZCMESH_CTRL_MAGIC || msg.version != ZCMESH_CTRL_VERSION) {
            continue;
        }
        if (msg.opcode == ZCMESH_CTRL_OP_SET_SKIP) {
            router.set_hop_skip_mask(msg.node_id, msg.mask);
            std::fprintf(stderr, "ctrl SET_SKIP node=%u mask=%u\n", msg.node_id, msg.mask);
        } else if (msg.opcode == ZCMESH_CTRL_OP_CLEAR) {
            router.set_hop_skip_mask(msg.node_id, 0);
            std::fprintf(stderr, "ctrl CLEAR node=%u\n", msg.node_id);
        }
    }
}

bool flush_udp_direct(zcmesh::UdpSocket& udp, const zcmesh::Endpoint& ep, zcmesh::FrameBatch& batch) {
    batch.discard_fully_sent();
    const std::size_t n = batch.unsent_count();
    const auto* frames = batch.unsent_frames();
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
    batch.discard_fully_sent();
    const std::size_t n = batch.unsent_count();
    const auto* frames = batch.unsent_frames();
    const uint64_t now = monotonic_ns();
    std::size_t forwarded = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (router.forward_udp(udp, node_id, &frames[i], ZCMESH_WIRE_FRAME_SIZE, now)) {
            ++forwarded;
        }
    }
    batch.clear();
    return forwarded == n;
}

bool should_drop(uint32_t seq, int drop_pct) {
    if (drop_pct <= 0) {
        return false;
    }
    if (drop_pct >= 100) {
        return true;
    }
    const uint32_t h = seq * 2654435761u;
    return static_cast<int>((h >> 24) % 100u) < drop_pct;
}

struct ReconnectGate {
    uint64_t next_ns = 0;
    int backoff_ms = 50;

    bool try_connect(zcmesh::TcpClient& tcp, const zcmesh::Endpoint& ep) {
        if (tcp.connected()) {
            return true;
        }
        const uint64_t now = monotonic_ns();
        if (now < next_ns) {
            return false;
        }
        if (tcp.connect(ep)) {
            backoff_ms = 50;
            next_ns = 0;
            std::fprintf(stderr, "tcp uplink reconnected %s:%u\n", ep.host, ep.port);
            return true;
        }
        next_ns = now + static_cast<uint64_t>(backoff_ms) * 1000000ull;
        backoff_ms = std::min(2000, backoff_ms * 2);
        return false;
    }
};

struct FlushStats {
    uint64_t mesh_failover = 0;
    uint64_t tcp_partial_abort = 0;
    uint64_t mesh_rescue_frames = 0;
};

bool flush_batch(Transport transport, zcmesh::FrameBatch& batch, zcmesh::TcpClient& tcp,
                 zcmesh::UdpSocket& udp, zcmesh::MeshRouter& router, const zcmesh::Endpoint& op,
                 uint16_t node_id, ReconnectGate& gate, FlushStats& stats) {
    const uint64_t t0 = monotonic_ns();
    bool ok = false;
    if (transport == Transport::Udp) {
        ok = flush_udp_direct(udp, op, batch);
    } else if (transport == Transport::Mesh) {
        ok = flush_udp_mesh(router, udp, node_id, batch);
    } else if (transport == Transport::Tcp) {
        gate.try_connect(tcp, op);
        ok = tcp.connected() && batch.flush_tcp_retry(tcp, 128);
        batch.adapt(ok, monotonic_ns() - t0);
    } else {
        gate.try_connect(tcp, op);
        if (tcp.connected() && batch.flush_tcp_retry(tcp, 64)) {
            ok = true;
            batch.adapt(true, monotonic_ns() - t0);
        } else {
            batch.adapt(false, monotonic_ns() - t0);
            const bool had_partial = batch.has_partial_send();
            const std::size_t rescue = batch.unsent_count();
            ++stats.mesh_failover;
            stats.mesh_rescue_frames += rescue;
            /* Only mesh-forward frames TCP did not fully drain (avoids dup of sent prefix). */
            ok = flush_udp_mesh(router, udp, node_id, batch);
            if (had_partial && tcp.connected()) {
                tcp.close(); /* abandon mid-frame TCP stream; reconnect clean */
                ++stats.tcp_partial_abort;
            }
        }
    }
    return ok;
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
    double duration_sec = 0.0;
    int drop_pct = 0;
    double print_stats_sec = 0.0;
    zcmesh::Endpoint hop_override[3]{};
    uint8_t hop_override_count = 0;
    const char* hop_skip_file = nullptr;
    const char* control_ep = nullptr;

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
        } else if (std::strcmp(argv[i], "--drop-pct") == 0 && i + 1 < argc) {
            drop_pct = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--print-stats-sec") == 0 && i + 1 < argc) {
            print_stats_sec = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--hop") == 0 && i + 1 < argc) {
            if (hop_override_count >= 3) {
                std::fprintf(stderr, "at most 3 --hop entries\n");
                return 1;
            }
            if (!zcmesh::parse_endpoint(argv[++i], hop_override[hop_override_count])) {
                std::fprintf(stderr, "invalid --hop endpoint\n");
                return 1;
            }
            ++hop_override_count;
        } else if (std::strcmp(argv[i], "--control") == 0 && i + 1 < argc) {
            control_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--hop-skip-file") == 0 && i + 1 < argc) {
            hop_skip_file = argv[++i];
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
    uint8_t hop_count = 0;
    if (hop_override_count > 0) {
        for (uint8_t i = 0; i < hop_override_count; ++i) {
            hops[i] = hop_override[i];
        }
        hop_count = hop_override_count;
    } else {
        zcmesh::parse_endpoint("127.0.0.1:9901", hops[0]);
        zcmesh::parse_endpoint("127.0.0.1:9902", hops[1]);
        hops[2] = op;
        hop_count = 3;
    }

    zcmesh::MeshRouter router(arena, 8);
    router.add_route(node_id, hops, hop_count);

    zcmesh::FrameBatch batch(arena, batch_size);

    zcmesh::UdpSocket udp;
    if (!udp.open()) {
        std::fprintf(stderr, "udp open failed\n");
        zcmesh::net_shutdown();
        return 1;
    }

    zcmesh::UdpSocket ctrl;
    bool ctrl_on = false;
    if (control_ep) {
        zcmesh::Endpoint cep{};
        if (!zcmesh::parse_endpoint(control_ep, cep) || !ctrl.open() || !ctrl.bind(cep)) {
            std::fprintf(stderr, "control bind failed %s\n", control_ep);
            zcmesh::net_shutdown();
            return 1;
        }
        ctrl_on = true;
        std::fprintf(stderr, "mesh control listening %s:%u\n", cep.host, cep.port);
    }

    ReconnectGate gate;
    zcmesh::TcpClient tcp;
    if (transport != Transport::Udp && transport != Transport::Mesh) {
        if (!gate.try_connect(tcp, op)) {
            std::fprintf(stderr, "tcp connect to %s:%u failed (will backoff-retry)\n", op.host, op.port);
            if (transport == Transport::Tcp) {
                /* stay up and retry — do not hard-exit */
            }
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
    uint64_t dropped = 0;
    uint64_t prev_ok = 0;
    FlushStats flush_stats{};
    auto next = std::chrono::steady_clock::now();
    const auto start = next;
    auto next_stats = start;
    const bool timed = duration_sec > 0.0;
    const bool stats_tick = print_stats_sec > 0.0;
    if (stats_tick) {
        next_stats += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(print_stats_sec));
    }

    const char* tname = transport == Transport::Tcp ? "tcp"
                      : transport == Transport::Udp ? "udp"
                      : transport == Transport::Mesh ? "mesh" : "auto";
    std::fprintf(stderr,
                 "zcmesh_edge node=%u rate=%.1fHz batch=%zu transport=%s drop_pct=%d frame=%uB\n",
                 node_id, rate_hz, batch_size, tname, drop_pct, ZCMESH_WIRE_FRAME_SIZE);

    while (true) {
        if (ctrl_on) {
            poll_control(ctrl, router);
        }
        if (hop_skip_file) {
            uint8_t mask = 0;
            FILE* sf = std::fopen(hop_skip_file, "rb");
            if (sf) {
                int v = 0;
                if (std::fscanf(sf, "%d", &v) == 1 && v > 0) {
                    mask = static_cast<uint8_t>(v & 0xFF);
                }
                std::fclose(sf);
            }
            router.set_hop_skip_mask(node_id, mask);
        }

        if (timed) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= duration_sec) {
                break;
            }
        }

        if (batch.should_flush() || batch.has_partial_send() || batch.full()) {
            const std::size_t n = batch.count();
            const bool ok = flush_batch(transport, batch, tcp, udp, router, op, node_id, gate,
                                        flush_stats);
            if (ok) {
                sent_ok += n;
            } else if (batch.empty()) {
                sent_ok += n;
            } else {
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

        if (should_drop(seq, drop_pct)) {
            ++seq;
            ++dropped;
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
            std::this_thread::sleep_until(next);
            continue;
        }

        zcmesh::SensorSample sample{};
        sample.seq = seq++;
        sample.timestamp_ns = monotonic_ns();
        sample.node_id = node_id;
        sample.sensor_type = sensor;
        /* Direct TCP/UDP uplink is last hop; mesh/auto leave LAST_HOP for hop --final. */
        sample.flags = (transport == Transport::Tcp || transport == Transport::Udp)
                           ? ZCMESH_FLAG_LAST_HOP
                           : 0;
        sample.reserved = 0;
        sample.raw_value = raw;

        if (!batch.push(sample)) {
            continue;
        }

        if (batch.should_flush() || batch.full()) {
            const std::size_t n = batch.count();
            const bool ok = flush_batch(transport, batch, tcp, udp, router, op, node_id, gate,
                                        flush_stats);
            if (ok) {
                sent_ok += n;
            } else if (!batch.empty()) {
                /* retain for drain path */
            } else {
                sent_fail += n;
            }
        }

        if ((seq & 0x3FFu) == 0) {
            std::fprintf(stderr, "seq=%u ok=%llu fail=%llu drop=%llu flush_at=%zu\n",
                         seq,
                         static_cast<unsigned long long>(sent_ok),
                         static_cast<unsigned long long>(sent_fail),
                         static_cast<unsigned long long>(dropped),
                         batch.flush_at());
        }

        if (stats_tick && std::chrono::steady_clock::now() >= next_stats) {
            const uint64_t delta = sent_ok - prev_ok;
            const double fps = static_cast<double>(delta) / print_stats_sec;
            const double bps = fps * static_cast<double>(ZCMESH_WIRE_FRAME_SIZE);
            const zcmesh::RouteEntry* route = router.find(node_id);
            const uint64_t hop0 = route ? route->hop_ok[0] : 0;
            const uint64_t hop1 = route ? route->hop_ok[1] : 0;
            const uint64_t hop2 = route ? route->hop_ok[2] : 0;
            std::fprintf(stderr,
                         "stats fps=%.0f bytes_s=%.0f ok=%llu fail=%llu drop=%llu flush_at=%zu"
                         " hop=%u route_fail=%llu hop_ok=%llu/%llu/%llu"
                         " mesh_failover=%llu tcp_abort=%llu rescue=%llu\n",
                         fps, bps,
                         static_cast<unsigned long long>(sent_ok),
                         static_cast<unsigned long long>(sent_fail),
                         static_cast<unsigned long long>(dropped),
                         batch.flush_at(),
                         route ? static_cast<unsigned>(route->active_hop) : 0u,
                         route ? static_cast<unsigned long long>(route->fails) : 0ull,
                         static_cast<unsigned long long>(hop0),
                         static_cast<unsigned long long>(hop1),
                         static_cast<unsigned long long>(hop2),
                         static_cast<unsigned long long>(flush_stats.mesh_failover),
                         static_cast<unsigned long long>(flush_stats.tcp_partial_abort),
                         static_cast<unsigned long long>(flush_stats.mesh_rescue_frames));
            prev_ok = sent_ok;
            next_stats += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(print_stats_sec));
        }

        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        std::this_thread::sleep_until(next);
    }

    if (!batch.empty()) {
        flush_batch(transport, batch, tcp, udp, router, op, node_id, gate, flush_stats);
    }

    std::fprintf(stderr,
                 "zcmesh_edge done seq=%u ok=%llu fail=%llu drop=%llu"
                 " mesh_failover=%llu tcp_abort=%llu rescue=%llu\n",
                 seq,
                 static_cast<unsigned long long>(sent_ok),
                 static_cast<unsigned long long>(sent_fail),
                 static_cast<unsigned long long>(dropped),
                 static_cast<unsigned long long>(flush_stats.mesh_failover),
                 static_cast<unsigned long long>(flush_stats.tcp_partial_abort),
                 static_cast<unsigned long long>(flush_stats.mesh_rescue_frames));
    zcmesh::net_shutdown();
    return 0;
}

#include "frame.hpp"
#include "net.hpp"
#include "zcm_file.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

void usage(const char* a0) {
    std::fprintf(stderr,
                 "Usage: %s --in path.zcm --target host:port [--transport tcp|udp]\n"
                 "          [--pace capture|fixed] [--rate Hz]\n"
                 "  Replay a .zcm capture. Default --pace capture uses timestamp_lo deltas.\n"
                 "  --rate implies fixed pacing at that Hz.\n",
                 a0);
}

uint64_t delta_ns(uint32_t prev_lo, uint32_t next_lo) {
    /* unsigned wrap handles u32 timestamp_lo rollover */
    const uint32_t d = next_lo - prev_lo;
    if (d == 0) {
        return 0;
    }
    /* Clamp pathological gaps (e.g. clock jump) to 1s */
    constexpr uint32_t kMax = 1'000'000'000u;
    return d > kMax ? kMax : static_cast<uint64_t>(d);
}

} // namespace

int main(int argc, char** argv) {
    const char* in_path = nullptr;
    const char* target_ep = "127.0.0.1:9900";
    const char* transport = "tcp";
    double rate_hz = 0.0;
    bool pace_capture = true;
    bool rate_set = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        } else if (std::strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--transport") == 0 && i + 1 < argc) {
            transport = argv[++i];
        } else if (std::strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            rate_hz = std::atof(argv[++i]);
            rate_set = true;
            pace_capture = false;
        } else if (std::strcmp(argv[i], "--pace") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (std::strcmp(mode, "capture") == 0) {
                pace_capture = true;
            } else if (std::strcmp(mode, "fixed") == 0) {
                pace_capture = false;
                if (!rate_set) {
                    rate_hz = 1000.0;
                }
            } else {
                usage(argv[0]);
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (!in_path) {
        usage(argv[0]);
        return 1;
    }
    if (!pace_capture && rate_hz <= 0.0) {
        rate_hz = 1000.0;
    }

    FILE* f = std::fopen(in_path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", in_path);
        return 1;
    }
    zcmesh_zcm_header hdr{};
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)
        || hdr.magic != ZCMESH_ZCM_MAGIC || hdr.version != ZCMESH_ZCM_VERSION) {
        std::fprintf(stderr, "bad .zcm header\n");
        std::fclose(f);
        return 1;
    }
    std::vector<uint8_t> blob(static_cast<std::size_t>(hdr.frame_count) * ZCMESH_WIRE_FRAME_SIZE);
    if (hdr.frame_count > 0) {
        const std::size_t want = blob.size();
        if (std::fread(blob.data(), 1, want, f) != want) {
            std::fprintf(stderr, "truncated .zcm\n");
            std::fclose(f);
            return 1;
        }
    }
    std::fclose(f);

    if (!zcmesh::net_init()) {
        return 1;
    }
    zcmesh::Endpoint ep{};
    if (!zcmesh::parse_endpoint(target_ep, ep)) {
        return 1;
    }

    zcmesh::TcpClient tcp;
    zcmesh::UdpSocket udp;
    const bool use_tcp = std::strcmp(transport, "tcp") == 0;
    if (use_tcp) {
        if (!tcp.connect(ep)) {
            std::fprintf(stderr, "tcp connect failed\n");
            zcmesh::net_shutdown();
            return 1;
        }
    } else {
        if (!udp.open()) {
            zcmesh::net_shutdown();
            return 1;
        }
    }

    const auto fixed_period =
        std::chrono::duration<double>(1.0 / (rate_hz > 0 ? rate_hz : 1.0));
    auto next = std::chrono::steady_clock::now();
    uint64_t sent = 0;
    uint64_t fail = 0;

    std::fprintf(stderr, "zcmesh_replay frames=%llu transport=%s pace=%s",
                 static_cast<unsigned long long>(hdr.frame_count), transport,
                 pace_capture ? "capture" : "fixed");
    if (!pace_capture) {
        std::fprintf(stderr, " rate=%.1f", rate_hz);
    }
    std::fprintf(stderr, " -> %s:%u\n", ep.host, ep.port);

    for (uint64_t i = 0; i < hdr.frame_count; ++i) {
        const uint8_t* frame = blob.data() + i * ZCMESH_WIRE_FRAME_SIZE;
        zcmesh_wire_frame check{};
        std::memcpy(&check, frame, ZCMESH_WIRE_FRAME_SIZE);
        if (!zcmesh::verify_frame(check)) {
            ++fail;
            continue;
        }
        bool ok = false;
        if (use_tcp) {
            std::size_t off = 0;
            while (off < ZCMESH_WIRE_FRAME_SIZE) {
                const int n = tcp.send_nb(frame + off, ZCMESH_WIRE_FRAME_SIZE - off);
                if (n < 0) {
                    break;
                }
                if (n == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    continue;
                }
                off += static_cast<std::size_t>(n);
            }
            ok = off == ZCMESH_WIRE_FRAME_SIZE;
        } else {
            ok = udp.send_to(ep, frame, ZCMESH_WIRE_FRAME_SIZE);
        }
        if (ok) {
            ++sent;
        } else {
            ++fail;
        }

        if (i + 1 >= hdr.frame_count) {
            break;
        }
        if (pace_capture) {
            zcmesh_wire_frame nxt{};
            std::memcpy(&nxt, blob.data() + (i + 1) * ZCMESH_WIRE_FRAME_SIZE,
                        ZCMESH_WIRE_FRAME_SIZE);
            const uint64_t dns = delta_ns(check.timestamp_lo, nxt.timestamp_lo);
            if (dns > 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(dns));
            }
        } else {
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(fixed_period);
            std::this_thread::sleep_until(next);
        }
    }

    std::fprintf(stderr, "replay done sent=%llu fail=%llu\n",
                 static_cast<unsigned long long>(sent),
                 static_cast<unsigned long long>(fail));
    zcmesh::net_shutdown();
    return fail == 0 ? 0 : 1;
}

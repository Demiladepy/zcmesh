#include "frame.hpp"
#include "net.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s --listen host:port --forward host:port [--loss-pct N]\n"
                 "  UDP mesh hop: CRC-verify then forward. --loss-pct drops N%% after verify (demo).\n",
                 argv0);
}

bool set_reuse(socket_t fd) {
    int on = 1;
#if defined(_WIN32)
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on)) == 0;
#else
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0;
#endif
}

/* Deterministic drop: hash(seq) %% 100 < loss_pct. */
bool should_drop(uint32_t seq, int loss_pct) {
    if (loss_pct <= 0) {
        return false;
    }
    if (loss_pct >= 100) {
        return true;
    }
    const uint32_t h = seq * 2654435761u;
    return static_cast<int>((h >> 24) % 100u) < loss_pct;
}

} // namespace

int main(int argc, char** argv) {
    const char* listen_ep = nullptr;
    const char* forward_ep = nullptr;
    int loss_pct = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--forward") == 0 && i + 1 < argc) {
            forward_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--loss-pct") == 0 && i + 1 < argc) {
            loss_pct = std::atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (!listen_ep || !forward_ep) {
        usage(argv[0]);
        return 1;
    }

    if (!zcmesh::net_init()) {
        std::fprintf(stderr, "net_init failed\n");
        return 1;
    }

    zcmesh::Endpoint listen{};
    zcmesh::Endpoint forward{};
    if (!zcmesh::parse_endpoint(listen_ep, listen) || !zcmesh::parse_endpoint(forward_ep, forward)) {
        std::fprintf(stderr, "invalid endpoint\n");
        zcmesh::net_shutdown();
        return 1;
    }

    socket_t fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == kInvalidSocket) {
        std::fprintf(stderr, "socket failed\n");
        zcmesh::net_shutdown();
        return 1;
    }
    set_reuse(fd);

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(listen.port);
    if (inet_pton(AF_INET, listen.host, &bind_addr.sin_addr) != 1) {
        std::fprintf(stderr, "bad listen host\n");
#if defined(_WIN32)
        closesocket(fd);
#else
        ::close(fd);
#endif
        zcmesh::net_shutdown();
        return 1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
        std::fprintf(stderr, "bind %s:%u failed\n", listen.host, listen.port);
#if defined(_WIN32)
        closesocket(fd);
#else
        ::close(fd);
#endif
        zcmesh::net_shutdown();
        return 1;
    }

    zcmesh::UdpSocket out;
    if (!out.open()) {
        std::fprintf(stderr, "forward socket open failed\n");
#if defined(_WIN32)
        closesocket(fd);
#else
        ::close(fd);
#endif
        zcmesh::net_shutdown();
        return 1;
    }

    std::fprintf(stderr, "zcmesh_hop listen=%s:%u forward=%s:%u loss_pct=%d\n",
                 listen.host, listen.port, forward.host, forward.port, loss_pct);

    uint8_t buf[512];
    uint64_t ok = 0;
    uint64_t drop = 0;
    uint64_t injected = 0;
    while (true) {
        sockaddr_in from{};
#if defined(_WIN32)
        int fromlen = sizeof(from);
#else
        socklen_t fromlen = sizeof(from);
#endif
        const int n = ::recvfrom(fd, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                                 reinterpret_cast<sockaddr*>(&from), &fromlen);
        if (n < static_cast<int>(ZCMESH_WIRE_FRAME_SIZE)) {
            ++drop;
            continue;
        }
        zcmesh_wire_frame frame{};
        std::memcpy(&frame, buf, ZCMESH_WIRE_FRAME_SIZE);
        if (!zcmesh::verify_frame(frame)) {
            ++drop;
            continue;
        }
        if (should_drop(frame.seq, loss_pct)) {
            ++injected;
            continue;
        }
        if (out.send_to(forward, buf, ZCMESH_WIRE_FRAME_SIZE)) {
            ++ok;
        } else {
            ++drop;
        }
        if (((ok + drop + injected) & 0x3FFu) == 0) {
            std::fprintf(stderr, "hop ok=%llu drop=%llu loss_inject=%llu last_seq=%u\n",
                         static_cast<unsigned long long>(ok),
                         static_cast<unsigned long long>(drop),
                         static_cast<unsigned long long>(injected),
                         frame.seq);
        }
    }
}

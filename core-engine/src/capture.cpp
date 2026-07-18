#include "frame.hpp"
#include "net.hpp"
#include "zcm_file.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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

#include <chrono>
#include <thread>

namespace {

void usage(const char* a0) {
    std::fprintf(stderr,
                 "Usage: %s --listen host:port --out path.zcm [--seconds N]\n"
                 "  Capture verified UDP frames into a .zcm file.\n",
                 a0);
}

} // namespace

int main(int argc, char** argv) {
    const char* listen_ep = "127.0.0.1:9900";
    const char* out_path = nullptr;
    int seconds = 10;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (!out_path) {
        usage(argv[0]);
        return 1;
    }

    if (!zcmesh::net_init()) {
        return 1;
    }

    zcmesh::Endpoint ep{};
    if (!zcmesh::parse_endpoint(listen_ep, ep)) {
        std::fprintf(stderr, "bad --listen\n");
        return 1;
    }

    socket_t fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == kInvalidSocket) {
        return 1;
    }
    int on = 1;
#if defined(_WIN32)
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(ep.port);
    inet_pton(AF_INET, ep.host, &bind_addr.sin_addr);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
        std::fprintf(stderr, "bind failed\n");
        return 1;
    }

    /* Non-blocking with poll timeout via select. */
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    /* leave blocking; use select */
#endif

    std::vector<uint8_t> blob;
    blob.reserve(1024 * 1024);
    uint64_t ok = 0;
    uint64_t drop = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);

    std::fprintf(stderr, "zcmesh_capture listen=%s:%u out=%s seconds=%d\n",
                 ep.host, ep.port, out_path, seconds);

    uint8_t buf[512];
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeval tv{0, 200000};
#if defined(_WIN32)
        const int sel = select(0, &rfds, nullptr, nullptr, &tv);
#else
        const int sel = select(static_cast<int>(fd) + 1, &rfds, nullptr, nullptr, &tv);
#endif
        if (sel <= 0) {
            continue;
        }
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
        const auto* p = reinterpret_cast<const uint8_t*>(&frame);
        blob.insert(blob.end(), p, p + ZCMESH_WIRE_FRAME_SIZE);
        ++ok;
    }

#if defined(_WIN32)
    closesocket(fd);
#else
    ::close(fd);
#endif

    FILE* f = std::fopen(out_path, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out_path);
        zcmesh::net_shutdown();
        return 1;
    }
    zcmesh_zcm_header hdr{};
    hdr.magic = ZCMESH_ZCM_MAGIC;
    hdr.version = ZCMESH_ZCM_VERSION;
    hdr.reserved = 0;
    hdr.frame_count = ok;
    std::fwrite(&hdr, 1, sizeof(hdr), f);
    if (!blob.empty()) {
        std::fwrite(blob.data(), 1, blob.size(), f);
    }
    std::fclose(f);

    std::fprintf(stderr, "captured ok=%llu drop=%llu bytes=%zu -> %s\n",
                 static_cast<unsigned long long>(ok),
                 static_cast<unsigned long long>(drop),
                 blob.size(), out_path);
    zcmesh::net_shutdown();
    return ok > 0 ? 0 : 1;
}

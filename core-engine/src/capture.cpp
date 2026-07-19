#include "frame.hpp"
#include "net.hpp"
#include "wire_frame.h"
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
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>

namespace {

enum class Mode { Udp, Tcp, Both };

void usage(const char* a0) {
    std::fprintf(stderr,
                 "Usage: %s --listen host:port --out path.zcm [--seconds N]\n"
                 "          [--mode udp|tcp|both]\n"
                 "  Capture verified frames into .zcm (streamed; header rewritten on exit).\n"
                 "  TCP path magic-scans after desync.\n",
                 a0);
}

bool set_nonblocking(socket_t fd) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void close_fd(socket_t fd) {
    if (fd == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(fd);
#else
    ::close(fd);
#endif
}

int find_magic(const uint8_t* data, int len, int from) {
    const uint8_t lo = static_cast<uint8_t>(ZCMESH_WIRE_MAGIC & 0xFF);
    const uint8_t hi = static_cast<uint8_t>((ZCMESH_WIRE_MAGIC >> 8) & 0xFF);
    const int last = len - static_cast<int>(ZCMESH_WIRE_FRAME_SIZE);
    for (int i = from; i <= last; ++i) {
        if (data[i] == lo && data[i + 1] == hi) {
            return i;
        }
    }
    return -1;
}

void append_frame(FILE* out, const zcmesh_wire_frame& frame, uint64_t& ok) {
    if (std::fwrite(&frame, 1, ZCMESH_WIRE_FRAME_SIZE, out) != ZCMESH_WIRE_FRAME_SIZE) {
        return;
    }
    ++ok;
}

/* Decode aligned/resync TCP stream buffer in-place (compact remaining). */
void decode_tcp_stream(std::vector<uint8_t>& stream, FILE* out,
                       uint64_t& ok, uint64_t& drop, uint64_t& resync) {
    int pos = 0;
    const int n = static_cast<int>(stream.size());
    while (n - pos >= static_cast<int>(ZCMESH_WIRE_FRAME_SIZE)) {
        const int magic = find_magic(stream.data(), n, pos);
        if (magic < 0) {
            const int keep = static_cast<int>(ZCMESH_WIRE_FRAME_SIZE) - 1;
            if (n - pos > keep) {
                resync += static_cast<uint64_t>(n - pos - keep);
                pos = n - keep;
            }
            break;
        }
        if (magic > pos) {
            resync += static_cast<uint64_t>(magic - pos);
            pos = magic;
        }
        zcmesh_wire_frame frame{};
        std::memcpy(&frame, stream.data() + pos, ZCMESH_WIRE_FRAME_SIZE);
        if (!zcmesh::verify_frame(frame)) {
            ++drop;
            ++resync;
            ++pos;
            continue;
        }
        append_frame(out, frame, ok);
        pos += static_cast<int>(ZCMESH_WIRE_FRAME_SIZE);
    }
    if (pos > 0) {
        stream.erase(stream.begin(), stream.begin() + pos);
    }
}

bool rewrite_header(FILE* f, uint64_t frame_count) {
    zcmesh_zcm_header hdr{};
    hdr.magic = ZCMESH_ZCM_MAGIC;
    hdr.version = ZCMESH_ZCM_VERSION;
    hdr.reserved = 0;
    hdr.frame_count = frame_count;
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        return false;
    }
    return std::fwrite(&hdr, 1, sizeof(hdr), f) == sizeof(hdr);
}

} // namespace

int main(int argc, char** argv) {
    const char* listen_ep = "127.0.0.1:9900";
    const char* out_path = nullptr;
    int seconds = 10;
    Mode mode = Mode::Udp;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_ep = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "udp") == 0) {
                mode = Mode::Udp;
            } else if (std::strcmp(argv[i], "tcp") == 0) {
                mode = Mode::Tcp;
            } else if (std::strcmp(argv[i], "both") == 0) {
                mode = Mode::Both;
            } else {
                usage(argv[0]);
                return 1;
            }
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

    socket_t udp_fd = kInvalidSocket;
    socket_t tcp_fd = kInvalidSocket;
    socket_t client_fd = kInvalidSocket;
    std::vector<uint8_t> tcp_stream;
    tcp_stream.reserve(64 * 1024);

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(ep.port);
    if (inet_pton(AF_INET, ep.host, &bind_addr.sin_addr) != 1) {
        std::fprintf(stderr, "bad listen host\n");
        zcmesh::net_shutdown();
        return 1;
    }

    if (mode == Mode::Udp || mode == Mode::Both) {
        udp_fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp_fd == kInvalidSocket) {
            zcmesh::net_shutdown();
            return 1;
        }
        int on = 1;
#if defined(_WIN32)
        setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
#else
        setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
        if (::bind(udp_fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
            std::fprintf(stderr, "udp bind failed\n");
            close_fd(udp_fd);
            zcmesh::net_shutdown();
            return 1;
        }
        set_nonblocking(udp_fd);
    }

    if (mode == Mode::Tcp || mode == Mode::Both) {
        tcp_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tcp_fd == kInvalidSocket) {
            close_fd(udp_fd);
            zcmesh::net_shutdown();
            return 1;
        }
        int on = 1;
#if defined(_WIN32)
        setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
#else
        setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
        if (::bind(tcp_fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
            std::fprintf(stderr, "tcp bind failed\n");
            close_fd(udp_fd);
            close_fd(tcp_fd);
            zcmesh::net_shutdown();
            return 1;
        }
        if (::listen(tcp_fd, 4) != 0) {
            std::fprintf(stderr, "listen failed\n");
            close_fd(udp_fd);
            close_fd(tcp_fd);
            zcmesh::net_shutdown();
            return 1;
        }
        set_nonblocking(tcp_fd);
    }

    FILE* out = std::fopen(out_path, "wb+");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", out_path);
        close_fd(udp_fd);
        close_fd(tcp_fd);
        zcmesh::net_shutdown();
        return 1;
    }
    if (!rewrite_header(out, 0)) {
        std::fprintf(stderr, "header write failed\n");
        std::fclose(out);
        close_fd(udp_fd);
        close_fd(tcp_fd);
        zcmesh::net_shutdown();
        return 1;
    }

    uint64_t ok = 0;
    uint64_t drop = 0;
    uint64_t resync = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);

    const char* mname = mode == Mode::Tcp ? "tcp" : mode == Mode::Both ? "both" : "udp";
    std::fprintf(stderr, "zcmesh_capture listen=%s:%u mode=%s out=%s seconds=%d (streaming)\n",
                 ep.host, ep.port, mname, out_path, seconds);

    uint8_t buf[64 * 1024];
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        socket_t maxfd = 0;
        if (udp_fd != kInvalidSocket) {
            FD_SET(udp_fd, &rfds);
            if (udp_fd > maxfd) {
                maxfd = udp_fd;
            }
        }
        if (tcp_fd != kInvalidSocket) {
            FD_SET(tcp_fd, &rfds);
            if (tcp_fd > maxfd) {
                maxfd = tcp_fd;
            }
        }
        if (client_fd != kInvalidSocket) {
            FD_SET(client_fd, &rfds);
            if (client_fd > maxfd) {
                maxfd = client_fd;
            }
        }
        timeval tv{0, 200000};
#if defined(_WIN32)
        const int sel = select(0, &rfds, nullptr, nullptr, &tv);
#else
        const int sel = select(static_cast<int>(maxfd) + 1, &rfds, nullptr, nullptr, &tv);
#endif
        if (sel <= 0) {
            continue;
        }

        if (tcp_fd != kInvalidSocket && FD_ISSET(tcp_fd, &rfds)) {
            sockaddr_in from{};
#if defined(_WIN32)
            int fromlen = sizeof(from);
#else
            socklen_t fromlen = sizeof(from);
#endif
            const socket_t c = ::accept(tcp_fd, reinterpret_cast<sockaddr*>(&from), &fromlen);
            if (c != kInvalidSocket) {
                if (client_fd != kInvalidSocket) {
                    close_fd(client_fd);
                    tcp_stream.clear();
                }
                client_fd = c;
                set_nonblocking(client_fd);
                std::fprintf(stderr, "accepted tcp client\n");
            }
        }

        if (client_fd != kInvalidSocket && FD_ISSET(client_fd, &rfds)) {
            const int n = ::recv(client_fd, reinterpret_cast<char*>(buf), sizeof(buf), 0);
            if (n <= 0) {
#if defined(_WIN32)
                const int err = WSAGetLastError();
                if (n < 0 && err == WSAEWOULDBLOCK) {
                    /* spin */
                } else {
                    close_fd(client_fd);
                    client_fd = kInvalidSocket;
                    tcp_stream.clear();
                }
#else
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    /* spin */
                } else {
                    close_fd(client_fd);
                    client_fd = kInvalidSocket;
                    tcp_stream.clear();
                }
#endif
            } else {
                tcp_stream.insert(tcp_stream.end(), buf, buf + n);
                decode_tcp_stream(tcp_stream, out, ok, drop, resync);
            }
        }

        if (udp_fd != kInvalidSocket && FD_ISSET(udp_fd, &rfds)) {
            sockaddr_in from{};
#if defined(_WIN32)
            int fromlen = sizeof(from);
#else
            socklen_t fromlen = sizeof(from);
#endif
            const int n = ::recvfrom(udp_fd, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                                     reinterpret_cast<sockaddr*>(&from), &fromlen);
            if (n < static_cast<int>(ZCMESH_WIRE_FRAME_SIZE)) {
                if (n > 0) {
                    ++drop;
                }
                continue;
            }
            zcmesh_wire_frame frame{};
            std::memcpy(&frame, buf, ZCMESH_WIRE_FRAME_SIZE);
            if (!zcmesh::verify_frame(frame)) {
                ++drop;
                continue;
            }
            append_frame(out, frame, ok);
        }
    }

    close_fd(client_fd);
    close_fd(tcp_fd);
    close_fd(udp_fd);

    if (!rewrite_header(out, ok)) {
        std::fprintf(stderr, "final header rewrite failed\n");
        std::fclose(out);
        zcmesh::net_shutdown();
        return 1;
    }
    std::fflush(out);
    std::fclose(out);

    std::fprintf(stderr, "captured ok=%llu drop=%llu resync=%llu bytes=%llu -> %s\n",
                 static_cast<unsigned long long>(ok),
                 static_cast<unsigned long long>(drop),
                 static_cast<unsigned long long>(resync),
                 static_cast<unsigned long long>(ok * ZCMESH_WIRE_FRAME_SIZE), out_path);
    zcmesh::net_shutdown();
    return ok > 0 ? 0 : 1;
}

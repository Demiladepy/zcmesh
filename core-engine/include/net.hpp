#pragma once

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace zcmesh {

struct Endpoint {
    char host[64];
    uint16_t port;
};

bool net_init();
void net_shutdown();

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open();
    void close();
    bool send_to(const Endpoint& ep, const void* data, std::size_t len);
    socket_t native() const noexcept { return fd_; }

private:
    socket_t fd_;
};

class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool connect(const Endpoint& ep);
    void close();
    bool connected() const noexcept { return connected_; }
    /* Non-blocking send; returns bytes sent or -1 on hard error. 0 = would block. */
    int send_nb(const void* data, std::size_t len);
    socket_t native() const noexcept { return fd_; }

private:
    void apply_socket_opts();

    socket_t fd_;
    bool connected_;
};

bool parse_endpoint(const char* host_port, Endpoint& out);

} // namespace zcmesh

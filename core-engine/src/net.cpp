#include "net.hpp"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

namespace zcmesh {

bool net_init() {
#if defined(_WIN32)
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void net_shutdown() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

static bool set_nonblocking(socket_t fd) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void close_socket(socket_t fd) {
    if (fd == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(fd);
#else
    ::close(fd);
#endif
}

bool parse_endpoint(const char* host_port, Endpoint& out) {
    if (!host_port) {
        return false;
    }
    std::memset(&out, 0, sizeof(out));
    const char* colon = std::strrchr(host_port, ':');
    if (!colon) {
        return false;
    }
    const std::size_t host_len = static_cast<std::size_t>(colon - host_port);
    if (host_len == 0 || host_len >= sizeof(out.host)) {
        return false;
    }
    std::memcpy(out.host, host_port, host_len);
    out.host[host_len] = '\0';
    out.port = static_cast<uint16_t>(std::atoi(colon + 1));
    return out.port != 0;
}

static bool fill_sockaddr(const Endpoint& ep, sockaddr_in& addr) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ep.port);
    if (inet_pton(AF_INET, ep.host, &addr.sin_addr) != 1) {
        return false;
    }
    return true;
}

UdpSocket::UdpSocket() : fd_(kInvalidSocket) {}

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::open() {
    close();
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ == kInvalidSocket) {
        return false;
    }
    if (!set_nonblocking(fd_)) {
        close();
        return false;
    }
    return true;
}

void UdpSocket::close() {
    close_socket(fd_);
    fd_ = kInvalidSocket;
}

bool UdpSocket::send_to(const Endpoint& ep, const void* data, std::size_t len) {
    if (fd_ == kInvalidSocket || !data || len == 0) {
        return false;
    }
    sockaddr_in addr{};
    if (!fill_sockaddr(ep, addr)) {
        return false;
    }
    const int n = ::sendto(fd_, static_cast<const char*>(data),
                           static_cast<int>(len), 0,
                           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return n == static_cast<int>(len);
}

TcpClient::TcpClient() : fd_(kInvalidSocket), connected_(false) {}

TcpClient::~TcpClient() {
    close();
}

void TcpClient::apply_socket_opts() {
    int one = 1;
    int snd = 256 * 1024;
#if defined(_WIN32)
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&snd), sizeof(snd));
#else
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));
#endif
}

bool TcpClient::connect(const Endpoint& ep) {
    close();
    fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ == kInvalidSocket) {
        return false;
    }
    if (!set_nonblocking(fd_)) {
        close();
        return false;
    }
    apply_socket_opts();
    sockaddr_in addr{};
    if (!fill_sockaddr(ep, addr)) {
        close();
        return false;
    }
    const int rc = ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#if defined(_WIN32)
    if (rc == 0) {
        connected_ = true;
        return true;
    }
    const int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd_, &wfds);
        timeval tv{2, 0};
        const int sel = select(0, nullptr, &wfds, nullptr, &tv);
        if (sel > 0 && FD_ISSET(fd_, &wfds)) {
            int so_error = 0;
            int len = sizeof(so_error);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len) == 0
                && so_error == 0) {
                connected_ = true;
                return true;
            }
        }
    }
#else
    if (rc == 0) {
        connected_ = true;
        return true;
    }
    if (errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd_, &wfds);
        timeval tv{2, 0};
        const int sel = select(fd_ + 1, nullptr, &wfds, nullptr, &tv);
        if (sel > 0 && FD_ISSET(fd_, &wfds)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 && so_error == 0) {
                connected_ = true;
                return true;
            }
        }
    }
#endif
    close();
    return false;
}

void TcpClient::close() {
    close_socket(fd_);
    fd_ = kInvalidSocket;
    connected_ = false;
}

int TcpClient::send_nb(const void* data, std::size_t len) {
    if (!connected_ || fd_ == kInvalidSocket || !data || len == 0) {
        return -1;
    }
    const int n = ::send(fd_, static_cast<const char*>(data), static_cast<int>(len), 0);
#if defined(_WIN32)
    if (n == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return 0;
        }
        connected_ = false;
        return -1;
    }
#else
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0;
        }
        connected_ = false;
        return -1;
    }
#endif
    return n;
}

} // namespace zcmesh

#include "router.hpp"

#include <cstring>

namespace zcmesh {

MeshRouter::MeshRouter(Arena& arena, std::size_t max_routes)
    : routes_(nullptr), capacity_(max_routes), count_(0) {
    routes_ = arena.alloc<RouteEntry>(max_routes);
    std::memset(routes_, 0, sizeof(RouteEntry) * max_routes);
}

bool MeshRouter::add_route(uint16_t node_id, const Endpoint* hops, uint8_t hop_count) {
    if (!hops || hop_count == 0 || hop_count > kMaxFallbacks || count_ >= capacity_) {
        return false;
    }
    RouteEntry& e = routes_[count_++];
    e.node_id = node_id;
    e.hop_count = hop_count;
    e.active_hop = 0;
    for (uint8_t i = 0; i < hop_count; ++i) {
        e.hops[i] = hops[i];
    }
    return true;
}

RouteEntry* MeshRouter::find(uint16_t node_id) noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (routes_[i].node_id == node_id) {
            return &routes_[i];
        }
    }
    return nullptr;
}

bool MeshRouter::forward_udp(UdpSocket& udp, uint16_t node_id, const void* data, std::size_t len) {
    RouteEntry* route = find(node_id);
    if (!route) {
        return false;
    }
    for (uint8_t attempt = 0; attempt < route->hop_count; ++attempt) {
        const uint8_t idx = static_cast<uint8_t>((route->active_hop + attempt) % route->hop_count);
        if (udp.send_to(route->hops[idx], data, len)) {
            route->active_hop = idx;
            return true;
        }
    }
    if (route->hop_count > 0) {
        route->active_hop = static_cast<uint8_t>((route->active_hop + 1) % route->hop_count);
    }
    return false;
}

bool MeshRouter::deliver(UdpSocket& udp, TcpClient& tcp, uint16_t node_id,
                         const void* data, std::size_t len, bool prefer_tcp) {
    if (prefer_tcp && tcp.connected()) {
        std::size_t sent = 0;
        const auto* bytes = static_cast<const char*>(data);
        while (sent < len) {
            const int n = tcp.send_nb(bytes + sent, len - sent);
            if (n < 0) {
                break;
            }
            if (n == 0) {
                /* Would block — treat as soft failure, fall through to UDP. */
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        if (sent == len) {
            return true;
        }
    }
    return forward_udp(udp, node_id, data, len);
}

} // namespace zcmesh

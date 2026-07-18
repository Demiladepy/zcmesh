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
    e.hop_skip_mask = 0;
    e.attempts = 0;
    e.fails = 0;
    e.last_probe_ns = 0;
    for (uint8_t i = 0; i < kMaxFallbacks; ++i) {
        e.hop_ok[i] = 0;
    }
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

const RouteEntry* MeshRouter::find(uint16_t node_id) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (routes_[i].node_id == node_id) {
            return &routes_[i];
        }
    }
    return nullptr;
}

bool MeshRouter::set_hop_skip_mask(uint16_t node_id, uint8_t mask) noexcept {
    RouteEntry* route = find(node_id);
    if (!route) {
        return false;
    }
    route->hop_skip_mask = mask;
    return true;
}

bool MeshRouter::forward_udp(UdpSocket& udp, uint16_t node_id, const void* data, std::size_t len,
                             uint64_t now_ns) {
    RouteEntry* route = find(node_id);
    if (!route) {
        return false;
    }
    ++route->attempts;

    const auto hop_allowed = [&](uint8_t idx) -> bool {
        return idx < route->hop_count && (route->hop_skip_mask & (1u << idx)) == 0;
    };

    if (!hop_allowed(route->active_hop)) {
        bool moved = false;
        for (uint8_t i = 0; i < route->hop_count; ++i) {
            if (hop_allowed(i)) {
                route->active_hop = i;
                moved = true;
                break;
            }
        }
        if (!moved) {
            ++route->fails;
            return false;
        }
    }

    if (route->active_hop > 0 && hop_allowed(0) &&
        (route->last_probe_ns == 0 || now_ns - route->last_probe_ns >= kPreferredProbeNs)) {
        route->last_probe_ns = now_ns;
        if (udp.send_to(route->hops[0], data, len)) {
            route->active_hop = 0;
            ++route->hop_ok[0];
            return true;
        }
    }

    for (uint8_t attempt = 0; attempt < route->hop_count; ++attempt) {
        const uint8_t idx = static_cast<uint8_t>((route->active_hop + attempt) % route->hop_count);
        if (!hop_allowed(idx)) {
            continue;
        }
        if (udp.send_to(route->hops[idx], data, len)) {
            route->active_hop = idx;
            ++route->hop_ok[idx];
            return true;
        }
    }
    ++route->fails;
    if (route->hop_count > 0) {
        for (uint8_t step = 1; step <= route->hop_count; ++step) {
            const uint8_t idx =
                static_cast<uint8_t>((route->active_hop + step) % route->hop_count);
            if (hop_allowed(idx)) {
                route->active_hop = idx;
                break;
            }
        }
    }
    return false;
}

bool MeshRouter::deliver(UdpSocket& udp, TcpClient& tcp, uint16_t node_id,
                         const void* data, std::size_t len, bool prefer_tcp, uint64_t now_ns) {
    if (prefer_tcp && tcp.connected()) {
        std::size_t sent = 0;
        const auto* bytes = static_cast<const char*>(data);
        while (sent < len) {
            const int n = tcp.send_nb(bytes + sent, len - sent);
            if (n < 0) {
                break;
            }
            if (n == 0) {
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        if (sent == len) {
            return true;
        }
    }
    return forward_udp(udp, node_id, data, len, now_ns);
}

} // namespace zcmesh

#pragma once

#include "arena.hpp"
#include "net.hpp"

#include <cstdint>

namespace zcmesh {

constexpr std::size_t kMaxFallbacks = 3;

struct RouteEntry {
    uint16_t node_id;
    Endpoint hops[kMaxFallbacks];
    uint8_t hop_count;
    uint8_t active_hop; /* advances on send failure */
};

class MeshRouter {
public:
    MeshRouter(Arena& arena, std::size_t max_routes);

    bool add_route(uint16_t node_id, const Endpoint* hops, uint8_t hop_count);
    RouteEntry* find(uint16_t node_id) noexcept;

    /* Try UDP to active hop; on failure advance fallback. Returns true if sent. */
    bool forward_udp(UdpSocket& udp, uint16_t node_id, const void* data, std::size_t len);

    /* Prefer TCP uplink to operator (last-hop). Falls back to UDP mesh if TCP down. */
    bool deliver(UdpSocket& udp, TcpClient& tcp, uint16_t node_id,
                 const void* data, std::size_t len, bool prefer_tcp);

private:
    RouteEntry* routes_;
    std::size_t capacity_;
    std::size_t count_;
};

} // namespace zcmesh

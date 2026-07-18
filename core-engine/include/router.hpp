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
    uint8_t active_hop; /* sticky; preferred hop 0 is probed periodically */
    uint64_t attempts;
    uint64_t fails;
    uint64_t hop_ok[kMaxFallbacks];
    uint64_t last_probe_ns;
};

class MeshRouter {
public:
    static constexpr uint64_t kPreferredProbeNs = 500'000'000ull; /* 500 ms */

    MeshRouter(Arena& arena, std::size_t max_routes);

    bool add_route(uint16_t node_id, const Endpoint* hops, uint8_t hop_count);
    RouteEntry* find(uint16_t node_id) noexcept;
    const RouteEntry* find(uint16_t node_id) const noexcept;

    /* Try UDP to active hop; on failure advance fallback. Probe hop 0 when demoted. */
    bool forward_udp(UdpSocket& udp, uint16_t node_id, const void* data, std::size_t len,
                     uint64_t now_ns);

    /* Prefer TCP uplink to operator (last-hop). Falls back to UDP mesh if TCP down. */
    bool deliver(UdpSocket& udp, TcpClient& tcp, uint16_t node_id,
                 const void* data, std::size_t len, bool prefer_tcp, uint64_t now_ns);

private:
    RouteEntry* routes_;
    std::size_t capacity_;
    std::size_t count_;
};

} // namespace zcmesh

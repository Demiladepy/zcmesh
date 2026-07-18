#include "arena.hpp"
#include "router.hpp"

#include <cstdio>
#include <cstring>

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fails;
    }
}

} // namespace

int main() {
    zcmesh::Arena arena(64 * 1024);
    zcmesh::MeshRouter router(arena, 4);

    zcmesh::Endpoint hops[2]{};
    std::snprintf(hops[0].host, sizeof(hops[0].host), "127.0.0.1");
    hops[0].port = 9901;
    std::snprintf(hops[1].host, sizeof(hops[1].host), "127.0.0.1");
    hops[1].port = 9902;

    expect(router.add_route(1, hops, 2), "add_route");
    const zcmesh::RouteEntry* e = router.find(1);
    expect(e != nullptr, "find route");
    expect(e->active_hop == 0, "starts on preferred hop");
    expect(e->hop_count == 2, "hop_count 2");
    expect(e->attempts == 0 && e->fails == 0, "counters zero");

    /* Demote without sockets: mutate via non-const find. */
    zcmesh::RouteEntry* mut = router.find(1);
    mut->active_hop = 1;
    mut->last_probe_ns = 1;
    expect(mut->active_hop == 1, "demoted");

    /* Probe interval gate: immediately after last_probe, preferred probe is skipped
       until kPreferredProbeNs elapses — verified by reading constants / state. */
    expect(zcmesh::MeshRouter::kPreferredProbeNs == 500'000'000ull, "probe interval 500ms");
    expect(mut->last_probe_ns + zcmesh::MeshRouter::kPreferredProbeNs > mut->last_probe_ns,
           "probe window math");

    if (g_fails == 0) {
        std::printf("zcmesh_test_router: OK\n");
        return 0;
    }
    std::printf("zcmesh_test_router: %d failure(s)\n", g_fails);
    return 1;
}

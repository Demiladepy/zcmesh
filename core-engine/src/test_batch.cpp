#include "arena.hpp"
#include "batch.hpp"
#include "frame.hpp"

#include <cstdio>

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
    zcmesh::FrameBatch batch(arena, 16);
    expect(batch.flush_at() == 16, "initial flush_at == capacity");

    batch.adapt(false, 5'000'000ull);
    expect(batch.flush_at() == 8, "stall halves flush_at");

    batch.adapt(false, 5'000'000ull);
    expect(batch.flush_at() == 4, "second stall halves again");

    for (int i = 0; i < 10; ++i) {
        batch.adapt(true, 50'000ull);
    }
    expect(batch.flush_at() > 4, "cheap flushes grow flush_at");
    expect(batch.flush_at() <= 16, "flush_at capped at capacity");

    zcmesh::SensorSample s{};
    s.seq = 1;
    s.node_id = 1;
    s.sensor_type = ZCMESH_SENSOR_VOLTAGE;
    s.raw_value = 42;
    expect(batch.push(s) != nullptr, "push ok");
    expect(batch.count() == 1, "count 1");
    expect(!batch.should_flush() || batch.flush_at() == 1, "should_flush respects target");

    if (g_fails == 0) {
        std::printf("zcmesh_test_batch: OK flush_at=%zu\n", batch.flush_at());
        return 0;
    }
    std::printf("zcmesh_test_batch: %d failure(s)\n", g_fails);
    return 1;
}

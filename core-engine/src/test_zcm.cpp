#include "frame.hpp"
#include "zcm_file.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

std::string temp_path() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetTempPathA(MAX_PATH, buf);
    return std::string(buf) + "zcmesh_test.zcm";
#else
    return "/tmp/zcmesh_test.zcm";
#endif
}

int g_fails = 0;
void expect(bool c, const char* m) {
    if (!c) {
        std::fprintf(stderr, "FAIL: %s\n", m);
        ++g_fails;
    }
}

} // namespace

int main() {
    const std::string path = temp_path();
    constexpr uint64_t N = 100;
    std::vector<zcmesh_wire_frame> frames(N);
    for (uint64_t i = 0; i < N; ++i) {
        zcmesh::SensorSample s{};
        s.seq = static_cast<uint32_t>(i);
        s.timestamp_ns = 1000ull * i;
        s.node_id = 3;
        s.sensor_type = static_cast<uint8_t>(i % 3);
        s.flags = 0;
        s.raw_value = static_cast<int32_t>(i);
        zcmesh::pack_frame_into(&frames[i], s);
        expect(zcmesh::verify_frame(frames[i]), "pack verify");
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    expect(f != nullptr, "open write");
    if (!f) {
        return 1;
    }
    zcmesh_zcm_header hdr{};
    hdr.magic = ZCMESH_ZCM_MAGIC;
    hdr.version = ZCMESH_ZCM_VERSION;
    hdr.frame_count = N;
    std::fwrite(&hdr, 1, sizeof(hdr), f);
    std::fwrite(frames.data(), ZCMESH_WIRE_FRAME_SIZE, static_cast<std::size_t>(N), f);
    std::fclose(f);

    f = std::fopen(path.c_str(), "rb");
    expect(f != nullptr, "open read");
    zcmesh_zcm_header rh{};
    expect(std::fread(&rh, 1, sizeof(rh), f) == sizeof(rh), "read hdr");
    expect(rh.magic == ZCMESH_ZCM_MAGIC, "magic");
    expect(rh.frame_count == N, "count");
    for (uint64_t i = 0; i < N; ++i) {
        zcmesh_wire_frame fr{};
        expect(std::fread(&fr, 1, sizeof(fr), f) == sizeof(fr), "read frame");
        expect(zcmesh::verify_frame(fr), "crc");
        expect(fr.seq == static_cast<uint32_t>(i), "seq");
        expect(fr.node_id == 3, "node");
    }
    std::fclose(f);
    std::remove(path.c_str());

    if (g_fails == 0) {
        std::printf("zcmesh_test_zcm: OK\n");
        return 0;
    }
    std::printf("zcmesh_test_zcm: %d failure(s)\n", g_fails);
    return 1;
}

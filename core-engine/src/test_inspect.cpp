#include "frame.hpp"
#include "zcm_analyze.hpp"
#include "zcm_file.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

std::string temp_path() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetTempPathA(MAX_PATH, buf);
    return std::string(buf) + "zcmesh_inspect_test.zcm";
#else
    return "/tmp/zcmesh_inspect_test.zcm";
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
    /* seq 0,1,2,5 → hole of 2 (missing 3,4) */
    std::vector<uint32_t> seqs = {0, 1, 2, 5};
    std::vector<zcmesh_wire_frame> frames(seqs.size());
    for (std::size_t i = 0; i < seqs.size(); ++i) {
        zcmesh::SensorSample s{};
        s.seq = seqs[i];
        s.timestamp_ns = 1'000'000ull * seqs[i];
        s.node_id = 7;
        s.sensor_type = ZCMESH_SENSOR_VOLTAGE;
        s.flags = ZCMESH_FLAG_LAST_HOP;
        s.reserved = 1;
        s.raw_value = static_cast<int32_t>(seqs[i]);
        zcmesh::pack_frame_into(&frames[i], s);
        expect(zcmesh::verify_frame(frames[i]), "verify");
    }

    zcmesh::ZcmAnalyze live{};
    for (const auto& fr : frames) {
        live.observe(fr);
    }
    expect(live.total_gaps() == 2, "gaps == 2");
    expect(live.by_node[7].max_hole == 2, "max_hole == 2");
    expect(live.by_node[7].gap_events == 1, "one gap event");
    expect(live.hop.last_hop == frames.size(), "all last_hop");
    expect(live.hop.hop_hist[1] == frames.size(), "hop_idx 1");

    const std::string path = temp_path();
    FILE* f = std::fopen(path.c_str(), "wb");
    expect(f != nullptr, "open");
    if (!f) {
        return 1;
    }
    zcmesh_zcm_header hdr{};
    hdr.magic = ZCMESH_ZCM_MAGIC;
    hdr.version = ZCMESH_ZCM_VERSION;
    hdr.frame_count = frames.size();
    std::fwrite(&hdr, 1, sizeof(hdr), f);
    std::fwrite(frames.data(), ZCMESH_WIRE_FRAME_SIZE, frames.size(), f);
    std::fclose(f);

    f = std::fopen(path.c_str(), "rb");
    expect(f != nullptr, "reopen");
    zcmesh_zcm_header rh{};
    expect(std::fread(&rh, 1, sizeof(rh), f) == sizeof(rh), "hdr");
    zcmesh::ZcmAnalyze from_disk{};
    for (uint64_t i = 0; i < rh.frame_count; ++i) {
        zcmesh_wire_frame fr{};
        expect(std::fread(&fr, 1, sizeof(fr), f) == sizeof(fr), "frame");
        expect(zcmesh::verify_frame(fr), "crc");
        from_disk.observe(fr);
    }
    std::fclose(f);
    std::remove(path.c_str());

    expect(from_disk.total_gaps() == 2, "disk gaps");
    expect(from_disk.total_dups() == 0, "no dups");

    if (g_fails == 0) {
        std::printf("zcmesh_test_inspect: OK gaps=%llu\n",
                    static_cast<unsigned long long>(from_disk.total_gaps()));
        return 0;
    }
    std::printf("zcmesh_test_inspect: %d failure(s)\n", g_fails);
    return 1;
}

#include "frame.hpp"
#include "zcm_file.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

void usage(const char* a0) {
    std::fprintf(stderr, "Usage: %s path.zcm [--verbose]\n", a0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* path = argv[1];
    bool verbose = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }

    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    zcmesh_zcm_header hdr{};
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fprintf(stderr, "short header\n");
        std::fclose(f);
        return 1;
    }
    if (hdr.magic != ZCMESH_ZCM_MAGIC || hdr.version != ZCMESH_ZCM_VERSION) {
        std::fprintf(stderr, "bad magic/version\n");
        std::fclose(f);
        return 1;
    }

    uint64_t crc_fail = 0;
    uint64_t ok = 0;
    std::unordered_set<uint16_t> nodes;
    std::unordered_map<uint8_t, uint64_t> by_sensor;
    uint32_t min_seq = 0xFFFFFFFFu;
    uint32_t max_seq = 0;
    bool first = true;

    for (uint64_t i = 0; i < hdr.frame_count; ++i) {
        zcmesh_wire_frame frame{};
        if (std::fread(&frame, 1, sizeof(frame), f) != sizeof(frame)) {
            std::fprintf(stderr, "truncated at frame %llu\n", static_cast<unsigned long long>(i));
            std::fclose(f);
            return 1;
        }
        if (!zcmesh::verify_frame(frame)) {
            ++crc_fail;
            continue;
        }
        ++ok;
        nodes.insert(frame.node_id);
        by_sensor[frame.sensor_type]++;
        if (first) {
            min_seq = max_seq = frame.seq;
            first = false;
        } else {
            if (frame.seq < min_seq) {
                min_seq = frame.seq;
            }
            if (frame.seq > max_seq) {
                max_seq = frame.seq;
            }
        }
        if (verbose && (i < 5 || i + 1 == hdr.frame_count)) {
            std::printf("frame[%llu] seq=%u node=%u sensor=%u raw=%d hop=%u last=%d\n",
                        static_cast<unsigned long long>(i), frame.seq, frame.node_id,
                        frame.sensor_type, frame.raw_value, frame.reserved,
                        (frame.flags & ZCMESH_FLAG_LAST_HOP) ? 1 : 0);
        }
    }
    std::fclose(f);

    std::printf("zcm %s\n", path);
    std::printf("  header_frames=%llu verified=%llu crc_fail=%llu\n",
                static_cast<unsigned long long>(hdr.frame_count),
                static_cast<unsigned long long>(ok),
                static_cast<unsigned long long>(crc_fail));
    std::printf("  nodes=%zu seq_range=[%u,%u]\n", nodes.size(), min_seq, max_seq);
    for (const auto& kv : by_sensor) {
        std::printf("  sensor_type=%u count=%llu\n", kv.first,
                    static_cast<unsigned long long>(kv.second));
    }
    return crc_fail == 0 && ok == hdr.frame_count ? 0 : 1;
}

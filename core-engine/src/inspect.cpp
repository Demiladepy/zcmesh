#include "frame.hpp"
#include "zcm_analyze.hpp"
#include "zcm_file.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void usage(const char* a0) {
    std::fprintf(stderr,
                 "Usage: %s path.zcm [--verbose]\n"
                 "          [--expect-gaps-min N] [--expect-gaps-max N]\n"
                 "          [--expect-hop-idx N] [--expect-last-hop-min-pct P]\n"
                 "  Offline .zcm report: CRC, per-node seq gaps, hop index / LAST_HOP.\n",
                 a0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* path = argv[1];
    bool verbose = false;
    int64_t expect_gaps_min = -1;
    int64_t expect_gaps_max = -1;
    int expect_hop_idx = -1;
    double expect_last_hop_min_pct = -1.0;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (std::strcmp(argv[i], "--expect-gaps-min") == 0 && i + 1 < argc) {
            expect_gaps_min = std::atoll(argv[++i]);
        } else if (std::strcmp(argv[i], "--expect-gaps-max") == 0 && i + 1 < argc) {
            expect_gaps_max = std::atoll(argv[++i]);
        } else if (std::strcmp(argv[i], "--expect-hop-idx") == 0 && i + 1 < argc) {
            expect_hop_idx = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--expect-last-hop-min-pct") == 0 && i + 1 < argc) {
            expect_last_hop_min_pct = std::atof(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
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

    zcmesh::ZcmAnalyze stats{};
    for (uint64_t i = 0; i < hdr.frame_count; ++i) {
        zcmesh_wire_frame frame{};
        if (std::fread(&frame, 1, sizeof(frame), f) != sizeof(frame)) {
            std::fprintf(stderr, "truncated at frame %llu\n", static_cast<unsigned long long>(i));
            std::fclose(f);
            return 1;
        }
        if (!zcmesh::verify_frame(frame)) {
            ++stats.crc_fail;
            continue;
        }
        stats.observe(frame);
        if (verbose && (i < 5 || i + 1 == hdr.frame_count)) {
            std::printf("frame[%llu] seq=%u node=%u sensor=%u raw=%d hop=%u last=%d\n",
                        static_cast<unsigned long long>(i), frame.seq, frame.node_id,
                        frame.sensor_type, frame.raw_value, frame.reserved,
                        (frame.flags & ZCMESH_FLAG_LAST_HOP) ? 1 : 0);
        }
    }
    std::fclose(f);

    const uint64_t gaps = stats.total_gaps();
    const uint64_t dups = stats.total_dups();
    const double last_pct =
        stats.ok > 0 ? (100.0 * static_cast<double>(stats.hop.last_hop) / static_cast<double>(stats.ok))
                     : 0.0;

    std::printf("zcm %s\n", path);
    std::printf("  header_frames=%llu verified=%llu crc_fail=%llu\n",
                static_cast<unsigned long long>(hdr.frame_count),
                static_cast<unsigned long long>(stats.ok),
                static_cast<unsigned long long>(stats.crc_fail));
    std::printf("  nodes=%zu seq_range=[%u,%u] gaps=%llu dups=%llu last_hop_pct=%.1f\n",
                stats.by_node.size(), stats.min_seq, stats.max_seq,
                static_cast<unsigned long long>(gaps),
                static_cast<unsigned long long>(dups), last_pct);
    for (const auto& kv : stats.by_node) {
        const zcmesh::NodeGapStats& n = kv.second;
        std::printf("  node=%u frames=%llu gaps=%llu gap_events=%llu max_hole=%u dups=%llu\n",
                    kv.first,
                    static_cast<unsigned long long>(n.frames),
                    static_cast<unsigned long long>(n.gaps),
                    static_cast<unsigned long long>(n.gap_events),
                    n.max_hole,
                    static_cast<unsigned long long>(n.dups));
    }
    for (const auto& kv : stats.by_sensor) {
        std::printf("  sensor_type=%u count=%llu\n", kv.first,
                    static_cast<unsigned long long>(kv.second));
    }
    for (int h = 0; h < 16; ++h) {
        if (stats.hop.hop_hist[h] > 0) {
            std::printf("  hop_idx=%d count=%llu\n", h,
                        static_cast<unsigned long long>(stats.hop.hop_hist[h]));
        }
    }
    /* Machine-readable one-liner for scripts. */
    std::printf("SUMMARY frames=%llu crc_fail=%llu gaps=%llu dups=%llu nodes=%zu last_hop_pct=%.1f\n",
                static_cast<unsigned long long>(stats.ok),
                static_cast<unsigned long long>(stats.crc_fail),
                static_cast<unsigned long long>(gaps),
                static_cast<unsigned long long>(dups),
                stats.by_node.size(), last_pct);

    if (stats.crc_fail != 0 || stats.ok != hdr.frame_count) {
        return 1;
    }
    if (expect_gaps_min >= 0 && static_cast<int64_t>(gaps) < expect_gaps_min) {
        std::fprintf(stderr, "expect-gaps-min %lld not met (gaps=%llu)\n",
                     static_cast<long long>(expect_gaps_min),
                     static_cast<unsigned long long>(gaps));
        return 1;
    }
    if (expect_gaps_max >= 0 && static_cast<int64_t>(gaps) > expect_gaps_max) {
        std::fprintf(stderr, "expect-gaps-max %lld exceeded (gaps=%llu)\n",
                     static_cast<long long>(expect_gaps_max),
                     static_cast<unsigned long long>(gaps));
        return 1;
    }
    if (expect_hop_idx >= 0) {
        if (expect_hop_idx > 15) {
            std::fprintf(stderr, "expect-hop-idx out of range\n");
            return 1;
        }
        const uint64_t at = stats.hop.hop_hist[expect_hop_idx];
        if (at != stats.ok || stats.ok == 0) {
            std::fprintf(stderr, "expect-hop-idx %d not exclusive (count=%llu ok=%llu)\n",
                         expect_hop_idx, static_cast<unsigned long long>(at),
                         static_cast<unsigned long long>(stats.ok));
            return 1;
        }
    }
    if (expect_last_hop_min_pct >= 0.0 && last_pct + 1e-9 < expect_last_hop_min_pct) {
        std::fprintf(stderr, "expect-last-hop-min-pct %.1f not met (got %.1f)\n",
                     expect_last_hop_min_pct, last_pct);
        return 1;
    }
    return 0;
}

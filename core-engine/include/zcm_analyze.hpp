#pragma once

#include "wire_frame.h"

#include <cstdint>
#include <unordered_map>

namespace zcmesh {

struct NodeGapStats {
    bool seen = false;
    uint32_t last_seq = 0;
    uint64_t frames = 0;
    uint64_t gaps = 0;      /* sum of missing seqs */
    uint64_t gap_events = 0; /* number of hole occurrences */
    uint64_t dups = 0;
    uint32_t max_hole = 0;
};

struct HopStats {
    uint64_t last_hop = 0;
    uint64_t hop_hist[16]{};
};

struct ZcmAnalyze {
    uint64_t ok = 0;
    uint64_t crc_fail = 0;
    uint32_t min_seq = 0xFFFFFFFFu;
    uint32_t max_seq = 0;
    bool any = false;
    std::unordered_map<uint16_t, NodeGapStats> by_node;
    std::unordered_map<uint8_t, uint64_t> by_sensor;
    HopStats hop{};

    void observe(const zcmesh_wire_frame& frame) noexcept {
        ++ok;
        by_sensor[frame.sensor_type]++;
        if (!any) {
            min_seq = max_seq = frame.seq;
            any = true;
        } else {
            if (frame.seq < min_seq) {
                min_seq = frame.seq;
            }
            if (frame.seq > max_seq) {
                max_seq = frame.seq;
            }
        }

        if ((frame.flags & ZCMESH_FLAG_LAST_HOP) != 0) {
            ++hop.last_hop;
        }
        const uint8_t hi = frame.reserved < 16 ? frame.reserved : 15;
        ++hop.hop_hist[hi];

        NodeGapStats& n = by_node[frame.node_id];
        if (!n.seen) {
            n.seen = true;
            n.last_seq = frame.seq;
            n.frames = 1;
            return;
        }
        ++n.frames;
        if (frame.seq <= n.last_seq) {
            ++n.dups;
        } else if (frame.seq > n.last_seq + 1) {
            const uint32_t hole = frame.seq - n.last_seq - 1;
            n.gaps += hole;
            ++n.gap_events;
            if (hole > n.max_hole) {
                n.max_hole = hole;
            }
        }
        n.last_seq = frame.seq;
    }

    uint64_t total_gaps() const noexcept {
        uint64_t g = 0;
        for (const auto& kv : by_node) {
            g += kv.second.gaps;
        }
        return g;
    }

    uint64_t total_dups() const noexcept {
        uint64_t d = 0;
        for (const auto& kv : by_node) {
            d += kv.second.dups;
        }
        return d;
    }
};

} // namespace zcmesh

#include "batch.hpp"

#include <algorithm>
#include <cstring>

namespace zcmesh {

FrameBatch::FrameBatch(Arena& arena, std::size_t capacity_frames)
    : frames_(nullptr),
      capacity_(capacity_frames),
      count_(0),
      send_offset_(0),
      flush_at_(capacity_frames) {
    frames_ = arena.alloc<zcmesh_wire_frame>(capacity_frames);
    std::memset(frames_, 0, sizeof(zcmesh_wire_frame) * capacity_frames);
}

zcmesh_wire_frame* FrameBatch::push(const SensorSample& sample) {
    if (full()) {
        return nullptr;
    }
    zcmesh_wire_frame* slot = &frames_[count_++];
    pack_frame_into(slot, sample);
    return slot;
}

void FrameBatch::clear() noexcept {
    count_ = 0;
    send_offset_ = 0;
}

bool FrameBatch::flush_tcp(TcpClient& tcp) {
    if (empty() || !tcp.connected()) {
        return false;
    }
    const auto* ptr = reinterpret_cast<const char*>(frames_);
    const std::size_t total = this->bytes();
    while (send_offset_ < total) {
        const int n = tcp.send_nb(ptr + send_offset_, total - send_offset_);
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            return false;
        }
        send_offset_ += static_cast<std::size_t>(n);
    }
    clear();
    return true;
}

bool FrameBatch::flush_tcp_retry(TcpClient& tcp, int attempts) {
    for (int i = 0; i < attempts; ++i) {
        if (flush_tcp(tcp)) {
            return true;
        }
        if (!tcp.connected()) {
            return false;
        }
        if (empty()) {
            return true;
        }
    }
    return empty();
}

void FrameBatch::adapt(bool fully_sent, uint64_t flush_ns) noexcept {
    if (!fully_sent || flush_ns > 2'000'000ull) {
        flush_at_ = std::max<std::size_t>(1, flush_at_ / 2);
        return;
    }
    if (flush_ns < 200'000ull && flush_at_ < capacity_) {
        flush_at_ = std::min(capacity_, flush_at_ + 1);
    }
}

} // namespace zcmesh

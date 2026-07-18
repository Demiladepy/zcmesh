#pragma once

#include "arena.hpp"
#include "frame.hpp"
#include "net.hpp"

#include <cstddef>
#include <cstdint>

namespace zcmesh {

/* Contiguous frame slab. Soft flush target adapts under TCP backpressure. */
class FrameBatch {
public:
    FrameBatch(Arena& arena, std::size_t capacity_frames);

    bool empty() const noexcept { return count_ == 0; }
    bool full() const noexcept { return count_ >= capacity_; }
    bool should_flush() const noexcept { return count_ >= flush_at_; }
    std::size_t count() const noexcept { return count_; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t flush_at() const noexcept { return flush_at_; }
    std::size_t bytes() const noexcept { return count_ * ZCMESH_WIRE_FRAME_SIZE; }

    zcmesh_wire_frame* push(const SensorSample& sample);
    void clear() noexcept;

    bool flush_tcp(TcpClient& tcp);
    bool flush_tcp_retry(TcpClient& tcp, int attempts);

    /* Call after a flush attempt: shrink target on stall, grow when cheap. */
    void adapt(bool fully_sent, uint64_t flush_ns) noexcept;

    bool has_partial_send() const noexcept { return send_offset_ > 0; }
    const void* data() const noexcept { return frames_; }

private:
    zcmesh_wire_frame* frames_;
    std::size_t capacity_;
    std::size_t count_;
    std::size_t send_offset_;
    std::size_t flush_at_;
};

} // namespace zcmesh

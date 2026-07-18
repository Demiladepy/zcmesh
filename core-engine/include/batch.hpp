#pragma once

#include "arena.hpp"
#include "frame.hpp"
#include "net.hpp"

#include <cstddef>
#include <cstdint>

namespace zcmesh {

/* Contiguous frame slab in the arena. Fills then flushes as one TCP write. */
class FrameBatch {
public:
    FrameBatch(Arena& arena, std::size_t capacity_frames);

    bool empty() const noexcept { return count_ == 0; }
    bool full() const noexcept { return count_ >= capacity_; }
    std::size_t count() const noexcept { return count_; }
    std::size_t bytes() const noexcept { return count_ * ZCMESH_WIRE_FRAME_SIZE; }

    zcmesh_wire_frame* push(const SensorSample& sample);
    void clear() noexcept;

    /* Non-blocking flush to TCP. Returns true if entire batch sent. */
    bool flush_tcp(TcpClient& tcp);

    const void* data() const noexcept { return frames_; }

private:
    zcmesh_wire_frame* frames_;
    std::size_t capacity_;
    std::size_t count_;
    std::size_t send_offset_; /* partial send resume */
};

} // namespace zcmesh

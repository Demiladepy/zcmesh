#pragma once

#include <cstddef>
#include <cstdint>

namespace zcmesh {

class Arena {
public:
    explicit Arena(std::size_t capacity_bytes);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
    void reset() noexcept;

    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t used() const noexcept { return offset_; }
    std::size_t remaining() const noexcept { return capacity_ - offset_; }

    template <typename T>
    T* alloc(std::size_t count = 1) {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

private:
    char* base_;
    std::size_t capacity_;
    std::size_t offset_;
};

} // namespace zcmesh

#include "arena.hpp"

#include <cstdlib>
#include <new>
#include <stdexcept>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace zcmesh {

Arena::Arena(std::size_t capacity_bytes)
    : base_(nullptr), capacity_(capacity_bytes), offset_(0) {
    if (capacity_bytes == 0) {
        throw std::invalid_argument("Arena capacity must be non-zero");
    }
#if defined(_WIN32)
    base_ = static_cast<char*>(
        VirtualAlloc(nullptr, capacity_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!base_) {
        throw std::bad_alloc();
    }
#else
    base_ = static_cast<char*>(std::malloc(capacity_bytes));
    if (!base_) {
        throw std::bad_alloc();
    }
#endif
}

Arena::~Arena() {
#if defined(_WIN32)
    if (base_) {
        VirtualFree(base_, 0, MEM_RELEASE);
    }
#else
    std::free(base_);
#endif
    base_ = nullptr;
}

void* Arena::allocate(std::size_t size, std::size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        throw std::invalid_argument("alignment must be power of two");
    }
    const std::uintptr_t current = reinterpret_cast<std::uintptr_t>(base_) + offset_;
    const std::uintptr_t aligned = (current + (alignment - 1)) & ~(static_cast<std::uintptr_t>(alignment) - 1);
    const std::size_t pad = static_cast<std::size_t>(aligned - current);
    const std::size_t total = pad + size;
    if (total > remaining()) {
        throw std::bad_alloc();
    }
    offset_ += total;
    return reinterpret_cast<void*>(aligned);
}

void Arena::reset() noexcept {
    offset_ = 0;
}

} // namespace zcmesh

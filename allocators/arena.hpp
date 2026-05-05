#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>

class Arena {
private:
    std::byte* buf_;
    std::byte* ptr_;
    std::byte* end_;

public:
    Arena(std::byte* buf, std::size_t size)
        : buf_(buf), ptr_(buf), end_(buf + size) {}

    void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
      /* Some bs ong - num bytes to allcoate..
       *
       *
       */
        std::uintptr_t addr    = reinterpret_cast<std::uintptr_t>(ptr_);
        std::uintptr_t aligned = (addr + align - 1) & ~(align - 1);

        std::byte* next = reinterpret_cast<std::byte*>(aligned + size);

        assert(next <= end_);

        ptr_ = next;
        return reinterpret_cast<void*>(aligned);
    }

    template<typename T>
    T* alloc(std::size_t n = 1) {
        return static_cast<T*>(allocate(sizeof(T) * n, alignof(T)));
    }

    void reset() { ptr_ = buf_; }

    [[nodiscard]] std::size_t used()      const { return ptr_ - buf_; }
    [[nodiscard]] std::size_t remaining() const { return end_ - ptr_; }
};

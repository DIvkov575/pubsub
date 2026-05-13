#include "../allocators/pool.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    constexpr size_t N         = 8;
    constexpr size_t slot_size = sizeof(uint64_t);
    Pool<slot_size, N> pool;

    assert(pool.available() == N);

    void* slots[N];
    for (size_t i = 0; i < N; ++i) {
        slots[i] = pool.acquire();
        assert(slots[i] != nullptr);
        assert(pool.available() == N - i - 1);
    }

    assert(pool.acquire() == nullptr);
    assert(pool.available() == 0);

    for (size_t i = 0; i < N; ++i)
        for (size_t j = i + 1; j < N; ++j)
            assert(slots[i] != slots[j]);

    constexpr size_t expected_stride = align_up(slot_size, alignof(std::max_align_t));
    for (size_t i = 0; i + 1 < N; ++i) {
        uintptr_t a = reinterpret_cast<uintptr_t>(slots[i]);
        uintptr_t b = reinterpret_cast<uintptr_t>(slots[i + 1]);
        assert(a > b);
        assert(a - b == expected_stride);
    }

    for (size_t i = 0; i < N; ++i)
        *static_cast<uint64_t*>(slots[i]) = i * 0xDEAD;
    for (size_t i = 0; i < N; ++i)
        assert(*static_cast<uint64_t*>(slots[i]) == i * 0xDEAD);

    pool.release(slots[0]);
    assert(pool.available() == 1);
    void* back = pool.acquire();
    assert(back == slots[0]);
    assert(pool.available() == 0);

    for (int cycle = 0; cycle < 3; ++cycle) {
        for (size_t i = 0; i < N; ++i) pool.release(slots[i]);
        assert(pool.available() == N);
        for (size_t i = 0; i < N; ++i) assert(pool.acquire() != nullptr);
        assert(pool.available() == 0);
        assert(pool.acquire() == nullptr);
    }

    std::cout << "pool: all tests passed (stride=" << expected_stride << ")\n";
}

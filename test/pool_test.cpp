#include "../allocators/pool.hpp"
#include <cassert>
#include <iostream>

int main() {
    constexpr size_t N = 5;
    Pool<sizeof(size_t), N> pool;

    assert(pool.available() == N);

    void* slots[N];
    for (size_t i = 0; i < N; ++i) {
        slots[i] = pool.acquire();
        assert(slots[i] != nullptr);
    }
    assert(pool.available() == 0);

    assert(pool.acquire() == nullptr);

    for (size_t i = 0; i < N; ++i)
        for (size_t j = i + 1; j < N; ++j)
            assert(slots[i] != slots[j]);

    pool.release(slots[0]);
    assert(pool.available() == 1);
    void* reacquired = pool.acquire();
    assert(reacquired == slots[0]);
    assert(pool.available() == 0);

    for (size_t i = 0; i < N; ++i) pool.release(slots[i]);
    assert(pool.available() == N);
    for (size_t i = 0; i < N; ++i) assert(pool.acquire() != nullptr);
    assert(pool.available() == 0);

    std::cout << "pool: all tests passed\n";
}

#include "../allocators/arena.hpp"
#include "../allocators/pool.hpp"
#include "../allocators/pmr_adapter.hpp"
#include <memory_resource>
#include <vector>
#include <cassert>
#include <iostream>
#include <cstdlib>

static int global_new_count = 0;

void* operator new(std::size_t size) {
    ++global_new_count;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept              { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main() {
    constexpr size_t capacity = 8192;
    std::byte* buf = static_cast<std::byte*>(std::malloc(capacity));

    {
        Arena arena(buf, capacity);
        ArenaPmrResource rsrc(arena);

        global_new_count = 0;

        std::pmr::vector<int> v(&rsrc);
        v.reserve(128);
        for (int i = 0; i < 128; ++i) v.push_back(i);

        assert(global_new_count == 0);
        for (int i = 0; i < 128; ++i) assert(v[i] == i);
        assert(reinterpret_cast<std::byte*>(v.data()) >= buf);
        assert(reinterpret_cast<std::byte*>(v.data()) <  buf + capacity);

        rsrc.reset();
        global_new_count = 0;

        std::pmr::vector<int> v2(&rsrc);
        v2.reserve(128);
        for (int i = 0; i < 128; ++i) v2.push_back(i * 2);

        assert(global_new_count == 0);
        assert(v2.data() == v.data());
        for (int i = 0; i < 128; ++i) assert(v2[i] == i * 2);
    }

    {
        constexpr size_t SlotSize  = 64;
        constexpr size_t SlotCount = 16;
        Pool<SlotSize, SlotCount> pool;
        PoolPmrResource rsrc(pool);

        global_new_count = 0;

        assert(pool.available() == SlotCount);
        void* p1 = rsrc.allocate(SlotSize, alignof(std::max_align_t));
        void* p2 = rsrc.allocate(SlotSize, alignof(std::max_align_t));
        assert(p1 != nullptr && p2 != nullptr && p1 != p2);
        assert(pool.available() == SlotCount - 2);

        rsrc.deallocate(p1, SlotSize, alignof(std::max_align_t));
        assert(pool.available() == SlotCount - 1);
        rsrc.deallocate(p2, SlotSize, alignof(std::max_align_t));
        assert(pool.available() == SlotCount);

        assert(global_new_count == 0);
    }

    std::cout << "pmr: all tests passed\n";
    std::free(buf);
}

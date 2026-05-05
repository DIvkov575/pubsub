#include "../allocators/arena.hpp"
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
void operator delete(void* p) noexcept          { std::free(p); }
void operator delete(void* p, std::size_t)  noexcept { std::free(p); }

int main() {
    constexpr size_t capacity = 4096;
    std::byte* buf = static_cast<std::byte*>(std::malloc(capacity));
    Arena arena(buf, capacity);
    ArenaPmrResource rsrc(arena);

    global_new_count = 0;

    std::pmr::vector<int> v(&rsrc);
    v.reserve(64);
    for (int i = 0; i < 64; ++i) v.push_back(i);

    assert(global_new_count == 0);
    for (int i = 0; i < 64; ++i) assert(v[i] == i);

    assert(reinterpret_cast<std::byte*>(v.data()) >= buf);
    assert(reinterpret_cast<std::byte*>(v.data()) <  buf + capacity);

    std::cout << "global operator new calls: " << global_new_count << "\n";
    std::cout << "pmr: all tests passed\n";

    std::free(buf);
}

#include "../allocators/arena.hpp"
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <iostream>

int main() {
    constexpr size_t capacity = 1024;
    std::byte* buf = reinterpret_cast<std::byte*>(malloc(capacity));
    Arena arena(buf, capacity);

    // default alignment
    void* p1 = arena.allocate(1);
    assert(reinterpret_cast<uintptr_t>(p1) % alignof(std::max_align_t) == 0);

    // explicit alignment
    void* p2 = arena.allocate(sizeof(double), alignof(double));
    assert(reinterpret_cast<uintptr_t>(p2) % alignof(double) == 0);

    // used() tracks consumed bytes
    size_t used_before = arena.used();
    arena.allocate(32);
    assert(arena.used() == used_before + 32);

    // remaining() is consistent
    assert(arena.used() + arena.remaining() == capacity);

    // reset returns to start
    arena.reset();
    assert(arena.used() == 0);

    // reallocate after reset gives same address
    void* p3 = arena.allocate(1);
    assert(p3 == p1);

    // alloc<T> helper - alignment and writability
    arena.reset();
    int* ints = arena.alloc<int>(10);
    assert(reinterpret_cast<uintptr_t>(ints) % alignof(int) == 0);
    for (int i = 0; i < 10; ++i) ints[i] = i;
    for (int i = 0; i < 10; ++i) assert(ints[i] == i);

    std::cout << "arena: all tests passed\n";
    free(buf);
}

#include "../allocators/arena.hpp"
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <iostream>

static bool is_aligned(void* p, std::size_t align) {
    return reinterpret_cast<uintptr_t>(p) % align == 0;
}

int main() {
    constexpr size_t capacity = 4096;
    std::byte* buf = reinterpret_cast<std::byte*>(malloc(capacity));
    Arena arena(buf, capacity);

    void* p = arena.allocate(1);
    assert(is_aligned(p, alignof(std::max_align_t)));

    arena.reset();
    for (size_t align : {1, 2, 4, 8, 16, 32, 64}) {
        arena.reset();
        arena.allocate(1, 1);
        void* q = arena.allocate(1, align);
        assert(is_aligned(q, align));
    }

    arena.reset();
    arena.allocate(1, 1);
    void* q = arena.allocate(1, 64);
    assert(is_aligned(q, 64));
    size_t expected = reinterpret_cast<uintptr_t>(q)
                    - reinterpret_cast<uintptr_t>(buf) + 1;
    assert(arena.used() == expected);
    assert(arena.used() > 2);

    arena.reset();
    for (int i = 0; i < 8; ++i) {
        arena.allocate(17, 1);
        assert(arena.used() + arena.remaining() == capacity);
    }

    arena.reset();
    void* first[4];
    for (int i = 0; i < 4; ++i) first[i] = arena.alloc<double>();

    for (int cycle = 0; cycle < 3; ++cycle) {
        arena.reset();
        assert(arena.used() == 0);
        for (int i = 0; i < 4; ++i)
            assert(arena.alloc<double>() == first[i]);
    }

    arena.reset();
    int*    ints = arena.alloc<int>(8);
    double* dbls = arena.alloc<double>(4);
    assert(is_aligned(ints, alignof(int)));
    assert(is_aligned(dbls, alignof(double)));
    assert(reinterpret_cast<std::byte*>(dbls) >=
           reinterpret_cast<std::byte*>(ints) + 8 * sizeof(int));
    for (int i = 0; i < 8; ++i) ints[i] = i;
    for (int i = 0; i < 4; ++i) dbls[i] = i * 1.5;
    for (int i = 0; i < 8; ++i) assert(ints[i] == i);
    for (int i = 0; i < 4; ++i) assert(dbls[i] == i * 1.5);

    std::cout << "arena: all tests passed\n";
    free(buf);
}

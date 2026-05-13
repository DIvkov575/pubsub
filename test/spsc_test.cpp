#include "../queues/spsc.hpp"
#include <cassert>
#include <iostream>
#include <thread>

static void test_empty_and_exhaustion() {
    SPSCQueue<int, 8> q;
    assert(q.empty());
    assert(q.size_approx() == 0);

    int val;
    assert(!q.try_pop(val));

    assert(q.try_push(42));
    assert(!q.empty());
    assert(q.size_approx() == 1);
}

static void test_fill_and_drain() {
    constexpr size_t N = 32;
    SPSCQueue<size_t, N> q;

    for (size_t i = 0; i < N; ++i)
        assert(q.try_push(i));

    assert(q.size_approx() == N);
    assert(!q.empty());
    assert(!q.try_push(99));

    for (size_t i = 0; i < N; ++i) {
        size_t v;
        assert(q.try_pop(v));
        assert(v == i);
    }

    assert(q.empty());
    assert(q.size_approx() == 0);

    size_t v;
    assert(!q.try_pop(v));
}

static void test_wraparound() {
    constexpr size_t N = 16;
    SPSCQueue<size_t, N> q;

    size_t pushed = 0, popped = 0;

    for (size_t i = 0; i < N / 2; ++i) q.try_push(pushed++);

    for (int round = 0; round < 6; ++round) {
        for (size_t i = 0; i < N / 2; ++i) {
            size_t v;
            assert(q.try_pop(v));
            assert(v == popped++);
        }
        for (size_t i = 0; i < N / 2; ++i) q.try_push(pushed++);
    }

    while (true) {
        size_t v;
        if (!q.try_pop(v)) break;
        assert(v == popped++);
    }

    assert(pushed == popped);
    assert(q.empty());
}

static void test_interleaved() {
    SPSCQueue<int, 4> q;
    for (int i = 0; i < 1000; ++i) {
        assert(q.try_push(i));
        int v;
        assert(q.try_pop(v));
        assert(v == i);
        assert(q.empty());
    }
}

static void test_stress() {
    constexpr size_t ITEMS = 10'000'000;
    constexpr size_t CAP   = 1024;
    SPSCQueue<uint64_t, CAP> q;

    std::thread producer([&] {
        for (uint64_t i = 0; i < ITEMS; ++i)
            while (!q.try_push(i)) {}
    });

    uint64_t errors = 0;
    for (uint64_t expected = 0; expected < ITEMS; ++expected) {
        uint64_t v;
        while (!q.try_pop(v)) {}
        if (v != expected) ++errors;
    }

    producer.join();
    assert(errors == 0);
    assert(q.empty());
}

int main() {
    test_empty_and_exhaustion();
    test_fill_and_drain();
    test_wraparound();
    test_interleaved();
    std::cout << "spsc: single-threaded tests passed\n";

    test_stress();
    std::cout << "spsc: stress test passed (10M items)\n";
}

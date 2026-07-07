#ifndef VERBOSE_TEST
#define VERBOSE_TEST 0
#endif

#include <cstdint>
#include <atomic>
#include <bit>
#include <cassert>
#include <iostream>
#include <print>
#include <thread>
#include<climits>
#include<iostream>
#include "../queues/broadcast.hpp"


static void test_empty() {
    BroadcastQueue<int, 8, 1> q;
    int v;
    assert(!q.publish(5));
}

static void test_fill_and_drain() {
    constexpr size_t N = 16;
    BroadcastQueue<size_t, N> q;

    for (size_t i = 0; i < N; ++i) {
        assert(q.try_push(i));
        if constexpr(VERBOSE_TEST)
          std::cout << i << std::endl;
    }


    assert(!q.try_push(99));

    for (size_t i = 0; i < N; ++i) {
        size_t v;
        assert(q.try_pop(v));
        assert(v == i);
    }

    size_t v;
    assert(!q.try_pop(v));
}

static void test_wraparound() {
    constexpr size_t N = 8;
    BroadcastQueue<size_t, N> q;

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
}

static void test_interleaved() {
    BroadcastQueue<int, 4> q;
    for (int i = 0; i < 1000; ++i) {
        assert(q.try_push(i));
        int v;
        assert(q.try_pop(v));
        assert(v == i);
    }
}


int main() {
  BroadcastQueue<size_t, 64, 32> q;

}
 

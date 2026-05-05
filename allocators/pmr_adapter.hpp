#pragma once
#include <memory>
#include <cassert>
#include "arena.hpp"
#include "pool.hpp"

class ArenaPmrResource : public std::pmr::memory_resource {
public:
    explicit ArenaPmrResource(Arena& arena) : arena_(arena) {}

    void reset() { arena_.reset(); }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        return arena_.allocate(bytes, align);
    }
    void do_deallocate(void*, std::size_t, std::size_t) override {}
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    Arena& arena_;
};


template<typename PoolT>
class PoolPmrResource : public std::pmr::memory_resource {
public:
    explicit PoolPmrResource(PoolT& pool) : pool_(pool) {}

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        return pool_.acquire();
    }
    void do_deallocate(void* p, std::size_t, std::size_t) override {
        pool_.release(p);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    PoolT& pool_;
};

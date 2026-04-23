# Module 1 — Allocator Suite

## Learning Resources

| Resource | What to read/watch | Why |
|---|---|---|
| [HRT huge_memory_bench.cpp](https://github.com/hudson-trading/hrtbeat/blob/master/huge_memory_bench.cpp) | The whole file (~100 lines) | This is the exact benchmark you will reproduce. Read it before writing a single line. |
| [HRT Huge Pages Blog Post](https://www.hudsonrivertrading.com/hrtbeat/low-latency-optimization-part-1/) | Full post | Explains *why* TLB misses matter. The mental model behind Module 1c. |
| [Jane Street Memory Allocator Showdown](https://blog.janestreet.com/memory-allocator-showdown/) | Full post | How to benchmark allocators correctly — RSS vs heap size distinction. Directly informs Module 7 benchmark methodology. |
| [CppReference: std::pmr](https://en.cppreference.com/w/cpp/memory/memory_resource) | `memory_resource`, `monotonic_buffer_resource`, `pool_resource` | The interface you implement in Module 1d. Read before writing the adapter. |
| [CppCon 2017: "How to Write a Custom Allocator" — Bob Steagall](https://www.youtube.com/watch?v=kSWfushlvB8) | Full talk (60 min) | Best single resource on C++ allocator design. Covers arena, pool, and pmr. |

**Firm provenance:** HRT huge-pages benchmark, Jane Street memory allocator showdown
**Location:** `pulse/allocators/`

---

## 1a. Arena Allocator (`arena.hpp`)

Bump-pointer allocator over a contiguous buffer. No individual deallocation.

```cpp
class Arena {
public:
    explicit Arena(std::byte* buf, std::size_t capacity);

    void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t));
    void  reset();  // bulk free — O(1)

    std::size_t used() const;
    std::size_t remaining() const;
};
```

**Internals:**
- `ptr_` starts at `buf`, advances on each allocation
- Alignment: round `ptr_` up to next multiple of `align` before returning
- `reset()`: set `ptr_ = buf_`
- No bounds check in release build (`NDEBUG`), assert in debug

**Use case:** per-connection scratch space. Broker allocates one arena per connection from a pool-allocated backing buffer. `reset()` on disconnect.

---

## 1b. Pool Allocator / Slab (`pool.hpp`)

Fixed-size object pool. All slots pre-allocated at construction.

```cpp
template<std::size_t SlotSize, std::size_t SlotCount>
class Pool {
public:
    Pool();  // allocates backing storage, builds freelist

    void* acquire();            // O(1), returns nullptr if exhausted
    void  release(void* slot);  // O(1), pushes back to freelist

    std::size_t available() const;
};
```

**Internals:**
- Backing storage: `alignas(64) std::byte storage_[SlotSize * SlotCount]`
- Freelist: intrusive singly-linked list stored inside free slots
- `acquire()`: pop head of freelist
- `release()`: push to head of freelist
- Each slot `alignas(64)` — prevents false sharing when slots used on different cores

**Use case:** message frame buffers. Every incoming TCP packet gets a slot. Slot released after message delivered to all subscribers.

---

## 1c. Huge-Page Backed Memory (`hugepage.hpp`)

`mmap`-based allocator returning 2MiB-page-aligned memory. Used as backing storage for Arena and Pool.

```cpp
class HugePageBuffer {
public:
    explicit HugePageBuffer(std::size_t size);  // rounds up to 2MiB boundary
    ~HugePageBuffer();                           // munmap

    std::byte* data();
    std::size_t size() const;

private:
    void*       ptr_;
    std::size_t size_;
};
```

**Internals:**
- `mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB, -1, 0)`
- Fallback if `MAP_HUGETLB` fails: retry without it + `madvise(ptr, size, MADV_HUGEPAGE)`
- Log which path was taken at construction (important for benchmarking)

**HRT angle:** their public `huge_memory_bench.cpp` measures 4.5× latency reduction on random-access workloads. Reproduce this in Module 7.

---

## 1d. `std::pmr` Adapter (`pmr_adapter.hpp`)

Wraps Arena and Pool as `std::pmr::memory_resource` so they work with STL containers.

```cpp
class ArenaPmrResource : public std::pmr::memory_resource {
public:
    explicit ArenaPmrResource(Arena& arena);

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override;
    void  do_deallocate(void* p, std::size_t bytes, std::size_t align) override;
    bool  do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    Arena& arena_;
};

class PoolPmrResource : public std::pmr::memory_resource { /* same shape */ };
```

**Use case:**
```cpp
HugePageBuffer   backing(4 << 20);           // 4MiB huge-page buffer
Arena            arena(backing.data(), backing.size());
ArenaPmrResource rsrc(arena);

std::pmr::unordered_map<std::string, int> map(&rsrc);  // zero malloc
```

**Jane Street angle:** eliminates GC pressure by giving explicit lifetime control — same motivation as their allocator work, expressed through C++ standard interface.

---

## Tests

- `arena_test.cpp`: allocate N objects, check alignment, check used(), reset and reallocate
- `pool_test.cpp`: acquire all slots, verify nullptr on exhaustion, release and reacquire
- `hugepage_test.cpp`: allocate, touch every page, verify no segfault; check `/proc/self/smaps` for `AnonHugePages`
- `pmr_test.cpp`: `std::pmr::vector<int>` backed by arena, verify no calls to global `operator new`

## Benchmark (Module 7 feeds from here)

- Allocate + free 10M objects: Arena vs Pool vs `malloc` vs `std::allocator`
- Random-access 32GiB of doubles: huge-page vs standard pages — reproduce HRT's 4.5× result
- Measure with `perf stat -e dTLB-load-misses`

# Module 2 — Lock-Free Queues

## Learning Resources

| Resource | What to read/watch | Why |
|---|---|---|
| [Dmitry Vyukov — Bounded MPMC Queue](https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue) | Full post + code | The exact algorithm you implement in Module 2b. Read the annotated C code carefully. |
| [Jeff Preshing — Introduction to Lock-Free Programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/) | Full post | Essential mental model before touching `std::atomic`. Read first. |
| [Jeff Preshing — Memory Ordering at Compile Time](https://preshing.com/20120625/memory-ordering-at-compile-time/) | Full post | Explains why `memory_order_relaxed` vs `acquire/release` matters. Directly applies to SPSC indices. |
| [Jeff Preshing — Memory Barriers Are Like Source Control Operations](https://preshing.com/20120710/memory-barriers-are-like-source-control-operations/) | Full post | The best intuition-building analogy for acquire/release semantics. |
| [LMAX Disruptor Technical Paper](https://lmax-exchange.github.io/disruptor/disruptor.html) | Sections 1–4 | The design behind Module 2c. Explains ring buffer + independent consumer cursors + mechanical sympathy. |
| [Martin Fowler — The LMAX Architecture](https://martinfowler.com/articles/lmax.html) | Full post | Higher-level context for why the Disruptor was built. Useful for explaining Module 2c to a recruiter. |
| [CppCon 2024 — MPMC Lock-Free Atomic Queue (Erez Strauss)](https://isocpp.org/blog/2024/08/cppcon-2024-multi-producer-multi-consumer-lock-free-atomic-queue-erez-strau) | Talk summary | Contemporary take on MPMC design with C++17 atomics. Cross-reference against Vyukov. |

**Firm provenance:** Optiver CppCon 2024 (SPSC per pipeline stage), Dmitry Vyukov MPMC design, LMAX Disruptor (broadcast)
**Location:** `pulse/queues/`

---

## 2a. SPSC Ring Buffer (`spsc.hpp`)

Single-producer single-consumer. Zero locks. Fixed capacity.

```cpp
template<typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");
public:
    bool try_push(const T& item);   // producer side
    bool try_pop(T& item);          // consumer side

    std::size_t size_approx() const;
    bool        empty() const;
};
```

**Internals:**
```cpp
private:
    alignas(64) std::atomic<uint64_t> head_{0};  // written by producer
    alignas(64) std::atomic<uint64_t> tail_{0};  // written by consumer
    alignas(64) T slots_[Capacity];
```
- `try_push`: load `tail_` (relaxed), check `head_ - tail_ < Capacity`, write slot, store `head_` (release)
- `try_pop`: load `head_` (acquire), check `head_ > tail_`, read slot, store `tail_` (release)
- Separate cache lines for head/tail prevents false sharing between producer and consumer cores

**Optiver pattern:** one SPSC instance per pipeline stage boundary — network recv → signal compute → order send. No shared state crosses stage boundaries.

---

## 2b. MPMC Queue (`mpmc.hpp`)

Multi-producer multi-consumer. Per-slot sequence numbers. Dmitry Vyukov's design.

```cpp
template<typename T, std::size_t Capacity>
class MPMCQueue {
    static_assert(std::has_single_bit(Capacity));
public:
    bool try_push(const T& item);
    bool try_pop(T& item);
};
```

**Internals:**
```cpp
struct alignas(64) Slot {
    std::atomic<uint64_t> sequence;
    T                     data;
};

alignas(64) Slot                  slots_[Capacity];
alignas(64) std::atomic<uint64_t> enqueue_pos_{0};
alignas(64) std::atomic<uint64_t> dequeue_pos_{0};
```
- `try_push`: CAS on `enqueue_pos_`, then write slot when `slot.sequence == pos`
- `try_pop`: CAS on `dequeue_pos_`, then read slot when `slot.sequence == pos + 1`
- Sequence number per slot eliminates ABA problem without hazard pointers

**Use case:** N publisher connections pushing frames to broker dispatch thread.

---

## 2c. Broadcast Queue / Disruptor (`broadcast.hpp`)

One producer, N consumers each independently read every message. LMAX Disruptor pattern.

```cpp
template<typename T, std::size_t Capacity>
class BroadcastQueue {
public:
    using ConsumerToken = std::size_t;  // opaque cursor handle

    ConsumerToken add_consumer();       // register new consumer, returns cursor id
    void          remove_consumer(ConsumerToken);

    // producer side
    bool publish(const T& item);        // blocks if any consumer is full-capacity behind

    // consumer side
    bool consume(ConsumerToken, T& item);
};
```

**Internals:**
```cpp
alignas(64) std::atomic<uint64_t>              write_cursor_{0};
alignas(64) std::atomic<uint64_t>              read_cursors_[MaxConsumers];
alignas(64) T                                  slots_[Capacity];
```
- Producer checks `min(read_cursors_)` before writing — stalls if slowest consumer is `Capacity` behind
- Each consumer independently advances its own cursor
- No copying — all consumers read from the same slot
- **LMAX Disruptor:** this is their core insight — one ring, multiple independent cursors, mechanical sympathy

**Use case:** broker fan-out. One write path from dispatch thread, N independent read paths per subscriber send thread. Zero copies of message data.

---

## 2d. Hazard Pointers (stretch, `hazard.hpp`)

Safe memory reclamation for dynamically-sized lock-free structures. C++26 `std::hazard_pointer` interface.

```cpp
// Wraps std::hazard_pointer (C++26) or a manual implementation
template<typename T>
class HazardPtr {
public:
    T*   protect(std::atomic<T*>& src);  // set hazard, load pointer safely
    void reset();                         // clear hazard
    static void retire(T* ptr);          // schedule for reclamation
};
```

**When needed:** if the broadcast queue or MPMC queue uses dynamically allocated nodes (e.g. for unbounded variant). For fixed-capacity ring buffers above, hazard pointers are not needed — nodes are never freed individually.

**Firm angle:** C++26 standardized from Folly's production implementation (Meta). Very few personal projects use this — strong differentiator if included.

---

## Tests

- `spsc_test.cpp`: single-threaded push/pop correctness; two-thread stress test 10M items, verify count and values
- `mpmc_test.cpp`: 4 producers × 4 consumers, 1M items each, verify all items received exactly once
- `broadcast_test.cpp`: 1 producer, 10 consumers, 1M items, verify each consumer sees all items in order
- All tests: run under ThreadSanitizer (`-fsanitize=thread`)

## Benchmark

- SPSC throughput: items/sec vs `std::mutex` + `std::queue`
- MPMC throughput: scale producers and consumers 1→8
- Broadcast: fan-out cost vs N consumers (1, 10, 100)
- Cache behavior: `perf stat -e cache-misses` with/without `alignas(64)` — demonstrate false sharing cost

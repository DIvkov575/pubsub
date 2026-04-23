# Module 5 — Broker

## Learning Resources

| Resource | What to read/watch | Why |
|---|---|---|
| [HRT HRTWorker: SharedWorker Framework for Real-Time Trading](https://www.hudsonrivertrading.com/hrtbeat/hrtworker-a-sharedworker-framework/) | Full post | HRT's real fan-out system — the direct inspiration for the broker's dispatch architecture. |
| [Optiver CppCon 2024 — When Nanoseconds Matter](https://optiver.com/working-at-optiver/career-hub/designing-low-latency-cpp-systems/) | Full talk | Optiver's staged pipeline design: distinct thread per stage, SPSC between stages, no shared mutable state. The philosophy behind Module 5's dispatch loop. |
| [C++ Design Patterns for Low-Latency Applications (arxiv)](https://arxiv.org/pdf/2309.04259) | Sections 3–5 | Academic survey of patterns used in production trading systems. The "thread-per-stage" and "polling loop" patterns described here are exactly what the broker implements. |
| [`_mm_pause()` and spin-wait loops (Intel)](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_pause) | PAUSE intrinsic entry | One instruction, used in every HFT spin loop. Understand why it exists before using it in the dispatch loop. |

**Firm provenance:** HRT HRTWorker (real-time fan-out), Optiver pipeline architecture (staged dispatch), universal market data handler pattern (drop counters, no backpressure to publisher)
**Location:** `pulse/broker/`

---

## Architecture

```
[Publisher conn]──┐
[Publisher conn]──┤  MPMC queue   ┌──[Dispatch thread]──┐
[Publisher conn]──┘  (Module 2b)  │                      ├──Broadcast queue (topic A)──[Subscriber send threads]
                                   │                      ├──Broadcast queue (topic B)──[Subscriber send threads]
                  ┌────────────────┘                      └──Broadcast queue (topic C)──[Subscriber send threads]
[Subscriber conn]─┤  epoll loop
[Subscriber conn]─┤  (Module 4)
[Subscriber conn]─┘
```

Three distinct thread roles — no thread does more than one job:
1. **IO threads** — accept connections, recv frames into pool slots, push to MPMC queue, send DELIVER frames
2. **Dispatch thread** — drains MPMC queue, looks up topic, pushes frame pointer into broadcast queue
3. **Send threads** — drain broadcast queue per topic, write DELIVER frames to subscriber fds

---

## Topic Registry (`topic_registry.hpp`)

```cpp
class TopicRegistry {
public:
    // Returns existing or newly created broadcast queue for topic
    BroadcastQueue<Frame*, QUEUE_CAPACITY>* get_or_create(std::string_view topic);

    // Returns nullptr if topic doesn't exist
    BroadcastQueue<Frame*, QUEUE_CAPACITY>* find(std::string_view topic) const;

    void remove(std::string_view topic);

    std::vector<std::string_view> topics() const;
};
```

**Internals:**
- `std::pmr::unordered_map<std::string, BroadcastQueue*>` backed by arena allocator (Module 1a)
- Broadcast queues themselves allocated from pool
- Read path (dispatch thread) is lock-free — only writes (new topic creation) take a lock
- **HRT HRTWorker angle:** this is their SharedWorker's topic table — map of channel names to subscriber groups

---

## Connection State (`broker.hpp`)

```cpp
struct Connection {
    int      fd;
    Arena    scratch;           // per-connection arena, reset on disconnect
    void*    read_buf;          // pool slot for incoming frames
    std::vector<std::string_view> subscriptions;  // topics this connection subscribed to
    uint64_t last_heartbeat_ns; // __rdtsc() timestamp of last HEARTBEAT
    bool     is_publisher;

    // per-connection drop counters (exposed via STATS opcode)
    std::atomic<uint64_t> messages_delivered{0};
    std::atomic<uint64_t> messages_dropped{0};
};
```

**Arena usage:** `Connection::scratch` is an arena allocated from the pool. Used for temporary allocations during frame processing — topic lookup buffers, subscription list updates. Reset on disconnect. Zero `malloc`.

---

## Dispatch Thread

```cpp
void dispatch_loop() {
    Frame* frame;
    while (running_) {
        while (inbound_.try_pop(frame)) {
            auto* queue = registry_.find(frame->topic());
            if (!queue) {
                pool_.release(frame->buf);  // unknown topic, drop
                continue;
            }
            if (!queue->publish(frame)) {
                // slowest consumer is full — drop oldest
                Frame* dropped;
                queue->force_publish(frame, &dropped);
                connections_[dropped->publisher_fd].messages_dropped.fetch_add(1, std::memory_order_relaxed);
                pool_.release(dropped->buf);
            }
        }
        _mm_pause();  // spin hint — avoids hammering bus on empty queue
    }
}
```

**`_mm_pause()`:** x86 PAUSE instruction in spin loops — signals to the CPU that this is a spin-wait, reducing power and improving performance of the other hyperthread. Used in every HFT spin loop.

---

## Backpressure Policy

No backpressure to publisher. Drop oldest message for slow subscribers.

**Why:** this is the universal market data handler pattern. A strategy process that can't keep up with the feed doesn't slow down the feed — it misses ticks. HFT firms track drop counters on every subscriber; a subscriber with nonzero drops gets investigated, not slowed down.

```cpp
// Exposed via STATS response
struct SubscriberStats {
    uint64_t delivered;
    uint64_t dropped;
    uint64_t queue_depth;
};
```

---

## Heartbeat / Connection Health

```cpp
void heartbeat_loop() {
    while (running_) {
        uint64_t now = rdtsc_ns();
        for (auto& [fd, conn] : connections_) {
            if (now - conn.last_heartbeat_ns > HEARTBEAT_TIMEOUT_NS) {
                close_connection(fd);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

**HRT Non-COD Server angle:** that intern project handled upstream downtime by tracking connection state and bootstrapping from logs on reconnect. Same heartbeat-driven health tracking here, simpler.

---

## Broker Lifecycle

```cpp
class Broker {
public:
    explicit Broker(BrokerConfig config);

    void start();   // spawns IO threads, dispatch thread, heartbeat thread
    void stop();    // drains queues, closes connections, joins threads

    BrokerStats stats() const;
};

struct BrokerConfig {
    uint16_t    port          = 9000;
    int         io_threads    = 4;      // one per core
    std::size_t pool_slots    = 65536;  // pre-allocated message buffers
    std::size_t slot_size     = 4096;   // bytes per slot
    bool        use_hugepages = true;
    bool        use_io_uring  = false;  // stretch
};
```

---

## Tests

- `broker_test.cpp`:
  - 1 publisher, 1 subscriber, 1M messages — verify all delivered, zero drops
  - 1 publisher, 100 subscribers on same topic — all receive all messages
  - Publisher to nonexistent topic — frame dropped, no crash
  - Subscriber disconnect mid-stream — no crash, remaining subscribers unaffected
  - Heartbeat timeout — dead connection closed within 2× timeout
- Integration: publisher sends `__rdtsc()` as payload, subscriber measures p99 latency

## Benchmark

- 1:1 throughput (messages/sec)
- 1:100 fan-out throughput
- Drop rate under intentional slow subscriber
- Memory usage: verify pool never exhausted at steady state

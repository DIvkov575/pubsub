# Module 6 — Client Library

## Learning Resources

| Resource | What to read/watch | Why |
|---|---|---|
| [Jane Street — Safe at Any Speed](https://www.janestreet.com/tech-talks/safe-at-any-speed/) | Second half of talk (zero-alloc callbacks section) | The callback contract in this module — `span` into pool buffer, valid only within scope — is directly from their IO buffer design. Watch to understand *why* the contract is structured this way. |
| [HRT Intern: Signal Stream Compression](https://www.hudsonrivertrading.com/hrtbeat/intern-spotlight-software-engineering-summer-projects/) | Charlotte Wang's project section | The `pub.publish("factor.momentum", span<double>)` API shape comes from this. Shows what real signal streams look like. |
| [CppReference: std::span](https://en.cppreference.com/w/cpp/container/span) | Full page | You use `std::span` throughout the client API. Understand the non-owning view semantics — this is why the zero-copy contract works. |
| [NASDAQ ITCH Sample Data](https://itch.io) | Free sample files from NASDAQ | The `tick_publisher.cpp` example replays real ITCH data. Download a sample file to use as your demo dataset. Actual link: `ftp://emi.nasdaq.com/ITCH/` |

**Firm provenance:** HRT signal stream (arrays of doubles over named streams), Jane Street zero-alloc callbacks (span into IO buffer, no heap in hot path)
**Location:** `pulse/client/`

---

## Design Principles

- Zero allocation in the subscriber callback hot path
- Payload delivered as `std::span` into pool buffer — caller reads in place, no copy
- Topic names are quant-meaningful: `SPY.bid`, `factor.momentum`, `sim.pnl`
- Publisher batches frames where possible — `burst_publish` for signal vector updates

---

## Publisher (`publisher.hpp`)

```cpp
class Publisher {
public:
    Publisher(std::string_view host, uint16_t port);
    ~Publisher();

    // Publish raw bytes to a topic
    void publish(std::string_view topic, std::span<const std::byte> payload);

    // Convenience: publish array of doubles — quant signal use case
    void publish(std::string_view topic, std::span<const double> values);

    // Publish N messages as a burst — broker marks last with FLAG_LAST_IN_BURST
    void burst_publish(std::string_view topic,
                       std::initializer_list<std::span<const std::byte>> payloads);

    // Block until broker ACKs (for testing/benchmarking only — not hot path)
    void publish_sync(std::string_view topic, std::span<const std::byte> payload);

    bool connected() const;
    void reconnect();  // blocking reconnect with backoff
};
```

**Quant use case:**
```cpp
Publisher pub("localhost", 9000);

// Publish momentum factor vector for 500 instruments
std::array<double, 500> momentum = compute_momentum(prices);
pub.publish("factor.momentum", std::span{momentum});

// Publish a single tick
struct Tick { uint64_t ts; double bid; double ask; uint32_t bid_sz; uint32_t ask_sz; };
Tick tick{rdtsc_ns(), 412.50, 412.51, 100, 200};
pub.publish("SPY.tick", as_bytes(std::span{&tick, 1}));
```

**Internals:**
- Maintains one persistent TCP connection
- Frames encoded directly into pool-allocated send buffer (Module 1b)
- Non-blocking send; queues in local SPSC (Module 2a) if socket not ready
- `TCP_NODELAY` always set

---

## Subscriber (`subscriber.hpp`)

```cpp
using MessageCallback = std::function<void(std::string_view topic,
                                            std::span<const std::byte> payload)>;

class Subscriber {
public:
    Subscriber(std::string_view host, uint16_t port);
    ~Subscriber();

    void subscribe(std::string_view topic);
    void unsubscribe(std::string_view topic);

    // Register callback. Called on IO thread — must return quickly, no blocking.
    void on_message(MessageCallback cb);

    // Drives the event loop. Blocks until stop() called.
    void run();
    void stop();

    // Non-blocking: process pending events, return immediately.
    // Use in your own event loop.
    void poll();

    SubscriberStats stats() const;
};
```

**Zero-alloc callback contract:**
- `payload` span points into a pool buffer slot
- Valid **only within the callback scope**
- Caller must copy if they need the data to outlive the callback
- **Jane Street pattern:** same contract as their IO buffer approach — "flat buffer representation, pointer adjustments rather than object creation"

**Quant use case:**
```cpp
Subscriber sub("localhost", 9000);
sub.subscribe("factor.momentum");
sub.subscribe("SPY.tick");

sub.on_message([](std::string_view topic, std::span<const std::byte> payload) {
    if (topic == "factor.momentum") {
        // reinterpret in place — zero copy
        auto factors = std::span<const double>{
            reinterpret_cast<const double*>(payload.data()),
            payload.size() / sizeof(double)
        };
        strategy.update_factors(factors);
    }
    else if (topic == "SPY.tick") {
        const auto& tick = *reinterpret_cast<const Tick*>(payload.data());
        strategy.on_tick(tick);
    }
});

sub.run();
```

---

## Latency Probe Utility (`probe.hpp`)

Embeds a TSC timestamp in the payload; subscriber measures wire latency. Used by the benchmark harness (Module 7).

```cpp
struct ProbePayload {
    uint64_t send_tsc;      // __rdtsc() at publisher
    uint64_t sequence;      // monotonic counter
    uint8_t  padding[48];   // fill to 64 bytes (one cache line)
};

// Publisher side
void publish_probe(Publisher& pub, std::string_view topic, uint64_t seq);

// Subscriber side — returns latency in nanoseconds
uint64_t measure_latency(std::span<const std::byte> payload);
```

**HRT prober angle:** their background Prober service continuously measures latency between components. This utility is the same idea — continuous latency measurement, not just at benchmark time.

---

## Reconnect Behavior

Both Publisher and Subscriber implement reconnect with exponential backoff:

```cpp
void reconnect_with_backoff() {
    uint64_t backoff_ms = 10;
    while (!try_connect()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        backoff_ms = std::min(backoff_ms * 2, uint64_t{5000});
    }
    resubscribe_all();  // re-send SUBSCRIBE frames after reconnect
}
```

**HRT Non-COD Server angle:** the intern built explicit state recovery on reconnect. Same pattern here — subscriber re-registers all topics on reconnect, no manual intervention needed.

---

## Tests

- `publisher_test.cpp`: connect, publish 1M messages, verify broker received all
- `subscriber_test.cpp`: subscribe, receive 1M messages, verify callback called with correct topic/payload
- `roundtrip_test.cpp`: publisher sends probe payloads, subscriber measures latency distribution
- `reconnect_test.cpp`: kill broker mid-stream, verify client reconnects and resumes
- Zero-alloc verification: override global `operator new`, assert zero calls during `on_message` callback

## Example Programs (`pulse/examples/`)

```
examples/
├── signal_publisher.cpp   # publishes random factor vectors on timer
├── signal_subscriber.cpp  # receives factors, prints stats
├── tick_publisher.cpp     # replays ITCH sample file as tick stream
└── latency_probe.cpp      # continuous round-trip latency measurement
```

**`tick_publisher.cpp`** replays real NASDAQ ITCH 5.0 sample data (free download from NASDAQ) through the broker to subscribers. This is the quant demo — real exchange data, real binary format, your infrastructure.

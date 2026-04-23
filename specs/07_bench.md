# Module 7 — Benchmark Harness

## Learning Resources

| Resource | What to read/watch | Why |
|---|---|---|
| [CppCon 2015 — Chandler Carruth: "Tuning C++: Benchmarks, CPUs, and Compilers"](https://www.programmingtalks.org/talk/cppcon-2015-chandler-carruth-tuning-c-benchmarks-and-cpus-and-compilers-oh-my/) | Full talk (60 min) | The canonical resource on avoiding benchmark pitfalls — dead code elimination, compiler reordering, measurement noise. Watch before writing a single benchmark. The `DoNotOptimize` / `ClobberMemory` helpers come from this talk. |
| [HRT huge_memory_bench.cpp](https://github.com/hudson-trading/hrtbeat/blob/master/huge_memory_bench.cpp) | Full file | The benchmark you reproduce in Scenario 6. Study the methodology: how they prevent prefetcher tricks, how they measure wall time. |
| [HdrHistogram — Gil Tene](http://hdrhistogram.org/) | README + "Why HDR?" section | Explains why average and stddev latency metrics are misleading in trading systems. Motivates your histogram choice. |
| [RDTSC deep dive — Preshing](https://preshing.com/20100511/locks-arent-slow-lock-contention-is/) | TSC section | Why `rdtsc` is the right tool for nanosecond-level measurement and where it can mislead you (out-of-order execution, TSC skew). |
| [`perf` tutorial](https://perf.wiki.kernel.org/index.php/Tutorial) | "Counting events" and "perf stat" sections | You use `perf stat -e dTLB-load-misses,cache-misses` throughout the benchmarks. Understand the output before quoting numbers to a recruiter. |

**Firm provenance:** HRT Prober (continuous latency measurement), HRT huge_memory_bench.cpp (reproducible public benchmark), TSC timestamps (universal HFT — no syscall), HDR Histogram (LMAX, Azul)
**Location:** `pulse/bench/`

---

## Timestamp Infrastructure (`tsc.hpp`)

All latency measurements use the CPU timestamp counter — no syscalls, ~1ns resolution.

```cpp
inline uint64_t rdtsc() {
    uint64_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (hi << 32) | lo;
}

// Calibrate TSC ticks per nanosecond at startup
struct TscCalibration {
    double ticks_per_ns;

    static TscCalibration measure();  // sleep 10ms, compare TSC delta to clock_gettime delta
    uint64_t to_ns(uint64_t ticks) const { return ticks / ticks_per_ns; }
};
```

**Why not `clock_gettime`?** It makes a syscall (or vDSO call) — adds noise. TSC is a register read. Every HFT latency measurement tool uses this.

**Caveat:** `rdtsc` is not serializing. Use `rdtscp` or `cpuid; rdtsc` when you need an exact barrier. Documented in comments.

---

## HDR Histogram (`histogram.hpp`)

Captures full latency distribution without losing tail data. Header-only, no external dependency.

```cpp
class HdrHistogram {
public:
    // value_range: max recordable value in ns (e.g. 10'000'000 = 10ms)
    // precision: significant figures (1-5)
    HdrHistogram(uint64_t value_range, int precision = 3);

    void record(uint64_t value_ns);
    void record_n(uint64_t value_ns, uint64_t count);
    void reset();

    uint64_t percentile(double p) const;  // p in [0.0, 100.0]
    uint64_t p50()   const { return percentile(50.0); }
    uint64_t p99()   const { return percentile(99.0); }
    uint64_t p999()  const { return percentile(99.9); }
    uint64_t p9999() const { return percentile(99.99); }
    uint64_t max()   const;
    uint64_t min()   const;
    uint64_t mean()  const;

    void print(std::ostream& out) const;
    void write_csv(std::string_view path) const;
};
```

**LMAX angle:** Gil Tene (Azul/LMAX) built HDR Histogram specifically because average latency hides tail latency. In trading, p99.9 matters more than p50. This is the correct tool.

---

## Benchmark Scenarios

### Scenario 1: Allocator Comparison

Reproduce HRT's huge-pages benchmark.

```
bench_allocators
  arena_4k          — Arena, 4KB slots, standard pages
  arena_4k_huge     — Arena, 4KB slots, huge pages
  pool_64b          — Pool, 64B slots
  pool_4k           — Pool, 4KB slots
  malloc_baseline   — ::malloc / ::free

Metric: allocate+free 10M objects, ns/op
Also: random-access 32GiB doubles, measure runtime (reproduce HRT 4.5x result)
perf stat output: dTLB-load-misses count
```

### Scenario 2: Queue Throughput

```
bench_queues
  spsc_1p1c         — SPSC, 1 producer 1 consumer
  mpmc_2p2c         — MPMC, 2 producers 2 consumers
  mpmc_4p4c         — MPMC, 4 producers 4 consumers
  broadcast_1p10c   — Broadcast, 1 producer 10 consumers
  mutex_baseline    — std::mutex + std::queue

Metric: million messages/sec
Also: false-sharing demo — SPSC with/without alignas(64) on indices
```

### Scenario 3: End-to-End Latency (1:1)

Single publisher, single subscriber, broker on same machine. Sweep message sizes.

```
bench_e2e_latency
  payload_sizes: 64B, 256B, 1KB, 4KB, 16KB

Per size:
  - Send 1M probe frames with TSC timestamp embedded
  - Subscriber records (recv_tsc - send_tsc) per message
  - Report p50/p99/p999/max histogram
  - Report with epoll, epoll+busy_poll, io_uring
```

### Scenario 4: Fan-Out (1:N)

Single publisher, N subscribers all on same topic.

```
bench_fanout
  subscribers: 1, 10, 50, 100, 500

Per subscriber count:
  - 1M messages, 256B payload
  - Report: throughput (msgs/sec), p99 latency at publisher
  - Report: drop rate at each subscriber count
```

### Scenario 5: Contention (N:1)

N publishers, single subscriber.

```
bench_contention
  publishers: 1, 2, 4, 8, 16

Per publisher count:
  - Each publisher sends 100K messages
  - Subscriber verifies receipt of all N * 100K messages
  - Report: throughput, p99 latency per publisher
```

### Scenario 6: Memory (reproduce HRT)

```
bench_hugepages
  - Allocate 32GiB of doubles
  - Random access pattern (stride = prime number to defeat prefetcher)
  - Standard 4KB pages vs huge 2MB pages
  - Measure wall time and dTLB-load-misses
  - Expected: ~4.5x speedup (HRT result)
```

---

## Harness Runner (`bench.cpp`)

```cpp
int main(int argc, char** argv) {
    BenchConfig cfg = parse_args(argc, argv);
    TscCalibration tsc = TscCalibration::measure();

    if (cfg.scenario == "allocators") run_allocator_bench(cfg, tsc);
    if (cfg.scenario == "queues")     run_queue_bench(cfg, tsc);
    if (cfg.scenario == "e2e")        run_e2e_bench(cfg, tsc);
    if (cfg.scenario == "fanout")     run_fanout_bench(cfg, tsc);
    if (cfg.scenario == "contention") run_contention_bench(cfg, tsc);
    if (cfg.scenario == "hugepages")  run_hugepage_bench(cfg, tsc);
    if (cfg.scenario == "all")        run_all(cfg, tsc);
}
```

**Output per scenario:** console table + CSV file + gnuplot script.

---

## Gnuplot Scripts (`plot.gnuplot`)

```gnuplot
# Latency CDF — p50 through p99.99
set terminal png size 1200,600
set output "latency_cdf.png"
set xlabel "Latency (ns)"
set ylabel "Percentile"
set logscale x
plot "e2e_epoll.csv"      using 1:2 title "epoll"       with linespoints, \
     "e2e_busy_poll.csv"  using 1:2 title "busy_poll"   with linespoints, \
     "e2e_io_uring.csv"   using 1:2 title "io_uring"    with linespoints

# Fan-out throughput vs subscriber count
set output "fanout_throughput.png"
set xlabel "Subscriber Count"
set ylabel "Throughput (M msgs/sec)"
plot "fanout.csv" using 1:2 title "throughput" with linespoints
```

---

## README Benchmark Table (target)

```
| Scenario              | epoll       | epoll+busy_poll | io_uring    |
|-----------------------|-------------|-----------------|-------------|
| 1:1 p50 latency       | ~2µs        | ~800ns          | ~600ns      |
| 1:1 p99 latency       | ~10µs       | ~2µs            | ~1.5µs      |
| 1:100 throughput      | ~8M msg/s   | ~10M msg/s      | ~12M msg/s  |
| Allocator vs malloc   | 3-4x faster |                 |             |
| Hugepages TLB miss    | 4.5x fewer  |                 |             |
```

Numbers are targets — fill in with actual results.

---

## Running

```bash
# Pin to cores 0 and 1 to reduce noise
taskset -c 0,1 ./bench --scenario e2e --transport epoll --messages 1000000

# Full suite
taskset -c 0-3 ./bench --scenario all --output results/

# Reproduce HRT hugepages benchmark
./bench --scenario hugepages
```

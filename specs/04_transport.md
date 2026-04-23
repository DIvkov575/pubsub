# Module 4 — Transport

## Learning Resources

| Resource | What to read/watch | Why |
|---|---|---|
| [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) | Chapters 1–6 | The definitive reference for raw C sockets — `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`. Read before writing a single socket call. Free online. |
| [epoll man page](https://man7.org/linux/man-pages/man7/epoll.7.html) | Full page | Short and dense. Read `epoll_create1`, `epoll_ctl`, `epoll_wait`. The edge-triggered vs level-triggered distinction is critical — you want `EPOLLET`. |
| [The C10K Problem](http://www.kegel.com/c10k.html) | Skim for historical context | Why `epoll` exists. Understanding the problem it solved makes the API less mysterious. |
| [Lord of the io_uring](https://unixism.net/loti/) | "What is io_uring?" + first 3 tutorials | Best io_uring tutorial. Builds up from basics to linked SQEs. Read before Module 4c. |
| [io_uring for High-Performance DBMSs (arxiv, Dec 2025)](https://arxiv.org/html/2512.04859v1) | Abstract + Section 3 | Academic treatment of exactly when io_uring wins vs epoll. Useful for benchmarking interpretation in Module 7. |
| [SO_BUSY_POLL kernel docs](https://www.kernel.org/doc/html/latest/networking/network_testing.html) | SO_BUSY_POLL section | Official docs for the socket option you set in Module 4b. Short read. |

**Firm provenance:** `epoll` universal HFT, `SO_REUSEPORT` Redis/Nginx/HFT per-core scaling, `SO_BUSY_POLL` software kernel bypass, `io_uring` emerging trading infra
**Location:** `pulse/transport/`

---

## Design

All transport implementations share a common interface so the broker is transport-agnostic:

```cpp
struct IOEvent {
    enum class Type { ACCEPT, READ, WRITE_READY, CLOSE };
    Type    type;
    int     fd;
    std::byte* buf;     // pool slot, valid for READ events
    std::size_t len;
};

class EventLoop {
public:
    virtual ~EventLoop() = default;

    virtual void add_server(int port) = 0;
    virtual void add_connection(int fd) = 0;
    virtual void remove_connection(int fd) = 0;
    virtual void send(int fd, const std::byte* buf, std::size_t len) = 0;
    virtual void run(std::function<void(IOEvent)> on_event) = 0;
    virtual void stop() = 0;
};
```

---

## 4a. `epoll` Event Loop (`epoll_loop.hpp`)

Baseline. Standard Linux async I/O. Well-documented, widely used in trading gateways.

**Setup:**
```cpp
int epfd = epoll_create1(EPOLL_CLOEXEC);

// server socket
int sfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));  // per-core scaling
bind(sfd, ...);
listen(sfd, SOMAXCONN);
epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, {EPOLLIN, sfd});
```

**Event loop:**
```cpp
while (running_) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, timeout_ms);
    for (int i = 0; i < n; i++) {
        if (events[i].data.fd == sfd)  handle_accept();
        else                           handle_read(events[i].data.fd);
    }
}
```

**Read path:**
- `acquire()` slot from pool allocator (Module 1b)
- `recv(fd, slot, SLOT_SIZE, MSG_DONTWAIT)`
- Pass slot pointer to broker via MPMC queue (Module 2b)
- Slot released by broker after delivery

**`SO_REUSEPORT`:**
- N threads each create their own server socket on the same port with `SO_REUSEPORT`
- Kernel distributes incoming connections across sockets with no userspace coordination
- Each thread pinned to a core with `pthread_setaffinity_np`
- **Firm angle:** Redis 6.0, Nginx, and HFT gateways all use this pattern — shared-nothing per-core

---

## 4b. `SO_BUSY_POLL` (`busy_poll.hpp`)

Software-only interrupt bypass. Set on individual sockets.

```cpp
void enable_busy_poll(int fd, int budget_us = 50) {
    setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &budget_us, sizeof(budget_us));
}
```

**What it does:** instead of sleeping in the kernel waiting for a packet interrupt, the kernel spins polling the NIC receive queue for up to `budget_us` microseconds. Eliminates interrupt latency without requiring DPDK or special hardware.

**When to enable:** on subscriber send sockets and publisher receive sockets — the hot path fds.

**Firm angle:** this is the software-only kernel bypass trick used at firms that can't run DPDK. Saves 10–50µs of interrupt wakeup latency. Almost never mentioned in tutorials.

**Add to epoll loop:**
```cpp
void add_connection(int fd) override {
    enable_busy_poll(fd);          // enable before adding to epoll
    set_nonblocking(fd);
    epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, {EPOLLIN | EPOLLET, fd});
}
```

---

## 4c. `io_uring` Event Loop (`uring_loop.hpp`) — stretch goal

Post-epoll Linux async I/O. Batches syscalls via shared ring buffer with kernel.

**Key difference from epoll:** epoll tells you a fd is *ready*, then you make a syscall. `io_uring` submits the I/O operation itself to a ring — kernel executes it and posts a completion. Near-zero syscalls on the hot path.

**Setup:**
```cpp
io_uring ring;
io_uring_queue_init(QUEUE_DEPTH, &ring, IORING_SETUP_SQPOLL);  // kernel polling thread
```

**Submit recv ops upfront:**
```cpp
void arm_recv(int fd, std::byte* buf, std::size_t len) {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_recv(sqe, fd, buf, len, 0);
    sqe->user_data = reinterpret_cast<uint64_t>(buf);
}
```

**Drain completions:**
```cpp
io_uring_cqe* cqe;
while (io_uring_peek_cqe(&ring_, &cqe) == 0) {
    auto* buf = reinterpret_cast<std::byte*>(cqe->user_data);
    on_event({IOEvent::READ, fd_for(buf), buf, (std::size_t)cqe->res});
    io_uring_cqe_seen(&ring_, cqe);
    arm_recv(fd_for(buf), buf, SLOT_SIZE);  // re-arm immediately
}
```

**Linked SQEs — recv → send in one syscall:**
```cpp
// Chain recv and send as linked operations
io_uring_prep_recv(sqe1, fd, buf, len, 0);
sqe1->flags |= IOSQE_IO_LINK;
io_uring_prep_send(sqe2, fd, response, resp_len, 0);
io_uring_submit(&ring_);  // one syscall submits both
```

**`IORING_SETUP_SQPOLL`:** kernel thread polls SQ, eliminating `io_uring_submit()` syscall entirely. Zero syscalls on hot path.

---

## Socket Options Applied Everywhere

```cpp
void configure_socket(int fd) {
    int one = 1;
    // Disable Nagle — don't buffer small packets
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // Larger receive buffer
    int rcvbuf = 4 << 20;  // 4MiB
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    // Busy poll (Module 4b)
    enable_busy_poll(fd);
}
```

**`TCP_NODELAY`:** disables Nagle's algorithm — critical for latency. Without it, small frames get buffered waiting for ACKs. Every trading system sets this.

---

## Tests

- `epoll_test.cpp`: echo server, send 1M messages, verify all received in order
- `busy_poll_test.cpp`: measure latency with/without `SO_BUSY_POLL` using `__rdtsc()`
- `uring_test.cpp`: same echo server with io_uring loop, verify correctness
- All tests: `SO_REUSEPORT` with 4 threads, verify no connection drops

## Benchmark

- p50/p99/p999 round-trip latency: epoll vs epoll+busy_poll vs io_uring
- Syscall count: `strace -c` — io_uring should show near-zero vs epoll
- Connection throughput: new connections/sec with `SO_REUSEPORT` 1 vs 4 vs 8 threads

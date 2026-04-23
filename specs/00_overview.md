# Pulse — Architecture Overview

## What the system does

A publisher process sends named binary messages ("topics") to a broker.
The broker fans them out to all subscriber processes that registered interest in that topic.

```
[Publisher]  →  [Broker]  →  [Subscriber A]
                         →  [Subscriber B]
                         →  [Subscriber C]
```

That's the whole system. Every module exists to make one part of that pipeline fast and correct.

---

## How the modules stack

```
┌─────────────────────────────────────────────────────────┐
│                    CLIENT (Module 6)                     │
│         Publisher API        Subscriber API              │
└──────────────────┬──────────────────┬───────────────────┘
                   │                  │
┌──────────────────▼──────────────────▼───────────────────┐
│                  PROTOCOL (Module 3)                     │
│         encode() frame          decode() frame           │
└──────────────────┬──────────────────┬───────────────────┘
                   │                  │
┌──────────────────▼──────────────────▼───────────────────┐
│                 TRANSPORT (Module 4)                     │
│          epoll / io_uring event loop, TCP sockets        │
└──────────────────┬──────────────────┬───────────────────┘
                   │                  │
          (bytes arrive)        (bytes go out)
                   │                  │
┌──────────────────▼──────────────────▼───────────────────┐
│                   BROKER (Module 5)                      │
│   TopicRegistry   →   BroadcastQueue   →   send threads  │
└──────────────────────────┬──────────────────────────────┘
                           │  uses
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
┌─────────────┐  ┌─────────────────┐  ┌──────────────┐
│  ALLOCATORS │  │     QUEUES      │  │   ALLOCATORS │
│  (Module 1) │  │   (Module 2)    │  │  (Module 1)  │
│             │  │                 │  │              │
│ Pool — feeds│  │ MPMC — inbound  │  │ Arena — per  │
│ recv buffers│  │ SPSC — per stage│  │ connection   │
│ HugePage    │  │ Broadcast—fanout│  │ scratch      │
└─────────────┘  └─────────────────┘  └──────────────┘
```

Allocators and Queues are not layers — they are infrastructure that every other module uses internally.

---

## The life of one message

```
1. Publisher calls pub.publish("SPY.bid", data)

2. CLIENT (Module 6)
   └─ calls encode() to write a PUBLISH frame

3. PROTOCOL (Module 3)
   └─ encode() writes [ header | topic | payload ] into a pool buffer slot
      zero allocation — slot was pre-acquired from Pool allocator

4. TRANSPORT (Module 4)
   └─ epoll/io_uring sends the frame bytes over TCP to the broker

5. BROKER receives bytes (Module 5 + 4)
   └─ Transport fires READ event
   └─ Pool allocator hands out a recv buffer slot
   └─ decode() parses the frame — pointer cast, no allocation

6. BROKER dispatches (Module 5)
   └─ IO thread pushes frame pointer into MPMC queue (Module 2b)
   └─ Dispatch thread pops from MPMC queue
   └─ Looks up "SPY.bid" in TopicRegistry
   └─ Pushes frame pointer into BroadcastQueue for that topic (Module 2c)

7. BROKER fans out (Module 5)
   └─ Each subscriber's send thread pops from BroadcastQueue
   └─ encode() wraps frame as DELIVER opcode
   └─ Transport sends bytes to subscriber fd

8. SUBSCRIBER receives (Module 4 + 6)
   └─ Transport fires READ event
   └─ decode() parses DELIVER frame — pointer cast into recv pool slot
   └─ on_message callback called with span into pool slot
   └─ Callback returns → pool slot released
   └─ Zero allocations end to end
```

---

## Module dependency map

```
Module 7 (Bench)
  └─ tests all modules end to end

Module 6 (Client)
  └─ uses Module 3 (encode/decode)
  └─ uses Module 4 (TCP connection)
  └─ uses Module 1 (pool for send buffers)

Module 5 (Broker)
  └─ uses Module 4 (accept connections, recv/send)
  └─ uses Module 2 (MPMC for inbound, Broadcast for fan-out)
  └─ uses Module 1 (Pool for recv buffers, Arena per connection)
  └─ uses Module 3 (decode inbound, encode outbound)

Module 4 (Transport)
  └─ uses Module 1 (Pool — hands out slots for recv)
  └─ uses Module 3 (decode to identify frame boundaries)

Module 3 (Protocol)
  └─ no dependencies — pure header math

Module 2 (Queues)
  └─ uses Module 1 (Pool backs queue node storage)

Module 1 (Allocators)
  └─ no dependencies — bottom of the stack
```

---

## Build order

Because of the dependency map, you build bottom-up:

```
Week 1:  Module 1 (Allocators)  →  Module 2 (Queues)
           No dependencies. Fully testable in isolation.
           Deliverable: allocator benchmarks, queue throughput numbers.

Week 2:  Module 3 (Protocol)    →  Module 4 (Transport)
           Protocol has no deps. Transport needs Pool from Module 1.
           Deliverable: echo server — send a frame, get it back.

Week 3:  Module 5 (Broker)      →  Module 6 (Client)
           Broker needs all of 1-4. Client needs 3+4.
           Deliverable: end-to-end pub/sub working.

Week 4:  Module 7 (Bench)
           Exercises all modules. Produces the README numbers.
           Deliverable: latency CDF charts, throughput table.
```

---

## What each module contributes to the recruiter story

| Module | What it proves |
|--------|---------------|
| 1 — Allocators | You understand memory layout, TLB, lifetime management |
| 2 — Queues | You can reason about memory ordering and cache behavior |
| 3 — Protocol | You can design a wire format, not just use someone else's |
| 4 — Transport | You understand Linux async I/O primitives |
| 5 — Broker | You can compose the above into a working system |
| 6 — Client | You can build a clean API over low-level infra |
| 7 — Bench | You measure things — you don't guess |

The benchmark numbers are what you lead with in conversation.
Everything else is what you explain when they ask "how does it work."

# Module 3 — Wire Protocol

## Learning Resources

| Resource | What to read/watch | Why |
|---|---|---|
| [NASDAQ TotalView-ITCH 5.0 Spec (PDF)](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf) | Message Types section (pp. 5–20) | The real protocol your design is modeled after. Look at how Add Order, Trade, Cancel messages are laid out as packed structs. |
| [Jane Street — Safe at Any Speed](https://www.janestreet.com/tech-talks/safe-at-any-speed/) | Full talk | The zero-alloc parsing philosophy: flat IO buffers, pointer arithmetic, no object construction. This is the "why" behind your codec design. |
| [itchcpp — C++20 ITCH Parser (GitHub)](https://github.com/bbalouki/itchcpp) | Source: `include/itch/` | A real zero-copy ITCH parser. Read how it casts raw bytes to packed structs. Your codec should look similar. |
| [Parsing ITCH Messages in C++](https://kevingivens.github.io/parsing-itch-messages-in-c/) | Full post | Short worked example of parsing ITCH with packed structs in C++. Good warm-up before writing your codec. |

**Firm provenance:** NASDAQ ITCH 5.0 (binary fixed-width framing), Jane Street "Safe at Any Speed" (zero-copy parsing, flat buffers)
**Location:** `pulse/protocol/`

---

## Design Principles

- No JSON, no protobuf, no varint — fixed-width fields only
- Zero-copy decode: cast raw buffer pointer directly to packed struct
- Header fits in one cache line (16 bytes)
- Topic names mirror ITCH instrument identifiers in spirit: short, fixed-purpose strings
- **ITCH angle:** NASDAQ ITCH uses fixed-width binary fields with no delimiters. Every message type is a packed struct. Parsing is a pointer cast. Same philosophy here.

---

## Frame Format

```
 0       1       2       3       4       5       6       7
 +-------+-------+-------+-------+-------+-------+-------+-------+
 | magic (4B)            | ver   | opcode| flags (2B)            |
 +-------+-------+-------+-------+-------+-------+-------+-------+
 | payload_len (4B)      | topic_len (4B)                        |
 +-------+-------+-------+-------+-------+-------+-------+-------+
 | topic (variable, max 64B)                                     |
 +-------+-------+-------+-------+-------+-------+-------+-------+
 | payload (variable)                                            |
 +-------+-------+-------+-------+-------+-------+-------+-------+
```

- **magic:** `0x50554C53` ("PULS") — fast rejection of garbage data
- **version:** `0x01`
- **opcode:** see table below
- **flags:** bitmask, currently `FLAG_COMPRESSED = 0x01`, `FLAG_LAST_IN_BURST = 0x02`
- **payload_len / topic_len:** uint32_t, big-endian (network byte order)
- **topic:** UTF-8, no null terminator, max 64 bytes. Examples: `SPY.bid`, `factor.momentum`, `sim.pnl`
- **payload:** raw bytes. Caller interprets — `double[]`, tick struct, anything.

---

## Opcodes

```cpp
enum class Opcode : uint8_t {
    PUBLISH     = 0x01,  // client → broker: push message to topic
    SUBSCRIBE   = 0x02,  // client → broker: register interest in topic
    UNSUBSCRIBE = 0x03,  // client → broker: deregister from topic
    DELIVER     = 0x04,  // broker → subscriber: message delivery
    ACK         = 0x05,  // broker → publisher: confirm receipt
    HEARTBEAT   = 0x06,  // bidirectional: keepalive
    STATS       = 0x07,  // broker → client: drop counters, queue depths
    ERROR       = 0xFF,  // broker → client: malformed frame, unknown topic, etc.
};
```

---

## Packed Structs (`frame.hpp`)

```cpp
#pragma pack(push, 1)

struct FrameHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  opcode;
    uint16_t flags;
    uint32_t payload_len;
    uint32_t topic_len;
    // topic bytes follow immediately
    // payload bytes follow topic
};

static_assert(sizeof(FrameHeader) == 16);

#pragma pack(pop)
```

**Zero-copy decode:** `const FrameHeader* hdr = reinterpret_cast<const FrameHeader*>(buf);`
Topic pointer: `const char* topic = reinterpret_cast<const char*>(buf + sizeof(FrameHeader));`
Payload pointer: `const std::byte* payload = buf + sizeof(FrameHeader) + hdr->topic_len;`

No allocation. No copy. Pointer arithmetic into the pool buffer.

---

## Codec (`codec.hpp`)

```cpp
// Encode into a pre-acquired pool buffer. Returns total bytes written.
std::size_t encode(
    Opcode             opcode,
    std::string_view   topic,
    std::span<const std::byte> payload,
    std::byte*         dest,
    std::size_t        dest_capacity
);

// Decode view into an existing buffer. Zero allocation.
struct FrameView {
    const FrameHeader*        header;
    std::string_view          topic;
    std::span<const std::byte> payload;
    bool valid;
};

FrameView decode(const std::byte* buf, std::size_t len);
```

**Jane Street angle:** their Protogen tool generates parsers that do exactly this — pointer arithmetic over flat IO buffers, no object construction, no heap allocation. Manual version of the same idea.

---

## Topic Naming Convention

Topics follow `{instrument}.{field}` or `{namespace}.{signal}`:

```
SPY.bid          — best bid for SPY
SPY.ask          — best ask for SPY
AAPL.trade       — last trade for AAPL
factor.momentum  — momentum factor signal vector (payload = double[])
factor.value     — value factor signal vector
sim.pnl          — simulation PnL stream (payload = double)
book.SPY         — full order book snapshot (payload = custom struct)
```

This makes the demo quant-meaningful without requiring a live feed.

---

## Tests (`protocol_test.cpp`)

- Encode then decode round-trip: verify all fields survive
- Max topic length (64B): encode succeeds, decode returns correct topic
- Oversized topic (65B): encode returns error
- Corrupt magic: decode returns `valid = false`
- Zero-length payload: valid frame
- Fuzz: libfuzzer target over `decode()` — must never crash or UB

## Benchmark

- `encode` ns/op for 64B payload
- `decode` ns/op — should be near-zero (pointer casts only)
- Compare vs `protobuf::ParseFromArray` on equivalent message

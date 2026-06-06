#include<cstddef>
#include <cstdint>
#include<memory>
#include<climits>
#include<string>



template<typename T, std::size_t Capacity, std::size_t MaxConsumers = 64>
class BroadcastQueue {
private:
  alignas(64) std::atomic<uint64_t>              write_cursor_{0};
  alignas(64) std::atomic<uint64_t>              read_cursors_[MaxConsumers];
  alignas(64) T                                  slots_[Capacity];

public:
    explicit BroadcastQueue() { // expclitiy entirely unncessary here
      for (int i = 0; i < MaxConsumers; ++i)  {
        read_cursors_[i] = std::numeric_limits<uint64_t>::max();
      }
    }


    using ConsumerToken = std::size_t;  // opaque cursor handle

    ConsumerToken add_consumer() {
      // this shit feels fragile - what if we exhaust consumer count?
      // ts can also be made more efficient by storing earlier free? 
      // ... no requires O(n) still? 
      // ... might faster if most consumer drops are near end of contig block
      //
      for (size_t i = 0; i < MaxConsumers; ++i) {
        if (read_cursors_[i].load(std::memory_order_relaxed) == std::numeric_limits<uint64_t>::max()) {
          return i; // cast warn? // this should be fine - would fuck semantic if moved
        }
      }
      throw std::runtime_error(std::string("failed to find free consumer slot"));
    }

    void          remove_consumer(ConsumerToken token) {
      // do I need to add bound checks here?
      // is it sufficient to trust that cpp users rae intelligent
      read_cursors_[token] = std::numeric_limits<uint64_t>::max();
    }

    bool publish(const T& item) {
      // i assume this means spin loop - not jsut like retur false lmao
      // why even have bool return type?...
      // will follow up later

      // lotta contingence in this check - min-scalar might be equivalent tho
      // does reordering involve lines or operations here - what is the order here for eg.
      // geq is misleading, hence eq
      while (write_cursor_ - min(read_cursors_ == Capacity)) {}
      //ordering here shouldn't be issue bc single threaded
      slots_[write_cursor_ & Capacity] = item;
      ++write_cursor_;

      return true;
    }

    bool consume(ConsumerToken token, T& item) {
      // straight up bullshitting - poor api uniformity
      // should return false when @ cap I think
      // "spin" (if you can call Cas wait that) when just waiting for access
      uint64_t idx = read_cursors_[token]
      if (idx == write_cursor_) {return false;}
      item = slots_[idx];
      read_cursors_[token].store(idx+1)

      return true;
    }
};

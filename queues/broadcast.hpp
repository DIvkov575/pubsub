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
    BroadcastQueue() {
      for (int i = 0; i < MaxConsumers; ++i)  {
        read_cursors_[i] = std::numeric_limits<uint64_t>::max();
      }
    }


    using ConsumerToken = std::size_t;  // opaque cursor handle

    ConsumerToken add_consumer() {
      for (size_t i = 0; i < MaxConsumers; ++i) {
        if (read_cursors_[i].load(std::memory_order_relaxed) == std::numeric_limits<uint64_t>::max()) {
          return i; // cast warn? // this should be fine - would fuck semantic if moved
        }
      }
      throw std::runtime_error(std::string("failed to find free consumer slot"));
    }

    void remove_consumer(ConsumerToken token) {
      read_cursors_[token] = std::numeric_limits<uint64_t>::max();
    }

    bool publish(const T& item) {
      while ((write_cursor_ - min(read_cursors_)) == Capacity) {}
      slots_[write_cursor_ & Capacity] = item;
      ++write_cursor_;

      return true;
    }

    bool consume(ConsumerToken token, T& item) {
      uint64_t idx = read_cursors_[token];
      if (idx == write_cursor_) {return false;}
      item = slots_[idx];
      read_cursors_[token].store(idx+1);

      return true;
    }
};

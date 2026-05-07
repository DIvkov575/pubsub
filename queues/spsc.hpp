#include<cstddef>
#include<cstdint>
#include<atomic>
#include<bit>

template<typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");


private:
    alignas(64) std::atomic<uint64_t> head_{0};  // written by producer
    alignas(64) std::atomic<uint64_t> tail_{0};  // written by consumer
    alignas(64) T slots_[Capacity];
public:
  bool try_push(const T& item) {
      uint64_t tail = tail_.load(std::memory_order_acquire);
      uint64_t head = head_.load(std::memory_order_relaxed);

      if (head - tail == Capacity)
          return false;

      slots_[head & (Capacity - 1)] = item;

      head_.store(head + 1, std::memory_order_release);

      return true;
  }

  bool try_pop(T& item) {
      uint64_t head = head_.load(std::memory_order_acquire);
      uint64_t tail = tail_.load(std::memory_order_relaxed);

      if (head == tail)
          return false;

      item = slots_[tail & (Capacity - 1)];

      tail_.store(tail + 1, std::memory_order_release);

      return true;
  }

  size_t size_approx() const {
      uint64_t head = head_.load(std::memory_order_relaxed);
      uint64_t tail = tail_.load(std::memory_order_relaxed);

      return head - tail;
  }

  bool empty() const {
      uint64_t head = head_.load(std::memory_order_acquire);
      uint64_t tail = tail_.load(std::memory_order_relaxed);

      return head == tail;
  }    
};

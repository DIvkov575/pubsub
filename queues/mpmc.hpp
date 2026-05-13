template<typename T, std::size_t Capacity>
class MPMCQueue {
    static_assert(std::has_single_bit(Capacity));
private:
  struct alignas(64) Slot {
      std::atomic<uint64_t> sequence;
      T                     data;
  };

  alignas(64) Slot                  slots_[Capacity];
  alignas(64) std::atomic<uint64_t> enqueue_pos_{0};
  alignas(64) std::atomic<uint64_t> dequeue_pos_{0};
public:

    bool try_push(const T& item) {
      uint64_t idx = enqueue_pos_.fetch_add(1, std::memory_order_acquire);
      uint64_t pos = idx & (Capacity-1);

      if (pos > slots_[pos].sequence) return false;

      slots_[pos].data = item;
      slots_[pos].sequence.store(idx+1, std::memory_order_release); 

      return true;
    }

    bool try_pop(T& item);
};

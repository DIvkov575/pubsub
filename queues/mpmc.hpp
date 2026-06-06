#include <cstddef>
#include <memory>
#include <atomic>
#include <bit>
#include <cstdint>

template<typename T, std::size_t Capacity>
class MPMCQueue {

    static_assert(std::has_single_bit(Capacity));

private:

    struct alignas(64) Slot {
        std::atomic<uint64_t> sequence;
        T data;
    };

    alignas(64) Slot slots_[Capacity];

    alignas(64) std::atomic<uint64_t> enqueue_pos_{0};
    alignas(64) std::atomic<uint64_t> dequeue_pos_{0};

public:

    bool try_push(const T& item) {

        uint64_t didx, idx, pos;

        while (true) {

            idx = enqueue_pos_.load();
            didx = dequeue_pos_.load();

            if ((idx - didx) % (Capacity - 1) == 0 && (idx > didx)) 
              return false; // capcity

            pos = idx & (Capacity - 1);

            // if (idx != slots_[pos].sequence.load(std::memory_order_acquire))
            if (idx != slots_[pos].sequence.load())
                continue;

            if (enqueue_pos_.compare_exchange_weak(idx, idx + 1)) {
                break;
            }
        }

        slots_[pos].data = item;
        slots_[pos].sequence.store(idx + 1, std::memory_order_release);

        return true;
    }

    bool try_pop(T& item) {

        uint64_t eidx, didx, pos;

        while (true) {

            didx = dequeue_pos_.load();
            eidx = enqueue_pos_.load();
            pos = didx & (Capacity - 1);

            if (didx == eidx) 
              return false;

            if (slots_[pos].sequence.load() != didx + 1)
                continue;

            if (dequeue_pos_.compare_exchange_weak(didx, didx+1)) {
                break;
            }
        }

        item = slots_[pos].data;
        slots_[pos].sequence.store(didx + Capacity);

        return true;
    }
};

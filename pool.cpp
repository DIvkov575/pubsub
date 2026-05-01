#include<memory>
#include<cstddef>
#include<cstdlib>
#include<iostream>


constexpr std::size_t align_up(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

template<
  std::size_t SlotSize_,
  std::size_t SlotCount,
  std::size_t Alignment = alignof(std::max_align_t)
>
class Pool {
  static_assert(Alignment >= sizeof(void*));

  static constexpr std::size_t SlotSize = align_up(SlotSize_, Alignment);
  static constexpr std::size_t Stride = SlotSize;
  alignas(Alignment) std::byte storage_[SlotSize * SlotCount];

  class alignas(Alignment) FreeNode {
    public:
      void* next;
  };

  FreeNode* head;
  FreeNode* tail;
  std::size_t free_count;

    std::byte* slot(std::size_t i) noexcept {
        return storage_ + i * SlotSize;
    }

public:
    Pool() {
      #ifdef DEBUG
        std::memset(storage_, 0, sizeof(storage_));
        head = nullptr;
        tail = nullptr;
      #endif


      tail = ::new (slot(0)) FreeNode{nullptr};
      head = tail;

      for (int i = 1; i < SlotCount; ++i) {
        head = ::new (slot(i)) FreeNode{head};
      }

      free_count = SlotCount;

    }

    void* acquire() {
      if (!head) { return nullptr; }

      --free_count;
      void* ret = head;
      head = head->next;
      return ret;
    }
    void  release(void* ptr) {
      head = ::new (ptr) FreeNode{head};
      ++free_count;
    }

    std::size_t available() const {
      return free_count;
    }
};


int main() {
  Pool<sizeof(size_t), 5> pool;

  std::cout << 2 << std::endl;
}



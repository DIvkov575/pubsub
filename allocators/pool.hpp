#pragma once
#include <cstddef>
#include <cstring>

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

    alignas(Alignment) std::byte storage_[SlotSize * SlotCount];

    struct alignas(Alignment) FreeNode {
        FreeNode* next;
    };

    FreeNode*   head;
    std::size_t free_count;

    std::byte* slot(std::size_t i) noexcept { return storage_ + i * SlotSize; }

public:
    Pool() {
#ifdef DEBUG
        std::memset(storage_, 0, sizeof(storage_));
        head = nullptr;
#endif
        FreeNode* tail = ::new (slot(0)) FreeNode{nullptr};
        head = tail;

        for (std::size_t i = 1; i < SlotCount; ++i)
            head = ::new (slot(i)) FreeNode{head};

        free_count = SlotCount;
    }

    void* acquire() {
        if (!head) return nullptr;
        void* ret = head;
        head = head->next;
        --free_count;
        return ret;
    }

    void release(void* ptr) {
        head = ::new (ptr) FreeNode{head};
        ++free_count;
    }

    std::size_t available() const { return free_count; }
};

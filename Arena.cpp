#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include<vector>
#include<iostream>
#include<cassert>



class Arena { // non owning...
private:
    std::byte* buf_;
    std::byte* ptr_;
    std::byte* end_;

public:
    Arena(std::byte* buf, std::size_t size)
        : buf_(buf), ptr_(buf), end_(buf + size) {}

    void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr_);
        std::uintptr_t aligned = (addr + align - 1) & ~(align - 1);

        std::byte* next = reinterpret_cast<std::byte*>(aligned + size);

        assert(next <= end_);

        ptr_ = next;
        return reinterpret_cast<void*>(aligned);
    }

    template<typename T>
    T* alloc(std::size_t n = 1) {
        return static_cast<T*>(allocate(sizeof(T) * n, alignof(T)));
    }

    void reset() {
        ptr_ = buf_;
    }

    [[nodiscard]] std::size_t used() const {
        return ptr_ - buf_;
    }

    [[nodiscard]] std::size_t remaining() const {
        return end_ - ptr_;
    }
};


int main() {
  size_t capacity = 1024;
  Arena arena = Arena(reinterpret_cast<std::byte*>(malloc(capacity)), capacity);

  // size_t tsize = sizeof(int);
  // std::vector<size_t> indices;
  // indices.reserve(capacity/tsize);
  // for (int i = 0; i < capacity/tsize; ++i) {
  //   indices.push_back(rand()%capacity);
  // }

  void* ptr1 = arena.allocate(sizeof(size_t));
  arena.allocate(32);
  void* ptr2 = reinterpret_cast<void*>(arena.allocate(sizeof(size_t)));
  arena.reset();
  void* ptr3 = reinterpret_cast<void*>(arena.allocate(sizeof(size_t)));

  std::cout << ptr1 << std::endl;
  std::cout << ptr2 << std::endl;
  std::cout << ptr3 << std::endl;

}

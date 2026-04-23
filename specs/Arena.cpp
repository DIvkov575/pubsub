#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include<vector>
#include<iostream>

// class Arena {
// public:
//     explicit Arena(std::byte* buf, std::size_t capacity);
//
//     void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t));
//     void  reset();  // bulk free — O(1)
//
//     std::size_t used() const;
//     std::size_t remaining() const;
// };
//
//
//
//

**Internals:**
- `ptr_` starts at `buf`, advances on each allocation
- Alignment: round `ptr_` up to next multiple of `align` before returning
- `reset()`: set `ptr_ = buf_`
- No bounds check in release build (`NDEBUG`), assert in debug



class Arena {
private:
    std::byte* buffer;
    std::size_t capacity;
    std::size_t offset = 0;
public:
    explicit Arena(std::byte* buf, std::size_t capacity): buffer(buf), capacity(capacity) {};

    void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
      uintptr_t addr = reinterpret_cast<uintptr_t>(buffer + offset);
      uintptr_t addr_aligned = (addr + align- 1) & ~(align - 1);
      size_t realized_offset = addr_aligned - reinterpret_cast<uintptr_t>(buffer);

      if (realized_offset + size > capacity) { return nullptr; }

      offset = realized_offset + size;

      return reinterpret_cast<void*>(addr_aligned);
    }

    void  reset() {
      offset = 0;
    }

    std::size_t used() const {
      return offset;
    }
    std::size_t remaining() const {
      return capacity - offset;
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

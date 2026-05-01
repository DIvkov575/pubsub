#include <sys/mman.h>
#include<memory>
#include<cstddef>
#include<cstdlib>
#include<iostream>
#include<cassert>

class HugePageBuffer {
public:
    explicit HugePageBuffer(std::size_t size) {
// round up to 2MiB
      std::size_t page_size = 1 << 22;
      size_ = ((size + page_size - 1) / page_size) * page_size;
      std::size_t sizef = (size_ + page_size - 1) & ~(page_size - 1);
      assert(size_ == sizef);

      void* p = mmap(nullptr,
          size_,
          PROT_READ | PROT_WRITE,
          MAP_ANON | MAP_PRIVATE | MAP_HUGETLB,
          -1,
          0
          );

      if (p == MAP_FAILED) {

      }

    }
    ~HugePageBuffer() {
      munmap(ptr_, size_);
    }

    std::byte* data(){
        return reinterpret_cast<std::byte*>(ptr_);
    }
    std::size_t size() const {
      return size_;
    }

private:
    void*       ptr_;
    std::size_t size_;
};


int main() {

} 

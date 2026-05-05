#pragma once
#include <sys/mman.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cassert>

class HugePageBuffer {
public:
    explicit HugePageBuffer(std::size_t size) {
        constexpr std::size_t page_size = 1 << 21; // 2 MiB
        size_ = ((size + page_size - 1) / page_size) * page_size;

        void* p = mmap(nullptr, size_,
                       PROT_READ | PROT_WRITE,
                       MAP_ANON | MAP_PRIVATE | MAP_HUGETLB,
                       -1, 0);

        if (p == MAP_FAILED) {
            p = mmap(nullptr, size_,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);

            if (p == MAP_FAILED) {
                perror("mmap");
                exit(1);
            }

            madvise(p, size_, MADV_HUGEPAGE);
        }

        ptr_ = p;
    }

    ~HugePageBuffer() { munmap(ptr_, size_); }

    std::byte*  data()  { return reinterpret_cast<std::byte*>(ptr_); }
    std::size_t size() const { return size_; }

private:
    void*       ptr_;
    std::size_t size_;
};

#include "../allocators/hugepage.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <string>

int main() {
    constexpr size_t request   = 3 * 1024 * 1024; // 3 MiB — not a multiple of 2MiB
    constexpr size_t page_2mib = 1 << 21;
    constexpr size_t expected  = 2 * page_2mib;   // rounds up to 4 MiB

    HugePageBuffer buf(request);

    // --- size is rounded up to 2MiB boundary ---
    assert(buf.size() == expected);
    assert(buf.size() % page_2mib == 0);

    // --- data() is non-null and page-aligned ---
    assert(buf.data() != nullptr);
    assert(reinterpret_cast<uintptr_t>(buf.data()) % 4096 == 0);

    // --- every byte is writable and readable ---
    std::byte* p = buf.data();
    for (size_t i = 0; i < buf.size(); ++i)
        p[i] = std::byte(i & 0xFF);
    for (size_t i = 0; i < buf.size(); ++i)
        assert(p[i] == std::byte(i & 0xFF));

    // --- report huge page status from smaps ---
    size_t anon_huge_kb = 0;
    std::ifstream smaps("/proc/self/smaps");
    for (std::string line; std::getline(smaps, line);)
        if (line.rfind("AnonHugePages:", 0) == 0)
            anon_huge_kb += std::stoul(line.substr(14));

    std::cout << "AnonHugePages: " << anon_huge_kb << " kB ("
              << (anon_huge_kb > 0 ? "huge pages active" : "fell back to standard pages") << ")\n";

    std::cout << "hugepage: all tests passed\n";
}

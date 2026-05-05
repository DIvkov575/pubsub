#include "../allocators/hugepage.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <string>

int main() {
    constexpr size_t size = 4 * 1024 * 1024; // 4 MiB
    HugePageBuffer buf(size);

    assert(buf.size() >= size);
    assert(buf.data() != nullptr);

    constexpr size_t page_size = 4096;
    std::byte* p = buf.data();
    for (size_t i = 0; i < buf.size(); i += page_size)
        p[i] = std::byte{0xAB};
    for (size_t i = 0; i < buf.size(); i += page_size)
        assert(p[i] == std::byte{0xAB});

    // check /proc/self/smaps for AnonHugePages
    size_t anon_huge_kb = 0;
    std::ifstream smaps("/proc/self/smaps");
    for (std::string line; std::getline(smaps, line);)
        if (line.rfind("AnonHugePages:", 0) == 0)
            anon_huge_kb += std::stoul(line.substr(14));

    std::cout << "AnonHugePages: " << anon_huge_kb << " kB ("
              << (anon_huge_kb > 0 ? "huge pages active" : "fell back to standard pages") << ")\n";

    std::cout << "hugepage: all tests passed\n";
}

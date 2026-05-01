# Spec-1 Allocator Reading List (macOS-focused)

All files in this directory. Open with a browser (HTML), `Preview` (PDF), or `bat`/`less` (source).

## Spec-1 primary sources (from `specs/01_allocators.md`)

| File | Source | What it is |
|---|---|---|
| `hrt_huge_pages_part1.html` | hudsonrivertrading.com | HRT blog — TLB/huge-pages mental model. The "why" behind Module 1c. |
| `hrt_huge_memory_bench.cpp` | github.com/hudson-trading/hrtbeat | The benchmark Module 7 will reproduce. Read this first. |
| `janestreet_allocator_showdown.html` | blog.janestreet.com | RSS vs heap-size methodology — directly informs the bench plan. |
| `cppref_memory_resource.html` | cppreference.com | `std::pmr::memory_resource` API — the interface for Module 1d. |

## macOS / Apple Silicon specifics

| File | Source | Why it matters here |
|---|---|---|
| `apple_mmap_manpage.html` | developer.apple.com | Apple's `mmap(2)` — note: **no `MAP_HUGETLB`, no `MADV_HUGEPAGE`** on macOS. Documents `MAP_ANON`, `MAP_JIT`, `MAP_32BIT`. |
| `xcode_mmap_manpage.html` | keith.github.io/xcode-man-pages | Same manpage, prettier render. |
| `apple_forums_superpages_m1.html` | developer.apple.com/forums | The definitive "can I use superpages on M1?" thread. **Answer: no — superpages are x86_64-only on macOS.** |
| `pankkor_large_pages_gist.html` | gist.github.com | Cross-platform large-page recipe (Linux/Windows/macOS). Confirms macOS path is `VM_FLAGS_SUPERPAGE_SIZE_2MB` via the `mmap` fd argument, x86_64 only. |
| `nodejs_macos_large_pages.patch` | github.com/nodejs/node | Real production code: how Node.js calls `mmap` with `VM_FLAGS_SUPERPAGE_SIZE_2MB` on macOS. Use as a template. |
| `ampere_arm64_page_sizes.html` | amperecomputing.com | Why arm64 supports 4K/16K/64K pages; TLB coverage math. Apple Silicon uses 16K base pages. |

## XNU kernel sources (authoritative)

| File | What's in it |
|---|---|
| `xnu_ubc_h.txt` | Public API of the Unified Buffer Cache: `ubc_getsize`, `ubc_msync`, `ubc_create_upl`, etc. The macOS equivalent of Linux's page cache. |
| `xnu_vm_statistics_h.txt` | Defines `VM_FLAGS_SUPERPAGE_SIZE_2MB`, `VM_FLAGS_SUPERPAGE_SIZE_ANY`, the mask/shift macros, and all `VM_FLAGS_*` you can pass to `mach_vm_map`. |
| `xnu_kern_mman_c.txt` | Kernel-side `mmap` implementation. Shows how user flags map to Mach VM operations. |

## UBC / page cache theory

| File | Source | What it is |
|---|---|---|
| `silvers_ubc_paper.pdf` | Chuck Silvers, USENIX Freenix 2000 | The foundational paper. NetBSD's UBC — XNU's UBC follows the same design (unify VM page cache and file buffer cache; mmap and read/write hit the same pages). |

## Linux comparison reading (for the spec's `MAP_HUGETLB` path)

| File | Source | Why |
|---|---|---|
| `rigtorp_hugepages.html` | rigtorp.se | Practical Linux huge-pages with `MAP_HUGETLB` and `MADV_HUGEPAGE`. The path the spec actually proposes. **You'll need a fallback on macOS.** |
| `easyperf_huge_pages_for_code.html` | easyperf.net | Huge pages for the `.text` section (instruction TLB). Same `mmap` patterns. |

---

## TL;DR for this project on macOS

1. **`MAP_HUGETLB` does not exist on Darwin.** The spec's `mmap(... MAP_HUGETLB ...)` call will not compile. Replace with the macOS path.
2. **`MADV_HUGEPAGE` does not exist on Darwin.** Spec's fallback is also Linux-only.
3. **macOS path on x86_64**: `mmap(NULL, size, PROT_..., MAP_ANON|MAP_PRIVATE, VM_FLAGS_SUPERPAGE_SIZE_2MB, 0)` — note the flag goes in the `fd` slot, not in `flags` (a Darwin oddity). See `nodejs_macos_large_pages.patch` for a working example.
4. **macOS path on Apple Silicon (M1/M2/M3/M4)**: there is no userspace path to large pages. The base page is 16 KiB (vs Linux's 4 KiB), which already gives ~4× the TLB coverage you'd otherwise get for free.
5. **For the spec's Module 1c**: implement the Linux happy-path, plus a macOS branch that on x86_64 uses `VM_FLAGS_SUPERPAGE_SIZE_2MB` and on arm64 falls back to a plain `mmap` with `MAP_ANON|MAP_PRIVATE` and a comment explaining why.
6. **For the page-cache discussion in the spec**: macOS's Unified Buffer Cache (`xnu_ubc_h.txt`, `silvers_ubc_paper.pdf`) is the relevant subsystem — `mmap`'d files share pages with `read()`/`write()` automatically, so there's no double-cache concern.

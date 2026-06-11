#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace process::disasm {
        auto scan_xrefs(uintptr_t address) -> std::vector<uintptr_t>;
        auto find_function_start(uintptr_t address) -> std::optional<uintptr_t>;
        auto resolve_mov_store(uintptr_t address, size_t pre = 0x50, size_t total = 0x200)
            -> std::optional<uintptr_t>;
        auto find_cmp_mem_zero_disp(uintptr_t search_start, size_t search_size, size_t min_disp = 0)
            -> std::optional<size_t>;
        auto find_movzx_mem_disp(uintptr_t search_start, size_t search_size, size_t min_disp = 0)
            -> std::optional<size_t>;
} // namespace process::disasm

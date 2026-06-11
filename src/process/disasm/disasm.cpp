#include "process/disasm/disasm.h"
#include "process/process.h"

namespace process::disasm {

#if defined(_WIN32) || defined(__APPLE__)
namespace x64 {
    auto scan_xrefs(uintptr_t address) -> std::vector<uintptr_t>;
    auto find_function_start(uintptr_t address) -> std::optional<uintptr_t>;
    auto resolve_mov_store(uintptr_t address, size_t pre, size_t total) -> std::optional<uintptr_t>;
    auto find_cmp_mem_zero_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t>;
    auto find_movzx_mem_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t>;
} // namespace x64
#endif

#ifdef __APPLE__
namespace arm64 {
    auto scan_xrefs(uintptr_t address) -> std::vector<uintptr_t>;
    auto find_function_start(uintptr_t address) -> std::optional<uintptr_t>;
    auto resolve_mov_store(uintptr_t address, size_t pre, size_t total) -> std::optional<uintptr_t>;
    auto find_cmp_mem_zero_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t>;
    auto find_movzx_mem_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t>;
} // namespace arm64
#endif

    static auto use_arm64_backend() -> bool {
#ifdef __APPLE__
        return g_process.get_target_arch() == target_arch::arm64;
#else
        return false;
#endif
    }

    auto scan_xrefs(uintptr_t address) -> std::vector<uintptr_t> {
#if defined(_WIN32) || defined(__APPLE__)
        if (use_arm64_backend()) {
#ifdef __APPLE__
            return arm64::scan_xrefs(address);
#endif
        }
        return x64::scan_xrefs(address);
#else
        return {};
#endif
    }

    auto find_function_start(uintptr_t address) -> std::optional<uintptr_t> {
#if defined(_WIN32) || defined(__APPLE__)
        if (use_arm64_backend()) {
#ifdef __APPLE__
            return arm64::find_function_start(address);
#endif
        }
        return x64::find_function_start(address);
#else
        return std::nullopt;
#endif
    }

    auto resolve_mov_store(uintptr_t address, size_t pre, size_t total) -> std::optional<uintptr_t> {
#if defined(_WIN32) || defined(__APPLE__)
        if (use_arm64_backend()) {
#ifdef __APPLE__
            return arm64::resolve_mov_store(address, pre, total);
#endif
        }
        return x64::resolve_mov_store(address, pre, total);
#else
        return std::nullopt;
#endif
    }

    auto find_cmp_mem_zero_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t> {
#if defined(_WIN32) || defined(__APPLE__)
        if (use_arm64_backend()) {
#ifdef __APPLE__
            return arm64::find_cmp_mem_zero_disp(search_start, search_size, min_disp);
#endif
        }
        return x64::find_cmp_mem_zero_disp(search_start, search_size, min_disp);
#else
        return std::nullopt;
#endif
    }

    auto find_movzx_mem_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t> {
#if defined(_WIN32) || defined(__APPLE__)
        if (use_arm64_backend()) {
#ifdef __APPLE__
            return arm64::find_movzx_mem_disp(search_start, search_size, min_disp);
#endif
        }
        return x64::find_movzx_mem_disp(search_start, search_size, min_disp);
#else
        return std::nullopt;
#endif
    }

} // namespace process::disasm

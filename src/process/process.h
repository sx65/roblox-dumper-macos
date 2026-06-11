#pragma once

#include "process/process_types.h"
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winternl.h>
typedef CLIENT_ID* PCLIENT_ID;

namespace process {
    class NtDll {
      public:
        NtDll();

        template <typename T = uintptr_t> auto get_export(const std::string& function_name) -> T {
            if (m_cache.find(function_name) == m_cache.end()) {
                m_cache[function_name] =
                    reinterpret_cast<uintptr_t>(GetProcAddress(m_module, function_name.c_str()));
            }
            return reinterpret_cast<T>(m_cache[function_name]);
        }

      private:
        HMODULE m_module;
        std::unordered_map<std::string, uintptr_t> m_cache;
    };
}
#endif

namespace process {

    class Process {
      public:
        Process() = default;
        ~Process();

        auto attach(std::string_view process_name) -> bool;
        auto get_pid() const -> process_id_t { return m_pid; }
        auto get_handle() const -> process_handle_t { return m_handle; }
        auto get_module_base() const -> uintptr_t { return m_module_base; }
        auto get_section(std::string_view section_name) const
            -> std::optional<std::pair<uintptr_t, size_t>>;
        auto get_window_dimensions() const -> std::optional<glm::vec2>;
        auto get_version() const -> std::optional<std::string>;
        auto get_target_arch() const -> target_arch;
        auto enumerate_memory_regions(uintptr_t start, uintptr_t end) const
            -> std::vector<memory_region_t>;

#ifdef _WIN32
        NtDll m_ntdll;
#endif

      private:
        auto open_process(process_id_t pid) -> process_handle_t;
        auto find_process_by_name(std::string_view process_name) -> std::optional<process_id_t>;
        auto cache_module_info() -> bool;

#ifdef _WIN32
        auto get_window_handle() const -> HWND;
#endif

        process_handle_t m_handle{};
        process_id_t m_pid{};
        uintptr_t m_module_base{};
        bool m_attached{};
    };

    inline Process g_process;
} // namespace process

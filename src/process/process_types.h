#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#else
#include <mach/mach.h>
using process_handle_t = mach_port_t;
using process_id_t = pid_t;
#endif

namespace process {

    enum class target_arch { x86_64, arm64, unknown };

#ifdef _WIN32
    using process_handle_t = HANDLE;
    using process_id_t = DWORD;
#endif

    struct memory_region_t {
        uintptr_t base{};
        size_t size{};
        bool readable{};
        bool writable{};
    };

} // namespace process

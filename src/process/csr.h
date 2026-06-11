#pragma once

#include <cstdint>

namespace process::csr {
    auto debugging_restrictions_active() -> bool;
    auto active_config() -> uint32_t;
} // namespace process::csr

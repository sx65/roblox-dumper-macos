#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace process::macho {

    struct section_alias {
        std::string_view pe_name;
        std::string_view segment;
        std::string_view section;
    };

    auto find_section(uintptr_t module_base, std::string_view pe_section_name)
        -> std::optional<std::pair<uintptr_t, size_t>>;

} // namespace process::macho

#ifdef __APPLE__

#include "process/rtti/rtti.h"
#include "process/memory/memory.h"
#include <cxxabi.h>
#include <cstring>
#include <sstream>
#include <spdlog/spdlog.h>

namespace process {

    static auto parse_msvc_type_name(std::string raw_name) -> std::string {
        if (raw_name.size() > 4 && raw_name.substr(0, 4) == ".?AV") {
            raw_name = raw_name.substr(4);
        }

        const size_t at_pos = raw_name.find("@@");
        if (at_pos != std::string::npos) {
            raw_name = raw_name.substr(0, at_pos);
        }

        return raw_name;
    }

    static auto scan_rtti_msvc(uintptr_t address) -> std::optional<RttiInfo> {
        auto vtable = Memory::read<uintptr_t>(address);
        if (!vtable || *vtable < 0x10000) {
            return std::nullopt;
        }

        auto col_ptr = Memory::read<uintptr_t>(*vtable - 0x8);
        if (!col_ptr || *col_ptr < 0x10000) {
            return std::nullopt;
        }

        auto signature = Memory::read<uint32_t>(*col_ptr);
        if (!signature || *signature > 1) {
            return std::nullopt;
        }

        if (*signature != 1) {
            return std::nullopt;
        }

        auto self_offset = Memory::read<int>(*col_ptr + 0x14);
        if (!self_offset) {
            return std::nullopt;
        }

        const uintptr_t module_base = *col_ptr - *self_offset;

        auto col_bytes = Memory::read_bytes(*col_ptr, sizeof(RttiCompleteObjectLocatorX64));
        if (col_bytes.empty()) {
            return std::nullopt;
        }

        RttiCompleteObjectLocatorX64 col;
        std::memcpy(&col, col_bytes.data(), sizeof(col));

        RttiInfo info{};
        info.type_descriptor = module_base + col.type_descriptor_offset;
        info.class_hierarchy_descriptor = module_base + col.class_descriptor_offset;

        auto td_bytes = Memory::read_bytes(info.type_descriptor, sizeof(TypeDescriptor));
        if (td_bytes.empty()) {
            return std::nullopt;
        }

        TypeDescriptor td;
        std::memcpy(&td, td_bytes.data(), sizeof(td));

        info.name = parse_msvc_type_name(std::string(td.name, strnlen(td.name, 255)));
        if (info.name.empty() || info.name.find('@') == std::string::npos) {
            return std::nullopt;
        }

        return info;
    }

    static auto demangle_name(const std::string& mangled) -> std::string {
        int status = 0;
        char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
        if (status == 0 && demangled) {
            std::string result(demangled);
            free(demangled);
            return result;
        }
        return mangled;
    }

    static auto to_msvc_style_name(const std::string& demangled) -> std::string {
        std::vector<std::string> parts;
        size_t start = 0;

        while (start < demangled.size()) {
            const size_t pos = demangled.find("::", start);
            if (pos == std::string::npos) {
                parts.push_back(demangled.substr(start));
                break;
            }

            parts.push_back(demangled.substr(start, pos - start));
            start = pos + 2;
        }

        if (parts.empty()) {
            return demangled;
        }

        std::string result = parts.back();
        for (int i = static_cast<int>(parts.size()) - 2; i >= 0; i--) {
            result += '@';
            result += parts[static_cast<size_t>(i)];
        }

        return result;
    }

    static auto read_type_name(uintptr_t typeinfo) -> std::optional<std::string> {
        auto name_ptr = Memory::read<uintptr_t>(typeinfo + 8);
        if (!name_ptr) {
            return std::nullopt;
        }

        auto mangled = Memory::read_string(*name_ptr);
        if (!mangled) {
            return std::nullopt;
        }

        return to_msvc_style_name(demangle_name(*mangled));
    }

    static auto scan_rtti_itanium(uintptr_t address) -> std::optional<RttiInfo> {
        auto vtable = Memory::read<uintptr_t>(address);
        if (!vtable || *vtable < 0x10000) {
            return std::nullopt;
        }

        auto typeinfo_ptr = Memory::read<uintptr_t>(*vtable - 8);
        if (!typeinfo_ptr || *typeinfo_ptr < 0x10000) {
            return std::nullopt;
        }

        auto name = read_type_name(*typeinfo_ptr);
        if (!name) {
            return std::nullopt;
        }

        RttiInfo info{};
        info.name = *name;
        info.type_descriptor = *typeinfo_ptr;
        info.class_hierarchy_descriptor = 0;
        return info;
    }

    static auto collect_itanium_base_names(uintptr_t typeinfo, std::vector<std::string>& names,
                                          int depth = 0) -> void {
        if (depth > 8) {
            return;
        }

        auto primary = read_type_name(typeinfo);
        if (primary) {
            names.push_back(*primary);
        }

        auto base_type = Memory::read<uintptr_t>(typeinfo + 16);
        if (base_type && *base_type > 0x10000) {
            collect_itanium_base_names(*base_type, names, depth + 1);
            return;
        }

        auto base_count = Memory::read<uint32_t>(typeinfo + 16);
        if (!base_count || *base_count == 0 || *base_count > 24) {
            return;
        }

        uintptr_t base_table = typeinfo + 24;
        for (uint32_t i = 0; i < *base_count; i++) {
            auto base_info = Memory::read<uintptr_t>(base_table + (i * 16) + 8);
            if (!base_info || *base_info < 0x10000) {
                continue;
            }

            auto base_typeinfo = Memory::read<uintptr_t>(*base_info);
            if (!base_typeinfo || *base_typeinfo < 0x10000) {
                continue;
            }

            collect_itanium_base_names(*base_typeinfo, names, depth + 1);
        }
    }

    static auto get_all_names_msvc(uintptr_t address) -> std::vector<std::string> {
        std::vector<std::string> names;

        auto vtable = Memory::read<uintptr_t>(address);
        if (!vtable) {
            return names;
        }

        auto col_ptr = Memory::read<uintptr_t>(*vtable - 0x8);
        if (!col_ptr) {
            return names;
        }

        auto signature = Memory::read<uint32_t>(*col_ptr);
        if (!signature || *signature != 1) {
            return names;
        }

        auto self_offset = Memory::read<int>(*col_ptr + 0x14);
        if (!self_offset) {
            return names;
        }

        const uintptr_t module_base = *col_ptr - *self_offset;

        auto col_bytes = Memory::read_bytes(*col_ptr, sizeof(RttiCompleteObjectLocatorX64));
        if (col_bytes.empty()) {
            return names;
        }

        RttiCompleteObjectLocatorX64 col;
        std::memcpy(&col, col_bytes.data(), sizeof(col));

        auto hierarchy =
            Memory::read<RttiClassHierarchyDescriptor>(module_base + col.class_descriptor_offset);
        if (!hierarchy || hierarchy->numBaseClasses == 0 || hierarchy->numBaseClasses >= 25) {
            return names;
        }

        uintptr_t base_class_table = module_base + hierarchy->pBaseClassArray;

        for (uint32_t i = 0; i < hierarchy->numBaseClasses; i++) {
            auto base_offset = Memory::read<uint32_t>(base_class_table + (4 * i));
            if (!base_offset) {
                break;
            }

            auto base_class = Memory::read<RttiBaseClassDescriptor>(module_base + *base_offset);
            if (!base_class) {
                continue;
            }

            auto td_bytes = Memory::read_bytes(module_base + base_class->pTypeDescriptor,
                                               sizeof(TypeDescriptor));
            if (td_bytes.empty()) {
                continue;
            }

            TypeDescriptor td;
            std::memcpy(&td, td_bytes.data(), sizeof(td));

            names.push_back(parse_msvc_type_name(std::string(td.name, strnlen(td.name, 255))));
        }

        return names;
    }

    auto Rtti::scan_rtti(uintptr_t address) -> std::optional<RttiInfo> {
        if (auto msvc = scan_rtti_msvc(address)) {
            return msvc;
        }

        return scan_rtti_itanium(address);
    }

    auto Rtti::find(uintptr_t base_address, const std::string& target_class, size_t max_offset,
                    size_t alignment) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            uintptr_t current_address = base_address + offset;
            auto pointer_value = Memory::read<uintptr_t>(current_address);

            if (!pointer_value || *pointer_value < 0x10000) {
                continue;
            }

            auto rtti = scan_rtti(*pointer_value);
            if (rtti && rtti->name == target_class) {
                return offset;
            }
        }

        return std::nullopt;
    }

    auto Rtti::find_all(uintptr_t base_address, const std::string& target_class, size_t max_offset,
                        size_t alignment) -> std::vector<size_t> {
        std::vector<size_t> matches;

        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            uintptr_t current_address = base_address + offset;
            auto pointer_value = Memory::read<uintptr_t>(current_address);

            if (!pointer_value || *pointer_value < 0x10000) {
                continue;
            }

            auto rtti = scan_rtti(*pointer_value);
            if (rtti && rtti->name == target_class) {
                matches.push_back(offset);
            }
        }

        return matches;
    }

    auto Rtti::find_deref(uintptr_t base_address, const std::string& target_class,
                          size_t max_offset, size_t alignment) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto ptr = Memory::read<uintptr_t>(base_address + offset);
            if (!ptr || *ptr < 0x10000) {
                continue;
            }

            auto ptr2 = Memory::read<uintptr_t>(*ptr);
            if (!ptr2 || *ptr2 < 0x10000) {
                continue;
            }

            auto names = get_all_names(*ptr2);

            for (const auto& name : names) {
                if (name.find(target_class) != std::string::npos) {
                    return offset;
                }
            }
        }
        return std::nullopt;
    }

    auto Rtti::get_all_names(uintptr_t address) -> std::vector<std::string> {
        auto msvc_names = get_all_names_msvc(address);
        if (!msvc_names.empty()) {
            return msvc_names;
        }

        std::vector<std::string> names;
        auto vtable = Memory::read<uintptr_t>(address);
        if (!vtable || *vtable < 0x10000) {
            return names;
        }

        auto typeinfo_ptr = Memory::read<uintptr_t>(*vtable - 8);
        if (!typeinfo_ptr || *typeinfo_ptr < 0x10000) {
            return names;
        }

        collect_itanium_base_names(*typeinfo_ptr, names);
        return names;
    }

} // namespace process

#endif

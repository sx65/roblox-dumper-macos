#include "process/memory/memory.h"
#include <algorithm>
#include <cstring>

#ifdef __APPLE__
#include <mach/mach_vm.h>
#endif

namespace process {

#ifdef _WIN32
    auto Memory::read_raw(uintptr_t address, void* buffer, size_t size) -> bool {
        using tNtReadVirtualMemory = NTSTATUS(NTAPI*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
        static auto fn =
            g_process.m_ntdll.get_export<tNtReadVirtualMemory>("NtReadVirtualMemory");

        SIZE_T bytes_read = 0;
        NTSTATUS status = fn(g_process.get_handle(), reinterpret_cast<PVOID>(address), buffer, size,
                             &bytes_read);

        return NT_SUCCESS(status) && bytes_read == size;
    }

    auto Memory::write_raw(uintptr_t address, const void* buffer, size_t size) -> bool {
        using tNtWriteVirtualMemory = NTSTATUS(NTAPI*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
        static auto fn =
            g_process.m_ntdll.get_export<tNtWriteVirtualMemory>("NtWriteVirtualMemory");

        SIZE_T bytes_written = 0;
        NTSTATUS status = fn(g_process.get_handle(), reinterpret_cast<PVOID>(address),
                             const_cast<void*>(buffer), size, &bytes_written);

        return NT_SUCCESS(status) && bytes_written == size;
    }
#else
    auto Memory::read_raw(uintptr_t address, void* buffer, size_t size) -> bool {
        mach_vm_size_t bytes_read = 0;
        kern_return_t kr = mach_vm_read_overwrite(g_process.get_handle(), address, size,
                                                  reinterpret_cast<mach_vm_address_t>(buffer),
                                                  &bytes_read);
        return kr == KERN_SUCCESS && bytes_read == size;
    }

    auto Memory::write_raw(uintptr_t address, const void* buffer, size_t size) -> bool {
        kern_return_t kr =
            mach_vm_write(g_process.get_handle(), address, reinterpret_cast<vm_offset_t>(buffer),
                          static_cast<mach_msg_type_number_t>(size));
        return kr == KERN_SUCCESS;
    }
#endif

    auto Memory::read_bytes(uintptr_t address, size_t size) -> std::vector<uint8_t> {
        std::vector<uint8_t> buffer(size);
        if (!read_raw(address, buffer.data(), size)) {
            return {};
        }
        return buffer;
    }

    auto Memory::write_bytes(uintptr_t address, const std::vector<uint8_t>& data) -> bool {
        return write_raw(address, data.data(), data.size());
    }

    auto Memory::read_string(uintptr_t address, size_t max_length) -> std::optional<std::string> {
        auto bytes = read_bytes(address, max_length);
        if (bytes.empty()) {
            return std::nullopt;
        }

        auto null_pos = std::find(bytes.begin(), bytes.end(), '\0');
        auto str = std::string(bytes.begin(), null_pos);

        if (str.empty()) {
            return std::nullopt;
        }

        return str;
    }

    auto Memory::read_sso_string(uintptr_t address) -> std::optional<std::string> {
        if (!address) {
            return std::nullopt;
        }

        auto length = read<int32_t>(address + 0x10);
        if (!length || *length <= 0 || *length > 1024) {
            return std::nullopt;
        }

        uintptr_t data_ptr = (*length >= 16) ? *read<uintptr_t>(address) : address;

        return read_string(data_ptr, *length);
    }

    auto Memory::scan_string(const std::string& target, std::string_view section)
        -> std::vector<uintptr_t> {
        std::vector<uintptr_t> matches;

        if (target.empty()) {
            return matches;
        }

        if (!section.empty()) {
            auto sec = g_process.get_section(section);
            if (!sec) {
                return matches;
            }

            auto buffer = read_bytes(sec->first, sec->second);
            if (buffer.size() < target.size()) {
                return matches;
            }

            for (size_t offset = 0; offset <= buffer.size() - target.size(); offset++) {
                if (std::memcmp(buffer.data() + offset, target.data(), target.size()) == 0) {
                    matches.push_back(sec->first + offset);
                }
            }

            return matches;
        }

        const uintptr_t module_base = g_process.get_module_base();
        const uintptr_t scan_end = module_base + 0x10000000;
        const auto regions = g_process.enumerate_memory_regions(module_base, scan_end);

        for (const auto& region : regions) {
            if (!region.readable || region.size < target.size()) {
                continue;
            }

            auto buffer = read_bytes(region.base, region.size);
            if (buffer.size() < target.size()) {
                continue;
            }

            for (size_t offset = 0; offset <= buffer.size() - target.size(); offset++) {
                if (std::memcmp(buffer.data() + offset, target.data(), target.size()) == 0) {
                    matches.push_back(region.base + offset);
                }
            }
        }

        return matches;
    }

} // namespace process

#pragma once
#include "process/process.h"
#include <optional>
#include <string>
#include <vector>

namespace process {
    class Memory {
      public:
        template <typename T> static auto read(uintptr_t address) -> std::optional<T> {
            T buffer{};
            if (!read_raw(address, &buffer, sizeof(T))) {
                return std::nullopt;
            }
            return buffer;
        }

        template <typename T> static auto write(uintptr_t address, const T& value) -> bool {
            return write_raw(address, &value, sizeof(T));
        }

        static auto read_bytes(uintptr_t address, size_t size) -> std::vector<uint8_t>;
        static auto write_bytes(uintptr_t address, const std::vector<uint8_t>& data) -> bool;
        static auto read_string(uintptr_t address, size_t max_length = 256)
            -> std::optional<std::string>;
        static auto read_sso_string(uintptr_t address) -> std::optional<std::string>;
        static auto scan_string(const std::string& target, std::string_view section = "")
            -> std::vector<uintptr_t>;

      private:
        static auto read_raw(uintptr_t address, void* buffer, size_t size) -> bool;
        static auto write_raw(uintptr_t address, const void* buffer, size_t size) -> bool;
    };
} // namespace process

#include "helpers.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include "process/rtti/rtti.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace process::helpers {
    auto find_pointer_by_rtti(std::string_view section_name,
                              const std::vector<std::string>& class_names, size_t alignment)
        -> std::unordered_map<std::string, std::optional<size_t>> {
        std::unordered_map<std::string, std::optional<size_t>> results;
        std::unordered_map<std::string, std::vector<uintptr_t>> all_matches;

        for (const auto& class_name : class_names) {
            results[class_name] = std::nullopt;
            all_matches[class_name] = {};
        }

        auto section = process::g_process.get_section(section_name);
        if (!section) {
            spdlog::error("Failed to find section: {}", section_name);
            return results;
        }

        auto [section_start, section_size] = *section;
        auto module_base = process::g_process.get_module_base();

        for (size_t offset = 0; offset < section_size; offset += alignment) {
            auto potential_ptr = process::Memory::read<uintptr_t>(section_start + offset);
            if (!potential_ptr || *potential_ptr < 0x10000) {
                continue;
            }

            auto rtti = process::Rtti::scan_rtti(*potential_ptr);
            if (!rtti) {
                continue;
            }

            for (const auto& class_name : class_names) {
                if (rtti->name == class_name) {
                    size_t final_offset = (section_start + offset) - module_base;
                    all_matches[class_name].push_back(final_offset);
                }
            }
        }

        for (const auto& class_name : class_names) {
            auto& matches = all_matches[class_name];

            if (matches.empty()) {
                spdlog::warn("Failed to find class: {}", class_name);
                continue;
            }

            if (class_name == "DataModel@RBX") {
                std::sort(matches.begin(), matches.end(),
                          [](uintptr_t a, uintptr_t b) { return a > b; });

                if (matches.size() >= 2) {
                    results[class_name] = matches[1];
                } else {
                    spdlog::warn("Found DataModel but not enough instances");
                }
            } else {
                results[class_name] = matches[0];
            }
        }

        return results;
    }

    auto find_sso_string_offset(uintptr_t base_address, const std::string& target_string,
                                size_t max_offset, size_t alignment, bool direct)
        -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            if (direct) {
                auto str = Memory::read_sso_string(base_address + offset);
                if (str && *str == target_string) {
                    return offset;
                }
            } else {
                auto string_ptr = Memory::read<uintptr_t>(base_address + offset);
                if (!string_ptr || *string_ptr < 0x10000) {
                    continue;
                }

                auto str = Memory::read_sso_string(*string_ptr);
                if (str && *str == target_string) {
                    return offset;
                }
            }
        }

        return std::nullopt;
    }

    auto find_string_offset(uintptr_t base_address, const std::string& target_string,
                            size_t max_offset, size_t alignment, size_t max_string_length,
                            bool direct) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            if (direct) {
                auto str = Memory::read_string(base_address + offset, max_string_length);
                if (str && *str == target_string) {
                    return offset;
                }
            } else {
                auto string_ptr = Memory::read<uintptr_t>(base_address + offset);
                if (!string_ptr || *string_ptr < 0x10000) {
                    continue;
                }

                auto str = Memory::read_string(*string_ptr, max_string_length);
                if (str && *str == target_string) {
                    return offset;
                }
            }
        }

        return std::nullopt;
    }

    auto find_string_by_regex(uintptr_t base_address, const std::string& regex_pattern,
                              size_t max_offset, size_t alignment, size_t max_string_length,
                              bool direct) -> std::optional<size_t> {
        std::regex pattern(regex_pattern);

        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            if (direct) {
                auto str = Memory::read_string(base_address + offset, max_string_length);
                if (str && std::regex_match(*str, pattern)) {
                    return offset;
                }
            } else {
                auto string_ptr = Memory::read<uintptr_t>(base_address + offset);
                if (!string_ptr || *string_ptr < 0x10000) {
                    continue;
                }

                auto str = Memory::read_string(*string_ptr, max_string_length);
                if (str && std::regex_match(*str, pattern)) {
                    return offset;
                }
            }
        }

        return std::nullopt;
    }

    auto find_pointer_offset(uintptr_t base_address, uintptr_t target_pointer, size_t max_offset,
                             size_t alignment) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += alignment) {
            auto ptr = Memory::read<uintptr_t>(base_address + offset);
            if (ptr && *ptr == target_pointer) {
                return offset;
            }
        }
        return std::nullopt;
    }

    static auto is_plausible_screen_dimension(float value) -> bool {
        return value >= 100.0f && value <= 8192.0f && !std::isnan(value) && !std::isinf(value);
    }

    static auto dimensions_pair_matches(float width, float height, const glm::vec2& expected,
                                        float tolerance) -> bool {
        return std::abs(width - expected.x) < tolerance &&
               std::abs(height - expected.y) < tolerance;
    }

    auto find_screen_dimensions_offset(uintptr_t base_address,
                                       const std::vector<glm::vec2>& candidates,
                                       size_t max_offset, float tolerance)
        -> std::optional<size_t> {
        for (const auto& expected : candidates) {
            for (size_t offset = 0; offset < max_offset; offset += 4) {
                const auto width = Memory::read<float>(base_address + offset);
                const auto height = Memory::read<float>(base_address + offset + 4);
                if (!width || !height) {
                    continue;
                }

                if (dimensions_pair_matches(*width, *height, expected, tolerance) ||
                    dimensions_pair_matches(*height, *width, expected, tolerance)) {
                    return offset;
                }

                const auto int_width = Memory::read<int32_t>(base_address + offset);
                const auto int_height = Memory::read<int32_t>(base_address + offset + 4);
                if (!int_width || !int_height) {
                    continue;
                }

                const glm::vec2 int_expected(std::round(expected.x), std::round(expected.y));
                if (dimensions_pair_matches(static_cast<float>(*int_width),
                                            static_cast<float>(*int_height), int_expected,
                                            tolerance) ||
                    dimensions_pair_matches(static_cast<float>(*int_height),
                                            static_cast<float>(*int_width), int_expected,
                                            tolerance)) {
                    return offset;
                }
            }
        }

        for (const auto& expected : candidates) {
            if (expected.x <= 0.0f || expected.y <= 0.0f) {
                continue;
            }

            for (size_t offset = 0; offset < max_offset; offset += 4) {
                const auto width = Memory::read<float>(base_address + offset);
                const auto height = Memory::read<float>(base_address + offset + 4);
                if (!width || !height || !is_plausible_screen_dimension(*width) ||
                    !is_plausible_screen_dimension(*height)) {
                    continue;
                }

                if (std::abs(*width - expected.x) < 2.0f &&
                    std::abs(*height - expected.y) < 80.0f) {
                    return offset;
                }
            }
        }

        for (const auto& expected : candidates) {
            if (expected.x <= 0.0f || expected.y <= 0.0f) {
                continue;
            }

            const float expected_aspect = expected.x / expected.y;
            std::vector<std::pair<size_t, float>> aspect_matches;

            for (size_t offset = 0; offset < max_offset; offset += 4) {
                const auto width = Memory::read<float>(base_address + offset);
                const auto height = Memory::read<float>(base_address + offset + 4);
                if (!width || !height || !is_plausible_screen_dimension(*width) ||
                    !is_plausible_screen_dimension(*height)) {
                    continue;
                }

                const float aspect = *width / *height;
                if (std::abs(aspect - expected_aspect) > 0.08f) {
                    continue;
                }

                const float delta =
                    std::abs(*width - expected.x) + std::abs(*height - expected.y);
                aspect_matches.emplace_back(offset, delta);
            }

            if (!aspect_matches.empty()) {
                std::sort(aspect_matches.begin(), aspect_matches.end(),
                          [](const auto& a, const auto& b) { return a.second < b.second; });
                return aspect_matches.front().first;
            }
        }

        return std::nullopt;
    }

    auto find_color3_offset(const std::vector<uintptr_t>& addresses,
                            std::function<std::tuple<uint8_t, uint8_t, uint8_t>(size_t)> get_rgb,
                            size_t max_offset) -> std::optional<size_t> {
        for (size_t offset = 0; offset < max_offset; offset += 1) {
            bool all_match = true;

            for (size_t i = 0; i < addresses.size(); i++) {
                auto [exp_r, exp_g, exp_b] = get_rgb(i);

                auto r = Memory::read<uint8_t>(addresses[i] + offset);
                auto g = Memory::read<uint8_t>(addresses[i] + offset + 1);
                auto b = Memory::read<uint8_t>(addresses[i] + offset + 2);

                if (!r || !g || !b || *r != exp_r || *g != exp_g || *b != exp_b) {
                    all_match = false;
                    break;
                }
            }

            if (all_match) {
                return offset;
            }
        }

        return std::nullopt;
    }

} // namespace process::helpers
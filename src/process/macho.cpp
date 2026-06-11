#ifdef __APPLE__

#include "process/macho.h"
#include "process/memory/memory.h"
#include <mach-o/loader.h>
#include <vector>

namespace process::macho {

    static constexpr section_alias k_section_aliases[] = {
        {".text", "__TEXT", "__text"},
        {".data", "__DATA", "__data"},
        {".rdata", "__TEXT", "__cstring"},
        {".rdata", "__DATA", "__const"},
        {".rdata", "__DATA", "__cstring"},
        {".bss", "__DATA", "__bss"},
    };

    static auto get_slide(uintptr_t module_base) -> std::optional<uintptr_t> {
        auto header = Memory::read<mach_header_64>(module_base);
        if (!header || header->magic != MH_MAGIC_64) {
            return std::nullopt;
        }

        uintptr_t cursor = module_base + sizeof(mach_header_64);
        for (uint32_t i = 0; i < header->ncmds; i++) {
            auto cmd = Memory::read<load_command>(cursor);
            if (!cmd) {
                break;
            }

            if (cmd->cmd == LC_SEGMENT_64) {
                auto segment = Memory::read<segment_command_64>(cursor);
                if (!segment) {
                    cursor += cmd->cmdsize;
                    continue;
                }

                std::string segname(segment->segname,
                                    strnlen(segment->segname, sizeof(segment->segname)));
                if (segname == "__TEXT") {
                    return module_base - static_cast<uintptr_t>(segment->vmaddr);
                }
            }

            cursor += cmd->cmdsize;
        }

        return std::optional<uintptr_t>{0};
    }

    static auto runtime_address(uintptr_t static_address, uintptr_t slide) -> uintptr_t {
        return static_address + slide;
    }

    auto find_section(uintptr_t module_base, std::string_view pe_section_name)
        -> std::optional<std::pair<uintptr_t, size_t>> {
        const auto slide = get_slide(module_base);
        if (!slide.has_value()) {
            return std::nullopt;
        }

        auto header = Memory::read<mach_header_64>(module_base);
        if (!header || header->magic != MH_MAGIC_64) {
            return std::nullopt;
        }

        uintptr_t cursor = module_base + sizeof(mach_header_64);
        std::vector<section_alias> candidates;

        for (const auto& alias : k_section_aliases) {
            if (alias.pe_name == pe_section_name) {
                candidates.push_back(alias);
            }
        }

        if (candidates.empty()) {
            return std::nullopt;
        }

        for (uint32_t i = 0; i < header->ncmds; i++) {
            auto cmd = Memory::read<load_command>(cursor);
            if (!cmd) {
                break;
            }

            if (pe_section_name == ".data" && cmd->cmd == LC_SEGMENT_64) {
                auto segment = Memory::read<segment_command_64>(cursor);
                if (segment) {
                    std::string segname(segment->segname,
                                        strnlen(segment->segname, sizeof(segment->segname)));
                    if (segname == "__DATA" && segment->vmsize > 0) {
                        return std::make_pair(
                            runtime_address(static_cast<uintptr_t>(segment->vmaddr), slide.value()),
                            static_cast<size_t>(segment->vmsize));
                    }
                }
            }

            if (cmd->cmd == LC_SEGMENT_64) {
                auto segment = Memory::read<segment_command_64>(cursor);
                if (!segment) {
                    cursor += cmd->cmdsize;
                    continue;
                }

                std::string segname(segment->segname,
                                    strnlen(segment->segname, sizeof(segment->segname)));

                uintptr_t section_addr = cursor + sizeof(segment_command_64);
                for (uint32_t s = 0; s < segment->nsects; s++) {
                    auto section = Memory::read<section_64>(section_addr);
                    if (!section) {
                        break;
                    }

                    std::string sectname(section->sectname,
                                         strnlen(section->sectname, sizeof(section->sectname)));

                    for (const auto& candidate : candidates) {
                        if (segname == candidate.segment && sectname == candidate.section) {
                            return std::make_pair(
                                runtime_address(static_cast<uintptr_t>(section->addr), slide.value()),
                                static_cast<size_t>(section->size));
                        }
                    }

                    section_addr += sizeof(section_64);
                }
            }

            cursor += cmd->cmdsize;
        }

        return std::nullopt;
    }

} // namespace process::macho

#endif

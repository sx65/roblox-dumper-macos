#if defined(_WIN32) || defined(__APPLE__)

#include "process/disasm/disasm.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include <Zydis/Zydis.h>

namespace process::disasm::x64 {

    static ZydisDecoder g_decoder;
    static auto ensure_decoder() -> void {
        static bool initialized = false;
        if (!initialized) {
            ZydisDecoderInit(&g_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
            initialized = true;
        }
    }

    static auto decode(const uint8_t* buffer, size_t length, ZydisDecodedInstruction& instruction,
                       ZydisDecodedOperand* operands) -> bool {
        ensure_decoder();
        return ZYAN_SUCCESS(
            ZydisDecoderDecodeFull(&g_decoder, buffer, length, &instruction, operands));
    }

    auto scan_xrefs(uintptr_t address) -> std::vector<uintptr_t> {
        std::vector<uintptr_t> xrefs;
        auto section = g_process.get_section(".text");
        if (!section) {
            return xrefs;
        }

        const uintptr_t section_start = section->first;
        const uintptr_t section_end = section_start + section->second;
        const auto regions = g_process.enumerate_memory_regions(section_start, section_end);

        for (const auto& region : regions) {
            if (!region.readable) {
                continue;
            }

            const uintptr_t region_start = region.base;
            const uintptr_t region_end = std::min(region.base + region.size, section_end);
            auto buffer = Memory::read_bytes(region_start, region_end - region_start);
            if (buffer.empty()) {
                continue;
            }

            uintptr_t offset = 0;
            while (offset < buffer.size()) {
                ZydisDecodedInstruction instruction;
                ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

                if (!decode(buffer.data() + offset, buffer.size() - offset, instruction, operands)) {
                    offset++;
                    continue;
                }

                for (int i = 0; i < instruction.operand_count_visible; i++) {
                    const auto& operand = operands[i];

                    if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY &&
                        operand.mem.base == ZYDIS_REGISTER_RIP &&
                        operand.mem.disp.has_displacement) {
                        uintptr_t absolute = (region_start + offset) + instruction.length +
                                             operand.mem.disp.value;
                        if (absolute == address) {
                            xrefs.push_back(region_start + offset);
                        }
                    } else if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
                               operand.imm.is_relative) {
                        ZyanU64 absolute = 0;
                        if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand,
                                                                  region_start + offset,
                                                                  &absolute))) {
                            if (absolute == address) {
                                xrefs.push_back(region_start + offset);
                            }
                        }
                    }
                }

                offset += instruction.length;
            }
        }

        return xrefs;
    }

    auto find_function_start(uintptr_t addr) -> std::optional<uintptr_t> {
        constexpr size_t search_size = 0x1000;
        uintptr_t region_start = addr - search_size;
        auto buffer = Memory::read_bytes(region_start, search_size);
        if (buffer.empty()) {
            return std::nullopt;
        }

        for (size_t i = buffer.size() - 1; i > 0; i--) {
            if (buffer[i] == 0xCC) {
                while (i < buffer.size() && buffer[i] == 0xCC) {
                    i++;
                }
                return region_start + i;
            }
        }

        return std::nullopt;
    }

    auto resolve_mov_store(uintptr_t addr, size_t pre, size_t total) -> std::optional<uintptr_t> {
        auto buffer = Memory::read_bytes(addr - pre, total);
        if (buffer.empty()) {
            return std::nullopt;
        }

        ensure_decoder();

        size_t offset = 0;
        while (offset < buffer.size()) {
            ZydisDecodedInstruction insn;
            ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_decoder, buffer.data() + offset,
                                                     buffer.size() - offset, &insn, ops))) {
                offset++;
                continue;
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                ops[0].mem.base == ZYDIS_REGISTER_RIP &&
                ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER) {
                ZyanU64 absolute = 0;
                if (ZYAN_SUCCESS(
                        ZydisCalcAbsoluteAddress(&insn, &ops[0], addr - pre + offset, &absolute))) {
                    return static_cast<uintptr_t>(absolute);
                }
            }

            offset += insn.length;
        }

        return std::nullopt;
    }

    auto find_cmp_mem_zero_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t> {
        auto buffer = Memory::read_bytes(search_start, search_size);
        if (buffer.empty()) {
            return std::nullopt;
        }

        ensure_decoder();

        size_t offset = 0;
        while (offset < buffer.size()) {
            ZydisDecodedInstruction insn;
            ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

            if (!decode(buffer.data() + offset, buffer.size() - offset, insn, ops)) {
                offset++;
                continue;
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_CMP && ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                ops[0].mem.base == ZYDIS_REGISTER_RDI && ops[0].mem.disp.has_displacement &&
                ops[0].mem.disp.value > static_cast<ZyanI64>(min_disp) &&
                ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[1].imm.value.u == 0) {
                return static_cast<size_t>(ops[0].mem.disp.value);
            }

            offset += insn.length;
        }

        return std::nullopt;
    }

    auto find_movzx_mem_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t> {
        auto buffer = Memory::read_bytes(search_start, search_size);
        if (buffer.empty()) {
            return std::nullopt;
        }

        ensure_decoder();

        size_t offset = 0;
        while (offset < buffer.size()) {
            ZydisDecodedInstruction insn;
            ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

            if (!decode(buffer.data() + offset, buffer.size() - offset, insn, ops)) {
                offset++;
                continue;
            }

            if (insn.mnemonic == ZYDIS_MNEMONIC_MOVZX &&
                ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                ops[1].mem.base == ZYDIS_REGISTER_RAX && ops[1].mem.disp.has_displacement &&
                ops[1].mem.disp.value > static_cast<ZyanI64>(min_disp)) {
                return static_cast<size_t>(ops[1].mem.disp.value);
            }

            offset += insn.length;
        }

        return std::nullopt;
    }

} // namespace process::disasm::x64

#endif

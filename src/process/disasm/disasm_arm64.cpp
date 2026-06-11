#ifdef __APPLE__

#include "process/disasm/disasm.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include <capstone/capstone.h>

namespace process::disasm::arm64 {

    static auto open_handle() -> csh {
        static csh handle = 0;
        static bool initialized = false;
        if (!initialized) {
            cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle);
            cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
            initialized = true;
        }
        return handle;
    }

    static auto disassemble(uintptr_t address, const std::vector<uint8_t>& buffer, cs_insn** out,
                            size_t* out_count) -> void {
        csh handle = open_handle();
        *out_count = cs_disasm(handle, buffer.data(), buffer.size(), address, 0, out);
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

            cs_insn* instructions = nullptr;
            size_t count = 0;
            disassemble(region_start, buffer, &instructions, &count);

            for (size_t i = 0; i < count; i++) {
                const auto* insn = &instructions[i];
                if (insn->detail == nullptr) {
                    continue;
                }

                for (int op_idx = 0; op_idx < insn->detail->arm64.op_count; op_idx++) {
                    const auto& op = insn->detail->arm64.operands[op_idx];
                    if (op.type == ARM64_OP_IMM &&
                        static_cast<uintptr_t>(op.imm) == address) {
                        xrefs.push_back(insn->address);
                    }
                }
            }

            cs_free(instructions, count);
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

        for (size_t i = buffer.size(); i-- > 4;) {
            if (buffer[i - 3] == 0x1F && buffer[i - 2] == 0x20 && buffer[i - 1] == 0x03 &&
                buffer[i] == 0xD5) {
                size_t j = i - 3;
                while (j + 3 < buffer.size() && buffer[j] == 0x1F && buffer[j + 1] == 0x20 &&
                       buffer[j + 2] == 0x03 && buffer[j + 3] == 0xD5) {
                    j += 4;
                }
                return region_start + j;
            }
        }

        return std::nullopt;
    }

    auto resolve_mov_store(uintptr_t addr, size_t pre, size_t total) -> std::optional<uintptr_t> {
        auto buffer = Memory::read_bytes(addr - pre, total);
        if (buffer.empty()) {
            return std::nullopt;
        }

        cs_insn* instructions = nullptr;
        size_t count = 0;
        disassemble(addr - pre, buffer, &instructions, &count);

        for (size_t i = 0; i < count; i++) {
            const auto* insn = &instructions[i];
            if (insn->id == ARM64_INS_STR && insn->detail && insn->detail->arm64.op_count > 1) {
                const auto& mem = insn->detail->arm64.operands[1];
                if (mem.type == ARM64_OP_MEM && mem.mem.disp > 0) {
                    const uintptr_t absolute = static_cast<uintptr_t>(insn->address + mem.mem.disp);
                    cs_free(instructions, count);
                    return absolute;
                }
            }
        }

        cs_free(instructions, count);
        return std::nullopt;
    }

    auto find_cmp_mem_zero_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t> {
        auto buffer = Memory::read_bytes(search_start, search_size);
        if (buffer.empty()) {
            return std::nullopt;
        }

        cs_insn* instructions = nullptr;
        size_t count = 0;
        disassemble(search_start, buffer, &instructions, &count);

        for (size_t i = 0; i < count; i++) {
            const auto* insn = &instructions[i];
            if ((insn->id != ARM64_INS_CMP && insn->id != ARM64_INS_CBNZ) || !insn->detail ||
                insn->detail->arm64.op_count < 2) {
                continue;
            }

            const auto& mem = insn->detail->arm64.operands[0];
            const auto& imm = insn->detail->arm64.operands[1];
            if (mem.type == ARM64_OP_MEM && imm.type == ARM64_OP_IMM && imm.imm == 0 &&
                static_cast<size_t>(mem.mem.disp) > min_disp) {
                const auto result = static_cast<size_t>(mem.mem.disp);
                cs_free(instructions, count);
                return result;
            }
        }

        cs_free(instructions, count);
        return std::nullopt;
    }

    auto find_movzx_mem_disp(uintptr_t search_start, size_t search_size, size_t min_disp)
        -> std::optional<size_t> {
        auto buffer = Memory::read_bytes(search_start, search_size);
        if (buffer.empty()) {
            return std::nullopt;
        }

        cs_insn* instructions = nullptr;
        size_t count = 0;
        disassemble(search_start, buffer, &instructions, &count);

        for (size_t i = 0; i < count; i++) {
            const auto* insn = &instructions[i];
            if (insn->id != ARM64_INS_LDRB || !insn->detail || insn->detail->arm64.op_count < 2) {
                continue;
            }

            const auto& mem = insn->detail->arm64.operands[1];
            if (mem.type == ARM64_OP_MEM && static_cast<size_t>(mem.mem.disp) > min_disp) {
                const auto result = static_cast<size_t>(mem.mem.disp);
                cs_free(instructions, count);
                return result;
            }
        }

        cs_free(instructions, count);
        return std::nullopt;
    }

} // namespace process::disasm::arm64

#endif

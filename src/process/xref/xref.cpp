#include "process/disasm/disasm.h"
#include "process/xref/xref.h"

namespace process {

    Xref::Xref() = default;

    auto Xref::scan(uintptr_t address) const -> std::vector<uintptr_t> {
        return disasm::scan_xrefs(address);
    }

    auto Xref::instruction_scan(
        uintptr_t start, const std::vector<uint8_t>& buffer,
        const std::function<bool(const ZydisDecodedInstruction&, const ZydisDecodedOperand*)>&
            predicate) const -> std::optional<InstructionMatch> {
        (void)start;
        (void)buffer;
        (void)predicate;
        return std::nullopt;
    }

} // namespace process

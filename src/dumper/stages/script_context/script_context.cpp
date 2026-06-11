#include "script_context.h"
#include "dumper/dumper.h"
#include "process/disasm/disasm.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include "process/xref/xref.h"
#include <spdlog/spdlog.h>

namespace dumper::stages::script_context {

    auto dump() -> bool {
        const auto string_results = process::Memory::scan_string(
            "Cannot require a RobloxScript module from a non RobloxScript context", ".rdata");

        if (string_results.empty()) {
            spdlog::warn("script_context: string not found");
            return false;
        }

        const auto xrefs = process::g_xref.scan(string_results.front());
        if (xrefs.empty()) {
            spdlog::warn("script_context: no xrefs found");
            return false;
        }

        const uintptr_t xref = xrefs.front();

        const auto xrefs2 = process::g_xref.scan(xref);
        if (xrefs2.empty()) {
            spdlog::warn("script_context: no second xrefs found");
            return false;
        }

        const uintptr_t xref2 = xrefs2.front();

        constexpr size_t search_back = 0x100;
        const uintptr_t search_start = xref2 - search_back;

        const auto require_bypass =
            process::disasm::find_cmp_mem_zero_disp(search_start, search_back, 0x100);

        if (!require_bypass) {
            spdlog::error("script_context: failed to find RequireBypass");
            return false;
        }

        g_dumper.add_offset("ScriptContext", "RequireBypass", *require_bypass);

        const auto is_roblox_script =
            process::disasm::find_movzx_mem_disp(search_start, search_back, 0x100);

        if (!is_roblox_script) {
            spdlog::error("script_context: failed to find IsRobloxScript");
            return false;
        }

        g_dumper.add_offset("ModuleScript", "IsRobloxScript", *is_roblox_script);

        return true;
    }

} // namespace dumper::stages::script_context

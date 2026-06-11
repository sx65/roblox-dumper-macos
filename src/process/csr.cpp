#ifdef __APPLE__

#include "process/csr.h"

extern "C" int csr_get_active_config(uint32_t* config);

namespace process::csr {

    // CSR_ALLOW_TASK_FOR_PID on modern macOS (csrutil enable --without debug sets this bit).
    constexpr uint32_t k_allow_task_for_pid = 0x00000004;

    auto active_config() -> uint32_t {
        uint32_t config = 0;
        if (csr_get_active_config(&config) != 0) {
            return 0;
        }
        return config;
    }

    auto debugging_restrictions_active() -> bool { return (active_config() & k_allow_task_for_pid) == 0; }

} // namespace process::csr

#endif

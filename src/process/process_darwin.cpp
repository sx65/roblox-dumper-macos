#ifdef __APPLE__

#include "process/csr.h"
#include "process/macho.h"
#include "process/memory/memory.h"
#include "process/process.h"
#include <mach-o/loader.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <libproc.h>
#include <mach/mach_error.h>
#include <mach/mach_vm.h>
#include <cerrno>
#include <regex>
#include <spdlog/spdlog.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <vector>

namespace process {

    Process::~Process() {
        if (m_handle != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), m_handle);
            m_handle = MACH_PORT_NULL;
        }
    }

    auto Process::attach(std::string_view process_name) -> bool {
        if (m_attached && m_handle != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), m_handle);
            m_handle = MACH_PORT_NULL;
            m_attached = false;
            m_module_base = 0;
        }

        const auto pid = find_process_by_name(process_name);
        if (!pid) {
            spdlog::error("Process '{}' not found", process_name);
            return false;
        }

        m_pid = *pid;
        m_handle = open_process(m_pid);

        if (m_handle == MACH_PORT_NULL) {
            char pathbuf[PROC_PIDPATHINFO_MAXSIZE]{};
            proc_pidpath(m_pid, pathbuf, sizeof(pathbuf));

            spdlog::error("Failed to get task port for PID {} ({})", m_pid,
                          pathbuf[0] ? pathbuf : "unknown");

            if (geteuid() == 0) {
                spdlog::error("Do not use sudo. Run as your normal user so debugger entitlements "
                                "and Developer Tools permissions apply.");
            }

            if (csr::debugging_restrictions_active()) {
                spdlog::error(
                    "SIP debugging restrictions are blocking task_for_pid "
                    "(csr_active_config=0x{:x}).",
                    csr::active_config());
                spdlog::error(
                    "Fix: reboot to Recovery Mode -> Terminal -> csrutil enable --without debug "
                    "-> reboot");
            } else {
                spdlog::error(
                    "macOS blocked memory access. Try:\n"
                    "  1. DevToolsSecurity -status should say enabled\n"
                    "  2. System Settings -> Privacy & Security -> Developer Tools -> allow Terminal\n"
                    "  3. Run WITHOUT sudo: ./run_dumper.sh");
            }
            return false;
        }

        if (!cache_module_info()) {
            mach_port_deallocate(mach_task_self(), m_handle);
            m_handle = MACH_PORT_NULL;
            return false;
        }

        m_attached = true;
        return true;
    }

    auto Process::cache_module_info() -> bool {
        mach_vm_address_t address = 0;
        mach_vm_size_t size = 0;
        natural_t depth = 0;
        vm_region_submap_info_64 info{};
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;

        while (true) {
            count = VM_REGION_SUBMAP_INFO_COUNT_64;
            kern_return_t kr = mach_vm_region_recurse(m_handle, &address, &size, &depth,
                                                      reinterpret_cast<vm_region_info_t>(&info),
                                                      &count);
            if (kr != KERN_SUCCESS) {
                break;
            }

            if (info.protection & VM_PROT_READ) {
                auto magic = Memory::read<uint32_t>(static_cast<uintptr_t>(address));
                if (magic && (*magic == MH_MAGIC_64 || *magic == MH_CIGAM_64)) {
                    m_module_base = static_cast<uintptr_t>(address);
                    return true;
                }
            }

            address += size;
        }

        return false;
    }

    auto Process::get_section(std::string_view section_name) const
        -> std::optional<std::pair<uintptr_t, size_t>> {
        if (!m_module_base) {
            return std::nullopt;
        }

        return macho::find_section(m_module_base, section_name);
    }

    static auto display_scale_at_point(CGPoint point) -> float {
        uint32_t display_count = 0;
        if (CGGetActiveDisplayList(0, nullptr, &display_count) != kCGErrorSuccess ||
            display_count == 0) {
            return 1.0f;
        }

        std::vector<CGDirectDisplayID> displays(display_count);
        if (CGGetActiveDisplayList(display_count, displays.data(), &display_count) !=
            kCGErrorSuccess) {
            return 1.0f;
        }

        for (const auto display_id : displays) {
            const CGRect bounds = CGDisplayBounds(display_id);
            if (!CGRectContainsPoint(bounds, point)) {
                continue;
            }

            CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display_id);
            if (!mode) {
                break;
            }

            const size_t pixel_width = CGDisplayModeGetPixelWidth(mode);
            const size_t point_width = CGDisplayModeGetWidth(mode);
            CGDisplayModeRelease(mode);

            if (point_width > 0) {
                return static_cast<float>(pixel_width) / static_cast<float>(point_width);
            }
        }

        return 1.0f;
    }

    auto Process::get_window_dimensions() const -> std::optional<glm::vec2> {
        CFArrayRef window_list =
            CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
        if (!window_list) {
            return std::nullopt;
        }

        std::optional<glm::vec2> result;
        float largest_area = 0.0f;
        const CFIndex count = CFArrayGetCount(window_list);

        for (CFIndex i = 0; i < count; i++) {
            auto window = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(window_list, i));
            auto pid_ref = static_cast<CFNumberRef>(CFDictionaryGetValue(window, kCGWindowOwnerPID));
            if (!pid_ref) {
                continue;
            }

            int window_pid = 0;
            CFNumberGetValue(pid_ref, kCFNumberIntType, &window_pid);
            if (window_pid != static_cast<int>(m_pid)) {
                continue;
            }

            auto bounds_ref =
                static_cast<CFDictionaryRef>(CFDictionaryGetValue(window, kCGWindowBounds));
            if (!bounds_ref) {
                continue;
            }

            CGRect bounds{};
            if (!CGRectMakeWithDictionaryRepresentation(bounds_ref, &bounds)) {
                continue;
            }

            if (bounds.size.width <= 0 || bounds.size.height <= 0) {
                continue;
            }

            const float area = static_cast<float>(bounds.size.width * bounds.size.height);
            if (area <= largest_area) {
                continue;
            }

            const CGPoint center =
                CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
            const float scale = display_scale_at_point(center);

            largest_area = area;
            result = glm::vec2(static_cast<float>(bounds.size.width) * scale,
                               static_cast<float>(bounds.size.height) * scale);
        }

        CFRelease(window_list);
        return result;
    }

    auto Process::get_target_arch() const -> target_arch {
        if (!m_module_base) {
            return target_arch::unknown;
        }

        auto header = Memory::read<mach_header_64>(m_module_base);
        if (!header) {
            return target_arch::unknown;
        }

        if (header->cputype == CPU_TYPE_ARM64) {
            return target_arch::arm64;
        }

        if (header->cputype == CPU_TYPE_X86_64) {
            return target_arch::x86_64;
        }

        return target_arch::unknown;
    }

    auto Process::get_version() const -> std::optional<std::string> {
        char pathbuf[PROC_PIDPATHINFO_MAXSIZE]{};
        if (proc_pidpath(m_pid, pathbuf, sizeof(pathbuf)) <= 0) {
            return std::nullopt;
        }

        std::string path(pathbuf);
        std::regex version_regex(R"(version-[a-f0-9]{16})");
        std::smatch match;

        if (std::regex_search(path, match, version_regex)) {
            return match.str();
        }

        return std::nullopt;
    }

    static auto try_task_for_pid(process_id_t pid, bool use_ptrace) -> process_handle_t {
        if (use_ptrace) {
            if (ptrace(PT_ATTACHEXC, pid, nullptr, 0) != 0) {
                spdlog::warn("ptrace attach failed for PID {} (errno {})", pid, errno);
            }
        }

        mach_port_t task = MACH_PORT_NULL;
        const kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
        if (kr != KERN_SUCCESS) {
            spdlog::error("task_for_pid failed: {} ({})", mach_error_string(kr), kr);
            return MACH_PORT_NULL;
        }

        return task;
    }

    auto Process::open_process(process_id_t pid) -> process_handle_t {
        if (auto task = try_task_for_pid(pid, false); task != MACH_PORT_NULL) {
            return task;
        }

        spdlog::warn("Retrying attach with ptrace...");
        return try_task_for_pid(pid, true);
    }

    auto Process::find_process_by_name(std::string_view process_name) -> std::optional<process_id_t> {
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
        size_t size = 0;

        if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0 || size == 0) {
            return std::nullopt;
        }

        std::vector<kinfo_proc> processes(size / sizeof(kinfo_proc));
        if (sysctl(mib, 4, processes.data(), &size, nullptr, 0) != 0) {
            return std::nullopt;
        }

        const auto count = size / sizeof(kinfo_proc);
        for (size_t i = 0; i < count; i++) {
            if (processes[i].kp_proc.p_stat == SZOMB) {
                continue;
            }

            std::string name(processes[i].kp_proc.p_comm);
            if (name == process_name) {
                return processes[i].kp_proc.p_pid;
            }
        }

        return std::nullopt;
    }

    auto Process::enumerate_memory_regions(uintptr_t start, uintptr_t end) const
        -> std::vector<memory_region_t> {
        std::vector<memory_region_t> regions;
        mach_vm_address_t address = start;
        mach_vm_size_t size = 0;
        natural_t depth = 0;
        vm_region_submap_info_64 info{};
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;

        while (address < end) {
            count = VM_REGION_SUBMAP_INFO_COUNT_64;
            kern_return_t kr = mach_vm_region_recurse(m_handle, &address, &size, &depth,
                                                      reinterpret_cast<vm_region_info_t>(&info),
                                                      &count);
            if (kr != KERN_SUCCESS) {
                break;
            }

            memory_region_t region{};
            region.base = static_cast<uintptr_t>(address);
            region.size = static_cast<size_t>(size);
            region.readable = (info.protection & VM_PROT_READ) != 0;
            region.writable = (info.protection & VM_PROT_WRITE) != 0;

            regions.push_back(region);
            address += size;
        }

        return regions;
    }

} // namespace process

#endif

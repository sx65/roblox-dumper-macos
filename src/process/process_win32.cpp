#ifdef _WIN32

#include "process/process.h"
#include "process/memory/memory.h"
#include <TlHelp32.h>
#include <regex>

namespace process {

    NtDll::NtDll() : m_module(GetModuleHandleA("ntdll.dll")) {}

    Process::~Process() {
        if (m_handle) {
            CloseHandle(m_handle);
            m_handle = nullptr;
        }
    }

    auto Process::attach(std::string_view process_name) -> bool {
        if (m_attached && m_handle) {
            CloseHandle(m_handle);
            m_handle = nullptr;
            m_attached = false;
            m_module_base = 0;
        }

        const auto pid = find_process_by_name(process_name);
        if (!pid) {
            return false;
        }

        m_pid = *pid;
        m_handle = open_process(m_pid);

        if (!m_handle) {
            return false;
        }

        if (!cache_module_info()) {
            CloseHandle(m_handle);
            m_handle = nullptr;
            return false;
        }

        m_attached = true;
        return true;
    }

    auto Process::cache_module_info() -> bool {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

        MODULEENTRY32W me = {sizeof(MODULEENTRY32W)};
        if (Module32FirstW(snapshot, &me)) {
            m_module_base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
            CloseHandle(snapshot);
            return true;
        }

        CloseHandle(snapshot);
        return false;
    }

    auto Process::get_section(std::string_view section_name) const
        -> std::optional<std::pair<uintptr_t, size_t>> {
        if (!m_module_base) {
            return std::nullopt;
        }

        auto dos_header = Memory::read<IMAGE_DOS_HEADER>(m_module_base);
        if (!dos_header || dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
            return std::nullopt;
        }

        auto nt_headers = Memory::read<IMAGE_NT_HEADERS64>(m_module_base + dos_header->e_lfanew);
        if (!nt_headers || nt_headers->Signature != IMAGE_NT_SIGNATURE) {
            return std::nullopt;
        }

        uintptr_t section_header_addr =
            m_module_base + dos_header->e_lfanew + sizeof(IMAGE_NT_HEADERS64);

        for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; i++) {
            auto section = Memory::read<IMAGE_SECTION_HEADER>(section_header_addr +
                                                              (i * sizeof(IMAGE_SECTION_HEADER)));
            if (!section) {
                continue;
            }

            std::string name(reinterpret_cast<const char*>(section->Name), 8);
            name = name.substr(0, name.find('\0'));

            if (name == section_name) {
                uintptr_t section_start = m_module_base + section->VirtualAddress;
                size_t section_size = section->Misc.VirtualSize;
                return std::make_pair(section_start, section_size);
            }
        }

        return std::nullopt;
    }

    auto Process::get_window_handle() const -> HWND {
        HWND hwnd = FindWindowA(nullptr, "Roblox");
        if (hwnd) {
            DWORD window_pid = 0;
            GetWindowThreadProcessId(hwnd, &window_pid);
            if (window_pid == m_pid && IsWindowVisible(hwnd)) {
                return hwnd;
            }
        }

        struct EnumData {
            DWORD pid;
            HWND found;
        };

        EnumData data{m_pid, nullptr};
        EnumWindows(
            [](HWND hwnd, LPARAM lParam) -> BOOL {
                EnumData* pData = reinterpret_cast<EnumData*>(lParam);
                DWORD window_pid = 0;
                GetWindowThreadProcessId(hwnd, &window_pid);
                if (window_pid == pData->pid && IsWindowVisible(hwnd)) {
                    pData->found = hwnd;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&data));

        return data.found;
    }

    auto Process::get_window_dimensions() const -> std::optional<glm::vec2> {
        HWND window = get_window_handle();
        if (!window) {
            return std::nullopt;
        }

        RECT rect;
        if (!GetClientRect(window, &rect)) {
            return std::nullopt;
        }

        float width = static_cast<float>(rect.right - rect.left);
        float height = static_cast<float>(rect.bottom - rect.top);

        if (width <= 0 || height <= 0) {
            return std::nullopt;
        }

        return glm::vec2(width, height);
    }

    auto Process::get_version() const -> std::optional<std::string> {
        if (!m_pid) {
            return std::nullopt;
        }

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return std::nullopt;
        }

        MODULEENTRY32W me = {sizeof(MODULEENTRY32W)};
        if (Module32FirstW(snapshot, &me)) {
            std::wstring path(me.szExePath);
            CloseHandle(snapshot);

            std::string narrow_path(path.begin(), path.end());

            std::regex version_regex(R"(version-[a-f0-9]{16})");
            std::smatch match;

            if (std::regex_search(narrow_path, match, version_regex)) {
                return match.str();
            }
        } else {
            CloseHandle(snapshot);
        }

        return std::nullopt;
    }

    auto Process::open_process(process_id_t pid) -> process_handle_t {
        using tNtOpenProcess =
            NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
        auto fn = m_ntdll.get_export<tNtOpenProcess>("NtOpenProcess");

        OBJECT_ATTRIBUTES obj_attrs{};
        obj_attrs.Length = sizeof(obj_attrs);

        CLIENT_ID client_id{};
        client_id.UniqueProcess = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));
        client_id.UniqueThread = nullptr;

        HANDLE handle = nullptr;
        NTSTATUS status = fn(&handle, PROCESS_ALL_ACCESS, &obj_attrs, &client_id);

        return NT_SUCCESS(status) ? handle : nullptr;
    }

    auto Process::find_process_by_name(std::string_view process_name) -> std::optional<process_id_t> {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return std::nullopt;
        }

        PROCESSENTRY32W pe = {sizeof(PROCESSENTRY32W)};
        std::wstring wide_name(process_name.begin(), process_name.end());

        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, wide_name.c_str()) == 0) {
                    CloseHandle(snapshot);
                    return pe.th32ProcessID;
                }
            } while (Process32NextW(snapshot, &pe));
        }

        CloseHandle(snapshot);
        return std::nullopt;
    }

    auto Process::get_target_arch() const -> target_arch { return target_arch::x86_64; }

    auto Process::enumerate_memory_regions(uintptr_t start, uintptr_t end) const
        -> std::vector<memory_region_t> {
        std::vector<memory_region_t> regions;
        MEMORY_BASIC_INFORMATION mbi{};
        uintptr_t current = start;

        while (current < end) {
            if (VirtualQueryEx(m_handle, reinterpret_cast<LPCVOID>(current), &mbi, sizeof(mbi)) !=
                sizeof(mbi)) {
                break;
            }

            const uintptr_t region_start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t region_end = region_start + mbi.RegionSize;

            memory_region_t region{};
            region.base = region_start;
            region.size = mbi.RegionSize;
            region.readable =
                mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && !(mbi.Protect & PAGE_NOACCESS);
            region.writable = region.readable && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                                                  PAGE_EXECUTE_READWRITE |
                                                                  PAGE_EXECUTE_WRITECOPY)) != 0;

            regions.push_back(region);
            current = region_end;
        }

        return regions;
    }

} // namespace process

#endif

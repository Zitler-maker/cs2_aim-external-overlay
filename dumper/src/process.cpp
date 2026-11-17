#include "process.h"

#include <tlhelp32.h>

#include "log.h"

namespace {

std::string win32_error(DWORD code) {
    char* msg = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, code, 0, reinterpret_cast<char*>(&msg), 0, nullptr);
    std::string out = std::to_string(code);
    if (len && msg) {
        std::string text(msg, len);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
            text.pop_back();
        out += " (" + text + ")";
    }
    if (msg) LocalFree(msg);
    return out;
}

} // namespace

Process::~Process() {
    if (process_) CloseHandle(process_);
}

bool Process::enable_debug_privilege(std::string& err) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        err = "OpenProcessToken failed: " + win32_error(GetLastError());
        return false;
    }
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        err = "LookupPrivilegeValue failed: " + win32_error(GetLastError());
        CloseHandle(token);
        return false;
    }
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(token, FALSE, &tp, sizeof tp, nullptr, nullptr)) {
        err = "AdjustTokenPrivileges failed: " + win32_error(GetLastError());
        CloseHandle(token);
        return false;
    }
    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        logx::warn("SeDebugPrivilege not granted (run elevated for live mode)");
    }
    CloseHandle(token);
    return true;
}

bool Process::attach(const std::wstring& exe_name, uint32_t pid, std::string& err) {
    if (!pid) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            err = "CreateToolhelp32Snapshot failed: " + win32_error(GetLastError());
            return false;
        }
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, exe_name.c_str()) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        if (!pid) {
            err = "process not found (see --process)";
            return false;
        }
    }

    process_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process_) {
        err = "OpenProcess failed: " + win32_error(GetLastError());
        return false;
    }
    pid_ = pid;
    return true;
}

bool Process::module(const std::wstring& name, uint64_t& base, uint32_t& size) const {
    base = 0;
    size = 0;
    if (!process_) return false;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me{};
    me.dwSize = sizeof me;
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, name.c_str()) == 0) {
                base = reinterpret_cast<uint64_t>(me.modBaseAddr);
                size = me.modBaseSize;
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

bool Process::read(uint64_t address, void* out, size_t size) const {
    if (!process_ || !out || size == 0) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(address), out, size, &got) != FALSE &&
           got == size;
}

bool Process::read_string(uint64_t address, std::string& out, size_t max_len) const {
    out.clear();
    if (!address) return false;
    char buf[256];
    size_t want = max_len < sizeof buf ? max_len : sizeof buf;
    if (!read(address, buf, want)) return false;
    for (size_t i = 0; i < want; ++i) {
        if (buf[i] == '\0') {
            if (i == 0) return false;
            out.assign(buf, i);
            return true;
        }
        unsigned char c = static_cast<unsigned char>(buf[i]);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return false;
}

bool Process::committed(uint64_t address, size_t size) const {
    if (!process_ || !address) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQueryEx(process_, reinterpret_cast<LPCVOID>(address), &mbi, sizeof mbi) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    uint64_t region_end = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize;
    return address + size <= region_end;
}

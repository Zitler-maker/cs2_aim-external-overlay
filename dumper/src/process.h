#pragma once
// Live process access: debug privilege, attach, module enumeration, safe reads.

#include <windows.h>

#include <cstdint>
#include <string>

class Process {
public:
    ~Process();

    bool enable_debug_privilege(std::string& err);
    bool attach(const std::wstring& exe_name, uint32_t pid, std::string& err);

    bool module(const std::wstring& name, uint64_t& base, uint32_t& size) const;

    bool read(uint64_t address, void* out, size_t size) const;
    template <typename T> bool read_value(uint64_t address, T& out) const {
        return read(address, &out, sizeof(T));
    }
    bool read_string(uint64_t address, std::string& out, size_t max_len) const;

    bool committed(uint64_t address, size_t size) const; // VirtualQueryEx check
    HANDLE handle() const { return process_; }
    uint32_t pid() const { return pid_; }

private:
    HANDLE process_ = nullptr;
    uint32_t pid_ = 0;
};

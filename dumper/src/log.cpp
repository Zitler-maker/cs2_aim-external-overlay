#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace logx {

namespace {
bool g_verbose = false;
std::wstring g_log_path;
std::mutex g_mu;

const char* level_name(Level l) {
    switch (l) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warn: return "WARN";
    case Level::Error: return "ERROR";
    }
    return "?";
}
} // namespace

void set_verbose(bool v) { g_verbose = v; }

void set_log_file(const std::wstring& path) { g_log_path = path; }

void write(Level level, const std::string& msg) {
    if (level == Level::Debug && !g_verbose) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);

    std::string line = std::string("[") + stamp + "] " + level_name(level) + " " + msg + "\n";

    std::lock_guard<std::mutex> lock(g_mu);
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);

    if (!g_log_path.empty()) {
        HANDLE h = CreateFileW(g_log_path.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            CloseHandle(h);
        }
    }
}

void vwrite(Level level, const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    write(level, buf);
}

} // namespace logx

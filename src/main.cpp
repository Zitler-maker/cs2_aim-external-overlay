#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <timeapi.h>
#include <string>
#include "shared.h"
#include "mem.h"
#include "config.h"
#include "overlay.h"

namespace {

PROCESS_INFORMATION g_dumper{};

std::wstring exeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir = path;
    return dir.substr(0, dir.find_last_of(L'\\') + 1);
}

// Keeps the offset dumper running in watch mode next to the cheat: it rewrites
// offsets.ini when the game build changes, config::reloadIfChanged() applies it.
void startDumper() {
    std::wstring dir = exeDir();
    std::wstring bin = dir + L"..\\dumper\\bin";
    std::wstring exe = bin + L"\\cs2_offsets_dumper.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    std::wstring cmd = L"\"" + exe + L"\" --source=live --watch=15 --out=\"" + bin +
                       L"\\config.json\" --ini=\"" + dir + L"offsets.ini\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, bin.c_str(), &si, &g_dumper)) {
        g_dumper = {};
        return;
    }
    CloseHandle(g_dumper.hThread);
    g_dumper.hThread = nullptr;
}

void stopDumper() {
    if (!g_dumper.hProcess) return;
    HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\cs2_offsets_watch_stop");
    if (ev) {
        SetEvent(ev);
        CloseHandle(ev);
    }
    if (WaitForSingleObject(g_dumper.hProcess, 3000) == WAIT_TIMEOUT)
        TerminateProcess(g_dumper.hProcess, 0);
    CloseHandle(g_dumper.hProcess);
    g_dumper = {};
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    config::load();
    startDumper();
    if (!mem::attach(L"cs2.exe")) {
        MessageBoxW(nullptr, L"cs2.exe not found", L"loader", MB_ICONERROR);
        stopDumper();
        return 1;
    }

    timeBeginPeriod(1);
    std::thread gt(gameThread);
    int rc = overlay::run(hInst);
    g_running = false;
    if (gt.joinable()) gt.join();
    timeEndPeriod(1);
    mem::detach();
    stopDumper();
    return rc;
}

#ifdef CHEAT_DBG
int main() {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, 0);
}
#endif

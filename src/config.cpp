#include "config.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

Offsets g_off;

namespace config {

namespace {

constexpr uintptr_t kMaxGlobalOffset = 0x4000000; // client.dll SizeOfImage bound
constexpr uintptr_t kMaxMemberOffset = 0x10000;

std::wstring iniPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ini = path;
    return ini.substr(0, ini.find_last_of(L'\\') + 1) + L"offsets.ini";
}

FILETIME g_stamp{};
uint64_t g_stampSize = ~0ull;

bool iniStamp(const std::wstring& path, FILETIME& ft, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return false;
    ft = fa.ftLastWriteTime;
    size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
    return true;
}

uintptr_t parseVal(const std::string& s) {
    return (uintptr_t)strtoull(s.c_str(), nullptr, 0);
}

bool saneValue(const std::string& key, uintptr_t v) {
    if (key == "entityChunksOffset" || key == "entityIdentityStride" || key == "signOnStateOffset")
        return v > 0 && v <= 0x1000;
    if (key.compare(0, 2, "dw") == 0) return v > 0 && v < kMaxGlobalOffset;
    return v > 0 && v <= kMaxMemberOffset;
}

bool applyKey(Offsets& o, const std::string& key, uintptr_t val) {
    if (key == "dwEntityList") o.dwEntityList = val;
    else if (key == "dwLocalPlayerController") o.dwLocalPlayerController = val;
    else if (key == "dwLocalPlayerPawn") o.dwLocalPlayerPawn = val;
    else if (key == "dwViewMatrix") o.dwViewMatrix = val;
    else if (key == "dwViewAngles") o.dwViewAngles = val;
    else if (key == "m_hPlayerPawn") o.m_hPlayerPawn = val;
    else if (key == "m_iTeamNum") o.m_iTeamNum = val;
    else if (key == "m_iHealth") o.m_iHealth = val;
    else if (key == "m_vOldOrigin") o.m_vOldOrigin = val;
    else if (key == "m_pGameSceneNode") o.m_pGameSceneNode = val;
    else if (key == "m_modelState") o.m_modelState = val;
    else if (key == "m_bDormant") o.m_bDormant = val;
    else if (key == "m_vecViewOffset") o.m_vecViewOffset = val;
    else if (key == "m_angEyeAngles") o.m_angEyeAngles = val;
    else if (key == "m_boneArray") o.m_boneArray = val;
    else if (key == "entityChunksOffset") o.entityChunksOffset = val;
    else if (key == "entityIdentityStride") o.entityIdentityStride = val;
    else if (key == "dwNetworkGameClient") o.dwNetworkGameClient = val;
    else if (key == "signOnStateOffset") o.signOnStateOffset = val;
    else return false;
    return true;
}

bool parseFile(Offsets& o) {
    FILE* f = _wfopen(iniPath().c_str(), L"r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\r' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ')) val.pop_back();
        if (key.empty() || key[0] == ';' || key[0] == '#' || key[0] == '[') continue;
        uintptr_t v = parseVal(val);
        if (!saneValue(key, v)) continue;
        applyKey(o, key, v);
    }
    fclose(f);
    return true;
}

} // namespace

void load() {
    std::wstring path = iniPath();
    parseFile(g_off);
    iniStamp(path, g_stamp, g_stampSize);
}

bool reloadIfChanged() {
    std::wstring path = iniPath();
    FILETIME ft{};
    uint64_t size = 0;
    if (!iniStamp(path, ft, size)) return false;
    if (size == g_stampSize && ft.dwLowDateTime == g_stamp.dwLowDateTime &&
        ft.dwHighDateTime == g_stamp.dwHighDateTime)
        return false;

    Offsets fresh = g_off;
    if (!parseFile(fresh)) return false; // file momentarily missing: retry next poll
    g_off = fresh;
    g_stamp = ft;
    g_stampSize = size;
    return true;
}

}

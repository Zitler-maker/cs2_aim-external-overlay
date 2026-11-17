#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "json.h"
#include "log.h"

namespace {
void offsets_map_insert(std::map<std::string, ConfigEntry>& m, const std::string& name, ConfigEntry e) {
    m[name] = std::move(e);
}
} // namespace

std::string hex32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%X", v);
    return buf;
}

std::string iso_timestamp() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring exe_directory() {
    wchar_t buf[MAX_PATH * 4]{};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    if (!len) return L".";
    std::wstring path(buf, len);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return path.substr(0, slash);
}

std::wstring join_path(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    wchar_t last = dir.back();
    return dir + (last == L'\\' || last == L'/' ? L"" : L"\\") + name;
}

bool ConfigFile::load(const std::wstring& path, std::string& err) {
    std::string text;
    if (!json::read_file_w(path, text)) { err = "config not readable"; return false; }

    json::Value root;
    if (!json::Value::parse(text, root, err)) { err = "config JSON parse error: " + err; return false; }
    if (!root.is_object()) { err = "config root is not an object"; return false; }

    version = static_cast<int>(root["version"].as_int());
    game_build = root["game_build"].is_string() ? root["game_build"].as_string() : "";
    timestamp = root["timestamp"].is_string() ? root["timestamp"].as_string() : "";

    const json::Value& offsets_val = root["offsets"];
    if (offsets_val.is_object()) {
        for (const auto& [name, val] : offsets_val.members()) {
            if (!val.is_object()) continue;
            ConfigEntry e;
            const json::Value& value = val["value"];
            if (value.is_string()) {
                const std::string& s = value.as_string();
                if (!s.empty()) {
                    e.value = static_cast<uint32_t>(std::strtoull(s.c_str(), nullptr, 0));
                    e.resolved = true;
                }
            } else if (value.is_number()) {
                e.value = static_cast<uint32_t>(value.as_int());
                e.resolved = true;
            }
            e.section = val["section"].is_string() ? val["section"].as_string() : "";
            e.module = val["module"].is_string() ? val["module"].as_string() : "client.dll";
            e.source = val["source"].is_string() ? val["source"].as_string() : "";
            e.sig = val["sig"].is_string() ? val["sig"].as_string() : "";
            e.class_name = val["class"].is_string() ? val["class"].as_string() : "";
            e.confidence = val["confidence"].is_number() ? val["confidence"].as_double() : 0.0;
            e.note = val["note"].is_string() ? val["note"].as_string() : "";
            if (val.has("chain") && val["chain"].is_array()) {
                for (const auto& hop : val["chain"].items()) {
                    if (hop.is_string())
                        e.chain.push_back(static_cast<uint32_t>(
                            std::strtoull(hop.as_string().c_str(), nullptr, 0)));
                    else if (hop.is_number())
                        e.chain.push_back(static_cast<uint32_t>(hop.as_int()));
                }
            }
            offsets_map_insert(offsets, name, std::move(e));
        }
    }
    return true;
}

void ConfigFile::merge_results(const std::vector<OffsetValue>& results) {
    for (const auto& r : results) {
        auto it = offsets.find(r.name);
        ConfigEntry stale;
        if (it != offsets.end()) stale = it->second;

        if (!r.resolved) {
            // Never overwrite a previously valid value with a miss.
            if (it == offsets.end()) {
                ConfigEntry e;
                e.resolved = false;
                e.source = "unresolved";
                e.note = r.note;
                offsets[r.name] = std::move(e);
            } else {
                it->second.note = "unresolved this run: " + r.note + " (kept previous value)";
            }
            continue;
        }
        ConfigEntry e;
        e.resolved = true;
        e.value = r.value;
        e.section = r.section;
        e.module = r.module;
        e.source = origin_name(r.origin);
        e.sig = r.sig;
        e.class_name = r.class_name;
        e.chain = r.chain;
        e.confidence = r.confidence;
        e.note = r.note;
        offsets[r.name] = std::move(e);
    }
}

std::string ConfigFile::serialize() const {
    json::Value root = json::Value::object();
    root["version"] = json::Value(version);
    root["game_build"] = json::Value(game_build);
    root["timestamp"] = json::Value(timestamp);

    json::Value offsets_obj = json::Value::object();
    for (const auto& [name, e] : offsets) {
        json::Value o = json::Value::object();
        o["value"] = e.resolved ? json::Value(hex32(e.value)) : json::Value();
        o["section"] = json::Value(e.section);
        o["module"] = json::Value(e.module);
        o["source"] = json::Value(e.source);
        o["confidence"] = json::Value(e.confidence);
        if (!e.sig.empty()) o["sig"] = json::Value(e.sig);
        if (!e.class_name.empty()) o["class"] = json::Value(e.class_name);
        if (!e.chain.empty()) {
            json::Value arr = json::Value::array();
            for (uint32_t hop : e.chain) arr.push(json::Value(hex32(hop)));
            o["chain"] = std::move(arr);
        }
        if (!e.note.empty()) o["note"] = json::Value(e.note);
        offsets_obj[name] = std::move(o);
    }
    root["offsets"] = std::move(offsets_obj);
    return root.dump(2) + "\n";
}

bool write_config_atomic(const std::wstring& path, const ConfigFile& cfg, std::string& err) {
    return json::write_file_atomic_w(path, cfg.serialize(), err);
}

bool write_text_atomic(const std::wstring& path, const std::string& text, std::string& err) {
    return json::write_file_atomic_w(path, text, err);
}

bool write_history(const std::wstring& history_dir, const ConfigFile& cfg, std::string& err) {
    if (!CreateDirectoryW(history_dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        err = "CreateDirectoryW(history) failed: " + std::to_string(GetLastError());
        return false;
    }
    wchar_t name[64];
    std::swprintf(name, 64, L"v%d.json", cfg.version);
    return write_text_atomic(join_path(history_dir, name), cfg.serialize(), err);
}

void prune_history(const std::wstring& history_dir, int keep) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(join_path(history_dir, L"v*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    std::vector<std::pair<int, std::wstring>> versions;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        int v = 0;
        if (std::swscanf(fd.cFileName, L"v%d.json", &v) == 1) {
            versions.emplace_back(v, fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(versions.begin(), versions.end());
    while (static_cast<int>(versions.size()) > keep) {
        DeleteFileW(join_path(history_dir, versions.front().second).c_str());
        versions.erase(versions.begin());
    }
}

std::string render_ini(const ConfigFile& cfg) {
    // Keys observed in the overlay's src/config.cpp.
    static const char* kKeys[] = {
        "dwEntityList",
        "dwLocalPlayerController",
        "dwLocalPlayerPawn",
        "dwViewMatrix",
        "dwViewAngles",
        "m_hPlayerPawn",
        "m_iTeamNum",
        "m_iHealth",
        "m_vOldOrigin",
        "m_pGameSceneNode",
        "m_modelState",
        "m_bDormant",
        "m_vecViewOffset",
        "m_angEyeAngles",
        "m_boneArray",
        "entityChunksOffset",
        "entityIdentityStride",
        "dwNetworkGameClient",
        "signOnStateOffset",
    };
    std::string out = "; generated by cs2_offsets_dumper\n";
    for (const char* key : kKeys) {
        auto it = cfg.offsets.find(key);
        if (it == cfg.offsets.end() || !it->second.resolved) continue;
        // Chain entries document "base + hops"; the overlay expects the final
        // member offset, which is the last hop.
        uint32_t value = it->second.chain.empty() ? it->second.value : it->second.chain.back();
        out += key;
        out += "=";
        out += hex32(value);
        out += "\n";
    }
    return out;
}

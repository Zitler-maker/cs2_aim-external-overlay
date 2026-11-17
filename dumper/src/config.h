#pragma once
// config.json model: load, diff-friendly save, atomic write, version history.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "offsets.h"

struct ConfigEntry {
    bool resolved = false;
    uint32_t value = 0;
    std::string section;
    std::string module;
    std::string source;
    std::string sig;
    std::string class_name;
    std::vector<uint32_t> chain;
    double confidence = 0.0;
    std::string note;
};

struct ConfigFile {
    int version = 0;
    std::string game_build;
    std::string timestamp;
    std::map<std::string, ConfigEntry> offsets;

    bool load(const std::wstring& path, std::string& err);
    std::string serialize() const;
    void merge_results(const std::vector<OffsetValue>& results);
};

bool write_config_atomic(const std::wstring& path, const ConfigFile& cfg, std::string& err);
bool write_history(const std::wstring& history_dir, const ConfigFile& cfg, std::string& err);
void prune_history(const std::wstring& history_dir, int keep);

std::string hex32(uint32_t v);
std::string iso_timestamp();
std::wstring exe_directory();
std::wstring join_path(const std::wstring& dir, const std::wstring& name);

// Legacy offsets.ini emitter for the existing overlay build (keys match
// src/config.cpp of the cheat).
std::string render_ini(const ConfigFile& cfg);
bool write_text_atomic(const std::wstring& path, const std::string& text, std::string& err);

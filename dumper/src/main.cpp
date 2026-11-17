#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "hash.h"
#include "json.h"
#include "log.h"
#include "netvar.h"
#include "offsets.h"
#include "pe.h"
#include "process.h"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitPartial = 1;
constexpr int kExitFatal = 2;
constexpr int kConfigHistoryKeep = 10;
constexpr int kWatchDefaultSecs = 15;
constexpr int kWatchMinSecs = 2;
constexpr int kWatchMaxSecs = 3600;

struct Options {
    std::string source = "file";
    std::wstring dll;
    std::wstring process_name = L"cs2.exe";
    uint32_t pid = 0;
    std::wstring out;
    std::wstring history_dir;
    std::wstring schema;
    std::wstring ini_out;
    int watch_secs = 0;
    bool verify_only = false;
    bool verbose = false;
    bool help = false;
};

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                  nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), len, nullptr,
                        nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

void print_help() {
    std::printf(
        "cs2_offsets_dumper - autonomous CS2 client.dll offset dumper\n"
        "\n"
        "Usage: cs2_offsets_dumper [options]\n"
        "\n"
        "  --source=file|live|sig   offset source (default: file)\n"
        "  --dll=<path>             client.dll path for file/sig modes\n"
        "  --process=<name>         process name for live mode (default: cs2.exe)\n"
        "  --pid=<n>                explicit process id for live mode\n"
        "  --out=<path>             output config.json (default: <exe dir>/config.json)\n"
        "  --history-dir=<path>     version history directory (default: <exe dir>/config_history)\n"
        "  --schema=<path>          schema dump JSON for netvars\n"
        "                           (default: <exe dir>/schema_client.json if present)\n"
        "  --ini=<path>             also emit a legacy offsets.ini for the existing overlay\n"
        "  --watch[=<sec>]          keep running; re-resolve only when the client.dll build\n"
        "                           changes (default %d s, min %d). Rewrites config.json and the\n"
        "                           offsets.ini; the cheat hot-reloads it. Waits for cs2.exe.\n"
        "  --verify-only            resolve and validate, write nothing\n"
        "  --verbose                verbose logging\n"
        "  --help                   this text\n"
        "\n"
        "Exit codes: 0 = all resolved + written (or up to date), 1 = partial (unresolved present),\n"
        "            2 = fatal (missing file, broken PE, access denied).\n",
        kWatchDefaultSecs, kWatchMinSecs);
}

bool arg_value(const std::wstring& arg, const wchar_t* name, std::wstring& out) {
    size_t n = std::wcslen(name);
    if (arg.size() <= n) return false;
    if (arg.compare(0, n, name) != 0) return false;
    if (arg[n] != L'=') return false;
    out = arg.substr(n + 1);
    return true;
}

bool parse_args(int argc, wchar_t** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        std::wstring value;
        if (a == L"--help" || a == L"-h") { opt.help = true; continue; }
        if (a == L"--verify-only") { opt.verify_only = true; continue; }
        if (a == L"--verbose" || a == L"-v") { opt.verbose = true; continue; }
        if (a == L"--watch") { opt.watch_secs = kWatchDefaultSecs; continue; }
        if (arg_value(a, L"--watch", value)) {
            long v = std::wcstol(value.c_str(), nullptr, 10);
            if (v < kWatchMinSecs) v = kWatchMinSecs;
            if (v > kWatchMaxSecs) v = kWatchMaxSecs;
            opt.watch_secs = static_cast<int>(v);
            continue;
        }
        if (arg_value(a, L"--source", value)) { opt.source = narrow(value); continue; }
        if (arg_value(a, L"--dll", value)) { opt.dll = value; continue; }
        if (arg_value(a, L"--process", value)) { opt.process_name = value; continue; }
        if (arg_value(a, L"--pid", value)) {
            opt.pid = static_cast<uint32_t>(std::wcstoul(value.c_str(), nullptr, 10));
            continue;
        }
        if (arg_value(a, L"--out", value)) { opt.out = value; continue; }
        if (arg_value(a, L"--history-dir", value)) { opt.history_dir = value; continue; }
        if (arg_value(a, L"--schema", value)) { opt.schema = value; continue; }
        if (arg_value(a, L"--ini", value)) { opt.ini_out = value; continue; }
        logx::error("unknown argument: " + narrow(a));
        return false;
    }
    return true;
}

bool file_exists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Cheap build fingerprint used by watch mode to decide whether a full resolve
// is needed. Live: module base + PE header (TimeDateStamp, SizeOfImage).
// File: size + last write time. Avoids a 43 MB image read every cycle.
struct BuildProbe {
    uint64_t base = 0;
    uint64_t size = 0;
    uint32_t timestamp = 0;
    uint32_t image_size = 0;

    std::string key() const {
        char b[96];
        std::snprintf(b, sizeof b, "%llX:%llX:%X:%X", static_cast<unsigned long long>(base),
                      static_cast<unsigned long long>(size), timestamp, image_size);
        return b;
    }
};

bool probe_build(const Options& opt, BuildProbe& p, std::string& err) {
    if (opt.source == "live") {
        Process proc;
        std::string perr;
        static bool privilege_tried = false;
        if (!privilege_tried) {
            proc.enable_debug_privilege(perr); // process-wide, warn at most once
            privilege_tried = true;
        }
        if (!proc.attach(opt.process_name, opt.pid, perr)) {
            err = perr;
            return false;
        }
        uint32_t mod_size = 0;
        if (!proc.module(L"client.dll", p.base, mod_size) || !mod_size) {
            err = "client.dll not loaded in target";
            return false;
        }
        p.size = mod_size;
        uint8_t hdr[0x400]{};
        size_t want = mod_size < sizeof hdr ? mod_size : sizeof hdr;
        if (!proc.read(p.base, hdr, want)) {
            err = "cannot read client.dll header";
            return false;
        }
        uint32_t e_lfanew = 0;
        std::memcpy(&e_lfanew, hdr + 0x3C, 4);
        if (e_lfanew + 0x60 > want) {
            err = "bad PE header";
            return false;
        }
        std::memcpy(&p.timestamp, hdr + e_lfanew + 8, 4);
        std::memcpy(&p.image_size, hdr + e_lfanew + 0x50, 4);
        return true;
    }

    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(opt.dll.c_str(), GetFileExInfoStandard, &fa)) {
        err = "cannot stat client.dll: " + narrow(opt.dll);
        return false;
    }
    p.size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
    p.timestamp = fa.ftLastWriteTime.dwLowDateTime;
    p.image_size = fa.ftLastWriteTime.dwHighDateTime;
    return true;
}

struct Run {
    PeImage image;
    std::string build_hash;
    std::wstring dll_path;
};

// Live module image cannot be content-hashed: .data mutates at runtime, so the
// hash would change between two reads of the same build. Use the PE header
// fingerprint instead (build time, section sizes, image size, checksum).
std::string live_build_tag(const PeImage& pe) {
    const uint8_t* d = pe.buffer();
    size_t n = pe.buffer_size();
    if (!d || n < 0x200) return {};
    uint32_t lf = 0;
    std::memcpy(&lf, d + 0x3C, 4);
    if (lf < 0x40 || lf + 24 + 70 > n) return {};
    uint16_t magic = 0;
    std::memcpy(&magic, d + lf + 24, 2);
    if (magic != 0x20B) return {}; // PE32+
    uint32_t timestamp = 0, size_code = 0, size_data = 0, checksum = 0, image_size = 0;
    std::memcpy(&timestamp, d + lf + 8, 4);
    const uint8_t* opt = d + lf + 24;
    std::memcpy(&size_code, opt + 4, 4);
    std::memcpy(&size_data, opt + 8, 4);
    std::memcpy(&image_size, opt + 56, 4);
    std::memcpy(&checksum, opt + 64, 4);
    char key[96];
    std::snprintf(key, sizeof key, "%X:%X:%X:%X:%X", timestamp, size_code, size_data,
                  image_size, checksum);
    return sha256_hex(reinterpret_cast<const uint8_t*>(key), std::strlen(key));
}

bool read_image(const Options& opt, Run& run, std::string& err) {
    if (opt.source == "live") {
        Process proc;
        std::string perr;
        if (!proc.enable_debug_privilege(perr)) {
            logx::warn("privilege: " + perr);
        }
        if (!proc.attach(opt.process_name, opt.pid, perr)) {
            err = perr;
            return false;
        }
        uint64_t base = 0;
        uint32_t size = 0;
        if (!proc.module(L"client.dll", base, size)) {
            err = "client.dll not found in target process";
            return false;
        }
        std::vector<uint8_t> buf(size);
        if (!proc.read(base, buf.data(), buf.size())) {
            err = "failed to read client.dll image from process";
            return false;
        }
        if (!run.image.map_memory(buf.data(), buf.size(), err)) return false;
        run.build_hash = live_build_tag(run.image);
        if (run.build_hash.empty()) {
            err = "cannot fingerprint client.dll headers";
            return false;
        }
        char basebuf[32];
        std::snprintf(basebuf, sizeof basebuf, "0x%llX", static_cast<unsigned long long>(base));
        logx::info(std::string("live: image @ ") + basebuf + ", " + std::to_string(size) + " bytes");
        return true;
    }

    std::wstring path = opt.dll;
    if (path.empty()) {
        err = "--dll is required for file/sig source";
        return false;
    }
    if (!file_exists(path)) {
        err = "client.dll not found: " + narrow(path);
        return false;
    }
    if (!run.image.load_file(path, err)) return false;
    run.dll_path = path;
    std::string bytes;
    if (!json::read_file_w(path, bytes)) {
        err = "cannot hash client.dll";
        return false;
    }
    run.build_hash = sha256_hex(bytes);
    logx::info("file: " + narrow(path) + ", " + std::to_string(bytes.size()) + " bytes");
    return true;
}

// Write text only when the content differs, so an idling watcher does not
// touch offsets.ini (the cheat reloads on mtime change).
bool write_ini_if_changed(const std::wstring& path, const std::string& text, std::string& err,
                          bool& wrote) {
    wrote = false;
    std::string current;
    if (json::read_file_w(path, current) && current == text) return true;
    if (!write_text_atomic(path, text, err)) return false;
    wrote = true;
    return true;
}

int run_once(const Options& opt, const std::wstring& schema_path) {
    Run run;
    std::string err;
    if (!read_image(opt, run, err)) {
        logx::error(err);
        return kExitFatal;
    }

    // ── schema / netvars ──
    SchemaDb schema;
    {
        if (opt.source == "live") {
            Process probe;
            std::string perr;
            probe.enable_debug_privilege(perr);
            if (probe.attach(opt.process_name, opt.pid, perr)) {
                if (schema_read_live(probe, schema, err)) {
                    logx::info("schema source: live schema system");
                } else {
                    logx::warn("live schema unavailable: " + err);
                }
            }
        }
        if (schema.empty() && !schema_path.empty()) {
            if (schema_load_json(schema_path, schema, err)) {
                logx::info("schema source: " + narrow(schema_path));
            } else {
                logx::warn("schema file rejected: " + err);
            }
        }
        if (schema.empty()) {
            logx::warn("no schema source: netvars will be unresolved (pass --schema)");
        }
    }

    // ── resolve ──
    std::string fatal;
    std::vector<OffsetValue> results = resolve_all(run.image, schema, &fatal);
    if (results.empty()) {
        logx::error("resolver produced no entries");
        return kExitFatal;
    }

    std::vector<std::string> unresolved;
    for (const auto& r : results) {
        if (!r.resolved) unresolved.push_back(r.name);
    }

    logx::info("resolved " + std::to_string(results.size() - unresolved.size()) + "/" +
               std::to_string(results.size()) + " offsets");
    for (const auto& name : unresolved) logx::warn("unresolved: " + name);

    if (opt.verify_only) {
        return unresolved.empty() ? kExitOk : kExitPartial;
    }

    // ── old config ──
    ConfigFile old_cfg;
    bool have_old = old_cfg.load(opt.out, err);
    if (!have_old && file_exists(opt.out)) {
        logx::error("existing config is unreadable: " + err);
        return kExitFatal;
    }

    // ── up to date? ──
    if (have_old && old_cfg.game_build == run.build_hash) {
        bool all_present = true;
        for (const auto& r : results) {
            auto it = old_cfg.offsets.find(r.name);
            if (it == old_cfg.offsets.end() || !it->second.resolved) { all_present = false; break; }
            if (r.resolved && it->second.value != r.value) { all_present = false; break; }
        }
        if (all_present && unresolved.empty()) {
            logx::info("up to date (game_build " + run.build_hash.substr(0, 16) + "...), config untouched");
            if (!opt.ini_out.empty()) {
                std::string ierr;
                bool wrote = false;
                if (!write_ini_if_changed(opt.ini_out, render_ini(old_cfg), ierr, wrote))
                    logx::warn("ini write failed: " + ierr);
                else if (wrote)
                    logx::info("wrote " + narrow(opt.ini_out));
            }
            return kExitOk;
        }
    }

    // ── merge + diff ──
    ConfigFile cfg = old_cfg;
    cfg.version = have_old ? old_cfg.version + 1 : 1;
    cfg.game_build = run.build_hash;
    cfg.timestamp = iso_timestamp();
    cfg.merge_results(results);

    int changed = 0;
    for (const auto& r : results) {
        if (!r.resolved) continue;
        auto old_it = old_cfg.offsets.find(r.name);
        if (old_it == old_cfg.offsets.end()) {
            logx::info("new " + r.name + " = " + hex32(r.value) + " (" + origin_name(r.origin) + ")");
            ++changed;
        } else if (!old_it->second.resolved) {
            logx::info("resolved " + r.name + " = " + hex32(r.value) + " (" + origin_name(r.origin) + ")");
            ++changed;
        } else if (old_it->second.value != r.value) {
            logx::vwrite(logx::Level::Info, "changed %s: %s -> %s (%s, conf %.2f)",
                         r.name.c_str(), hex32(old_it->second.value).c_str(),
                         hex32(r.value).c_str(), origin_name(r.origin), r.confidence);
            ++changed;
        }
    }
    logx::info("version " + std::to_string(cfg.version) + ", " + std::to_string(changed) +
               " changed offsets");

    // ── write ──
    if (!write_history(opt.history_dir, cfg, err)) logx::warn("history write failed: " + err);
    prune_history(opt.history_dir, kConfigHistoryKeep);

    if (!write_config_atomic(opt.out, cfg, err)) {
        logx::error("config write failed: " + err);
        return kExitFatal;
    }
    logx::info("wrote " + narrow(opt.out));

    if (!opt.ini_out.empty()) {
        std::string ierr;
        bool wrote = false;
        if (!write_ini_if_changed(opt.ini_out, render_ini(cfg), ierr, wrote)) {
            logx::warn("ini write failed: " + ierr);
        } else if (wrote) {
            logx::info("wrote " + narrow(opt.ini_out));
        }
    }

    if (!unresolved.empty()) {
        logx::warn(std::to_string(unresolved.size()) +
                   " offsets unresolved; previous values kept, exit code 1");
        return kExitPartial;
    }
    return kExitOk;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return kExitFatal;
    if (opt.help) { print_help(); return kExitOk; }

    logx::set_verbose(opt.verbose);

    if (opt.source != "file" && opt.source != "live" && opt.source != "sig") {
        logx::error("bad --source (expected file, live or sig)");
        return kExitFatal;
    }
    if (opt.source == "sig" && opt.dll.empty()) {
        logx::error("--source=sig requires --dll");
        return kExitFatal;
    }

    std::wstring exe_dir = exe_directory();
    if (opt.out.empty()) opt.out = join_path(exe_dir, L"config.json");
    if (opt.history_dir.empty()) opt.history_dir = join_path(exe_dir, L"config_history");

    // A file/sig watcher without --dll is useless; point it at the same place
    // the source image is normally taken from if the user passed nothing.
    if (opt.watch_secs > 0 && opt.source != "live" && opt.dll.empty()) {
        logx::error("--watch with --source=file|sig requires --dll");
        return kExitFatal;
    }

    std::wstring schema_path = opt.schema;
    if (schema_path.empty()) {
        std::wstring candidate = join_path(exe_dir, L"schema_client.json");
        if (file_exists(candidate)) schema_path = candidate;
    }

    if (!opt.verify_only) {
        size_t slash = opt.out.find_last_of(L"\\/");
        std::wstring log_dir = slash == std::wstring::npos ? std::wstring() : opt.out.substr(0, slash);
        logx::set_log_file(join_path(log_dir, L"update.log"));
    }

    if (opt.watch_secs <= 0 || opt.verify_only) {
        return run_once(opt, schema_path);
    }

    // ── watch mode: self-updating offsets ──
    HANDLE instance = CreateMutexW(nullptr, FALSE, L"Local\\cs2_offsets_watch_mutex");
    if (instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        logx::info("watch: another watcher is already running — exiting");
        CloseHandle(instance);
        return kExitOk;
    }
    HANDLE stop_ev = CreateEventW(nullptr, TRUE, FALSE, L"Local\\cs2_offsets_watch_stop");

    logx::info("watch: polling every " + std::to_string(opt.watch_secs) + "s for build changes" +
               (opt.ini_out.empty() ? std::string() : (", ini " + narrow(opt.ini_out))));

    std::string last_key;
    bool waiting = false;
    int rc = kExitOk;
    for (;;) {
        if (stop_ev && WaitForSingleObject(stop_ev, 0) == WAIT_OBJECT_0) break;

        BuildProbe probe;
        std::string perr;
        if (!probe_build(opt, probe, perr)) {
            if (!waiting) {
                logx::warn("watch: " + perr + " — waiting");
                waiting = true;
            } else {
                logx::debug("watch: still waiting (" + perr + ")");
            }
            rc = kExitFatal;
            if (stop_ev)
                WaitForSingleObject(stop_ev, static_cast<DWORD>(opt.watch_secs) * 1000);
            else
                Sleep(static_cast<DWORD>(opt.watch_secs) * 1000);
            continue;
        }
        if (waiting) {
            logx::info("watch: target available");
            waiting = false;
        }

        std::string key = probe.key();
        if (!last_key.empty() && key == last_key) {
            logx::debug("watch: no build change");
        } else {
            if (!last_key.empty())
                logx::info("watch: client.dll changed (" + key + ") — re-resolving");
            rc = run_once(opt, schema_path);
            if (rc == kExitFatal) {
                last_key.clear(); // retry on the next cycle
                logx::warn("watch: resolve failed, will retry");
            } else {
                last_key = key;
                logx::info("watch: offsets updated (exit " + std::to_string(rc) + ")");
            }
        }

        if (stop_ev) {
            if (WaitForSingleObject(stop_ev, static_cast<DWORD>(opt.watch_secs) * 1000) ==
                WAIT_OBJECT_0)
                break;
        } else {
            Sleep(static_cast<DWORD>(opt.watch_secs) * 1000);
        }
    }

    logx::info("watch: stopped");
    if (stop_ev) CloseHandle(stop_ev);
    if (instance) CloseHandle(instance);
    return rc;
}

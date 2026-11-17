#include "netvar.h"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <set>
#include <vector>

#include "json.h"
#include "log.h"
#include "pe.h"
#include "process.h"
#include "sig_scan.h"

namespace {

bool valid_ident(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != ':') return false;
    }
    return true;
}

} // namespace

const ClassSchema* SchemaDb::find_class(const std::string& name) const {
    auto it = classes.find(name);
    return it == classes.end() ? nullptr : &it->second;
}

bool SchemaDb::find_field(const std::string& field, uint32_t& value, std::string& owner,
                          int& distinct_values) const {
    distinct_values = 0;
    bool found = false;
    std::set<uint32_t> seen;
    for (const auto& [cls_name, cls] : classes) {
        auto it = cls.fields.find(field);
        if (it == cls.fields.end()) continue;
        if (it->second == 0) continue; // offset 0 is a union/padding artefact, not a real member
        seen.insert(it->second);
        if (!found) {
            value = it->second;
            owner = cls_name;
            found = true;
        }
    }
    distinct_values = static_cast<int>(seen.size());
    return found;
}

// ── JSON schema dump ─────────────────────────────────────────

bool schema_load_json(const std::wstring& path, SchemaDb& db, std::string& err) {
    std::string text;
    if (!json::read_file_w(path, text)) { err = "cannot read schema file"; return false; }

    json::Value root;
    if (!json::Value::parse(text, root, err)) { err = "schema JSON parse error: " + err; return false; }
    if (!root.is_object()) { err = "schema root is not an object"; return false; }

    const json::Value& module = root["client.dll"];
    const json::Value& classes = module["classes"];
    if (!classes.is_object()) { err = "schema has no client.dll.classes object"; return false; }

    db.classes.clear();
    for (const auto& [cls_name, cls_val] : classes.members()) {
        if (!valid_ident(cls_name)) continue;
        ClassSchema cls;
        cls.name = cls_name;
        const json::Value& parent = cls_val["parent"];
        if (parent.is_string()) cls.parent = parent.as_string();

        const json::Value& fields = cls_val["fields"];
        if (fields.is_object()) {
            for (const auto& [fname, fval] : fields.members()) {
                if (!valid_ident(fname) || !fval.is_number()) continue;
                int64_t off = fval.as_int();
                if (off < 0 || off > 0x10000) continue;
                cls.fields[fname] = static_cast<uint32_t>(off);
            }
        }
        db.classes.emplace(cls_name, std::move(cls));
    }

    if (db.classes.empty()) { err = "schema contains no classes"; return false; }
    db.origin = "json";
    return true;
}

// ── live schema system walk ──────────────────────────────────
//
// Structures mirror schemasystem.dll / client.dll runtime layout (x64).

namespace s2 {

#pragma pack(push, 1)
struct UtlVector {
    int32_t count;
    uint8_t pad[4];
    uint64_t data;
};

struct SchemaSystem {
    uint8_t pad_0[0x190];
    UtlVector type_scopes; // 0x190
    uint8_t pad_1[0xE0];
    int32_t registration_count; // 0x280
};

struct UtlTsHashBucket {
    uint64_t add_lock;
    uint64_t first;
    uint64_t first_uncommitted;
};

struct UtlTsHashNode {
    uint64_t key;
    uint64_t next;
    uint64_t data;
};

struct UtlMemoryPool {
    int32_t block_size;
    int32_t blocks_per_blob;
    uint32_t grow_mode;
    int32_t blocks_allocated;
    int32_t peak_allocated;
    uint16_t alignment_val;
    uint16_t blob_count;
    uint8_t pad_0[8];
    uint64_t free_list_head; // 0x20
    uint8_t pad_1[0x20];
    uint64_t blob_head; // 0x48
    int32_t total_size; // 0x50
    uint8_t pad_2[0xC];
};

struct UtlTsHash {
    UtlMemoryPool entry_mem;                 // 0x00
    UtlTsHashBucket buckets[256];            // 0x60
};

struct TypeScope {
    uint8_t pad_0[0x8];
    char name[256]; // 0x08
    uint8_t pad_1[0x458];
    UtlTsHash class_bindings; // 0x560
};

struct ClassInfo {
    uint64_t self;            // 0x00
    uint64_t name;            // 0x08
    uint64_t binary_name;     // 0x10
    uint64_t module_name;     // 0x18
    int32_t size;             // 0x20
    int16_t field_count;      // 0x24
    int16_t metadata_count;   // 0x26
    uint8_t pad_0[2];         // 0x28
    uint8_t alignment;        // 0x2A
    uint8_t has_base;         // 0x2B
    int16_t total_class_size; // 0x2C
    int16_t derived_size;     // 0x2E
    uint64_t fields;          // 0x30
    uint64_t pad_1;           // 0x38
    uint64_t base_classes;    // 0x40
    uint64_t static_metadata; // 0x48
    uint64_t pad_2;           // 0x50
    uint64_t type_scope;      // 0x58
};

struct ClassField {
    uint64_t name;      // 0x00
    uint64_t type_ptr;  // 0x08
    int32_t offset;     // 0x10
    int32_t meta_count; // 0x14
    uint64_t metadata;  // 0x18
};
#pragma pack(pop)

static_assert(sizeof(UtlVector) == 0x10, "layout");
static_assert(sizeof(UtlMemoryPool) == 0x60, "layout");
static_assert(sizeof(UtlTsHash) == 0x1860, "layout");
static_assert(sizeof(TypeScope) == 0x560 + 0x1860, "layout");
static_assert(sizeof(ClassInfo) == 0x60, "layout");
static_assert(sizeof(ClassField) == 0x20, "layout");

constexpr uint32_t kMaxClasses = 8192;
constexpr uint32_t kMaxFields = 4096;
constexpr int kHashBuckets = 256;

} // namespace s2

namespace {

bool collect_hash_data(Process& proc, uint64_t hash_addr, std::vector<uint64_t>& out) {
    s2::UtlTsHash hash{};
    if (!proc.read(hash_addr, &hash, sizeof hash)) return false;

    int used = hash.entry_mem.blocks_allocated;
    if (used < 0 || used > 65536) used = 0;

    std::set<uint64_t> seen;
    for (int i = 0; i < s2::kHashBuckets && static_cast<int>(out.size()) < used + 4096; ++i) {
        uint64_t node_ptr = hash.buckets[i].first_uncommitted;
        int guard = 0;
        while (node_ptr && guard++ < 8192) {
            if (!proc.committed(node_ptr, sizeof(s2::UtlTsHashNode))) break;
            s2::UtlTsHashNode node{};
            if (!proc.read(node_ptr, &node, sizeof node)) break;
            if (node.data && seen.insert(node.data).second) out.push_back(node.data);
            node_ptr = node.next;
        }
    }
    // Free-list blobs hold elements that were allocated then released into
    // the pool; they still carry live data pointers.
    int blob_limit = hash.entry_mem.peak_allocated;
    if (blob_limit < 0 || blob_limit > 65536) blob_limit = 0;
    uint64_t blob_ptr = hash.entry_mem.free_list_head;
    for (int i = 0; blob_ptr && i < blob_limit; ++i) {
        if (!proc.committed(blob_ptr, 0x18)) break;
        uint64_t data = 0;
        uint64_t next = 0;
        if (!proc.read(blob_ptr, &next, 8) || !proc.read(blob_ptr + 0x10, &data, 8)) break;
        if (data && seen.insert(data).second) out.push_back(data);
        blob_ptr = next;
    }

    return !out.empty();
}

bool read_class(Process& proc, uint64_t address, ClassSchema& cls) {
    s2::ClassInfo info{};
    if (!proc.read(address, &info, sizeof info)) return false;

    if (!proc.read_string(info.name, cls.name, 128) || !valid_ident(cls.name)) return false;

    if (info.fields && info.field_count > 0 && info.field_count <= static_cast<int16_t>(s2::kMaxFields)) {
        for (int i = 0; i < info.field_count; ++i) {
            s2::ClassField field{};
            uint64_t addr = info.fields + static_cast<uint64_t>(i) * sizeof(s2::ClassField);
            if (!proc.read(addr, &field, sizeof field)) break;
            std::string fname;
            if (!proc.read_string(field.name, fname, 128) || !valid_ident(fname)) continue;
            if (field.offset < 0 || field.offset > 0x10000) continue;
            cls.fields[fname] = static_cast<uint32_t>(field.offset);
        }
    }
    return !cls.fields.empty();
}

} // namespace

bool schema_read_live(Process& proc, SchemaDb& db, std::string& err) {
    uint64_t mod_base = 0;
    uint32_t mod_size = 0;
    if (!proc.module(L"schemasystem.dll", mod_base, mod_size)) {
        err = "schemasystem.dll not loaded in target";
        return false;
    }

    std::vector<uint8_t> image(mod_size);
    if (!proc.read(mod_base, image.data(), image.size())) {
        err = "cannot read schemasystem.dll image";
        return false;
    }

    PeImage pe;
    if (!pe.map_memory(image.data(), image.size(), err)) {
        err = "bad schemasystem.dll PE: " + err;
        return false;
    }

    SigMatch hit;
    int count = 0;
    std::string sig_err;
    if (!find_first(pe, "4c8d35${'} 0f2845", hit, count, sig_err) || hit.saves.empty()) {
        err = "schema system signature not found (update needed)";
        return false;
    }

    s2::SchemaSystem system{};
    if (!proc.read(mod_base + hit.saves[0], &system, sizeof system)) {
        err = "cannot read SchemaSystem";
        return false;
    }
    if (system.registration_count <= 0 || system.registration_count > 0x1000000) {
        err = "SchemaSystem registration count looks wrong";
        return false;
    }
    int scope_count = system.type_scopes.count;
    if (scope_count <= 0 || scope_count > 1024 || !system.type_scopes.data) {
        err = "SchemaSystem type scope vector looks wrong";
        return false;
    }

    int classes_total = 0;
    for (int i = 0; i < scope_count; ++i) {
        uint64_t scope_ptr = 0;
        if (!proc.read(system.type_scopes.data + static_cast<uint64_t>(i) * 8, &scope_ptr, 8) || !scope_ptr)
            continue;
        if (!proc.committed(scope_ptr, sizeof(s2::TypeScope))) continue;

        s2::TypeScope scope{};
        if (!proc.read(scope_ptr, &scope, sizeof scope)) continue;

        // Type scopes exist for every module; class bindings with the same
        // name appear in several of them, but only the client.dll scope
        // matches the offsets the game actually uses for client entities.
        std::string scope_name(scope.name, strnlen(scope.name, sizeof(scope.name)));
        for (char& ch : scope_name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        logx::debug("schema scope: \"" + scope_name + "\"");
        if (scope_name != "client.dll" && scope_name != "client") continue;

        std::vector<uint64_t> class_ptrs;
        if (!collect_hash_data(proc, scope_ptr + offsetof(s2::TypeScope, class_bindings), class_ptrs))
            continue;

        for (uint64_t ptr : class_ptrs) {
            if (classes_total >= static_cast<int>(s2::kMaxClasses)) break;
            if (!proc.committed(ptr, sizeof(s2::ClassInfo))) continue;
            ClassSchema cls;
            if (!read_class(proc, ptr, cls)) continue;
            ++classes_total;
            auto [it, inserted] = db.classes.emplace(cls.name, std::move(cls));
            if (!inserted && it->second.fields.size() < cls.fields.size()) {
                it->second = std::move(cls);
            }
        }
    }

    if (db.classes.empty()) {
        err = "live schema walk produced no classes";
        return false;
    }
    db.origin = "live";
    logx::info("live schema: " + std::to_string(db.classes.size()) + " classes");
    return true;
}

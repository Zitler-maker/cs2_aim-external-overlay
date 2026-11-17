#include "offsets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "log.h"
#include "netvar.h"
#include "pe.h"
#include "sig_scan.h"

namespace {

struct SigEntry {
    const char* name;
    const char* pattern;   // pelite-style AOB
    int capture = 0;       // index into SigMatch::saves
    const char* base;      // when set, value = base entry value + capture
    uint32_t max_value;    // upper bound sanity (0 = module bound)
    bool pointer_like;     // require 0x8 alignment
};

// All client.dll signatures verified against build 14197 (2026-11-12).
constexpr SigEntry kSigTable[] = {
    {"dwCSGOInput", "488905${'} 0f57c0 0f1105", 0, nullptr, 0, true},
    {"dwEntityList", "48890d${'} e9${} cc", 0, nullptr, 0, true},
    {"dwGameEntitySystem", "488b1d${'} 48891d[4] 4c63b3", 0, nullptr, 0, true},
    {"dwGameEntitySystem_highestEntityIndex", "ff81u4 4885d2", 0, nullptr, 0x10000, false},
    {"dwGameRules", "f6c1010f85${} 4c8b05${'} 4d85", 0, nullptr, 0, true},
    {"dwGlobalVars", "488915${'} 488942", 0, nullptr, 0, true},
    {"dwGlowManager", "488b05${'} c3 cccccccccccccccc 8b41", 0, nullptr, 0, true},
    {"dwLocalPlayerController", "488b05${'} 4189be", 0, nullptr, 0, true},
    {"dwPlantedC4", "488b1d${'} 4532f6", 0, nullptr, 0, true},
    {"dwPrediction", "488d05${'} c3 cccccccccccccccc 405356 4154", 0, nullptr, 0, true},
    {"dwSensitivity", "488d0d${[8]'} 0f57c90f28f0", 0, nullptr, 0, true},
    {"dwViewMatrix", "488d0d${'} 48c1e006", 0, nullptr, 0, true},
    {"dwWeaponC4", "488b15${'} 488b5c24? ffc0 8905${} 488bc6 488934ea 80be", 0, nullptr, 0, true},
    {"dwViewRender", "488905${'} 488bc8 4885c0", 0, nullptr, 0, true},
};

struct DerivedEntry {
    const char* name;
    const char* base;      // resolved global whose value is the anchor
    const char* pattern;   // signature inside the same function
    const char* what;      // free-form note
};

constexpr DerivedEntry kDerivedTable[] = {
    {"dwViewAngles", "dwCSGOInput", "f2420f108428u4", "view angle vector, anchored to CSGOInput"},
    {"dwLocalPlayerPawn", "dwPrediction", "4c39b6u4 74? 4488be", "local pawn pointer, anchored to Prediction"},
};

struct NetvarEntry {
    const char* name;
    const char* field;          // schema field name
    const char* preferred_class;
    uint32_t max_value;
};

constexpr NetvarEntry kNetvarTable[] = {
    {"m_iHealth", "m_iHealth", "C_BaseEntity", 0x2000},
    {"m_iTeamNum", "m_iTeamNum", "C_BaseEntity", 0x2000},
    {"m_lifeState", "m_lifeState", "C_BaseEntity", 0x2000},
    {"m_fFlags", "m_fFlags", "C_BaseEntity", 0x2000},
    {"m_vecOrigin", "m_vecOrigin", "CGameSceneNode", 0x2000},
    {"m_vecViewOffset", "m_vecViewOffset", "C_BaseModelEntity", 0x2000},
    {"m_vOldOrigin", "m_vOldOrigin", "C_BasePlayerPawn", 0x4000},
    {"m_angEyeAngles", "m_angEyeAngles", "C_CSPlayerPawn", 0x8000},
    {"m_hPlayerPawn", "m_hPlayerPawn", "CCSPlayerController", 0x2000},
    {"m_hActiveWeapon", "m_hActiveWeapon", "CPlayer_WeaponServices", 0x2000},
    {"m_iItemDefinitionIndex", "m_iItemDefinitionIndex", "C_EconItemView", 0x2000},
    {"m_iShotsFired", "m_iShotsFired", "C_CSPlayerPawn", 0x8000},
    {"m_bDormant", "m_bDormant", "CGameSceneNode", 0x2000},
    {"m_pGameSceneNode", "m_pGameSceneNode", "C_BaseEntity", 0x2000},
    {"m_vecAbsOrigin", "m_vecAbsOrigin", "CGameSceneNode", 0x2000},
    {"m_iCompetitiveRanking", "m_iCompetitiveRanking", "CCSPlayerController", 0x4000},
    {"m_iCompetitiveWins", "m_iCompetitiveWins", "CCSPlayerController", 0x4000},
    {"m_flFlashDuration", "m_flFlashDuration", "C_CSPlayerPawnBase", 0x4000},
    {"m_flFlashMaxAlpha", "m_flFlashMaxAlpha", "C_CSPlayerPawnBase", 0x4000},
    {"m_szName", "m_szName", "CCSWeaponBaseVData", 0x2000},
    {"m_iCrosshairId", "m_iIDEntIndex", "C_CSPlayerPawn", 0x8000},
    // Intermediate fields needed by chain entries, also exported for tooling.
    {"m_entitySpottedState", "m_entitySpottedState", "C_CSPlayerPawn", 0x8000},
    {"m_pAimPunchServices", "m_pAimPunchServices", "C_CSPlayerPawn", 0x8000},
};

struct ChainEntry {
    const char* name;
    const char* base_field;     // netvar field used as the base
    const char* base_class;
    uint32_t base_max;
    std::vector<uint32_t> hops;
    double confidence;
    const char* note;
};

const std::vector<ChainEntry>& chain_table() {
    static const std::vector<ChainEntry> table = {
        {"m_aimPunchAngle", "m_pAimPunchServices", "C_CSPlayerPawn", 0x8000,
         {0x50}, 0.6, "CCSPlayer_AimPunchServices::m_predictableBaseAngle"},
        {"m_bSpotted", "m_entitySpottedState", "C_CSPlayerPawn", 0x8000,
         {0x8}, 0.8, "EntitySpottedState_t::m_bSpotted"},
        {"m_bSpottedByMask", "m_entitySpottedState", "C_CSPlayerPawn", 0x8000,
         {0xC}, 0.8, "EntitySpottedState_t::m_bSpottedByMask"},
        {"m_boneArray", "m_pGameSceneNode", "C_BaseEntity", 0x2000,
         {0x140, 0x80}, 0.5, "CModelState::m_pBoneArray (runtime layout constant)"},
    };
    return table;
}

struct ConstantEntry {
    const char* name;
    uint32_t value;
    double confidence;
    const char* note;
};

constexpr ConstantEntry kConstantTable[] = {
    {"dwSensitivity_sensitivity", 0x58, 1.0, "CInput::m_flSensitivity, stable member offset"},
    {"m_modelState", 0x140, 0.5, "CGameSceneNode::m_modelState, runtime layout constant"},
};

bool validate_global(const PeImage& img, uint32_t value, uint32_t max_value, bool pointer_like,
                     std::string& section, std::string& why) {
    if (value == 0) { why = "resolved to zero"; return false; }
    if (max_value) {
        if (value >= max_value) { why = "value above sanity bound"; return false; }
        section = "member_offset";
        if (pointer_like && (value & 7u)) { why = "not 8-byte aligned"; return false; }
        return true;
    }
    if (value >= img.size_of_image()) { why = "outside SizeOfImage"; return false; }
    if (pointer_like && (value & 7u)) { why = "not 8-byte aligned"; return false; }
    section = img.section_name_of(value);
    if (section != ".data") {
        why = "target not in .data (got " + (section.empty() ? "none" : section) + ")";
        return false;
    }
    return true;
}

void add_unresolved(std::vector<OffsetValue>& out, const char* name, const char* module,
                    const std::string& note) {
    OffsetValue v;
    v.name = name;
    v.module = module;
    v.resolved = false;
    v.origin = Origin::Unresolved;
    v.confidence = 0.0;
    v.note = note;
    out.push_back(std::move(v));
}

} // namespace

const char* origin_name(Origin o) {
    switch (o) {
    case Origin::Sig: return "sig";
    case Origin::Derived: return "sig+derived";
    case Origin::Netvar: return "netvar";
    case Origin::Chain: return "netvar+chain";
    case Origin::Constant: return "constant";
    case Origin::Unresolved: return "unresolved";
    }
    return "?";
}

std::vector<OffsetValue> resolve_all(const PeImage& img, const SchemaDb& schema,
                                     std::string* fatal_err) {
    std::vector<OffsetValue> results;
    std::map<std::string, size_t> index;

    auto emit = [&](OffsetValue v) {
        index[v.name] = results.size();
        results.push_back(std::move(v));
    };
    auto find_value = [&](const std::string& name, uint32_t& value) -> bool {
        auto it = index.find(name);
        if (it == index.end() || !results[it->second].resolved) return false;
        value = results[it->second].value;
        return true;
    };

    // 1. Signature globals.
    for (const auto& entry : kSigTable) {
        OffsetValue v;
        v.name = entry.name;
        v.sig = entry.pattern;
        v.module = "client.dll";

        SigMatch hit;
        int count = 0;
        std::string err;
        if (!find_first(img, entry.pattern, hit, count, err)) {
            logx::warn(std::string("sig miss: ") + entry.name + " (" + err + ")");
            v.origin = Origin::Unresolved;
            v.note = "signature not found: " + err;
            emit(std::move(v));
            continue;
        }
        if (entry.capture < 0 || static_cast<size_t>(entry.capture) >= hit.saves.size()) {
            v.origin = Origin::Unresolved;
            v.note = "signature capture index out of range";
            emit(std::move(v));
            continue;
        }

        uint32_t value = hit.saves[static_cast<size_t>(entry.capture)];
        if (entry.base) {
            uint32_t base_value = 0;
            if (!find_value(entry.base, base_value)) {
                v.origin = Origin::Unresolved;
                v.note = std::string("base offset ") + entry.base + " unresolved";
                emit(std::move(v));
                continue;
            }
            value += base_value;
            v.origin = Origin::Derived;
        } else {
            v.origin = Origin::Sig;
        }

        std::string section, why;
        if (!validate_global(img, value, entry.max_value, entry.pointer_like, section, why)) {
            logx::warn(std::string("validation reject: ") + entry.name + " -> " + why);
            v.origin = Origin::Unresolved;
            v.note = "validation failed: " + why;
            emit(std::move(v));
            continue;
        }

        v.resolved = true;
        v.value = value;
        v.section = section;
        v.confidence = count == 1 ? 1.0 : 0.95;
        if (count > 1) v.note = "pattern matched " + std::to_string(count) + " times";
        emit(std::move(v));
    }

    // 2. Derived globals anchored to a signature-found global.
    for (const auto& entry : kDerivedTable) {
        OffsetValue v;
        v.name = entry.name;
        v.sig = entry.pattern;
        v.module = "client.dll";

        uint32_t base_value = 0;
        if (!find_value(entry.base, base_value)) {
            v.origin = Origin::Unresolved;
            v.note = std::string("anchor ") + entry.base + " unresolved";
            emit(std::move(v));
            continue;
        }

        SigMatch hit;
        int count = 0;
        std::string err;
        if (!find_first(img, entry.pattern, hit, count, err) || hit.saves.empty()) {
            v.origin = Origin::Unresolved;
            v.note = "anchor signature not found: " + err;
            emit(std::move(v));
            continue;
        }

        uint32_t value = base_value + hit.saves[0];
        std::string section, why;
        if (!validate_global(img, value, 0, true, section, why)) {
            v.origin = Origin::Unresolved;
            v.note = "validation failed: " + why;
            emit(std::move(v));
            continue;
        }
        v.resolved = true;
        v.value = value;
        v.section = section;
        v.origin = Origin::Derived;
        v.confidence = count == 1 ? 1.0 : 0.9;
        v.note = entry.what;
        emit(std::move(v));
    }

    // 3. Netvars from the schema database.
    if (schema.empty()) {
        logx::warn("no schema database: all netvars will be unresolved");
    }
    for (const auto& entry : kNetvarTable) {
        OffsetValue v;
        v.name = entry.name;
        v.module = "client.dll";

        if (schema.empty()) {
            v.origin = Origin::Unresolved;
            v.note = "no schema source";
            emit(std::move(v));
            continue;
        }

        uint32_t value = 0;
        std::string owner;
        uint32_t picked = 0;
        int distinct = 0;
        bool found = false;

        if (const ClassSchema* cls = schema.find_class(entry.preferred_class)) {
            auto it = cls->fields.find(entry.field);
            if (it != cls->fields.end() && it->second != 0) {
                picked = it->second;
                owner = cls->name;
                found = true;
            }
        }
        if (!found) {
            std::string any_owner;
            if (!schema.find_field(entry.field, value, any_owner, distinct)) {
                v.origin = Origin::Unresolved;
                v.note = std::string("field ") + entry.field + " not present in schema";
                emit(std::move(v));
                continue;
            }
            picked = value;
            owner = any_owner;
        }

        if (picked == 0 || picked >= entry.max_value) {
            v.origin = Origin::Unresolved;
            v.note = "offset outside sanity bound";
            emit(std::move(v));
            continue;
        }

        v.resolved = true;
        v.value = picked;
        v.origin = Origin::Netvar;
        v.section = "class_schema";
        v.class_name = owner;
        v.confidence = 1.0;
        if (owner != entry.preferred_class) {
            v.confidence = 0.8;
            v.note = std::string("resolved via ") + owner + " (expected " + entry.preferred_class + ")";
        }
        emit(std::move(v));
    }

    // 4. Constant member offsets.
    for (const auto& entry : kConstantTable) {
        OffsetValue v;
        v.name = entry.name;
        v.value = entry.value;
        v.resolved = true;
        v.origin = Origin::Constant;
        v.section = "const";
        v.confidence = entry.confidence;
        v.note = entry.note;
        emit(std::move(v));
    }

    // 5. Chained netvars.
    for (const auto& entry : chain_table()) {
        OffsetValue v;
        v.name = entry.name;
        v.module = "client.dll";

        if (schema.empty()) {
            v.origin = Origin::Unresolved;
            v.note = "no schema source";
            emit(std::move(v));
            continue;
        }

        uint32_t base_value = 0;
        bool base_ok = false;
        if (const ClassSchema* cls = schema.find_class(entry.base_class)) {
            auto it = cls->fields.find(entry.base_field);
            if (it != cls->fields.end() && it->second != 0) {
                base_value = it->second;
                base_ok = true;
            }
        }
        if (!base_ok) {
            std::string owner;
            int distinct = 0;
            base_ok = schema.find_field(entry.base_field, base_value, owner, distinct);
        }
        if (!base_ok || base_value == 0 || base_value >= entry.base_max) {
            v.origin = Origin::Unresolved;
            v.note = std::string("chain base ") + entry.base_field + " unresolved";
            emit(std::move(v));
            continue;
        }

        v.resolved = true;
        v.value = base_value;
        v.chain = entry.hops;
        v.origin = Origin::Chain;
        v.section = "class_schema";
        v.class_name = entry.base_class;
        v.confidence = entry.confidence;
        v.note = entry.note;
        emit(std::move(v));
    }

    if (fatal_err) fatal_err->clear();
    return results;
}

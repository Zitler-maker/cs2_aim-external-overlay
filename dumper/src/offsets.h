#pragma once
// Offset table + resolver: every entry knows how it is found, how it is
// validated, and what confidence to report.

#include <cstdint>
#include <string>
#include <vector>

class PeImage;
struct SchemaDb;

enum class Origin { Sig, Derived, Netvar, Chain, Constant, Unresolved };

const char* origin_name(Origin o);

struct OffsetValue {
    std::string name;
    bool resolved = false;
    uint32_t value = 0;
    Origin origin = Origin::Unresolved;
    std::string module = "client.dll";
    std::string section;   // .data / .text / class_schema / const
    std::string sig;       // pattern used, if any
    std::string class_name;
    std::vector<uint32_t> chain; // extra dereferences/offsets for chain entries
    double confidence = 0.0;
    std::string note;
};

// Resolves every mandatory offset plus optional extras.
// `img` must contain client.dll (file or live image).
// `schema` may be empty; netvars then stay unresolved.
std::vector<OffsetValue> resolve_all(const PeImage& img, const SchemaDb& schema,
                                     std::string* fatal_err);

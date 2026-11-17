#pragma once
// Schema (netvar) resolution.
//   - offline: parse a schema dump JSON (client_dll.json produced by any CS2
//     schema dumper, format: { "client.dll": { "classes": { name: { fields: {..} } } } })
//   - live: walk the schema system inside the running game (schemasystem.dll)

#include <cstdint>
#include <map>
#include <string>

class Process;

struct ClassSchema {
    std::string name;
    std::string parent;
    std::map<std::string, uint32_t> fields;
};

struct SchemaDb {
    std::map<std::string, ClassSchema> classes;
    std::string origin; // "json:<path>" or "live"

    bool empty() const { return classes.empty(); }
    const ClassSchema* find_class(const std::string& name) const;
    // Look up a field inside one class, then across all classes.
    bool find_field(const std::string& field, uint32_t& value, std::string& owner,
                    int& distinct_values) const;
};

bool schema_load_json(const std::wstring& path, SchemaDb& db, std::string& err);
bool schema_read_live(Process& proc, SchemaDb& db, std::string& err);

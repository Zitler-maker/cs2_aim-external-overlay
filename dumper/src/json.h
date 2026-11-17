#pragma once
// Minimal but complete JSON value / parser / serializer (RFC 8259 subset:
// objects, arrays, strings with escapes, numbers, booleans, null).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool v) : type_(Type::Bool), bool_(v) {}
    Value(double v) : type_(Type::Number), num_(v) {}
    Value(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(uint32_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}

    static Value array() { Value v; v.type_ = Type::Array; return v; }
    static Value object() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_string() const { return type_ == Type::String; }
    bool is_number() const { return type_ == Type::Number; }

    bool as_bool() const { return bool_; }
    double as_double() const { return num_; }
    int64_t as_int() const { return static_cast<int64_t>(num_); }
    const std::string& as_string() const { return str_; }

    // Object helpers.
    bool has(const std::string& key) const;
    const Value& operator[](const std::string& key) const; // returns null value if missing
    Value& operator[](const std::string& key);
    const std::map<std::string, Value>& members() const { return obj_; }

    // Array helpers.
    void push(Value v) { arr_.push_back(std::move(v)); }
    const std::vector<Value>& items() const { return arr_; }
    size_t size() const { return type_ == Type::Array ? arr_.size() : obj_.size(); }

    std::string dump(int indent = 2) const;

    static bool parse(const std::string& text, Value& out, std::string& err);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Value> arr_;
    std::map<std::string, Value> obj_;

    void dump_to(std::string& out, int indent, int depth) const;
};

bool read_file(const std::string& path, std::string& out);
bool write_file_atomic(const std::string& path, const std::string& content, std::string& err);
bool read_file_w(const std::wstring& path, std::string& out);
bool write_file_atomic_w(const std::wstring& path, const std::string& content, std::string& err);

} // namespace json

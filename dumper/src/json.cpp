#include "json.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace json {

static const Value g_null;

bool Value::has(const std::string& key) const {
    return type_ == Type::Object && obj_.find(key) != obj_.end();
}

const Value& Value::operator[](const std::string& key) const {
    if (type_ != Type::Object) return g_null;
    auto it = obj_.find(key);
    return it == obj_.end() ? g_null : it->second;
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::Object) type_ = Type::Object;
    return obj_[key];
}

// ── serializer ───────────────────────────────────────────────

static void escape_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

static void format_number(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out += "0";
        return;
    }
    char buf[64];
    if (v == std::floor(v) && std::fabs(v) < 9.0e15) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    } else {
        std::snprintf(buf, sizeof buf, "%.17g", v);
    }
    out += buf;
}

void Value::dump_to(std::string& out, int indent, int depth) const {
    std::string pad(static_cast<size_t>(indent * depth), ' ');
    std::string pad_inner(static_cast<size_t>(indent * (depth + 1)), ' ');
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: format_number(out, num_); break;
    case Type::String: escape_string(out, str_); break;
    case Type::Array: {
        if (arr_.empty()) { out += "[]"; break; }
        out += "[\n";
        for (size_t i = 0; i < arr_.size(); ++i) {
            out += pad_inner;
            arr_[i].dump_to(out, indent, depth + 1);
            if (i + 1 < arr_.size()) out += ',';
            out += '\n';
        }
        out += pad;
        out += ']';
        break;
    }
    case Type::Object: {
        if (obj_.empty()) { out += "{}"; break; }
        out += "{\n";
        size_t i = 0;
        for (const auto& [k, v] : obj_) {
            out += pad_inner;
            escape_string(out, k);
            out += indent > 0 ? ": " : ":";
            v.dump_to(out, indent, depth + 1);
            if (++i < obj_.size()) out += ',';
            out += '\n';
        }
        out += pad;
        out += '}';
        break;
    }
    }
}

std::string Value::dump(int indent) const {
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

// ── parser ───────────────────────────────────────────────────

namespace {

struct Parser {
    const std::string& s;
    size_t pos = 0;
    std::string err;

    explicit Parser(const std::string& text) : s(text) {}

    void skip_ws() {
        while (pos < s.size()) {
            char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos; continue; }
            break;
        }
    }

    bool fail(const char* msg) {
        if (err.empty()) {
            err = std::string(msg) + " at byte " + std::to_string(pos);
        }
        return false;
    }

    bool parse_value(Value& out, int depth) {
        if (depth > 200) return fail("nesting too deep");
        skip_ws();
        if (pos >= s.size()) return fail("unexpected end of input");
        char c = s[pos];
        switch (c) {
        case '{': return parse_object(out, depth);
        case '[': return parse_array(out, depth);
        case '"': {
            std::string str;
            if (!parse_string(str)) return false;
            out = Value(std::move(str));
            return true;
        }
        case 't':
            if (s.compare(pos, 4, "true") == 0) { pos += 4; out = Value(true); return true; }
            return fail("bad literal");
        case 'f':
            if (s.compare(pos, 5, "false") == 0) { pos += 5; out = Value(false); return true; }
            return fail("bad literal");
        case 'n':
            if (s.compare(pos, 4, "null") == 0) { pos += 4; out = Value(); return true; }
            return fail("bad literal");
        default:
            return parse_number(out);
        }
    }

    bool parse_object(Value& out, int depth) {
        ++pos; // '{'
        Value obj = Value::object();
        skip_ws();
        if (pos < s.size() && s[pos] == '}') { ++pos; out = std::move(obj); return true; }
        for (;;) {
            skip_ws();
            if (pos >= s.size() || s[pos] != '"') return fail("expected object key");
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos >= s.size() || s[pos] != ':') return fail("expected ':'");
            ++pos;
            Value val;
            if (!parse_value(val, depth + 1)) return false;
            obj[key] = std::move(val);
            skip_ws();
            if (pos >= s.size()) return fail("unterminated object");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == '}') { ++pos; out = std::move(obj); return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool parse_array(Value& out, int depth) {
        ++pos; // '['
        Value arr = Value::array();
        skip_ws();
        if (pos < s.size() && s[pos] == ']') { ++pos; out = std::move(arr); return true; }
        for (;;) {
            Value val;
            if (!parse_value(val, depth + 1)) return false;
            arr.push(std::move(val));
            skip_ws();
            if (pos >= s.size()) return fail("unterminated array");
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == ']') { ++pos; out = std::move(arr); return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool parse_string(std::string& out) {
        ++pos; // opening quote
        out.clear();
        while (pos < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[pos++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= s.size()) return fail("bad escape");
                char e = s[pos++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > s.size()) return fail("bad \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = s[pos + static_cast<size_t>(i)];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail("bad \\u escape");
                    }
                    pos += 4;
                    // BMP only; surrogate pairs are rare in this data.
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return fail("bad escape");
                }
                continue;
            }
            if (c < 0x20) return fail("control character in string");
            out.push_back(static_cast<char>(c));
        }
        return fail("unterminated string");
    }

    bool parse_number(Value& out) {
        size_t start = pos;
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
        bool any = false;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { ++pos; any = true; }
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { ++pos; any = true; }
        }
        if (any && pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (!any) return fail("expected number");
        out = Value(std::strtod(s.substr(start, pos - start).c_str(), nullptr));
        return true;
    }
};

} // namespace

bool Value::parse(const std::string& text, Value& out, std::string& err) {
    Parser p(text);
    Value v;
    if (!p.parse_value(v, 0)) { err = p.err; return false; }
    p.skip_ws();
    if (p.pos != text.size()) { err = "trailing data at byte " + std::to_string(p.pos); return false; }
    out = std::move(v);
    return true;
}

// ── file helpers ─────────────────────────────────────────────

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_file_atomic_w(const std::wstring& path, const std::string& content, std::string& err) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = "CreateFileW(tmp) failed: " + std::to_string(GetLastError());
        return false;
    }
    const char* p = content.data();
    size_t left = content.size();
    bool ok = true;
    while (left > 0) {
        DWORD chunk = static_cast<DWORD>(left > 0x10000000u ? 0x10000000u : left);
        DWORD written = 0;
        if (!WriteFile(h, p, chunk, &written, nullptr) || written != chunk) {
            err = "WriteFile failed: " + std::to_string(GetLastError());
            ok = false;
            break;
        }
        p += written;
        left -= written;
    }
    if (ok && !FlushFileBuffers(h)) {
        err = "FlushFileBuffers failed: " + std::to_string(GetLastError());
        ok = false;
    }
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = "MoveFileExW failed: " + std::to_string(GetLastError());
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool write_file_atomic(const std::string& path, const std::string& content, std::string& err) {
    int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), len);
    if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();
    return write_file_atomic_w(wpath, content, err);
}

bool read_file_w(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 512LL * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t left = out.size();
    size_t off = 0;
    bool ok = true;
    while (left > 0) {
        DWORD chunk = static_cast<DWORD>(left > 0x10000000u ? 0x10000000u : left);
        DWORD got = 0;
        if (!ReadFile(h, out.data() + off, chunk, &got, nullptr) || got == 0) { ok = false; break; }
        off += got;
        left -= got;
    }
    CloseHandle(h);
    return ok;
}

} // namespace json

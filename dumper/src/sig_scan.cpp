#include "sig_scan.h"

#include "pe.h"

#include <cstring>

namespace {

bool hex_nibble(char c, uint8_t& v) {
    if (c >= '0' && c <= '9') { v = static_cast<uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { v = static_cast<uint8_t>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { v = static_cast<uint8_t>(c - 'A' + 10); return true; }
    return false;
}

bool is_sep(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

} // namespace

bool compile_sig(const std::string& pattern, CompiledSig& out, std::string& err) {
    out = CompiledSig{};
    size_t i = 0;
    const size_t n = pattern.size();

    auto skip_sep = [&]() { while (i < n && is_sep(pattern[i])) ++i; };

    while (true) {
        skip_sep();
        if (i >= n) break;
        char c = pattern[i];

        if (c == '?') {
            size_t j = i;
            while (j < n && pattern[j] == '?') ++j;
            size_t qmarks = j - i;
            // "??" is one wildcard byte; a lone "?" inside tokens like
            // "488b5c24?" is also one wildcard byte.
            size_t bytes = (qmarks + 1) / 2;
            CompiledSig::Op op;
            op.kind = CompiledSig::Op::Kind::Skip;
            op.count = static_cast<uint32_t>(bytes);
            out.ops.push_back(op);
            i = j;
            continue;
        }

        if (c == '[') {
            size_t close = pattern.find(']', i);
            if (close == std::string::npos) { err = "unterminated skip group"; return false; }
            std::string num = pattern.substr(i + 1, close - i - 1);
            if (num.empty()) { err = "empty skip group"; return false; }
            CompiledSig::Op op;
            op.kind = CompiledSig::Op::Kind::Skip;
            op.count = static_cast<uint32_t>(std::strtoul(num.c_str(), nullptr, 10));
            if (op.count == 0 || op.count > 256) { err = "bad skip count"; return false; }
            out.ops.push_back(op);
            i = close + 1;
            continue;
        }

        if (c == '$') {
            // ${'} | ${} | ${[N]'}
            if (i + 2 >= n || pattern[i + 1] != '{') { err = "malformed ${"; return false; }
            size_t j = i + 2;
            uint32_t add = 0;
            if (j < n && pattern[j] == '[') {
                size_t close = pattern.find(']', j);
                if (close == std::string::npos) { err = "bad ${[N]"; return false; }
                add = static_cast<uint32_t>(std::strtoul(pattern.substr(j + 1, close - j - 1).c_str(), nullptr, 10));
                j = close + 1;
            }
            bool save = false;
            if (j < n && pattern[j] == '\'') { save = true; ++j; }
            if (j >= n || pattern[j] != '}') { err = "missing } in ${"; return false; }
            CompiledSig::Op op;
            op.kind = save ? CompiledSig::Op::Kind::RipSave : CompiledSig::Op::Kind::RipNoSave;
            op.count = add;
            out.ops.push_back(op);
            i = j + 1;
            continue;
        }

        if (c == 'u') {
            if (i + 1 >= n) { err = "bad uN token"; return false; }
            char sz = pattern[i + 1];
            CompiledSig::Op op;
            if (sz == '4') op.kind = CompiledSig::Op::Kind::ReadU32;
            else if (sz == '1') op.kind = CompiledSig::Op::Kind::ReadU8;
            else { err = "unsupported uN size"; return false; }
            out.ops.push_back(op);
            i += 2;
            continue;
        }

        // hex byte
        if (i + 1 >= n) { err = "truncated hex byte"; return false; }
        uint8_t hi = 0, lo = 0;
        if (!hex_nibble(pattern[i], hi) || !hex_nibble(pattern[i + 1], lo)) {
            err = std::string("bad token near '") + pattern.substr(i, 2) + "'";
            return false;
        }
        CompiledSig::Op op;
        op.kind = CompiledSig::Op::Kind::Byte;
        op.byte = static_cast<uint8_t>((hi << 4) | lo);
        out.ops.push_back(op);
        i += 2;
    }

    if (out.ops.empty()) { err = "empty pattern"; return false; }
    return true;
}

namespace {

bool try_match(const uint8_t* buf, size_t size, size_t offset, uint32_t section_rva,
               const CompiledSig& sig, SigMatch& out) {
    std::vector<uint32_t> saves;
    size_t i = offset;
    for (const auto& op : sig.ops) {
        switch (op.kind) {
        case CompiledSig::Op::Kind::Byte:
            if (i >= size || buf[i] != op.byte) return false;
            ++i;
            break;
        case CompiledSig::Op::Kind::Wildcard:
            if (i >= size) return false;
            ++i;
            break;
        case CompiledSig::Op::Kind::Skip:
            if (op.count > size - i) return false;
            i += op.count;
            break;
        case CompiledSig::Op::Kind::ReadU32: {
            if (4 > size - i) return false;
            uint32_t v;
            std::memcpy(&v, buf + i, 4);
            saves.push_back(v);
            i += 4;
            break;
        }
        case CompiledSig::Op::Kind::ReadU8:
            if (i >= size) return false;
            saves.push_back(buf[i]);
            ++i;
            break;
        case CompiledSig::Op::Kind::RipSave:
        case CompiledSig::Op::Kind::RipNoSave: {
            if (4 > size - i) return false;
            int32_t disp;
            std::memcpy(&disp, buf + i, 4);
            // rva of this operand = section base + offset, where offset points at the operand.
            int64_t target = static_cast<int64_t>(section_rva) + static_cast<int64_t>(i) + 4 + disp;
            i += 4;
            if (target < 0 || target > 0xFFFFFFFFll) return false;
            if (op.kind == CompiledSig::Op::Kind::RipSave) {
                saves.push_back(static_cast<uint32_t>(target + op.count));
            }
            break;
        }
        }
    }
    out.saves = std::move(saves);
    return true;
}

} // namespace

bool find_first(const PeImage& img, const std::string& pattern, SigMatch& match,
                int& match_count, std::string& err) {
    match_count = 0;
    CompiledSig sig;
    if (!compile_sig(pattern, sig, err)) return false;

    // Anchor: first exact byte op in the pattern (wildcards/skips precede it rarely).
    size_t anchor_op = 0;
    size_t anchor_offset = 0;
    for (size_t k = 0; k < sig.ops.size(); ++k) {
        const auto& op = sig.ops[k];
        if (op.kind == CompiledSig::Op::Kind::Byte) { anchor_op = k; break; }
        if (op.kind == CompiledSig::Op::Kind::Skip) anchor_offset += op.count;
        else if (op.kind != CompiledSig::Op::Kind::Wildcard) break; // unusual, just scan linearly
    }
    bool have_anchor = sig.ops[anchor_op].kind == CompiledSig::Op::Kind::Byte;
    uint8_t anchor_byte = have_anchor ? sig.ops[anchor_op].byte : 0;

    // Bytes consumed before the anchor within the pattern.
    size_t prefix = 0;
    for (size_t k = 0; k < anchor_op; ++k) {
        const auto& op = sig.ops[k];
        switch (op.kind) {
        case CompiledSig::Op::Kind::Byte:
        case CompiledSig::Op::Kind::Wildcard:
        case CompiledSig::Op::Kind::ReadU8: prefix += 1; break;
        case CompiledSig::Op::Kind::Skip: prefix += op.count; break;
        case CompiledSig::Op::Kind::ReadU32:
        case CompiledSig::Op::Kind::RipSave:
        case CompiledSig::Op::Kind::RipNoSave: prefix += 4; break;
        }
    }

    for (const auto& sec : img.sections()) {
        if (sig.executable_sections_only && !sec.executable()) continue;
        const uint8_t* buf = img.at(sec.virtual_address, 0);
        if (!buf) continue;
        size_t size = 0;
        for (const auto& s : img.sections()) {
            if (&s == &sec) { size = s.virtual_size ? s.virtual_size : s.raw_size; break; }
        }
        if (size < prefix) continue;

        size_t scan_end = size;
        if (!have_anchor) {
            for (size_t off = 0; off + prefix < scan_end; ++off) {
                SigMatch m;
                if (try_match(buf, size, off, sec.virtual_address, sig, m)) {
                    if (++match_count == 1) {
                        m.rva = sec.virtual_address + static_cast<uint32_t>(off);
                        match = std::move(m);
                    }
                }
            }
            continue;
        }

        const uint8_t* base = buf;
        size_t pos = prefix;
        while (pos < scan_end) {
            const uint8_t* hit = static_cast<const uint8_t*>(
                std::memchr(base + pos, anchor_byte, scan_end - pos));
            if (!hit) break;
            size_t off = static_cast<size_t>(hit - base) - prefix;
            SigMatch m;
            if (try_match(buf, size, off, sec.virtual_address, sig, m)) {
                if (++match_count == 1) {
                    m.rva = sec.virtual_address + static_cast<uint32_t>(off);
                    match = std::move(m);
                }
            }
            pos = static_cast<size_t>(hit - base) + 1;
        }
    }

    if (match_count == 0) {
        err = "pattern not found";
        return false;
    }
    return true;
}

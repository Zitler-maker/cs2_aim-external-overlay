#include "pe.h"

#include <windows.h>

#include <cstring>

#include "json.h"
#include "log.h"

namespace {

constexpr size_t kPeHeaderScanLimit = 0x1000;
constexpr uint16_t kMaxSections = 96;
constexpr uint16_t kPe32PlusMagic = 0x20B;

uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

} // namespace

bool PeImage::load_file(const std::wstring& path, std::string& err) {
    buffer_.clear();
    sections_.clear();
    valid_ = false;
    in_memory_ = false;

    std::string bytes;
    if (!json::read_file_w(path, bytes)) {
        err = "cannot read file";
        return false;
    }
    buffer_.assign(bytes.begin(), bytes.end());
    if (!parse_headers(err)) return false;
    valid_ = true;
    return true;
}

bool PeImage::map_memory(const uint8_t* data, size_t size, std::string& err) {
    buffer_.assign(data, data + size);
    sections_.clear();
    valid_ = false;
    in_memory_ = true;
    if (!parse_headers(err)) return false;
    valid_ = true;
    return true;
}

bool PeImage::parse_headers(std::string& err) {
    if (buffer_.size() < 0x40) { err = "file too small for DOS header"; return false; }
    const uint8_t* base = buffer_.data();
    const size_t total = buffer_.size();

    if (base[0] != 'M' || base[1] != 'Z') { err = "missing MZ signature"; return false; }
    uint32_t e_lfanew = rd32(base + 0x3C);
    if (e_lfanew > kPeHeaderScanLimit || e_lfanew + 24 > total) {
        err = "invalid e_lfanew";
        return false;
    }
    const uint8_t* nt = base + e_lfanew;
    if (rd32(nt) != 0x00004550u) { err = "missing PE signature"; return false; }

    const uint8_t* file_header = nt + 4;
    uint16_t num_sections = rd16(file_header + 2);
    uint16_t optional_size = rd16(file_header + 16);
    if (num_sections == 0 || num_sections > kMaxSections) { err = "bad section count"; return false; }
    if (optional_size < 112) { err = "optional header too small"; return false; }

    const uint8_t* opt = file_header + 20;
    if (static_cast<size_t>(opt - base) + optional_size > total) {
        err = "optional header out of bounds";
        return false;
    }
    if (rd16(opt) != kPe32PlusMagic) { err = "not a PE32+ image"; return false; }

    image_base_ = rd64(opt + 24);
    size_of_image_ = rd32(opt + 56);
    if (size_of_image_ == 0 || size_of_image_ > 0x40000000u) {
        err = "implausible SizeOfImage";
        return false;
    }

    const uint8_t* sec = opt + optional_size;
    if (static_cast<size_t>(sec - base) + static_cast<size_t>(num_sections) * 40 > total) {
        err = "section table out of bounds";
        return false;
    }

    sections_.reserve(num_sections);
    for (uint16_t i = 0; i < num_sections; ++i) {
        const uint8_t* s = sec + i * 40;
        PeSection ps;
        char name[9]{};
        std::memcpy(name, s, 8);
        ps.name = name;
        ps.virtual_size = rd32(s + 8);
        ps.virtual_address = rd32(s + 12);
        ps.raw_size = rd32(s + 16);
        ps.raw_offset = rd32(s + 20);
        ps.characteristics = rd32(s + 36);

        if (ps.raw_size != 0) {
            if (ps.raw_offset > total || ps.raw_size > total - ps.raw_offset) {
                err = "section '" + ps.name + "' raw range out of bounds";
                return false;
            }
        }

        if (in_memory_) {
            // In a mapped image the section body sits at its RVA.
            size_t body = ps.virtual_address;
            size_t avail = body <= total ? total - body : 0;
            ps.buffer_offset_ = body;
            ps.buffer_size_ = ps.virtual_size < avail ? ps.virtual_size : avail;
        } else {
            ps.buffer_offset_ = ps.raw_offset;
            ps.buffer_size_ = ps.raw_size;
        }
        sections_.push_back(std::move(ps));
    }
    return true;
}

const uint8_t* PeImage::at(uint32_t rva, size_t size) const {
    for (const auto& s : sections_) {
        if (rva < s.virtual_address) continue;
        size_t delta = rva - s.virtual_address;
        if (delta >= s.buffer_size_) continue;
        if (size > s.buffer_size_ - delta) return nullptr;
        return buffer_.data() + s.buffer_offset_ + delta;
    }
    return nullptr;
}

const PeSection* PeImage::section_of(uint32_t rva) const {
    for (const auto& s : sections_) {
        uint32_t span = s.virtual_size ? s.virtual_size : s.raw_size;
        if (rva >= s.virtual_address && rva < s.virtual_address + span) return &s;
    }
    return nullptr;
}

std::string PeImage::section_name_of(uint32_t rva) const {
    const PeSection* s = section_of(rva);
    return s ? s->name : std::string();
}

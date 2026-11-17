#pragma once
// PE64 image reader for both on-disk files and in-memory module images.
// All external sizes are validated against the buffer before use.

#include <cstdint>
#include <string>
#include <vector>

struct PeSection {
    std::string name;
    uint32_t virtual_address = 0;
    uint32_t virtual_size = 0;
    uint32_t raw_offset = 0;
    uint32_t raw_size = 0;
    uint32_t characteristics = 0;
    bool executable() const { return (characteristics & 0x20000000u) != 0; }

private:
    friend class PeImage;
    size_t buffer_offset_ = 0; // byte offset of the section body inside the buffer
    size_t buffer_size_ = 0;   // usable bytes of the section body inside the buffer
};

class PeImage {
public:
    // Load a PE64 from disk and lay it out like an in-memory image would be seen by a scanner.
    bool load_file(const std::wstring& path, std::string& err);

    // Wrap an already loaded in-memory image (RVA == buffer offset).
    bool map_memory(const uint8_t* data, size_t size, std::string& err);

    bool valid() const { return valid_; }
    uint64_t image_base() const { return image_base_; }
    uint32_t size_of_image() const { return size_of_image_; }
    const std::vector<PeSection>& sections() const { return sections_; }

    // Returns a pointer to `size` bytes at `rva`, or nullptr if out of bounds.
    const uint8_t* at(uint32_t rva, size_t size) const;

    // Section that contains `rva`, or nullptr.
    const PeSection* section_of(uint32_t rva) const;
    std::string section_name_of(uint32_t rva) const;

    const uint8_t* buffer() const { return buffer_.data(); }
    size_t buffer_size() const { return buffer_.size(); }

private:
    bool parse_headers(std::string& err);

    std::vector<uint8_t> buffer_;
    std::vector<PeSection> sections_;
    bool in_memory_ = false;
    bool valid_ = false;
    uint64_t image_base_ = 0;
    uint32_t size_of_image_ = 0;
};

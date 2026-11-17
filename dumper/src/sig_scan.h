#pragma once
// AOB pattern scanner with pelite-style syntax:
//   48 8b 05      exact bytes
//   ? / ??        wildcard byte
//   [N]           skip N bytes
//   u4 / u1       read little-endian value, save it, advance
//   ${'}          RIP-relative disp32, save resolved target RVA
//   ${}           RIP-relative disp32, do not save
//   ${[N]'}       RIP-relative disp32 plus N, save

#include <cstdint>
#include <string>
#include <vector>

class PeImage;

struct SigMatch {
    uint32_t rva = 0;              // start of the pattern inside the module
    std::vector<uint32_t> saves;   // captured values in order of appearance
};

struct CompiledSig {
    struct Op {
        enum class Kind : uint8_t { Byte, Wildcard, Skip, RipSave, RipNoSave, ReadU32, ReadU8 };
        Kind kind = Kind::Byte;
        uint8_t byte = 0;
        uint32_t count = 0; // Skip length or RIP extra addend
    };
    std::vector<Op> ops;
    bool executable_sections_only = true;
};

bool compile_sig(const std::string& pattern, CompiledSig& out, std::string& err);

// Scans executable sections. Returns true if at least one match was found.
bool find_first(const PeImage& img, const std::string& pattern, SigMatch& match,
                int& match_count, std::string& err);

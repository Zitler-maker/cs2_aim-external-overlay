#pragma once
// SHA-256 for the game_build tag.

#include <cstddef>
#include <cstdint>
#include <string>

std::string sha256_hex(const uint8_t* data, size_t size);
std::string sha256_hex(const std::string& data);

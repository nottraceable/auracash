#pragma once
#include <cstdint>
#include <vector>
#include <string>

std::vector<uint8_t> Keccak256(const std::vector<uint8_t>& data);
std::vector<uint8_t> Keccak256(const uint8_t* data, size_t len);


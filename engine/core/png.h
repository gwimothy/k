#pragma once
// Minimal PNG encoder (uncompressed deflate blocks). Good enough for screenshots and
// test output without pulling in a library.

#include <cstdint>
#include <span>
#include <vector>

namespace k::png {

// `pixels` is tightly packed, top row first. channels = 3 (RGB) or 4 (RGBA).
std::vector<uint8_t> encode(std::span<const uint8_t> pixels, int width, int height, int channels);

uint32_t crc32(std::span<const uint8_t> data, uint32_t crc = 0);

} // namespace k::png

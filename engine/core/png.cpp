#include "core/png.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace k::png {
namespace {

const std::array<uint32_t, 256>& crcTable() {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t>& out, std::string_view type, std::span<const uint8_t> data) {
    putU32(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), data.begin(), data.end());
    putU32(out, crc32(std::span(out).subspan(start)));
}

} // namespace

uint32_t crc32(std::span<const uint8_t> data, uint32_t crc) {
    crc = ~crc;
    for (uint8_t b : data) crc = crcTable()[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::vector<uint8_t> encode(std::span<const uint8_t> pixels, int width, int height, int channels) {
    const size_t stride = static_cast<size_t>(width) * static_cast<size_t>(channels);

    // Raw scanlines, each prefixed with filter type 0.
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        auto row = pixels.subspan(static_cast<size_t>(y) * stride, stride);
        raw.insert(raw.end(), row.begin(), row.end());
    }

    // zlib stream with stored (uncompressed) deflate blocks of up to 65535 bytes.
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len));
        z.push_back(static_cast<uint8_t>(len >> 8));
        z.push_back(static_cast<uint8_t>(~len));
        z.push_back(static_cast<uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    putU32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    putU32(ihdr, static_cast<uint32_t>(width));
    putU32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);                      // bit depth
    ihdr.push_back(channels == 4 ? 6 : 2);  // color type: RGBA or RGB
    ihdr.push_back(0);                      // compression
    ihdr.push_back(0);                      // filter
    ihdr.push_back(0);                      // interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

} // namespace k::png

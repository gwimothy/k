#include "core/png.h"

#include <doctest/doctest.h>

#include <string>

using namespace k;

TEST_CASE("CRC32 matches the standard check value") {
    std::string s = "123456789";
    CHECK(png::crc32({reinterpret_cast<const uint8_t*>(s.data()), s.size()}) == 0xCBF43926u);
}

TEST_CASE("PNG encoder writes a valid header") {
    std::vector<uint8_t> px(4 * 3 * 3, 128);
    auto out = png::encode(px, 4, 3, 3);
    REQUIRE(out.size() > 33);
    CHECK(out[0] == 0x89);
    CHECK(out[1] == 'P');
    CHECK(out[12] == 'I');
    CHECK(out[15] == 'R');
    CHECK(out[19] == 4); // width
    CHECK(out[23] == 3); // height
}

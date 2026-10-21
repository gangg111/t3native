// gd::gunzip (gd/io.h): gzip members made by Python's zlib (gunzip_vectors.h) inflate to the
// pattern they were made from, whatever their blocks; a damaged member is refused.
#include <filesystem>
#include <vector>

#include "gd/io.h"
#include "gunzip_vectors.h"
#include "support/test_suite.h"

namespace {

// The bytes the vectors were made from (the generator's own pattern()).
std::vector<uint8_t> pattern() {
    std::vector<uint8_t> out;
    for (int i = 0; i < 3000; ++i) out.push_back(static_cast<uint8_t>((i * 7 + (i >> 5)) % 251));
    for (int i = 0; i < 700; ++i) out.insert(out.end(), {'a', 'b', 'c'});
    for (int r = 0; r < 4; ++r)
        for (int b = 0; b < 256; ++b) out.push_back(static_cast<uint8_t>(b));
    out.insert(out.end(), 900, 0);
    return out;
}

gd::PBytes bytes(const std::vector<uint8_t>& v) {
    gd::PBytes b;
    b.vec() = v;
    return b;
}

}  // namespace

T3_TEST(gunzip, every_block_kind) {
    const std::vector<uint8_t> want = pattern();
    // source: Python 3.12 zlib, compresslevel 9 (dynamic), Z_FIXED (fixed), 0 (stored)
    t.expect(gd::gunzip(bytes(gunzip_vectors::DYNAMIC)).vec() == want, "dynamic Huffman blocks");
    t.expect(gd::gunzip(bytes(gunzip_vectors::FIXED)).vec() == want, "fixed Huffman blocks");
    t.expect(gd::gunzip(bytes(gunzip_vectors::STORED)).vec() == want, "stored blocks");
}

T3_TEST(gunzip, damage_is_refused) {
    std::vector<uint8_t> v = gunzip_vectors::DYNAMIC;
    v[v.size() / 2] ^= 0x10;
    bool refused = false;
    try {
        gd::gunzip(bytes(v));
    } catch (const std::exception&) {
        refused = true;
    }
    t.expect(refused, "a flipped bit in the middle: an error, not wrong bytes");
}

// Every converted asset inflates with its CRC and size checked (gunzip throws otherwise).
T3_TEST(gunzip, converted_assets) {
    if (!t.require(t3test::imported("tables/flow.json"))) return;
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(t3test::paths().imported)) {
        if (e.path().extension() != ".gz") continue;
        gd::gunzip(gd::read_file(e.path().string()));
        n += 1;
    }
    t.expect(n > 0, "found " + std::to_string(n) + " gzip assets");
}

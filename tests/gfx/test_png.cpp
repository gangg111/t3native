// The PNG decoder against Pillow on every converted picture (tools/png_digests.py writes the
// reference: size and SHA-256 of the RGBA8 rows per file), and an encode -> decode round trip.
#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "gd/io.h"
#include "gfx/image.h"
#include "support/test_suite.h"

namespace {

// SHA-256 (FIPS 180-4), for comparing pixel buffers with the Python reference.
std::string sha256(const std::vector<uint8_t>& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
        0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
        0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
        0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<uint8_t> m = data;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int k = 7; k >= 0; --k) m.push_back(static_cast<uint8_t>(bits >> (8 * k)));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(m[off + 4 * i]) << 24) | (uint32_t(m[off + 4 * i + 1]) << 16) | (uint32_t(m[off + 4 * i + 2]) << 8) | m[off + 4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
}

}  // namespace

T3_TEST(png, sha256_known_vector) {
    // source: FIPS 180-4 example, SHA-256("abc")
    t.expect_equal(sha256({'a', 'b', 'c'}), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256(abc)");
}

T3_TEST(png, decodes_every_converted_picture_like_pillow) {
    std::string ref = "D:/Tekken 3/_tools/tmp/png_digests.tsv";
    if (!t.require(ref, "run tools/png_digests.py <imported> " + ref + " first")) return;
    std::istringstream lines(gd::read_file(ref).get_string_from_utf8());
    std::string line;
    gd::Int files = 0, pixels = 0;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        std::istringstream cols(line);
        std::string path, digest;
        int w = 0, h = 0;
        std::getline(cols, path, '\t');
        cols >> w >> h >> digest;
        std::string error;
        t3::gfx::Image im = t3::gfx::load_png(t3test::imported(path), &error);
        files += 1;
        pixels += static_cast<gd::Int>(im.width) * im.height;
        if (!t.expect(!im.empty(), path + ": " + error)) return;
        if (!t.expect(im.width == w && im.height == h, path + ": size")) return;
        if (!t.expect(sha256(im.rgba) == digest, path + ": pixels differ from Pillow")) return;
    }
    t.expect(files > 0, "no reference lines");
    t.messages.push_back(gd::str(files) + " files, " + gd::str(pixels) + " pixels equal to Pillow");
}

T3_TEST(png, encode_decode_round_trip) {
    t3::gfx::Image im;
    im.width = 300;   // > 65535 bytes of scanlines: several stored blocks
    im.height = 100;
    im.rgba.resize(static_cast<size_t>(im.width) * im.height * 4);
    for (size_t i = 0; i < im.rgba.size(); ++i) im.rgba[i] = static_cast<uint8_t>(i * 7 + i / 1201);
    std::string error;
    t3::gfx::Image back = t3::gfx::decode_png(t3::gfx::encode_png(im), &error);
    t.expect(error.empty(), error);
    t.expect(back.width == im.width && back.height == im.height && back.rgba == im.rgba, "round trip differs");
}

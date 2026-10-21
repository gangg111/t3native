#include "gfx/image.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "gd/io.h"

namespace t3::gfx {

namespace {

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

uint32_t crc32(const uint8_t* data, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        return t;
    }();
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c;
}

uint8_t paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    return static_cast<uint8_t>(pb <= pc ? b : c);
}

bool fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

}  // namespace

Image decode_png(const gd::PBytes& bytes, std::string* error) {
    static const uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    const uint8_t* p = bytes.data();
    size_t n = static_cast<size_t>(bytes.size());
    Image out;
    if (n < 8 || std::memcmp(p, SIGNATURE, 8) != 0) return fail(error, "not a PNG"), out;
    int width = 0, height = 0, depth = 0, colour = 0, interlace = 0;
    std::vector<uint8_t> palette;      // RGB triples
    std::vector<uint8_t> trns;         // palette alpha
    gd::PBytes idat;
    for (size_t at = 8; at + 12 <= n;) {
        uint32_t len = be32(p + at);
        const uint8_t* type = p + at + 4;
        const uint8_t* body = p + at + 8;
        if (at + 12 + len > n) return fail(error, "truncated chunk"), out;
        if (!std::memcmp(type, "IHDR", 4)) {
            width = static_cast<int>(be32(body));
            height = static_cast<int>(be32(body + 4));
            depth = body[8];
            colour = body[9];
            interlace = body[12];
        } else if (!std::memcmp(type, "PLTE", 4)) {
            palette.assign(body, body + len);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            trns.assign(body, body + len);
        } else if (!std::memcmp(type, "IDAT", 4)) {
            size_t old = static_cast<size_t>(idat.size());
            idat.resize(static_cast<gd::Int>(old + len));
            std::memcpy(idat.data() + old, body, len);
        } else if (!std::memcmp(type, "IEND", 4)) {
            break;
        }
        at += 12 + len;
    }
    if (width <= 0 || height <= 0) return fail(error, "no IHDR"), out;
    if (interlace != 0) return fail(error, "interlaced PNG not supported"), out;
    int channels = colour == 0 ? 1 : colour == 2 ? 3 : colour == 3 ? 1 : colour == 4 ? 2 : colour == 6 ? 4 : 0;
    if (!channels || (depth != 8 && !(colour == 3 && (depth == 1 || depth == 2 || depth == 4))))
        return fail(error, "unsupported PNG format"), out;
    size_t stride = (static_cast<size_t>(width) * channels * depth + 7) / 8;
    size_t bpp = std::max<size_t>(1, static_cast<size_t>(channels) * depth / 8);
    gd::PBytes raw = gd::unzlib(idat, (stride + 1) * static_cast<size_t>(height));
    if (static_cast<size_t>(raw.size()) < (stride + 1) * height) return fail(error, "short image data"), out;
    std::vector<uint8_t> prev(stride, 0), row(stride);
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<size_t>(width) * height * 4);
    const uint8_t* src = raw.data();
    for (int y = 0; y < height; ++y) {
        uint8_t filter = *src++;
        // Each filter's own loop (the first bpp bytes have no left neighbour: a = c = 0).
        uint8_t* r = row.data();
        const uint8_t* up = prev.data();
        switch (filter) {
            case 0: std::memcpy(r, src, stride); break;
            case 1:
                for (size_t i = 0; i < stride; ++i) r[i] = static_cast<uint8_t>(src[i] + (i >= bpp ? r[i - bpp] : 0));
                break;
            case 2:
                for (size_t i = 0; i < stride; ++i) r[i] = static_cast<uint8_t>(src[i] + up[i]);
                break;
            case 3:
                for (size_t i = 0; i < stride; ++i) {
                    int a = i >= bpp ? r[i - bpp] : 0;
                    r[i] = static_cast<uint8_t>(src[i] + ((a + up[i]) >> 1));
                }
                break;
            case 4:
                for (size_t i = 0; i < bpp && i < stride; ++i) r[i] = static_cast<uint8_t>(src[i] + paeth(0, up[i], 0));
                for (size_t i = bpp; i < stride; ++i) r[i] = static_cast<uint8_t>(src[i] + paeth(r[i - bpp], up[i], up[i - bpp]));
                break;
            default: return fail(error, "bad filter"), Image();
        }
        src += stride;
        if (colour == 6) {
            // RGBA rows are the picture's rows.
            std::memcpy(out.pixel(0, y), r, static_cast<size_t>(width) * 4);
            std::swap(prev, row);
            continue;
        }
        for (int x = 0; x < width; ++x) {
            uint8_t* d = out.pixel(x, y);
            switch (colour) {
                case 0: d[0] = d[1] = d[2] = row[x]; d[3] = 255; break;
                case 2: d[0] = row[3 * x]; d[1] = row[3 * x + 1]; d[2] = row[3 * x + 2]; d[3] = 255; break;
                case 4: d[0] = d[1] = d[2] = row[2 * x]; d[3] = row[2 * x + 1]; break;
                case 6: std::memcpy(d, &row[4 * x], 4); break;
                case 3: {
                    int bit = x * depth;
                    int idx = (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1);
                    size_t k = static_cast<size_t>(idx);
                    if (3 * k + 2 < palette.size()) {
                        d[0] = palette[3 * k]; d[1] = palette[3 * k + 1]; d[2] = palette[3 * k + 2];
                    } else {
                        d[0] = d[1] = d[2] = 0;
                    }
                    d[3] = k < trns.size() ? trns[k] : 255;
                    break;
                }
            }
        }
        std::swap(prev, row);
    }
    return out;
}

Image load_png(const std::string& path, std::string* error) {
    gd::PBytes data = gd::read_file(path);
    if (data.is_empty()) {
        if (error) *error = "cannot read " + path;
        return Image();
    }
    return decode_png(data, error);
}

gd::PBytes encode_png(const Image& image) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto put32 = [&](uint32_t v) {
        out.push_back(static_cast<uint8_t>(v >> 24)); out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 8)); out.push_back(static_cast<uint8_t>(v));
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& body) {
        put32(static_cast<uint32_t>(body.size()));
        size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), body.begin(), body.end());
        put32(crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu);
    };
    std::vector<uint8_t> ihdr(13, 0);
    for (int k = 0; k < 4; ++k) {
        ihdr[k] = static_cast<uint8_t>(image.width >> (24 - 8 * k));
        ihdr[4 + k] = static_cast<uint8_t>(image.height >> (24 - 8 * k));
    }
    ihdr[8] = 8;
    ihdr[9] = 6;
    chunk("IHDR", ihdr);
    // Raw scanlines (filter 0) in stored deflate blocks of at most 65535 bytes.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(image.height) * (image.width * 4 + 1));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), image.rgba.begin() + static_cast<size_t>(y) * image.width * 4,
                   image.rgba.begin() + static_cast<size_t>(y + 1) * image.width * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size() || at == 0;) {
        size_t len = std::min<size_t>(65535, raw.size() - at);
        bool last = at + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len)); z.push_back(static_cast<uint8_t>(len >> 8));
        z.push_back(static_cast<uint8_t>(~len)); z.push_back(static_cast<uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
        at += len;
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    uint32_t adler = (b << 16) | a;
    for (int k = 3; k >= 0; --k) z.push_back(static_cast<uint8_t>(adler >> (8 * k)));
    chunk("IDAT", z);
    chunk("IEND", {});
    gd::PBytes bytes;
    bytes.vec() = std::move(out);
    return bytes;
}

bool save_png(const Image& image, const std::string& path) {
    gd::PBytes data = encode_png(image);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, static_cast<size_t>(data.size()), f) == static_cast<size_t>(data.size());
    std::fclose(f);
    return ok;
}

}  // namespace t3::gfx

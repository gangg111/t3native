#pragma once
// RGBA8 images: PNG decoding (the converter's pictures: non-interlaced, 1/2/4/8-bit palette,
// grey, grey+alpha, RGB, RGBA at 8 bits per channel) and PNG encoding (screenshots), on top of
// the runtime's own inflate (gd/io.h): no external library.
#include <cstdint>
#include <string>
#include <vector>

#include "gd/gd.h"

namespace t3::gfx {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width * height * 4, rows top to bottom

    bool empty() const { return width == 0 || height == 0; }
    uint8_t* pixel(int x, int y) { return rgba.data() + (static_cast<size_t>(y) * width + x) * 4; }
    const uint8_t* pixel(int x, int y) const { return rgba.data() + (static_cast<size_t>(y) * width + x) * 4; }
};

// The decoded image, or an empty one (with `error` set) when the bytes are not a supported PNG.
Image decode_png(const gd::PBytes& data, std::string* error = nullptr);
Image load_png(const std::string& path, std::string* error = nullptr);
// A PNG file of the image (stored deflate blocks: large, exact, quick to write).
gd::PBytes encode_png(const Image& image);
bool save_png(const Image& image, const std::string& path);

}  // namespace t3::gfx

#include "content/vram_image.h"

#include "content/texture_packs.h"
#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

gd::Ref<VramImage> VramImage::load_system(const std::string& path) {
    auto v = gd::make<VramImage>();
    v->words = gd::read_file(path);
    if (v->words.size() != WIDTH * HEIGHT * 2) {
        Log::error("VramImage: " + path + " is not a VRAM image");
        v->words.resize(WIDTH * HEIGHT * 2);
    }
    return v;
}

gd::Ref<VramImage> VramImage::duplicate_image() const {
    auto v = gd::make<VramImage>();
    v->words = words.duplicate();
    return v;
}

void VramImage::upload_tim(const gd::PBytes& tim) {
    gd::Int flags = tim.decode_u32(4);
    gd::Int at = 8;
    gd::Int blocks = (flags & 8) != 0 ? 2 : 1;
    for (gd::Int i = 0; i < blocks; ++i) {
        gd::Int length = tim.decode_u32(at);
        if (length >= 12) {
            gd::Int x = tim.decode_u16(at + 4);
            gd::Int y = tim.decode_u16(at + 6);
            gd::Int w = tim.decode_u16(at + 8);
            gd::Int h = tim.decode_u16(at + 10);
            bool wraps = (x & (WIDTH - 1)) + w > WIDTH;
            for (gd::Int row = 0; row < h; ++row) {
                gd::Int src = at + 12 + row * w * 2;
                gd::Int line = ((y + row) & (HEIGHT - 1)) * WIDTH;
                gd::Int n = gd::clampi(tim.size() - src, 0, w * 2);
                if (!wraps) {
                    gd::Int dst = 2 * (line + (x & (WIDTH - 1)));
                    for (gd::Int k = 0; k < n; ++k) words[dst + k] = tim[src + k];
                } else {
                    for (gd::Int k = 0; k < n; ++k) words[2 * (line + ((x + (k >> 1)) & (WIDTH - 1))) + (k & 1)] = tim[src + k];
                }
            }
        }
        at += length;
    }
    _clear();
}

void VramImage::_clear() {
    _cache.clear();
    _generation = TexturePacks::generation;
}

gfx::TextureRef VramImage::_cached(gd::Int key) {
    if (_generation != TexturePacks::generation) _clear();
    auto it = _cache.find(key);
    return it == _cache.end() ? nullptr : it->second;
}

gfx::TextureRef VramImage::_replacement(gd::Int key, gd::Int page_x, gd::Int page_y, gd::Int depth, gd::Int u, gd::Int v,
                                        gd::Int w, gd::Int h, gd::Int clut_x, gd::Int clut_y) {
    if (depth >= 2) return nullptr;
    gfx::TextureRef hd = TexturePacks::vram_sprite(*this, page_x, page_y, gd::Int(4) << depth, u, v, w, h, clut_x, clut_y);
    if (hd && key >= 0) _cache[key] = hd;
    return hd;
}

void VramImage::upload_file(const std::string& path) {
    gd::PBytes data = gd::read_file(path);
    if (data.is_empty()) return;
    gd::Int count = data.decode_u32(0);
    gd::Int at = 4;
    for (gd::Int i = 0; i < count; ++i) {
        gd::Int n = data.decode_u32(at);
        upload_tim(data.slice(at + 4, at + 4 + n));
        at += 4 + n;
    }
}

gd::Int VramImage::_rgba(gd::Int c) {
    if (c == 0) return 0;
    return OPAQUE | _channel(c & 31) | _channel((c >> 5) & 31) << 8 | _channel((c >> 10) & 31) << 16;
}

gfx::TextureRef VramImage::_texels(gd::Int page_x, gd::Int page_y, gd::Int u, gd::Int v, gd::Int w, gd::Int h, gd::Int mask,
                                   gd::Int depth, gd::Int clut_x, gd::Int clut_y) const {
    gfx::Image image;
    image.width = static_cast<int>(w);
    image.height = static_cast<int>(h);
    image.rgba.resize(static_cast<size_t>(w * h * 4));
    std::vector<int32_t> palette;
    if (depth == 0 || depth == 1) {
        palette.resize(static_cast<size_t>(gd::Int(16) << (depth * 4)));
        for (size_t k = 0; k < palette.size(); ++k) palette[k] = static_cast<int32_t>(_rgba(word(clut_x + static_cast<gd::Int>(k), clut_y)));
    }
    auto put = [&](size_t i, gd::Int texel) {
        uint32_t t = static_cast<uint32_t>(texel);
        image.rgba[4 * i] = static_cast<uint8_t>(t);
        image.rgba[4 * i + 1] = static_cast<uint8_t>(t >> 8);
        image.rgba[4 * i + 2] = static_cast<uint8_t>(t >> 16);
        image.rgba[4 * i + 3] = static_cast<uint8_t>(t >> 24);
    };
    size_t i = 0;
    for (gd::Int y = 0; y < h; ++y) {
        gd::Int row = ((page_y + ((v + y) & mask)) & 511) * WIDTH;
        switch (depth) {
            case 0:
                for (gd::Int x = 0; x < w; ++x) {
                    gd::Int tx = (u + x) & mask;
                    gd::Int wd = words.decode_u16(2 * (row + ((page_x + (tx >> 2)) & 1023)));
                    put(i++, palette[static_cast<size_t>((wd >> ((tx & 3) * 4)) & 0xF)]);
                }
                break;
            case 1:
                for (gd::Int x = 0; x < w; ++x) {
                    gd::Int tx = (u + x) & mask;
                    gd::Int wd = words.decode_u16(2 * (row + ((page_x + (tx >> 1)) & 1023)));
                    put(i++, palette[static_cast<size_t>((wd >> ((tx & 1) * 8)) & 0xFF)]);
                }
                break;
            default:
                for (gd::Int x = 0; x < w; ++x) put(i++, _rgba(words.decode_u16(2 * (row + ((page_x + ((u + x) & mask)) & 1023)))));
                break;
        }
    }
    return std::make_shared<gfx::Texture>(image);
}

gfx::TextureRef VramImage::sprite(gd::Int u, gd::Int v, gd::Int w, gd::Int h, gd::Int clut, gd::Int tpage) {
    if (w <= 0 || h <= 0 || w > MAX_SIZE_X || h > MAX_SIZE_Y) return nullptr;   // the GPU draws nothing for such a rectangle
    // Every input bit the sprite depends on: u, v 8 bits, w - 1 10, h - 1 9, clut 15, tpage 7.
    gd::Int key = (u & 0xFF) | (v & 0xFF) << 8 | (w - 1) << 16 | (h - 1) << 26 | (clut & 0x7FFF) << 35 | (tpage & 0x1F) << 50 |
                  ((tpage >> 7) & 3) << 55;
    if (gfx::TextureRef cached = _cached(key)) return cached;
    gd::Int page_x = (tpage & 0xF) * 64;
    gd::Int page_y = ((tpage >> 4) & 1) * 256;
    gd::Int depth = (tpage >> 7) & 3;
    gd::Int clut_x = (clut & 0x3F) * 16;
    gd::Int clut_y = (clut >> 6) & 0x1FF;
    if (gfx::TextureRef hd = _replacement(key, page_x, page_y, depth, u & 0xFF, v & 0xFF, w, h, clut_x, clut_y)) return hd;
    gfx::TextureRef tex = _texels(page_x, page_y, u, v, w, h, 0xFF, depth, clut_x, clut_y);
    _cache[key] = tex;
    return tex;
}

gfx::TextureRef VramImage::picture(gd::Int vx, gd::Int vy, gd::Int w, gd::Int h, gd::Int clut, gd::Int depth) {
    if (w <= 0 || h <= 0 || w > MAX_SIZE_X || h > MAX_SIZE_Y) return nullptr;
    // Keyed apart from the sprites (bit 62) while every input fits its field, else not cached.
    gd::Int key = -1;
    if (vx >= 0 && vx < 0x1000 && vy >= 0 && vy < 0x1000 && depth >= 0 && depth < 4)
        key = PICTURE_KEY | vx | vy << 12 | (w - 1) << 24 | (h - 1) << 34 | (clut & 0x7FFF) << 43 | depth << 58;
    if (key >= 0)
        if (gfx::TextureRef cached = _cached(key)) return cached;
    gd::Int clut_x = (clut & 0x3F) * 16;
    gd::Int clut_y = (clut >> 6) & 0x1FF;
    gd::Int page_x = vx & ~gd::Int(63);
    gd::Int page_y = vy & ~gd::Int(255);
    if (gfx::TextureRef hd = _replacement(key, page_x, page_y, depth, (vx - page_x) * gd::shr(4, depth), vy - page_y, w, h, clut_x, clut_y))
        return hd;
    gfx::TextureRef tex = _texels(vx, vy, 0, 0, w, h, -1, depth, clut_x, clut_y);
    if (key >= 0) _cache[key] = tex;
    return tex;
}

}  // namespace t3

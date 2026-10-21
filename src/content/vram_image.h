#pragma once
// remake/content/vram_image.gd: the PlayStation's VRAM (1024 x 512 words) for the 2D screens: the
// resident system textures, then the screen's own pictures uploaded in the game's order (LoadImage of
// TIM blocks). Sprites are cut out by texture page, CLUT and texel rectangle, as the GPU reads them:
// 4- and 8-bit texels through a CLUT, 15-bit direct; the colour 0x0000 is transparent. Pictures are
// cached.
#include <string>
#include <unordered_map>

#include "gd/gd.h"
#include "gfx/texture.h"

namespace t3 {

class VramImage {
public:
    static constexpr gd::Int WIDTH = 1024;
    static constexpr gd::Int HEIGHT = 512;
    static constexpr gd::Int MAX_SIZE_X = 1024, MAX_SIZE_Y = 512;   // the largest rectangle the GPU draws
    static constexpr gd::Int OPAQUE = -0x1000000;                     // alpha 0xFF of an RGBA8 texel as a signed 32-bit int
    static constexpr gd::Int PICTURE_KEY = gd::Int(1) << 62;          // the picture cache keys' flag

    gd::PBytes words;   // little-endian u16 per VRAM word

    static gd::Ref<VramImage> load_system(const std::string& path);
    gd::Ref<VramImage> duplicate_image() const;
    gd::Int word(gd::Int x, gd::Int y) const { return words.decode_u16(2 * ((y & 511) * WIDTH + (x & 1023))); }
    // LoadImage of a TIM's CLUT and pixel blocks; a rectangle past the VRAM's edges wraps around, as
    // the GPU's transfer does.
    void upload_tim(const gd::PBytes& tim);
    // Every TIM of a `.tims` file (u32 count, then u32 length and bytes per TIM), in order.
    void upload_file(const std::string& path);
    // A w x h sprite from texel (u, v) of texture page `tpage` through CLUT `clut` (the GPU's packet
    // words: tpage bits 0-4 page x/y, 7-8 depth; clut bits 0-5 x/16, 6-14 y).
    gfx::TextureRef sprite(gd::Int u, gd::Int v, gd::Int w, gd::Int h, gd::Int clut, gd::Int tpage);
    // FUN_8004DE84's picture: w x h texels from VRAM word (vx, vy) at depth 0 (4-bit), 1 (8-bit) or
    // 2 (15-bit), through CLUT `clut` for the indexed depths.
    gfx::TextureRef picture(gd::Int vx, gd::Int vy, gd::Int w, gd::Int h, gd::Int clut, gd::Int depth);

    // A 15-bit colour as RGBA8 bytes in a little-endian int (0x0000 transparent). Each 5-bit channel
    // fills 8 bits with its top bits repeated below (31 -> 255), as the converter's pictures.
    static gd::Int _rgba(gd::Int c);
    static gd::Int _channel(gd::Int v) { return (v << 3) | (v >> 2); }

private:
    void _clear();
    gfx::TextureRef _cached(gd::Int key);
    gfx::TextureRef _replacement(gd::Int key, gd::Int page_x, gd::Int page_y, gd::Int depth, gd::Int u, gd::Int v, gd::Int w,
                                 gd::Int h, gd::Int clut_x, gd::Int clut_y);
    gfx::TextureRef _texels(gd::Int page_x, gd::Int page_y, gd::Int u, gd::Int v, gd::Int w, gd::Int h, gd::Int mask,
                            gd::Int depth, gd::Int clut_x, gd::Int clut_y) const;

    std::unordered_map<gd::Int, gfx::TextureRef> _cache;   // packed sprite / picture key -> texture
    gd::Int _generation = -1;                               // the TexturePacks generation the caches belong to
};

}  // namespace t3

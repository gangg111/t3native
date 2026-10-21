#pragma once
// remake/content/texture_packs.gd: the converted pictures by path, cached; a chosen replacement
// pack's pictures in their place.
// TODO: pack replacements (replacement_file, vram_sprite and the VRAM checks) arrive with the
// Settings port (platform/settings.gd: the chosen pack); until then `chosen()` is "" and the
// originals are drawn, exactly as the remake does without a pack.
#include <string>

#include "gfx/texture.h"

namespace t3 {

class VramImage;

class TexturePacks {
public:
    static inline gd::Int generation = 0;   // changes with the chosen pack (`clear`)

    // The texture at `path`, or the chosen pack's replacement of it (null when it cannot be read).
    static gfx::TextureRef texture(const std::string& path);
    // The pack in use ("" none).
    static std::string chosen();
    // The chosen pack's replacement of a VRAM sprite, or null.
    static gfx::TextureRef vram_sprite(const VramImage& vram, gd::Int page_x, gd::Int page_y, gd::Int depth, gd::Int u,
                                       gd::Int v, gd::Int w, gd::Int h, gd::Int clut_x, gd::Int clut_y);
    static void clear();
};

}  // namespace t3

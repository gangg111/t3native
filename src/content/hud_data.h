#pragma once
// remake/content/hud_data.gd: the fight HUD's sprites (`imported/hud/`, tools/remake_import/hud.py):
// bar caps and fills, round marks, timer digits, name plates and names, and the text engine's glyph
// sheets per font and colour. Textures load when first used.
#include <string>

#include "gd/json.h"
#include "gfx/texture.h"

namespace t3 {

class HudData {
public:
    class FontInfo {
    public:
        gd::Int advance = 0;
        gd::Int line = 0;
        gd::Int width = 0;
        gd::Int height = 0;
        gd::Int first = 0;
        gd::Int columns = 0;
    };

    std::string directory;
    gd::Json names;                                  // costume slot (string) -> {file, width, plate}
    gd::Dict<gd::Int, gd::Ref<FontInfo>> fonts;      // font -> FontInfo

    static gd::Ref<HudData> load_from(const std::string& dir);
    // A picture of the HUD, or the chosen texture pack's replacement of it.
    gfx::TextureRef texture(const std::string& file) const;
    // The name sprite of a costume slot and its width, or null.
    gfx::TextureRef name_texture(gd::Int slot) const;
    gd::Int name_width(gd::Int slot) const;
    gfx::TextureRef plate_texture(gd::Int slot) const;
    gfx::TextureRef glyphs(gd::Int font, gd::Int colour) const;
    // `hud.fonts.get(font)`: null when the font is unknown.
    gd::Ref<FontInfo> font(gd::Int index) const { return fonts.get(index, nullptr); }
};

}  // namespace t3

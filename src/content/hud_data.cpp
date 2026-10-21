#include "content/hud_data.h"

#include "content/json_file.h"
#include "content/texture_packs.h"
#include "gd/io.h"

namespace t3 {

gd::Ref<HudData> HudData::load_from(const std::string& dir) {
    auto h = gd::make<HudData>();
    h->directory = dir;
    gd::Json data = JsonFile::read(gd::path_join(dir, "hud.json"));
    h->names = data.get("names");
    for (const auto& [key, f] : data.get("fonts").members()) {
        auto info = gd::make<FontInfo>();
        info->advance = JsonFile::number(f["advance"]);
        info->line = JsonFile::number(f["line"]);
        info->width = JsonFile::number(f["width"]);
        info->height = JsonFile::number(f["height"]);
        info->first = JsonFile::number(f["first"]);
        info->columns = JsonFile::number(f["columns"]);
        h->fonts.set(std::stoll(key), info);
    }
    return h;
}

gfx::TextureRef HudData::texture(const std::string& file) const { return TexturePacks::texture(gd::path_join(directory, file)); }

gfx::TextureRef HudData::name_texture(gd::Int slot) const {
    const gd::Json& entry = names.get(gd::str(slot));
    return entry.is_object() ? texture(entry["file"].str()) : nullptr;
}

gd::Int HudData::name_width(gd::Int slot) const {
    const gd::Json& entry = names.get(gd::str(slot));
    return entry.is_object() ? JsonFile::number(entry["width"]) : 0;
}

gfx::TextureRef HudData::plate_texture(gd::Int slot) const {
    const gd::Json& entry = names.get(gd::str(slot));
    return entry.is_object() ? texture(entry["plate"].str()) : nullptr;
}

gfx::TextureRef HudData::glyphs(gd::Int font, gd::Int colour) const {
    return texture("font_" + gd::str(font) + "_" + gd::str(colour) + ".png");
}

}  // namespace t3

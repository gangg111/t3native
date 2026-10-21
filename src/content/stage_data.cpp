// remake/content/stage_data.gd
#include "content/stage_data.h"

#include <cstring>

#include "content/arcade_stage_data.h"
#include "content/json_file.h"
#include "gd/io.h"

namespace t3 {

namespace {
// Typed reads of `data.get(key, fallback)` (Variant float -> int truncates).
gd::Int int_or(const gd::Json& d, const std::string& key, gd::Int fallback) {
    return d.has(key) ? d[key].to_int() : fallback;
}
std::string str_or(const gd::Json& d, const std::string& key, const std::string& fallback) {
    return d.has(key) && d[key].is_string() ? d[key].str() : fallback;
}
gd::Json object_or_empty(const gd::Json& d, const std::string& key) {
    return d.has(key) ? d[key] : gd::Json::parse("{}");
}
// PackedByteArray.to_float32_array().
gd::Packed<float> to_float32_array(const gd::PBytes& b) {
    gd::Packed<float> out;
    out.resize(b.size() / 4);
    if (out.size() > 0) std::memcpy(out.data(), b.data(), static_cast<size_t>(out.size()) * 4);
    return out;
}
}  // namespace

gd::Ref<StageData> StageData::load_from(const std::string& dir) {
    gd::Json data = JsonFile::read(dir + "/stage.json");
    auto s = gd::make<StageData>();
    s->directory = dir;
    s->number = int_or(data, "stage", -1);
    s->letter = str_or(data, "letter", "");
    s->cell = int_or(data, "cell", 0);
    s->panorama_height = int_or(data, "panorama_height", 0);
    s->floor_kind = int_or(data, "floor_kind", 0);
    s->floor_shade = JsonFile::number(data.get("floor_shade"));   // default 0: number(null) is 0
    s->floor_fade = JsonFile::number(data.get("floor_fade"));
    s->floor_tile_size = int_or(data, "floor_tile_size", 1800);
    for (const gd::Json& tile : data.get("floor_tiles").items()) {
        s->floor_tiles.append(tile);
    }
    s->floor_pattern = str_or(data, "floor_pattern", "");
    s->floor_pattern_smooth = str_or(data, "floor_pattern_smooth", "");
    s->vertex_floats = int_or(data, "vertex_floats", 0);
    s->panorama_extent = data.has("panorama_extent") ? JsonFile::ints(data["panorama_extent"]) : gd::PI32{0, 0, 0, 0};
    if (gd::file_exists(dir + "/panorama.bin")) {
        s->panorama = to_float32_array(gd::read_file(dir + "/panorama.bin"));
    }
    s->tile_map = object_or_empty(data, "tile_map");
    s->item = object_or_empty(data, "item");
    gd::Json light = object_or_empty(data, "lighting");
    s->ambient = int_or(light, "ambient", 128);
    s->ambient_level = int_or(light, "ambient_level", s->ambient << 4);
    gd::PI32 base = light.has("base_rgb") ? JsonFile::ints(light["base_rgb"])
                                          : gd::PI32{BASE_NEUTRAL, BASE_NEUTRAL, BASE_NEUTRAL};
    // Vector3(base) / BASE_NEUTRAL in float32.
    s->base_colour = {static_cast<float>(gd::Int(base[0])) / static_cast<float>(BASE_NEUTRAL),
                      static_cast<float>(gd::Int(base[1])) / static_cast<float>(BASE_NEUTRAL),
                      static_cast<float>(gd::Int(base[2])) / static_cast<float>(BASE_NEUTRAL)};
    s->light_colour = light.has("light_rgb") ? _colour(light["light_rgb"]) : _colour(gd::Json::parse("[200, 200, 200]"));
    s->light_pitch = int_or(light, "light_pitch", 512);
    s->light_yaw = int_or(light, "light_yaw", 2560);
    gd::Json cover = object_or_empty(data, "coverage");
    s->coverage = JsonFile::ints(cover.get("elevation"));
    s->coverage_eye = cover.has("eye") ? JsonFile::ints(cover["eye"]) : gd::PI32{0, 0, 0};
    s->arcade = ArcadeStageData::load_from(dir);
    return s;
}

std::array<float, 4> StageData::_colour(const gd::Json& value) {
    gd::PI32 c = JsonFile::ints(value);
    // Color8(r, g, b): channels / 255, alpha 1.
    return {static_cast<float>(gd::Int(c[0])) / 255.0f, static_cast<float>(gd::Int(c[1])) / 255.0f,
            static_cast<float>(gd::Int(c[2])) / 255.0f, 1.0f};
}

}  // namespace t3

// remake/content/arcade_stage_data.gd
#include "content/arcade_stage_data.h"

#include <cstring>

#include "content/json_file.h"
#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

namespace {
// Typed reads of `data.get(key, fallback)` (Variant float -> int truncates).
gd::Int int_or(const gd::Json& d, const std::string& key, gd::Int fallback) {
    return d.has(key) ? d[key].to_int() : fallback;
}
std::string str_or(const gd::Json& d, const std::string& key, const std::string& fallback) {
    return d.has(key) && d[key].is_string() ? d[key].str() : fallback;
}
// `JsonFile.number(d.get(key, fallback))`.
gd::Int number_or(const gd::Json& d, const std::string& key, gd::Int fallback) {
    return d.has(key) ? JsonFile::number(d[key]) : fallback;
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

gd::Ref<ArcadeStageData> ArcadeStageData::load_from(const std::string& stage_dir) {
    std::string dir = gd::path_join(stage_dir, "arcade");
    if (!gd::file_exists(gd::path_join(dir, FILE))) {
        return nullptr;
    }
    gd::Json data = JsonFile::read(gd::path_join(dir, FILE));
    auto a = gd::make<ArcadeStageData>();
    a->directory = dir;
    a->vertex_floats = int_or(data, "vertex_floats", 0);
    if (a->vertex_floats <= 0) {
        Log::warning("ArcadeStageData: " + dir + ": no vertex_floats; the PlayStation backdrop is drawn");
        return nullptr;
    }
    a->scene_texture = str_or(data, "scene_texture", "");
    a->scene_scale = static_cast<double>(number_or(data, "scene_scale", 10));
    a->scene_y_scale = data.has("scene_y_scale") ? data["scene_y_scale"].num() : 1.0;
    a->scene_height = number_or(data, "scene_height", 0);
    a->scene_extent = data.has("scene_extent") ? JsonFile::ints(data["scene_extent"]) : gd::PI32{0, 0, 0, 0};
    a->sky = object_or_empty(data, "sky");
    const gd::Json& animation = data.get("scene_animation");
    if (animation.is_object()) {
        const gd::Json& d = animation;
        a->scene_animation_frames = number_or(d, "frames", 0);
        a->scene_animation_period = number_or(d, "period", 1);
        // StageView.step divides by both: without them the scene stays as loaded.
        if (a->scene_animation_frames > 0 && a->scene_animation_period > 0) {
            a->scene_animation_rect = JsonFile::ints(d.get("rect"));
            a->scene_animation_texture = str_or(d, "texture", "");
        } else {
            Log::warning("ArcadeStageData: " + dir + ": scene animation without frames or period");
        }
    }
    gd::Json floor = object_or_empty(data, "floor");
    a->floor_kind = number_or(floor, "kind", 0);
    a->floor_shade = number_or(floor, "shade", 0);
    a->floor_fade = number_or(floor, "fade", 0);
    a->floor_tile_size = number_or(floor, "tile_size", 2500);
    a->floor_pattern = str_or(floor, "pattern", "");
    a->floor_pattern_smooth = str_or(floor, "pattern_smooth", "");
    const gd::Json& props = data.get("props");
    if (props.is_object()) {
        a->props_kind = str_or(props, "kind", "");
        for (const gd::Json& file : props.get("meshes").items()) {
            a->props_meshes.append(to_float32_array(gd::read_file(gd::path_join(dir, file.str()))));
        }
    }
    std::string mesh_path = gd::path_join(dir, "scene.bin");
    if (gd::file_exists(mesh_path)) {
        a->scene = to_float32_array(gd::read_file(mesh_path));
    }
    return a;
}

}  // namespace t3

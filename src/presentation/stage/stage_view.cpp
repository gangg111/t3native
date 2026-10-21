// remake/presentation/stage/stage_view.gd
// Draws a stage: the distant panorama and the tiled floor (see the header).
#include "presentation/stage/stage_view.h"

#include <set>

#include "content/arcade_stage_data.h"
#include "content/json_file.h"
#include "content/stage_data.h"
#include "content/texture_packs.h"
#include "gd/io.h"
#include "gfx/image.h"
#include "platform/log.h"
#include "presentation/render_quality.h"
#include "presentation/world_space.h"
#include "platform/settings.h"

namespace t3 {

namespace {

// String.get_base_dir(): the path up to its last separator ("" without one; "/" stays the root).
std::string get_base_dir(const std::string& path) {
    size_t at = path.find_last_of('/');
    if (at == std::string::npos) return "";
    if (at == 0) return "/";
    return path.substr(0, at);
}

}  // namespace

const gd::Dict<std::string, gd::Int> StageView::SKY_KINDS = [] {
    gd::Dict<std::string, gd::Int> d;
    d.set("tiles", 0);
    d.set("gradient", 1);
    d.set("fill", 2);
    return d;
}();

StageView::StageView() {
    panorama = gd::make<MeshInstance3D>();
    floor_mesh = gd::make<MeshInstance3D>();
}

// The arcade version to draw for `stage` (the Stage backdrops setting), or null.
gd::Ref<ArcadeStageData> StageView::arcade_of(const gd::Ref<StageData>& stage) {
    if (stage->arcade == nullptr || stage->tile_map.size() != 0 || GameSettings::read_text("stage_backdrops") != "arcade") return nullptr;
    return stage->arcade;
}

void StageView::setup(const gd::Ref<StageData>& stage) {
    _stage = stage;
    _steps = 0;
    arcade = arcade_of(stage);
    // Settings.changed.connect(_on_setting_changed): GameApp forwards the changes to the views it shows.
    // The tile-map stages (Tekken Ball, Tekken Force) draw their backdrop in screen space
    // (TiledPanorama) and have no panorama objects.
    if (arcade != nullptr) {
        panorama->mesh = _arcade_mesh(arcade->scene, arcade->vertex_floats);
        panorama->material_override = gd::make<ShaderMaterial>();
        _panorama_textures();
        _build_props(stage);
        panorama->set_scale(Vector3(static_cast<float>(arcade->scene_scale),
                                    static_cast<float>(arcade->scene_scale * arcade->scene_y_scale),
                                    static_cast<float>(arcade->scene_scale)));
        panorama->set_position(WorldSpace::point(0, static_cast<double>(arcade->scene_height), 0));
        panorama->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
        sky_material = gd::make<ShaderMaterial>();
        _sky_parameters();
    } else if (stage->tile_map.size() == 0) {
        panorama->mesh = _panorama_mesh(stage);
        panorama->material_override = gd::make<ShaderMaterial>();
        _panorama_textures();
        panorama->set_scale(Vector3::ONE() * static_cast<float>(PANORAMA_PARALLAX));
        panorama->set_position(WorldSpace::point(0, static_cast<double>(stage->panorama_height), 0));
        panorama->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
    } else {
        panorama->visible = false;
    }
    add_child(panorama);
    floor_mesh->mesh = _floor_mesh(stage, arcade);
    add_child(floor_mesh);
}

// Keeps the panorama and the floor around the fighters' midpoint (see the header).
void StageView::follow(const Vector3& midpoint) {
    panorama->transform.origin.x = midpoint.x;
    panorama->transform.origin.z = midpoint.z;
    floor_mesh->transform.origin.x = midpoint.x;
    floor_mesh->transform.origin.z = midpoint.z;
}

gd::Ref<ArrayMesh> StageView::_panorama_mesh(const gd::Ref<StageData>& stage) {
    // PackedFloat32Array: read through vec() (a packed slot reads as an int).
    const std::vector<float>& v = stage->panorama.vec();
    gd::Int stride = stage->vertex_floats;
    gd::Int count = gd::div(static_cast<gd::Int>(v.size()), stride);
    std::vector<Vector3> positions;
    std::vector<gfx::Vec2> uvs;
    std::vector<gfx::Color> colours;
    positions.resize(static_cast<size_t>(count));
    uvs.resize(static_cast<size_t>(count));
    colours.resize(static_cast<size_t>(count));
    for (gd::Int i = 0; i < count; ++i) {
        size_t o = static_cast<size_t>(i * stride);
        positions[static_cast<size_t>(i)] = WorldSpace::point(v[o], v[o + 1], v[o + 2]);
        uvs[static_cast<size_t>(i)] = gfx::Vec2(v[o + 3], v[o + 4]);
        // Packet RGB modulation, 0x80 = unchanged; halved to fit the vertex colour range.
        colours[static_cast<size_t>(i)] = gfx::Color(static_cast<float>(v[o + 5] / 2.0), static_cast<float>(v[o + 6] / 2.0),
                                                     static_cast<float>(v[o + 7] / 2.0));
    }
    MeshArrays arrays;
    arrays.vertex = positions;
    arrays.tex_uv = uvs;
    arrays.color = colours;
    gd::Ref<ArrayMesh> mesh = gd::make<ArrayMesh>();
    mesh->add_surface_from_arrays(Primitive::TRIANGLES, arrays);
    return mesh;
}

// The arcade scene or a prop (arcade.py's scene.bin format).
gd::Ref<ArrayMesh> StageView::_arcade_mesh(const gd::Packed<float>& v, gd::Int stride) {
    (void)v;
    (void)stride;
    _arcade_stub("_arcade_mesh");   // TODO: arcade stages (no arcade data)
    return gd::make<ArrayMesh>();
}

// One simulation step (see the header).
void StageView::step(gd::Int camera_yaw, gd::Int round_number, gd::Int round_state, gd::Int held) {
    _steps += 1;
    if (arcade == nullptr) return;
    if (!arcade->scene_animation_rect.is_empty()) {
        gd::Int copies = gd::div(_steps, arcade->scene_animation_period);
        gd::Int frame = copies > 0 ? gd::mod(copies - 1, arcade->scene_animation_frames) : -1;
        std::dynamic_pointer_cast<ShaderMaterial>(panorama->material_override)
            ->set_shader_parameter("frame", static_cast<int>(frame));
    }
    if (props == nullptr) return;
    // TODO: arcade stages (no arcade data): the props' round start (ArcadeProps.start with
    // ArcadeProps.arcade_buttons(held)), the helicopter's visibility and ArcadeProps.step.
    (void)camera_yaw;
    (void)round_number;
    (void)round_state;
    (void)held;
    _arcade_stub("step");
    _place_props();
}

// The props' meshes under the scene.
void StageView::_build_props(const gd::Ref<StageData>& stage) {
    if (arcade->props_kind.empty() || arcade->props_meshes.is_empty()) return;
    (void)stage;
    _arcade_stub("_build_props");   // TODO: arcade stages (no arcade data): ArcadeProps is not ported
}

// The props' transforms from their SVECTOR angles and positions.
void StageView::_place_props() {
    _arcade_stub("_place_props");   // TODO: arcade stages (no arcade data)
}

gd::PI32 StageView::_arcade_sine_table(const std::string& stages_dir) {
    (void)stages_dir;
    _arcade_stub("_arcade_sine_table");   // TODO: arcade stages (no arcade data)
    return _arcade_sine;
}

// The floor's kind of this stage as drawn (stages.md#floor; the arcade's own when it is drawn).
gd::Int StageView::floor_kind() const {
    return arcade != nullptr ? arcade->floor_kind : _stage->floor_kind;
}

// The camera rig's coverage: the arcade's sky surrounds the camera, so it covers every view.
gd::PI32 StageView::coverage() const {
    return arcade != nullptr ? gd::PI32() : _stage->coverage;
}

// Shows the arcade's sky as `environment`'s background (see the header).
void StageView::apply_environment(const gd::Ref<Environment>& environment, bool true_ogre) {
    if (sky_material == nullptr || true_ogre) return;
    // TODO: arcade stages (no arcade data): Sky with sky_material as environment.sky,
    // REFLECTION_SOURCE_DISABLED (gfx3d's Environment has no sky).
    (void)environment;
    _arcade_stub("apply_environment");
}

void StageView::_notification(gd::Int what) {
    if (what == NOTIFICATION_VISIBILITY_CHANGED || what == NOTIFICATION_ENTER_TREE) _show_sky();
}

void StageView::_show_sky() {
    if (_environment != nullptr) {
        // TODO: arcade stages (no arcade data): BG_SKY while is_visible_in_tree(), else BG_COLOR
        // (gfx3d's Environment has no BG_SKY; _environment is only set with the arcade sky).
        _arcade_stub("_show_sky");
    }
}

// Every rendered frame: the arcade sky's layout for `camera` (see the header).
void StageView::place_sky(const gd::Ref<Camera3D>& camera, gfx::Vec2 frame_tan) {
    if (sky_material == nullptr) return;
    (void)camera;
    (void)frame_tan;
    _arcade_stub("place_sky");   // TODO: arcade stages (no arcade data)
}

void StageView::_sky_parameters() {
    _arcade_stub("_sky_parameters");   // TODO: arcade stages (no arcade data)
}

// An RGB fill (enabled: alpha 1) or none (alpha 0).
gfx::Color StageView::_fill(const gd::Json& value) {
    if (value.is_null()) return gfx::Color(0, 0, 0, 0);
    gd::PI32 c = JsonFile::ints(value);
    return gfx::Color::c8(static_cast<int>(gd::Int(c[0])), static_cast<int>(gd::Int(c[1])), static_cast<int>(gd::Int(c[2])));
}

// Where the floor ends (the tile-map stages' backdrop covers the rest): a Godot-space line
// dot(xz, (x, y)) = z.
void StageView::set_floor_edge(const Vector3& edge) {
    gd::Ref<ShaderMaterial> material = std::dynamic_pointer_cast<ShaderMaterial>(
        std::dynamic_pointer_cast<ArrayMesh>(floor_mesh->mesh)->surface_get_material(0));
    material->set_shader_parameter("far_edge", edge);
}

// The spotlight floor of this step (kind 0: none): the light points in Godot space.
void StageView::set_spotlight(gd::Int kind, const Vector3& a, const Vector3& b, float k, float limit) {
    gd::Ref<ShaderMaterial> material = std::dynamic_pointer_cast<ShaderMaterial>(
        std::dynamic_pointer_cast<ArrayMesh>(floor_mesh->mesh)->surface_get_material(0));
    material->set_shader_parameter("spot_kind", static_cast<int>(kind));
    material->set_shader_parameter("spot_lights", Vector4(a.x, a.z, b.x, b.z));
    material->set_shader_parameter("spot_k", k);
    material->set_shader_parameter("spot_limit", limit);
}

// Chooses the floor picture: the exact pattern or the one with smoothed tile seams (the arcade's
// with `a`).
std::string StageView::floor_texture_path(const gd::Ref<StageData>& stage, bool smooth, const gd::Ref<ArcadeStageData>& a) {
    if (a != nullptr) return gd::path_join(a->directory, smooth ? a->floor_pattern_smooth : a->floor_pattern);
    return gd::path_join(stage->directory, smooth ? stage->floor_pattern_smooth : stage->floor_pattern);
}

// Half the side of the floor square (game units).
gd::Int StageView::floor_half_extent(const gd::Ref<StageData>& stage, const gd::Ref<ArcadeStageData>& a) {
    // The arcade draws its 10 x 10 tiles and nothing more: its scene's own ground lies beyond.
    if (a != nullptr) return a->floor_tile_size * FLOOR_PATTERN / 2;
    // The tile-map stages have no walls: the floor reaches the backdrop's edge wherever the
    // camera looks (the far edge is cut per frame, StageView.set_floor_edge).
    if (stage->tile_map.size() != 0) return TILED_FLOOR_HALF;
    gd::Int reach = 0;
    for (gd::Int v : stage->panorama_extent) reach = gd::maxi(reach, gd::absi(v));
    gd::Int walls = static_cast<gd::Int>(static_cast<double>(reach) * PANORAMA_PARALLAX);
    return gd::maxi(walls, stage->floor_tile_size * FLOOR_PATTERN / 2) + FLOOR_UNDER_WALLS;
}

// One quad under the whole panorama. The floor shader derives the texture coordinates
// from the world position, so the pattern repeats without per-tile seams.
gd::Ref<ArrayMesh> StageView::_floor_mesh(const gd::Ref<StageData>& stage, const gd::Ref<ArcadeStageData>& a) {
    float half = static_cast<float>(floor_half_extent(stage, a));
    gd::Arr<Vector3> quad{
        WorldSpace::point(-half, 0, half), WorldSpace::point(half, 0, half),
        WorldSpace::point(-half, 0, -half), WorldSpace::point(half, 0, -half),
    };
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    for (gd::Int i : gd::PI32{0, 1, 2, 1, 3, 2}) {
        positions.push_back(quad[i]);
        normals.push_back(Vector3::UP());
    }
    MeshArrays arrays;
    arrays.vertex = positions;
    arrays.normal = normals;
    gd::Ref<ArrayMesh> mesh = gd::make<ArrayMesh>();
    mesh->add_surface_from_arrays(Primitive::TRIANGLES, arrays);
    gd::Ref<ShaderMaterial> material = gd::make<ShaderMaterial>();
    _floor_textures(material, stage, a);
    gd::Int tile_size = a != nullptr ? a->floor_tile_size : stage->floor_tile_size;
    gd::Int shade = a != nullptr ? a->floor_shade : stage->floor_shade;
    gd::Int fade = a != nullptr ? a->floor_fade : stage->floor_fade;
    material->set_shader_parameter("pattern_size",
                                   static_cast<float>(static_cast<double>(tile_size * FLOOR_PATTERN) / WorldSpace::UNITS_PER_METRE));
    if (shade != 0 && fade > 0) {
        gfx::TextureRef table = a != nullptr ? _arcade_floor_fade_table(get_base_dir(stage->directory))
                                             : _floor_fade_table(get_base_dir(stage->directory));
        material->set_shader_parameter("fade_table", table);
        material->set_shader_parameter("shade", static_cast<int>(shade));
        material->set_shader_parameter("fade_scale", static_cast<int>(gd::div(FADE_SCALE, fade)));
        material->set_shader_parameter("units_per_metre", static_cast<float>(WorldSpace::UNITS_PER_METRE));
    }
    mesh->surface_set_material(0, material);
    return mesh;
}

// FloorDrawGrid's shading table (stages/floor_fade.json) as a 1-texel-high picture of its bytes.
gfx::TextureRef StageView::_floor_fade_table(const std::string& stages_dir) {
    if (_fade_table == nullptr) _fade_table = _table_texture(gd::path_join(stages_dir, FLOOR_FADE_FILE));
    return _fade_table;
}

// The arcade floor's table (FUN_801A941C, stages/arcade_floor_fade.json).
gfx::TextureRef StageView::_arcade_floor_fade_table(const std::string& stages_dir) {
    (void)stages_dir;
    _arcade_stub("_arcade_floor_fade_table");   // TODO: arcade stages (no arcade data)
    return _arcade_fade_table;
}

gfx::TextureRef StageView::_table_texture(const std::string& path) {
    gd::Json d = JsonFile::read(path);
    gd::PI32 table = JsonFile::ints(d.get("table"));
    gd::PBytes bytes;
    for (gd::Int v : table) bytes.append(v & 0xFF);
    if (bytes.is_empty()) bytes.append(0);
    // Image.FORMAT_R8: gfx::Image is RGBA8, so each byte is a texel's red (floor.gdshader reads .r).
    gfx::Image image;
    image.width = static_cast<int>(bytes.size());
    image.height = 1;
    image.rgba.assign(static_cast<size_t>(4 * bytes.size()), 0);
    for (gd::Int i = 0; i < bytes.size(); ++i) {
        image.rgba[static_cast<size_t>(4 * i)] = static_cast<uint8_t>(gd::Int(bytes[i]));
        image.rgba[static_cast<size_t>(4 * i + 3)] = 0xFF;
    }
    // Kept: the floors' materials hold it, so it makes itself again after a GL context loss.
    return std::make_shared<gfx::Texture>(image, false, true);
}

// The panorama's turn of this step (BackdropTurn.angle, 4096 units).
void StageView::set_backdrop_turn(gd::Int angle) {
    // rotation.y = ...: the panorama and the floor turn only about y (their x and z rotations are 0).
    panorama->set_rotation(Vector3(0, static_cast<float>(WorldSpace::radians(angle)), 0));
    if (arcade != nullptr) floor_mesh->set_rotation(Vector3(0, static_cast<float>(WorldSpace::radians(angle)), 0));
    _backdrop_turn = angle;
}

// The camera's horizontal distance to the fighters' midpoint (game units) of this step, from
// which the floor's shading starts.
void StageView::set_floor_distance(float distance) {
    gd::Ref<ShaderMaterial> material = std::dynamic_pointer_cast<ShaderMaterial>(
        std::dynamic_pointer_cast<ArrayMesh>(floor_mesh->mesh)->surface_get_material(0));
    material->set_shader_parameter("camera_distance", distance);
}

void StageView::_floor_textures(const gd::Ref<ShaderMaterial>& material, const gd::Ref<StageData>& stage,
                                const gd::Ref<ArcadeStageData>& a) {
    material->shader = RenderQuality::shader(FLOOR_SHADER);
    material->set_shader_parameter("pattern", TexturePacks::texture(floor_texture_path(stage, true, a)));
}

void StageView::_panorama_textures() {
    gd::Ref<ShaderMaterial> material = std::dynamic_pointer_cast<ShaderMaterial>(panorama->material_override);
    if (arcade != nullptr) {
        // TODO: arcade stages (no arcade data): ARCADE_SCENE_SHADER, its atlas and animation frames.
        _arcade_stub("_panorama_textures");
        return;
    }
    material->shader = RenderQuality::shader(PANORAMA_SHADER);
    material->set_shader_parameter("atlas", TexturePacks::texture(_stage->directory + "/panorama.png"));
}

// The Textures or Texture pack setting changed: the shaders and pictures again.
void StageView::_on_setting_changed(const std::string& key) {
    if (_stage == nullptr || !RenderQuality::TEXTURE_SETTINGS.has(key)) return;
    if (panorama->material_override != nullptr) _panorama_textures();
    gd::Ref<ShaderMaterial> floor_material = std::dynamic_pointer_cast<ShaderMaterial>(
        std::dynamic_pointer_cast<ArrayMesh>(floor_mesh->mesh)->surface_get_material(0));
    _floor_textures(floor_material, _stage, arcade);
    if (sky_material != nullptr) _sky_parameters();
}

// TODO: arcade stages (no arcade data): where an arcade branch would draw; logs once per place.
void StageView::_arcade_stub(const std::string& where) {
    static std::set<std::string> logged;
    if (logged.insert(where).second)
        Log::warning("StageView." + where + ": arcade stages are not ported (no arcade data); drawing nothing");
}

}  // namespace t3

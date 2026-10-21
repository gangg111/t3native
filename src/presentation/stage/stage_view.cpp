// remake/presentation/stage/stage_view.gd
// Draws a stage: the distant panorama and the tiled floor (see the header).
#include "presentation/stage/stage_view.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "content/arcade_stage_data.h"
#include "core/fight/round_state.h"
#include "presentation/stage/arcade_props.h"
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

constexpr double TAU = 6.28318530717958647692;

// Godot's fposmod and wrapf (floats).
double fposmod(double x, double y) {
    double m = std::fmod(x, y);
    return (m < 0) != (y < 0) && m != 0 ? m + y : m;
}

double wrapf(double value, double min, double max) {
    double range = max - min;
    return range == 0 ? min : value - range * std::floor((value - min) / range);
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
    const std::vector<float>& f = v.vec();
    size_t count = stride > 0 ? f.size() / static_cast<size_t>(stride) : 0;
    MeshArrays arrays;
    arrays.vertex.resize(count);
    arrays.tex_uv.resize(count);
    arrays.custom[0].resize(4 * count);
    arrays.custom[1].resize(3 * count);
    arrays.custom_components = {4, 3, 4, 4};   // ARRAY_CUSTOM_RGBA_FLOAT, ARRAY_CUSTOM_RGB_FLOAT
    for (size_t i = 0; i < count; ++i) {
        size_t o = i * static_cast<size_t>(stride);
        arrays.vertex[i] = WorldSpace::point(f[o], f[o + 1], f[o + 2]);
        arrays.tex_uv[i] = gfx::Vec2(f[o + 3], f[o + 4]);
        for (size_t k = 0; k < 4; ++k) arrays.custom[0][4 * i + k] = f[o + 5 + k];
        for (size_t k = 0; k < 3; ++k) arrays.custom[1][3 * i + k] = f[o + 9 + k];
    }
    gd::Ref<ArrayMesh> mesh = gd::make<ArrayMesh>();
    mesh->add_surface_from_arrays(Primitive::TRIANGLES, arrays);
    return mesh;
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
    if (round_number >= 0 && round_number != _round) {
        _round = round_number;
        props->start(ArcadeProps::arcade_buttons(held), _steps);
    }
    if (props->kind == ArcadeProps::HELICOPTER) {
        bool shown = round_state >= RoundState::REPLAY;
        prop_nodes[0]->visible = shown;
        if (!shown) return;
    }
    props->step(camera_yaw);
    _place_props();
}

// The props' meshes under the scene.
void StageView::_build_props(const gd::Ref<StageData>& stage) {
    if (arcade->props_kind.empty() || arcade->props_meshes.is_empty()) return;
    props = gd::make<ArcadeProps>(arcade->props_kind, _arcade_sine_table(get_base_dir(stage->directory)));
    for (gd::Int k = 0; k < arcade->props_meshes.size(); ++k) {
        gd::Ref<MeshInstance3D> node = gd::make<MeshInstance3D>();
        node->mesh = _arcade_mesh(arcade->props_meshes[k], arcade->vertex_floats);
        node->material_override = panorama->material_override;
        node->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
        Node3D* parent = props->kind == ArcadeProps::HELICOPTER && k > 0 ? prop_nodes[0].get() : panorama.get();
        parent->add_child(node);
        prop_nodes.append(node);
    }
    if (props->kind == ArcadeProps::HELICOPTER) prop_nodes[0]->visible = false;
    props->start();
    _place_props();
}

// The props' transforms from their SVECTOR angles and positions.
void StageView::_place_props() {
    // Basis.from_scale(Vector3(1, -1, -1)): the game's y-down frame and Godot's.
    const Basis flip(Vector3(1, 0, 0), Vector3(0, -1, 0), Vector3(0, 0, -1));
    for (gd::Int k = 0; k < prop_nodes.size(); ++k) {
        const gd::PI32& a = props->angles[k];
        Basis rx = Basis::from_axis_angle(Vector3(1, 0, 0), static_cast<float>(WorldSpace::radians(a[0])));
        Basis ry = Basis::from_axis_angle(Vector3(0, 1, 0), static_cast<float>(WorldSpace::radians(a[1])));
        Basis rz = Basis::from_axis_angle(Vector3(0, 0, 1), static_cast<float>(WorldSpace::radians(a[2])));
        Basis turn = props->kind == ArcadeProps::HELICOPTER ? ry * rx * rz : rx * ry * rz;
        prop_nodes[k]->transform = Transform3D(flip * turn * flip, WorldSpace::point_i(props->positions[k]));
    }
}

gd::PI32 StageView::_arcade_sine_table(const std::string& stages_dir) {
    if (_arcade_sine.is_empty()) {
        gd::Json d = JsonFile::read(gd::path_join(stages_dir, ArcadeStageData::SINE_FILE));
        _arcade_sine = JsonFile::ints(d.get("table"));
    }
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
    // Sky.new() with sky_material (gfx3d draws an Environment's sky material directly; it has no
    // reflections to disable).
    environment->sky_material = sky_material;
    _environment = environment;
    _show_sky();
}

void StageView::_notification(gd::Int what) {
    if (what == NOTIFICATION_VISIBILITY_CHANGED || what == NOTIFICATION_ENTER_TREE) _show_sky();
}

void StageView::_show_sky() {
    if (_environment != nullptr) {
        _environment->background_mode = is_visible_in_tree() ? Environment::BG_SKY : Environment::BG_COLOR;
    }
}

// Every rendered frame: the arcade sky's layout for `camera` (see the header).
void StageView::place_sky(const gd::Ref<Camera3D>& camera, gfx::Vec2 frame_tan) {
    if (sky_material == nullptr) return;
    Basis b = camera->get_global_basis();
    Vector3 z = b.get_column(2);
    double yaw = std::atan2(z.x, z.z) * WorldSpace::ANGLE_UNITS / TAU;
    double pitch = wrapf(std::asin(std::clamp(static_cast<double>(z.y), -1.0, 1.0)) * WorldSpace::ANGLE_UNITS / TAU,
                             -ANGLE_HALF, ANGLE_HALF);
    const gd::Json& sky = arcade->sky;
    auto num = [&](const char* key, double fallback) {
        const gd::Json& v = sky.get(key);
        return v.is_null() ? fallback : v.num();
    };
    double step = num("step", 1.0);
    double k = num("pitch_scale", 0.0);
    double y = -pitch * k / 256.0 + num("offset", 0.0);
    if ((sky.get("clamp_top").is_bool() && sky.get("clamp_top").boolean()) || sky.get("kind").str() == "gradient") y = std::min(y, 0.0);
    if (!sky.get("clamp_bottom").is_null()) y = std::max(y, sky.get("clamp_bottom").num());
    sky_material->set_shader_parameter("view", b.inverse());
    sky_material->set_shader_parameter("frame_tan", frame_tan);
    sky_material->set_shader_parameter("scroll", static_cast<float>(fposmod(static_cast<double>(_backdrop_turn) - yaw,
                                                                                WorldSpace::ANGLE_UNITS) *
                                                                    512.0 / step));
    sky_material->set_shader_parameter("sky_top", static_cast<float>(y));
}

void StageView::_sky_parameters() {
    const gd::Json& sky = arcade->sky;
    sky_material->shader = RenderQuality::shader(ARCADE_SKY_SHADER);
    std::string kind = sky.get("kind").is_null() ? "tiles" : sky.get("kind").str();
    sky_material->set_shader_parameter("kind", static_cast<int>(SKY_KINDS.has(kind) ? SKY_KINDS.at(kind) : 0));
    if (sky.has("texture")) {
        sky_material->set_shader_parameter("strip", TexturePacks::texture(gd::path_join(arcade->directory, sky.get("texture").str())));
    }
    sky_material->set_shader_parameter("turn_pixels",
                                       static_cast<float>(sky.get("turn_pixels").is_null() ? 4096.0 : sky.get("turn_pixels").num()));
    sky_material->set_shader_parameter("rows", static_cast<float>(sky.get("rows").is_null() ? 0.0 : sky.get("rows").num()));
    sky_material->set_shader_parameter("upper_fill", _fill(sky.get("upper_fill")));
    sky_material->set_shader_parameter("lower_fill", _fill(sky.get("lower_fill")));
    const gd::Json& gradient = sky.get("gradient");
    if (gradient.is_array() && gradient.size() == 2) {
        sky_material->set_shader_parameter("gradient_top", _fill(gradient[0]));
        sky_material->set_shader_parameter("gradient_bottom", _fill(gradient[1]));
    }
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
    if (_arcade_fade_table == nullptr) _arcade_fade_table = _table_texture(gd::path_join(stages_dir, ArcadeStageData::FADE_FILE));
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
        material->shader = RenderQuality::shader(ARCADE_SCENE_SHADER);
        material->set_shader_parameter("atlas", TexturePacks::texture(gd::path_join(arcade->directory, arcade->scene_texture)));
        if (!arcade->scene_animation_rect.is_empty()) {
            const gd::PI32& r = arcade->scene_animation_rect;
            material->set_shader_parameter("frames",
                                           TexturePacks::texture(gd::path_join(arcade->directory, arcade->scene_animation_texture)));
            material->set_shader_parameter("frames_rect", Vector4(static_cast<float>(gd::Int(r[0])), static_cast<float>(gd::Int(r[1])),
                                                                  static_cast<float>(gd::Int(r[2])), static_cast<float>(gd::Int(r[3]))));
        }
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

}  // namespace t3

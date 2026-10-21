// remake/presentation/stage/stage_lighting.gd
// The stage's light rig (stages.md#lighting) as real lights, and the fighter shader's light rig.
#include "presentation/stage/stage_lighting.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "content/stage_data.h"
#include "core/fighter/fighter_animation.h"
#include "presentation/render_quality.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

// Godot's Color.r8 / g8 / b8 / a8: round(channel * 255), clamped to 0 ... 255.
gd::Int _c8(float channel) {
    return static_cast<gd::Int>(std::clamp(std::round(channel * 255.0f), 0.0f, 255.0f));
}

// StageData keeps its Color as (r, g, b, a).
gfx::Color _colour(const std::array<float, 4>& c) { return gfx::Color(c[0], c[1], c[2], c[3]); }

}  // namespace

StageLighting::StageLighting() {
    environment = gd::make<Environment>();
    sun = gd::make<DirectionalLight3D>();
    fill = gd::make<DirectionalLight3D>();
}

void StageLighting::setup(const gd::Ref<StageData>& stage, const gd::Ref<Camera3D>& camera) {
    environment->background_mode = Environment::BG_COLOR;
    environment->background_color = gfx::Color::black();
    environment->ambient_light_source = Environment::AMBIENT_SOURCE_COLOR;
    environment->ambient_light_color = gfx::Color(static_cast<float>(stage->ambient / 255.0),
                                                  static_cast<float>(stage->ambient / 255.0),
                                                  static_cast<float>(stage->ambient / 255.0));
    environment->ambient_light_energy = static_cast<float>(AMBIENT_ENERGY);
    environment->tonemap_mode = Environment::TONE_MAPPER_LINEAR;
    camera->environment = environment;
    sun->light_color = _colour(stage->light_colour);
    sun->light_energy = static_cast<float>(LIGHT_ENERGY);
    sun->shadow_enabled = true;
    sun->directional_shadow_max_distance = static_cast<float>(SHADOW_DISTANCE);
    add_child(sun);
    aim(sun, light_travel(stage));
    _apply_quality();
    // Settings.changed.connect(_on_setting_changed): GameApp forwards the changes to the views it shows.
    fill->light_color = _colour(stage->light_colour);
    fill->light_energy = static_cast<float>(LIGHT_ENERGY * FILL_SHARE);
    fill->shadow_enabled = false;
    fill->light_cull_mask = static_cast<uint32_t>(ALL_LAYERS & ~FIGHTER_LAYER);
    fill->visible = stage->number != NO_FILL_STAGE;
    add_child(fill);
    aim(fill, Vector3::UP());
}

// The graphics preset on the environment and the main light (RenderQuality).
void StageLighting::_apply_quality() {
    RenderQuality::apply_environment(environment);
    RenderQuality::apply_light(sun);
}

void StageLighting::_on_setting_changed(const std::string& key) {
    if (key == "graphics") _apply_quality();
}

// The fighter shader's light rig for `stage` (fighter_skin.gdshaderinc): FUN_80039BB4's colour
// matrix (main light byte << 4, the light from below (byte >> 1) << 4), SetBackColor's level
// (FUN_8003A3B8: ambient << 4) and the base colour, in GTE units of 4096.
StageLighting::FighterParameters StageLighting::fighter_parameters(const gd::Ref<StageData>& stage) {
    gfx::Color c = _colour(stage->light_colour);
    Vector3 light = Vector3(static_cast<float>(_c8(c.r)), static_cast<float>(_c8(c.g)), static_cast<float>(_c8(c.b))) *
                    16.0f / static_cast<float>(GTE_ONE);
    Vector3 fill = Vector3(static_cast<float>(_c8(c.r) >> 1), static_cast<float>(_c8(c.g) >> 1),
                           static_cast<float>(_c8(c.b) >> 1)) *
                   16.0f / static_cast<float>(GTE_ONE);
    FighterParameters p;
    p.base_colour = Vector3(stage->base_colour[0], stage->base_colour[1], stage->base_colour[2]);
    p.back_colour = back_colour(stage->ambient, stage->ambient, stage->ambient);
    p.light_colour = light;
    p.fill_colour = stage->number != NO_FILL_STAGE ? fill : Vector3::ZERO();
    return p;
}

// The back colour FUN_8003AA6C lights a fighter with in one frame (see the header).
Vector3 StageLighting::fighter_back_colour(gd::Int ambient_level, gd::Int flash, const gfx::Color& freeze) {
    if (flash == FighterAnimation::LIT_BLACK) return Vector3::ZERO();
    if (_c8(freeze.a) != 0) return back_colour(_c8(freeze.r), _c8(freeze.g), _c8(freeze.b));
    gd::Int level = ambient_level >> 4;
    if (flash == 0) return back_colour(level, level, level);
    gd::Int ramp = flash_ramp(ambient_level, flash);
    return back_colour(ramp, ramp, (flash & 1) != 0 ? ramp : level);
}

// FUN_8003A3B8's ramp for the flash count `flash` (1 to 32): the level, in bytes (up to 0x200,
// 2.0 in GTE units), falling linearly from FLASH_PEAK to the stage's level; the product rounds
// towards zero.
gd::Int StageLighting::flash_ramp(gd::Int ambient_level, gd::Int flash) {
    gd::Int t = (FLASH_PEAK - ambient_level) * (FighterAnimation::FLASH_FRAMES + 1 - flash) * 0x80;
    if (t < 0) t += 0xFFF;
    return ((t >> 12) + ambient_level) >> 4;
}

// SetBackColor's colour for bytes (r, g, b): byte << 4 in GTE units of 4096.
Vector3 StageLighting::back_colour(gd::Int r, gd::Int g, gd::Int b) {
    return Vector3(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)) * 16.0f /
           static_cast<float>(GTE_ONE);
}

// Turns a directional light to shine along `direction`, with an up vector that is never
// parallel to it (a light along +-z uses +y).
void StageLighting::aim(const gd::Ref<DirectionalLight3D>& light, const Vector3& direction) {
    Vector3 up = std::fabs(direction.normalized().dot(Vector3::FORWARD())) < 0.99 ? Vector3::FORWARD() : Vector3::UP();
    light->transform.basis = Basis::looking_at(direction, up);
}

// The direction the main light travels, in Godot space: the game's
// (cos(yaw - 0x800) cos p, sin p, sin(yaw - 0x800) cos p) with y down.
Vector3 StageLighting::light_travel(const gd::Ref<StageData>& stage) {
    double p = WorldSpace::radians(stage->light_pitch);
    double y = WorldSpace::radians(stage->light_yaw - 0x800);
    return WorldSpace::point(std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)).normalized();
}

}  // namespace t3

#pragma once
// remake/presentation/stage/stage_lighting.gd
// The stage's light rig (stages.md#lighting) as real lights: the main directional light with
// shadows, the half-strength light shining up from below, and the back colour as ambient
// light, for the floor. The fighters are lit by the record itself as the game's NCCT does
// (fighter_parameters, fighter_skin.gdshaderinc), only the main light's shadows come from here.
// No tonemapping: the unshaded panorama and the fighters show the game's display-space colours.
// The environment is the view's camera's (Camera3D.environment): a WorldEnvironment per view would
// leave only the first one in the tree active, whichever view is shown.
//
// The back colour of a fighter changes per frame (fighter_back_colour: Tekken Force's pick-up
// flash, practice's FREEZE SIGNAL, a burning body); in the game it lights the fighters only (the
// floor and the panorama are not lit by the GTE), so the ambient light here, on the stage's meshes,
// stays the stage's.
#include <cstdint>
#include <string>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx3d/nodes.h"

namespace t3 {

class StageData;

class StageLighting : public Node3D {
public:
    // fighter_parameters' Dictionary ("base_colour", "back_colour", "light_colour", "fill_colour").
    struct FighterParameters {
        Vector3 base_colour;
        Vector3 back_colour;
        Vector3 light_colour;
        Vector3 fill_colour;
    };

    static constexpr double FILL_SHARE = 0.5;
    static constexpr double LIGHT_ENERGY = 1.1;
    static constexpr double AMBIENT_ENERGY = 0.55;
    static constexpr double SHADOW_DISTANCE = 30.0;
    static constexpr gd::Int NO_FILL_STAGE = 17;     // FUN_80039BB4: no light from below on this stage
    static constexpr double GTE_ONE = 4096.0;        // the GTE's 1.0: colour matrix and back colour are byte << 4
    static constexpr gd::Int FLASH_PEAK = 0x2000;    // the pick-up flash starts at 2.0 (GTE units) and falls to the stage's level
    static inline const gfx::Color NO_SIGNAL = gfx::Color::c8(0, 0, 0, 0);   // practice's FREEZE SIGNAL with its flag (alpha) clear
    // The fighters are on their own render layer and the fill light does not reach it: the fighter
    // shader adds the fill itself (fighter_skin.gdshaderinc) and takes every directional light that
    // reaches it for the main light. So the sun is the only directional light that may reach this
    // layer: a light added to a scene with fighters in it has to leave FIGHTER_LAYER out of its
    // `light_cull_mask` (the tests check the stage's two).
    static constexpr gd::Int FIGHTER_LAYER = 1 << 1;
    static constexpr gd::Int ALL_LAYERS = 0xFFFFF;

    gd::Ref<Environment> environment;
    gd::Ref<DirectionalLight3D> sun;
    gd::Ref<DirectionalLight3D> fill;

    StageLighting();
    void setup(const gd::Ref<StageData>& stage, const gd::Ref<Camera3D>& camera);
    // The graphics preset on the environment and the main light (RenderQuality).
    void _apply_quality();
    void _on_setting_changed(const std::string& key);
    // The fighter shader's light rig for `stage` (fighter_skin.gdshaderinc): FUN_80039BB4's colour
    // matrix (main light byte << 4, the light from below (byte >> 1) << 4), SetBackColor's level
    // (FUN_8003A3B8: ambient << 4) and the base colour, in GTE units of 4096.
    static FighterParameters fighter_parameters(const gd::Ref<StageData>& stage);
    // The back colour FUN_8003AA6C lights a fighter with in one frame, in GTE units of 4096
    // (stages.md#lighting), from FightState.flash_level `flash`: black while the fighter burns
    // (FighterAnimation.LIT_BLACK), else practice's FREEZE SIGNAL `freeze` while its flag (alpha) is
    // set, else the pick-up flash's ramp for the count (1 to 32; 0: none), else the stage's level.
    // `ambient_level` is the stage record's word (StageData.ambient_level). The flash's blue is the
    // ramp on odd counts and the stage's level on even ones, so it flickers between white and yellow.
    static Vector3 fighter_back_colour(gd::Int ambient_level, gd::Int flash, const gfx::Color& freeze);
    // FUN_8003A3B8's ramp for the flash count `flash` (1 to 32): the level, in bytes (up to 0x200,
    // 2.0 in GTE units), falling linearly from FLASH_PEAK to the stage's level; the product rounds
    // towards zero.
    static gd::Int flash_ramp(gd::Int ambient_level, gd::Int flash);
    // SetBackColor's colour for bytes (r, g, b): byte << 4 in GTE units of 4096.
    static Vector3 back_colour(gd::Int r, gd::Int g, gd::Int b);
    // Turns a directional light to shine along `direction`, with an up vector that is never
    // parallel to it (a light along +-z uses +y).
    static void aim(const gd::Ref<DirectionalLight3D>& light, const Vector3& direction);
    // The direction the main light travels, in Godot space: the game's
    // (cos(yaw - 0x800) cos p, sin p, sin(yaw - 0x800) cos p) with y down.
    static Vector3 light_travel(const gd::Ref<StageData>& stage);
};

}  // namespace t3

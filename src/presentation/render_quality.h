#pragma once
// remake/presentation/render_quality.gd
// The graphics settings (remake-plan.md#quality-presets): the preset of the renderer in use
// applied to the environments, the stage lights and the window's viewport; the Textures setting
// (the crisp variant of a shader is compiled with CRISP defined); and the replacement texture
// packs (TexturePacks). Presets change only the look, never the game.
//
// | Preset       | Renderer      | Features                                                        |
// | web          | Compatibility | filtered shadow maps, basic bloom, MSAA 2x                     |
// | mobile       | Mobile        | shadows, bloom, MSAA 2x, the 3D view at 85 % (bilinear)       |
// | mobile_high  | Mobile        | soft 4k shadows, bloom, MSAA 4x, the 3D view at full size      |
// | desktop_low  | Forward+      | shadows, bloom, MSAA 2x                                        |
// | desktop_high | Forward+      | soft 4k shadows, bloom, MSAA 4x, SSAO, SSIL                    |
//
// The native app has no Godot viewport or RenderingServer: apply_viewport fills a ViewportQuality
// the app reads (MSAA, 3D scale, shadow atlas size and filter quality) instead of calling them.
#include <string>

#include "gd/gd.h"

namespace t3 {

class Shader;
class Environment;
class DirectionalLight3D;

class RenderQuality {
public:
    // Viewport.MSAA (Godot's values).
    enum MSAA : gd::Int { MSAA_DISABLED = 0, MSAA_2X = 1, MSAA_4X = 2, MSAA_8X = 3 };
    // RenderingServer.ShadowQuality (Godot's values).
    enum ShadowQuality : gd::Int {
        SHADOW_QUALITY_HARD = 0,
        SHADOW_QUALITY_SOFT_VERY_LOW = 1,
        SHADOW_QUALITY_SOFT_LOW = 2,
        SHADOW_QUALITY_SOFT_MEDIUM = 3,
        SHADOW_QUALITY_SOFT_HIGH = 4,
        SHADOW_QUALITY_SOFT_ULTRA = 5,
    };
    // Viewport.Scaling3DMode (Godot's values).
    enum Scaling3DMode : gd::Int { SCALING_3D_MODE_BILINEAR = 0, SCALING_3D_MODE_FSR = 1 };

    // One preset of PRESETS (a Dictionary in the remake): the keys a preset may leave out have the
    // value `current().get(key, default)` reads (shadow_bias, shadow_normal_bias, ssao, ssil).
    struct Preset {
        gd::Int msaa = MSAA_2X;
        gd::Int shadow_size = 2048;
        gd::Int soft = SHADOW_QUALITY_SOFT_LOW;
        double glow = 0.6;
        double scale = 1.0;
        double shadow_bias = 0.1;          // SHADOW_BIAS
        double shadow_normal_bias = 2.0;   // SHADOW_NORMAL_BIAS
        bool ssao = false;
        bool ssil = false;
    };

    // What apply_viewport sets on the window's viewport and the RenderingServer, for the app.
    struct ViewportQuality {
        gd::Int msaa_3d = MSAA_DISABLED;
        double scaling_3d_scale = 1.0;
        gd::Int scaling_3d_mode = SCALING_3D_MODE_BILINEAR;
        gd::Int directional_shadow_atlas_size = 4096;
        bool directional_shadow_16_bits = true;
        gd::Int directional_soft_shadow_filter_quality = SHADOW_QUALITY_SOFT_LOW;
    };

    // The main light's shadow biases (Godot's DirectionalLight3D defaults) where a preset sets none.
    static constexpr double SHADOW_BIAS = 0.1;
    static constexpr double SHADOW_NORMAL_BIAS = 2.0;

    static const gd::Dict<std::string, Preset> PRESETS;
    static inline const std::string CRISP_DEFINE = "#define CRISP\n";
    // The settings that change the textures' look: the views built with them refresh their materials.
    static inline const gd::Arr<std::string> TEXTURE_SETTINGS{"texture_filter", "texture_pack"};

    static inline gd::Dict<std::string, gd::Ref<Shader>> _crisp;   // shader path -> its crisp variant

    // The settings of the preset in effect.
    static Preset current();
    static void apply_environment(const gd::Ref<Environment>& env);
    static void apply_light(const gd::Ref<DirectionalLight3D>& light);
    // The window's viewport and the shadow atlas.
    static void apply_viewport(ViewportQuality& viewport);
    // `base` for the Textures setting: itself (smooth), or compiled with CRISP defined.
    static gd::Ref<Shader> shader(const gd::Ref<Shader>& base);
};

}  // namespace t3

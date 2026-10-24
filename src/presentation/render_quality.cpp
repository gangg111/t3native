// remake/presentation/render_quality.gd
// The graphics settings (remake-plan.md#quality-presets): the presets, the Textures setting's
// CRISP shader variants and the viewport's quality values.
#include "presentation/render_quality.h"

#include "gfx3d/nodes.h"
#include "gfx3d/resources.h"
#include "platform/settings.h"

namespace t3 {

namespace {

// Settings.preset() without the autoload (tests): the renderer's first preset.
#ifdef __ANDROID__
const std::string DEFAULT_PRESET = "mobile";
#else
const std::string DEFAULT_PRESET = "desktop_low";
#endif

}  // namespace

const gd::Dict<std::string, RenderQuality::Preset> RenderQuality::PRESETS = [] {
    gd::Dict<std::string, Preset> d;
    // The Compatibility renderer offsets its shadow lookups along the vertex normal, by less the more
    // that normal faces the light. With smooth normals (the Shading setting) a flat face slanted to
    // the light gets almost no offset, and with the default biases and one unfiltered sample the
    // fighters shadow themselves in rows of shadow-map texels (shadow acne), largest where the camera
    // is far and the texels are coarse (the demonstration's close-ups). Larger biases and the
    // 5-sample filter clear them, but for faint steps where the light grazes a limb, while the
    // floor shadows still meet the feet. Changing the fighter shader instead (ignoring or forcing
    // the shadow on faces turned from the light) shows the flat faces.
    Preset web;
    web.msaa = MSAA_2X;
    web.shadow_size = 2048;
    web.soft = SHADOW_QUALITY_SOFT_LOW;
    web.glow = 0.5;
    web.scale = 1.0;
    web.shadow_bias = 2.0;
    web.shadow_normal_bias = 4.0;
    d.set("web", web);
    Preset mobile;
    mobile.msaa = MSAA_2X;
    mobile.shadow_size = 2048;
    mobile.soft = SHADOW_QUALITY_SOFT_VERY_LOW;
    mobile.glow = 0.5;
    mobile.scale = 0.85;
    d.set("mobile", mobile);
    // Native: for phones the Standard preset is too much for (a Mali-G52's scene took about 25 ms):
    // no multisampling, a smaller shadow map and a smaller view.
    Preset mobile_low;
    mobile_low.msaa = MSAA_DISABLED;
    mobile_low.shadow_size = 1024;
    mobile_low.soft = SHADOW_QUALITY_SOFT_VERY_LOW;
    mobile_low.glow = 0.5;
    mobile_low.scale = 0.7;
    d.set("mobile_low", mobile_low);
    // The Mobile renderer has no SSAO, SSIL or SSR: its best is sharper shadows and edges at full size.
    Preset mobile_high;
    mobile_high.msaa = MSAA_4X;
    mobile_high.shadow_size = 4096;
    mobile_high.soft = SHADOW_QUALITY_SOFT_MEDIUM;
    mobile_high.glow = 0.6;
    mobile_high.scale = 1.0;
    d.set("mobile_high", mobile_high);
    Preset desktop_low;
    desktop_low.msaa = MSAA_2X;
    desktop_low.shadow_size = 2048;
    desktop_low.soft = SHADOW_QUALITY_SOFT_LOW;
    desktop_low.glow = 0.6;
    desktop_low.scale = 1.0;
    d.set("desktop_low", desktop_low);
    Preset desktop_high;
    desktop_high.msaa = MSAA_4X;
    desktop_high.shadow_size = 4096;
    desktop_high.soft = SHADOW_QUALITY_SOFT_HIGH;
    desktop_high.glow = 0.6;
    desktop_high.scale = 1.0;
    desktop_high.ssao = true;
    desktop_high.ssil = true;
    d.set("desktop_high", desktop_high);
    return d;
}();

// The settings of the preset in effect.
RenderQuality::Preset RenderQuality::current() {
    return PRESETS.get(GameSettings::read_preset(DEFAULT_PRESET), PRESETS.at("desktop_low"));
}

void RenderQuality::apply_environment(const gd::Ref<Environment>& env) {
    Preset p = current();
    env->glow_enabled = true;
    env->glow_intensity = static_cast<float>(p.glow);
    env->ssao_enabled = p.ssao;
    env->ssao_intensity = 1.2f;
    env->ssao_radius = 0.6f;
    env->ssil_enabled = p.ssil;
}

void RenderQuality::apply_light(const gd::Ref<DirectionalLight3D>& light) {
    Preset p = current();
    gd::Int soft = p.soft;
    light->shadow_blur = soft >= SHADOW_QUALITY_SOFT_HIGH ? 1.5f : 1.0f;
    light->shadow_bias = static_cast<float>(p.shadow_bias);
    light->shadow_normal_bias = static_cast<float>(p.shadow_normal_bias);
}

// The window's viewport and the shadow atlas.
void RenderQuality::apply_viewport(ViewportQuality& viewport) {
    Preset p = current();
    viewport.msaa_3d = p.msaa;
    double scale = p.scale;
    viewport.scaling_3d_scale = scale;
    // FSR is Forward+ only: the Mobile renderer scales bilinearly.
    bool fsr = scale < 1.0 && GameSettings::renderer() == "forward_plus";
    viewport.scaling_3d_mode = fsr ? SCALING_3D_MODE_FSR : SCALING_3D_MODE_BILINEAR;
    // RenderingServer.directional_shadow_atlas_set_size(p["shadow_size"], true)
    viewport.directional_shadow_atlas_size = p.shadow_size;
    viewport.directional_shadow_16_bits = true;
    // RenderingServer.directional_soft_shadow_filter_set_quality(p["soft"])
    viewport.directional_soft_shadow_filter_quality = p.soft;
}

// `base` for the Textures setting: itself (smooth), or compiled with CRISP defined.
gd::Ref<Shader> RenderQuality::shader(const gd::Ref<Shader>& base) {
    if (GameSettings::read_text("texture_filter") != "crisp") return base;
    if (!_crisp.has(base->path)) {
        // The remake inserts CRISP_DEFINE after the `shader_type` line of a copy of the code; the
        // gdshader compiler takes the define instead (Shader::with_define).
        gd::Ref<Shader> variant = base->with_define("CRISP");
        _crisp[base->path] = variant;
    }
    return _crisp[base->path];
}

}  // namespace t3

// remake/tests/presentation/test_stage_lighting.gd: the stage's light rig from its record: the
// fighters' parameters (StageLighting.fighter_parameters, stages.md#lighting) and the environment
// each view's camera carries.
//
// Not ported: test_view_takes_the_frames_back_colour and test_fighter_meshes_are_on_the_fighters_layer
// (they need FighterView, not translated yet).
#include <algorithm>
#include <cmath>

#include "content/stage_data.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/fighter_animation.h"
#include "dev/record_trace.h"
#include "gd/io.h"
#include "platform/log.h"
#include "presentation/render_quality.h"
#include "presentation/stage/stage_lighting.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

gd::Ref<StageData> _stage(gd::Int number) {
    gd::Ref<StageData> s = gd::make<StageData>();
    s->number = number;
    s->ambient = 137;
    s->base_colour = {126.0f / StageData::BASE_NEUTRAL, 126.0f / StageData::BASE_NEUTRAL, 128.0f / StageData::BASE_NEUTRAL};
    gfx::Color c = gfx::Color::c8(234, 220, 188);
    s->light_colour = {c.r, c.g, c.b, c.a};
    s->light_pitch = 512;
    s->light_yaw = 2560;
    return s;
}

// Godot's Color.r8.
gd::Int r8(float channel) { return static_cast<gd::Int>(std::clamp(std::round(channel * 255.0f), 0.0f, 255.0f)); }

// Godot's is_equal_approx for floats (CMP_EPSILON 0.00001, scaled by the magnitude).
bool is_equal_approx(double a, double b) {
    if (a == b) return true;
    double tolerance = 0.00001 * std::fabs(a);
    if (tolerance < 0.00001) tolerance = 0.00001;
    return std::fabs(a - b) < tolerance;
}

bool is_equal_approx(const Vector3& a, const Vector3& b) {
    return is_equal_approx(a.x, b.x) && is_equal_approx(a.y, b.y) && is_equal_approx(a.z, b.z);
}

}  // namespace

T3_TEST(stage_lighting, gte_units) {
    StageLighting::FighterParameters p = StageLighting::fighter_parameters(_stage(4));
    t.expect_equal(p.back_colour, Vector3::ONE() * static_cast<float>(137 * 16 / 4096.0), "SetBackColor: ambient << 4");
    t.expect_equal(StageLighting::back_colour(250, 0, 0), Vector3(static_cast<float>(250 * 16 / 4096.0), 0, 0),
                   "the FREEZE SIGNAL's red");
    t.expect_equal(p.light_colour, Vector3(234, 220, 188) / 256.0f, "colour matrix column 0: byte << 4");
    t.expect_equal(p.fill_colour, Vector3(117, 110, 94) / 256.0f, "the light from below: (byte >> 1) << 4");
    t.expect_equal(p.base_colour, Vector3(126, 126, 128) / 128.0f);
    // expect(not p.has("light_towards")): FighterParameters has no such field.
    t.expect(is_equal_approx(StageLighting::light_travel(_stage(4)),
                             Vector3(0.5f, static_cast<float>(-std::sqrt(0.5)), -0.5f)),
             "the light travels down, towards +x and -z");
}

T3_TEST(stage_lighting, no_light_from_below_on_stage_17) {
    t.expect_equal(StageLighting::fighter_parameters(_stage(StageLighting::NO_FILL_STAGE)).fill_colour, Vector3::ZERO());
}

T3_TEST(stage_lighting, each_camera_carries_its_stage_environment) {
    gd::Arr<gd::Ref<Camera3D>> cameras{gd::make<Camera3D>(), gd::make<Camera3D>()};
    gd::Arr<gd::Ref<StageLighting>> rigs{gd::make<StageLighting>(), gd::make<StageLighting>()};
    gd::Ref<StageData> dark = _stage(4);
    gd::Ref<StageData> bright = _stage(5);
    bright->ambient = 175;
    rigs[0]->setup(dark, cameras[0]);
    rigs[1]->setup(bright, cameras[1]);
    t.expect(cameras[0]->environment == rigs[0]->environment && cameras[1]->environment == rigs[1]->environment, "its own");
    t.expect(cameras[0]->environment != cameras[1]->environment, "not shared between views");
    t.expect_equal(r8(cameras[0]->environment->ambient_light_color.r), gd::Int(137), "stage 4's ambient");
    t.expect_equal(r8(cameras[1]->environment->ambient_light_color.r), gd::Int(175), "the other stage's ambient");
}

// The fighter shader takes every directional light that reaches the fighters' render layer for the
// stage's main light and adds the fill light itself, so exactly one of the stage's two directional
// lights (the sun) may reach that layer.
T3_TEST(stage_lighting, only_the_sun_reaches_the_fighters) {
    gd::Ref<Camera3D> camera = gd::make<Camera3D>();
    gd::Ref<StageLighting> lighting = gd::make<StageLighting>();
    lighting->setup(_stage(4), camera);
    t.expect((lighting->sun->light_cull_mask & StageLighting::FIGHTER_LAYER) != 0, "the sun lights the fighters");
    t.expect((lighting->fill->light_cull_mask & StageLighting::FIGHTER_LAYER) == 0, "the fill light does not");
    t.expect((lighting->fill->light_cull_mask & 1) != 0, "but it lights the floor");
    gd::Int reaching = 0;
    for (const gd::Ref<Node3D>& child : lighting->get_children()) {
        gd::Ref<DirectionalLight3D> light = std::dynamic_pointer_cast<DirectionalLight3D>(child);
        if (light != nullptr && (light->light_cull_mask & StageLighting::FIGHTER_LAYER) != 0) reaching += 1;
    }
    t.expect_equal(reaching, gd::Int(1), "directional lights on the fighters' layer");
}

// The sun takes the shadow biases of the preset in effect; the web preset raises Godot's defaults
// and filters its samples against the Compatibility renderer's shadow acne (stripes of texels on
// the fighters' slanted faces with smooth normals).
T3_TEST(stage_lighting, sun_takes_the_presets_shadow_biases) {
    gd::Ref<Camera3D> camera = gd::make<Camera3D>();
    gd::Ref<StageLighting> lighting = gd::make<StageLighting>();
    lighting->setup(_stage(4), camera);
    RenderQuality::Preset p = RenderQuality::current();
    // The light keeps its parameters in single precision.
    t.expect(is_equal_approx(lighting->sun->shadow_bias, static_cast<float>(p.shadow_bias)), "depth bias");
    t.expect(is_equal_approx(lighting->sun->shadow_normal_bias, static_cast<float>(p.shadow_normal_bias)), "normal bias");
    RenderQuality::Preset web = RenderQuality::PRESETS.at("web");
    t.expect(web.shadow_bias > RenderQuality::SHADOW_BIAS, "web: a larger depth bias");
    t.expect(web.shadow_normal_bias > RenderQuality::SHADOW_NORMAL_BIAS, "web: a larger normal bias");
    t.expect(web.soft >= RenderQuality::SHADOW_QUALITY_SOFT_LOW, "web: filtered shadow samples");
}

// The back colour of a fighter against the game's FUN_8003A3B8 run in the CPU harness
// (`work/traces/back_colour.bin`, tools/research/back_colour_cases.py): every stage's light record with
// each flash count, and random records, counts and practice signals. SetBackColor's red, green and
// blue are the bytes the remake's colour is made of.
T3_TEST(stage_lighting, back_colour_matches_the_original) {
    const std::string BACK_COLOUR_FILE = "back_colour.bin";
    const gd::Int BACK_COLOUR_CASE = 14;
    if (!t.require(TraceFiles::path(BACK_COLOUR_FILE), "run tools/research/export_traces.py first")) return;
    gd::PBytes data = gd::read_file(TraceFiles::path(BACK_COLOUR_FILE));
    gd::Int count = data.decode_u32(8);
    gd::Int failed = 0;
    gd::Int flashes = 0;
    for (gd::Int n = 0; n < count; ++n) {
        gd::Int pos = 12 + BACK_COLOUR_CASE * n;
        gd::Int word = data.decode_u16(pos);
        gd::Int flash = data.decode_u8(pos + 2);
        bool practice = data.decode_u8(pos + 3) != 0;
        gfx::Color freeze = gfx::Color::c8(static_cast<int>(data.decode_u8(pos + 5)), static_cast<int>(data.decode_u8(pos + 6)),
                                           static_cast<int>(data.decode_u8(pos + 7)), static_cast<int>(data.decode_u8(pos + 4)));
        Vector3 want = StageLighting::back_colour(data.decode_u16(pos + 8), data.decode_u16(pos + 10), data.decode_u16(pos + 12));
        Vector3 got = StageLighting::fighter_back_colour(word, flash, practice ? freeze : StageLighting::NO_SIGNAL);
        if (flash != 0 && flash <= FighterAnimation::FLASH_FRAMES) flashes += 1;
        if (got != want) {
            failed += 1;
            if (failed <= 5)
                Log::info("case " + gd::str(n) + ": light word " + gd::str(word) + ", flash " + gd::str(flash) + ", practice " +
                          (practice ? "true" : "false"));
        }
    }
    t.expect_equal(failed, gd::Int(0), "cases that differ of " + gd::str(count));
    t.expect(flashes > 100, "the cases reach many flash counts (" + gd::str(flashes) + ")");
}

// The flash's precedence and its end: a burning body is black, the FREEZE SIGNAL beats a flash.
T3_TEST(stage_lighting, back_colour_precedence) {
    gfx::Color red = gfx::Color::c8(250, 0, 0, 1);
    t.expect_equal(StageLighting::fighter_back_colour(2200, FighterAnimation::LIT_BLACK, red), Vector3::ZERO(), "burning");
    t.expect_equal(StageLighting::fighter_back_colour(2200, 5, red), StageLighting::back_colour(250, 0, 0), "signal over flash");
    t.expect_equal(StageLighting::fighter_back_colour(2200, 0, StageLighting::NO_SIGNAL), StageLighting::back_colour(137, 137, 137),
                   "the stage's level: the record's word >> 4");
}

// The core's counter: a pick-up (count 1) lights 32 frames with counts 1 to 32 and then ends; the
// frame's count is the one before the step. A burning fighter shows black and holds the flash.
T3_TEST(stage_lighting, flash_counter_lights_32_frames) {
    gd::Ref<FightState> fight = gd::make<FightState>(nullptr, RuleSet::original());
    gd::Ref<FighterState> f = fight->fighters[0];
    fight->health_flash[0] = 1;
    gd::Arr<gd::Int> seen;
    for (gd::Int frame = 0; frame < FighterAnimation::FLASH_FRAMES + 3; ++frame) {
        FighterAnimation::flash_step(fight, f);
        seen.append(fight->flash_level[0]);
    }
    gd::Arr<gd::Int> expected;
    for (gd::Int count = 1; count < FighterAnimation::FLASH_FRAMES + 1; ++count) expected.append(count);
    expected.append_array(gd::Arr<gd::Int>{0, 0, 0});
    t.expect(seen == expected, "the counts the fighter is lit with");
    t.expect_equal(gd::Int(fight->health_flash[0]), gd::Int(0), "ended");
    t.expect_equal(gd::Int(fight->flash_level[1]), gd::Int(0), "the other fighter is not lit");
}

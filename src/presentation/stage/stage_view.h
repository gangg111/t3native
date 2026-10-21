#pragma once
// remake/presentation/stage/stage_view.gd
// Draws a stage: the distant panorama and the tiled floor.
//
// The game draws the panorama with one eighth of the camera's displacement from the
// fighters' midpoint and a per-stage height (StageBackgroundDraw). In world terms that is
// the panorama scaled by 8 around the midpoint, raised to that height, which is how it is
// placed here.
// The whole panorama is drawn: the game's visibility cells only spare it the hidden objects. It
// turns about the midpoint with the camera's sideways slides (BackdropTurn): the game turns its
// objects by Ry(-angle), which is a turn by +angle about Godot's y (WorldSpace's axes).
//
// A kind-0 floor darkens with the distance past the fighters (FloorDrawGrid, stages.md#floor):
// per pixel here, from the depth beyond the camera's distance to the fighters' midpoint.
//
// With the Stage backdrops setting on Arcade and the stage's arcade version converted
// (ArcadeStageData), the arcade's scene, sky and floor are drawn instead
// (docs/research/arcade/stages.md): the scene is placed like the panorama, scaled by
// 10 instead of 8, with stages 6 and 12's texture animation and stages 3 and 11's animated props
// (step: the carousel, and the helicopter after a round's end and in replays); the sky is the
// environment's background (apply_environment), and the floor is the arcade's pattern of
// 2,500-unit tiles, 10 x 10 around the fighters as in the game, with the scene's ground beyond
// it; its square is centred on the midpoint and turned with the scene, not snapped to the tiles
// in world axes as in the arcade (game-bugs.md #67: the corners showed the sky). Every tile shows its quarter-tile texture: the arcade's own budget of 11 split tiles a frame
// (FUN_801A941C, the others at half the resolution) is not reproduced.
//
// TODO: arcade stages (no arcade data): the converted data is the PlayStation disc's only
// (manifest "arcade": false), so StageData.arcade is always null. The arcade branches keep their
// call sites and go to `_arcade_stub`, which logs once and draws nothing; ArcadeProps and the
// arcade scene and sky shaders are not ported.
#include <string>

#include "gd/gd.h"
#include "gd/json.h"
#include "gfx/texture.h"
#include "gfx3d/nodes.h"

namespace t3 {

class StageData;
class ArcadeStageData;
class ArcadeProps;

class StageView : public Node3D {
public:
    static inline const gd::Ref<Shader> PANORAMA_SHADER = Shader::load("res://presentation/stage/panorama.gdshader");
    static inline const gd::Ref<Shader> FLOOR_SHADER = Shader::load("res://presentation/stage/floor.gdshader");
    static constexpr double PANORAMA_PARALLAX = 8.0;
    static constexpr gd::Int FLOOR_PATTERN = 10;        // the game draws 10 x 10 tiles around the fighters
    // How far the floor reaches past the panorama's footprint (game units): the walls stand
    // on it, their base below the ground, so the floor edge is never visible.
    static constexpr gd::Int FLOOR_UNDER_WALLS = 500;
    static constexpr gd::Int TILED_FLOOR_HALF = 60000;
    static inline const std::string FLOOR_FADE_FILE = "floor_fade.json";
    // TODO: arcade stages (no arcade data): these shaders are not in t3native/shaders (never drawn).
    static inline const gd::Ref<Shader> ARCADE_SCENE_SHADER = Shader::load("res://presentation/stage/arcade_scene.gdshader");
    static inline const gd::Ref<Shader> ARCADE_SKY_SHADER = Shader::load("res://presentation/stage/arcade_sky.gdshader");
    static const gd::Dict<std::string, gd::Int> SKY_KINDS;   // {"tiles": 0, "gradient": 1, "fill": 2}
    static constexpr gd::Int ANGLE_HALF = 2048;         // 4096-unit angles wrap to -2048 ... 2047
    static constexpr gd::Int FADE_SCALE = 0x4000000;    // FloorDrawGrid: the table index is depth * (FADE_SCALE / fade) >> 16
    // Godot's Node3D notifications (_notification).
    static constexpr gd::Int NOTIFICATION_ENTER_TREE = 10;
    static constexpr gd::Int NOTIFICATION_VISIBILITY_CHANGED = 43;

    static inline gfx::TextureRef _fade_table;
    static inline gfx::TextureRef _arcade_fade_table;
    static inline gd::PI32 _arcade_sine;

    gd::Ref<MeshInstance3D> panorama;
    gd::Ref<MeshInstance3D> floor_mesh;
    gd::Ref<ArcadeStageData> arcade;          // the arcade version drawn, or null
    gd::Ref<ShaderMaterial> sky_material;     // the arcade's sky, or null
    gd::Ref<StageData> _stage;
    gd::Int _steps = 0;                       // simulation steps since setup (the texture animation's clock)
    gd::Ref<ArcadeProps> props;               // the arcade's animated props, or null
    gd::Arr<gd::Ref<MeshInstance3D>> prop_nodes;
    gd::Int _round = -1;                      // the round the props were started for
    gd::Ref<Environment> _environment;        // the view's environment, its background the sky while shown
    gd::Int _backdrop_turn = 0;               // the backdrop's turn of the last step (the sky turns with it)

    StageView();

    // The arcade version to draw for `stage` (the Stage backdrops setting), or null.
    static gd::Ref<ArcadeStageData> arcade_of(const gd::Ref<StageData>& stage);
    void setup(const gd::Ref<StageData>& stage);
    // Keeps the panorama and the floor around the fighters' midpoint (Godot units, x and z): the
    // game centres its floor grid under the fighters every frame, so the floor never ends. The
    // floor's pattern comes from the world position and stays in place. The arcade's floor (10 x 10
    // tiles around the tile under the midpoint, FUN_801A941C) is centred on the midpoint here and
    // turns with the scene (set_backdrop_turn), so its edge stays under the scene's walls
    // (game-bugs.md #67).
    void follow(const Vector3& midpoint);
    static gd::Ref<ArrayMesh> _panorama_mesh(const gd::Ref<StageData>& stage);
    // The arcade scene or a prop (arcade.py's scene.bin format): positions, atlas texels, the
    // texture window in CUSTOM0 and the colour factor in CUSTOM1, as floats (ARRAY_COLOR would
    // store 8 bits per channel, too coarse for the dark linear colours of untextured packets).
    static gd::Ref<ArrayMesh> _arcade_mesh(const gd::Packed<float>& v, gd::Int stride);
    // One simulation step (the camera's yaw, and in fights the round's number and state):
    // stages 6 and 12 copy the next frame into their animated texture every `period` steps, cycling
    // (FUN_801E2114, called every frame of the stage's display); the loaded picture shows until the
    // first copy. The props start again with each round (FUN_801E3D94: the helicopter's mode from
    // the first player's attack buttons held then, `held`, else the step counter's low bits as the
    // arcade's counter); the carousel turns every step, the helicopter flies only while shown: in
    // the replay and from the win poses on (FUN_801E2A2C).
    void step(gd::Int camera_yaw = 0, gd::Int round_number = -1, gd::Int round_state = -1, gd::Int held = 0);
    // The props' meshes under the scene (FUN_801E2A68 draws them with the scene's matrices): the
    // carousel's parts side by side, the helicopter's rotor on its body.
    void _build_props(const gd::Ref<StageData>& stage);
    // The props' transforms from their SVECTOR angles and positions: libgte RotMatrix (Rx Ry Rz)
    // for the carousel, RotMatrixYXZ (Ry Rx Rz) for the helicopter (both checked in the CPU
    // harness), in the game's y-down frame turned into Godot's.
    void _place_props();
    static gd::PI32 _arcade_sine_table(const std::string& stages_dir);
    // The floor's kind of this stage as drawn (stages.md#floor; the arcade's own when it is drawn).
    gd::Int floor_kind() const;
    // The camera rig's coverage: the arcade's sky surrounds the camera, so it covers every view.
    gd::PI32 coverage() const;
    // Shows the arcade's sky as `environment`'s background (no sky in True Ogre fights, which
    // clear to black in the arcade too: FUN_801A7688), only while this view is shown: a hidden view's
    // camera may still be the viewport's (the attract demonstration's, set up at boot), and between
    // the 2D screens the background must stay the clear colour.
    void apply_environment(const gd::Ref<Environment>& environment, bool true_ogre = false);
    // Godot calls this on NOTIFICATION_VISIBILITY_CHANGED and NOTIFICATION_ENTER_TREE; here the
    // app calls it when it shows or hides the view.
    void _notification(gd::Int what);
    void _show_sky();
    // Every rendered frame: the arcade sky's layout for `camera`, whose original 4:3 frame spans
    // +-frame_tan in view-plane units (CameraRig.frame_tan). FUN_801A7688 scrolls the picture by the
    // camera's yaw (minus the backdrop's turn) and pitch: the frame's left edge shows strip pixel
    // ((-yaw) & 0xFFF) 512 / step, and the top row lies -pitch k / 256 + offset lines down the
    // 480-line frame; stages 0, 3, 5 and 11 keep it at or above the frame's top, 11 not more than 192
    // lines above.
    void place_sky(const gd::Ref<Camera3D>& camera, gfx::Vec2 frame_tan);
    void _sky_parameters();
    // An RGB fill (enabled: alpha 1) or none (alpha 0).
    static gfx::Color _fill(const gd::Json& value);
    // Where the floor ends (the tile-map stages' backdrop covers the rest): a Godot-space line
    // dot(xz, (x, y)) = z.
    void set_floor_edge(const Vector3& edge);
    // The spotlight floor of this step (kind 0: none): the light points in Godot space.
    void set_spotlight(gd::Int kind, const Vector3& a, const Vector3& b, float k, float limit);
    // Chooses the floor picture: the exact pattern or the one with smoothed tile seams (the arcade's
    // with `a`).
    static std::string floor_texture_path(const gd::Ref<StageData>& stage, bool smooth,
                                          const gd::Ref<ArcadeStageData>& a = nullptr);
    // Half the side of the floor square (game units). The game's floor is a 10 x 10-tile grid
    // (+-9,000 units) that fades out, leaving gaps before the panorama walls (+-9,600 units in
    // stg_e, more in the corners). Here the floor reaches under the walls instead.
    static gd::Int floor_half_extent(const gd::Ref<StageData>& stage, const gd::Ref<ArcadeStageData>& a = nullptr);
    // One quad under the whole panorama. The floor shader derives the texture coordinates
    // from the world position, so the pattern repeats without per-tile seams.
    static gd::Ref<ArrayMesh> _floor_mesh(const gd::Ref<StageData>& stage, const gd::Ref<ArcadeStageData>& a);
    // FloorDrawGrid's shading table (stages/floor_fade.json) as a 1-texel-high picture of its bytes.
    static gfx::TextureRef _floor_fade_table(const std::string& stages_dir);
    // The arcade floor's table (FUN_801A941C, stages/arcade_floor_fade.json).
    static gfx::TextureRef _arcade_floor_fade_table(const std::string& stages_dir);
    static gfx::TextureRef _table_texture(const std::string& path);
    // The panorama's turn of this step (BackdropTurn.angle, 4096 units); the arcade's sky turns
    // with it (FUN_801D8FD0 draws it with the camera's yaw minus the turn: place_sky), and so does its floor's
    // outline (the pattern stays in world axes: game-bugs.md #67).
    void set_backdrop_turn(gd::Int angle);
    // The camera's horizontal distance to the fighters' midpoint (game units) of this step, from
    // which the floor's shading starts.
    void set_floor_distance(float distance);
    static void _floor_textures(const gd::Ref<ShaderMaterial>& material, const gd::Ref<StageData>& stage,
                                const gd::Ref<ArcadeStageData>& a);
    void _panorama_textures();
    // The Textures or Texture pack setting changed: the shaders and pictures again.
    void _on_setting_changed(const std::string& key);

    // TODO: arcade stages (no arcade data): where an arcade branch would draw; logs once.
    static void _arcade_stub(const std::string& where);
};

}  // namespace t3

#pragma once
// remake/presentation/fighter/fighter_view.gd
// Draws one fighter from its converted model and the joint frames of the simulation.
// The mesh keeps part-local positions; the fighter shaders skin them on the GPU.
//
// The hands are separate meshes, one per vertex variant, swapped as the simulation's hand
// channels select variants; so are an arcade True Ogre's wings and tail (`set_hands`). The eyes
// close by swapping the atlas for the one with the closed-eye rectangle copied over the eyes (the
// game's blink copies it within VRAM).
// Between two simulation steps the joint rows are interpolated for high refresh rates.
#include <string>
#include <vector>

#include "content/character_model.h"
#include "gd/gd.h"
#include "gfx/texture.h"
#include "gfx3d/math3d.h"
#include "gfx3d/nodes.h"
#include "gfx3d/resources.h"
#include "presentation/fighter/gaze_atlas.h"

namespace t3 {

class ArcadeAttachments;
class ArcadeFace;
class CurvedMesh;
class FighterHands;
class JointFrame;

class FighterView : public Node3D {
public:
    // preload("res://presentation/fighter/fighter.gdshader") and fighter_double_sided.gdshader:
    // functions (one object each, loaded on first use) so no static initialiser runs Shader::load.
    static const gd::Ref<Shader>& SHADER();
    static const gd::Ref<Shader>& SHADER_DOUBLE_SIDED();
    static constexpr gd::Int JOINT_SLOTS = 22;
    static constexpr gd::Int STICK_JOINT = 18;      // Mokujin's stick (attachment part 18)
    static constexpr gd::Int OVERHEAD_SEEN = 4;     // the value FUN_80034354 leaves in Gon's palette cache (fighter +0x1291)
    // The mesh is stored in part-local units, so the engine cannot derive its bounds; they are
    // rebuilt every frame from the joint positions, grown by the largest reach of a part.
    static constexpr float BOUNDS_MARGIN = 0.8f;
    static constexpr gd::Int HALF_RGBA_BYTES = 8;   // a CUSTOM_RGBA_HALF attribute per vertex
    static constexpr Vector3 NO_BACK_COLOUR = Vector3(-1.0f, -1.0f, -1.0f);   // set_back_colour not called yet: the stage's

    gd::Ref<CharacterModel> model;
    gd::Arr<gd::Ref<ShaderMaterial>> materials;     // single-sided, double-sided
    gd::Ref<MeshInstance3D> mesh_instance;
    gd::Arr<gd::Ref<MeshInstance3D>> hand_instances;
    gd::Arr<gd::Arr<gd::Ref<ArrayMesh>>> hand_meshes;   // per hand: ArrayMesh per variant
    gd::PI32 hand_channels;
    gd::Arr<gd::Ref<MeshInstance3D>> wing_instances;
    gd::Arr<gd::Arr<gd::Ref<ArrayMesh>>> wing_meshes;   // per wing group: ArrayMesh per variant
    gd::PI32 wing_channels;
    gfx::TextureRef texture_open;
    gd::Dict<gd::Int, gfx::TextureRef> eye_textures;     // eye shape (int) -> Texture2D, the shapes the model has
    gfx::TextureRef texture_swap;                   // Gon's atlas with his head palette swapped, else null
    gd::Ref<GazeAtlas> gaze_atlas;                  // Gon's atlas with his eyes' look direction, else null
    gd::Ref<ArcadeFace> face;                       // an arcade model's face changes, null for the PlayStation's blink
    gd::Int _eye_shape = CharacterModel::OPEN_SHAPE;
    gfx::TextureRef _albedo;                        // the atlas the materials have (_show_atlas)
    bool _swapped = false;                          // Gon's head palette is the swapped one (_step_palette)
    gd::Int _swap_seen = 0;                         // the variant PartSelectVertexVariant last swapped for (fighter +0x1291)
    bool _overhead = false;                         // the overhead KO camera (0x800B08D4): the palette follows set_overhead
    gd::PI32 _gaze_cache{0, 0};                     // the shift each eye last copied (fighter +0x1292, +0x1293)
    gd::PI32 _gaze_shifts{GazeAtlas::NO_SHIFT, GazeAtlas::NO_SHIFT};   // what each eye's strip shows
    gd::Int _hidden_joints = -1;                    // the mask the materials have, -1 unknown
    gd::Dict<std::string, ShaderValue> _lighting;   // the stage's light rig parameters (set_lighting)
    Vector3 _back_colour = NO_BACK_COLOUR;          // this frame's back colour (set_back_colour), or the stage's
    std::vector<Vector4> _rows;                     // PackedVector4Array
    std::vector<Vector4> _previous_rows;
    AABB _bounds;
    gd::Ref<CurvedMesh> _curved;                    // the curved grids' corners, null for flat triangles
    gd::PI32 _hand_variants{0, 0};
    gd::PI32 _wing_variants{0, 0};
    gd::Ref<ArcadeAttachments> _arcade;             // an arcade model's attachment joints, else null
    // The flat surfaces' mesh arrays (Surface -> Array): the view's own, or one its fight shares
    // (FightView: a model swapped in again, Tekken Force's enemies, does not split its corners again).
    gd::Dict<gd::Ref<CharacterModel::Surface>, MeshArrays> array_cache;

    FighterView();

    void setup(const gd::Ref<CharacterModel>& character);
    // The body, hand and wing meshes: the converter's triangles, or with the Shading setting's
    // "curved" the grids CurvedMesh lifts onto curved patches.
    void _build_meshes();
    // A mesh instance showing the first variant of `group`, with a mesh per variant (the group's
    // `select` repeats the meshes of equal variants), added to the given lists (its pose value's index
    // in `channels`).
    void _add_variants(const gd::Ref<CharacterModel::VariantGroup>& group, const gd::Arr<gd::Ref<MeshInstance3D>>& instances,
                       const gd::Arr<gd::Arr<gd::Ref<ArrayMesh>>>& meshes_of, const gd::PI32& channels);
    void _load_textures();
    // The Textures or Texture pack setting changed: the shaders and atlases again.
    // (Godot: Settings.changed; the app calls it once Settings is ported.)
    void _on_setting_changed(const std::string& key);
    gd::Ref<ArrayMesh> _mesh(const gd::Arr<gd::Ref<CharacterModel::Surface>>& surfaces);
    // The vertex variants a fighter's hand channels select: the PlayStation's models' by
    // FighterHands.variant, an arcade model's hands (or jaw) by the arcade's own numbers of the
    // channels' values, and an arcade True Ogre's wings and tail by the wing sequence the simulation
    // steps (the arcade's tables, the same variants) and his level channel (ArcadePoses).
    void set_hands(const gd::Ref<FighterHands>& hands);
    // The vertex variant each hand channel shows (FighterHands.variant).
    void set_hand_variants(const gd::PI32& variants);
    // The vertex variant each wing group shows: the wings' (channel 0), the tail's (channel 1).
    void set_wing_variants(const gd::PI32& variants);
    static void _show_variants(const gd::Arr<gd::Ref<MeshInstance3D>>& instances,
                               const gd::Arr<gd::Arr<gd::Ref<ArrayMesh>>>& meshes_of, const gd::PI32& channels,
                               const gd::PI32& variants);
    // The stage light rig the fighter is lit by (StageLighting.fighter_parameters).
    void set_lighting(const gd::Dict<std::string, ShaderValue>& parameters);
    // The back colour the fighter is lit with this frame (StageLighting.fighter_back_colour: the
    // pick-up flash, practice's FREEZE SIGNAL, a burning body), in GTE units of 4096, replacing the
    // stage's (set_lighting) from the first call on.
    void set_back_colour(const Vector3& colour);
    void _apply_lighting();
    // The Shading setting's uniforms (fighter_skin.gdshaderinc): the game's own normals or the
    // smooth ones, and the curved grids' corners.
    void _apply_shading();
    // The PlayStation's blink: the closed-eye atlas while `closed`.
    void set_eyes_closed(bool closed);
    // An arcade model's face for one simulation step (ArcadeFace.step).
    void step_face(bool held, bool down);
    // An attack shout of voice id `code` (SimEvents SHOUT) changes an arcade model's face.
    void shout(gd::Int code);
    // The atlas with eye shape `shape` over the eyes (the eyes as loaded when the model lacks it).
    void set_eye_shape(gd::Int shape);
    // PartSelectVertexVariant's palette swap for Gon: it runs for every row of both hand channels
    // with that channel's variant (the head's rows 19 and 20 are channel 0's for Gon), and when the
    // variant differs from the last it swapped for (fighter +0x1291, 0 when the fight is set up)
    // copies his head's palette from the swapped one for the model's `swap_variants`, else back
    // from the one FUN_800342a0 saved: channel 1's variant, called last, decides.
    void _step_palette(const gd::PI32& variants);
    // GonEyesSetOffset: Gon's eyes' look direction (`FightState` body.gon_gaze, -17 to 17): eye 0 takes
    // the shifts from 0 up, eye 1 those below, the other staying at 0; an eye copies its strip when its
    // shift differs from the one it last copied (so one that was never copied keeps the strip as loaded
    // while the look stays at 0).
    void set_gaze(gd::Int offset);
    // Under the overhead KO camera (0x800B08D4: `overhead`) PartSelectVertexVariant leaves Gon's
    // palette alone; FUN_80034354 swaps it, once, on his moves with flag 0x40000 (`react_chain`), and
    // nothing copies it back before the next fight is set up. Call before set_hands.
    void set_overhead(bool overhead, bool react_chain);
    // The atlas for the eye shape, Gon's head palette and his eyes' look direction: the swapped palette
    // over the eye shapes (Gon has none), the pasted patches over either.
    void _show_atlas();
    // The wind on an arcade model's swinging attachments (ArcadeWind.vector), felt from the next
    // set_joints on.
    void set_wind(const gd::PI32& vector);
    // Hides the faces skinned to the joints of bit mask `mask` (a part the game stops drawing).
    void set_hidden_joints(gd::Int mask);
    // Applies the world joint frames of a simulation step (game units), and an arcade model's
    // attachment joints built from them. The view node itself stays at the origin: the joints carry
    // the fighter's position. `snap` drops interpolation, and an arcade model's attachments start
    // again from their rest (the fighter appears: their last step is stale).
    void set_joints(const gd::Arr<gd::Ref<JointFrame>>& joints, bool snap = false);
    // Draws the pose between the previous step (0) and the current one (1).
    void show_between(double weight);
    static gd::Int _format();
    // Splits the converter's interleaved corners (pa, pb, wb, n, uv, bones, the smooth normal's
    // halves s_own and s_other, bone_other, the third term pc, wc, bone_c) into mesh arrays: CUSTOM1.w
    // is bone_c + wc, UV2 and CUSTOM3.w hold pc; s_own in half precision.
    static MeshArrays _arrays(const gd::Packed<float>& v, gd::Int stride);
};

}  // namespace t3

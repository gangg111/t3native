#pragma once
// remake/content/character_model.gd
// A converted character model: the 22 draw parts and the seam-blended mesh surfaces.
// See tools/remake_import/character.py for the mesh layout. An arcade model
// (tools/remake_import/arcade_character.py) keeps the PlayStation's parts, which the fight builds
// its joints from, and adds its own attachments on joints ARCADE_JOINT and up.
#include <memory>
#include <string>

#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class CharacterModel {
public:
    static constexpr gd::Int PART_COUNT = 22;
    static constexpr gd::Int ARCADE_JOINT = 22;   // the first arcade attachment's joint
    static constexpr gd::Int JOINT_COUNT = 28;    // the joints a mesh may be skinned to
    static constexpr gd::Int OPEN_SHAPE = 0;      // the eye shapes: the eyes as loaded
    static constexpr gd::Int CLOSED_SHAPE = 1;    // and the PlayStation's blink (the arcade's shapes are ArcadeEyes')
    // The floats of a corner's three positions (pa, pb, pc) in the mesh, which a packed variant group
    // patches (character.POSITION_FLOATS). Untyped constant Array of ints, indexed.
    static inline const gd::Arr<gd::Int> POSITION_FLOATS{0, 3, 23};
    static constexpr gd::Int POSITION_BYTES = 12;   // a position in the pose tables: three float32
    static constexpr gd::Int REF_BYTES = 12;        // a corner's three vertex numbers in the pose refs: three int32

    class Part {
    public:
        gd::Int row = 0;
        gd::Int parent = 0;          // parent draw part, -1 for the root part
        gd::Vector3i offset;         // KMD joint offset; the joint translation is (x, y, -z)
        gd::Int joint = 0;           // matrix slot of the part
        bool drawn = false;
        gd::PI32 rest;               // attachments: rest Euler angles (16-bit units)
        gd::PI32 dynamics;           // attachments: FUN_80037F10's record, empty when static
    };

    // An arcade attachment (rows 18-23 of the arcade model, FUN_8019b634).
    class ArcadeAttachment {
    public:
        gd::Int joint = 0;
        gd::Int parent = 0;          // parent joint (a skeleton joint or an earlier attachment's)
        gd::Vector3i offset;         // joint offset; the translation is (x, y, -z)
        gd::PI32 rest;               // rest Euler angles (16-bit units)
        bool static_record = false;  // FUN_8019b6d8's static record: the rest rotation only
        gd::PI32 record;             // the dynamics record (s16)
    };

    // An arcade costume's face (tools/remake_import/arcade_eyes.py, ArcadeFace): the eye shapes the
    // game shows for the blink, a move that closes the eyes, a lying fighter and a shout (0: the eyes
    // as loaded, NO_SHAPE: the costume lacks that shape), and the frames of each shout voice.
    class ArcadeEyes {
    public:
        static constexpr gd::Int NO_SHAPE = -1;
        gd::Int blink = 0;
        gd::Int held = 0;
        gd::Int down = 0;
        gd::Int shout = 0;
        gd::PI32 shout_frames;   // per voice code 0x2000 on, empty when the character does not shout
    };

    class Surface {
    public:
        bool double_sided = false;
        gd::Packed<float> vertices;   // PackedFloat32Array: vertex_floats per corner (raw floats via vec())
    };

    // The faces that use the vertices a posable part moves, once per vertex variant: a hand's
    // (FighterHandPoses), or the arcade True Ogre's wings and tail (arcade/README.md#true-ogres-wings).
    // `channel` indexes the pose value that selects the variant (FighterView.set_hand_variants,
    // set_wing_variants); `part` is the hand's part (the jaw's: the head's), or the wing's first row.
    // `select` maps a pose value's variant to its entry of `variants` when the converter kept only the
    // distinct ones (the arcade's hands: 41 variants, 33-38 distinct); empty: the identity.
    class VariantGroup {
    public:
        gd::Int channel = 0;
        gd::Int part = 0;
        gd::Arr<gd::Arr<gd::Ref<Surface>>> variants;   // per distinct variant: Array of Surface
        gd::PI32 select;
    };

    std::string name;
    bool arcade = false;             // converted from the arcade's model (its hands select the arcade's variants)
    gd::Int costume_slot = 0;
    gd::Int scale_percent = 0;
    gd::Int vertex_floats = 0;
    double smooth_crease_cos = 0.0;  // the shader joins a smooth normal's halves within this angle
    std::shared_ptr<void> curved_cache;   // CurvedMesh's grids of this model, built on first use (presentation)
    gd::Arr<gd::Ref<Part>> parts;
    gd::Arr<gd::Ref<Surface>> surfaces;
    std::string directory;
    std::string texture;             // atlas of the used texture regions
    gd::Dict<gd::Int, std::string> eye_textures;   // eye shape (int) -> the atlas with that shape over the eyes (PlayStation: 1 closed)
    gd::Ref<ArcadeEyes> arcade_eyes; // an arcade model's face changes, null for the PlayStation's
    std::string swap_texture = "";   // Gon's: the atlas with his head's palette swapped (PartSelectVertexVariant), else ""
    gd::PI32 swap_variants;          // the hand variants (of either channel) that swap it
    gd::Json gaze;                   // Gon's eyes' look direction (tools/remake_import/gaze.py, GazeAtlas), else empty (JSON object as read)
    gd::Arr<gd::Ref<VariantGroup>> hands;
    gd::Arr<gd::Ref<VariantGroup>> wings;
    gd::Arr<gd::Ref<ArcadeAttachment>> arcade_attachments;

    static gd::Ref<CharacterModel> load_from(const std::string& dir);
    // Native: the folder of a costume's arcade model (tools/arcade_overlay.py: `<dir>/arcade`), or ""
    // when the costume has none (Mokujin, a conversion without the arcade set).
    static std::string arcade_dir(const std::string& dir);

    // The hands' and the wings' groups together.
    gd::Arr<gd::Ref<VariantGroup>> variant_groups() const;

    static gd::Arr<gd::Ref<VariantGroup>> _groups(const gd::Json& list, const gd::PBytes& mesh, gd::Int floats);
    // The surface sets of a packed group (character.VariantGroup.packed): `base` is the first
    // variant's; the group's "poses" give every distinct variant's positions of the moving vertices
    // (`table`, per variant `vertices` float32 triples) and, per corner of `base` in order, the
    // numbers of the vertices its three positions are (`refs`, int32, -1: the position stays).
    static gd::Arr<gd::Arr<gd::Ref<Surface>>> _posed(const gd::Arr<gd::Ref<Surface>>& base, const gd::Json& poses,
                                                     const gd::PBytes& mesh, gd::Int floats);
    static gd::Arr<gd::Ref<Surface>> _surfaces(const gd::Json& list, const gd::PBytes& mesh, gd::Int floats);

    // Fighter scale in 4.12 (`value x 4096 / 100`, fighter +0x4EE).
    gd::Int scale_fixed() const;
    // Local joint translation of each matrix slot (the game stores (x, y, -z)).
    gd::Arr<gd::PI32> joint_offsets() const;
    // Parent matrix slot of each matrix slot (-1 for the root joint).
    gd::PI32 joint_parents() const;
};

}  // namespace t3

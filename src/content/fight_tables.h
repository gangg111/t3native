#pragma once
// remake/content/fight_tables.gd
// The executable's tables the fight simulation uses, converted into `imported/tables/`
// (`fight.json`, with the sine, square-root and arctangent tables of `pose.json` and
// `camera.json`, the per-costume tables of `fighter.json` and the CPU's `ai.json`). Meanings: moves.md, combat.md.
#include <string>

#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class FighterTables;
class PoseTables;
class CameraTables;
class AiTables;

class FightTables {
public:
    static constexpr gd::Int REACTION_WORDS = 21;

    // The effect objects' tables (effects.md#effect-objects).
    class EffectTables {
    public:
        gd::Arr<gd::Arr<gd::PI32>> breath_offsets;   // per player: two (x, y, z) from the head joint
        gd::Arr<gd::PI32> flame_offsets;
        gd::Arr<gd::PI32> cloud_offsets;
        gd::Arr<gd::PI32> gas_offsets;
        gd::PI32 burn_joints;
        gd::Arr<gd::PI32> gas_times;                 // (frame, speed, height)
        gd::PI32 glow_joints;                        // per descriptor joint: the glow joint
        gd::Arr<gd::Arr<gd::PI32>> hand_offsets;     // per character: glow offsets of joints 6, 10
        gd::Dict<std::string, gd::PI32> segment_frames;   // name -> u8 per frame (0: segment off)
        gd::Arr<gd::PI32> mokujin_sounds;            // (sound id, replacement code)
    };

    // PadVibrate's patterns (sound.md#vibration).
    class VibrationTables {
    public:
        gd::PI32 slots;              // per pattern: its queue slot 0-3
        gd::PI32 pulses;             // small-motor pulse mask per level (bit = frame & 7)
        gd::Arr<gd::PI32> small;     // per pattern: small-motor script steps
        gd::Arr<gd::PI32> large;     // per pattern: large-motor script steps
    };

    // A g_charVoices record (sound.md#character-voices).
    class VoiceSet {
    public:
        gd::PI32 counts;          // voices per category (ids 0x2nnn-0x5nnn)
        gd::PI32 voices;          // the sound codes of all categories in order
        gd::PI32 shouts;          // attack shout voice ids
        gd::PI32 damage_counts;   // light, heavy
        gd::PI32 damage;          // damage voice ids: light, then heavy
        gd::Int cooldown = 0;
        gd::PI32 ids;             // five more sound ids (+0x0E)
    };

    gd::PI32 sin_table;                    // g_sinTable (4096 + 1024 entries; cos from 1024 on)
    gd::PI32 atan;                         // Atan2Angle's u8 octant table
    gd::PI32 square_root;                  // FUN_8004B174's mantissas
    gd::Ref<FighterTables> fighter;
    gd::Ref<PoseTables> pose;
    gd::Arr<gd::PI32> common_branches;     // rows as MotionBank.branches
    gd::Arr<gd::PI32> sequences_a;         // window, steps... (commands 0xC00E-0xC04C)
    gd::Arr<gd::PI32> sequences_b;         // commands 0xC7FF-0xC827
    gd::PI32 builtin_attack_ids;           // ids of the 13 built-in attack descriptors
    gd::PI32 builtin_attack_offsets;       // their offsets in `builtin_blob`
    gd::PBytes builtin_blob;               // the descriptors and the bytes after them
    gd::Arr<gd::PI32> air_kinds;           // (ground offset, air move slot)
    gd::PI32 push;                         // push tables: frames, speed, 8 offsets
    gd::PI32 launch_push;
    gd::Arr<gd::PI32> reactions;           // 21 s16 per record
    gd::PI32 forced_reaction;
    gd::PI32 throw_victim_slots;
    gd::Arr<gd::PI32> close_reactions;     // (reaction, distance)
    gd::PI32 face_attack;
    gd::PI32 face_hit;
    gd::PI32 hurt_zone_joints;
    gd::PI32 body_sphere_joints;
    gd::Arr<gd::PI32> hurt_radii;          // per character, 14 radii
    gd::Arr<gd::PI32> body_radii;          // per character, 8 body sphere radii
    gd::Ref<CameraTables> camera;
    gd::PI32 health;
    gd::PI32 recovery_team;                // FUN_80051AD4: the team winner's recovery (1/256 of full) by eighths left
    gd::PI32 recovery_survival;            // by the survival fight's time (300-frame steps)
    gd::Arr<gd::PI32> reversal_lists;      // animation tags (reversal condition 0x4B)
    gd::Arr<gd::PI32> directions;          // per side: pad direction bits of numpad 1-9
    gd::Arr<gd::PI32> shake_scripts;       // camera shake pitch offsets, -128 ends
    // Array[Dictionary] of JSON objects kept as parsed: callers read `record["bank"]` with
    // JsonFile::number and `record["name"].str()`.
    gd::Arr<gd::Json> characters;          // per costume key: name, voice_set, bank, stage, music
    gd::PI32 sound_codes;                  // g_soundTable: sound code per 12-bit sound id
    gd::Arr<gd::PI32> sound_scripts;       // frame | type << 12 | code << 16, frame 0 ends
    gd::Arr<gd::Ref<VoiceSet>> voices;     // g_charVoices per voice set
    gd::PI32 impact_voices;                // FUN_80041ED8
    gd::PI32 round_voices;                 // FUN_8003DCE0: "ROUND n" by round number
    gd::PI32 result_voices;                // FUN_8003E158: 0x8009749A, by round result
    gd::PI32 strike_sounds;                // FUN_80041F4C
    gd::PI32 special_sounds;               // FUN_80041C48 case 3
    gd::Ref<EffectTables> effects;         // EffectTables.new() (constructor)
    gd::Ref<VibrationTables> vibration;    // VibrationTables.new() (constructor)
    gd::Ref<AiTables> ai;

    FightTables();

    static gd::Ref<FightTables> load_from(const std::string& folder);
    static gd::Arr<gd::PI32> _rows(const gd::Json& value);

    // FUN_800364E8 / FUN_80036518: a per-costume face shape, 0 beyond the tables.
    gd::Int face_shape(const gd::PI32& table, gd::Int slot) const;
    // The push values of push-table entry `index` (an s16 index): frames, speed, then offsets.
    gd::PI32 push_entry(gd::Int index) const;
    gd::Json character(gd::Int key) const;
};

}  // namespace t3

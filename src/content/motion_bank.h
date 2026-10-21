#pragma once
// remake/content/motion_bank.gd: a motion bank (`divmot*.bin`) as converted: move rows, the
// move-index slots, the baked 49-channel pose vectors of every animation stream, the branch rows,
// the sound and frame event lists, the attack records and the baked camera streams.
// See docs/research/formats/divmot-banks.md.
#include <memory>
#include <string>

#include "gd/gd.h"

namespace t3 {

class MoveRow;

// enable_shared_from_this: link() passes `self` to MoveRow.link as a Ref.
class MotionBank : public std::enable_shared_from_this<MotionBank> {
public:
    static constexpr gd::Int COMMON_FLAG = 0x40000000;   // slot entries / animation words in the common bank
    static constexpr gd::Int EMPTY_SLOT = 0x3FFFFFFF;
    static constexpr gd::Int POSE_CHANNELS = 49;
    static constexpr gd::Int POSE_BYTES = POSE_CHANNELS * 2;
    static constexpr gd::Int ROW_ANIM = 0;      // word index of the animation offset in a move row
    static constexpr gd::Int ROW_EVENTS = 8;    // word index of the event list index (+0x20), -1 for none
    static constexpr gd::Int ROW_FLAGS = 9;     // word index of the flags (+0x24)
    static constexpr gd::Int NO_LIST = 0xFFFFFFFF;
    static constexpr gd::Int CAMERA_CHANNELS = 7;   // eye x, y, z, target x, y, z, fight-camera weight
    static constexpr gd::Int CAMERA_BYTES = CAMERA_CHANNELS * 2;

    std::string name;
    gd::Int type_code = 0;
    gd::Arr<gd::PI64> moves;
    gd::PI64 move_index;
    gd::Dict<gd::Int, gd::Vector2i> anims;          // stream offset -> (first pose, frame count)
    gd::PBytes poses;
    gd::PI32 events;          // section 4 as u16 words: (frame, command) pairs, 0-ended
    gd::Arr<gd::PI32> camera_choices;               // section 7: (kind, weight, script)
    gd::Dict<gd::Int, gd::Vector3i> camera_streams;  // section-8 offset -> (first sample, frames, yaw mode)
    gd::PBytes camera_samples;
    gd::Arr<gd::PI32> branches;   // command, restriction, condition, parameter, target, flags, first, last, entry
    gd::PI32 sounds;          // section 3 as u16 words
    gd::PI32 empty_slots;     // section 5: bit s of word s / 16 set when slot s is empty
    gd::PBytes attacks;       // section 9: attack records
    gd::Dict<gd::Int, gd::Vector2i> anim_bytes;     // stream offset -> (first byte of the stream, tag)

    // A converted bank by name from a folder (`<name>.json.gz`, `.poses.bin.gz`, `.camera.bin`,
    // `.attacks.bin`).
    static gd::Ref<MotionBank> load_named(const std::string& folder, const std::string& bank_name);

    // DivmotLinkBank: every move row decoded and linked (`common` is the common bank, this bank
    // itself for the common bank; `builtins` the built-in attack descriptor ids). The rows refer to
    // this bank; the bank does not keep them, so that no reference cycle forms (FightContent keeps
    // them).
    gd::Arr<gd::Ref<MoveRow>> link(const gd::Ref<MotionBank>& common, const gd::PI32& builtins);

    // The first byte of a stream's header (the frame count's low byte), which DivmotLinkBank stores
    // as the move's length.
    gd::Int anim_first_byte(gd::Int offset) const {
        gd::Vector2i entry = anim_bytes.get(offset, gd::Vector2i(-1, 0));
        return entry.x >= 0 ? entry.x : frame_count(offset) & 0xFF;
    }
    // The s16 tag before a stream (reversal condition 0x4B).
    gd::Int anim_tag(gd::Int offset) const { return anim_bytes.get(offset, gd::Vector2i(0, 0)).y; }
    bool slot_empty(gd::Int slot) const { return ((empty_slots[slot >> 4] >> (slot & 15)) & 1) != 0; }
    bool has_anim(gd::Int offset) const { return anims.has(offset); }
    gd::Int frame_count(gd::Int offset) const { return anims.at(offset).y; }

    // Pose vector of an animation at a 0-based frame, clamped to the stream like AnimDecodePose.
    gd::PI32 pose(gd::Int offset, gd::Int frame) const;
    gd::Int move_flags(gd::Int row) const { return moves[row][ROW_FLAGS]; }
    // One frame of a camera stream as FUN_80038F38 returns it (7 channels).
    gd::PI32 camera_sample(gd::Int offset, gd::Int frame) const;
    gd::Int camera_frames(gd::Int offset) const { return camera_streams.at(offset).y; }
};

}  // namespace t3

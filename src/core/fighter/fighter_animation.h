#pragma once
// remake/core/fighter/fighter_animation.gd: FighterSkeletonUpdate (0x8002BCB8) / FighterAnimate
// (0x8003AAD0): root and pose decoding, RootReanchor, FighterTransitionBlend, RootUpdate, the hand
// poses, blinking, and FighterComposeJoints with the blend, the head look-at, True Ogre's sway and
// the costume attachments (moves.md#root-motion, moves.md#motion-blending, animation.md).
//
// Written from the verified ports root_reanchor, root_update, transition_blend,
// matrix_add_scaled and matrix_orthonormalize in tools/research/fight_sim.py and the decompiled
// FighterAnimate, FighterComposeJoints, HeadLookAt, FUN_80034354 and FUN_80033C54.
#include "gd/gd.h"
#include "core/fighter/transition.h"

namespace t3 {

class FightState;
class FightTables;
class PoseSolver;
class CameraDirector;
class CameraTables;
class FighterState;
class MoveRow;
class ViewMatrix;

class FighterAnimation {
public:
    static constexpr gd::Int HEAD_SLOT = 2;
    static constexpr gd::Int CHEST_JOINT = 1;
    static constexpr gd::Int HEAD_JOINT = 2;
    static constexpr gd::Int GAZE_HALF_TURN = 0x800;          // GonEyesFollow: the angle below which the look is to the first side
    static constexpr gd::Int GAZE_FORWARD = 0xF00;            // the angle the second side's counts from
    static constexpr gd::Int GAZE_UNITS = 0x3C;               // angle units per step of the eyes' shift
    static constexpr gd::Int GAZE_RANGE = 0x11;               // the largest shift
    static constexpr gd::Int SCREEN_JOINT = 11;
    static constexpr gd::Int FLASH_FRAMES = 0x20;
    static constexpr gd::Int LIT_BLACK = -1;               // FightState.flash_level of a burning fighter: back colour black
    static constexpr gd::Int LOOK_UP = 0x11C7;
    static constexpr gd::Int LOOK_DOWN = -0x2AAA;
    static constexpr gd::Int LOOK_STEP = 0x71C;
    static constexpr gd::Int LOOK_HOLD = 0x3C;
    static constexpr gd::Int BLINK_CLOSED = 4;
    // Untyped Array of transition codes (ints, only tested with `in`): gd::Arr<gd::Int>.
    static inline const gd::Arr<gd::Int> ENTRY_DST_CODES{Transition::CONTINUE, Transition::SLIDE_CONTINUE,
        Transition::SLIDE_TURNED, Transition::CONTINUE_TRACK_SLOW, Transition::CONTINUE_TRACK, Transition::CONTINUE_EASE,
        Transition::CONTINUE_EASE_TURNING, Transition::STEP, Transition::SIDE_STEP_CONTINUE, Transition::TO_TERMINATOR};

    // The transition blend's mode (moves.md#motion-blending): the old pose decaying into a new move,
    // or a forward blend into the pending branch move or the stance.
    enum Blend : gd::Int { NONE, DECAY, BRANCH, STANCE };

    gd::Ref<FightState> fight;
    gd::Ref<FightTables> t;
    gd::Ref<PoseSolver> solver;
    gd::PI32 look_base;           // 0x8009E900: the rotation of angles (0x4000, 0, 0x4000)
    gd::Ref<CameraDirector> camera;   // the fight's, whose `overhead` flag (0x800B08D4) Gon's face reads; null: none

    explicit FighterAnimation(const gd::Ref<FightState>& fight_state);

    // PoseSolver.build with Gameplay fix #8 as it is now: the option can change in the middle of a session.
    void _solve(const gd::PI32& pose, const gd::Arr<gd::PI32>& slots);
    // FighterSkeletonUpdate: every active fighter in index order (FUN_8003AA6C).
    void update_all(const gd::Ref<ViewMatrix>& view, bool blend_enabled, bool draw);
    // FUN_8003A3B8's counter: a lighting flash (Tekken Force's health pick-up) runs 32 frames. The
    // count lights the frame it is read in (FightState.flash_level, StageLighting.fighter_back_colour)
    // and is stepped after: counts 1 to 32 colour the fighter, then it ends.
    static void flash_step(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f);
    // FighterAnimate for one fighter.
    void animate(const gd::Ref<FighterState>& f, const gd::Ref<ViewMatrix>& view, bool blend_enabled, bool draw);
    gd::PI32 _decode_root(const gd::Ref<FighterState>& f, const gd::Ref<MoveRow>& move, gd::Int frame);
    // RootReanchor (0x8003AC1C): switching the root move does not make the body jump.
    void _reanchor(const gd::Ref<FighterState>& f);
    // RootUpdate (0x8003AD48): root matrix, blend weight and root position.
    void _root_update(const gd::Ref<FighterState>& f);

    // FighterTransitionBlend (0x8003C4C4)

    // BlendSuppressed (0x8003CAD0): move flag bit 8, +0xBA, or the bytes +0x89 and +0x8A of the word
    // at +0x88 (guardedPrev, inReaction).
    bool _blend_suppressed(const gd::Ref<FighterState>& f);
    gd::Int _decay_length(const gd::Ref<FighterState>& f);
    bool _no_blend_forward(const gd::Ref<FighterState>& f);
    gd::Int _branch_length(const gd::Ref<FighterState>& f);
    gd::Int _stance_length(const gd::Ref<FighterState>& f);
    void _transition_blend(const gd::Ref<FighterState>& f);
    // Ends a finished blend and starts a new one: the decay blend when the move changed, else a
    // forward blend into the pending move or the stance; returns its length (0: none).
    gd::Int _blend_choose(const gd::Ref<FighterState>& f);
    // The blend's source and destination poses and its length `n` (during replay playback the
    // recorded ones are kept and only the counter restarts).
    void _blend_start(const gd::Ref<FighterState>& f, gd::Int n);
    // The root and joint rotation differences: the displayed pose against the new move's first pose
    // for the decay blend, destination against source for the forward blends.
    void _blend_deltas(const gd::Ref<FighterState>& f);
    // A running blend's counter: down for the decay blend, up to its length for the forward ones.
    void _blend_count(const gd::Ref<FighterState>& f);
    void _blend_finish(const gd::Ref<FighterState>& f);
    // The 18 local rotations of a move's frame, from zeroed slots (the pose solver leaves slots
    // whose limb target is too close unchanged).
    gd::Arr<gd::PI32> _pose_slots(const gd::Ref<MoveRow>& move, gd::Int frame);

    // blinking (FUN_80034354)

    // The eyes: moves with flag 0x40000 (and lying moves without the look-at flag) keep them
    // closed; otherwise they stay open for a random odd 1 to 255 frames and closed for 4. Gon's
    // mouth follows with HandFaceCommand kind 1. The texture copies are the presentation's.
    void _blink(const gd::Ref<FighterState>& f);

    // FighterComposeJoints (0x8003B220)

    void _compose(const gd::Ref<FighterState>& f);
    // MatrixAddScaled (0x8003A980): base + sat16(weight · delta >> 12).
    static gd::PI32 _add_scaled(const gd::PI32& delta, const gd::PI32& base, gd::Int weight);
    // MatrixOrthonormalize (0x800762B0): Gram-Schmidt on the first two columns, the third their
    // cross product.
    gd::PI32 _orthonormalize(const gd::PI32& m);

    // True Ogre (FUN_80033C54)

    // The local matrix slot True Ogre draws joint `i` with: joints 5, 6, 18 and 19 (the wings and
    // tail) take the next slot of the cycle 5 → 18 → 19 → 6 → 5. ComposeJoint reads the joint offset
    // from the slot's matrix too, so these joints are also placed with the slot's offset.
    static gd::Int _ogre_slot(gd::Int i);
    // The local rotation True Ogre draws joint `i` with: joints 5, 6, 18 and 19 sway with a counter
    // and his level channel (written into the slot of _ogre_slot); the others keep their pose
    // rotation.
    gd::PI32 _ogre_local(const gd::Ref<FighterState>& f, gd::Int i, gd::Int tilt);

    // head look-at (HeadLookAt, 0x800392C8)

    // GonEyesFollow (0x80039A50): the direction of the opponent's head in the frame of Gon's own
    // head (his joint block of the last frame), as an angle in 4096 units that GonEyesSetOffset turns
    // into the pupils' shift: 0 to 17 to one side, 0 to −17 to the other. HeadLookAt leaves his yaw alone.
    void _gon_eyes_follow(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // The shift GonEyesFollow turns the opponent's head position `target` into, for Gon's head
    // frame (`rot`, `own`).
    static gd::Int gon_gaze(const gd::PI32& rot, const gd::PI32& own, const gd::PI32& target,
                            const gd::Ref<CameraTables>& tables);
    void _head_look_at(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // The three angles of the head's animated local matrix, so the look-at starts without a pop.
    gd::PI32 _head_angles(const gd::PI32& m);
    // FUN_8003A5A0: a rotation from three 16-bit angles (x at +0, y at +2, z at +4).
    gd::PI32 _angles_to_matrix(const gd::PI32& a);
};

}  // namespace t3

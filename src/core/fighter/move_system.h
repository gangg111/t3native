#pragma once
// remake/core/fighter/move_system.gd: the move lifecycle (moves.md): MoveStartAll / MoveStartOrAdvance
// with every transition code, MoveStepAll / MoveStep (distances, hit reactions, KO collapse, throw
// pairing), MoveBranchStep with BranchFind, InputMatch and BranchCondition, and MoveBranchAll with
// the reversals.
//
// Written from the verified ports in tools/research/fight_sim.py (MoveStartOrAdvance, MoveBranchStep,
// BranchCondition, InputMatch, relative angles and LaunchTrajectory in fight_math.py) and the
// decompiled MoveStartAll, MoveStep, MoveReactionBranch, BranchFindReversal, HitSelectReaction,
// AirReactionKind, FighterCollapse and ThrowVictimStart.
#include "gd/gd.h"
#include "core/fighter/tracking.h"

namespace t3 {

class FightState;
class FightTables;
class FighterState;
class MoveRow;

class MoveSystem {
public:
    static constexpr gd::Int END = 0xC000;
    static constexpr gd::Int CALL = 0xC00C;
    static constexpr gd::Int RETURN = 0xC00D;
    static constexpr gd::Int ROW_COMMAND = 0;
    static constexpr gd::Int ROW_RESTRICTION = 1;
    static constexpr gd::Int ROW_CONDITION = 2;
    static constexpr gd::Int ROW_PARAMETER = 3;
    static constexpr gd::Int ROW_TARGET = 4;
    static constexpr gd::Int ROW_FLAGS = 5;
    static constexpr gd::Int ROW_FIRST = 6;
    static constexpr gd::Int ROW_LAST = 7;
    static constexpr gd::Int ROW_ENTRY = 8;
    static constexpr gd::Int HISTORY = 60;
    static constexpr gd::Int COLLAPSE_SLOT = 0xD66;
    static constexpr gd::Int STAND_SLOT = 3;
    static constexpr gd::Int CROUCH_SLOT = 40;
    static constexpr gd::Int FACING_LIMIT = 0x4FA4;       // relAngle 112°: beyond it the fighter counts as turned away
    static inline const gd::Arr<gd::Int> TRACK_MODES{Tracking::TARGET_SLOW, Tracking::TARGET_HALF, Tracking::TARGET};
    static constexpr gd::Int SIDE_STEP_COOLDOWN = 24;
    static constexpr gd::Int THROW_TURN_FRAMES = 0x10;
    static constexpr gd::Int MAX_THROWN_RECOVERY = 300;
    static constexpr gd::Int MIN_THROWN_MASH = 34;
    static constexpr gd::Int MAX_MASH = 66;
    static constexpr gd::Int LAUNCH_AIR_KIND = 5;
    static constexpr gd::Int HIGH_ATTACK_RANGE = 0x700;
    static constexpr gd::Int FACING_EACH_OTHER = 0x2AAA;   // relAngle 60°, both fighters (reversals)

    // The fields of a reaction record (21 s16, combat.md#reactions): reaction slots standing and
    // crouching, then the push-back.
    enum Reaction : gd::Int {
        FRONT, FRONT_CROUCH, COUNTER, COUNTER_CROUCH, SIDE_1, SIDE_1_CROUCH, SIDE_3, SIDE_3_CROUCH,
        BACK, BACK_CROUCH, GUARD, GUARD_CROUCH, DOWN, DOWN_OTHER,
        PUSH_ANGLE, PUSH_HIT, UNUSED_ANGLE, PUSH_COUNTER, PUSH_ANGLE_DOWN, PUSH_DOWN, PUSH_GUARD,
    };

    // A row of a branch list: its list (a bank's branch rows or the common rows) and index.
    class Cursor {
    public:
        gd::Arr<gd::PI32> rows;
        gd::Int i = 0;

        Cursor(const gd::Arr<gd::PI32>& list, gd::Int index);

        gd::PI32 row() const { return rows[i]; }
    };

    gd::Ref<FightState> fight;
    gd::Ref<FightTables> t;

    explicit MoveSystem(const gd::Ref<FightState>& fight_state);

    // angles

    // FighterRelativeAngles (0x80043D2C).
    void relative_angles(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);

    // MoveStartAll / MoveStartOrAdvance

    // MoveStartAll (0x80045834): a pending throw (transition THROW) that reached its entry frame is
    // resolved first (one of several by the frame counter), its victim starting with THROWN.
    void start_all();
    // ThrowVictimStart (0x8002D9B0): the victim's move is the thrower's pending move's +0x32 entry.
    void throw_victim_start(const gd::Ref<FighterState>& victim, const gd::Ref<FighterState>& thrower);
    // MoveStartOrAdvance (0x8002F600).
    void start_or_advance(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // The pending move starts at its entry frame (playing backwards: at or before it), or when the
    // running move has run out.
    bool _pending_starts(const gd::Ref<FighterState>& f);
    // The per-move state cleared at a move start.
    void _reset_for_move(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, const gd::Ref<MoveRow>& move);
    // The move's damage, attack class, state, attack and guard words; the guard of a humans-only
    // guard state is dropped for the CPU.
    void _load_move_words(const gd::Ref<FighterState>& f, const gd::Ref<MoveRow>& move);
    // reactChain: consecutive reactions (and reaction-chain moves) counted, reset outside them.
    void _count_react_chain(const gd::Ref<FighterState>& f, gd::Int move_flags, gd::Int pose_flags);
    void _advance(const gd::Ref<FighterState>& f);
    void _continue_frames(const gd::Ref<FighterState>& f);
    void _clamp_to_length(const gd::Ref<FighterState>& f);
    void _recover_mash(const gd::Ref<FighterState>& f);
    void _load_push(const gd::Ref<FighterState>& f, gd::Int index);
    gd::Int _turn_step(gd::Int target, gd::Int heading, gd::Int frames);
    void _timed_turn(const gd::Ref<FighterState>& f, gd::Int target);
    void _keep_root_move(const gd::Ref<FighterState>& f);
    void _restart(const gd::Ref<FighterState>& f);
    void _anchor(const gd::Ref<FighterState>& f);
    void _clear_turn(const gd::Ref<FighterState>& f);
    // One transition case (moves.md#transition-codes); true when the common re-anchor tail runs.
    bool _transition(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int code, gd::Int reanchor);
    // Transitions HOLD_TURN and HOLD_TRACK: the pose restarts and holds for the branch window, the
    // root continues on the old root move.
    void _hold(const gd::Ref<FighterState>& f);
    // Transitions LAUNCH and HIT_AIR: the ballistic flight from the entry frame (combat.md#launches-
    // and-juggles); a launch goes into air kind 5, a juggle keeps the velocity of earlier hits.
    void _start_airborne(const gd::Ref<FighterState>& f, bool launch);
    // Transition THROW: the thrower aligns with its victim (combat.md#throws).
    void _start_throw(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // Transition THROWN: the victim turns to the thrower; the time spent down afterwards comes from the
    // thrower's move (combat.md#knock-down-recovery).
    void _start_thrown(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // The hit and guard reactions: restart pushed back along `base` + the record's angle + 180°
    // (combat.md#push-back).
    void _start_pushed(const gd::Ref<FighterState>& f, gd::Int push_field, gd::Int base, gd::Int angle_field);
    void _stop_push(const gd::Ref<FighterState>& f);
    // PushRepeatCount (0x8003159C).
    void _push_repeat_count(const gd::Ref<FighterState>& f);
    void _finish(const gd::Ref<FighterState>& f);
    // The first bytes of a move's attack descriptor (joints a0, b0, a1, b1, then two layout bytes).
    gd::PBytes _descriptor(const gd::Ref<MoveRow>& move);
    // LaunchTrajectory (0x8002F2C8): the launch of a hit or launching reaction (combat.md).
    void _launch_trajectory(const gd::Ref<FighterState>& f);

    // MoveStepAll (0x80045450): the fighters in the order of the frame counter's parity.
    void step_all();
    // MoveStep (0x800454E0).
    void step(const gd::Ref<FighterState>& f);
    // HitSelectReaction (0x8002EDF8): the defender's reaction move from its reaction record.
    void _hit_select_reaction(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& attacker);
    // AirReactionKind (0x8002F0E8): the air kind (ground offset and air move) of a juggle hit.
    void _air_reaction_kind(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& attacker);
    void _air_kind_rows(const gd::Ref<FighterState>& f);
    // FighterCollapse (0x8002DA80): a KO'd fighter falls with the collapse move 0xD66.
    void _collapse(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);

    // branches

    // MoveBranchStep (0x8002DC3C).
    void branch_step(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    gd::Int _remap(const gd::Ref<FighterState>& f, gd::Int code);
    // BranchFind (0x8002E078): the first matching row of a list, else its terminator.
    gd::Ref<Cursor> _branch_find(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp,
                                 const gd::Ref<Cursor>& start, gd::Int frame);
    // practice.ovl FUN_800B3520 (the FREEZE SIGNAL): a branch of the running move could be taken
    // now (input aside), or, near the move's end, a branch of its default continuation.
    bool can_act(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // The branch search of FUN_800B3520: window, restriction and condition (no input); for the
    // continuation (`ahead`) rows open from the first frame.
    gd::Ref<Cursor> _can_act_find(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp,
                                  const gd::Ref<Cursor>& start, bool ahead);
    bool _restriction_ok(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int value);
    // InputMatch (0x8002CE7C).
    bool _input_match(const gd::Ref<FighterState>& f, const gd::PI32& row, gd::Int direction, gd::Int pressed);
    static bool _buttons_match(gd::Int spec, gd::Int pressed, gd::Int held);
    static gd::Int _prev(gd::Int i);
    // FUN_8002CB08 / FUN_8002CBF0: forward (6) or back (4) double tap within 20 frames.
    bool _double_tap(const gd::Ref<FighterState>& f, gd::Int direction);
    // InputSequenceMatch (0x8002CCD8).
    bool _sequence_match(const gd::Ref<FighterState>& f, const gd::PI32& seq);
    // SituationClassify (0x800313B0): the throw situation code, cached per player.
    void _situation_classify(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // BranchCondition (0x8002E310), types 0x00 to 0x44.
    bool condition(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // MoveBranchAll (0x8004539C): MoveReactionBranch for both fighters by frame parity.
    void branch_all();
    // MoveReactionBranch (0x8002DB08): a hit fighter's running move may reverse or parry the hit.
    void reaction_branch(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // BranchFindReversal (0x8002E95C): the first row of the running move's list with a reversal
    // condition (0x45 to 0x4B) that matches, or null.
    gd::Ref<Cursor> _find_reversal(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // ReversalCondition (0x8002EB24), types 0x45 to 0x4B.
    bool _reversal_condition(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);
    // MoveInSlotList (the list `param` of 0x80095AB8): whether the attacker's animation tag is in it.
    bool _anim_in_list(const gd::Ref<MoveRow>& move, gd::Int list);
};

}  // namespace t3

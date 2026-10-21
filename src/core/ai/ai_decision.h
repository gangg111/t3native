#pragma once
// remake/core/ai/ai_decision.gd
// AiUpdate (0x80059B78; ai.md#per-frame-behaviour), written from the verified port
// `tools/research/ai_update.py`: the CPU fighter's held and newly pressed pad words for one frame.
// The steps run in the game's order until one produces the frame's input; the bookkeeping at
// the end always runs.
#include <functional>

#include "core/ai/cpu_opponent.h"
#include "gd/gd.h"

namespace t3 {

class AiRecord;
class AiTables;
class FightState;
class FighterState;
class MoveRow;

class AiDecision {
public:
    static inline const gd::Arr<gd::Int> FORCED_ESCAPES{0x189, 0x18E, 0x17F, 0x181};

    static constexpr gd::Int THROW_ROWS = 48;
    static constexpr gd::Int DIRECT = 0x7FFFFFFF;           // _choose: execute the marked neutral move directly
    static constexpr gd::Int ROW_COMMAND = CpuOpponent::ROW_COMMAND;
    static constexpr gd::Int ROW_CONDITION = CpuOpponent::ROW_CONDITION;
    static constexpr gd::Int ROW_TARGET = CpuOpponent::ROW_TARGET;
    static constexpr gd::Int ROW_FLAGS = CpuOpponent::ROW_FLAGS;
    static constexpr gd::Int ROW_FIRST = CpuOpponent::ROW_FIRST;
    static constexpr gd::Int ROW_LAST = CpuOpponent::ROW_LAST;
    static constexpr gd::Int ROW_ENTRY = CpuOpponent::ROW_ENTRY;
    static constexpr gd::Int ROW_RESTRICTION = CpuOpponent::ROW_RESTRICTION;
    static constexpr gd::Int HINT_NEVER = CpuOpponent::HINT_NEVER;
    static constexpr gd::Int HINT_SAFE = CpuOpponent::HINT_SAFE;
    static constexpr gd::Int HINT_CLOSE = CpuOpponent::HINT_CLOSE;
    static constexpr gd::Int HINT_MIDDLE = CpuOpponent::HINT_MIDDLE;
    static constexpr gd::Int HINT_REACH = CpuOpponent::HINT_REACH;
    static constexpr gd::Int HINT_SPECIAL = CpuOpponent::HINT_SPECIAL;
    static constexpr gd::Int GRAB = CpuOpponent::GRAB;
    static constexpr gd::Int PAD_UP = CpuOpponent::PAD_UP;
    static constexpr gd::Int PAD_FORWARD = CpuOpponent::PAD_FORWARD;
    static constexpr gd::Int PAD_DOWN = CpuOpponent::PAD_DOWN;
    static constexpr gd::Int PAD_BACK = CpuOpponent::PAD_BACK;
    static constexpr gd::Int PAD_DOWN_FORWARD = CpuOpponent::PAD_DOWN_FORWARD;
    static constexpr gd::Int PAD_DOWN_BACK = CpuOpponent::PAD_DOWN_BACK;

    // ---- Tekken Ball (volley.ovl)

    static constexpr gd::Int BALL_LINE = 0x800B6AF0LL;      // s32: player 1, player 2 and the ball along the line, its height, its lateral offset
    static constexpr gd::Int BALL_AHEAD = 0x7D0;

    gd::Ref<CpuOpponent> ai;
    gd::Ref<FightState> fight;
    gd::Ref<AiTables> t;

    // The frame's context.
    gd::Ref<AiRecord> rec;
    gd::Ref<FighterState> f;
    gd::Ref<FighterState> o;
    gd::Ref<MoveRow> sm;
    gd::Ref<MoveRow> om;
    gd::Int my_first = 0;
    gd::Int my_last = 0;
    gd::Int my_frame = 0;
    gd::Int op_first = 0;
    gd::Int op_last = 0;
    gd::Int op_frame = 0;

    explicit AiDecision(const gd::Ref<CpuOpponent>& opponent);

    // AiUpdate for CPU fighter `fighter`: (held pad, newly pressed pad).
    gd::PI32 update(const gd::Ref<FighterState>& fighter);

    void _decide();

    bool _draw(gd::Int word_index);

    // ---- bookkeeping

    void _situation();

    // Pending script steps, the disabled input modes and the alternate-frame rule.
    bool _fixed_input();

    // Per-frame bookkeeping before the decision: windows, distance bands, guard needs.
    void _track();

    void _distance_bands();

    // How long until the opponent's attack is active (+0x20A) and whether it is coming (+0x20C).
    void _opponent_attack();

    // +0x6A: the CPU already holds the guard that stops the opponent's attack level.
    gd::Int _needs_guard();

    void _guard_state();

    // ---- throws and stances

    // Throw escapes (the CPU is thrown, throwState < 0) and throw follow-ups (it throws).
    bool _throws();

    // The command pressed to escape with a row: 1 or 2 for the latched tap conditions, else the
    // row's command.
    static gd::Int _escape_press(const gd::PI32& row);

    // Up to six rows of the running throw the CPU may continue with; a few slots are tied to banks.
    gd::Arr<gd::PI32> _throw_follow_ups(const gd::Ref<MoveRow>& move);

    gd::Int _collect(bool current_only);

    // Stance moves (state 0x4C02, no active window): with probability word 49 execute a candidate
    // whose branch row flags are 0x1C or 0x1D.
    bool _stance();

    // ---- neutral actions

    // A grounded CPU in a neutral state (+0x224 bits 2 or 10): crouch-cancel rows (RECOVERED)
    // or a random action from a mask chosen by the opponent's attack timing.
    bool _crouch_and_actions();

    gd::Int _action_mask();

    static bool _side_step_or_special(const gd::Ref<MoveRow>& m, const gd::PI32& br);

    // ---- guard, new moves, follow-through

    // After the opponent whiffs, a draw marks a punish chance (+0x8F); otherwise the direction is
    // held while the guard state (+0x6A) says so.
    bool _hold_guard();

    // The move a row leads to, or null when its slot is empty in both fighters' banks (bug #61: the
    // original reads a garbage row there; the remake treats the row as leading nowhere).
    gd::Ref<MoveRow> _self_target(const gd::PI32& row);

    // Rows of a move's branch list that pass the restriction and `accept` (called with the row and
    // the move it leads to), up to `limit`.
    gd::Arr<gd::PI32> _gather(const gd::Ref<MoveRow>& move, gd::Int limit,
                              const std::function<bool(const gd::PI32&, const gd::Ref<MoveRow>&)>& accept);

    // As _gather, with the restriction tested after `pre` (the game's order of tests).
    gd::Arr<gd::PI32> _gather_ordered(const gd::Ref<MoveRow>& move, gd::Int limit,
                                      const std::function<bool(const gd::PI32&)>& pre,
                                      const std::function<bool(const gd::PI32&, const gd::Ref<MoveRow>&)>& accept);

    // When the CPU's move changed (+0x28), the follow-up lists: string continuations, planned
    // follow-ups in reach, or rows that open on frame 1 of an attack; the direction is held while a
    // list is ready. Otherwise the candidates are collected, and after 220 frames in one move any
    // of them is executed.
    bool _new_move();

    // The +0xDC list: rows in reach whose target is usable; AI-excluded targets pass only while
    // +0x208 is clear, and plain ground moves with the hint only after a draw above word 11.
    gd::Arr<gd::PI32> _planned_follow_ups(const gd::Ref<MoveRow>& move);

    // During a move usable in the air or as a special (+0x52), any open candidate is executed when
    // the opponent's attack comes later than this move's, the band is above 2 or the CPU faces
    // away; this ends the decision either way.
    bool _follow_through();

    // The opponent's attack is coming: attack first (word 19), duck or step back (word 21) or
    // side-step (word 18).
    bool _counter();

    // ---- reactions

    // The opponent's move reach bits against the band mask of `band`.
    bool _reach_ok(gd::Int band);

    bool _can_counter();

    // The reaction to the opponent's attack (jump table 0x8002317C): 0 none, 1 escape attempt,
    // 2 guard or evade, 3 evasion, 4 side step, 5-8 bank-specific counters, 9 a neutral move.
    gd::Int _reaction_kind();

    // Kind 1, or kind 9 on a 1-in-64 draw against the attack word 0x412.
    gd::Int _low_parry_or_escape();

    // LAB_8005D7A4: with `execute`, runs a marked candidate (and restarts the approach timers when
    // one was pressed); the reaction then ends the decision.
    bool _execute_marked(gd::Int count, bool execute);

    bool _react(gd::Int kind);

    // FUN_800595E4: restarts the approach timers; forward on frames with +0x02 bit 0 clear.
    gd::Int _pad_back_on_even();

    // The evasion states in +0x94: 1 pick, 3 retreat, 4 back off, 5 side step, 6 keep out (no path
    // sets 6, bug #22).
    bool _evade();

    bool _open_now(const gd::PI32& row);

    // The follow-up lists are tested for their open rows and dropped: the code that would press
    // one counts them in a register that is never incremented (bug #10). The tests still run, with
    // the side effects of their branch conditions.
    void _expire_lists();

    // Crouch-back (back against mids) when facing the opponent, else nothing;
    // restarts the approach timers.
    gd::Int _duck_or_back();

    // Outside its own attack the CPU backs off from airborne attacks at close range, turns towards
    // the opponent, or walks in (bank 7 moves differently).
    bool _avoid_air_attack();

    // ---- attacks

    // Picks and executes an attack; true ends the decision.
    bool _attack();

    // With nothing marked, far away and at 10 % health, a 1-in-4 draw and word 16 mark the
    // desperation moves (move +0x24 bit 16).
    gd::Int _last_resort(gd::Int n);

    // Marks the candidates to execute: their count (−1: input already set), or DIRECT.
    gd::Int _choose();

    // `extra`: GDScript untyped Array of attack words, always ints.
    static bool _low_or_hint(const gd::Ref<MoveRow>& m, const gd::Arr<gd::Int>& extra);

    // The opponent is down (+0x208).
    gd::Int _vs_grounded(gd::Int wait);

    static bool _hint_ground(const gd::Ref<MoveRow>& m, const gd::PI32& _br);

    // The opponent is in a stance (state 0x4C02), after a draw against word 30.
    gd::Int _vs_stance();

    // Waiting is over and nothing urgent: throws, bank setups, neutral moves, side steps or the
    // regular attack filters.
    gd::Int _approach_attack(gd::Int threshold);

    // ---- movement

    void _move();

    gd::Int _movement();

    // A random movement by distance band (or, while waiting long, by reach band).
    gd::Int _pick_movement();

    gd::Int _by_distance(gd::Int v);

    gd::Int _by_closing(gd::Int v);

    // The movements: 1 side step or walk in, 2 walk in, 3 step back, 4 side step with a long
    // approach or dash in, 5 dash in, 6 crouch dash or step back, 8 side step then a candidate.
    gd::Int _do_movement(gd::Int choice);

    // ---- the end of the frame

    static gd::Int _dec(gd::Int v) { return v >= 0 ? v - 1 : v; }

    // Timers, adaptive difficulty, guard memory and the pad output.
    gd::PI32 _finish();

    void _raise_capped(gd::Int word_index, gd::Int step, gd::Int cap);

    // Whether the opponent's move is one that hurt the CPU before: the game compares only history slot
    // 0 (bug #23); `every_entry` (RuleSet.fix_ai_damage_history) compares the whole history.
    static bool learned(const gd::Arr<gd::Ref<MoveRow>>& history, const gd::Ref<MoveRow>& move, bool every_entry);

    // The CPU lost health: after a draw against word 35 the wait shortens and, in one-on-one play,
    // the opponent's move goes into an 8-entry history; a move already there twice switches to
    // level 9 for 600 frames (the adaptive mode).
    void _learn_from_damage();

    // ---- Tekken Ball (volley.ovl)

    // FUN_800B4958: both fighters and the ball projected onto the line through the fighters. The
    // line's z intercept divides by the fighters' x difference unchecked (bug #24: with equal x the
    // R3000's quotient ±1 gives one frame of wrong positions; with the fix the line keeps its values).
    void _ball_project();

    gd::Int _ball_line(gd::Int i);

    // FUN_800B4B08: the CPU's distance to the ball on the line.
    gd::Int _ball_distance();

    // FUN_800B4B64: the ball is behind the CPU on the line: it back-dashes to get under it.
    bool _ball_behind();

    // FUN_800B4BE4: the ball is ahead of the CPU: for player 1 more than 2,000 units ahead, for
    // player 2 ahead but closer than 2,000 units (bug #3; the fix mirrors player 1's test).
    bool _ball_ahead();
};

}  // namespace t3

#pragma once
// remake/core/ai/cpu_opponent.gd
// The CPU opponent's shared machinery (ai.md), written from the verified port
// `tools/research/ai_sim.py`: the candidate table filled from the running move's branch list and its
// filters, executing a candidate as pad input, input scripts, the character hooks, and the
// record set-up (AiInitRound, AiReset, AiLoadParams). AiDecision runs AiUpdate on top of it.
//
// As in the game, one candidate table serves every CPU fighter, and `ai_self` / `ai_opp` are
// globals set by the last AiInitRound or AiUpdate (AiReset reads them, not its argument).
#include <functional>

#include "core/fighter/branch_condition.h"
#include "core/fighter/move_system.h"
#include "gd/gd.h"
#include "platform/pad_state.h"

namespace t3 {

class AiRecord;
class AiTables;
class FightState;
class FighterState;
class MoveRow;

class CpuOpponent {
public:
    static constexpr gd::Int MAX_CANDIDATES = 180;
    static constexpr gd::Int END = MoveSystem::END;
    static constexpr gd::Int COMMON = MoveSystem::CALL;
    static constexpr gd::Int SIDE_STEP = 0xC001;
    static constexpr gd::Int CROUCH_DASH = 0xC002;
    // The CPU's pad words are in its own frame (ai.md#per-frame-behaviour).
    static constexpr gd::Int PAD_UP = PadState::UP;
    static constexpr gd::Int PAD_FORWARD = PadState::RIGHT;
    static constexpr gd::Int PAD_DOWN = PadState::DOWN;
    static constexpr gd::Int PAD_BACK = PadState::LEFT;
    static constexpr gd::Int PAD_DOWN_FORWARD = PAD_DOWN | PAD_FORWARD;
    static constexpr gd::Int PAD_DOWN_BACK = PAD_DOWN | PAD_BACK;
    // The AI byte of a move's attack word (+0x0A, ai.md#move-row-hint-bits).
    static constexpr gd::Int HINT_SAFE = 0x10000;           // a safe or priority attack
    static constexpr gd::Int HINT_CLOSE = 0x40000;          // reach bits: close, middle, far (none: close or middle)
    static constexpr gd::Int HINT_MIDDLE = 0x80000;
    static constexpr gd::Int HINT_FAR = 0x100000;
    static constexpr gd::Int HINT_REACH = HINT_CLOSE | HINT_MIDDLE | HINT_FAR;
    static constexpr gd::Int HINT_SPECIAL = 0x200000;       // usable as an airborne or special move
    static constexpr gd::Int HINT_NEVER = 0x400000;         // never chosen by the AI
    static constexpr gd::Int GRAB = HINT_NEVER;             // the same bit of the opponent's move: a grab
    static constexpr gd::Int ROW_COMMAND = MoveSystem::ROW_COMMAND;
    static constexpr gd::Int ROW_RESTRICTION = MoveSystem::ROW_RESTRICTION;
    static constexpr gd::Int ROW_CONDITION = MoveSystem::ROW_CONDITION;
    static constexpr gd::Int ROW_TARGET = MoveSystem::ROW_TARGET;
    static constexpr gd::Int ROW_FLAGS = MoveSystem::ROW_FLAGS;
    static constexpr gd::Int ROW_FIRST = MoveSystem::ROW_FIRST;
    static constexpr gd::Int ROW_LAST = MoveSystem::ROW_LAST;
    static constexpr gd::Int ROW_ENTRY = MoveSystem::ROW_ENTRY;
    // Branch conditions answered from the cached situation bits (record +0x64).
    static inline const gd::Arr<gd::Int> SITUATION_CONDITIONS{BranchCondition::TURNED_AWAY, BranchCondition::TURNED_SIDE_A,
        BranchCondition::TURNED_SIDE_B, BranchCondition::HIGH_ATTACK_COMING, BranchCondition::FACING_QUADRANT_0};
    static constexpr gd::Int SIT_TURNED_AWAY = 1;
    static constexpr gd::Int SIT_SIDE_A = 2;
    static constexpr gd::Int SIT_SIDE_B = 4;
    static constexpr gd::Int SIT_HIGH_COMING = 8;
    static constexpr gd::Int SIT_FACING = 0x10;
    static inline const gd::Arr<gd::Int> SITUATION_BITS{SIT_TURNED_AWAY, SIT_SIDE_A, SIT_SIDE_B, SIT_HIGH_COMING, SIT_FACING};
    static constexpr gd::Int BANK_HWOARANG_SLOT = 0xDE3;
    static constexpr gd::Int YOSHIMITSU_STANCE = 0x456;
    static constexpr gd::Int FORCE_LEVELS = 4;              // Tekken Force's pattern words (0x80023210): levels per group
    static constexpr gd::Int FORCE_LEVEL_WORDS = 3;
    static constexpr gd::Int FORCE_GROUP_WORDS = 15;

    // _walk_to's result (GDScript: the untyped Array [found, after row, terminator row]).
    struct WalkTo {
        bool found = false;
        gd::PI32 after;        // the row after the last walked row
        gd::PI32 terminator;   // the list's terminator row (the row at the walk's stop)
    };

    gd::Ref<FightState> fight;
    gd::Ref<AiTables> t;
    gd::Ref<MoveSystem> moves;
    gd::Arr<gd::Ref<AiRecord>> records;
    // 0x800AFF24: the fighter being driven (in the mode region). GDScript has a setter: assign
    // through set_ai_self(), which also writes ModeRegion.AI_SELF.
    gd::Ref<FighterState> ai_self;
    gd::Ref<FighterState> ai_opp;              // 0x800A8B44: its target
    gd::Int multi = 0;                         // 0x8009F6A4: team, survival or Tekken Force: odd slots think every other frame
    gd::PI32 force_timers{-1, -1, -1, -1};     // 0x8009F6A8 (Tekken Force)
    // The candidate table (0x8009FD20): target move, branch row, mark (−1 unusable, 0, 1 marked).
    gd::Arr<gd::Ref<MoveRow>> cand_target;
    gd::Arr<gd::PI32> cand_row;
    gd::PI32 cand_mark;

    CpuOpponent(const gd::Ref<FightState>& fight_state, const gd::Ref<MoveSystem>& move_system);

    // The `ai_self` property setter.
    void set_ai_self(const gd::Ref<FighterState>& v);

    gd::Ref<AiRecord> record(const gd::Ref<FighterState>& f) const;

    // ---- random numbers

    // The AI generator 0x800AE168 (x = 5x + 3): returns the old value and advances it.
    gd::Int lcg_next();

    // FUN_800569D0 and the draws like it: n · ((rand() + LCG) & 0x7FFF) >> 15.
    gd::Int random_below(gd::Int n);

    // ---- branch lists

    // The move a branch row leads to through the index of AI slot `slot` (record +0x0C).
    gd::Ref<MoveRow> slot_move(gd::Int slot, const gd::PI32& row) const;

    // The rows of a branch list with the common blocks (0xC00C) expanded by their count, without
    // the terminator.
    gd::Arr<gd::PI32> list_rows(const gd::Ref<MoveRow>& move) const;

    // The branch-row restriction byte as the AI tests it (values above 0x73 never match).
    static bool restriction_ok(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int value);

    // A row's condition with the situation bits standing in for the conditions they cache.
    bool cond_ok(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int sit);

    static gd::Int situation_bit(gd::Int condition);

    // AiCollectCandidates (0x80056A4C): the rows of the running move (record +0x58) open now into
    // the table; unless `current_only`, also those opening on frame 1 of the default continuation.
    // Returns the count. Two quirks are kept: the look-ahead tests an ordinary row's condition on
    // the row after the last common block (or the list start; bug #18, fixed by
    // RuleSet.fix_ai_lookahead_row), and the stale-word test of bug #17 never matches.
    gd::Int collect_candidates(const gd::Ref<AiRecord>& rec, bool current_only);

    gd::Int _walk_lookahead(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& move, gd::Int count,
                            const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int sit);

    bool _cond_ok_lookahead(const gd::PI32& row, const gd::PI32& cond_row, bool lookahead,
                            const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int sit);

    bool _open_now(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) const;

    bool _open_lookahead(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) const;

    gd::Int _add(const gd::Ref<AiRecord>& rec, gd::Int count, const gd::PI32& row);

    // ---- filters

    // The loop every filter shares: marks each unmarked candidate that passes `pred(target, row)`,
    // adds the number to the marked count (+0x72) and returns it.
    gd::Int mark_candidates(const gd::Ref<AiRecord>& rec,
                            const std::function<bool(const gd::Ref<MoveRow>&, const gd::PI32&)>& pred);

    // The target's reach bits (none: close or middle) against the distance band's mask.
    bool reach_ok(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& target) const;

    static bool is_attack(const gd::Ref<MoveRow>& m);

    static bool grounded_or_special(const gd::Ref<MoveRow>& m);

    bool _attack_in_reach(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& m) const;

    static gd::Int damage_of(const gd::Ref<MoveRow>& m);

    // FUN_8005758C: side steps (0xC001), state bit 19 moves, and attacks in reach.
    gd::Int filter_attacks(const gd::Ref<AiRecord>& rec);

    // FUN_800576DC: attacks in reach except the attack words 0x412 and 0x706.
    gd::Int filter_attacks_no_special(const gd::Ref<AiRecord>& rec);

    // FUN_80057820: damaging ground attacks with startup below 14 frames.
    gd::Int filter_fast_attacks(const gd::Ref<AiRecord>& rec);

    // FUN_80057954: attacks in reach whose level suits the opponent's posture (+0x230).
    gd::Int filter_vs_posture(const gd::Ref<AiRecord>& rec);

    // Attack level `a` against the opponent state `g` (FUN_80057AC4, FUN_80057C88).
    static bool level_vs_state(gd::Int g, gd::Int a);

    // FUN_80057AC4: attacks in reach whose level hits the opponent's current state.
    gd::Int filter_punish_level(const gd::Ref<AiRecord>& rec);

    // FUN_80057C88: as FUN_80057AC4 without the attack word 0x412.
    gd::Int filter_punish_level_no_high(const gd::Ref<AiRecord>& rec);

    // FUN_80057E48: attacks in reach from rows without command bits 5-13 (plain buttons).
    gd::Int filter_plain_attacks(const gd::Ref<AiRecord>& rec);

    // FUN_80058040: damaging attacks in reach faster than +0x20A (air ones only with state bit 19).
    gd::Int filter_quick_attacks(const gd::Ref<AiRecord>& rec);

    // FUN_8005817C: the same without the attack word 0x412.
    gd::Int filter_quick_attacks_no_high(const gd::Ref<AiRecord>& rec);

    gd::Int mark_command(const gd::Ref<AiRecord>& rec, gd::Int command);

    // FUN_80058AC0: side steps with probability 32/4096 at band 2 or 1024/4096 at band 3, then
    // FUN_8005817C against a standing opponent (+0x230 bit 0), else FUN_80058040.
    gd::Int filter_side_steps_then_quick(const gd::Ref<AiRecord>& rec);

    static bool is_neutral(const gd::Ref<MoveRow>& m);

    gd::Int mark_neutral(const gd::Ref<AiRecord>& rec);

    // FUN_800582A0: guard and evasion candidates against the opponent's attack word (+0x234). The
    // last block's count replaces the earlier ones (bug #21; RuleSet.fix_ai_guard_count adds it).
    gd::Int filter_defence(const gd::Ref<AiRecord>& rec);

    // `states`: GDScript untyped Array of state words, always ints.
    gd::Int _mark_guard(const gd::Ref<AiRecord>& rec, const gd::Arr<gd::Int>& states, bool low_bit_clear);

    gd::Int _crouch_dashes_if_closing(const gd::Ref<AiRecord>& rec);

    // FUN_800616B4: unmarks candidates with AI hint bit 0 or the low attack word 0x10F.
    gd::Int filter_no_lows(const gd::Ref<AiRecord>& rec);

    // FUN_80057300: a branch of the running move open now leads to a move the AI never chooses
    // (such moves are reserved for the hooks).
    bool hint_never_open(const gd::Ref<AiRecord>& rec);

    // ---- pad input

    // AiPadFromStep (0x80061AC4): numpad direction in bits 0-3, buttons in bits 8-11.
    gd::Int pad_from_step(gd::Int step) const;

    // AiStartScript (0x80061B24): presses the first step now and keeps the rest pending.
    void start_script(const gd::Ref<AiRecord>& rec, const gd::PI32& script) const;

    // FUN_8002CFD0: the steps of a motion command's input sequence (empty when it has none).
    gd::PI32 input_sequence(gd::Int command) const;

    // AiPressCommand (0x80061BA8): a branch command as pad input, remembering the target move
    // (+0x5C). Returns the pad word, −1 for a motion script, 0 when the command has no script.
    gd::Int press_command(const gd::Ref<AiRecord>& rec, gd::Int command, const gd::Ref<MoveRow>& target) const;

    // FUN_80056A30: restarts the approach timers.
    static void reset_approach(const gd::Ref<AiRecord>& rec);

    // AiSideStep (0x80061D40). `side` −1 chooses from the heading difference (random when nearly
    // straight or reversed) and the fighters' order on screen.
    void side_step(const gd::Ref<AiRecord>& rec, gd::Int side);

    // ---- executing a candidate

    // AiAfterAttack (0x80058C40): counts forced "never chosen" moves and, for an attack, sets the
    // wait (+0x68) to word 2 plus a random one of words 3-6 and resets the approach timers.
    void after_attack(const gd::Ref<AiRecord>& rec, const gd::PI32& row);

    // AiExecuteCandidate (0x80058D6C): picks the k-th marked candidate (k random below +0x72) and
    // presses it. On odd frame counts the walk goes forwards and stops at k ≤ 0 (bug #19: the first
    // marked candidate is twice as likely, the last never chosen; RuleSet.fix_ai_candidate_pick stops
    // at k < 0 as backwards); backwards is uniform. Returns the table index, or −1.
    gd::Int execute_candidate(const gd::Ref<AiRecord>& rec);

    // The k-th marked entry of the first `n` marks, walking forwards (stopping at k ≤ 0 as the game
    // does, or at k < 0 when `fixed`) or backwards (k < 0); −1 without candidates.
    static gd::Int marked_pick(const gd::PI32& marks, gd::Int n, gd::Int k, bool forward, bool fixed);

    // FUN_80058ED0: a branch of the running move to `slot` is open now or, failing that, opens on
    // frame 1 of the default continuation, which is read from the row after the last walked row
    // (bug #20: a common row when the list ends with a common block; RuleSet.fix_ai_continuation
    // reads the terminator row, as AiCollectCandidates does).
    bool branch_open_to(const gd::Ref<AiRecord>& rec, gd::Int slot);

    // Walks a branch list for an open row to `slot`: [found, the row after the last walked row, and
    // when none is found the list's terminator row].
    WalkTo _walk_to(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& move, gd::Int slot, bool first_frame_one,
                    const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);

    // ---- character hooks
    // Hooks return a pad word to hold, −1 (nothing), −2 (input set, for example a script) or −3
    // (execute a marked candidate).

    // FUN_8005967C: FUN_800616B4 filters this frame's candidates.
    static void install_no_lows(const gd::Ref<AiRecord>& rec);

    // AiCallHook (0x800594C4): with no script pending (+0x8A < 0) and a draw below word 29, runs the
    // hook of the opponent's bank (`own` false) or the CPU's own. True when it produced input.
    bool call_hook(const gd::Ref<AiRecord>& rec, bool own);

    // `hook`: an AiTables::Hook value.
    gd::Int _run_hook(const gd::Ref<AiRecord>& rec, gd::Int hook);

    gd::Int _hook_vs_paul(const gd::Ref<AiRecord>& rec);

    gd::Int _hook_vs_gon(const gd::Ref<AiRecord>& rec);

    // Reacts to its own slot 0xDE3, which no bank defines (bug #11).
    gd::Int _hook_as_hwoarang(const gd::Ref<AiRecord>& rec);

    gd::Int _hook_vs_yoshimitsu(const gd::Ref<AiRecord>& rec);

    // Inlined in the hook against Ogre: a side from the heading unless the CPU's own move has state
    // bit 22, the fighters are 7,001+ apart or the heading is ambiguous; side-steps at once.
    gd::Int _ogre_side(const gd::Ref<AiRecord>& rec);

    // Against Ogre's fire breath (slot 0x16E); the 0xDE4 branch is dead (bug #11).
    gd::Int _hook_vs_ogre(const gd::Ref<AiRecord>& rec);

    // A chosen side step is started again (the second call repeats the first).
    gd::Int _step_again(const gd::Ref<AiRecord>& rec, gd::Int side);

    // King's command throws (slot 0x15 open) and the scripts against crouching or downed opponents.
    gd::Int _hook_as_king(const gd::Ref<AiRecord>& rec);

    // ---- reset and parameters

    // AiLoadParams (0x80061770).
    void load_params(const gd::Ref<AiRecord>& rec, gd::Int group, gd::Int level, gd::Int disabled);

    // AiReset (0x800596B0) for `f`'s record. The hooks, the band tables and the health come from
    // the `ai_self` / `ai_opp` globals, not from `f`.
    void reset(const gd::Ref<FighterState>& f, gd::Int group, gd::Int level, gd::Int disabled);

    // AiInitRound (0x8005993C): the AI slots and targets of the fighters, each record reset. Tekken
    // Force gives its two enemy records the AI slots 0 and 1, both after the player.
    void init_round();
};

}  // namespace t3

#pragma once
// remake/core/game/progress_rules.gd
// The rules that change the game's progress (modes.md#unlocks, #records), written from the
// verified port `tools/research/screens_sim.py`: the unlock schedule, the play-count unlocks, arcade
// clears, Gon's unlock, the character statistics and the save request.
#include "gd/gd.h"
#include "core/fighter/character.h"

namespace t3 {

class GameProgress;
class GameGlobals;
class ModeRegion;
class FlowData;
class RuleSet;

class ProgressRules {
public:
    static constexpr gd::Int START_COSTUME_MASK = 0x50382;
    static constexpr gd::Int ALL_CHARACTERS = 0x1FFFFF;
    static constexpr gd::Int FIGHTS_CAP = 0xFFFF;
    static constexpr gd::Int NO_CHARACTER = 0x16;
    static constexpr gd::Int DOCTOR_B_BIT = gd::Int(1) << Character::DOCTOR_B;
    static constexpr gd::Int UNLOCK_STEP_MASK = 0x83FF;        // the characters whose clears count as unlock steps
    static constexpr gd::Int SECOND_CLEARS = 0x10900;          // the second-clear bits: Kuma (Panda), Eddy (Tiger), Gun Jack
    // Start costumes unlocked by usage: (character, usage, costume bit).
    static inline gd::Arr<gd::PI32> USAGE_COSTUMES{gd::PI32{9, 50, 0x200},
        gd::PI32{7, 50, 0x80}, gd::PI32{18, 25, 0x40000}, gd::PI32{16, 10, 0x10000}};

    gd::Ref<GameProgress> progress;
    gd::Ref<GameGlobals> globals;
    gd::PI32 schedule;
    gd::Ref<ModeRegion> region;
    gd::Ref<RuleSet> fight_rules;

    ProgressRules(const gd::Ref<GameProgress>& game_progress, const gd::Ref<GameGlobals>& game_globals,
                  const gd::Ref<ModeRegion>& mode_region, const gd::Ref<FlowData>& flow,
                  const gd::Ref<RuleSet>& rule_set);

    static gd::Int popcount(gd::Int v);
    // FUN_80056498.
    void unlock_character(gd::Int character);
    // `--unlock`: every character, Start costume, mode and Theater movie open, as after the schedule's
    // last step but without its side effects (no "just unlocked" character, no reset of the play
    // count). The movies open with the arcade clears of every character and the three second clears.
    static void open_all(const gd::Ref<GameProgress>& p);
    void update_unlock_class();
    void add_start_costume(gd::Int bit);
    // ArcadeUnlocks (FUN_800564C8): the first `n` steps of the schedule.
    void arcade_unlocks(gd::Int n);
    void _open_theater();
    // FUN_800567A0: Start costumes by usage, then one more schedule step per 100 (then 50) fights.
    void play_unlocks();
    // FUN_8004C678: after a statistics update, the play-count unlocks and a pending save.
    void request_save();
    // FUN_8005696C: beating Gon as the CPU in arcade or Tekken Ball unlocks him.
    void gon_check(gd::Int character);
    // FUN_800B2350 (arcade.ovl): an arcade clear, and the steps the clears open (unlock_steps).
    void record_clear(gd::Int character, gd::Int costume);
    // The unlock steps a cleared mask opens: the bit positions below the number of cleared
    // characters that are set in 0x83FF (bug #29), or with `fixed` the cleared characters in it.
    static gd::Int unlock_steps(gd::Int cleared, bool fixed);
    void _bump(gd::Int character, gd::Int which);
    // FUN_8005169C: a finished run or a VS win of `character`.
    void count_play(gd::Int character);
    // FUN_80051740: a win and a loss.
    void count_result(gd::Int winner, gd::Int loser);
    // FUN_80051838: a draw for both characters.
    void count_draw(gd::Int a, gd::Int b);
};

}  // namespace t3

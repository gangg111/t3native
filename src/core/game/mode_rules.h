#pragma once
// remake/core/game/mode_rules.gd
// The modes' hooks that FightMain calls (arcade.ovl and the resident code; modes.md#match-flow):
// the start hook (sub-state 1), the fight hook (sub-state 2) with StageAndMusicSelect, and the
// end hook when a match is decided; and the records the modes keep (FUN_80051674, the time
// attack record FUN_800B4B80, the survivors FUN_800B4C94).
//
// Tekken Ball's hooks are volley.ovl's, Tekken Force's force.ovl's.
#include <string>

#include "gd/gd.h"
#include "core/game/flow_part.h"

namespace t3 {

class GameFlow;
class GameGlobals;
class TekkenForce;

class ModeRules : public FlowPart {
public:
    static constexpr gd::Int KEY_NONE = 0x58;
    static constexpr gd::Int TEKKEN_BALL_STAGE = 0x13;
    static constexpr gd::Int VS_STAGE_KEY = 0x44;            // mode context +0x44/+0x48/+0x4C: the VS stage memory
    static constexpr gd::Int LADDER = 0x90;                  // mode context: ten (character, costume, stage) entries
    static constexpr gd::Int OGRE_SCENE = 0xBC;              // mode context: the Ogre scene has played
    static constexpr gd::Int STAGE_RECORDS = 0x40;           // mode context: ten (u32 time, u8 character, u8 costume, u16 player)
    static constexpr gd::Int LAST_STAGE = 9;
    static constexpr gd::Int TEAMS = 0x59;                   // mode context: two teams of 13 bytes
    static constexpr gd::Int TEAM_SIZE = 0xD;
    static constexpr gd::Int TEAM_ROUND = 0x58;
    static constexpr gd::Int TEAM_OUTCOMES = 0x38;           // mode context: u16 per team round, the outcome
    static constexpr gd::Int SURVIVAL_RANK = 0x40;           // mode context: the survivor rank the wins would reach
    static inline const std::string BLANK_NAME = "   ";      // arcade.ovl 0x800B0C58: a new record's name before entry
    static constexpr gd::Int SURVIVOR_COUNT = 10;
    static constexpr gd::Int RECORD_TIME = 1;                // the name entry's kinds (0x800984DE)
    static constexpr gd::Int RECORD_SURVIVOR = 2;
    static constexpr gd::Int FORCE_FIRST_STAGE = 0xF;
    static constexpr gd::Int FORCE_LAST_STAGE = 0x12;
    static constexpr gd::Int FORCE_EXTRA_MUSIC = 0x19;
    static constexpr gd::Int BALL_POOL = 0x53FFF;            // FUN_800B528C: the opponents Tekken Ball may pick
    static constexpr gd::Int BALL_MET = 0x44;                // mode context: u16 per character, the Tekken Ball met counts
    static constexpr gd::Int BALL_OPPONENT = 0x70;           // mode context: the chosen costume key
    static constexpr gd::Int BALL_FIRST_OPPONENT = 0x44;     // Gon, costume 0

    // FlowPart's _init(flow): ModeRules has no _init and no object-valued member of its own.
    using FlowPart::FlowPart;

    gd::Int _team(gd::Int side);

    // The start hook (FightMain sub-state 1).
    void mode_start();
    // The fight hook (FightMain sub-state 2).
    void fight_start();
    // The end hook of a decided match.
    void match_end();

    // ---- arcade and time attack

    // FUN_800B0C5C: a new run builds the ladder for the human's character (not on a challenger's return).
    void _arcade_start();
    // FUN_800B0DC0: the CPU level of the ladder stage and, with one human, its opponent.
    void _arcade_fight();
    // FUN_800B134C: with one human a win moves up the ladder (the tenth: the ending), a loss offers
    // the continue; with two humans the loser drops out.
    void _arcade_end();
    // FUN_800B197C: time attack is arcade without the Gon check, ending on its result screen.
    void _time_attack_end();
    // The won ladder fight: its time counts and is kept per stage, and the ladder moves on.
    void _ladder_win();
    // The run's winner and costume key (mode context +0x38, +0x3C).
    void _run_over(gd::Int w);
    // The human lost: the fight's time carries into the retry, and the continue starts.
    void _lost();
    // FUN_80051674: a ladder stage's record (time, character, costume, player).
    void _stage_record(gd::Int stage, gd::Int character, gd::Int costume, gd::Int player, gd::Int frames);
    // FUN_800B4B80: a run with one character and player on every stage beating the character's best
    // time sets the record (named blank until the name entry).
    bool _time_record();

    // ---- VS

    // FUN_800B15CC: VS: the win or the draw counted, then back to select.
    void _vs_end();

    // ---- Tekken Ball

    // FUN_800B4C80: the CPU level from the difficulty and, with one human, the opponent
    // (FUN_800B528C) on the CPU side.
    void _ball_fight();
    // FUN_800B528C: the first single-player game always meets Gon (costume key 0x44); later ones a
    // random character among those met at most 4 times more than the least met (bug #30: the list
    // is sorted the wrong way, so every unlocked candidate stays in). The met counts are the mode
    // context's u16s at +0x44; Gon's first game counts nowhere (bug #31: a stray write).
    gd::Int _ball_opponent();
    // FUN_800B4D24: the plays counted (and Gon unlocked when the single player beat him), then back
    // to select; with two players the loser leaves.
    void _ball_end();

    // ---- Tekken Force

    // FUN_800B60A4 (and FUN_800B58FC): the keys, score and hi-score (FUN_800B1508), the run's result
    // words cleared (the boss records to "none"), the boss list when Doctor B. is unlocked, and the
    // manual target switch off.
    void _force_start();
    // FUN_800B6110: the enemies' CPU level by level (0, 3, 4, 5, 5) and the CPU side's character 21.
    void _force_fight();
    // FUN_800B61D4: a lost level ends the run (the play counted, the hi-score saved, game over); a
    // cleared one goes to the next level, and after the last (the fourth, or the Doctor B. level when
    // the keys earned it) to the Tekken Force result (Doctor B. unlocked after his level).
    void _force_end();
    static void _force_leave(const gd::Ref<GameGlobals>& gl);
    // FUN_800B6414: the run's hi-score into the save (save pending).
    void _force_save_hiscore(const gd::Ref<TekkenForce>& force);

    // ---- team battle

    // FUN_800B0CD0: the rounds cleared, and each team completed (a CPU team takes the size of the
    // other and the other costume).
    void _team_start();
    // FUN_800B0E78: a side whose member changed fields the next one; the CPU level from the options
    // and the members left.
    void _team_fight();
    // FUN_800B16F0: the round's outcome counts; a side with no members left ends the match on the
    // team result screen.
    void _team_end();
    // FUN_80051A98: a fighter's health and carried health cleared (a new member or opponent).
    void fighter_reset(gd::Int fighter);

    // ---- survival

    // FUN_800B104C: the next opponent and the CPU level from the wins.
    void _survival_fight();
    // FUN_800B4B44: the survivor rank (1-10) the wins would take, 0 for none.
    gd::Int _survivor_rank(gd::Int wins);
    // FUN_800B1B10: a win counts per defeated character; the loss ends the run on the survival
    // result screen with its record.
    void _survival_end();
    // FUN_800B4C94: a run of `wins` enters the survivors (sorted by wins, a new entry after equal
    // ones) with a blank name.
    bool _survivor_record(gd::Int player, gd::Int character, gd::Int wins);

    // StageAndMusicSelect (0x8004F698): the stage and music of the fight from a character record:
    // the human's (or the side that won ties) in most modes, VS keeps its stage until both players
    // change characters, Tekken Ball plays on stage 19, Tekken Force by its level.
    void stage_and_music();
    // The player whose character decides: the CPU's side with one human, else the tie loser.
    gd::Int _player_of_humans();
    gd::Int _key(gd::Int player);
};

}  // namespace t3

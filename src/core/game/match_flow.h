#pragma once
// remake/core/game/match_flow.gd
// FightMain (0x80050710, game state 8; modes.md#match-flow): character select, the mode's start
// and fight hooks, the VS screen, loading the fighters, the rounds, the mode's end hook, and the
// continue, game over and new challenger screens (sub-states 9–14, screens_sim.fight_main).
#include "gd/gd.h"
#include "core/game/flow_part.h"

namespace t3 {

class GameFlow;
class OgreScene;

class MatchFlow : public FlowPart {
public:
    static constexpr gd::Int FIRST_ROUND_FRAMES = 0x3B;
    static constexpr gd::Int CHALLENGER_WAIT = 8;
    static constexpr gd::Int CONTINUE_FRAMES = 808;
    static constexpr gd::Int DIGIT_FRAMES = 90;
    static constexpr gd::Int GAME_OVER_FRAMES = 0x79;
    static constexpr gd::Int CHALLENGER_FRAMES = 0x4A;
    static constexpr gd::Int CHALLENGER_LOAD = 0x28;
    static constexpr gd::Int CHALLENGER_FREEZE = 0x18;
    static constexpr gd::Int SOUND_CHALLENGER = 0x49A1;
    static constexpr gd::Int SYSTEM_CONTINUE = 0x16;
    static constexpr gd::Int SYSTEM_GAME_OVER = 0x1017;
    static constexpr gd::Int MUSIC_GAME_OVER = 3;
    static constexpr gd::Int MUSIC_FADE = 0x3C;
    static constexpr gd::Int QUICK_BYTES = 0xFFFF00FFLL;      // FUN_80051204: bytes 0, 2 and 3 of 0x800AFF5C
    // The sub-states of game state 8 (modes.md#match-flow).
    enum Sub : gd::Int {
        SELECT, MODE_START, NEXT_FIGHT, LOAD, LOAD_WAIT, DISPLAY_OFF, DISPLAY_ON, ROUND_START, FIGHT,
        CONTINUE_START, CONTINUE, GAME_OVER_START, GAME_OVER, CHALLENGER_START, CHALLENGER,
        OGRE_DISPLAY_ON, OGRE_SETUP, OGRE, OGRE_DISPLAY_OFF, TRUE_OGRE_DISPLAY_ON, TRUE_OGRE,
        AREA_DISPLAY_OFF, AREA_DISPLAY_ON, FORCE_BOSS, DEMO_OVER, EXIT,
    };

    gd::Ref<OgreScene> ogre;                   // OgreScene.new(flow), made in the constructor

    explicit MatchFlow(GameFlow* flow);

    // FUN_80051204: the mode (or a challenger) asks for quick select.
    bool quick_select();
    void step();
    // Character select (or quick select); the demonstration fight skips it.
    void _select();
    // CONTINUE?: the loser's countdown over the frozen fight; the same fight, select again, or game
    // over.
    void _continue(bool first);
    // GAME OVER over the fight, then the ranking.
    void _game_over(bool first);
    // A NEW CHALLENGER ENTERS!!: the fight freezes, then character select (or quick select at once).
    void _challenger(bool first);
    // Sub-state TRUE_OGRE: the boss of the last stage becomes True Ogre, and the match goes on with the
    // same round score (RoundTallySave / RoundTallyRestore; one-round matches restart at 0).
    void _true_ogre();
    // Sub-state MODE_START: the mode's start hook, then the match counters (falls into NEXT_FIGHT).
    void _mode_start();
    // Sub-state NEXT_FIGHT: the humans, the mode's fight hook, the stage and music, then the VS screen.
    void _next_fight();
    // Sub-state FIGHT: the fight, challengers, and the round's hand-over.
    void _fight();
    // ChallengerJoin (FUN_80051244): a player who is not playing presses Start.
    bool challenger_join(gd::Int player);
    // FUN_800502D8: 1 the same fight again, 2 back to select, 0 counting, −1 over. CONTINUE? n
    // counts down from 9, with system sound 4 + n for each digit from 8 on.
    gd::Int _continue_countdown(gd::Int player);
};

}  // namespace t3

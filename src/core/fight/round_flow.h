#pragma once
// remake/core/fight/round_flow.gd
// The round state machine (FUN_8003CDD8, fight-frame.md#round-flow) of a VS match:
//
// | State | |
// |---:|---|
// | 0 | Intro: 150 frames in round 1, 90 later, then input on. |
// | 1 | Fight: the timer runs until a KO or the time is up; then the result. |
// | 2 | Result shown (240 frames after a KO, 90 after a time out), losers on scripted input. |
// | 3, 4 | The replay (after a KO), then the win poses and the round-end camera. Tekken Ball
// |   | shows the deciding point's last 30 frames (twice, six times after an iron ball's lethal
// |   | hit) from the result, and skips state 4. |
// | 5, 6 | Win poses until they end (or 300 frames); Start skips. |
// | 9 | The round is over (FightMain starts the next round or ends the match). |
//
// Tekken Force: a won level (the player's round won) goes to state 8, STAGE CLEAR!! until the
// tally is done and Start is pressed or 300 frames pass; state 7 (the level runner's area
// change) returns to the fight.
#include "gd/gd.h"

namespace t3 {

class FightSimulation;
class FightState;
class FighterState;

class RoundFlow {
public:
    static constexpr gd::Int INTRO_ROUND1 = 0x97;
    static constexpr gd::Int INTRO_LATER = 0x5B;
    static constexpr gd::Int RESULT_KO_HOLD = 0xF0;
    static constexpr gd::Int RESULT_TIME_HOLD = 0x5A;
    static constexpr gd::Int RESULT_CAMERA_TIME = 0x5A;
    static constexpr gd::Int WIN_HOLD = 300;
    static constexpr gd::Int WIN_CAMERA_END = 0x4B;
    static constexpr gd::Int LOSER_CAMERA_END = 0x3C;
    static constexpr gd::Int TIME_UP = 1;
    static constexpr gd::Int PERFECT_FLAGS = 0x1E;           // result bits that end the result display at 90 frames
    static constexpr gd::Int DRAW = 0x20;
    static constexpr gd::Int DOUBLE_KO = 0x26;
    static constexpr gd::Int KO = 2;
    static constexpr gd::Int PERFECT = 10;
    static constexpr gd::Int GREAT = 0x12;
    static constexpr gd::Int FULL_HEALTH = 0x1000;
    static constexpr gd::Int GREAT_HEALTH = 0xCD;
    static constexpr gd::Int LOSE_POSE_DOWN = 0xD65;
    static constexpr gd::Int LOSE_POSE_UP = 0xD6C;
    static constexpr gd::Int WIN_POSE_FIRST = 0xD67;
    static constexpr gd::Int SPECIAL_WIN_COSTUME = 0x26;
    static inline const gd::Arr<gd::Int> RECOVERY_CLOCK{300, 600, 900, 1200, 1500, 1800, 2100};   // survival's recovery steps (frames)
    static constexpr gd::Int OGRE_SCENE = 0xBC;              // mode context: the Ogre scene has played
    static constexpr gd::Int STAGE_CLEAR_FRAMES = 300;
    static constexpr gd::Int STAGE_CLEAR_FADE = 0xB4;
    static constexpr gd::Int BALL_REPLAY_DELAY = 0x10;
    static constexpr gd::Int BALL_LOSER_CAMERA_END = 0xF0;

    gd::Ref<FightState> fight;

    explicit RoundFlow(const gd::Ref<FightState>& fight_state);

    // FUN_8003EE94: a requested replay starts (ReplayStart(0)).
    void replay_request_check(const gd::Ref<FightSimulation>& sim);

    // ReplayUpdate (0x800323CC): non-zero while the replay plays.
    gd::Int replay_update(const gd::Ref<FightSimulation>& sim);

    // FUN_8003CDD8 for one frame.
    void step(const gd::Ref<FightSimulation>& sim);

    // RESULT: the losers on scripted input until the result display ends; then the replay (Tekken
    // Force: the round's end, or STAGE CLEAR!! for a won level).
    void _result_step(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // REPLAY_DONE: after the replay, the win poses and the round-end camera, or (skipped) the round's
    // end.
    void _replay_done(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // STAGE_CLEAR (Tekken Force): until the tally is done and Start is pressed, or the frames run out.
    void _stage_clear(const gd::Ref<FightSimulation>& sim);

    // State RESULT of Tekken Ball: 16 frames after the result the deciding point's replay starts, with
    // the loser camera; each finished replay starts the next until RESULT of them have played.
    void _ball_replays(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // State 3: other modes go on to the replay (state 4); Tekken Ball, whose replays are shown, to
    // the win poses (a CPU's defeated opponent keeps a sliver of health).
    void _ball_result(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // States 5 → 6: the win poses start (FUN_8003CE64 case 5 falls into 6).
    void _win_state(const gd::Ref<FightSimulation>& sim);

    // State 6: the win poses and the round-end camera until they end, the hold runs out or Start.
    void _win_wait(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_8003E608: a fighter is out of health or the time is up. Tekken Force watches the player
    // while the enemies come, the first two records in the boss fight (0x800B70E8 set).
    bool _round_over();

    // The round's result (state 1 → 2).
    void _result(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_80051AD4: the health the next fight starts with. Team battle's winner and survival's
    // winning human recover part of it (by the health left, by the fight's time); other modes keep it.
    void _carry_health(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    gd::Int _recovered(const gd::Ref<FighterState>& f, gd::Int share);

    // Game state 9 (and every frame in it): after the first round the human wins against Ogre in
    // arcade or time attack, the Ogre scene is requested (OgreSceneCheck 0x800514EC, round state 10).
    void _round_end();

    // FUN_8003E70C: time up, draw, double KO, KO, perfect and great flags.
    void _result_flags(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_8003E7C4: the round's winner (+0x46 = 1, the loser −1) and the round wins; a draw counts
    // for both (and costs a round of the maximum), the match ends when a fighter has enough wins
    // or the last round is played (FUN_8003EA98).
    void _round_wins(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // The draw tie breaker by the human mask (1: player 2 first; 3: the stored choice 0x800AE406).
    gd::Int _tie_breaker();

    // FUN_8003EA98: the match result from the round wins.
    void _match_result(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_8003EBF4: after a time out the losers' win-pose field takes the −1 of the result.
    void _lose_markers(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_8003D7A8: a KO'd fighter or the loser stands still (scripted input 0); the winner of a KO
    // keeps the pad unless it is CPU-controlled; the time-out winner holds its guard or crouch.
    void _scripted_input(const gd::Ref<FighterState>& f);

    // FUN_8003D860: the scripted word that keeps the fighter's stance (down while crouching). A
    // stance slot empty in both banks has no row (bug #61, not reproduced): no hold.
    void _hold_input(const gd::Ref<FighterState>& f);

    // FUN_8003EB60: the round winner's win pose (1 + frame parity; the physical buttons held
    // choose one).
    void _win_poses(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FighterStartWinPose (0x8002D6B0).
    void _start_win_pose(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f,
                         const gd::Ref<FighterState>& opp, gd::Int button);

    // FUN_8002D8C0 / FUN_8002D844: the loser's pose of a match-deciding round (down or standing).
    void _lose_poses(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // A move started from its first frame with transition 6 (`grounded`: on the floor first).
    void _start_move(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f,
                     const gd::Ref<FighterState>& opp, gd::Int slot, bool grounded);

    // FUN_8003ECF4: the round-end camera phase (winner 3/4, loser 6/7, none 2) and whose sounds
    // stay on (FUN_8002BBE0). The loser camera only follows a match-deciding round a CPU won.
    void _result_camera(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_8002BBE0: only this fighter's sounds play (the `active` bytes).
    void _sounds_of(const gd::Ref<FighterState>& f);

    // FUN_8002C23C: every fighter's blend state cleared.
    void _clear_blends();

    // FUN_8002BC28 & 0x800: a human pressed Start (Tekken Force: the player's pad).
    bool start_pressed(const gd::Ref<FightSimulation>& sim);
};

}  // namespace t3

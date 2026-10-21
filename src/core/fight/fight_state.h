#pragma once
// remake/core/fight/fight_state.gd
// The whole state of a fight (remake-plan.md#simulation-core): both fighter records and the
// game globals the fight code keeps between frames. Names follow the documentation; the
// comment gives the global's address (Japan Rev.1) where it helps to follow the RE notes.
#include <functional>

#include "gd/gd.h"

namespace t3 {

class BackdropTurn;
class CRand;
class EffectPool;
class FightTables;
class FighterState;
class GameProgress;
class ModeRegion;
class MoveRow;
class ReplayState;
class RuleSet;
class TekkenBall;
class TekkenForce;

// A GDScript property whose getter and setter forward to another object's field
// (`var mode: int: get: return region.mode  set(v): region.mode = v`), translated as a slot like
// ByteField: `gd::Int v = fight->mode;` reads through the getter, `fight->mode = v;` (and += -=
// |= &= ^=) writes through the setter. In ternaries and template calls wrap it:
// `gd::Int(fight->mode)`. The owner's constructor binds the getter and setter (in the .cpp).
class IntProperty {
public:
    IntProperty() = default;
    IntProperty(const IntProperty&) = delete;

    void bind(std::function<gd::Int()> getter, std::function<void(gd::Int)> setter) {
        get_ = std::move(getter);
        set_ = std::move(setter);
    }
    gd::Int get() const { return get_(); }
    void set(gd::Int v) const { set_(v); }
    operator gd::Int() const { return get(); }
    const IntProperty& operator=(gd::Int v) const { set(v); return *this; }
    const IntProperty& operator=(const IntProperty& o) const { set(o.get()); return *this; }
    const IntProperty& operator+=(gd::Int v) const { set(get() + v); return *this; }
    const IntProperty& operator-=(gd::Int v) const { set(get() - v); return *this; }
    const IntProperty& operator|=(gd::Int v) const { set(get() | v); return *this; }
    const IntProperty& operator&=(gd::Int v) const { set(get() & v); return *this; }
    const IntProperty& operator^=(gd::Int v) const { set(get() ^ v); return *this; }

private:
    std::function<gd::Int()> get_;
    std::function<void(gd::Int)> set_;
};

// Godot's Color as `Color8(r, g, b, a)` makes it (0-255 components; the runtime has no Color).
// Only the practice back light uses it; the presentation converts it.
struct Color8 {
    gd::Int r = 0;
    gd::Int g = 0;
    gd::Int b = 0;
    gd::Int a = 0;
    Color8() = default;
    Color8(gd::Int r8, gd::Int g8, gd::Int b8, gd::Int a8) : r(r8), g(g8), b(b8), a(a8) {}
    bool operator==(const Color8& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color8& o) const { return !(*this == o); }
};

class FightState {
public:
    static constexpr gd::Int FIGHTERS = 2;
    static constexpr gd::Int RECORDS = 3;                       // fighter records: 0x800A96F0, 0x800AAF7C, 0x800AC808

    gd::Ref<RuleSet> rules;
    gd::Ref<ModeRegion> region;                // the mode context and the fight's globals (0x800AFF00)
    gd::Ref<GameProgress> progress;            // the save block and records (0x800982D0)
    gd::Ref<FightTables> tables;
    gd::Arr<gd::Ref<FighterState>> fighters;   // the three records; the third fights in Tekken Force only
    gd::Int count = FIGHTERS;                  // the records that fight: 3 in Tekken Force (mode 8)
    // per player: the merged move index (MoveRow per slot; `Array[Array]`, null where no row)
    gd::Arr<gd::Arr<gd::Ref<MoveRow>>> slots{gd::Arr<gd::Ref<MoveRow>>(), gd::Arr<gd::Ref<MoveRow>>()};

    // ---- mode and match (the mode context 0x800AFF50)
    IntProperty mode;                          // g_gameMode 0x800AFF50 (region.mode)
    IntProperty frame_counter;                 // 0x800AFF14 (main loop frames) (region.frame_counter)
    gd::Int vblank = 0;                        // 0x800AE6E0: video frames when the main-loop frame began (FUN_80029B78); the frame counter at a steady 60 fps
    gd::Int round_time_option = 2;             // 0x800AE3C0: timer = (option + 2) · 600
    IntProperty rounds_option;                 // 0x800AFF70: rounds to win − 1 (region.rounds_option)
    IntProperty chip_damage;                   // 0x800AFF20 GUARD DAMAGE (region.chip_damage)
    IntProperty pause_allowed;                 // 0x800AFF54 (region.pause_allowed)
    IntProperty collapse_on_ko;                // 0x800AFF5B (region.collapse_on_ko)
    IntProperty carry_health;                  // 0x800AFF59 (region.carry_health)
    gd::Int human_mask = 3;                    // 0x800AE3D8
    gd::Int hud_shown = 0;                     // 0x800AE6C8: the HUD's bars, timer and marks are drawn (from the first round start)
    // Health bars (0x800980F4, 0x10 per player): the health last seen, then the target, drawn and
    // recent-damage lengths in pixels (hud.md#elements).
    gd::Arr<gd::PI32> hud_bars{gd::PI32{0, 0, 0, 0}, gd::PI32{0, 0, 0, 0}};
    gd::PI32 hud_wins_seen{0, 0};              // 0x80098114: round wins at the round start (the newest mark blinks)
    gd::Int stage = 0;                         // g_stage 0x800AE14C
    // The stage whose archive is in memory: the load cache's stage slot (0x800A0C48, FUN_8006C924(0x10));
    // −1 once something overwrote it (FUN_8006C870: a mode start, a screen overlay, a movie).
    gd::Int stage_cached = -1;
    gd::Ref<BackdropTurn> backdrop;            // the panorama's turn (StageBackgroundDraw)
    gd::Int music = 0;                         // 0x800AE1D0

    // ---- round flow
    gd::Int round_state = 0;                   // 0x80097350
    gd::Int round_counter = 0;                 // 0x80097354
    gd::Int round_frame = 0;                   // 0x80095884 frames of the round (input frames)
    gd::Int round_frame_seen = 0;              // 0x80095888 its value at the last move step
    gd::Int rounds_played = 0;                 // 0x800958A8
    gd::Int timer = 0;                         // 0x800AE094
    gd::Int result_flags = 0;                  // 0x800AE340
    gd::Int rounds_to_win = 0;                 // 0x800AE2C4
    gd::Int max_rounds = 0;                    // 0x800AE404
    gd::PI32 input_mode{0, 0, 0};              // 0x80095894: 0 none, 1 pad or CPU, 2 script
    gd::Int freeze = 0;                        // 0x8009588C: the fight only draws
    gd::Int undrawn_mask = 0;                  // fighters the Ogre scene does not draw this step (it draws the ones it stepped), a bit each
    gd::Int paused_player = 0;                 // 0x800958B8
    gd::Int no_damage = 0;                     // 0x800958A0
    gd::Int ko_started = 0;                    // 0x800958BC
    gd::Int replay_playing = 0;                // 0x800958C8
    gd::Int throw_count = 0;                   // 0x800958C4
    gd::Int blend_enabled = 1;                 // 0x800958C0: FighterAnimate may blend transitions
    gd::Int pause_request = 0;                 // 0x800958B0: 1/2 the player who asked for the pause
    gd::Int pause_shown = 0;                   // 0x800958B4
    gd::Int pause_close = 0;                   // 0x800958AC: the pause closes itself
    gd::Int pause_cursor = 0;                  // 0x80098DDC: the menu's item (−1: just opened)
    gd::Int pause_page = 0;                    // 0x80098DDD: the confirmed item + 1 (1 CANCEL, 3 RESET)
    gd::Int pause_delay = 0;                   // 0x800A8B3A: frames before the menu takes input
    gd::PI32 command_cursor{0, 0};             // 0x800A39C0: per player the move list's first move
    gd::PI32 command_offset{0, 0};             // 0x800A39C4: per player the slide still to go (pixels)
    gd::Int replay_skip = 0;                   // 0x800958A4: Start skipped the replay
    gd::Int replay_request = 0;                // 0x80097358: the round flow asks for the replay
    gd::PI32 win_pose_done{0, 0};              // 0x8009E9A0 / 0x8009E9A4
    gd::Int round_end_hold = 0;                // 0x8009735C: frames the round result is shown
    gd::Int round_end_done = 0;                // 0x80097360
    IntProperty match_result;                  // 0x800AFF71: 0 undecided, 1/2 winner + 1, 3 draw (region.match_result)
    IntProperty match_winner;                  // 0x800AFF72 (region.match_winner)
    IntProperty match_loser;                   // 0x800AFF73 (region.match_loser)
    gd::Int timer_stopped = 0;                 // 0x800958D4: infinite round time
    gd::Int practice = 0;                      // 0x800958D8: practice mode's fight (no damage)
    gd::Int practice_intro = 0;                // 0x800958E0: practice waits for its first pause
    gd::Int practice_paused = 0;               // 0x800958DC: FUN_800B3084's paused flag
    gd::PI32 counter_forced{0, 0, 0};          // 0x800958E4: practice's COUNTER ATTACKS per fighter
    gd::Arr<Color8> signal_colour{Color8(0, 0, 0, 0), Color8(0, 0, 0, 0)};   // 0x800AE430: the freeze signal's back light
    bool practice_swap = false;                // practice S+0x86: the dummy on CONTROLLER reads the other pad
    // FUN_800B7AEC: practice records each fighter's pad word. Callable(fighter index, pad word);
    // empty (`== null`) until the game flow sets it.
    std::function<void(gd::Int, gd::Int)> practice_pads;
    IntProperty fight_clock;                   // 0x800AFF7C: the fight's time in frames (region.fight_clock)
    IntProperty true_ogre_mask;                // 0x800AFF68 (region.true_ogre_mask)
    IntProperty draw_wins;                     // 0x800AFF58: a draw may decide the match (region.draw_wins)
    IntProperty team_round;                    // 0x800AFF5A (region.team_round)
    gd::Int tie_choice = 0;                    // 0x800AE406: who wins a tie between two humans
    IntProperty unlocked;                      // 0x800982D0: the characters Mokujin may copy (progress.unlocked)
    gd::Int throw_count_seen = 0;              // 0x80097E84: the count the throw link last saw

    // ---- camera
    gd::Int camera_phase = 0;                  // g_cameraPhase 0x80095890
    gd::Int shake_script = -1;                 // 0x80097EC4: the running camera shake script
    gd::Int shake_pos = 0;

    // ---- fight globals of the move and combat code
    IntProperty fighter_distance;              // g_fighterDistance 0x800AFF04 (region.fighter_distance)
    gd::PI32 pair_distance{0, 0, 0, 0, 0, 0, 0, 0};   // 0x8009EA08 per pair bit sum
    gd::PI32 pair_dx{0, 0, 0, 0, 0, 0, 0, 0};
    gd::PI32 pair_dz{0, 0, 0, 0, 0, 0, 0, 0};
    gd::PI32 pair_dir{0, 0, 0, 0, 0, 0, 0, 0};
    gd::PI32 situation{0, 0, 0};               // 0x80095B88 throw situation per player
    gd::PI32 step_accum_x{0, 0, 0};            // 0x8009E9A8
    gd::PI32 step_accum_z{0, 0, 0};            // 0x8009E9B8
    gd::PI32 tap_previous{0, 0, 0};            // 0x8009746C newest history entry at the last latch
    gd::Int latch_register = 0;                // the value LatchButtonTaps finds in $a2 (bug #52)
    gd::PI32 script_previous{0, 0, 0};         // 0x80095A94 the scripted input's last word
    gd::PI32 step_helper_side{0, 0};           // 0x80095AA0 STEP key direction per player
    gd::PI32 step_helper_timer{0, 0};          // 0x80095AA4
    gd::PI32 air_frames{0, 0, 0};              // 0x80095B94 LaunchTrajectory's flight frames
    gd::PI32 bound_ramp{0, 0, 0};              // 0x80097E68 (never reset: the image's values)
    gd::PI32 bound_ramp_step{2000, 2000, 2000};   // 0x80097E74
    gd::Int throw_link = 0;                    // 0x80097E80
    gd::Arr<gd::PI32> prev_roots;              // 0x8009EAA0: previous roots (u16) for BodySeparate
    gd::Int blend_hold = 0;                    // 0x800A9234
    gd::Arr<gd::PI32> segment_anchor;          // 0x800A0920 anchors of the last frame
    gd::Arr<gd::PI32> segment_end0;            // 0x800A08D0 segment 0 end of the last frame
    gd::Arr<gd::PI32> segment_end1;            // 0x800A08F4
    gd::PI32 alt_points{0, 0, 0};              // 0x800A0950 alternative attack point block

    // ---- animation globals
    gd::Int blend_skip = 0;                    // 0x800972CC: the next FighterAnimate skips blending
    gd::PI32 gon_mouth{0, 0};                  // 0x8009C098 per player: Gon's mouth variant
    gd::Int ogre_phase_x = 0;
    gd::Int ogre_phase_y = 0;
    gd::Int ogre_height = 0;

    // ---- the modes' own state
    gd::Ref<TekkenBall> ball;                  // Tekken Ball (volley.ovl): the ball, its tuning and gauges
    gd::Ref<TekkenForce> force;                // Tekken Force (force.ovl): the level, its enemies, camera and score
    gd::PI32 health_flash{0, 0};               // 0x8009E998: per player a lighting flash (a pick-up in Tekken Force)
    gd::PI32 flash_level{0, 0, 0};             // per fighter record the health_flash count FUN_8003A3B8 lit it with last (0: none; FighterAnimation.LIT_BLACK: a burning body), read by the views
    bool save_request = false;                 // the fight asks for a save (FUN_8004C678; the game flow runs it)

    // ---- replay and effects
    gd::Ref<ReplayState> replay;
    gd::Ref<EffectPool> effects;
    gd::Int display_buffer = 0;                // 0x800AE3C4: the drawing buffer (frame parity)

    // ---- sounds
    gd::PI32 voice_cooldown{0, 0, 0};          // 0x8009760C per player
    gd::PI32 damage_voice_timer{0, 0, 0};      // 0x80097600
    gd::PI32 sound_hold{0, 0, 0};              // 0x80097614: no jump or landing sound
    gd::PI32 wooden_sounds{0, 0};              // 0x800958CC: Mokujin's wooden sounds

    // ---- random numbers
    gd::Ref<CRand> rng;                        // libc rand() (0x800A3E80)
    gd::Int camera_rng = 0;                    // 0x800A95F4: (x + 1) · 0x10DCD per main-loop frame
    gd::Int frame_rng = 0;                     // 0x8009F650: 5x + 1 per main-loop frame
    gd::Int ai_rng = 0;                        // 0x800AE168: 5x + 3 per AiReset
    gd::Int ai_difficulty = 1;                 // 0x800AE6D0: the AI group (DIFFICULTY LEVEL, 0-2)
    gd::Int ai_level = 0;                      // 0x800AE6D9: the CPU level (arcade stage 0-9)
    gd::Int challenger_lock = 0;               // 0x800AE6DA: no challenger may join (a human lost in arcade; Force)
    IntProperty challenger;                    // 0x800AFF63: a new challenger is entering (no announcements) (region.challenger)
    gd::Int attract = 0;                       // 0x800AE39C: outside a started mode (the demonstration fight): CPUs at level 4, halved waits

    // Creates region, progress, backdrop, replay, effects and rng (declaration order), binds the
    // IntProperty members to region / progress, then runs _init.
    FightState(const gd::Ref<FightTables>& fight_tables, const gd::Ref<RuleSet>& fight_rules);
    FightState(const FightState&) = delete;

    // The records that fight (two, three in Tekken Force).
    gd::Arr<gd::Ref<FighterState>> active() const;

    // MoveLookup (0x8002D3CC): the move row of a slot in the fighter's merged move index.
    gd::Ref<MoveRow> move_for_slot(const gd::Ref<FighterState>& f, gd::Int slot) const;

    // FighterOpponentIndex (0x80045E50): the throw partner during a throw, else the last attacker
    // when this move was interrupted by a hit, else the last fighter this move touched, else the
    // default opponent.
    gd::Int opponent_index(const gd::Ref<FighterState>& f) const;

    // FighterOpponent (0x80045EB0).
    gd::Ref<FighterState> opponent(const gd::Ref<FighterState>& f) const;

    // FUN_80078940: the move lists start at their first move.
    void command_scroll_reset();
};

}  // namespace t3

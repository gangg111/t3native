#pragma once
// remake/core/ball/tekken_ball.gd
// Tekken Ball (volley.ovl, mode 7; modes.md#tekken-ball), written from the verified port
// tools/research/ball_sim.py: the ball object, its physics and touches (FUN_800B16A4 with the attack
// and body tests FUN_80048030 / FUN_800481AC), the render kind and squash (FUN_800B2F14), the
// fight-start, round and frame hooks FightFrame calls (FUN_800B0C90, FUN_800B0DD8, FUN_800B0E14),
// the move tuning after MoveStartAll (FUN_800B0B54), and the power gauges and popup texts.
//
// State keeps the game's layout, as practice's does: the ball object (`b`, the fight heap's
// 0x800AE23C record) and volley.ovl's data from BASE (`v`, the ball types, the tuning records,
// the gauges and popups), loaded from the overlay image when the mode starts. The popups keep
// the world point they were placed at; the presentation projects it with the frame's camera
// (FUN_800B445C's screen point).
#include <memory>
#include <string>

#include "gd/gd.h"
#include "core/combat/attack_word.h"

namespace t3 {

class FightState;
class FightTables;
class ByteBlock;
class ViewMatrix;
class GameRam;
class FightSimulation;
class SimEvents;
class FighterState;

// enable_shared_from_this: frame() passes `self` to ReplayState.ball.
class TekkenBall : public std::enable_shared_from_this<TekkenBall> {
public:
    static constexpr gd::Int BASE = 0x800B6400LL;              // volley.ovl's data and bss (the flow traces' "ball_mode" range)
    static constexpr gd::Int SIZE = 0xA00;
    static constexpr gd::Int BALL_SIZE = 0x160;
    static constexpr gd::Int THIRD = 2;                        // the third fighter record: the ball's stand-in attacker

    // ---- the ball object
    static constexpr gd::Int B_STATE = 0x00;
    static constexpr gd::Int B_POS = 0x68;                     // s32 x, y, z
    static constexpr gd::Int B_VEL = 0x78;                     // 8.8 per frame
    static constexpr gd::Int B_GRAVITY = 0x8C;
    static constexpr gd::Int B_ANGLES = 0x98;                  // u16 x, y, z
    static constexpr gd::Int B_SPIN = 0xA0;
    static constexpr gd::Int B_SPEED = 0xA8;                   // horizontal speed (8.8) along B_DIR
    static constexpr gd::Int B_DIR = 0xB0;                     // s16 angle (4096 a turn)
    static constexpr gd::Int B_HOMING = 0xB2;
    static constexpr gd::Int B_CENTRE = 0xB8;                  // the attack test's cylinder: x, y, z, radius, radius²
    static constexpr gd::Int B_RADIUS = 0xC4;
    static constexpr gd::Int B_LOCK = 0xCC;                    // u8 frames without touch tests
    static constexpr gd::Int B_SQUASHING = 0xCD;
    static constexpr gd::Int B_POINTS = 0xD0;                  // 4 swept hit points (start xyz, end xyz)
    static constexpr gd::Int B_OWNER = 0x130;                  // fighter record address
    static constexpr gd::Int B_TARGET = 0x134;
    static constexpr gd::Int B_SPIKE_TARGET = 0x138;
    static constexpr gd::Int B_POWER = 0x13C;                  // s16 per player
    static constexpr gd::Int B_CHARGE = 0x140;
    static constexpr gd::Int B_SPIKE_SPEED = 0x142;
    static constexpr gd::Int B_REHITS = 0x144;
    static constexpr gd::Int B_REHIT = 0x145;
    static constexpr gd::Int B_KIND = 0x146;                   // the render kind
    static constexpr gd::Int B_TIMER = 0x147;
    static constexpr gd::Int B_SQUASH_AXIS = 0x148;
    static constexpr gd::Int B_SQUASH = 0x150;

    // ---- volley.ovl (addresses)
    static constexpr gd::Int GLOWS = 0x800B645CLL;
    static constexpr gd::Int BALL_TABLE = 0x800B6460LL;        // per type 24 bytes: spike unit, speed unit, gravity, gain, zone
    static constexpr gd::Int BALL_VERTICES = 0x800B64A8LL;
    static constexpr gd::Int HIT_VOLUME = 0x800B6808LL;        // 4 SVECTORs around the ball
    static constexpr gd::Int GAUGES = 0x800B68E8LL;            // 2 × 0x10: power seen, target, fill, trail, direction, x, y
    static constexpr gd::Int CENTRE_BAND = 0x800B6948LL;
    static constexpr gd::Int SIDE_LINES = 0x800B69D8LL;
    static constexpr gd::Int COURT = 0x800B6AB8LL;
    static constexpr gd::Int SPEED_UNIT = 0x800B6AD8LL;
    static constexpr gd::Int SPIKE_UNIT = 0x800B6ADALL;
    static constexpr gd::Int GRAVITY = 0x800B6ADCLL;
    static constexpr gd::Int NEUTRAL = 0x800B6ADELL;
    static constexpr gd::Int GAIN = 0x800B6AE0LL;
    static constexpr gd::Int IDLE = 0x800B6AE4LL;
    static constexpr gd::Int TOUCH_SOUND_WAIT = 0x800B6AE8LL;
    static constexpr gd::Int DRIFT = 0x800B6B40LL;             // per player 6 bytes: angle, speed, two frames, look-ahead
    static constexpr gd::Int POINT = 0x800B6B4CLL;             // 1 + the index of the player who lost the point
    static constexpr gd::Int POPUP_NEXT = 0x800B6B50LL;
    static constexpr gd::Int CHARGED_HIT = 0x800B6B54LL;
    static constexpr gd::Int POPUP_SLOTS = 0x800B6B58LL;       // 8 × 0x20
    static constexpr gd::Int POPUPS = 0x800B6C58LL;            // the list head
    static inline const std::string EXCLAIM = "!";

    static constexpr gd::Int RESULT = 0x3C;                    // mode context +0x3C (0x800AFF8C): points per round
    static constexpr gd::Int SERVE_SIDE = 0x40;                // +0x40 (0x800AFF90): the side that serves first
    static constexpr gd::Int BALL_TYPE = 0x41;                 // +0x41 (0x800AFF91)
    static constexpr gd::Int LAST_POINT = 0x38;                // +0x38 (0x800AFF88): a point is being scored
    static constexpr gd::Int CHOOSER = 0x43;                   // +0x43 (0x800AFF93): 1 << the side choosing the ball type, 3 while it does
    static inline const gd::Arr<gd::Int> IDENTITY_WORDS{0, 8, 16};   // the words of an identity MATRIX holding 0x1000

    static constexpr gd::Int SOUND_SERVE = 0x4CE8;
    static constexpr gd::Int SOUND_RESET = 0x4CE9;
    static constexpr gd::Int SOUND_BOUNCE = 0x4E6E;
    static constexpr gd::Int SOUND_TOUCH = 0x704E;
    static constexpr gd::Int SOUND_SET = 0x7082;
    static constexpr gd::Int SOUND_SPIKE = 0x4DAF;
    static constexpr gd::Int SOUND_GUARDED = 0x7043;
    static constexpr gd::Int SOUND_CHARGED = 0x4E6D;
    static constexpr gd::Int HIGH = AttackWord::HIGH;
    static inline const gd::Arr<gd::Int> MIDS{AttackWord::MID_2, AttackWord::MID};
    static inline const gd::Arr<gd::Int> LOWS{AttackWord::LOW, AttackWord::LOW_2};
    static inline const gd::Arr<gd::Int> UNBLOCKABLE{AttackWord::UNBLOCKABLE, AttackWord::UNBLOCKABLE_2};
    static constexpr gd::Int POINT_DAMAGE = 0x1E0000;
    static constexpr gd::Int IDLE_FRAMES = 600;
    static constexpr gd::Int FLOOR_Y = -0x180;
    static constexpr gd::Int COURT_HALF = 0x2800;
    static constexpr gd::Int CEILING = -0x2000;
    static constexpr gd::Int SPIKE_CEILING = -0x1400;

    // One popup text over the ball (FUN_800B445C): `!` or a charged hit's damage.
    class BallPopup {
    public:
        std::string text = "";
        gd::Int colour = 0;
        gd::Int font = 0;
        gd::Int frames = 0;
        gd::Int rise = 0;                      // pixels risen since it was placed
        gd::PI32 point{0, 0, 0};
        gd::Ref<ViewMatrix> view;              // the camera it was placed with
    };

    // `fight` is a GDScript property over the weak reference: held weakly, FightState owns this object.
    gd::Ref<FightState> fight() const { return _fight.lock(); }
    std::weak_ptr<FightState> _fight;
    gd::Ref<FightTables> t;
    gd::Ref<ByteBlock> v;                      // ByteBlock.new(SIZE), made in the constructor
    gd::Ref<ByteBlock> b;                      // ByteBlock.new(BALL_SIZE), made in the constructor
    gd::Arr<gd::Ref<BallPopup>> popups;        // newest first (the list at 0x800B6C58)

    explicit TekkenBall(const gd::Ref<FightState>& fight_state);

    // The mode's overlay image: volley.ovl's initial data (the ball types, the sphere, the hit volume).
    void load_overlay(const gd::Ref<GameRam>& ram);

    // ==== byte access by game address
    gd::Int _v16(gd::Int a);
    gd::Int _vu16(gd::Int a);
    gd::Int _v32(gd::Int a);
    void _vput16(gd::Int a, gd::Int x);
    void _vput32(gd::Int a, gd::Int x);

    // 0x800B6ADE: the half-width of the court's neutral zone (the side lines).
    gd::Int neutral_zone();
    // 0x800B6B54: the ball's last hit was a charged one (HitApply takes the attacker's reaction).
    bool charged_hit();
    // 1 + the index of the player who lost the point this frame, 0 for none (MoveStep knocks that
    // fighter down).
    gd::Int point_loser();
    gd::Int state();
    gd::PI32 position();

    // ==== hooks

    // FUN_800B0B24: a player's move tuning cleared.
    void drift_clear(gd::Int player);
    // FUN_800B0C90 (the round start in mode 7): the ball type's constants, the ball, the players at
    // x = ∓0x1400 facing each other, and two points per round.
    void fight_start(const gd::Ref<FightSimulation>& sim);
    // FUN_800B0DD8: the court's matrix and a new ball (the select screen's preview).
    void round_reset();
    // FUN_800B5B6C: the ball on the select screen: no side has chosen yet, the preview spins at the
    // centre (render kind 10).
    void preview();
    // FUN_800B569C: the ball type chosen by the first side to finish (`pressed`: its pad): left /
    // right spin the ball to the previous / next type (it slides out and back in), a face button
    // takes the type once the ball rests (1), Select backs out (−1); 0 while choosing.
    gd::Int side_select(gd::Int pressed, const gd::Ref<SimEvents>& events);
    // FUN_800B0FDC: the ball at the start of a round (and after a point).
    void ball_init();
    // FUN_800B0B54 (after MoveStartAll): the Tekken Ball tuning of the running move slot
    // (curSlot − 0x18): slots 0x18/0x1A drift (0x8000, 30, 3, 9); 0x1E/0x20 (0x8000, 150, 5, 12,
    // look-ahead from frame 18); 0x1F/0x21 only the look-ahead frame 18.
    void move_tuning(const gd::Ref<FighterState>& f);
    // A player's tuning record (the move system reads the look-ahead frame, the physics the drift).
    gd::PI32 drift(gd::Int player);
    // FUN_800B0E14 (after MoveBranchAll, and on the frames the fight only draws or replays): the
    // ball's physics unless the fight is frozen, then the gauges and popups.
    void frame(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events);

    // ==== physics (FUN_800B16A4)
    void update(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f_a, const gd::Ref<FighterState>& f_b,
                const gd::Ref<SimEvents>& events);
    gd::Int _floor_and_touches(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f_a,
                               const gd::Ref<FighterState>& f_b, gd::Int mask, const gd::Ref<SimEvents>& events);
    void _tail(gd::Int mask);
    // The four hit points sweep from their last position to the ball's position + offset.
    void _hit_volume();

    // ==== touches

    // The attack and body tests of a flying ball; returns the hit mask.
    gd::Int _touches(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& f_a,
                     const gd::Ref<FighterState>& f_b, const gd::Ref<SimEvents>& events);
    // FUN_80048030: the fighters' attack segments against the ball's cylinder; bit 2 for `f_a`, bit 1
    // for `f_b`, bit 8 when a move uses projectile segments. The first hitting segment's direction
    // goes to `d`.
    gd::Int _attack_touch(const gd::Ref<FighterState>& f_a, const gd::Ref<FighterState>& f_b, const gd::PI32& d);
    // FUN_800481AC: the ball's four swept hit points against each fighter's 14 hurt cylinders; the
    // first contact goes to the fighter's first hit slot. Bit 2 for `f_a`, bit 1 for `f_b`.
    gd::Int _body_touch(const gd::Ref<FighterState>& f_a, const gd::Ref<FighterState>& f_b);
    // The shared part of a charging hit: re-hit bookkeeping, the hitter's power gain (damage · gain
    // / 100), the charge and the damage popup; returns the speed steps clamped to 20..cap.
    gd::Int _charge_hit(const gd::Ref<FighterState>& hitter, gd::Int base_add, gd::Int cap);
    void _speed_from_steps(gd::Int steps);
    // A fighter's attack hits the ball: the ball takes the hitter's record as its attacker and flies
    // by the attack level.
    void _attack_hit(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, gd::Int mask,
                     const gd::PI32& d, const gd::Ref<SimEvents>& events);
    // The hitter takes the ball: the third record carries its move, the ball turns towards the
    // other player and squashes by the damage.
    void _take_ball(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, const gd::PI32& d,
                    const gd::Ref<SimEvents>& events);
    // Yoshimitsu's unblockable: his own health pays the opponent's stored power, and the point ends.
    void _yoshimitsu_unblockable(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other);
    // A move without damage sets a charged ball up, or hits it flat.
    void _set_hit(gd::Int damage, gd::Int g, gd::Int unit, gd::Int x, const gd::Ref<SimEvents>& events);
    // Unblockable: the ball homes in with the hitter's power as its charge.
    void _homing_hit(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, gd::Int lob);
    // A grounded hit by level: mids lob it charged, lows shoot it low, highs hit it flat or spike a
    // set ball.
    void _ground_hit(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other, gd::Int level,
                     gd::Int damage, gd::Int g, gd::Int unit, gd::Int x, gd::Int lob, const gd::Ref<SimEvents>& events);
    // A flat, fast hit (highs, sets without a charge).
    void _flat(gd::Int extra, gd::Int damage, gd::Int g, gd::Int unit, gd::Int x);
    // The charged ball reaches the opponent: the resident hit system with the third record as the
    // attacker, whose damage is the charge.
    void _charged_touch(const gd::Ref<FighterState>& hitter, const gd::Ref<FighterState>& other,
                        const gd::Ref<SimEvents>& events);
    // The replay's record of a lethal charged hit (FUN_8004A840(player, 0x82, buffer, 1)): a dust
    // ring when the replay plays it. Bug #45 (not reproduced): the game copies the point from a stack
    // buffer it never fills; here it is the burned fighter's position, as a fall's dust records.
    void _log_burn(const gd::Ref<FighterState>& hitter);
    // volley.ovl FUN_800B3118: the squash axis angles from the hit direction.
    void _squash_axis(const gd::PI32& d);

    // ==== render kind and glow (FUN_800B2F14)
    void after_update();
    // FUN_800B4720: once per player and size, effect type 15 (the charged ball's glow).
    void glow(gd::Int player, gd::Int big);

    // ==== what the drawing does to the state (FUN_800B3198)
    gd::PI32 streaks{0, 0, 0, 0};   // kind 2: each streak's x jitter (screen units from the ball)

    // The ball's drawing (kind 2, a point) spends rand() on its streaks' jitter while the fight runs,
    // and at the point's strength 5 spawns 16 sparks (FUN_800B47B4) around the ball. The drawing
    // itself is the presentation's.
    void _draw_effects();
    // FUN_800B47B4: 16 sparks (effect type 1) within ±255 of the ball; above the floor they fly out
    // with a random side, on it they take the ball's velocity halfwords.
    void _sparks(bool high);

    // ==== gauges and popups

    // FUN_800B4E6C's state: the fill moves 3 pixels per frame towards power · 76 / maximum; a lighter
    // trail follows a drop by a sixteenth of the gap per frame. (The drawing is the presentation's.)
    void _gauges(gd::Int p1, gd::Int maximum, gd::Int p2, gd::Int highlight);

    gd::Int gauge_highlight = 0;               // the gauge that pulses (1 << player): the charging player's

    // A gauge's state for the presentation: fill, trail (pixels of 76), grows rightwards, x, y.
    gd::PI32 gauge(gd::Int g);
    // FUN_800B445C: a popup text over the ball in the next of eight slots (skipped while that slot
    // is still showing), at the head of the list.
    void _popup_add(const std::string& text, gd::Int colour, gd::Int font, gd::Int frames);

    gd::Ref<ViewMatrix> view;                  // the frame's camera (set by the simulation before the hook)
    gd::PI32 _slots;                           // the slot of each popup

    // FUN_800B4350's state: each popup rises a pixel per frame and leaves when its frames run out;
    // while the fight is frozen they only show.
    void _popups_step(bool frozen);

    // ==== helpers
    void _sound(gd::Int code, const gd::Ref<SimEvents>& events);
    // C division with the R3000's quotient for a zero divisor.
    static gd::Int _div(gd::Int a, gd::Int d);
    // FUN_8004B634: the angle of (x, z) in 4096 units.
    gd::Int _atan2(gd::Int x, gd::Int z);
};

}  // namespace t3

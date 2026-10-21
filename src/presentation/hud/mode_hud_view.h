#pragma once
// remake/presentation/hud/mode_hud_view.gd: Tekken Ball's and Tekken Force's parts of the fight HUD, over
// FightHud (which keeps the timer, and in Tekken Ball the usual bars, marks and names):
//
// - Tekken Force (force.ovl): the player's and the shown enemies' name plates and health bars
//   (FUN_800B3790, FUN_800B3118), the level progress bar (FUN_800B3D34), the keys earned
//   (FUN_800B4924), LIFE UP! (FUN_800B4374), "+n SEC" over a defeated enemy,
//   NOW LOADING..., the bonus time left over the player and the stage clear tally
//   (FUN_800B1778; `tally`, `items`, `progress_draw`, `key_icons` in tools/research/force_sim.py);
// - Tekken Ball (volley.ovl): the two power gauges (FUN_800B4E6C), the popup texts
//   (FUN_800B4350; `ball_gauges`, `ball_popups` in tools/research/draw_sim.py) and a point's four
//   streaks (FUN_800B3198, `_streaks` in tools/research/ball_sim.py).
#include <string>

#include "presentation/screens/fight_canvas.h"

namespace t3 {

class FightSimulation;
class FightState;
class FighterState;
class TekkenBall;
class TekkenForce;
class ViewMatrix;

class ModeHudView : public FightCanvas {
public:
    static constexpr gd::Int NAME_POS = 0x800B0EC8LL;      // force.ovl: per record (x, y); the enemies' right-aligned
    static constexpr gd::Int BAR_COLOURS = 0x800B69FCLL;   // 4 x RGB (4 bytes apart): the progress bar's segments
    static inline const gfx::Color HALF_DARK{0, 0, 0, 0.5f};   // a black texel half and half: the scene halved
    static constexpr gd::Int STR_NOW_LOADING = 0x800B0D54LL;
    static constexpr gd::Int STR_BONUS_LEFT = 0x800B0D6CLL;
    static constexpr gd::Int STR_STAGE_BONUS = 0x800B0D80LL;
    static constexpr gd::Int STR_CLEAR_BONUS = 0x800B0D9CLL;
    static constexpr gd::Int STR_CLEAR_TIME = 0x800B0DB8LL;
    static constexpr gd::Int STR_BONUS_TIME = 0x800B0DDCLL;
    static constexpr gd::Int STR_KEY = 0x800B0E00LL;
    static constexpr gd::Int STR_FINAL = 0x800B0E18LL;
    static constexpr gd::Int STR_LIFE_UP = 0x800B0EF0LL;
    static constexpr gd::Int PLAYER_BAR = 0x9C;
    static constexpr gd::Int BAR_HEIGHT = 0x18;
    static inline const gfx::Vec2 CAP{6, 24};
    static constexpr gd::Int BAR_TOP = 0x1B4;
    static constexpr gd::Int BAR_BOTTOM = 0x1C4;
    static constexpr gd::Int BAR_LEFT = 0x40;
    static constexpr gd::Int SEGMENT = 0x3C;
    static constexpr gd::Int POINTER_Y = 0x1A8;
    static inline const gd::Vector2i KEY_SIZE{20, 36};
    static constexpr gd::Int KEY_Y = 0x1A4;
    static inline const gd::Vector2i KEY_VRAM{688, 320};
    static inline const gd::Vector2i KEY_CLUT{128, 486};
    static constexpr gd::Int GAUGE_WIDTH = 0x4C;
    static constexpr gd::Int GAUGE_CAP_RIGHT = 0x4A;
    static inline const gd::Arr<gd::Int> GAUGE_FILL_CLUTS{0x7EDE, 0x7EDD};   // the fill: channel 0x3E, 0x3D of the bar CLUTs
    static inline const gd::Dict<gd::Int, gd::Vector2i> POPUP_SHIFT = [] {
        gd::Dict<gd::Int, gd::Vector2i> d;
        d.set(0, gd::Vector2i(5, 8));
        d.set(1, gd::Vector2i(7, 12));
        d.set(2, gd::Vector2i(12, 20));
        return d;
    }();
    static inline const gd::Arr<gd::Int> TALLY_AT{0x1E, 0x3C, 0x5A, 0x78};
    static constexpr gd::Int STREAK_BOTTOM = 0x1E0;

    gd::Ref<GameRam> force_ram;
    gd::Ref<VramImage> keys_vram;
    gd::Ref<FightSimulation> _sim;
    bool _shown = false;
    gfx::Rect2 _glow;                    // the progress bar's additive part this frame
    gfx::Color _glow_colour = gfx::Color::black();

    void setup_modes(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& exe, const gd::Ref<VramImage>& system,
                     const gd::Ref<GameRam>& force, const std::string& screens_dir);
    // After a simulation step.
    void show_state(const gd::Ref<FightSimulation>& sim, bool fighting);
    void _draw() override;

    // ---- Tekken Force
    void _force(const gd::Ref<FightState>& fight);
    gd::Ref<FighterState> _record(const gd::Ref<FightState>& fight, gd::Int i);
    // A shown enemy: not hidden (+0xC2) and past the round's intro.
    bool _visible(const gd::Ref<FightState>& fight, gd::Int i);
    std::string ram_text(gd::Int address);
    void _name(const gd::Ref<FightState>& fight, gd::Int i);
    void _bar(const gd::Ref<TekkenForce>& force, gd::Int i);
    // The level progress bar: four 60-pixel segments from x 64 (lit up to the current level, the
    // current one pulsing and split at the camera's marker), the marker and its pointer.
    void _progress(const gd::Ref<TekkenForce>& force);
    // A segment's CLUT colour: BAR_COLOURS (4 bytes apart) at 5 bits per channel (FUN_800B396C).
    gfx::Color _bar_colour(gd::Int i);
    // A texel modulated by a vertex colour (0x80 neutral, saturating).
    static gfx::Color _modulated(const gfx::Color& c, gd::Int level);
    // The additive pass: the current level's pulsing part of the progress bar.
    void _progress_glow();
    // One key per key earned: copper, silver, gold (20 x 36 at x 0, 20, 40).
    void _keys(const gd::Ref<TekkenForce>& force);
    gd::PI32 _screen(const gd::PI32& point, const gd::Ref<ViewMatrix>& view);
    // A picked-up item's LIFE UP!, rising half a pixel a frame, flashing (the placed items are
    // ForceItemsView's, in the scene).
    void _life_up(const gd::Ref<TekkenForce>& force);
    // "+n SEC" over a defeated enemy, rising half a pixel a frame.
    void _seconds(const gd::Ref<TekkenForce>& force, gd::Int i);
    // The bonus time left over the player (FUN_800B1778 state 6).
    void _bonus_left(const gd::Ref<TekkenForce>& force);
    static gd::PI32 _mss(gd::Int t);
    // The stage clear tally (state 7): the lines appear 30 frames apart.
    void _tally(const gd::Ref<TekkenForce>& force);

    // ---- Tekken Ball
    void _ball(const gd::Ref<FightState>& fight);
    // The ball's render kind 2 (a point): four additive Gouraud triangles in the 2D ordering table,
    // white at the ball's centre, 0x303030 at the screen's bottom (the first two) or top edge, 8 x
    // the kind's strength wide there, from the centre's x plus each streak's jitter.
    void _streaks();
    // A 76-pixel power gauge: the fill in the player's colour, the trail, the rest; the charging
    // player's pulses.
    void _gauge(const gd::Ref<FightState>& fight, const gd::Ref<TekkenBall>& tb, gd::Int g);
};

}  // namespace t3

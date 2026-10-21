#pragma once
// remake/presentation/hud/fight_hud.gd: the fight HUD (hud.md) with the game's own sprites: health bars
// with their animation, the timer, the round marks, the name plates, the round messages and the replay
// caption, laid out in the game's 384 x 480 frame-buffer units inside the scene's 4:3 frame.
//
// A Control with `_draw` (no ScreenCanvas): `draw(canvas, window)` sets `size` and draws into `target`.
#include <string>

#include "content/hud_data.h"
#include "core/fight/round_hud.h"
#include "gfx/canvas.h"

namespace t3 {

class FightState;
class FightSimulation;

class FightHud {
public:
    static constexpr float BAR_Y = 40.0f;
    static inline const gd::Arr<float> BAR_X{10.0f, 202.0f};
    static constexpr float BAR_LENGTH = 152.0f;
    static constexpr float BAR_HEIGHT = 24.0f;
    static inline const gfx::Vec2 BAR_CAP{6, 24};
    static constexpr float BAR_RIGHT_CAP = 150.0f;   // the second cap at x + 0x96
    static inline const gfx::Rect2 BAR_FILL_SOURCE{0, 0, 1, 24};   // one texel column, stretched
    static constexpr float TIMER_UNITS_X = 184.0f;
    // Native: the HUD is laid out about x = 184 (the timer, the bars at 10 and 202 + 156), the
    // scene's centre is 192 (HudFrame: the 368 columns shown from x = 8). The game showed it 8
    // columns left of the centre, which a wide window makes plain (the left bar at its edge): the
    // HUD is moved onto the scene's centre; and to the window's top where the window is taller
    // than the frame (point()).
    static constexpr float CENTRE_SHIFT = 8.0f;
    static constexpr float TIMER_STEP = 15.0f;
    static constexpr float TIMER_Y = 22.0f;
    static inline const gfx::Vec2 DIGIT{16, 46};
    static inline const gd::Arr<gd::Int> INFINITE{11, 10};   // the halves of the infinity sign (units, tens)
    static constexpr float MARK_Y = 70.0f;
    static inline const gd::Arr<float> MARK_X{151.0f, 202.0f};
    static inline const gd::Arr<float> MARK_STEP{-14.0f, 14.0f};
    static inline const gfx::Vec2 MARK{15, 18};
    static inline const gd::Arr<gfx::Vec2> NAME_POS{gfx::Vec2(14, 66), gfx::Vec2(354, 66)};   // player 2: the right end
    static constexpr float NAME_HEIGHT = 16.0f;
    static constexpr float PLATE_TOP = 9.0f;
    static constexpr float PLATE_BOTTOM = 19.0f;
    static constexpr float PLATE_MARGIN = 4.0f;
    static inline const gfx::Rect2 PLATE_SOURCE{0, 0, 2, 10};
    static inline const gfx::Vec2 REPLAY_POS{244, 30};
    static constexpr gd::Int REPLAY_COLOUR = 7;
    static constexpr gd::Int REPLAY_FONT = 1;

    gd::Ref<HudData> data;
    gfx::Rect2 box;                      // the scene's 4:3 frame in the window
    gd::Ref<FightState> _fight;
    gd::PI32 _slots{0, 0};
    bool _replay = false;
    // The flow's own texts over the fight (CONTINUE?, GAME OVER, A NEW CHALLENGER).
    gd::Arr<gd::Ref<RoundHud::Message>> flow_texts;

    // The Control's state.
    bool visible = true;
    gfx::Vec2 size;
    gfx::Canvas* target = nullptr;       // the canvas `_draw` draws into (set by `draw`)

    void setup(const gd::Ref<HudData>& hud_data, const gd::PI32& costume_slots);
    // A record's costume swapped in during the fight (its name plate).
    void set_slot(gd::Int record, gd::Int costume_slot);
    // After a simulation step.
    void show_state(const gd::Ref<FightSimulation>& sim);
    // The original 4:3 frame in the window (CameraRig.frame_rect, normalised).
    void place(gfx::Rect2 frame_rect);
    gfx::Vec2 point(float x, float y) const;
    gfx::Vec2 unit_scale() const;
    // A rectangle in HUD units.
    gfx::Rect2 rect(float x, float y, float w, float h) const;
    // Sets `size` and draws (the app draws only visible views).
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw();
    // FUN_8004E55C: the fills (health innermost, recent damage, empty) under the two end caps.
    void _draw_bar(gd::Int p);
    // FUN_8004E410: the seconds left, (frames + 59) / 60, or infinity; units first, tens 15 to the left.
    void _draw_timer();
    // FUN_8004E87C: one mark per round to win; the newest won mark blinks.
    void _draw_marks(gd::Int p);
    // FUN_8004EB9C: the name sprite on its backing quad.
    void _draw_name(gd::Int p);
    // FUN_8004D15C: glyphs of a font and colour from (x, y), one advance per character.
    void draw_text_at(gfx::Canvas& item, const std::string& text, gd::Int font_index, gd::Int colour, float x, float y);
};

}  // namespace t3

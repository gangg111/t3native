#pragma once
// remake/presentation/hud/pause_menu_view.gd: draws the pause menu (FUN_8007854C, modes.md#pause) over
// the frozen fight: a dark blue box on a light grey one behind CANCEL, COMMAND and RESET in font 0
// (colour 2 for the chosen item, 6 for the others) centred at x = 184 from y = 104, 20 apart, and
// "nP PAUSE" blinking at 32 from the left (player 1) or right (player 2) edge of the display (font 1,
// colour 5); Tekken Ball's HOW TO page shows the rules picture instead.
// Coordinates are the HUD's 384 x 480 units (FightHud).
//
// A Control with `_draw` (no ScreenCanvas): `draw(canvas, window)` sets `size` and draws.
#include <string>

#include "core/fight/pause_menu.h"
#include "gfx/canvas.h"

namespace t3 {

class FightHud;
class FightSimulation;
class TextLocale;

class PauseMenuView {
public:
    static constexpr gd::Int ITEM_Y = 0x68;
    static constexpr gd::Int ITEM_STEP = 0x14;
    static constexpr gd::Int ITEM_CENTRE = 0xB8;
    static constexpr gd::Int ITEM_ADVANCE = 9;
    static gfx::Rect2 BOX() { return gfx::Rect2(0x94, 0x64, 0x48, 0x41); }          // dark blue (0, 16, 48); 0x55 high in Tekken Ball
    static gfx::Rect2 BORDER() { return gfx::Rect2(0x93, 0x62, 0x4A, 0x41 + 4); }   // light grey (224, 224, 224) under it
    static constexpr gd::Int BALL_BOX_HEIGHT = 0x55;
    static gfx::Color BOX_COLOUR() { return gfx::Color::c8(0, 16, 48); }
    static gfx::Color BORDER_COLOUR() { return gfx::Color::c8(224, 224, 224); }
    static constexpr gd::Int LABEL_Y = 0x78;
    static constexpr gd::Int LABEL_LEFT = 0x20;
    // Player 2: the display's x + width - 0x88 (0x800AE6F8 holds (0, 20, 368, 448) in the fight).
    static constexpr gd::Int DISPLAY_X = 0;
    static constexpr gd::Int DISPLAY_WIDTH = 368;
    static constexpr gd::Int LABEL_RIGHT = DISPLAY_X + DISPLAY_WIDTH - 0x88;
    static constexpr gd::Int SELECTED = 2;
    static constexpr gd::Int OTHER = 6;
    static constexpr gd::Int LABEL_COLOUR = 5;
    static gfx::Vec2 HOW_TO_AT() { return gfx::Vec2(0x4C, 0x80); }
    static gfx::Vec2 HOW_TO_SIZE() { return gfx::Vec2(0xD8, 0xCC); }
    // The USA release's wider RULES picture (volley.ovl FUN_800B3BC4): its card, shadow and place.
    static gfx::Rect2 RULES_CARD() { return gfx::Rect2(0x34, 0x78, 0x108, 0xDC); }
    static gfx::Rect2 RULES_SHADOW() { return gfx::Rect2(0x3C, 0x84, 0x108, 0xDC); }
    static gfx::Vec2 RULES_AT() { return gfx::Vec2(0x38, 0x80); }
    static constexpr gd::Int RULES_WIDTH = 0x100;
    static gfx::Rect2 HOW_TO_CARD() { return gfx::Rect2(0x40, 0x70, 0xF0, 0xEC); }
    static gfx::Rect2 HOW_TO_SHADOW() { return gfx::Rect2(0x46, 0x78, 0xF0, 0xEC); }

    FightHud* hud = nullptr;
    gd::Int player = 0;                  // 1 or 2 while shown
    gd::Int cursor = 0;
    gd::Int items = PauseMenu::ITEMS.size() - 1;
    bool blink = false;
    bool how_to = false;                 // Tekken Ball's HOW TO page is shown
    gfx::TextureRef _how_to_picture;
    gd::Ref<TextLocale> locale;          // the texts' language: the items and the rules picture
    // TODO: Assets (remake/platform/assets.gd) is not ported: the imported root Assets.path reads
    // from, set by the owner.
    std::string assets_root = "";

    // The Control's state.
    bool visible = false;
    gfx::Vec2 size;
    gfx::Canvas* target = nullptr;       // the canvas `_draw` draws into (set by `draw`)

    void setup(FightHud* fight_hud);
    // Drops the rules picture so the next HOW TO page loads it in the texts' language.
    void reset_pictures();
    void show_state(const gd::Ref<FightSimulation>& sim);
    // Sets `size` and draws (the app draws only visible views).
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw();
    // volley.ovl FUN_800B4088: the rules picture (216 x 204) on a white card with a black shadow.
    void _draw_how_to();
};

}  // namespace t3

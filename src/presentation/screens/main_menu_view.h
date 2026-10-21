#pragma once
// remake/presentation/screens/main_menu_view.gd: the main menu (title.ovl FUN_800DBBD0's drawing,
// menu_sim._main_menu_draw): the title picture, the cursor's bars once the list rests, and in the band
// y 316-431 (a draw area) seven entries of the list in font 1 around the cursor (28 pixels apart,
// sliding), darkened at the band's edges by two subtracted gradients. A NEW entry blinks in colour 14.
#include "core/game/main_menu.h"
#include "presentation/screens/screen_canvas.h"

namespace t3 {

class MainMenuView : public ScreenCanvas {
public:
    static constexpr gd::Int LIST_Y = 0x116;
    static constexpr gd::Int LINE = 0x1C;
    static constexpr gd::Int CENTRE = 0xB8;
    static gfx::Rect2 BAND() { return gfx::Rect2(0x30, 0x13C, 0x140, 0x74); }
    static constexpr gd::Int NEW_COLOUR = 14;

    gfx::TextureRef title;
    gd::Ref<MainMenu> menu;
    gd::Int frame_count = 0;
    // Native: NETWORK MODE's pages in the list's place (lines around the cursor's band, in their
    // colours), and a line under the band (the page's name, the host's address, the session).
    static constexpr gd::Int FOOTER_Y = 0x1C0;
    std::vector<std::string> page;
    std::vector<gd::Int> page_colours;
    gd::Int page_cursor = 0;
    std::string footer;
    gd::Int footer_colour = 6;

    void setup(const gd::Ref<HudData>& hud_data, const gfx::TextureRef& title_picture);
    void show_state(const gd::Ref<MainMenu>& main_menu, gd::Int frames);
    void _draw() override;
    void _list();
    void _fades();
    void _bars(gd::Int glow);
};

}  // namespace t3

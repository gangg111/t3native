#pragma once
// Native: NETWORK MODE's line over the game outside the main menu (WAITING FOR PLAYER 2, PLAYER 1
// LEFT): the game's font on a dark strip across the top of the 2D screen.
#include <string>

#include "presentation/screens/screen_canvas.h"

namespace t3 {

class NetBanner : public ScreenCanvas {
public:
    static constexpr gd::Int TOP = 0x14, HEIGHT = 0x16;
    std::string text;
    gd::Int colour = 6;   // white

    void setup(const gd::Ref<HudData>& hud_data) { setup_canvas(hud_data); }
    void _draw() override {
        if (text.empty()) return;
        fill(0, static_cast<float>(TOP), static_cast<float>(SCREEN_X), static_cast<float>(HEIGHT), gfx::Color(0, 0, 0, 0.6f));
        centred(text, 1, colour, static_cast<float>(SCREEN_X) / 2.0f, static_cast<float>(TOP + 3));
    }
};

}  // namespace t3

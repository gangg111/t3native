#pragma once
// remake/content/screen_data.gd
// The attract loop's 2D screen data (`imported/screens/`, tools/remake_import/screens.py):
// the title pictures, texts with their original font, colour and position on the 368 x 480
// screen, and the text colours.
#include <string>

#include "gd/gd.h"
#include "gfx/canvas.h"

namespace t3 {

class ScreenData {
public:
    class Text {
    public:
        std::string text;
        gd::Int font = 0;
        gd::Int colour = 0;
        gd::Vector2i at{0, 0};
    };

    std::string directory;
    gd::Vector2i screen{368, 480};
    gd::Arr<gd::Ref<Text>> presents;
    gd::Arr<gd::Ref<Text>> prompts;        // no pad, P1, P2, both; `at.x` is the centre
    gd::Arr<gd::Ref<Text>> copyright;
    gd::Vector2i logo_at{0, 0};
    gd::PI32 font_advance;
    gd::PI32 font_height;
    // per font, per colour: [top, bottom, outline] Color (untyped Array of Arrays of Array[Color])
    gd::Arr<gd::Arr<gd::Arr<gfx::Color>>> colours;

    static gd::Ref<ScreenData> load_from(const std::string& dir);
    static gd::Ref<Text> _text(const std::string& text, gd::Int font, gd::Int colour, gd::Vector2i at);
    // The centre x of a left-aligned original text run (the original font is fixed-pitch).
    float run_centre(const gd::Ref<Text>& t) const;
    // Fill and outline colours of a text colour: the glyph gradient's upper part, the outline.
    gd::Arr<gfx::Color> text_colour(gd::Int font, gd::Int colour) const;
};

}  // namespace t3

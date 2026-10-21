// remake/tests/presentation/test_print_fmt.gd: ScreenCanvas.print_fmt (FUN_8004D15C's format): the
// text runs it draws, each with its font, colour and pen position. %c / %f / %H / %V end a run, a
// line break returns to the line start (%H), %% prints a percent sign without taking an argument,
// and %d / %x keep their width.
#include <tuple>

#include "presentation/screens/screen_canvas.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

// Records the runs instead of drawing glyphs.
class Recorder : public ScreenCanvas {
public:
    std::vector<std::tuple<std::string, gd::Int, gd::Int, float, float>> calls;
    void text(const std::string& s, gd::Int font_index, gd::Int colour, float x, float y, float) override {
        calls.emplace_back(s, font_index, colour, x, y);
    }
};

gd::Ref<HudData::FontInfo> font(gd::Int advance, gd::Int line) {
    auto info = gd::make<HudData::FontInfo>();
    info->advance = advance;
    info->line = line;
    return info;
}

}  // namespace

T3_TEST(print_fmt, runs_and_pen) {
    Recorder canvas;
    canvas.hud = gd::make<HudData>();
    canvas.hud->fonts.set(0, font(8, 16));
    canvas.hud->fonts.set(1, font(12, 20));
    canvas.print_fmt("AB%3d\nC%cD%fE%HF\nG%s%%%02x%V%C",
                     {gd::Int(7), gd::Int(5), gd::Int(1), gd::Int(40), std::string("xy"), gd::Int(0x1AB), gd::Int(100), gd::Int(65)}, 0, 2);
    using C = std::tuple<std::string, gd::Int, gd::Int, float, float>;
    std::vector<C> want = {
        C{"AB  7", 0, 2, 0.0f, 0.0f},      // %3d: two spaces and the digit
        C{"C", 0, 2, 0.0f, 16.0f},         // after the line break, at the line start
        C{"D", 0, 5, 8.0f, 16.0f},         // %c 5
        C{"E", 1, 5, 16.0f, 16.0f},        // %f 1
        C{"F", 1, 5, 40.0f, 16.0f},        // %H 40 moves the pen and the line start
        C{"Gxy%AB", 1, 5, 40.0f, 36.0f},   // line 2 of font 1 at x 40; %02x keeps two digits
        C{"A", 1, 5, 112.0f, 100.0f},      // %V 100, %C 65
    };
    t.expect_equal(canvas.calls.size(), want.size(), "run count");
    for (size_t i = 0; i < want.size() && i < canvas.calls.size(); ++i)
        t.expect(canvas.calls[i] == want[i], "run " + std::to_string(i) + ": \"" + std::get<0>(canvas.calls[i]) + "\" at " +
                                                 std::to_string(std::get<3>(canvas.calls[i])) + "," + std::to_string(std::get<4>(canvas.calls[i])));
}

T3_TEST(print_fmt, negative_and_large_numbers) {
    t.expect_equal(ScreenCanvas::_number_text(-5, 'd', 3, false), std::string(" -5"), "a minus sign takes a place");
    t.expect_equal(ScreenCanvas::_number_text(123, 'd', 2, false), std::string("23"), "a width keeps the low digits");
    t.expect_equal(ScreenCanvas::_number_text(200'000'000, 'd', 0, false), std::string("99999999"), "past 10^8");
    t.expect_equal(ScreenCanvas::_number_text(100'000'000, 'd', 0, false), std::string("99999999"), "exactly 10^8 (game-bugs #66 not reproduced)");
    t.expect_equal(ScreenCanvas::_number_text(-5, 'D', 0, false), std::string("99999999"), "%D is unsigned: -5 is past 10^8 (draw_sim.py)");
    t.expect_equal(ScreenCanvas::_number_text(42, 'D', 0, false), std::string("42"));
}

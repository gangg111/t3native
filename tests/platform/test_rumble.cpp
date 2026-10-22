// remake/tests/platform/test_rumble.gd: the motors (Rumble): the touch controls' player feels the
// phone's own motor at the stronger of the two levels, rounded to eighths; it stops when the game
// stops asking.
#include <array>
#include <vector>

#include "platform/app.h"
#include "platform/input_router.h"
#include "platform/rumble.h"
#include "support/test_suite.h"

using namespace t3;

namespace {
std::vector<std::pair<int, int>> g_pulses;   // the Platform::vibrate calls (ms, strength)
std::vector<std::array<int, 4>> g_pad_pulses;   // the Platform::pad_rumble calls (device, weak, strong, ms)
}

// t3tests links no platform: its side of the app, recording the motor pulses.
namespace t3::Platform {
void vibrate(int milliseconds, int strength) { g_pulses.emplace_back(milliseconds, strength); }
void perf_line(const std::string&) {}
void pad_rumble(int device, int weak, int strong, int milliseconds) {
    g_pad_pulses.push_back({device, weak, strong, milliseconds});
}
double now() { return 0.0; }
bool ask_text(const std::string&, const std::string&, TextKind) { return false; }
TextAnswer poll_text(std::string&) { return TEXT_NONE; }
}  // namespace t3::Platform

T3_TEST(rumble, touch_drives_the_phone_motor) {
    Rumble rumble;
    rumble.set_motors(InputRouter::TOUCH, false, 255);
    rumble.update();
    t.expect_equal(rumble.handheld_level, 1.0, "the large motor at full strength");
    rumble.set_motors(InputRouter::TOUCH, true, 0);
    rumble.update();
    t.expect_equal(rumble.handheld_level, Rumble::SMALL_SHARE, "the small motor's buzz");
    rumble.set_motors(InputRouter::TOUCH, true, 200);
    rumble.update();
    t.expect_equal(rumble.handheld_level, 7.0 / 8.0, "the stronger one, rounded up to eighths");
    rumble.update();
    t.expect_equal(rumble.handheld_level, 0.0, "no request this frame: still");
    rumble.set_motors(InputRouter::KEYBOARD, true, 255);
    rumble.update();
    t.expect_equal(rumble.handheld_level, 0.0, "a keyboard has no motor");
}

// Native: the pulses reach Platform::vibrate as Input.vibrate_handheld would send them: 100 ms
// (6 frames, rounded) at the level as 1-255, and a 1 ms cut when the level falls to zero.
T3_TEST(rumble, pulses_reach_the_platform) {
    g_pulses.clear();
    Rumble rumble;
    rumble.set_motors(InputRouter::TOUCH, false, 255);
    rumble.update();
    rumble.set_motors(InputRouter::TOUCH, true, 0);
    rumble.update();
    rumble.update();
    t.expect_equal(gd::Int(g_pulses.size()), gd::Int(3), "three pulses");
    if (g_pulses.size() != 3) return;
    t.expect_equal(g_pulses[0].first, 100, "a pulse of HANDHELD_PULSE frames");
    t.expect_equal(g_pulses[0].second, 255, "at full strength");
    t.expect_equal(g_pulses[1].second, 128, "half strength rounds to 128 of 255");
    t.expect_equal(g_pulses[2].first, 1, "the cut");
    t.expect_equal(g_pulses[2].second, 1, "at the least strength");
}

// Native: a pad's own motors (Platform::pad_rumble), not the phone's: a 250 ms pulse on a change,
// renewed after 9 frames, a zero pulse when the game stops asking; the phone stays still.
T3_TEST(rumble, pads_drive_their_own_motors) {
    g_pulses.clear();
    g_pad_pulses.clear();
    Rumble rumble;
    const gd::Int pad = 7;
    for (int frame = 0; frame < 10; ++frame) {
        rumble.set_motors(pad, true, 255);
        rumble.update();
    }
    // source: by hand: a pulse on frame 0, its renewal 9 frames later (frame 9)
    t.expect_equal(gd::Int(g_pad_pulses.size()), gd::Int(2), "the first pulse and one renewal");
    if (g_pad_pulses.size() == 2) {
        t.expect(g_pad_pulses[0] == std::array<int, 4>{7, 255, 255, 250}, "both motors full, 250 ms");
    }
    rumble.set_motors(pad, false, 128);
    rumble.update();
    // source: by hand: 128/255 = 0.502, rounded up to eighths 5/8, 0.625 * 255 = 159.4 -> 159
    t.expect(!g_pad_pulses.empty() && g_pad_pulses.back() == std::array<int, 4>{7, 0, 159, 250}, "a change: a new pulse");
    rumble.update();
    t.expect(!g_pad_pulses.empty() && g_pad_pulses.back() == std::array<int, 4>{7, 0, 0, 0}, "no request: stopped");
    std::size_t sent = g_pad_pulses.size();
    rumble.update();
    t.expect_equal(gd::Int(g_pad_pulses.size()), gd::Int(sent), "stopped once only");
    t.expect(g_pulses.empty(), "the phone's motor stays still");
}

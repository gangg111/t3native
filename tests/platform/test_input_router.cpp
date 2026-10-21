// remake/tests/platform/test_input_router.gd: pad words and device assignment of the input router.
#include "platform/input_router.h"
#include "platform/keys.h"
#include "platform/pad_state.h"
#include "support/test_suite.h"

using namespace t3;

T3_TEST(input_router, devices_join_in_order_and_only_once) {
    InputRouter router;
    t.expect_equal(router.assign(InputRouter::KEYBOARD), gd::Int(0));
    t.expect_equal(router.assign(3), gd::Int(1));
    t.expect_equal(router.assign(4), gd::Int(-1), "no free player");
    t.expect_equal(gd::Int(router.devices[0]), InputRouter::KEYBOARD);
}

T3_TEST(input_router, unassigned_player_reads_nothing) {
    InputRouter router;
    t.expect_equal(router.read_device(InputRouter::NO_DEVICE), gd::Int(0));
}

T3_TEST(input_router, pad_bits_follow_libetc) {
    // The game's face buttons: square 0x80, triangle 0x10, cross 0x40, circle 0x20; directions 0x1000-0x8000.
    t.expect_equal(PadState::SQUARE, gd::Int(0x80));
    t.expect_equal(PadState::TRIANGLE, gd::Int(0x10));
    t.expect_equal(PadState::CROSS, gd::Int(0x40));
    t.expect_equal(PadState::CIRCLE, gd::Int(0x20));
    t.expect_equal(PadState::UP | PadState::RIGHT | PadState::DOWN | PadState::LEFT, gd::Int(0xF000));
    t.expect_equal(PadState::describe(PadState::START | PadState::L1), std::string("L1 Start"));
}

T3_TEST(input_router, two_keyboard_layouts_are_two_players) {
    InputRouter router;
    router.key_event(Keys::LEFT, true);
    router.key_event(Keys::letter('A'), true);
    router.key_event(Keys::LEFT, false);
    router.key_event(Keys::LEFT, true);
    t.expect_equal(gd::Int(router.devices[0]), InputRouter::KEYBOARD, "WASD takes player 1 though pressed second");
    t.expect_equal(gd::Int(router.devices[1]), InputRouter::KEYBOARD_2, "the arrows take player 2");
    auto left = InputRouter::default_layout(0), right = InputRouter::default_layout(1);
    for (const auto& [key, bit] : left) t.expect(!right.count(key), "a key belongs to one layout only");
}

T3_TEST(input_router, rebound_layouts_pick_the_device) {
    InputRouter router;
    router.layouts[1] = {{Keys::letter('A'), PadState::LEFT}};
    router.layouts[0] = {{Keys::letter('H'), PadState::LEFT}};
    router.key_event(Keys::letter('A'), true);
    t.expect_equal(gd::Int(router.devices[1]), InputRouter::KEYBOARD_2, "A now belongs to the right layout");
    t.expect(router.keyboard_seen, "a key was seen");
    t.expect(router.has_keyboard(), "a keyboard at hand");
}

T3_TEST(input_router, escape_keeps_the_buttons_of_its_press) {
    InputRouter router;
    router.escape_buttons = PadState::START;
    t.expect_equal(router.escape_latch(true), PadState::START, "the pause in a fight");
    router.escape_buttons = PadState::START | PadState::SELECT;
    t.expect_equal(router.escape_latch(true), PadState::START, "held across a screen change");
    t.expect_equal(router.escape_latch(false), gd::Int(0), "released");
    t.expect(!router.escape_pressed, "held, not pressed");
    t.expect_equal(router.escape_latch(true), PadState::START | PadState::SELECT, "the next press");
    t.expect(router.escape_pressed, "pressed");
}

T3_TEST(input_router, escape_goes_to_the_keyboard_player_without_joining) {
    InputRouter router;
    router.key_event(Keys::ESCAPE, true);
    t.expect(router.devices == gd::PI32{InputRouter::NO_DEVICE, InputRouter::NO_DEVICE}, "Escape takes no player");
    t.expect_equal(router.escape_owner(), gd::Int(0), "no keyboard: player 1");
    router.assign(3);
    t.expect_equal(router.escape_owner(), gd::Int(0), "a pad on player 1: still player 1");
    router.assign(InputRouter::KEYBOARD);
    t.expect_equal(gd::Int(router.devices[1]), InputRouter::KEYBOARD, "the keyboard joins as player 2");
    t.expect_equal(router.escape_owner(), gd::Int(1), "then Escape is player 2's");
}

// The system's Back (Android) presses Escape for a few frames: the buttons Escape stands for.
T3_TEST(input_router, back_presses_escape) {
    InputRouter router;
    router.escape_buttons = PadState::SELECT;
    router.back();
    router._physics_process();
    t.expect(router.escape_pressed, "pressed on the first frame");
    t.expect_equal(gd::Int(router.held[0]), PadState::SELECT, "Select for player 1");
    for (gd::Int i = 0; i < InputRouter::BACK_FRAMES - 1; ++i) router._physics_process();
    t.expect_equal(gd::Int(router.held[0]), PadState::SELECT, "held for BACK_FRAMES frames");
    router._physics_process();
    t.expect_equal(gd::Int(router.held[0]), gd::Int(0), "then released");
    router.back_allowed = false;
    router.back();
    router._physics_process();
    t.expect(!router.escape_pressed && router.held[0] == 0, "nothing where the game does not allow it");
}

// A gamepad joins on its stick and triggers too, once the axis has been seen at rest.
T3_TEST(input_router, stick_and_triggers_join) {
    InputRouter router;
    router.joy_axis(5, InputRouter::AXIS_LEFT_X, -0.9f);
    t.expect(!router.devices.has(5), "never seen at rest (a drifting stick): no join");
    router.joy_axis(5, InputRouter::AXIS_LEFT_X, InputRouter::STICK_DEADZONE * 0.5f);
    t.expect(!router.devices.has(5), "a stick inside its dead zone does not join");
    router.joy_axis(5, InputRouter::AXIS_LEFT_X, -0.9f);
    t.expect_equal(gd::Int(router.devices[0]), gd::Int(5), "pushed from rest, the stick joins its pad");
    router.joy_axis(6, InputRouter::AXIS_TRIGGER_RIGHT, 0.0f);
    router.joy_axis(6, InputRouter::AXIS_TRIGGER_RIGHT, 0.9f);
    t.expect_equal(gd::Int(router.devices[1]), gd::Int(6), "a squeezed trigger joins its pad");
    InputRouter other;
    other.joy_axis(7, InputRouter::AXIS_TRIGGER_RIGHT, 1.0f);
    t.expect_equal(gd::Int(other.devices[0]), gd::Int(7), "a digital trigger (0 to 1 in one event) joins on its first press");
    router.joy_connection(5, false);
    router.joy_connection(5, true);
    router.joy_axis(5, InputRouter::AXIS_LEFT_X, -0.9f);
    t.expect(!router.devices.has(5), "another pad under the same id is not known to rest");
}

T3_TEST(input_router, axis_bits) {
    t.expect_equal(InputRouter::axis_bits(InputRouter::AXIS_LEFT_X, -0.9f), PadState::LEFT);
    t.expect_equal(InputRouter::axis_bits(InputRouter::AXIS_LEFT_Y, 0.9f), PadState::DOWN);
    t.expect_equal(InputRouter::axis_bits(InputRouter::AXIS_LEFT_Y, 0.2f), gd::Int(0), "inside the dead zone");
    t.expect_equal(InputRouter::axis_bits(InputRouter::AXIS_TRIGGER_LEFT, 0.9f), PadState::L2);
    t.expect_equal(InputRouter::axis_bits(2, 1.0f), gd::Int(0), "the right stick presses nothing");
}

T3_TEST(input_router, escape_owner_and_reserved_stats_key) {
    InputRouter router;
    router.assign(InputRouter::KEYBOARD_2);
    t.expect_equal(router.escape_owner(), gd::Int(1), "the keyboard's player");
    t.expect(InputRouter::reserved(Keys::F3), "F3 is no layout's key");
}

// Escape's buttons go to the player the game names, latched with them on the press.
T3_TEST(input_router, escape_player_is_latched) {
    InputRouter router;
    router.assign(InputRouter::KEYBOARD);
    gd::Vector2i rules(PadState::START, 1);
    router.escape_rules = [&rules] { return rules; };
    router.back();
    router._physics_process();
    t.expect_equal(gd::Int(router.held[1]), PadState::START, "the fight's human gets Start");
    t.expect_equal(gd::Int(router.held[0]), gd::Int(0), "not the keyboard's player");
    rules = gd::Vector2i(PadState::SELECT, -1);
    router._physics_process();
    t.expect_equal(gd::Int(router.held[1]), PadState::START, "held across the screen change: the same buttons, the same player");
    t.expect_equal(gd::Int(router.held[0]), gd::Int(0), "nothing moves to the keyboard's player");
    for (gd::Int i = 0; i < InputRouter::BACK_FRAMES; ++i) router._physics_process();
    router.back();
    router._physics_process();
    t.expect_equal(gd::Int(router.held[0]), PadState::SELECT, "the next press follows the new rules");
}

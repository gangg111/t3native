// Native: Input.get_connected_joypads() from the platforms' connection reports (App::pad_connected),
// as game.gd counts them for the title's start prompt and InputRouter frees a gone pad's player.
#include "platform/connected_pads.h"
#include "platform/input_router.h"
#include "support/test_suite.h"

using namespace t3;

T3_TEST(connected_pads, counts_changes_only) {
    ConnectedPads pads;
    t.expect_equal(pads.size(), gd::Int(0));
    t.expect(pads.report(0, true), "a new pad is a change");
    t.expect(!pads.report(0, true), "a repeated report is not");
    t.expect(pads.report(3, true), "a second pad");
    t.expect_equal(pads.size(), gd::Int(2));
    t.expect(!pads.report(7, false), "a pad never seen going is not a change");
    t.expect(pads.report(0, false), "the first pad goes");
    t.expect(!pads.report(0, false), "and only once");
    t.expect_equal(pads.size(), gd::Int(1));
    t.expect(pads.has(3) && !pads.has(0), "the pad left is the second");
    // game.gd: attract_screen.prompt = 3 if _joypads >= 2 else 1
    gd::Int prompt = pads.size() >= 2 ? 3 : 1;
    t.expect_equal(prompt, gd::Int(1), "one pad: the P1 prompt");
}

T3_TEST(connected_pads, a_gone_pad_frees_its_player) {
    ConnectedPads pads;
    InputRouter router;
    auto report = [&](int device, bool connected) {
        if (pads.report(device, connected)) router.joy_connection(device, connected);
    };
    report(4, true);
    report(5, true);
    router.joy_button(4, PadButton::A, true);
    router.joy_button(5, PadButton::A, true);
    t.expect_equal(gd::Int(router.devices[0]), gd::Int(4));
    t.expect_equal(gd::Int(router.devices[1]), gd::Int(5));
    t.expect_equal(pads.size(), gd::Int(2), "both pads: the prompt for both players");
    report(4, false);
    t.expect_equal(gd::Int(router.devices[0]), InputRouter::NO_DEVICE, "player 1's pad went");
    t.expect_equal(gd::Int(router.devices[1]), gd::Int(5), "player 2 keeps theirs");
    report(4, true);
    router.joy_button(4, PadButton::A, true);
    t.expect_equal(gd::Int(router.devices[0]), gd::Int(4), "the pad back: its held button was forgotten, a press joins");
}

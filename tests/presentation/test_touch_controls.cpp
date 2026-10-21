// remake/tests/presentation/test_touch_controls.gd: the touch controls (TouchControls): each finger
// on its own, the face buttons, the stick's eight directions past its dead zone, sliding between
// buttons, the macros, and the pad word that InputRouter reads as the touch device. The `Pads`
// autoload is a router of each test's own. At the end, the native layout edit (not in the remake):
// moving, resizing (handle and pinch), the safe area, Done / Reset and the saved file.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

#include "content/fight_content.h"
#include "content/flow_data.h"
#include "core/fight/rule_set.h"
#include "core/game/game_flow.h"
#include "core/game/options_screen.h"
#include "platform/input_router.h"
#include "platform/pad_state.h"
#include "presentation/hud/touch_controls.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

const gfx::Vec2 WINDOW(1280, 720);
constexpr float TAU = 6.28318530717958648f;

// A TouchControls with its own `Pads` (the remake's `_controls(macros)`).
struct Touch {
    InputRouter Pads;
    TouchControls tc;
    explicit Touch(const std::string& macros, gfx::Vec2 window = WINDOW) {
        tc.Pads = &Pads;
        tc.size = window;
        tc.set_macros(macros);
    }
};

gfx::Vec2 centre_of(const TouchControls& tc, gd::Int bits) {
    for (const auto& c : tc._controls) {
        if (c->bits == bits) {
            if (c->rect.size.x == 0 && c->rect.size.y == 0) return c->centre;
            return c->rect.position + c->rect.size * 0.5f;
        }
    }
    return gfx::Vec2(INFINITY, INFINITY);
}

gd::Ref<TouchControls::Control2> control_of(const TouchControls& tc, gd::Int bits) {
    for (const auto& c : tc._controls) {
        if (c->bits == bits) return c;
    }
    return nullptr;
}

bool is_round(const TouchControls::Control2& c) { return c.rect.size.x == 0 && c.rect.size.y == 0; }
float distance(gfx::Vec2 a, gfx::Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
// Godot's is_equal_approx for floats.
bool approx(float a, float b) { return std::fabs(a - b) <= std::max(1e-5f, std::fabs(a) * 1e-5f); }
bool same(gfx::Vec2 a, gfx::Vec2 b) { return approx(a.x, b.x) && approx(a.y, b.y); }
gfx::Rect2 grow(const gfx::Rect2& r, float by) {
    return gfx::Rect2(r.position - gfx::Vec2(by, by), r.size + gfx::Vec2(2 * by, 2 * by));
}
bool encloses(const gfx::Rect2& a, const gfx::Rect2& b) {
    return b.position.x >= a.position.x && b.position.y >= a.position.y && b.end().x <= a.end().x && b.end().y <= a.end().y;
}
bool intersects(const gfx::Rect2& a, const gfx::Rect2& b) {
    return a.position.x < b.end().x && a.end().x > b.position.x && a.position.y < b.end().y && a.end().y > b.position.y;
}

Pointer finger(int id, gfx::Vec2 at) { return Pointer{id, at.x, at.y}; }

}  // namespace

T3_TEST(touch_controls, buttons_and_stick) {
    Touch f("none");
    TouchControls& tc = f.tc;
    gfx::Vec2 cross = centre_of(tc, PadState::CROSS);
    t.expect(tc.press(0, cross), "a face button takes the finger");
    t.expect_equal(tc.bits, PadState::CROSS, "Cross held");
    gfx::Vec2 start(200, 500);
    t.expect(tc.press(1, start), "the stick zone takes a second finger");
    tc.move(1, start + gfx::Vec2(0, -3 * tc._unit));
    t.expect_equal(tc.bits, PadState::CROSS | PadState::UP, "up with Cross");
    tc.move(1, start + gfx::Vec2(2 * tc._unit, 2 * tc._unit));
    t.expect_equal(tc.bits, PadState::CROSS | PadState::DOWN | PadState::RIGHT, "down-right");
    tc.move(1, start + gfx::Vec2(0.1f * tc._unit, 0));
    t.expect_equal(tc.bits, PadState::CROSS, "inside the dead zone: no direction");
    tc.move(0, centre_of(tc, PadState::CIRCLE));
    t.expect_equal(tc.bits, PadState::CIRCLE, "the finger slid from Cross to Circle");
    tc.release(0);
    tc.release(1);
    t.expect_equal(tc.bits, gd::Int(0), "all lifted");
    t.expect((f.Pads.read_device(InputRouter::TOUCH) & PadState::CIRCLE) != 0, "a press lifted within the frame still counts");
    t.expect(!tc.press(2, gfx::Vec2(WINDOW.x * 0.75f, WINDOW.y * 0.5f)), "an empty place on the right is not taken");
    t.expect(tc.press(3, centre_of(tc, PadState::START)), "Start");
    t.expect_equal(tc.bits, PadState::START, "Start held");
    tc.reset();
    t.expect_equal(f.Pads.touch_bits, gd::Int(0), "the reset clears the router's touch word");
}

T3_TEST(touch_controls, stick_belongs_to_its_first_finger) {
    Touch f("none");
    TouchControls& tc = f.tc;
    gfx::Vec2 start(200, 500);
    t.expect(tc.press(0, start), "the first finger takes the stick");
    t.expect(tc.press(1, start + gfx::Vec2(20, 0)), "a second finger in the zone is taken too");
    t.expect_equal(tc.bits, gd::Int(0), "it does not move the stick");
    tc.move(0, start + gfx::Vec2(0, -3 * tc._unit));
    t.expect_equal(tc.bits, PadState::UP, "the first finger steers");
    tc.release(0);
    t.expect_equal(tc.bits, gd::Int(0), "its lift frees the stick");
    t.expect(!tc._fingers.has(1), "the second finger is not tracked");
    t.expect(tc.press(2, start), "a new finger takes the freed stick");
    tc.move(2, start + gfx::Vec2(3 * tc._unit, 0));
    t.expect_equal(tc.bits, PadState::RIGHT, "and steers it");
    tc.release(2);
    t.expect_equal(tc.bits, gd::Int(0), "all lifted");
}

// The lift of a finger can be lost (an app switch, a system gesture): losing the focus lets every
// finger go, so that the stick is not held by a finger that is gone.
T3_TEST(touch_controls, losing_the_focus_lets_the_fingers_go) {
    Touch f("none");
    TouchControls& tc = f.tc;
    gfx::Vec2 start(200, 500);
    t.expect(tc.press(1, start), "a stick finger");
    tc.move(1, start + gfx::Vec2(3 * tc._unit, 0));
    t.expect_equal(tc.bits, PadState::RIGHT, "steering");
    tc._notification(TouchControls::NOTIFICATION_APPLICATION_FOCUS_OUT);
    t.expect_equal(tc.bits, gd::Int(0), "everything let go");
    t.expect(tc.press(0, start), "a new finger, another index, takes the stick");
    tc.move(0, start + gfx::Vec2(0, 3 * tc._unit));
    t.expect_equal(tc.bits, PadState::DOWN, "and steers it");
    tc.reset();
}

T3_TEST(touch_controls, macros) {
    Touch f("throws");
    TouchControls& tc = f.tc;
    gd::Int throw_ = PadState::SQUARE | PadState::CROSS;
    t.expect(tc.press(0, centre_of(tc, throw_)), "the 1+3 macro button");
    t.expect_equal(tc.bits, throw_, "1+3 held at once");
    t.expect_equal(f.Pads.read_device(InputRouter::TOUCH), throw_, "InputRouter reads the touch word");
    tc.reset();
    t.expect_equal(Touch("none").tc._controls.size(), gd::Int(10), "four face buttons, Start, Select, L1, L2, R1 and R2");
    t.expect_equal(Touch("all").tc._controls.size(), gd::Int(14), "and four macros");
}

// The shoulder buttons in one row, L1 L2 over the stick and R2 R1 over the face buttons, Select and Start at
// the bottom in the middle, the macros in the top corners; every control inside the window and
// none overlapping another, at every size setting, on wide and 4:3 screens.
static void check_layout(t3test::Context& t, gfx::Vec2 window, gd::Int share) {
    Touch f("all", window);
    TouchControls& tc = f.tc;
    tc.set_scale_share(static_cast<float>(share) / 100.0f);
    std::string at = gd::str(share) + "% at (" + gd::str(gd::Int(window.x)) + ", " + gd::str(gd::Int(window.y)) + ")";
    gfx::Vec2 l1 = centre_of(tc, PadState::L1);
    gfx::Vec2 l2 = centre_of(tc, PadState::L2);
    gfx::Vec2 r1 = centre_of(tc, PadState::R1);
    gfx::Vec2 r2 = centre_of(tc, PadState::R2);
    gfx::Vec2 triangle = centre_of(tc, PadState::TRIANGLE);
    t.expect(l1.x < l2.x && l2.x < window.x * 0.3f && l1.y < triangle.y, "L1 L2 over the stick (" + at + ")");
    t.expect(r2.x < triangle.x && r1.x > triangle.x && r1.y < triangle.y, "R2 R1 over the face buttons (" + at + ")");
    t.expect(approx(l1.y, r1.y) && approx(l2.y, r2.y) && approx(l1.y, l2.y), "all four on one level (" + at + ")");
    gfx::Vec2 select = centre_of(tc, PadState::SELECT);
    gfx::Vec2 start = centre_of(tc, PadState::START);
    float middle = (select.x + start.x) / 2.0f;
    t.expect(select.x < start.x && select.y > window.y * 0.8f && middle <= window.x / 2.0f + 0.01f && middle > window.x * 0.4f,
             "Select and Start at the bottom, in the middle or left of it (" + at + ")");
    if (window.x >= window.y * 16.0f / 9.0f - 1.0f) {
        t.expect(approx(middle, window.x / 2.0f), "in the middle on wide screens (" + at + ")");
    }
    gfx::Vec2 throw_1 = centre_of(tc, PadState::SQUARE | PadState::CROSS);
    gfx::Vec2 throw_2 = centre_of(tc, PadState::TRIANGLE | PadState::CIRCLE);
    t.expect(throw_1.x < window.x * 0.25f && throw_2.x > window.x * 0.75f && throw_1.y < window.y * 0.2f,
             "the throws in the top corners (" + at + ")");
    t.expect(tc.top_left_free.position.x > centre_of(tc, PadState::SQUARE | PadState::TRIANGLE).x,
             "the free place right of the left macros (" + at + ")");
    std::vector<gfx::Rect2> boxes;
    for (const auto& c : tc._controls) {
        boxes.push_back(!is_round(*c) ? c->rect
                                      : gfx::Rect2(c->centre - gfx::Vec2(c->radius, c->radius), gfx::Vec2(2 * c->radius, 2 * c->radius)));
    }
    gd::Int n = static_cast<gd::Int>(boxes.size());
    for (gd::Int i = 0; i < n; ++i) {
        t.expect(encloses(gfx::Rect2(gfx::Vec2(0, 0), window), boxes[i]), "control " + gd::str(i) + " inside the window (" + at + ")");
        for (gd::Int j = i + 1; j < n; ++j) {
            const auto& a = tc._controls[i];
            const auto& b = tc._controls[j];
            bool apart = is_round(*a) && is_round(*b) ? distance(a->centre, b->centre) > a->radius + b->radius
                                                      : !intersects(boxes[i], boxes[j]);
            t.expect(apart, "controls " + gd::str(i) + " and " + gd::str(j) + " apart (" + at + ")");
            // A rectangle's touch area (taken before any round button's) must not reach a round
            // button's touch area.
            if (is_round(*a) != is_round(*b)) {
                const auto& round = is_round(*a) ? a : b;
                gfx::Rect2 rect = grow((round == a ? b : a)->rect, TouchControls::RECT_SLACK * tc._unit);
                gfx::Vec2 nearest(std::clamp(round->centre.x, rect.position.x, rect.end().x),
                                  std::clamp(round->centre.y, rect.position.y, rect.end().y));
                t.expect(distance(nearest, round->centre) > round->radius * TouchControls::HIT_SLACK,
                         "touch areas of controls " + gd::str(i) + " and " + gd::str(j) + " apart (" + at + ")");
            }
        }
    }
    t.expect(tc.press(0, l1) && tc.bits == PadState::L1, "L1 pressed (" + at + ")");
    tc.reset();
}

T3_TEST(touch_controls, layout) {
    // GameSettings.SCHEMA["touch_size"]'s choices.
    for (gfx::Vec2 window : {WINDOW, gfx::Vec2(2400, 1080), gfx::Vec2(1024, 768)}) {
        for (gd::Int share : {70, 85, 100, 115, 130}) check_layout(t, window, share);
    }
}

// The key configuration page shows all four shoulder buttons, assigned or not.
T3_TEST(touch_controls, key_configuration_shows_every_shoulder) {
    if (!t.require(t3test::imported("tables/flow.json"))) return;
    auto content = FightContent::load_from(t3test::paths().imported);
    auto data = FlowData::load_from(t3test::imported("tables/flow.json"));
    auto flow = gd::make<GameFlow>(content, data, RuleSet::original(), 1);
    gd::Int all = PadState::L1 | PadState::L2 | PadState::R1 | PadState::R2;
    t.expect_equal(TouchControls::shoulders_shown(*flow), gd::Int(0), "none assigned by default");
    flow->state = GameFlow::State::OPTIONS;
    flow->options->page = OptionsScreen::PAGE_RECORDS;
    t.expect_equal(TouchControls::shoulders_shown(*flow), gd::Int(0), "other options pages");
    flow->options->page = OptionsScreen::PAGE_KEYS;
    t.expect_equal(TouchControls::shoulders_shown(*flow), all, "the key configuration page");
    flow->state = GameFlow::State::MENU;
    t.expect_equal(TouchControls::shoulders_shown(*flow), gd::Int(0), "left the options");
}

// Only the shoulder buttons the key configuration uses are shown; the others leave their places
// empty (a touch there is not taken).
T3_TEST(touch_controls, unassigned_shoulders_hidden) {
    Touch f("none");
    TouchControls& tc = f.tc;
    gfx::Vec2 r1 = centre_of(tc, PadState::R1);
    gfx::Vec2 l1 = centre_of(tc, PadState::L1);
    tc.set_shoulders(0);
    t.expect_equal(tc._controls.size(), gd::Int(6), "none by default: four face buttons, Select and Start");
    t.expect(!tc.press(0, r1), "R1's place is empty");
    tc.set_shoulders(PadState::L1);
    t.expect_equal(tc._controls.size(), gd::Int(7), "L1 once it has an action");
    t.expect(same(centre_of(tc, PadState::L1), l1), "in its own place");
    t.expect(tc.press(1, l1) && tc.bits == PadState::L1, "and pressed");
    tc.reset();
}

// A shoulder button that goes away under a finger lets go; one that stays keeps its finger.
T3_TEST(touch_controls, hidden_shoulder_lets_go) {
    Touch f("none");
    TouchControls& tc = f.tc;
    tc.set_shoulders(PadState::L1 | PadState::R1);
    t.expect(tc.press(0, centre_of(tc, PadState::L1)), "L1 held");
    t.expect(tc.press(1, centre_of(tc, PadState::R1)), "R1 held");
    tc.set_shoulders(PadState::R1);
    t.expect_equal(tc.bits, PadState::R1, "L1 is gone and let go; R1 stays held");
    tc.release(1);
    t.expect_equal(tc.bits, gd::Int(0), "R1 lifted");
    tc.reset();
}

// While something plays only Select and Start stay, in their places; the stick is gone.
T3_TEST(touch_controls, playback_keeps_select_and_start) {
    Touch f("throws");
    TouchControls& tc = f.tc;
    gfx::Vec2 start = centre_of(tc, PadState::START);
    gfx::Vec2 cross = centre_of(tc, PadState::CROSS);
    t.expect(tc.press(0, cross), "Cross held");
    tc.set_playback(true);
    t.expect_equal(tc.bits, gd::Int(0), "the fingers down let go");
    t.expect_equal(tc._controls.size(), gd::Int(2), "Select and Start only");
    t.expect(same(centre_of(tc, PadState::START), start), "Start in its place");
    t.expect(!tc.press(1, cross), "Cross's place is empty");
    t.expect(!tc.press(2, gfx::Vec2(200, 500)), "no stick");
    t.expect(tc.press(3, start) && tc.bits == PadState::START, "Start skips");
    tc.set_playback(false);
    t.expect_equal(tc._controls.size(), gd::Int(12), "everything back: face buttons, throws, Select, Start, shoulders");
}

T3_TEST(touch_controls, stick_directions) {
    Touch f("none");
    TouchControls& tc = f.tc;
    gd::Int expected[8] = {PadState::RIGHT, PadState::RIGHT | PadState::DOWN, PadState::DOWN, PadState::DOWN | PadState::LEFT,
                           PadState::LEFT, PadState::LEFT | PadState::UP, PadState::UP, PadState::UP | PadState::RIGHT};
    for (gd::Int k = 0; k < 8; ++k) {
        float a = TAU * static_cast<float>(k) / 8.0f;
        gfx::Vec2 offset = gfx::Vec2(std::cos(a), std::sin(a)) * tc._unit;   // Vector2.from_angle
        t.expect_equal(tc.stick_bits(offset), expected[k], "direction " + gd::str(k));
    }
}

// The touch controls take player 1: a keyboard there moves to player 2, a pad on player 1 with
// player 2 taken leaves.
T3_TEST(touch_controls, touch_takes_player_one) {
    Touch f("none");
    TouchControls& tc = f.tc;
    f.Pads.devices = gd::PI32{InputRouter::KEYBOARD, InputRouter::NO_DEVICE};
    tc.press(0, centre_of(tc, PadState::CROSS));
    t.expect(f.Pads.devices == gd::PI32{InputRouter::TOUCH, InputRouter::KEYBOARD}, "the keyboard moves to 2P");
    tc.reset();
    f.Pads.devices = gd::PI32{0, 1};
    tc.press(0, centre_of(tc, PadState::CROSS));
    t.expect(f.Pads.devices == gd::PI32{InputRouter::TOUCH, 1}, "pad 0 leaves, pad 1 stays 2P");
    tc.reset();
}

// Native layout edit ------------------------------------------------------------------------------

// The settings' "Edit layout" opens the edit (enter_edit); the top row's free place (the settings
// button's) takes no tap; while editing, touches give the game no buttons.
T3_TEST(touch_controls, enter_edit_and_touch_gives_no_pad) {
    Touch f("throws");
    TouchControls& tc = f.tc;
    tc.visible = true;
    gfx::Rect2 free = tc.top_left_free;
    gfx::Vec2 at = free.position + gfx::Vec2(free.size.y * 0.5f, free.size.y * 0.5f);
    t.expect(free.size.y > 0 && free.position.x > centre_of(tc, PadState::SQUARE | PadState::CROSS).x,
             "the free place right of the left macro");
    t.expect(!tc.pointer(PointerEvent::DOWN, finger(0, at)), "the free place is empty: the settings button's");
    tc.enter_edit();
    t.expect(tc.editing, "enter_edit opens the edit");
    t.expect_equal(tc._controls.size(), gd::Int(12), "every shoulder button placeable in the edit");
    tc.pointer(PointerEvent::DOWN, finger(2, centre_of(tc, PadState::CROSS)));
    t.expect_equal(tc.bits, gd::Int(0), "a finger on Cross holds nothing");
    t.expect_equal(f.Pads.read_device(InputRouter::TOUCH), gd::Int(0), "the game reads nothing from touch");
    tc.pointer(PointerEvent::UP, finger(2, centre_of(tc, PadState::CROSS)));
    tc.pointer(PointerEvent::DOWN, finger(3, tc._done_rect.position + tc._done_rect.size * 0.5f));
    t.expect(!tc.editing, "Done closes the edit");
    tc.set_shoulders(0);
    t.expect_equal(tc._controls.size(), gd::Int(8), "the unassigned shoulders hidden again");
}

// A drag moves a control, the corner handle and a pinch resize it, the safe area holds them, Reset
// brings the remake's layout back, Done saves and a new start reads the file.
T3_TEST(touch_controls, edit_moves_resizes_saves_and_resets) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "t3native_touch_test";
    std::filesystem::remove_all(dir);
    std::string file = (dir / TouchControls::LAYOUT_FILE).string();
    Touch f("throws");
    TouchControls& tc = f.tc;
    tc.visible = true;
    tc.load_layout(file);
    gfx::Vec2 cross = centre_of(tc, PadState::CROSS);
    float radius = control_of(tc, PadState::CROSS)->radius;
    tc.enter_edit();
    // Move Cross by (-200, -130) (clear of the other controls' centre lines: no snapping).
    tc.pointer(PointerEvent::DOWN, finger(0, cross + gfx::Vec2(5, 5)));
    tc.pointer(PointerEvent::MOVE, finger(0, cross + gfx::Vec2(-195, -125)));
    tc.pointer(PointerEvent::UP, finger(0, cross + gfx::Vec2(-195, -125)));
    gfx::Vec2 moved = centre_of(tc, PadState::CROSS);
    t.expect(same(moved, cross + gfx::Vec2(-200, -130)), "Cross follows the drag");
    // The corner handle: twice as far from the centre, twice the size.
    gfx::Vec2 corner = moved + gfx::Vec2(radius, radius);
    tc.pointer(PointerEvent::DOWN, finger(1, corner));
    tc.pointer(PointerEvent::MOVE, finger(1, moved + gfx::Vec2(radius, radius) * 2.0f));
    tc.pointer(PointerEvent::UP, finger(1, moved + gfx::Vec2(radius, radius) * 2.0f));
    t.expect(approx(control_of(tc, PadState::CROSS)->radius, radius * 2.0f), "the handle doubles it");
    t.expect(same(centre_of(tc, PadState::CROSS), moved), "about its centre");
    // A pinch on Start: the fingers 1.5 times as far apart.
    gfx::Vec2 start = centre_of(tc, PadState::START);
    gfx::Vec2 start_size = control_of(tc, PadState::START)->rect.size;
    tc.pointer(PointerEvent::DOWN, finger(2, start - gfx::Vec2(10, 0)));
    tc.pointer(PointerEvent::DOWN, finger(3, start + gfx::Vec2(10, 0)));
    tc.pointer(PointerEvent::MOVE, finger(2, start - gfx::Vec2(15, 0)));
    tc.pointer(PointerEvent::MOVE, finger(3, start + gfx::Vec2(15, 0)));
    tc.pointer(PointerEvent::UP, finger(2, start));
    tc.pointer(PointerEvent::UP, finger(3, start));
    t.expect(approx(control_of(tc, PadState::START)->rect.size.x, start_size.x * 1.5f), "the pinch makes Start 1.5 times as wide");
    t.expect(same(centre_of(tc, PadState::START), start), "in its place");
    // Dragged past the left edge, Circle stops at the safe area.
    tc.set_window(WINDOW, SafeArea{100, 0, 0, 0});
    gfx::Vec2 circle = centre_of(tc, PadState::CIRCLE);
    tc.pointer(PointerEvent::DOWN, finger(4, circle));
    tc.pointer(PointerEvent::MOVE, finger(4, gfx::Vec2(-50, circle.y)));
    tc.pointer(PointerEvent::UP, finger(4, gfx::Vec2(-50, circle.y)));
    auto c = control_of(tc, PadState::CIRCLE);
    t.expect(approx(c->centre.x - c->radius, 100.0f), "Circle stops at the safe area's left edge");
    // The stick's rest moves, and the stick then starts there.
    gfx::Vec2 hint = tc._stick_hint;
    // A place clear of every control's centre lines (no snapping), 150 px right.
    gfx::Vec2 to = hint + gfx::Vec2(150, 0);
    for (float dy = -40.0f; dy > -300.0f; dy -= 5.0f) {
        bool clear = true;
        for (const auto& k : tc._controls) {
            gfx::Vec2 kc = k->rect.size.x == 0 ? k->centre : k->rect.position + k->rect.size * 0.5f;
            float near = TouchControls::SNAP * tc._unit * 1.5f;
            if (std::fabs(kc.x - (hint.x + 150.0f)) < near || std::fabs(kc.y - (hint.y + dy)) < near) clear = false;
        }
        if (clear) {
            to = hint + gfx::Vec2(150, dy);
            break;
        }
    }
    tc.pointer(PointerEvent::DOWN, finger(5, hint));
    tc.pointer(PointerEvent::MOVE, finger(5, to));
    tc.pointer(PointerEvent::UP, finger(5, to));
    t.expect(same(tc._stick_hint, to), "the stick's rest follows the drag");
    tc.pointer(PointerEvent::DOWN, finger(6, tc._done_rect.position + tc._done_rect.size * 0.5f));
    t.expect(!tc.editing, "Done");
    t.expect(std::filesystem::exists(file), "the layout is saved");
    gfx::Vec2 stick = tc._stick_hint;
    t.expect(tc.press(7, stick) && tc._fingers.at(7).stick, "a finger on the moved stick takes it");
    tc.release(7);
    // A new start reads the saved layout.
    Touch g("throws");
    g.tc.set_window(WINDOW, SafeArea{100, 0, 0, 0});
    g.tc.load_layout(file);
    t.expect(same(centre_of(g.tc,PadState::CROSS), moved), "Cross where it was left");
    t.expect(approx(control_of(g.tc,PadState::CROSS)->radius, radius * 2.0f), "at its size");
    t.expect(same(g.tc._stick_hint, stick), "the stick too");
    // Reset: the remake's layout, saved by Done.
    g.tc.visible = true;
    g.tc.enter_edit();
    g.tc.pointer(PointerEvent::DOWN, finger(0, g.tc._reset_rect.position + g.tc._reset_rect.size * 0.5f));
    g.tc.pointer(PointerEvent::DOWN, finger(1, g.tc._done_rect.position + g.tc._done_rect.size * 0.5f));
    g.tc.set_window(WINDOW, SafeArea{});
    t.expect(same(centre_of(g.tc,PadState::CROSS), cross), "Cross back in the remake's place");
    Touch h("throws");
    h.tc.load_layout(file);
    t.expect(h.tc.placements.is_empty(), "the reset layout is saved empty");
    std::filesystem::remove_all(dir);
}

// The edit shows each control's size in pixels; − and + change the last control touched by one pixel.
T3_TEST(touch_controls, edit_sizes_in_pixels_and_one_pixel_steps) {
    Touch f("throws");
    TouchControls& tc = f.tc;
    tc.visible = true;
    tc.enter_edit();
    float radius = control_of(tc, PadState::CROSS)->radius;
    // source: by hand: the unit is 720 * 0.085 = 61.2 px, Cross's radius 0.78 units = 47.736 px,
    // a diameter of 95.472 px, shown rounded
    t.expect(approx(radius, 47.736f), "Cross's radius 0.78 units");
    t.expect_equal(tc.size_text("Cross"), std::string("95 px"), "its diameter");
    // source: by hand: Start is 1.9 x 0.75 units = 116.28 x 45.9 px
    t.expect_equal(tc.size_text("Start"), std::string("116x46 px"), "Start's width and height");
    gfx::Vec2 plus = tc._plus_rect.position + tc._plus_rect.size * 0.5f;
    gfx::Vec2 minus = tc._minus_rect.position + tc._minus_rect.size * 0.5f;
    tc.pointer(PointerEvent::DOWN, finger(0, plus));
    t.expect(approx(control_of(tc, PadState::CROSS)->radius, radius), "+ does nothing with no control selected");
    gfx::Vec2 cross = centre_of(tc, PadState::CROSS);
    tc.pointer(PointerEvent::DOWN, finger(1, cross));
    tc.pointer(PointerEvent::UP, finger(1, cross));
    t.expect_equal(tc.selected, std::string("Cross"), "the control touched is selected");
    for (int i = 0; i < 3; ++i) {
        tc.pointer(PointerEvent::DOWN, finger(2, plus));
        tc.pointer(PointerEvent::UP, finger(2, plus));
    }
    // source: by hand: 95 + 3 = 98 px, a radius of 49
    t.expect(approx(control_of(tc, PadState::CROSS)->radius, 49.0f), "three + make it 98 px across");
    t.expect_equal(tc.size_text("Cross"), std::string("98 px"), "shown as 98 px");
    t.expect(same(centre_of(tc, PadState::CROSS), cross), "in its place");
    tc.pointer(PointerEvent::DOWN, finger(3, minus));
    t.expect_equal(tc.size_text("Cross"), std::string("97 px"), "- takes one off");
    // Circle set to the same size: equal radii.
    gfx::Vec2 circle = centre_of(tc, PadState::CIRCLE);
    tc.pointer(PointerEvent::DOWN, finger(4, circle));
    tc.pointer(PointerEvent::UP, finger(4, circle));
    tc.pointer(PointerEvent::DOWN, finger(5, plus));
    tc.pointer(PointerEvent::DOWN, finger(6, plus));
    t.expect(approx(control_of(tc, PadState::CIRCLE)->radius, control_of(tc, PadState::CROSS)->radius), "Circle as big as Cross");
}

// A foldable: each window size keeps its own layout; a version 1 file is used where none is saved.
T3_TEST(touch_controls, layouts_per_screen) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "t3native_touch_screens";
    std::filesystem::remove_all(dir);
    std::string file = (dir / TouchControls::LAYOUT_FILE).string();
    const gfx::Vec2 folded(2316, 904), unfolded(2176, 1812);
    Touch f("throws", folded);
    TouchControls& tc = f.tc;
    tc.visible = true;
    tc.load_layout(file);
    tc.set_window(folded, SafeArea{});
    gfx::Vec2 folded_cross = centre_of(tc, PadState::CROSS);
    tc.enter_edit();
    tc.pointer(PointerEvent::DOWN, finger(0, folded_cross));
    tc.pointer(PointerEvent::MOVE, finger(0, folded_cross - gfx::Vec2(300, 0)));
    tc.pointer(PointerEvent::UP, finger(0, folded_cross - gfx::Vec2(300, 0)));
    tc.finish_edit();
    t.expect_equal(tc.screen, std::string("2316x904"), "the folded screen's key");
    // Unfolded: the remake's layout, then an edit of its own.
    tc.set_window(unfolded, SafeArea{});
    Touch d("throws", unfolded);
    d.tc.set_window(unfolded, SafeArea{});
    gfx::Vec2 unfolded_default = centre_of(d.tc, PadState::CROSS);
    t.expect(same(centre_of(tc, PadState::CROSS), unfolded_default), "unfolded: the remake's place");
    tc.enter_edit();
    tc.pointer(PointerEvent::DOWN, finger(1, unfolded_default));
    tc.pointer(PointerEvent::MOVE, finger(1, unfolded_default - gfx::Vec2(0, 200)));
    tc.pointer(PointerEvent::UP, finger(1, unfolded_default - gfx::Vec2(0, 200)));
    tc.finish_edit();
    gfx::Vec2 unfolded_cross = centre_of(tc, PadState::CROSS);
    // Folding again brings the folded layout back.
    tc.set_window(folded, SafeArea{});
    t.expect(same(centre_of(tc, PadState::CROSS), folded_cross - gfx::Vec2(300, 0)), "folded: its own layout");
    // A new start reads both.
    Touch g("throws", unfolded);
    g.tc.load_layout(file);
    g.tc.set_window(unfolded, SafeArea{});
    t.expect(same(centre_of(g.tc, PadState::CROSS), unfolded_cross), "unfolded read back");
    g.tc.set_window(folded, SafeArea{});
    t.expect(same(centre_of(g.tc, PadState::CROSS), folded_cross - gfx::Vec2(300, 0)), "folded read back");
    // source: by hand: 2316/904 = 2.562, 2300/904 = 2.544 (0.7 % off): the folded layout stands in
    g.tc.set_window(gfx::Vec2(2300, 904), SafeArea{});
    t.expect(approx(centre_of(g.tc, PadState::CROSS).x / 2300.0f, (folded_cross.x - 300.0f) / 2316.0f), "a near size uses it");
    // A version 1 file: its controls for every screen.
    Touch h("throws");
    t.expect(h.tc.parse_layout("{\"version\": 1, \"controls\": {\"Cross\": {\"x\": 0.5, \"y\": 0.5, \"scale\": 1}}}"), "version 1");
    h.tc.layout();
    t.expect(same(centre_of(h.tc, PadState::CROSS), gfx::Vec2(640, 360)), "its Cross in the middle");
    std::filesystem::remove_all(dir);
}

// ADD's wizard: toggles, a typed name, OK; the button presses its pad buttons, Done saves it with
// its place; EDIT changes and DELETE removes it.
T3_TEST(touch_controls, wizard_makes_edits_and_deletes_own_buttons) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "t3native_touch_wizard";
    std::filesystem::remove_all(dir);
    std::string file = (dir / TouchControls::LAYOUT_FILE).string();
    Touch f("none");
    TouchControls& tc = f.tc;
    tc.visible = true;
    tc.load_layout(file);
    // source: by hand: four face buttons, Select, Start and the four shoulders
    t.expect_equal(tc._controls.size(), gd::Int(10), "no own buttons at first (no 1+3, 2+4)");
    std::vector<std::pair<std::string, std::string>> asked;
    tc.ask_text = [&](const std::string& title, const std::string& initial) {
        asked.emplace_back(title, initial);
        return true;
    };
    auto tap = [&](gd::Int id, gfx::Vec2 at) {
        tc.pointer(PointerEvent::DOWN, finger(id, at));
        tc.pointer(PointerEvent::UP, finger(id, at));
    };
    auto mid = [](const gfx::Rect2& r) { return r.position + r.size * 0.5f; };
    tc.enter_edit();
    tap(0, mid(tc._new_rect));
    t.expect(tc.wizard.has_value() && tc.wizard->id == 0, "ADD opens the wizard");
    TouchControls::WizardRects w = tc.wizard_rects();
    tap(1, mid(w.ok));
    t.expect(tc.wizard.has_value() && tc.custom.empty(), "OK does nothing with no button chosen");
    tap(2, mid(w.toggles[0]));   // Square
    tap(3, mid(w.toggles[2]));   // Cross
    tap(4, mid(w.toggles[5]));   // L2
    tap(5, mid(w.toggles[5]));   // L2 off again
    t.expect_equal(tc.wizard->steps[0], gd::Int(PadState::SQUARE | PadState::CROSS), "Square and Cross on, L2 toggled off");
    tap(6, mid(w.name));
    t.expect(tc.wizard->asking && asked.size() == 1 && asked[0].second.empty(), "the name asked for, empty so far");
    tap(7, mid(w.ok));
    t.expect(tc.wizard.has_value(), "the wizard waits while the name is typed");
    // source: by hand: 14 characters, at most MAX_LABEL = 12 kept (the Polish letters whole)
    tc.text_entered("  Rzut łokciem!!  ");
    t.expect_equal(tc.wizard->label, std::string("Rzut łokciem"), "the name trimmed to 12 characters");
    tap(8, mid(w.ok));
    t.expect(!tc.wizard.has_value() && tc.custom.size() == 1, "OK makes the button");
    auto own = control_of(tc, PadState::SQUARE | PadState::CROSS);
    t.expect(own != nullptr && own->label == "Rzut łokciem" && own->custom_id == 1, "with its name");
    t.expect_equal(tc.selected, std::string("Button 1"), "and selected");
    // Moved, then Done: saved with its place.
    gfx::Vec2 at = own->centre;
    tc.pointer(PointerEvent::DOWN, finger(9, at));
    tc.pointer(PointerEvent::MOVE, finger(9, gfx::Vec2(640, 300)));
    tc.pointer(PointerEvent::UP, finger(9, gfx::Vec2(640, 300)));
    tc.finish_edit();
    t.expect(tc.press(10, gfx::Vec2(640, 300)), "a finger on it in play");
    t.expect_equal(tc.bits, gd::Int(PadState::SQUARE | PadState::CROSS), "holds Square and Cross at once");
    tc.release(10);
    Touch g("none");
    g.tc.load_layout(file);
    auto back = control_of(g.tc, PadState::SQUARE | PadState::CROSS);
    t.expect(back != nullptr && back->label == "Rzut łokciem" && same(back->centre, gfx::Vec2(640, 300)), "read back, in its place");
    // EDIT: Triangle added, the name kept; DELETE removes it and its place.
    tc.enter_edit();
    tap(11, gfx::Vec2(640, 300));
    tap(12, mid(tc._change_rect));
    t.expect(tc.wizard.has_value() && tc.wizard->id == 1 && tc.wizard->label == "Rzut łokciem", "EDIT opens it with its values");
    tap(13, mid(w.toggles[1]));
    tap(14, mid(w.ok));
    t.expect_equal(tc.custom[0].bits(), gd::Int(PadState::SQUARE | PadState::CROSS | PadState::TRIANGLE), "Triangle added");
    tap(15, mid(tc._change_rect));
    tap(16, mid(tc.wizard_rects().remove));
    t.expect(tc.custom.empty() && !tc.placements.has("Button 1"), "DELETE removes the button and its place");
    t.expect_equal(tc._controls.size(), gd::Int(10), "the ten controls again");
    std::filesystem::remove_all(dir);
}

// Directions in an own button: forward and back are RIGHT and LEFT, turned while the fighter is on
// the right; up and down, forward and back exclude each other in the wizard.
T3_TEST(touch_controls, own_button_directions_turn_with_the_side) {
    Touch f("none");
    TouchControls& tc = f.tc;
    tc.visible = true;
    bool left = false;
    tc.facing_left = [&] { return left; };
    tc.enter_edit();
    tc.open_wizard(0);
    TouchControls::WizardRects w = tc.wizard_rects();
    auto tap = [&](gd::Int id, const gfx::Rect2& r) {
        gfx::Vec2 at = r.position + r.size * 0.5f;
        tc.pointer(PointerEvent::DOWN, finger(id, at));
        tc.pointer(PointerEvent::UP, finger(id, at));
    };
    tap(0, w.toggles[8]);    // UP
    tap(1, w.toggles[9]);    // DOWN: takes UP's place
    tap(2, w.toggles[11]);   // BACK
    tap(3, w.toggles[10]);   // FORWARD: takes BACK's place
    tap(4, w.toggles[2]);    // Cross
    tap(5, w.toggles[3]);    // Circle
    gd::Int want = PadState::DOWN | PadState::RIGHT | PadState::CROSS | PadState::CIRCLE;
    t.expect_equal(tc.wizard->steps[0], want, "down, forward, 3 and 4");
    // source: by hand: the game's notation, directions first
    t.expect_equal(TouchControls::default_label(tc.wizard->steps), std::string("D+F+3+4"), "named D+F+3+4");
    tap(6, w.ok);
    tc.finish_edit();
    auto own = control_of(tc, want);
    t.expect(own != nullptr, "the button made");
    tc.press(0, own->centre);
    t.expect_equal(tc.bits, want, "on the left: forward is RIGHT");
    tc.release(0);
    left = true;
    tc.press(0, own->centre);
    t.expect_equal(tc.bits, gd::Int(PadState::DOWN | PadState::LEFT | PadState::CROSS | PadState::CIRCLE), "on the right: LEFT");
    tc.reset();
}

// A combo: its steps pressed in turn, HOLD steps each, `gap` steps apart, played through once from
// the finger's landing (lifted or not); the wizard's steps and timing.
T3_TEST(touch_controls, own_combo_plays_its_steps) {
    Touch f("none");
    TouchControls& tc = f.tc;
    tc.visible = true;
    tc.enter_edit();
    tc.open_wizard(0);
    TouchControls::WizardRects w = tc.wizard_rects();
    auto tap = [&](gd::Int id, const gfx::Rect2& r) {
        gfx::Vec2 at = r.position + r.size * 0.5f;
        tc.pointer(PointerEvent::DOWN, finger(id, at));
        tc.pointer(PointerEvent::UP, finger(id, at));
    };
    tap(0, w.toggles[0]);       // step 1: Square (1)
    t.expect(!w.gap_text.size.x || tc.wizard->steps.size() == 1, "one step");
    tap(1, w.add_step);         // step 2
    t.expect(tc.wizard->step == 1 && !tc.wizard->complete(), "a new empty step: no OK yet");
    tap(2, w.toggles[0]);       // step 2: Square (1)
    tap(3, w.add_step);
    tap(4, w.toggles[1]);       // step 3: Triangle (2)
    for (int i = 0; i < 2; ++i) tap(5 + i, w.gap_minus);   // 8 -> 6 frames
    t.expect_equal(tc.wizard->gap, gd::Int(6), "two frames less between presses");
    t.expect_equal(TouchControls::default_label(tc.wizard->steps), std::string("1,1,2"), "named 1,1,2");
    tap(7, w.prev);
    t.expect_equal(tc.wizard->step, gd::Int(1), "< goes back a step");
    tap(8, w.ok);
    tc.finish_edit();
    auto own = control_of(tc, PadState::SQUARE | PadState::TRIANGLE);
    t.expect(own != nullptr && tc.custom[0].combo(), "the combo made");
    // source: by hand: gap 6, hold 3: frames 0-2 Square, 3-5 none, 6-8 Square, 9-11 none, 12-14
    // Triangle, then over
    std::vector<gd::Int> got;
    tc.press(0, own->centre);
    got.push_back(tc.bits);
    tc.release(0);              // lifted at once: the combo goes on
    for (int i = 1; i < 18; ++i) {
        tc.tick();
        got.push_back(tc.bits);
    }
    gd::Int S = PadState::SQUARE, T = PadState::TRIANGLE;
    std::vector<gd::Int> want{S, S, S, 0, 0, 0, S, S, S, 0, 0, 0, T, T, T, 0, 0, 0};
    t.expect(got == want, "1, 1, 2 six frames apart, three frames each");
    tc.tick();   // source: by hand: 3 steps x 6 frames = frame 18, the end
    t.expect(!tc._playing.has_value(), "then over");
    // Saved and read back with its steps and timing; build 9's single "bits" reads as one step.
    Touch g("none");
    t.expect(g.tc.parse_layout(tc.layout_json()), "read back");
    t.expect(g.tc.custom.size() == 1 && g.tc.custom[0].steps == std::vector<gd::Int>{S, S, T} && g.tc.custom[0].gap == 6, "steps and gap");
    t.expect(g.tc.parse_layout("{\"buttons\": [{\"id\": 3, \"bits\": 192, \"label\": \"x\"}], \"screens\": {}}"), "build 9's file");
    t.expect(g.tc.custom.size() == 1 && g.tc.custom[0].steps == std::vector<gd::Int>{192}, "one step");
    tc.reset();
}

// A dragged control's centre snaps to another control's centre line within SNAP units, and the
// guide line shows while the finger is down.
T3_TEST(touch_controls, drag_snaps_to_centre_lines) {
    Touch f("none");
    TouchControls& tc = f.tc;
    tc.visible = true;
    tc.enter_edit();
    gfx::Vec2 triangle = centre_of(tc, PadState::TRIANGLE);
    gfx::Vec2 cross = centre_of(tc, PadState::CROSS);
    // source: by hand: the unit is 720 * 0.085 = 61.2 px, SNAP 0.3 units = 18.36 px
    // Cross dragged 10 px right of Triangle's line (and down, away from any row): onto the line.
    gfx::Vec2 to(triangle.x + 10.0f, cross.y + 200.0f);
    tc.pointer(PointerEvent::DOWN, finger(0, cross));
    tc.pointer(PointerEvent::MOVE, finger(0, to));
    t.expect(approx(centre_of(tc, PadState::CROSS).x, triangle.x), "10 px off Triangle's line: on it");
    t.expect(approx(tc._guide_x, triangle.x), "the vertical guide at Triangle's x");
    // 30 px off (beyond 18.36): where the finger is.
    tc.pointer(PointerEvent::MOVE, finger(0, gfx::Vec2(triangle.x + 30.0f, cross.y + 200.0f)));
    t.expect(approx(centre_of(tc, PadState::CROSS).x, triangle.x + 30.0f), "30 px off: not snapped");
    tc.pointer(PointerEvent::UP, finger(0, gfx::Vec2(triangle.x + 30.0f, cross.y + 200.0f)));
    t.expect(std::isnan(tc._guide_x) && std::isnan(tc._guide_y), "no guide once the finger is up");
}

// Touch directions' D-pad: a finger in the zone points from the cross's centre (the stick's rest),
// not from where it lands as the stick does.
T3_TEST(touch_controls, dpad_points_from_its_centre) {
    Touch f("none");
    TouchControls& tc = f.tc;
    gfx::Vec2 hint = tc._stick_hint;
    float unit = tc._unit;
    tc.dpad = true;
    // source: by hand: a finger one unit right of the centre is past the dead zone (0.35 units)
    tc.press(0, hint + gfx::Vec2(unit, 0));
    t.expect_equal(tc.bits, gd::Int(PadState::RIGHT), "right of the centre: right at once");
    tc.release(0);
    tc.dpad = false;
    tc.press(0, hint + gfx::Vec2(unit, 0));
    t.expect_equal(tc.bits, gd::Int(0), "the stick: centred where it lands, nothing yet");
    tc.reset();
}

// Touch vibration: a buzz as a finger lands on a button or slides onto another, none for the stick.
T3_TEST(touch_controls, buzz_on_buttons_only) {
    Touch f("none");
    TouchControls& tc = f.tc;
    int buzzes = 0;
    tc.on_press = [&] { buzzes += 1; };
    tc.press(0, centre_of(tc, PadState::CROSS));
    t.expect_equal(buzzes, 1, "a finger on Cross");
    tc.move(0, centre_of(tc, PadState::CIRCLE));
    t.expect_equal(buzzes, 2, "slid onto Circle");
    tc.move(0, centre_of(tc, PadState::CIRCLE) + gfx::Vec2(2, 0));
    t.expect_equal(buzzes, 2, "moving on Circle: none");
    tc.press(1, gfx::Vec2(200, 500));
    t.expect(tc._fingers.at(1).stick && buzzes == 2, "the stick: none");
    tc.reset();
}

// A damaged file leaves the remake's layout.
T3_TEST(touch_controls, damaged_layout_file_is_ignored) {
    Touch f("throws");
    gfx::Vec2 cross = centre_of(f.tc,PadState::CROSS);
    t.expect(!f.tc.parse_layout("{\"controls\": [1, 2"), "not JSON");
    t.expect(f.tc.parse_layout("{\"controls\": {\"Cross\": {\"x\": \"a\"}, \"Start\": 3}}"), "JSON with bad records");
    f.tc.layout();
    t.expect(f.tc.placements.is_empty(), "the bad records are skipped");
    t.expect(same(centre_of(f.tc,PadState::CROSS), cross), "Cross in the remake's place");
}

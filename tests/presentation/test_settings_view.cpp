// remake/tests/presentation/test_settings_view.gd: the settings screen's layout (SettingsView): two
// tabs (General: the game and sound settings, the display ones beside them; Controls: vibration,
// the touch controls and the keyboard layouts' key fields beside their pad pictures); two columns
// on landscape windows, one on narrow windows; every row and field inside the panel and the panel
// inside the window, none overlapping, on both tabs; the settings with a single choice not shown; a
// tap or a drag on a volume's bar sets it there; a key field takes the next key, Escape keeps its key.
//
// Native: the autoloads `Settings` and `Pads` are a GameSettings and an InputRouter of each test's
// own; `_gui_input` is `pointer` (a shown view: it takes no events hidden), `_input` is `key`. At
// the end, the native additions: the touch layout edit's row and the settings button.
#include <string>

#include "gd/json.h"
#include "platform/input_router.h"
#include "platform/keys.h"
#include "platform/pad_state.h"
#include "platform/settings.h"
#include "presentation/screens/settings_button.h"
#include "presentation/screens/settings_view.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

bool same(const gfx::Rect2& a, const gfx::Rect2& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.size.x == b.size.x && a.size.y == b.size.y;
}
bool encloses(const gfx::Rect2& a, const gfx::Rect2& b) {
    return b.position.x >= a.position.x && b.position.y >= a.position.y && b.end().x <= a.end().x && b.end().y <= a.end().y;
}
gfx::Rect2 grow(const gfx::Rect2& r, float by) {
    return gfx::Rect2(r.position - gfx::Vec2(by, by), r.size + gfx::Vec2(2 * by, 2 * by));
}
bool intersects(const gfx::Rect2& a, const gfx::Rect2& b) {
    return !(a.position.x >= b.end().x || a.end().x <= b.position.x || a.position.y >= b.end().y || a.end().y <= b.position.y);
}
bool has_point(const gfx::Rect2& r, gfx::Vec2 p) {
    return p.x >= r.position.x && p.y >= r.position.y && p.x < r.end().x && p.y < r.end().y;
}
gfx::Vec2 center(const gfx::Rect2& r) { return r.position + r.size * 0.5f; }
std::string show(gfx::Vec2 v) { return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")"; }

// The autoloads of a test and a view on them.
struct Fixture {
    GameSettings Settings;
    InputRouter Pads;
    SettingsView view;
    Fixture() {
        Settings.Pads = &Pads;
        Settings.values = GameSettings::sanitized(gd::Json::parse("{}"));
        view.Settings = &Settings;
        view.Pads = &Pads;
    }
    // _view(window, tab)
    void at(gfx::Vec2 window, gd::Int tab) {
        view.size = window;
        view.tab = tab;
        view._layout();
    }
};

bool same_values(GameSettings& a, const gd::Dict<std::string, GameSettings::Value>& saved) {
    if (a.values.size() != saved.size()) return false;
    for (const auto& [key, v] : saved) {
        if (!a.values.has(key) || !GameSettings::_equal(a.values.at(key), v)) return false;
    }
    return true;
}

// Native: `touch` also shows the layout edit's row (the touch controls on).
void check(t3test::Context& t, gfx::Vec2 window, gd::Int columns, bool touch = false) {
    for (gd::Int tab = 0; tab < SettingsView::TABS.size(); ++tab) {
        Fixture f;
        if (touch) f.Settings.set_value("touch_controls", std::string("on"));
        f.at(window, tab);
        SettingsView& view = f.view;
        std::string at = show(window) + " on tab " + gd::str(tab);
        t.expect_equal(view.columns(), columns, gd::str(columns) + " column(s) at " + at);
        t.expect(encloses(gfx::Rect2(gfx::Vec2(0, 0), window), view._panel), "the panel inside the window at " + at);
        std::vector<gfx::Rect2> placed;
        for (gd::Int i = 0; i < view._rects.size(); ++i) {
            gfx::Rect2 r = view._rects[i];
            bool there = view.shown(i) && (SettingsView::tab_of(i) == tab || SettingsView::tab_of(i) < 0);
            if (!there) {
                t.expect(same(r, gfx::Rect2()), gd::str(i) + " not on this tab: no place at " + at);
                continue;
            }
            t.expect(encloses(view._panel, r), gd::str(i) + " inside the panel at " + at);
            placed.push_back(r);
        }
        for (gd::Int k = 0; k < view._pictures.size(); ++k) placed.push_back(view._pictures[k]);
        for (size_t a = 0; a < placed.size(); ++a) {
            for (size_t b = a + 1; b < placed.size(); ++b) {
                t.expect(!intersects(grow(placed[a], -1.0f), grow(placed[b], -1.0f)),
                         "rects " + std::to_string(a) + " and " + std::to_string(b) + " apart at " + at);
            }
        }
        if (tab == SettingsView::GAME && columns == 2) {
            gfx::Rect2 display = view._rects[SettingsView::column_break[SettingsView::GAME]];
            t.expect(display.position.x > view._rects[0].end().x - 1.0f, "Display heads the right column");
            t.expect_equal(display.position.y, view._rects[0].position.y, "both columns start on one line");
            gfx::Rect2 sound = view._rects[SettingsView::ROWS.find(SettingsView::Row{"SETTINGS_SOUND", ""})];
            t.expect(sound.position.x == view._rects[0].position.x && sound.position.y > view._rects[0].position.y, "Sound under Game");
        }
        if (tab == SettingsView::CONTROLS) {
            t.expect_equal(view._pictures.size(), gd::Int(2), "two pad pictures at " + at);
            gd::Int fields = 0;
            for (gd::Int i = 0; i < view._rects.size(); ++i) {
                if (SettingsView::is_field(i) && view._rects[i].has_area()) fields += 1;
            }
            t.expect_equal(fields, gd::Int(28), "14 key fields per pad at " + at);
            for (gd::Int layout = 0; layout < 2; ++layout) {
                t.expect(has_point(view._pictures[layout], view.button_spot(layout, PadState::CROSS)), "Cross on its picture");
            }
        } else {
            t.expect(same(view._rects[SettingsView::find_row("vibration")], gfx::Rect2()), "Controls rows only on their tab");
        }
    }
}

}  // namespace

T3_TEST(settings_view, layout) {
    check(t, gfx::Vec2(2400, 1080), 2);
    check(t, gfx::Vec2(1280, 720), 2);
    check(t, gfx::Vec2(1024, 768), 2);
    check(t, gfx::Vec2(720, 1280), 1);
}

T3_TEST(settings_view, panel_keeps_its_size_across_tabs) {
    Fixture general, controls;
    general.at(gfx::Vec2(1280, 720), SettingsView::GAME);
    controls.at(gfx::Vec2(1280, 720), SettingsView::CONTROLS);
    t.expect(same(general.view._panel, controls.view._panel), "one panel for both tabs");
}

T3_TEST(settings_view, no_keyboard_no_key_fields) {
    Fixture f;
    SettingsView& view = f.view;
    view.keyboard = false;
    view.size = gfx::Vec2(1280, 720);
    view.tab = SettingsView::CONTROLS;
    view._layout();
    t.expect(view._pictures.is_empty(), "no pad pictures");
    t.expect(same(view._rects[SettingsView::first_field], gfx::Rect2()), "no key fields");
    t.expect(same(view._rects[SettingsView::default_keys_row], gfx::Rect2()), "no Default keys");
    t.expect(!same(view._rects[SettingsView::find_row("vibration")], gfx::Rect2()), "vibration stays");
    t.expect(!same(view._rects[SettingsView::back_row], gfx::Rect2()), "and Back");
}

T3_TEST(settings_view, single_choices_hidden) {
    Fixture f;
    SettingsView& view = f.view;
    f.Settings.texture_packs = gd::Arr<std::string>();
    gd::Int pack = SettingsView::ROWS.find(SettingsView::Row{"texture_pack", "SETTINGS_PACK_HELP"});
    t.expect(!view.shown(pack) && !view._selectable(pack), "no texture pack installed: the row is hidden");
    view.size = gfx::Vec2(1280, 720);
    view._layout();
    t.expect_equal(view.neighbour(pack - 1, gfx::Vec2(0, 1)), pack + 1, "and skipped");
    f.Settings.texture_packs = gd::Arr<std::string>{"hd"};
    t.expect(view.shown(pack), "shown with a pack");
}

T3_TEST(settings_view, tabs_and_directions) {
    Fixture f;
    gd::Dict<std::string, GameSettings::Value> saved = f.Settings.values.duplicate();   // left and right change the settings they pass
    f.at(gfx::Vec2(1280, 720), SettingsView::GAME);
    SettingsView& view = f.view;
    view.visible = true;
    view._suppress = false;
    view.handle(PadState::R1);
    t.expect_equal(view.tab, SettingsView::CONTROLS, "R1: the next tab");
    t.expect_equal(view.cursor, SettingsView::find_row("vibration"), "on its first row");
    view.handle(0);
    gd::Int l2 = SettingsView::field_of(0, PadState::L2);
    for (gd::Int n = 0; n < 4; ++n) {
        view.handle(PadState::DOWN);
        view.handle(0);
        if (view.cursor == l2) break;
    }
    t.expect(SettingsView::is_field(view.cursor), "down reaches the key fields");
    gd::Int from = view.cursor;
    view.handle(PadState::RIGHT);
    view.handle(0);
    t.expect(view.cursor != from && SettingsView::is_field(view.cursor), "right moves between fields");
    view.handle(PadState::L1);
    t.expect_equal(view.tab, SettingsView::GAME, "L1: back");
    t.expect(same_values(f.Settings, saved), "no setting changed on the way");
}

T3_TEST(settings_view, key_field_takes_the_next_key) {
    Fixture f;
    f.Settings.reset_key_bindings();
    f.at(gfx::Vec2(1280, 720), SettingsView::CONTROLS);
    SettingsView& view = f.view;
    view.visible = true;
    view._suppress = false;
    gd::Int field = SettingsView::field_of(1, PadState::TRIANGLE);
    view.cursor = field;
    view.handle(PadState::CROSS);
    t.expect_equal(view.listening, field, "Cross: waiting for a key");
    view.key(Keys::ESCAPE, true);
    t.expect_equal(view.listening, gd::Int(-1), "Escape: no longer waiting");
    t.expect_equal(f.Settings.bound_key(1, PadState::TRIANGLE), gd::Int('I'), "and the key kept");
    view.key(Keys::ESCAPE, false);
    view.listening = field;
    view.key('N', true);
    t.expect_equal(f.Settings.bound_key(1, PadState::TRIANGLE), gd::Int('N'), "the next key bound");
    t.expect_equal(view.listening, gd::Int(-1), "done");
    view.handle(PadState::CROSS);
    t.expect_equal(view.listening, gd::Int(-1), "buttons held from the binding wait for their release");
}

T3_TEST(settings_view, volume_by_touch) {
    Fixture f;
    f.at(gfx::Vec2(2400, 1080), SettingsView::GAME);
    SettingsView& view = f.view;
    view.visible = true;
    gd::Int row = SettingsView::find_row("volume_music");
    gfx::Rect2 bar = view._bar_rect(row);
    auto finger = [](gfx::Vec2 at) { return Pointer{0, at.x, at.y}; };
    view.pointer(PointerEvent::DOWN, finger(gfx::Vec2(bar.position.x + bar.size.x * 0.3f, center(bar).y)));
    t.expect_equal(f.Settings.number("volume_music"), gd::Int(3), "a tap at 30 % of the bar");
    view.pointer(PointerEvent::MOVE, finger(gfx::Vec2(bar.position.x - 50.0f, center(bar).y + 200.0f)));
    t.expect_equal(f.Settings.number("volume_music"), gd::Int(0), "a drag past its start");
    view.pointer(PointerEvent::UP, finger(gfx::Vec2(bar.position.x - 50.0f, center(bar).y + 200.0f)));
    view.pointer(PointerEvent::MOVE, finger(bar.end()));
    t.expect_equal(f.Settings.number("volume_music"), gd::Int(0), "a drag after the finger lifted changes nothing");
    view.pointer(PointerEvent::DOWN, finger(view._rects[row].position + gfx::Vec2(10, 10)));
    t.expect_equal(f.Settings.number("volume_music"), gd::Int(0), "a tap on the label only picks the row");
    t.expect_equal(view.cursor, row, "picked");
}

// Native: "Edit layout" stands in the touch controls' section while they are on; picking it closes
// the screen and asks for the layout edit.
T3_TEST(settings_view, touch_layout_row) {
    Fixture f;
    SettingsView& view = f.view;
    gd::Int row = SettingsView::touch_layout_row;
    t.expect_equal(SettingsView::tab_of(row), SettingsView::CONTROLS, "on the Controls tab");
    f.Settings.set_value("touch_controls", std::string("off"));
    t.expect(!view.shown(row), "hidden with the touch controls off");
    f.Settings.set_value("touch_controls", std::string("on"));
    t.expect(view.shown(row), "shown with them on");
    for (gfx::Vec2 window : {gfx::Vec2(2400, 1080), gfx::Vec2(1280, 720), gfx::Vec2(1024, 768)}) check(t, window, 2, true);
    check(t, gfx::Vec2(720, 1280), 1, true);
    f.at(gfx::Vec2(1280, 720), SettingsView::CONTROLS);
    gfx::Rect2 r = view._rects[row];
    gfx::Rect2 size_row = view._rects[SettingsView::find_row("touch_size")];
    t.expect(r.has_area() && encloses(view._panel, r), "placed in the panel");
    t.expect(r.position.x == size_row.position.x && r.position.y > size_row.position.y, "under Touch button size");
    bool closed = false, edit = false;
    view.closed.push_back([&] { closed = true; });
    view.edit_layout.push_back([&] { edit = closed; });
    f.Settings._dirty = false;   // close() saves: nothing to write
    view.open();
    view.pointer(PointerEvent::DOWN, Pointer{0, center(r).x, center(r).y});
    t.expect(!view.visible && closed, "picking it closes the screen");
    t.expect(edit, "then asks for the layout edit");
}

// Native: the settings button in the window's corner and in the touch controls' free place; a tap
// on it presses it.
T3_TEST(settings_view, settings_button) {
    SettingsButton button;
    button.size = gfx::Vec2(1280, 720);
    gfx::Rect2 corner = button.area();
    t.expect(corner.position.x > 0 && corner.position.y > 0 && corner.size.x > corner.size.y, "a band at the top left");
    gd::Int presses = 0;
    button.pressed.push_back([&] { presses += 1; });
    t.expect(!button.pointer(PointerEvent::DOWN, Pointer{0, center(corner).x, center(corner).y}), "hidden: no tap");
    button.visible = true;
    t.expect(button.pointer(PointerEvent::DOWN, Pointer{0, center(corner).x, center(corner).y}), "a tap on it");
    t.expect(!button.pointer(PointerEvent::DOWN, Pointer{0, 640, 600}), "none far from it");
    t.expect_equal(presses, gd::Int(1), "pressed once");
    button.place(gfx::Rect2(300, 20, 0, 70));
    t.expect(button.area().position.x == 300 && button.area().size.y == 70, "in the free place with its height");
}

// Native SYSTEM OPTION (app/system_options.h): OPTION MODE's item in DISPLAY ADJUST's place, its
// entries and lists, a setting changed with left/right, Select and EXIT back out; off (a trace
// replay) the item stays hidden as in the remake.
#include "app/system_options.h"
#include "content/fight_content.h"
#include "content/flow_data.h"
#include "core/fight/rule_set.h"
#include "core/game/game_flow.h"
#include "core/game/options_screen.h"
#include "platform/input_router.h"
#include "platform/pad_state.h"
#include "platform/save_store.h"
#include "platform/settings.h"
#include "presentation/screens/settings_view.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

struct Fixture {
    InputRouter Pads;
    GameSettings Settings;
    SettingsView view;
    SystemOptions system;
    gd::Ref<GameFlow> flow;
    gd::Int left = 0;
    explicit Fixture(bool on) {
        Settings.Pads = &Pads;
        Settings.values = GameSettings::sanitized(gd::Json::parse("{}"));
        Settings.store->settings_path = "user://no_such_folder/settings.json";   // never written
        view.Settings = &Settings;
        system.Settings = &Settings;
        system.view = &view;
        system.closed = [this] { left += 1; };
        auto content = FightContent::load_from(t3test::paths().imported);
        auto data = FlowData::load_from(t3test::imported("tables/flow.json"));
        flow = gd::make<GameFlow>(content, data, RuleSet::original(), 1);
        flow->options->system_option = on;
        flow->options->system_step = [this] { return system.step(*flow, *flow->options); };
        flow->state = GameFlow::State::OPTIONS;
        flow->sub = 0;
        idle(2);
    }
    void idle(int frames) {
        for (int i = 0; i < frames; ++i) flow->step(gd::PI32{0, 0});
    }
    // A button pressed for one step and let go.
    void press(gd::Int bits) {
        flow->step(gd::PI32{bits, 0});
        idle(1);
    }
};

}  // namespace

T3_TEST(system_options, in_option_mode_with_its_lists) {
    if (!t.require(t3test::imported("tables/flow.json"))) return;
    Fixture f(true);
    OptionsScreen& o = *f.flow->options;
    // source: flow.json's OPTION MODE: GAME OPTION, KEY CONFIGURATION, RECORDS, DISPLAY ADJUST (4th)
    for (int i = 0; i < 3; ++i) f.press(PadState::DOWN);
    t.expect_equal(o.cursors[0], gd::Int(3), "the 4th item is reached");
    t.expect_equal(o.item_label(0, 3), std::string("SYSTEM OPTION"), "shown as SYSTEM OPTION");
    f.press(PadState::CROSS);
    t.expect_equal(o.page, OptionsScreen::PAGE_DISPLAY_ADJUST, "Cross opens its page");
    t.expect_equal(o.system_title, std::string("SYSTEM OPTION"), "its title");
    std::vector<std::string> labels;
    for (const auto& r : o.system_rows) labels.push_back(r.label);
    t.expect(labels == std::vector<std::string>{"GRAPHICS", "SOUND", "GAME", "CONTROLS", "EXIT"}, "its entries");
    // SOUND: MUSIC one step down with Left, shown as not the default.
    f.press(PadState::DOWN);
    f.press(PadState::CROSS);
    t.expect(o.system_title == "SOUND" && o.system_values, "SOUND's list");
    t.expect(o.system_rows[0].label == "MUSIC" && o.system_rows[0].value == "10" && o.system_rows[0].is_default, "MUSIC 10, the default");
    f.press(PadState::LEFT);
    t.expect_equal(f.Settings.number("volume_music"), gd::Int(9), "Left turns the music down");
    t.expect(o.system_rows[0].value == "9" && !o.system_rows[0].is_default, "shown as 9, not the default");
    // Select back to the entries, EXIT back to OPTION MODE (the settings saved, `closed` called).
    f.press(PadState::SELECT);
    t.expect_equal(o.system_title, std::string("SYSTEM OPTION"), "Select: back to the entries");
    t.expect_equal(o.system_cursor, gd::Int(1), "on SOUND still");
    for (int i = 0; i < 3; ++i) f.press(PadState::DOWN);
    f.press(PadState::CROSS);
    t.expect(o.page == 0 && f.left == 1, "EXIT: back in OPTION MODE");
    t.expect_equal(o.cursors[0], gd::Int(3), "on SYSTEM OPTION");
    // Opened again: from its first entry.
    f.press(PadState::CROSS);
    t.expect(o.system_title == "SYSTEM OPTION" && o.system_cursor == 0, "reopened at GRAPHICS");
}

// Without a pad or keyboard, TOUCH CONTROLS never turns OFF (no way back without touch).
T3_TEST(system_options, touch_off_needs_another_input) {
    if (!t.require(t3test::imported("tables/flow.json"))) return;
    Fixture f(true);
    OptionsScreen& o = *f.flow->options;
    for (int i = 0; i < 3; ++i) f.press(PadState::DOWN);
    f.press(PadState::CROSS);        // SYSTEM OPTION
    for (int i = 0; i < 3; ++i) f.press(PadState::DOWN);
    f.press(PadState::CROSS);        // CONTROLS
    t.expect(o.system_title == "CONTROLS" && o.system_rows[0].label == "TOUCH CONTROLS", "on TOUCH CONTROLS");
    f.system.other_input = false;
    // source: settings.h's choices: auto, on, off
    f.press(PadState::RIGHT);
    t.expect_equal(f.Settings.text("touch_controls"), std::string("on"), "AUTO -> ON");
    f.press(PadState::RIGHT);
    t.expect_equal(f.Settings.text("touch_controls"), std::string("auto"), "ON -> AUTO: OFF passed");
    f.press(PadState::LEFT);
    t.expect_equal(f.Settings.text("touch_controls"), std::string("on"), "back: ON, OFF passed");
    f.system.other_input = true;
    f.press(PadState::RIGHT);
    t.expect_equal(f.Settings.text("touch_controls"), std::string("off"), "with a pad: OFF");
}

// The memory card as the phone's save data: renamed, no slot to choose.
T3_TEST(system_options, save_data_in_place_of_the_card) {
    if (!t.require(t3test::imported("tables/flow.json"))) return;
    Fixture f(true);
    OptionsScreen& o = *f.flow->options;
    // source: flow.json's OPTION MODE: MEMORY CARD is the 5th item; its page's CARD SAVE, CARD LOAD
    t.expect_equal(o.item_label(0, 4), std::string("SAVE DATA"), "MEMORY CARD shown as SAVE DATA");
    t.expect_equal(o.item_label(3, 0), std::string("SAVE"), "CARD SAVE as SAVE");
    t.expect_equal(o.item_label(3, 1), std::string("LOAD"), "CARD LOAD as LOAD");
    t.expect_equal(o.item_label(3, 2), std::string("AUTO SAVE"), "AUTO SAVE kept");
    t.expect_equal(o.shown("NO TEKKEN3 FILE!"), std::string("NO SAVE DATA!"), "the missing file's message");
    std::vector<std::string> keys;
    for (const auto& e : f.system.entries(SystemOptions::GAME)) keys.push_back(e.key);
    // source: by hand: GAME lists ORIGINAL BUGS, GAME TEXTS, EXIT (no MEMORY CARD slot)
    t.expect(keys == std::vector<std::string>{"gameplay_fixes", "game_texts", ""}, "GAME has no card slot");
    Fixture off(false);
    t.expect_equal(off.flow->options->item_label(0, 4), std::string("MEMORY CARD"), "off: the original's name");
}

T3_TEST(system_options, off_keeps_the_remakes_menu) {
    if (!t.require(t3test::imported("tables/flow.json"))) return;
    Fixture f(false);
    OptionsScreen& o = *f.flow->options;
    for (int i = 0; i < 3; ++i) f.press(PadState::DOWN);
    // source: the remake's options_screen.gd: DISPLAY ADJUST (index 3) is passed over
    t.expect_equal(o.cursors[0], gd::Int(4), "the cursor passes over the hidden item");
}

T3_TEST(system_options, capitals_and_values_fit) {
    // source: by hand: font 0 draws 0x20-0x5F only, Polish letters become "?"
    t.expect_equal(SystemOptions::capitals("Smooth + curved"), std::string("SMOOTH + CURVED"), "capitals");
    // source: by hand: Ł, ó and ź are not in font 0, d is
    t.expect_equal(SystemOptions::capitals("Łódź pack"), std::string("??D? PACK"), "letters font 0 lacks");
    GameSettings settings;
    settings.values = GameSettings::sanitized(gd::Json::parse("{}"));
    SettingsView view;
    view.Settings = &settings;
    SystemOptions s;
    s.Settings = &settings;
    s.view = &view;
    // source: by hand (system_options.cpp's LABELS, FLAG_VALUES): Gameplay fixes says what it changes
    s.menu = SystemOptions::GAME;
    t.expect_equal(s.entries(SystemOptions::GAME)[0].label, std::string("ORIGINAL BUGS"), "Gameplay fixes' label");
    t.expect_equal(s.value("gameplay_fixes", "ORIGINAL BUGS"), std::string("KEPT"), "off: the bugs kept");
    settings.values.set("gameplay_fixes", true);
    t.expect_equal(s.value("gameplay_fixes", "ORIGINAL BUGS"), std::string("FIXED"), "on: fixed");
    // source: by hand: (324 - 24 * 9) / 9 = 12 characters beside a 23-character label
    t.expect_equal(s.value("game_texts", std::string(23, 'X')), std::string("ENGLISH (USA"), "cut to the value column");
}

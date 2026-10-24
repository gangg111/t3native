// remake/tests/platform/test_settings.gd: the remake's settings (GameSettings): a saved file is
// sanitized, unknown keys dropped and bad values replaced by defaults; a Japanese interface stays
// only where it can be drawn, the Japanese game texts only when the game was converted from a
// Japanese disc.
//
// Native: the autoload `Settings` is a GameSettings each test makes (with `Pads` and the asset
// manifest as hooks). Not ported: test_session_texture_pack_ends_with_any_choice (TexturePacks has
// no session pack yet), test_fullscreen_hotkeys_by_system and test_alt_enter_is_not_a_button
// (InputRouter has no fullscreen hotkeys yet).
#include <filesystem>
#include <string>

#include "gd/io.h"
#include "gd/json_writer.h"
#include "platform/input_router.h"
#include "platform/keys.h"
#include "platform/pad_state.h"
#include "platform/save_store.h"
#include "platform/settings.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

using Value = GameSettings::Value;
using Bindings = GameSettings::Bindings;

void use_test_user_dir() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "t3tests_user";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    SaveStore::user_dir = dir.string();
}

gd::Json json(const std::string& text) { return gd::Json::parse(text); }

// A Dictionary of the test as the JSON file would hold it.
gd::Json as_json(const Bindings& b) { return gd::Json::parse(gd::json_stringify(GameSettings::_to_json(Value(b)))); }

Bindings duplicate_deep(const Bindings& b) {
    Bindings out;
    for (const auto& [layout, keys] : b) out.set(layout, keys.duplicate());
    return out;
}

std::string at(const Bindings& b, const std::string& layout, const std::string& button) { return b.at(layout).at(button); }

bool same(const Value& a, const Value& b) { return GameSettings::_equal(a, b); }

}  // namespace

T3_TEST(settings, sanitized) {
    gd::Dict<std::string, Value> out = GameSettings::sanitized(json(
        "{\"volume_music\": 14.6, \"graphics\": \"ultra\", \"unknown\": 1, \"language\": \"ja\", \"touch_size\": 115.0, \"texture_pack\": \"hd\"}"));
    t.expect(same(out.at("volume_music"), GameSettings::VOLUME_STEPS), "a volume clamped");
    t.expect(same(out.at("graphics"), std::string("auto")), "an unknown choice back to its default");
    t.expect(!out.has("unknown"), "unknown keys dropped");
    t.expect(same(out.at("touch_size"), gd::Int(115)), "a size read back from JSON's float");
    t.expect(std::holds_alternative<gd::Int>(out.at("touch_size")), "as an integer");
    t.expect(same(out.at("texture_pack"), std::string("hd")), "a texture pack's name kept");
    t.expect(same(GameSettings::sanitized(json("{\"touch_size\": 120}")).at("touch_size"), gd::Int(100)), "a size not offered: the default");
    t.expect(same(out.at("language"), std::string(GameSettings::japanese_interface() ? "ja" : "auto")),
             "Japanese only where the interface can show it");
    t.expect_equal(out.size(), GameSettings::SCHEMA.size() + 1, "every key present, and the key bindings");
    t.expect(same(out.at(GameSettings::KEY_BINDINGS), GameSettings::default_bindings()), "no bindings saved: the defaults");
    // source: the user's choice (2026-10-24): DEBUG LOG off unless chosen
    t.expect(same(out.at("perf_log"), false), "the debug log off by default");
}

T3_TEST(settings, key_bindings_sanitized) {
    Bindings defaults = GameSettings::default_bindings();
    t.expect_equal(at(defaults, "keyboard_1", "up"), std::string("W"), "the left layout's Up");
    t.expect_equal(at(defaults, "keyboard_2", "r2"), std::string("Semicolon"), "the right layout's R2");
    Bindings saved = duplicate_deep(defaults);
    saved.at("keyboard_1").set("up", "H");
    saved.at("keyboard_1").set("down", "Escape");
    saved.at("keyboard_1").set("left", "Nonsense");
    saved.at("keyboard_2").set("up", "H");
    Bindings out = GameSettings::sanitized_bindings(as_json(saved));
    t.expect_equal(at(out, "keyboard_1", "up"), std::string("H"), "a free key kept");
    t.expect_equal(at(out, "keyboard_1", "down"), std::string("S"), "Escape is reserved: the default");
    t.expect_equal(at(out, "keyboard_1", "left"), std::string("A"), "an unknown key: the default");
    t.expect_equal(at(out, "keyboard_2", "up"), std::string("Up"), "a key taken already: the default");
    saved.at("keyboard_2").set("up", "W");
    saved.at("keyboard_1").set("up", "W");
    saved.at("keyboard_1").set("down", "Up");
    out = GameSettings::sanitized_bindings(as_json(saved));
    t.expect_equal(at(out, "keyboard_1", "down"), std::string("Up"), "a free key kept, another layout's default too");
    t.expect_equal(at(out, "keyboard_2", "up"), std::string("S"), "its default taken: the default key left over");
    t.expect_equal(at(out, "keyboard_1", "up"), std::string("W"), "the other bindings stay");
    // A key reserved since it was bound (F3), its button's default taken by another button.
    saved = duplicate_deep(defaults);
    saved.at("keyboard_1").set("select", "F3");
    saved.at("keyboard_1").set("start", "Q");
    saved.at("keyboard_1").set("cross", "H");
    out = GameSettings::sanitized_bindings(as_json(saved));
    t.expect_equal(at(out, "keyboard_1", "start"), std::string("Q"), "Start keeps Q");
    t.expect_equal(at(out, "keyboard_1", "select"), std::string("Space"), "Select takes the key Start left");
    t.expect_equal(at(out, "keyboard_1", "cross"), std::string("H"), "custom bindings are not reset");
    t.expect(same(GameSettings::sanitized_bindings(json("\"junk\"")), defaults), "not a dictionary");
}

T3_TEST(settings, bind_key_swaps) {
    InputRouter pads;
    GameSettings settings;
    settings.Pads = &pads;
    settings.values = GameSettings::sanitized(json("{}"));
    settings.reset_key_bindings();
    settings.bind_key(0, PadState::UP, Keys::letter('S'));
    t.expect_equal(settings.bound_key(0, PadState::UP), Keys::letter('S'), "bound");
    t.expect_equal(settings.bound_key(0, PadState::DOWN), Keys::letter('W'), "Down had S: it takes Up's old W");
    settings.bind_key(0, PadState::CROSS, Keys::letter('J'));
    t.expect_equal(settings.bound_key(0, PadState::CROSS), Keys::letter('J'), "a key of the other layout");
    t.expect_equal(settings.bound_key(1, PadState::CROSS), Keys::letter('F'), "swapped across the layouts");
    t.expect_equal(pads.layouts[0].at(Keys::letter('J')), gd::Int(PadState::CROSS), "the router follows");
    settings.bind_key(0, PadState::CROSS, Keys::ESCAPE);
    t.expect_equal(settings.bound_key(0, PadState::CROSS), Keys::letter('J'), "Escape is not taken");
    settings.reset_key_bindings();
    t.expect(same(settings.values.at(GameSettings::KEY_BINDINGS), GameSettings::default_bindings()), "back to the defaults");
}

T3_TEST(settings, a_failed_save_is_retried) {
    use_test_user_dir();
    std::string path = "user://test_settings_retry.json";
    GameSettings settings;
    settings.values = GameSettings::sanitized(json("{}"));
    gd::Int saved = settings.number("volume_music");
    settings.store->settings_path = "user://no_such_folder/settings.json";
    settings.set_value("volume_music", (saved + 1) % GameSettings::VOLUME_STEPS);
    settings.save();
    t.expect(settings._dirty, "a failed write leaves the settings unsaved");
    settings.store->settings_path = path;
    settings.save();
    t.expect(!settings._dirty, "saved once the folder is writable");
    t.expect(gd::file_exists(SaveStore::globalize_path(path)), "the retry wrote the file");
    // Native: what was written reads back as the same settings.
    GameSettings again;
    again.store->settings_path = path;
    again.load_saved();
    t.expect_equal(again.number("volume_music"), (saved + 1) % GameSettings::VOLUME_STEPS, "read back");
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(SaveStore::globalize_path(path)), ec);
}

T3_TEST(settings, game_texts_follow_the_source) {
    GameSettings settings;
    settings.values = GameSettings::sanitized(json("{}"));
    std::string source = "usa";
    settings.Assets_japanese_texts = [&source] { return source != "usa"; };
    settings.values.set("game_texts", std::string("japanese"));
    t.expect(settings.choices("game_texts") == gd::Arr<Value>{std::string("english")}, "English only");
    t.expect_equal(settings.text("game_texts"), std::string("english"), "read as English");
    settings.cycle("game_texts", 1);
    t.expect(same(settings.values.at("game_texts"), std::string("japanese")), "the saved choice stays");
    source = "jp_orig";
    t.expect(settings.choices("game_texts") == gd::Arr<Value>{std::string("english"), std::string("japanese")}, "both");
    t.expect_equal(settings.text("game_texts"), std::string("japanese"), "the saved choice again");
}

T3_TEST(settings, session_preset) {
    GameSettings settings;
    settings.values = GameSettings::sanitized(json("{}"));
    gd::Arr<std::string> offered = GameSettings::RENDERER_PRESETS.get(GameSettings::renderer(), gd::Arr<std::string>{"desktop_low"});
    Value saved = settings.values.at("graphics");
    std::string chosen = offered[offered.size() - 1];
    settings.session.set("graphics", chosen);
    t.expect_equal(settings.preset(), chosen, "--graphics= wins over the setting");
    t.expect_equal(settings.text("graphics"), chosen, "and the settings screen shows it");
    // Native: a choice other than the session's (the GDScript's `"auto" if saved != "auto" else
    // chosen` picks the session's own preset when the saved one is "auto", which ends nothing).
    settings.set_value("graphics", !same(saved, std::string("auto")) ? saved : Value(std::string("auto")));
    t.expect(!settings.session.has("graphics"), "choosing a preset in the settings ends the session's");
}

T3_TEST(settings, session_shading_ends_with_any_choice) {
    GameSettings settings;
    settings.values = GameSettings::sanitized(json("{}"));
    settings.values.set("shading", std::string("smooth"));
    settings.session.set("shading", std::string("original"));
    t.expect_equal(settings.text("shading"), std::string("original"), "--shading= wins over the setting");
    settings.set_value("shading", std::string("original"));
    t.expect(settings.session.has("shading"), "choosing the session's own value changes nothing");
    settings.set_value("shading", std::string("smooth"));
    t.expect(!settings.session.has("shading"), "choosing the saved value still ends the session's");
    t.expect_equal(settings.text("shading"), std::string("smooth"), "the setting again");
}

T3_TEST(settings, fullscreen_setting_is_sanitized) {
    t.expect(same(GameSettings::sanitized(json("{}")).at("fullscreen"), false), "windowed by default");
    t.expect(same(GameSettings::sanitized(json("{\"fullscreen\": \"yes\"}")).at("fullscreen"), false), "a bad value: the default");
    t.expect(same(GameSettings::sanitized(json("{\"fullscreen\": true}")).at("fullscreen"), true), "a saved fullscreen kept");
    t.expect(InputRouter::reserved(Keys::F11), "F11 is no layout's key");
}

// The toggle reads the window's mode (here a window that stays windowed): from windowed it turns the
// setting on, and a setting already on while the window is not fullscreen stays on (the window is
// made fullscreen to match, not the setting turned off).
T3_TEST(settings, toggle_fullscreen_follows_the_window) {
    GameSettings settings;
    settings.values = GameSettings::sanitized(json("{}"));
    gd::Int applied = 0;
    settings.DisplayServer_window_fullscreen = [] { return false; };
    settings.DisplayServer_set_fullscreen = [&applied](bool) { applied += 1; };
    if (!settings.fullscreen_offered()) return;
    settings.values.set("fullscreen", false);
    settings.toggle_fullscreen();
    t.expect_equal(settings.flag("fullscreen"), true, "windowed window, setting off -> on");
    settings.toggle_fullscreen();
    t.expect_equal(settings.flag("fullscreen"), true, "windowed window, setting already on: stays on");
    t.expect_equal(applied, gd::Int(2), "the window was asked both times");
}

// Native: the settings file is Godot's JSON.stringify(values, "\t"): keys sorted, ints as ints.
T3_TEST(settings, file_is_written_as_godot_writes_it) {
    GameSettings settings;
    settings.values = GameSettings::sanitized(json("{}"));
    std::string text = gd::json_stringify(GameSettings::_to_json(settings.values), "\t");
    t.expect(text.rfind("{\n\t\"framing\": \"stage\",\n\t\"fullscreen\": false,\n\t\"game_texts\": \"english\",\n\t\"gameplay_fixes\": false,\n", 0) == 0,
             "sorted keys, tab indent: " + text.substr(0, 90));
    t.expect(text.find("\n\t\"key_bindings\": {\n\t\t\"keyboard_1\": {\n\t\t\t\"circle\": \"G\",") != std::string::npos,
             "nested dictionaries sorted too");
    t.expect(text.find("\"volume_music\": 10,") != std::string::npos, "an integer");
}

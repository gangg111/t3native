#pragma once
// remake/platform/settings.gd
// The remake's own settings (autoload `Settings`, remake-plan.md#save-data-and-settings): the
// Gameplay fixes, the game texts' language (English: the USA release's), the graphics preset and its options, the texture pack, audio
// volumes, vibration, touch controls, the keyboard layouts, the interface language and the memory
// card in use (its progress is ProgressCard's). They are kept in
// `user://settings.json` apart from the game's progress. The game's own options (difficulty,
// rounds, the key configuration, ...) stay on its OPTIONS screen and in the progress block.
//
// Each setting is one key of SCHEMA with its choices; `choices(key)` narrows them to what this
// build and platform offer (the presets by the renderer, the texture packs found).
//
// Native: an object GameApp owns (`Settings`) and passes to whoever needs it. The signal `changed`
// is a list of std::function. A setting's Variant is `Value` (bool, int, String or the key
// bindings' Dictionary). The engine's parts are hooks the app sets (empty: not there): the audio
// buses (AudioServer), the window's fullscreen (DisplayServer), the asset manifest (Assets) and
// the input router (Pads). The fullscreen hotkeys (`is_fullscreen_hotkey`, `_input`) are not
// translated: InputRouter has no fullscreen_hotkeys / is_alt_enter yet (TODO).
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "gd/gd.h"
#include "gd/json.h"
#include "gd/json_writer.h"

namespace t3 {

class InputRouter;
class SaveStore;

class GameSettings {
public:
    // {"keyboard_1": {"up": "W", ...}, "keyboard_2": {...}}
    using Bindings = gd::Dict<std::string, gd::Dict<std::string, std::string>>;
    using Value = std::variant<bool, gd::Int, std::string, Bindings>;
    // SCHEMA's [default, choices].
    struct Entry {
        Value def;
        gd::Arr<Value> choices;
    };

    static constexpr gd::Int VOLUME_STEPS = 10;
    static constexpr gd::Int KEY_NONE = 0;
    // key -> [default, choices]; an empty choice list is an integer 0-VOLUME_STEPS, a list with
    // "" first is a name chosen at run time (the texture packs).
    static inline const gd::Dict<std::string, Entry> SCHEMA = [] {
        using S = std::string;
        gd::Dict<std::string, Entry> d;
        d.set("gameplay_fixes", Entry{false, {false, true}});
        d.set("memory_card", Entry{gd::Int(1), {gd::Int(1), gd::Int(2)}});
        d.set("game_texts", Entry{S("english"), {S("english"), S("japanese")}});
        d.set("language", Entry{S("auto"), {S("auto"), S("en"), S("ja")}});
        d.set("fullscreen", Entry{false, {false, true}});
        d.set("graphics", Entry{S("auto"), {S("auto"), S("web"), S("mobile_low"), S("mobile"), S("mobile_high"), S("desktop_low"), S("desktop_high")}});
        d.set("texture_filter", Entry{S("smooth"), {S("smooth"), S("crisp")}});
        d.set("shading", Entry{S("smooth"), {S("original"), S("smooth"), S("curved")}});
        d.set("texture_pack", Entry{S(""), {S("")}});
        d.set("interpolation", Entry{true, {true, false}});
        d.set("framing", Entry{S("stage"), {S("stage"), S("bars")}});
        // Native: the select screens' backgrounds over the whole window (ScreenCanvas::wide).
        d.set("wide_screens", Entry{true, {true, false}});
        // Native: the remake's default is the arcade's; here CONSOLE (the PlayStation's) until chosen.
        d.set("stage_backdrops", Entry{S("playstation"), {S("arcade"), S("playstation")}});
        d.set("volume_music", Entry{VOLUME_STEPS, {}});
        d.set("volume_sfx", Entry{VOLUME_STEPS, {}});
        d.set("volume_voices", Entry{VOLUME_STEPS, {}});
        d.set("volume_system", Entry{VOLUME_STEPS, {}});
        d.set("vibration", Entry{true, {true, false}});
        d.set("touch_controls", Entry{S("auto"), {S("auto"), S("on"), S("off")}});
        d.set("touch_size", Entry{gd::Int(100), {gd::Int(70), gd::Int(85), gd::Int(100), gd::Int(115), gd::Int(130)}});
        // Native: the touch buttons' opacity at rest (percent; 55 is the remake's) and a short buzz
        // as a finger lands on one.
        d.set("touch_opacity", Entry{gd::Int(55), {gd::Int(25), gd::Int(40), gd::Int(55), gd::Int(70), gd::Int(85), gd::Int(100)}});
        d.set("touch_haptics", Entry{true, {true, false}});
        // Native: the performance log (PerfLog) in Downloads.
        d.set("perf_log", Entry{true, {true, false}});
        // Native: the left control: a fixed cross (D-pad) or the remake's stick that centres where the
        // finger lands.
        d.set("touch_directions", Entry{S("dpad"), {S("dpad"), S("stick")}});
        return d;
    }();
    // The keys of the two keyboard layouts (InputRouter.layouts), kept apart from SCHEMA as
    // {"keyboard_1": {"up": "W", ...}, "keyboard_2": {...}}: each InputRouter.BINDABLE button and its
    // key's name (OS.get_keycode_string); no key twice, none of InputRouter.RESERVED_KEYS.
    static inline const std::string KEY_BINDINGS = "key_bindings";
    static inline const gd::Arr<std::string> LAYOUT_NAMES{"keyboard_1", "keyboard_2"};
    static inline const gd::Dict<std::string, std::string> BUS_VOLUMES = [] {
        gd::Dict<std::string, std::string> d;
        d.set("volume_music", "Music"); d.set("volume_sfx", "SFX"); d.set("volume_voices", "Voices"); d.set("volume_system", "System");
        return d;
    }();
    // The presets each renderer can draw (remake-plan.md#quality-presets).
    static inline const gd::Dict<std::string, gd::Arr<std::string>> RENDERER_PRESETS = [] {
        gd::Dict<std::string, gd::Arr<std::string>> d;
        d.set("gl_compatibility", {"web"});
        d.set("mobile", {"mobile_low", "mobile", "mobile_high"});   // native: mobile_low (Auto stays mobile)
        d.set("forward_plus", {"desktop_low", "desktop_high"});
        return d;
    }();

    GameSettings();

    // The autoload `Settings` the views read (GameApp's, set when the app starts); null outside the
    // app (tests), where the readers below give the schema's defaults.
    static inline GameSettings* autoload = nullptr;
    // Settings.text(key), or the schema's default without the autoload.
    static std::string read_text(const std::string& key);
    // Settings.preset(), or `fallback` without the autoload.
    static std::string read_preset(const std::string& fallback);

    // signal changed(key: String)
    std::vector<std::function<void(const std::string& key)>> changed;

    gd::Ref<SaveStore> store;
    gd::Dict<std::string, Value> values;
    bool usa_converted = false;              // the USA localization was converted (imported/usa)
    gd::Arr<std::string> texture_packs;
    // Choices of this session only (`--graphics=`, `--shading=`, `--texture-pack=`): key -> value, read
    // before the saved ones (and offered among the choices) and never saved; choosing anything for
    // that key in the settings ends it.
    gd::Dict<std::string, Value> session;
    bool _dirty = false;                     // changed since the last save

    // Native: the engine and autoloads the GDScript reaches (set by the app; empty: not there).
    InputRouter* Pads = nullptr;
    std::function<bool(const std::string& group)> Assets_has_group;
    std::function<bool()> Assets_japanese_texts;
    std::function<gd::Int(const std::string& bus)> AudioServer_get_bus_index;
    std::function<void(gd::Int bus, bool mute)> AudioServer_set_bus_mute;
    std::function<void(gd::Int bus, float volume_db)> AudioServer_set_bus_volume_db;
    std::function<bool()> DisplayServer_window_fullscreen;     // _window_fullscreen
    std::function<void(bool on)> DisplayServer_set_fullscreen;  // window_set_mode (FULLSCREEN / WINDOWED)
    bool touchscreen_available = false;      // DisplayServer.is_touchscreen_available()
    std::string os_locale_language = "en";   // OS.get_locale_language()

    void _ready();
    void load_saved();
    // `saved` with unknown keys dropped and every missing or invalid value replaced by its default.
    static gd::Dict<std::string, Value> sanitized(const gd::Json& saved);
    // The default keyboard layouts (InputRouter.DEFAULT_LAYOUTS) in the settings' form.
    static Bindings default_bindings();
    // Saved key bindings with every missing, unknown, reserved or repeated key replaced: by the
    // button's default when it is free, else by a free default key of another button (a saved key
    // took it, so that one's default is left over); every other binding is kept.
    static Bindings sanitized_bindings(const gd::Json& saved);
    static gd::Int _first_free(const gd::PI64& keys, const gd::Dict<gd::Int, bool>& used);
    // [layout, button] bound to a key name in `bindings`, empty if none.
    static gd::Arr<std::string> _holder(const Bindings& bindings, const std::string& name);
    // The key a saved name stands for; KEY_NONE for anything but a plain key no layout may take.
    static gd::Int key_of(const std::string& name);
    static std::string _button_name(gd::Int bit);
    // A keyboard layout (0: left, 1: right) as key -> pad bit, for InputRouter.layouts.
    std::map<gd::Int, gd::Int> key_layout(gd::Int layout);
    // The key of a button in a keyboard layout.
    gd::Int bound_key(gd::Int layout, gd::Int bit);
    // Binds a key to a button of a keyboard layout; the button that had the key (in either layout)
    // takes this button's old one.
    void bind_key(gd::Int layout, gd::Int bit, gd::Int key);
    void reset_key_bindings();
    // A setting's value; a choice this build does not offer reads as its first one (converted from
    // the USA disc the game texts are English), while the saved choice stays for another build.
    Value value(const std::string& key);
    bool flag(const std::string& key);
    std::string text(const std::string& key);
    gd::Int number(const std::string& key);
    double level(const std::string& key);
    // The choices this build offers for a key (empty: an integer 0-VOLUME_STEPS).
    gd::Arr<Value> choices(const std::string& key);
    gd::Arr<Value> _offered(const std::string& key);
    void set_value(const std::string& key, const Value& v);
    // Writes the settings when they changed since the last save (the settings screen saves as it
    // closes; leaving or pausing the application saves too). A failed write keeps them unsaved, so
    // the next save tries again.
    void save();
    static constexpr gd::Int NOTIFICATION_PREDELETE = 1;
    static constexpr gd::Int NOTIFICATION_WM_CLOSE_REQUEST = 1006;
    static constexpr gd::Int NOTIFICATION_APPLICATION_PAUSED = 2015;
    void _notification(gd::Int what);
    // The next (or with `step` -1 the previous) choice of a key, wrapping; volumes stop at their ends.
    void cycle(const std::string& key, gd::Int step);
    // Fullscreen is for the desktop builds (the others' windows are the system's).
    bool fullscreen_offered();
    // Switches the window between fullscreen and windowed, from the window's mode as it is now (it can
    // have been changed outside the setting: the engine's --fullscreen, the system).
    void toggle_fullscreen();
    // The saved fullscreen to the window at the start, by the entry points once their options are read
    // (DevOptions.parse): only a saved fullscreen is applied, so a window already made fullscreen (the
    // engine's --fullscreen) is left as it is.
    void apply_window();
    bool _window_fullscreen();
    // The window's mode to `on` (a borderless full-screen window, not the exclusive mode: it switches
    // quickly and keeps the desktop's other windows usable). Not on builds without the setting (a
    // synced settings file) and not without a window.
    void _apply_fullscreen(bool on);
    // The renderer this run uses: its method name as in the project settings.
    static std::string renderer();
    // The graphics preset in effect: the chosen one, or for "auto" the renderer's first (desktop
    // builds start on the high preset).
    std::string preset();
    // Whether the touch controls are shown: "auto" on devices with a touch screen.
    bool touch_enabled();
    // The touch controls' size as a share of the default one ("touch_size" is a percentage).
    double touch_scale();
    // The interface locale: "auto" follows the system's language (Japanese or else English);
    // English where the Japanese interface cannot be drawn.
    std::string locale();
    // Whether the interface can be shown in Japanese: the project bundles no CJK font, so it relies on
    // the system fonts' fallback, which the web builds do not have (native builds are no web build).
    static bool japanese_interface();
    void _apply_volumes();
    // The keyboard layouts to the input router (Pads).
    void _apply_key_layouts();
    // After the audio buses exist (the audio director adds them).
    void apply_audio();

    // Native: Variant helpers. str(v); `a == b` (Dictionaries by content); a value as JSON.
    static std::string _str(const Value& v);
    static bool _equal(const Value& a, const Value& b);
    static gd::JsonValue _to_json(const Value& v);
    static gd::JsonValue _to_json(const gd::Dict<std::string, Value>& values);
    // OS.get_keycode_string / OS.find_keycode_from_string over the keys the platforms send (keys.h).
    static std::string keycode_string(gd::Int key);
    static gd::Int find_keycode_from_string(const std::string& name);
};

}  // namespace t3

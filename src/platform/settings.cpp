// remake/platform/settings.gd
#include "platform/settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "audio/mixer.h"
#include "content/json_file.h"
#include "platform/input_router.h"
#include "platform/keys.h"
#include "platform/pad_state.h"
#include "platform/save_store.h"

namespace t3 {

namespace {

// InputRouter.BINDABLE in its declaration order (the C++ InputRouter keeps it in a sorted map,
// whose order the bindings' repair would not follow).
const std::pair<const char*, gd::Int> BINDABLE[] = {
    {"up", PadState::UP}, {"right", PadState::RIGHT}, {"down", PadState::DOWN}, {"left", PadState::LEFT},
    {"square", PadState::SQUARE}, {"triangle", PadState::TRIANGLE}, {"cross", PadState::CROSS}, {"circle", PadState::CIRCLE},
    {"l1", PadState::L1}, {"r1", PadState::R1}, {"l2", PadState::L2}, {"r2", PadState::R2},
    {"start", PadState::START}, {"select", PadState::SELECT}};

// InputRouter.DEFAULT_LAYOUTS in their declaration order (key -> pad bit).
const std::vector<std::pair<gd::Int, gd::Int>>& default_layout(gd::Int layout) {
    using namespace Keys;
    static const std::vector<std::pair<gd::Int, gd::Int>> layouts[2] = {
        {{letter('W'), PadState::UP}, {letter('D'), PadState::RIGHT}, {letter('S'), PadState::DOWN},
         {letter('A'), PadState::LEFT}, {letter('R'), PadState::SQUARE}, {letter('T'), PadState::TRIANGLE},
         {letter('F'), PadState::CROSS}, {letter('G'), PadState::CIRCLE}, {letter('Z'), PadState::L1},
         {letter('X'), PadState::R1}, {letter('C'), PadState::L2}, {letter('V'), PadState::R2},
         {SPACE, PadState::START}, {letter('Q'), PadState::SELECT}},
        {{UP, PadState::UP}, {RIGHT, PadState::RIGHT}, {DOWN, PadState::DOWN}, {LEFT, PadState::LEFT},
         {letter('U'), PadState::SQUARE}, {letter('I'), PadState::TRIANGLE}, {letter('J'), PadState::CROSS},
         {letter('K'), PadState::CIRCLE}, {letter('O'), PadState::L1}, {letter('P'), PadState::R1},
         {letter('L'), PadState::L2}, {SEMICOLON, PadState::R2}, {ENTER, PadState::START}, {BACKSPACE, PadState::SELECT}}};
    return layouts[layout];
}

gd::Int bindable(const std::string& button) {
    for (const auto& [name, bit] : BINDABLE) {
        if (button == name) return bit;
    }
    return 0;
}

// Godot's KeyModifierMask.
constexpr gd::Int KEY_MASK_SHIFT = 1 << 25, KEY_MASK_ALT = 1 << 26, KEY_MASK_META = 1 << 27, KEY_MASK_CTRL = 1 << 28;
constexpr gd::Int KEY_MODIFIER_MASK = 0x7E000000;

// Godot's key names (core/os/keyboard.cpp) of the keys keys.h has.
const std::vector<std::pair<gd::Int, std::string>>& key_names() {
    static const std::vector<std::pair<gd::Int, std::string>> names = [] {
        using namespace Keys;
        std::vector<std::pair<gd::Int, std::string>> out = {
            {ESCAPE, "Escape"}, {TAB, "Tab"}, {BACKSPACE, "Backspace"}, {ENTER, "Enter"}, {KP_ENTER, "Kp Enter"},
            {LEFT, "Left"}, {UP, "Up"}, {RIGHT, "Right"}, {DOWN, "Down"}, {SHIFT, "Shift"}, {CTRL, "Ctrl"},
            {ALT, "Alt"}, {SPACE, "Space"}, {SEMICOLON, "Semicolon"}};
        for (gd::Int i = 0; i < 12; ++i) out.emplace_back(F1 + i, "F" + gd::str(i + 1));
        for (char c = '0'; c <= '9'; ++c) out.emplace_back(c, std::string(1, c));
        for (char c = 'A'; c <= 'Z'; ++c) out.emplace_back(c, std::string(1, c));
        return out;
    }();
    return names;
}

bool same_text(const std::string& a, const std::string& b) {   // String.nocasecmp_to == 0
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// str() of a JSON value (a float as Godot prints it: "3.0", "14.6").
std::string json_str(const gd::Json& v) {
    switch (v.type()) {
        case gd::Json::Type::Null: return "<null>";
        case gd::Json::Type::Bool: return v.boolean() ? "true" : "false";
        case gd::Json::Type::Number: {
            std::string s = gd::json_stringify(gd::JsonValue(v.num(), 14));
            return s;
        }
        case gd::Json::Type::String: return v.str();
        default: return "";   // an Array or Dictionary: no setting takes one
    }
}

GameSettings::Bindings deep_copy(const GameSettings::Bindings& b) {   // duplicate(true)
    GameSettings::Bindings out;
    for (const auto& [layout, keys] : b) out.set(layout, keys.duplicate());
    return out;
}

}  // namespace

GameSettings::GameSettings() : store(gd::make<SaveStore>()) {}

void GameSettings::_ready() {
    usa_converted = Assets_has_group && Assets_has_group("usa");
    // TODO: TexturePacks.installed() (the texture pack slice): no packs are offered yet.
    texture_packs = gd::Arr<std::string>();
    load_saved();
}

void GameSettings::load_saved() {
    values = sanitized(store->load_settings());
    _apply_volumes();
    _apply_key_layouts();
}

gd::Dict<std::string, GameSettings::Value> GameSettings::sanitized(const gd::Json& saved) {
    gd::Dict<std::string, Value> out;
    for (const auto& [key, entry] : SCHEMA) {
        // var value: Variant = saved.get(key, entry[0]): the saved JSON value, or the default.
        bool given = saved.has(key);
        const gd::Json& json = saved.get(key);
        const gd::Arr<Value>& list = entry.choices;
        // JSON keeps numbers as floats.
        bool is_number = given ? json.is_number() : std::holds_alternative<gd::Int>(entry.def);
        Value value = entry.def;
        if (list.is_empty()) {
            gd::Int n = given ? JsonFile::number(json) : (is_number ? std::get<gd::Int>(entry.def) : 0);
            value = is_number ? Value(gd::clampi(n, 0, VOLUME_STEPS)) : entry.def;
        } else if (std::holds_alternative<std::string>(list[0]) && std::get<std::string>(list[0]).empty()) {
            value = given ? json_str(json) : _str(entry.def);
        } else {
            bool known = true;   // false: a value no choice can equal (a float, null, an array, ...)
            if (given) {
                if (std::holds_alternative<gd::Int>(list[0]) && is_number) value = JsonFile::number(json);
                else if (json.is_bool()) value = json.boolean();
                else if (json.is_string()) value = json.str();
                else known = false;
            }
            if (!known || !list.has(value)) {
                value = entry.def;
            }
        }
        out.set(key, value);
    }
    out.set(KEY_BINDINGS, sanitized_bindings(saved.get(KEY_BINDINGS)));
    // A Japanese interface saved where it cannot be drawn (the web builds) follows the system.
    if (_str(out.at("language")) == "ja" && !japanese_interface()) {
        out.set("language", std::string("auto"));
    }
    return out;
}

GameSettings::Bindings GameSettings::default_bindings() {
    Bindings out;
    for (gd::Int layout = 0; layout < LAYOUT_NAMES.size(); ++layout) {
        gd::Dict<std::string, std::string> keys;
        for (const auto& [key, bit] : default_layout(layout)) {
            keys.set(_button_name(bit), keycode_string(key));
        }
        out.set(LAYOUT_NAMES[layout], keys);
    }
    return out;
}

GameSettings::Bindings GameSettings::sanitized_bindings(const gd::Json& saved) {
    Bindings defaults = default_bindings();
    if (!saved.is_object()) {
        return defaults;
    }
    Bindings out;
    gd::Dict<gd::Int, bool> used;
    gd::Arr<gd::Arr<std::string>> missing;     // [layout, button] still without a key
    for (const std::string& layout_name : LAYOUT_NAMES) {
        const gd::Json& given = saved.get(layout_name);   // {} when it is no Dictionary: get() of a non-object is null
        gd::Dict<std::string, std::string> keys;
        for (const auto& [button, bit] : BINDABLE) {
            const gd::Json& name = given.get(button);
            gd::Int key = name.is_string() ? key_of(name.str()) : KEY_NONE;
            if (key == KEY_NONE || used.has(key)) {
                missing.append(gd::Arr<std::string>{layout_name, button});
                continue;
            }
            used.set(key, true);
            keys.set(button, keycode_string(key));
        }
        out.set(layout_name, keys);
    }
    gd::PI64 spare;                            // default keys nobody holds, in layout order
    for (const std::string& layout_name : LAYOUT_NAMES) {
        for (const auto& [button, bit] : BINDABLE) {
            gd::Int key = key_of(defaults.at(layout_name).at(button));
            if (!used.has(key)) {
                spare.append(key);
            }
        }
    }
    for (const gd::Arr<std::string>& entry : missing) {
        gd::Int key = key_of(defaults.at(entry[0]).at(entry[1]));
        if (used.has(key)) {
            // The default of the button that holds this one's (a swap), else any left over.
            gd::Arr<std::string> holder = _holder(out, keycode_string(key));
            gd::Int swapped = !holder.is_empty() ? key_of(defaults.at(holder[0]).at(holder[1])) : KEY_NONE;
            key = swapped != KEY_NONE && !used.has(swapped) ? swapped : _first_free(spare, used);
            if (key == KEY_NONE) {
                return defaults;     // cannot happen: each missing button leaves a default key over
            }
        }
        used.set(key, true);
        out.at(entry[0]).set(entry[1], keycode_string(key));
    }
    return out;
}

gd::Int GameSettings::_first_free(const gd::PI64& keys, const gd::Dict<gd::Int, bool>& used) {
    for (gd::Int key : keys) {
        if (!used.has(key)) {
            return key;
        }
    }
    return KEY_NONE;
}

gd::Arr<std::string> GameSettings::_holder(const Bindings& bindings, const std::string& name) {
    for (const auto& [layout_name, keys] : bindings) {
        for (const auto& [button, key] : keys) {
            if (key == name) {
                return gd::Arr<std::string>{layout_name, button};
            }
        }
    }
    return gd::Arr<std::string>();
}

gd::Int GameSettings::key_of(const std::string& name) {
    if (name.empty()) {
        return KEY_NONE;
    }
    gd::Int key = find_keycode_from_string(name);
    if ((key & KEY_MODIFIER_MASK) != 0 || InputRouter::reserved(key)) {
        return KEY_NONE;
    }
    return key;
}

std::string GameSettings::_button_name(gd::Int bit) {
    for (const auto& [name, b] : BINDABLE) {
        if (b == bit) return name;
    }
    return "<null>";   // str(find_key) of a bit no button has
}

std::map<gd::Int, gd::Int> GameSettings::key_layout(gd::Int layout) {
    const gd::Dict<std::string, std::string>& keys = std::get<Bindings>(values.at(KEY_BINDINGS)).at(LAYOUT_NAMES[layout]);
    std::map<gd::Int, gd::Int> out;
    for (const auto& [button, key] : keys) {
        out[key_of(key)] = bindable(button);
    }
    return out;
}

gd::Int GameSettings::bound_key(gd::Int layout, gd::Int bit) {
    const gd::Dict<std::string, std::string>& keys = std::get<Bindings>(values.at(KEY_BINDINGS)).at(LAYOUT_NAMES[layout]);
    return key_of(keys.at(_button_name(bit)));
}

void GameSettings::bind_key(gd::Int layout, gd::Int bit, gd::Int key) {
    if (key_of(keycode_string(key)) == KEY_NONE) {
        return;
    }
    Bindings bindings = deep_copy(std::get<Bindings>(values.at(KEY_BINDINGS)));
    std::string button = _button_name(bit);
    std::string name = keycode_string(key);
    std::string old = bindings.at(LAYOUT_NAMES[layout]).at(button);
    gd::Arr<std::string> holder = _holder(bindings, name);
    if (!holder.is_empty()) {
        bindings.at(holder[0]).set(holder[1], old);
    }
    bindings.at(LAYOUT_NAMES[layout]).set(button, name);
    set_value(KEY_BINDINGS, bindings);
}

void GameSettings::reset_key_bindings() {
    set_value(KEY_BINDINGS, default_bindings());
}

GameSettings::Value GameSettings::value(const std::string& key) {
    if (session.has(key)) {
        return session.at(key);
    }
    Value v = values.get(key, SCHEMA.at(key).def);
    if (key == "game_texts" && !choices(key).has(v)) {
        return choices(key)[0];
    }
    return v;
}

bool GameSettings::flag(const std::string& key) {
    Value v = value(key);
    return std::holds_alternative<bool>(v) && std::get<bool>(v);
}

std::string GameSettings::text(const std::string& key) {
    return _str(value(key));
}

std::string GameSettings::read_text(const std::string& key) {
    if (autoload) return autoload->text(key);
    return SCHEMA.has(key) ? _str(SCHEMA.at(key).def) : "";
}

std::string GameSettings::read_preset(const std::string& fallback) {
    return autoload ? autoload->preset() : fallback;
}

gd::Int GameSettings::number(const std::string& key) {
    Value v = value(key);
    return std::holds_alternative<gd::Int>(v) ? std::get<gd::Int>(v) : 0;
}

double GameSettings::level(const std::string& key) {
    return static_cast<double>(number(key)) / VOLUME_STEPS;
}

gd::Arr<GameSettings::Value> GameSettings::choices(const std::string& key) {
    gd::Arr<Value> list = _offered(key);
    if (session.has(key) && !list.has(session.at(key))) {
        list = list.duplicate();
        list.append(session.at(key));
    }
    return list;
}

gd::Arr<GameSettings::Value> GameSettings::_offered(const std::string& key) {
    gd::Arr<Value> list = SCHEMA.at(key).choices;
    auto names = [](std::initializer_list<std::string> first, const gd::Arr<std::string>& rest) {
        gd::Arr<Value> out;
        for (const std::string& s : first) out.append(s);
        for (const std::string& s : rest) out.append(s);
        return out;
    };
    if (key == "graphics") {
        return names({"auto"}, RENDERER_PRESETS.get(renderer(), gd::Arr<std::string>()));
    } else if (key == "texture_pack") {
        return names({""}, texture_packs);
    } else if (key == "language") {
        return japanese_interface() ? list : names({"auto", "en"}, {});
    } else if (key == "game_texts") {
        // Assets.japanese_texts(): without a manifest the Japanese release's (Japan Rev.1).
        bool japanese = Assets_japanese_texts ? Assets_japanese_texts() : true;
        return japanese ? list : names({"english"}, {});
    } else if (key == "stage_backdrops") {
        bool arcade = Assets_has_group && Assets_has_group("arcade");
        return arcade ? list : names({"playstation"}, {});
    } else if (key == "fullscreen") {
        // OS.has_feature("pc"): the desktop builds.
#ifdef __ANDROID__
        return gd::Arr<Value>{false};
#else
        return list;
#endif
    }
    return list;
}

void GameSettings::set_value(const std::string& key, const Value& v) {
    // A session choice ends with any other choice, the saved one too.
    if (session.has(key)) {
        if (_equal(session.at(key), v)) {
            return;
        }
        session.erase(key);
    } else if (values.has(key) && _equal(values.at(key), v)) {
        return;
    }
    values.set(key, v);
    if (BUS_VOLUMES.has(key)) {
        _apply_volumes();
    }
    if (key == KEY_BINDINGS) {
        _apply_key_layouts();
    } else if (key == "fullscreen") {
        _apply_fullscreen(std::holds_alternative<bool>(v) && std::get<bool>(v));
    }
    _dirty = true;
    for (const auto& f : std::vector<std::function<void(const std::string&)>>(changed)) f(key);
}

void GameSettings::save() {
    if (_dirty && store->save_settings(_to_json(values))) {
        _dirty = false;
    }
}

void GameSettings::_notification(gd::Int what) {
    if (what == NOTIFICATION_WM_CLOSE_REQUEST || what == NOTIFICATION_APPLICATION_PAUSED ||
        what == NOTIFICATION_PREDELETE) {
        save();
    }
}

void GameSettings::cycle(const std::string& key, gd::Int step) {
    gd::Arr<Value> list = choices(key);
    if (list.is_empty()) {
        set_value(key, gd::clampi(number(key) + step, 0, VOLUME_STEPS));
        return;
    }
    if (list.size() == 1) {
        return;
    }
    gd::Int i = list.find(value(key));
    set_value(key, list[gd::posmod(i + step, list.size())]);
}

bool GameSettings::fullscreen_offered() {
    return choices("fullscreen").size() > 1;
}

void GameSettings::toggle_fullscreen() {
    if (!fullscreen_offered()) {
        return;
    }
    bool on = !_window_fullscreen();
    if (values.has("fullscreen") && _equal(values.at("fullscreen"), on)) {
        _apply_fullscreen(on);
    } else {
        set_value("fullscreen", on);
    }
}

void GameSettings::apply_window() {
    if (flag("fullscreen")) {
        _apply_fullscreen(true);
    }
}

bool GameSettings::_window_fullscreen() {
    return DisplayServer_window_fullscreen && DisplayServer_window_fullscreen();
}

void GameSettings::_apply_fullscreen(bool on) {
    // DisplayServer.get_name() == "headless": no window hook.
    if (!DisplayServer_set_fullscreen || !fullscreen_offered()) {
        return;
    }
    DisplayServer_set_fullscreen(on);
}

std::string GameSettings::renderer() {
    // RenderingServer.get_current_rendering_method(): the remake's Android export uses the Mobile
    // renderer, the desktop builds Forward+ (as presentation/render_quality.cpp).
#ifdef __ANDROID__
    return "mobile";
#else
    return "forward_plus";
#endif
}

std::string GameSettings::preset() {
    std::string chosen = text("graphics");
    gd::Arr<std::string> offered = RENDERER_PRESETS.get(renderer(), gd::Arr<std::string>{"desktop_low"});
    if (offered.has(chosen)) {
        return chosen;
    }
    if (offered.has("desktop_high")) return "desktop_high";
    return offered.has("mobile") ? std::string("mobile") : offered[0];   // native: mobile_low is listed first
}

bool GameSettings::touch_enabled() {
    std::string chosen = text("touch_controls");
    if (chosen == "on") {
        return true;
    } else if (chosen == "off") {
        return false;
    }
    return touchscreen_available;
}

double GameSettings::touch_scale() {
    return static_cast<double>(number("touch_size")) / 100.0;
}

std::string GameSettings::locale() {
    if (!japanese_interface()) {
        return "en";
    }
    std::string chosen = text("language");
    if (chosen != "auto") {
        return chosen;
    }
    return os_locale_language == "ja" ? "ja" : "en";
}

bool GameSettings::japanese_interface() {
    return true;   // not OS.has_feature("web")
}

void GameSettings::_apply_volumes() {
    for (const auto& [key, name] : BUS_VOLUMES) {
        gd::Int bus = AudioServer_get_bus_index ? AudioServer_get_bus_index(name) : -1;
        if (bus < 0) {
            continue;
        }
        double share = level(key);
        if (AudioServer_set_bus_mute) AudioServer_set_bus_mute(bus, share <= 0.0);
        if (AudioServer_set_bus_volume_db) {
            AudioServer_set_bus_volume_db(bus, Mixer::linear_to_db(static_cast<float>(std::max(share, 0.001))));
        }
    }
}

void GameSettings::_apply_key_layouts() {
    if (Pads == nullptr) return;
    for (gd::Int layout = 0; layout < LAYOUT_NAMES.size(); ++layout) {
        Pads->layouts[layout] = key_layout(layout);
    }
}

void GameSettings::apply_audio() {
    _apply_volumes();
}

std::string GameSettings::_str(const Value& v) {
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<gd::Int>(v)) return gd::str(std::get<gd::Int>(v));
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    return gd::json_stringify(_to_json(v));
}

bool GameSettings::_equal(const Value& a, const Value& b) {
    if (a.index() != b.index()) return false;
    if (!std::holds_alternative<Bindings>(a)) return a == b;
    const Bindings& x = std::get<Bindings>(a);
    const Bindings& y = std::get<Bindings>(b);
    if (x.size() != y.size()) return false;
    for (const auto& [layout, keys] : x) {
        if (!y.has(layout) || y.at(layout).size() != keys.size()) return false;
        for (const auto& [button, key] : keys) {
            if (!y.at(layout).has(button) || y.at(layout).at(button) != key) return false;
        }
    }
    return true;
}

gd::JsonValue GameSettings::_to_json(const Value& v) {
    if (std::holds_alternative<bool>(v)) return gd::JsonValue(std::get<bool>(v));
    if (std::holds_alternative<gd::Int>(v)) return gd::JsonValue(std::get<gd::Int>(v));
    if (std::holds_alternative<std::string>(v)) return gd::JsonValue(std::get<std::string>(v));
    gd::JsonValue out = gd::JsonValue::object();
    for (const auto& [layout, keys] : std::get<Bindings>(v)) {
        gd::JsonValue rec = gd::JsonValue::object();
        for (const auto& [button, key] : keys) rec.set(button, key);
        out.set(layout, rec);
    }
    return out;
}

gd::JsonValue GameSettings::_to_json(const gd::Dict<std::string, Value>& values) {
    gd::JsonValue out = gd::JsonValue::object();
    for (const auto& [key, v] : values) out.set(key, _to_json(v));
    return out;
}

std::string GameSettings::keycode_string(gd::Int key) {
    std::string out;
    if ((key & KEY_MASK_SHIFT) != 0) out += "Shift+";
    if ((key & KEY_MASK_CTRL) != 0) out += "Ctrl+";
    if ((key & KEY_MASK_ALT) != 0) out += "Alt+";
    if ((key & KEY_MASK_META) != 0) out += "Meta+";
    gd::Int code = key & ~KEY_MODIFIER_MASK;
    for (const auto& [k, name] : key_names()) {
        if (k == code) return out + name;
    }
    return out + (code > 0 && code < 0x7F ? std::string(1, static_cast<char>(code)) : std::string());
}

gd::Int GameSettings::find_keycode_from_string(const std::string& name) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t plus = name.find('+', start);
        parts.push_back(name.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    gd::Int key = KEY_NONE;
    for (const auto& [k, text] : key_names()) {
        if (same_text(parts.back(), text)) {
            key = k;
            break;
        }
    }
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (same_text(parts[i], "Shift")) key |= KEY_MASK_SHIFT;
        else if (same_text(parts[i], "Ctrl")) key |= KEY_MASK_CTRL;
        else if (same_text(parts[i], "Meta")) key |= KEY_MASK_META;
        else if (same_text(parts[i], "Alt")) key |= KEY_MASK_ALT;
    }
    return key;
}

}  // namespace t3

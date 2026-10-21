// remake/presentation/screens/settings_view.gd
// The remake's settings screen (GameSettings): a vector panel over the main menu (settings_view.h).
#include "presentation/screens/settings_view.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

#include "platform/input_router.h"
#include "platform/keys.h"
#include "platform/settings.h"
#include "presentation/hud/button_glyphs.h"

namespace t3 {

namespace {

// Native: Godot's Vector2 / Rect2 methods the view uses (gfx::Vec2 / Rect2 are plain records).
bool is_zero(gfx::Vec2 v) { return v.x == 0.0f && v.y == 0.0f; }
gfx::Vec2 mul(gfx::Vec2 a, gfx::Vec2 b) { return gfx::Vec2(a.x * b.x, a.y * b.y); }
gfx::Vec2 div(gfx::Vec2 a, gfx::Vec2 b) { return gfx::Vec2(a.x / b.x, a.y / b.y); }
float dot(gfx::Vec2 a, gfx::Vec2 b) { return a.x * b.x + a.y * b.y; }
float length(gfx::Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
gfx::Vec2 center(const gfx::Rect2& r) { return r.position + r.size * 0.5f; }
bool has_point(const gfx::Rect2& r, gfx::Vec2 p) {
    return p.x >= r.position.x && p.y >= r.position.y && p.x < r.position.x + r.size.x && p.y < r.position.y + r.size.y;
}
gfx::Rect2 merge(const gfx::Rect2& a, const gfx::Rect2& b) {
    gfx::Vec2 p(std::min(a.position.x, b.position.x), std::min(a.position.y, b.position.y));
    gfx::Vec2 e(std::max(a.end().x, b.end().x), std::max(a.end().y, b.end().y));
    return gfx::Rect2(p, e - p);
}
std::string upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}
// "%d" % n
std::string format_d(const std::string& fmt, gd::Int n) {
    std::string out = fmt;
    size_t at = out.find("%d");
    if (at != std::string::npos) out.replace(at, 2, gd::str(n));
    return out;
}
// InputRouter.has_keyboard: the desktop builds always have one; on Android one that was used
// (TODO: DisplayServer.has_hardware_keyboard).
bool has_keyboard(const InputRouter* pads) {
#ifdef __ANDROID__
    return pads != nullptr && pads->has_keyboard();
#else
    (void)pads;
    return true;
#endif
}

// The English column of remake/app/translations.csv, for the keys this screen and the settings
// button use (TODO: translations: TranslationServer and the Japanese column).
const gd::Dict<std::string, std::string>& english() {
    static const gd::Dict<std::string, std::string> d = [] {
        gd::Dict<std::string, std::string> t;
        t.set("SETTINGS_TITLE", "SETTINGS");
        t.set("SETTINGS_GAME", "Game");
        t.set("SETTINGS_DISPLAY", "Display");
        t.set("SETTINGS_SOUND", "Sound");
        t.set("SETTINGS_BACK", "Back");
        t.set("SETTINGS_ON", "On");
        t.set("SETTINGS_OFF", "Off");
        t.set("SETTINGS_NONE", "None");
        t.set("SETTINGS_KEYS", "Directions: choose    Left/Right, Cross: change    L1/R1, Tab: tab    Circle, Select, Esc: back");
        t.set("SETTINGS_TAB_GAME", "General");
        t.set("SETTINGS_TAB_CONTROLS", "Controls");
        t.set("SETTING_GAMEPLAY_FIXES", "Gameplay fixes");
        t.set("SETTINGS_FIXES_HELP", "Fixes the original's bugs that change matches, the CPU's behaviour and the unlock pace. Takes effect from the next match.");
        t.set("SETTING_GAME_TEXTS", "Game texts");
        t.set("SETTINGS_TEXTS_HELP", "English shows the North American release's wording everywhere it has one: menus, practice, records, Tekken Force, the Theater, the staff roll, the move lists and the rules page. The game plays the same either way.");
        t.set("SETTINGS_TEXTS_MISSING", "Names, move lists and pictures need the USA disc converted (tools/remake_import/convert.py --usa-image).");
        t.set("SETTING_LANGUAGE", "Interface language");
        t.set("SETTINGS_LANGUAGE_HELP", "The language of the remake's own screens (this one and the hints). The game's screens follow Game texts.");
        t.set("LANGUAGE_EN", "English");
        t.set("LANGUAGE_JA", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        t.set("SETTING_MEMORY_CARD", "Memory card");
        t.set("SETTINGS_CARD_HELP", "The save the game uses, as the memory card in slot 1 or 2: each keeps its own progress, records and game options. Switching loads that card's progress (unsaved progress is left behind).");
        t.set("SETTINGS_VALUE_CARD", "Slot %d");
        t.set("SETTING_FULLSCREEN", "Fullscreen");
        t.set("SETTINGS_FULLSCREEN_HELP", "Fills the whole screen with the game's window. On Windows F11 or Alt+Enter switches it anywhere.");
        t.set("SETTING_GRAPHICS", "Graphics preset");
        t.set("SETTINGS_GRAPHICS_HELP", "Shadows, bloom, anti-aliasing and on High ambient occlusion and global illumination. Auto picks the best preset of this renderer.");
        t.set("SETTING_TEXTURE_FILTER", "Textures");
        t.set("SETTINGS_FILTER_HELP", "Smooth filters the original textures; Crisp keeps their pixels for a retro look.");
        t.set("SETTING_SHADING", "Shading");
        t.set("SETTINGS_SHADING_HELP", "Smooth blends the fighters' polygons by averaging their normals; Curved also rounds their outlines; Original lights them as the game does.");
        t.set("SETTING_TEXTURE_PACK", "Texture pack");
        t.set("SETTINGS_PACK_HELP", "Replacement textures from user://texture_packs/<name>/ (see remake/README.md). Applies to the next scene.");
        t.set("SETTING_INTERPOLATION", "Frame interpolation");
        t.set("SETTINGS_INTERPOLATION_HELP", "Smooth motion between the 60 Hz steps on faster displays; off shows each step as it is.");
        t.set("SETTING_FRAMING", "Beyond the scene");
        t.set("SETTING_STAGE_BACKDROPS", "Stage backdrops");
        t.set("SETTINGS_BACKDROPS_HELP", "Arcade draws the arcade version's polygonal scenery, sky and floor (from the MAME set given to the converter); PlayStation draws the home version's flat panoramas. Takes effect from the next stage.");
        t.set("SETTINGS_FRAMING_HELP", "What fills the window where the scene ends (wide or tall windows): the stage's clear colour or black bars.");
        t.set("SETTING_VOLUME_MUSIC", "Music");
        t.set("SETTING_VOLUME_SFX", "Effects");
        t.set("SETTING_VOLUME_VOICES", "Voices");
        t.set("SETTING_VOLUME_SYSTEM", "System sounds");
        t.set("SETTING_VIBRATION", "Vibration");
        t.set("SETTING_TOUCH_CONTROLS", "Touch controls");
        t.set("SETTINGS_TOUCH_HELP", "The on-screen stick and buttons. Auto shows them on touch screens.");
        t.set("SETTING_TOUCH_MACROS", "Touch macros");
        t.set("SETTINGS_MACROS_HELP", "Extra touch buttons that press several limbs at once: the throws (1+3, 2+4), or also 1+2 and 3+4.");
        t.set("SETTING_TOUCH_SIZE", "Touch button size");
        t.set("SETTING_TOUCH_OPACITY", "Touch button opacity");
        t.set("SETTING_TOUCH_HAPTICS", "Touch vibration");
        t.set("SETTING_TOUCH_DIRECTIONS", "Touch directions");
        t.set("SETTINGS_VALUE_DPAD", "D-pad");
        t.set("SETTINGS_VALUE_STICK", "Stick");
        t.set("SETTINGS_TOUCH_SIZE_HELP", "The size of the on-screen stick and buttons, in percent of the default.");
        // Native: the layout edit's row.
        t.set("SETTING_TOUCH_LAYOUT", "Edit layout");
        t.set("SETTINGS_TOUCH_LAYOUT_HELP", "Move and resize the on-screen stick and buttons: drag a control, pinch it or drag its corner. Done keeps the layout, Reset brings back the default one.");
        t.set("SETTING_DEFAULT_KEYS", "Default keys");
        t.set("SETTINGS_DEFAULT_KEYS_HELP", "Gives both keyboard layouts their default keys back: W A S D + R T / F G for player 1, the arrows + U I / J K for player 2.");
        t.set("SETTINGS_PLAYER_KEYS", "Player %d keyboard");
        t.set("SETTINGS_PRESS_KEY", "Press a key");
        t.set("SETTINGS_BIND_HELP", "Cross, Enter or a click, then press the new key. A key another button has swaps with this one's. Esc cancels.");
        t.set("SETTINGS_VALUE_THROWS", "Throws");
        t.set("SETTINGS_VALUE_ALL", "All");
        t.set("SETTINGS_VALUE_NONE", "None");
        t.set("SETTINGS_VALUE_AUTO", "Auto");
        t.set("SETTINGS_VALUE_ENGLISH", "English (USA)");
        t.set("SETTINGS_VALUE_JAPANESE", "Japanese release");
        t.set("SETTINGS_VALUE_WEB", "Web");
        t.set("SETTINGS_VALUE_MOBILE", "Standard");
        t.set("SETTINGS_VALUE_MOBILE_HIGH", "High");
        t.set("SETTINGS_VALUE_DESKTOP_LOW", "Low");
        t.set("SETTINGS_VALUE_DESKTOP_HIGH", "High");
        t.set("SETTINGS_VALUE_SMOOTH", "Smooth");
        t.set("SETTINGS_VALUE_ORIGINAL", "Original");
        t.set("SETTINGS_VALUE_CURVED", "Smooth + curved");
        t.set("SETTINGS_VALUE_CRISP", "Crisp (retro)");
        t.set("SETTINGS_VALUE_STAGE", "Stage colour");
        t.set("SETTINGS_VALUE_BARS", "Black bars");
        t.set("SETTINGS_VALUE_ARCADE", "Arcade");
        t.set("SETTINGS_VALUE_PLAYSTATION", "PlayStation");
        t.set("SETTINGS_VALUE_ON", "On");
        t.set("SETTINGS_VALUE_OFF", "Off");
        t.set("SETTINGS_OPEN_HINT", "Select: settings");
        return t;
    }();
    return d;
}

}  // namespace

std::string SettingsView::tr(const std::string& key) {
    const gd::Dict<std::string, std::string>& t = english();
    return t.has(key) ? t.at(key) : key;
}

gd::Int SettingsView::find_row(const std::string& key) {
    for (gd::Int i = 0; i < ROWS.size(); ++i) {
        if (ROWS[i].key == key) return i;
    }
    return -1;
}

SettingsView::SettingsView() {
    font = gfx::Font::fallback();
    visible = false;
}

void SettingsView::open() {
    keyboard = has_keyboard(Pads);
    listening = -1;
    cursor = _first_item(tab);
    _suppress = true;
    visible = true;
}

void SettingsView::close() {
    visible = false;
    listening = -1;
    if (Settings != nullptr) Settings->save();
    for (const auto& f : closed) f();
}

void SettingsView::handle(gd::Int held) {
    if (!visible) {
        return;
    }
    if (listening >= 0) {
        // The new key arrives as a key event (_input); the pads wait for their release.
        _held = held;
        _suppress = true;
        return;
    }
    if (_suppress) {
        if (held != 0) {
            _held = held;
            return;
        }
        _suppress = false;
    }
    gd::Int pressed = held & ~_held;
    if (held != 0 && held == _held) {
        _held_frames += 1;
    } else {
        _held_frames = 0;
    }
    _held = held;
    gd::Int repeat = pressed;
    if (_held_frames >= REPEAT_DELAY && (_held_frames - REPEAT_DELAY) % REPEAT_RATE == 0) {
        repeat |= held & (UP | DOWN | LEFT | RIGHT);
    }
    if ((pressed & CANCEL) != 0) {
        close();
        return;
    }
    // Nothing new pressed nor repeating: the screen stays as drawn (it is drawn again on a change,
    // a resize and a pointer's touch).
    if (pressed == 0 && repeat == 0) {
        return;
    }
    _layout();
    if ((pressed & (PREVIOUS_TAB | NEXT_TAB)) != 0) {
        switch_tab((pressed & PREVIOUS_TAB) != 0 ? -1 : 1);
    } else if ((repeat & UP) != 0) {
        cursor = neighbour(cursor, gfx::Vec2(0, -1));
    } else if ((repeat & DOWN) != 0) {
        cursor = neighbour(cursor, gfx::Vec2(0, 1));
    } else if ((repeat & (LEFT | RIGHT)) != 0) {
        gd::Int step = (repeat & LEFT) != 0 ? -1 : 1;
        if (_is_setting(cursor)) {
            _change(cursor, step);
        } else {
            cursor = neighbour(cursor, gfx::Vec2(static_cast<float>(step), 0));
        }
    } else if ((pressed & CONFIRM) != 0) {
        _change(cursor, 1);
    }
}

// Keys: a waiting field's new key, Tab between the tabs; a gamepad button stops the wait.
bool SettingsView::key(gd::Int key, bool down) {
    if (key == Keys::SHIFT) _shift = down;
    if (!visible) {
        return false;
    }
    if (!down) return false;
    if (listening >= 0) {
        // TODO: A fullscreen hotkey is no key to bind: it reaches Settings and the field keeps
        // waiting (GameSettings.is_fullscreen_hotkey is not ported).
        _handled();
        // The layouts read physical keys only (InputRouter): a key event without one cannot
        // be bound, and the field keeps waiting.
        if (key == GameSettings::KEY_NONE) {
            return true;
        }
        if (!InputRouter::reserved(key)) {
            Settings->bind_key(field_layout(listening), field_button(listening), key);
        }
        _stop_listening();
        return true;
    } else if (key == Keys::TAB) {
        _handled();
        switch_tab(_shift ? -1 : 1);
        return true;
    }
    return false;
}

bool SettingsView::joy_button(bool down) {
    if (!visible) return false;
    if (listening >= 0 && down) {
        _handled();
        _stop_listening();
        return true;
    }
    return false;
}

// get_viewport().set_input_as_handled(): the callers' return value.
void SettingsView::_handled() {}

void SettingsView::_stop_listening() {
    listening = -1;
    _suppress = true;
}

void SettingsView::switch_tab(gd::Int step) {
    tab = gd::posmod(tab + step, TABS.size());
    keyboard = has_keyboard(Pads);     // a keyboard attached while the screen is open
    listening = -1;
    cursor = _first_item(tab);
}

bool SettingsView::pointer(PointerEvent kind, const Pointer& p) {
    if (!visible) return false;
    // A tap arrives as the touch and as a mouse click emulated from it (a drag as a mouse motion
    // too): only the touch counts. Native: the platforms send each finger once.
    gfx::Vec2 position(p.x, p.y);
    if (kind == PointerEvent::UP || kind == PointerEvent::CANCEL) {
        _dragging = -1;
        return true;
    }
    if (kind == PointerEvent::MOVE) {
        if (_dragging >= 0) {
            _set_level(_dragging, position.x);
        }
        return true;
    }
    gfx::Vec2 at = position;
    _layout();
    bool was_listening = listening >= 0;
    listening = -1;
    for (gd::Int t = 0; t < _tab_rects.size(); ++t) {
        if (has_point(_tab_rects[t], at)) {
            if (t != tab) {
                switch_tab(t - tab);
            }
            return true;
        }
    }
    for (gd::Int i = 0; i < _rects.size(); ++i) {
        if (!has_point(_rects[i], at) || !_selectable(i)) {
            continue;
        }
        cursor = i;
        if (_is_level(i)) {
            // On the bar (or its number) the volume goes where the finger is; the label only picks the row.
            if (at.x >= _bar_rect(i).position.x - _row * BAR_SLACK) {
                _dragging = i;
                _set_level(i, at.x);
            }
        } else {
            _change(i, 1);
        }
        return true;
    }
    // A tap that ended a wait only redraws (queue_redraw: drawn every frame).
    if (!was_listening && !has_point(_panel, at)) {
        close();
    }
    return true;
}

gfx::Rect2 SettingsView::_bar_rect(gd::Int i) const {
    gfx::Rect2 r = _rects[i];
    float pad = _row * 0.6f;
    float inner = r.size.x - pad * 0.8f;
    gfx::Vec2 at(r.position.x + pad * 0.4f + inner * LABEL_SHARE, r.position.y);
    return gfx::Rect2(at.x, at.y + _row * 0.36f, inner * (1.0f - LABEL_SHARE) * BAR_SHARE, _row * 0.28f);
}

void SettingsView::_set_level(gd::Int i, float x) {
    gfx::Rect2 bar = _bar_rect(i);
    float share = std::clamp((x - bar.position.x) / bar.size.x, 0.0f, 1.0f);
    Settings->set_value(_key(i), static_cast<gd::Int>(std::round(share * static_cast<float>(GameSettings::VOLUME_STEPS))));
}

std::string SettingsView::_key(gd::Int i) {
    return i < ROWS.size() ? ROWS[i].key : "";
}

bool SettingsView::_is_group(gd::Int i) {
    return _key(i).rfind("SETTINGS_", 0) == 0;
}

bool SettingsView::is_field(gd::Int i) {
    return i >= first_field;
}

gd::Int SettingsView::field_layout(gd::Int i) {
    return (i - first_field) / FIELD_BUTTONS.size();
}

gd::Int SettingsView::field_button(gd::Int i) {
    return FIELD_BUTTONS[(i - first_field) % FIELD_BUTTONS.size()];
}

gd::Int SettingsView::field_of(gd::Int layout, gd::Int bit) {
    return first_field + layout * FIELD_BUTTONS.size() + FIELD_BUTTONS.find(bit);
}

gd::Int SettingsView::tab_of(gd::Int i) {
    if (i == back_row) {
        return -1;
    }
    return is_field(i) || i >= gd::Int(tab_start[CONTROLS]) ? CONTROLS : GAME;
}

bool SettingsView::_is_setting(gd::Int i) const {
    return !is_field(i) && i != back_row && i != default_keys_row && i != touch_layout_row && !_is_group(i);
}

bool SettingsView::_selectable(gd::Int i) const {
    return !_is_group(i) && shown(i) && (tab_of(i) == tab || tab_of(i) < 0);
}

bool SettingsView::shown(gd::Int i) const {
    if (is_field(i) || i == default_keys_row) {
        return keyboard;
    }
    if (i == touch_layout_row) {
        return _touch_layout_shown();
    }
    std::string key = _key(i);
    if (_is_group(i) || key == "back" || _is_level(i)) {
        return true;
    }
    return Settings->choices(key).size() > 1;
}

bool SettingsView::_touch_layout_shown() const {
    return Settings != nullptr && Settings->touch_enabled();
}

bool SettingsView::_is_level(gd::Int i) const {
    return GameSettings::SCHEMA.has(_key(i)) && GameSettings::SCHEMA.at(_key(i)).choices.is_empty();
}

gd::Int SettingsView::_first_item(gd::Int t) const {
    for (gd::Int i = 0; i < item_count; ++i) {
        if (tab_of(i) == t && _selectable(i)) {
            return i;
        }
    }
    return back_row;
}

gd::Int SettingsView::neighbour(gd::Int from, gfx::Vec2 direction) const {
    if (_rects.size() != item_count || is_zero(_rects[from].size)) {
        return from;
    }
    gfx::Vec2 origin = center(_rects[from]);
    gd::Int found = _nearest(from, origin, direction);
    if (found < 0 && direction.y != 0.0f) {
        origin.y = direction.y < 0.0f ? _panel.end().y + 1.0f : _panel.position.y - 1.0f;
        found = _nearest(from, origin, direction);
    }
    return found >= 0 ? found : from;
}

gd::Int SettingsView::_nearest(gd::Int from, gfx::Vec2 origin, gfx::Vec2 direction) const {
    gd::Int best = -1;
    float best_score = std::numeric_limits<float>::infinity();
    gfx::Rect2 source = _rects[from];
    for (gd::Int i = 0; i < _rects.size(); ++i) {
        if (i == from || is_zero(_rects[i].size) || !_selectable(i)) {
            continue;
        }
        gfx::Rect2 r = _rects[i];
        float along = dot(center(r) - origin, direction);
        if (along <= 1.0f) {
            continue;
        }
        float across = 0.0f;          // how far beside the line of sight
        bool facing = false;
        if (direction.x != 0.0f) {
            across = std::max(0.0f, std::max(r.position.y - origin.y, origin.y - r.end().y));
            facing = r.position.y < source.end().y && r.end().y > source.position.y;
        } else {
            across = std::max(0.0f, std::max(r.position.x - origin.x, origin.x - r.end().x));
            facing = r.position.x < source.end().x && r.end().x > source.position.x;
        }
        float score = facing ? along : along + across + length(_panel.size);
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

void SettingsView::_change(gd::Int i, gd::Int step) {
    if (i == back_row) {
        close();
        return;
    }
    if (i == default_keys_row) {
        Settings->reset_key_bindings();
        return;
    }
    // Native: the layout edit opens over the main menu once the screen has closed.
    if (i == touch_layout_row) {
        close();
        for (const auto& f : edit_layout) f();
        return;
    }
    if (is_field(i)) {
        listening = i;
        return;
    }
    Settings->cycle(_key(i), step);
}

std::string SettingsView::value_text(const std::string& key) const {
    GameSettings::Value v = Settings->value(key);
    if (GameSettings::SCHEMA.at(key).choices.is_empty()) {
        return gd::str(Settings->number(key));
    }
    if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v) ? tr("SETTINGS_ON") : tr("SETTINGS_OFF");
    }
    std::string s = GameSettings::_str(v);
    if (key == "texture_pack") {
        return !s.empty() ? s : tr("SETTINGS_NONE");
    }
    if (key == "language" && s != "auto") {
        return tr("LANGUAGE_" + upper(s));
    }
    if (key == "memory_card") {
        return format_d(tr("SETTINGS_VALUE_CARD"), Settings->number(key));
    }
    if (key == "touch_size" || key == "touch_opacity") {
        return gd::str(Settings->number(key)) + "%";
    }
    if (key == "graphics" && s == "auto") {
        return tr("SETTINGS_VALUE_AUTO") + " (" + tr("SETTINGS_VALUE_" + upper(Settings->preset())) + ")";
    }
    return tr("SETTINGS_VALUE_" + upper(s));
}

gd::Int SettingsView::columns() const {
    return size.x >= size.y * TWO_COLUMNS_ASPECT ? 2 : 1;
}

bool SettingsView::_is_footer(gd::Int i, gd::Int t) const {
    return t == CONTROLS && (i == default_keys_row || i == back_row);
}

gd::Int SettingsView::_arrange(gd::Int t, gd::Int count) {
    _column_of.resize(ROWS.size());
    _line_of.resize(ROWS.size());
    _line_of.fill(-1);
    gd::PI32 lines_in{0, 0};
    for (gd::Int i = 0; i < ROWS.size(); ++i) {
        if ((tab_of(i) != t && tab_of(i) >= 0) || !shown(i) || _is_footer(i, t)) {
            continue;
        }
        gd::Int column = count == 2 && i >= gd::Int(column_break[t]) ? 1 : 0;
        _column_of[i] = column;
        _line_of[i] = gd::Int(lines_in[column]);
        lines_in[column] += 1;
    }
    return gd::maxi(lines_in[0], lines_in[1]);
}

gd::Arr<gd::Int> SettingsView::_footer(gd::Int t) const {
    gd::Arr<gd::Int> out;
    for (gd::Int i : {default_keys_row, back_row}) {
        if (_is_footer(i, t) && shown(i)) {
            out.append(i);
        }
    }
    return out;
}

float SettingsView::_content_lines(gd::Int t, gd::Int count) {
    float lines = static_cast<float>(_arrange(t, count));
    if (t == CONTROLS && keyboard) {
        lines += PAD_LINES * static_cast<float>(count == 2 ? 1 : LAYOUTS);
    }
    gd::Int footer = _footer(t).size();
    if (footer > 0) {
        lines += static_cast<float>(count == 2 ? 1 : footer);
    }
    return lines;
}

void SettingsView::_layout() {
    gd::Int count = columns();
    float content = 0.0f;
    for (gd::Int t = 0; t < TABS.size(); ++t) {
        content = std::max(content, _content_lines(t, count));
    }
    float lines = content + EXTRA_LINES + TAB_LINES;
    _row = std::min(std::max(MIN_ROW, size.y * ROW_SHARE), (size.y - 2.0f * SCREEN_MARGIN) / lines);
    float pad = _row * 0.6f;
    float fcount = static_cast<float>(count);
    float width = std::min(size.x - 2.0f * SCREEN_MARGIN, fcount * COLUMN_ROWS * _row + (fcount + 1.0f) * pad);
    float height = _row * lines;
    _panel = gfx::Rect2((size - gfx::Vec2(width, height)) * 0.5f, gfx::Vec2(width, height));
    float column_width = (width - (fcount + 1.0f) * pad) / fcount;
    float left = _panel.position.x + pad * 0.6f;     // the first column's rows, a column further each column_step
    float column_step = column_width + pad;
    _tab_rects.clear();
    float tab_width = (width - 2.0f * pad) / static_cast<float>(TABS.size());
    for (gd::Int t = 0; t < TABS.size(); ++t) {
        _tab_rects.append(gfx::Rect2(_panel.position.x + pad + static_cast<float>(t) * tab_width, _panel.position.y + _row * 1.55f,
                                     tab_width, _row * 0.9f));
    }
    _rects.clear();
    _rects.resize(item_count);
    _pictures.clear();
    float top = _panel.position.y + _row * (1.7f + TAB_LINES);
    float y = top + static_cast<float>(_arrange(tab, count)) * _row;
    for (gd::Int i = 0; i < ROWS.size(); ++i) {
        if (gd::Int(_line_of[i]) >= 0) {
            _rects[i] = gfx::Rect2(left + static_cast<float>(gd::Int(_column_of[i])) * column_step,
                                   top + static_cast<float>(gd::Int(_line_of[i])) * _row, column_width + pad * 0.8f, _row);
        }
    }
    if (tab == CONTROLS && keyboard) {
        for (gd::Int layout = 0; layout < LAYOUTS; ++layout) {
            gd::Int column = count == 2 ? layout : 0;
            gfx::Vec2 at(left + static_cast<float>(column) * column_step,
                         y + static_cast<float>(count == 2 ? 0 : layout) * PAD_LINES * _row);
            _place_pad(layout, gfx::Rect2(at, gfx::Vec2(column_width + pad * 0.8f, PAD_LINES * _row)));
        }
        y += PAD_LINES * _row * static_cast<float>(count == 2 ? 1 : LAYOUTS);
    }
    gd::Arr<gd::Int> footer = _footer(tab);
    for (gd::Int n = 0; n < footer.size(); ++n) {
        gd::Int column = count == 2 ? n : 0;
        gd::Int line = count == 2 ? 0 : n;
        _rects[footer[n]] = gfx::Rect2(left + static_cast<float>(column) * column_step, y + static_cast<float>(line) * _row,
                                       column_width + pad * 0.8f, _row);
    }
}

void SettingsView::_place_pad(gd::Int layout, gfx::Rect2 panel) {
    float pitch = _row * FIELD_PITCH;
    gfx::Rect2 area(panel.position.x, panel.position.y + _row * 1.1f, panel.size.x, pitch * static_cast<float>(FIELDS_PER_SIDE));
    gfx::Vec2 picture_size(area.size.y * 0.95f * PAD_SIZE.x / PAD_SIZE.y, area.size.y * 0.95f);
    if (picture_size.x > panel.size.x * PICTURE_SHARE) {
        picture_size = picture_size * (panel.size.x * PICTURE_SHARE / picture_size.x);
    }
    gfx::Rect2 picture(center(area) - picture_size * 0.5f, picture_size);
    _pictures.append(picture);
    float gap = _row * 0.4f;
    float field_width = (panel.size.x - picture_size.x) / 2.0f - gap;
    for (gd::Int k = 0; k < FIELD_BUTTONS.size(); ++k) {
        bool right = k >= FIELDS_PER_SIDE;
        float x = right ? picture.end().x + gap : panel.position.x;
        _rects[first_field + layout * FIELD_BUTTONS.size() + k] =
            gfx::Rect2(x, area.position.y + static_cast<float>(k % FIELDS_PER_SIDE) * pitch, field_width, pitch * 0.88f);
    }
}

gfx::Vec2 SettingsView::button_spot(gd::Int layout, gd::Int bit) const {
    gfx::Rect2 picture = _pictures[layout];
    return picture.position + div(mul(BUTTON_SPOTS.at(bit), picture.size), PAD_SIZE);
}

void SettingsView::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    size = window;
    _draw(canvas);
}

void SettingsView::_draw(gfx::Canvas& canvas) {
    _layout();
    canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), DIM);
    float row = _row;
    int text_size = static_cast<int>(row * 0.56f);
    canvas.draw_rect(_panel, PANEL);
    canvas.draw_rect(_panel, EDGE, false, 1.5f);
    float pad = row * 0.6f;
    float x = _panel.position.x + pad;
    float width = _panel.size.x - 2.0f * pad;
    font->draw_string(canvas, gfx::Vec2(x, _panel.position.y + row * 1.1f), tr("SETTINGS_TITLE"), gfx::HorizontalAlignment::CENTER,
                      width, static_cast<int>(row * 0.8f), VALUE);
    for (gd::Int t = 0; t < _tab_rects.size(); ++t) {
        gfx::Rect2 r = _tab_rects[t];
        if (t == tab) {
            canvas.draw_rect(r, TAB_ON);
        }
        font->draw_string(canvas, gfx::Vec2(r.position.x, r.position.y + row * 0.62f), tr(TABS[t]), gfx::HorizontalAlignment::CENTER,
                          r.size.x, static_cast<int>(row * 0.5f), t == tab ? VALUE : MUTED);
    }
    // One line under every tab sets the tabs apart from the rows.
    if (!_tab_rects.is_empty()) {
        gfx::Rect2 strip = merge(_tab_rects[0], _tab_rects[_tab_rects.size() - 1]);
        canvas.draw_rect(gfx::Rect2(strip.position.x, strip.end().y - 2.0f, strip.size.x, 2.0f), GROUP);
    }
    float bottom = 0.0f;
    for (gd::Int i = 0; i < ROWS.size(); ++i) {
        gfx::Rect2 r = _rects[i];
        if (is_zero(r.size)) {
            continue;
        }
        bottom = std::max(bottom, r.end().y);
        std::string key = _key(i);
        float left = r.position.x + pad * 0.4f;
        float inner = r.size.x - pad * 0.8f;
        float base = r.position.y + row * 0.68f;
        if (_is_group(i)) {
            font->draw_string(canvas, gfx::Vec2(left, base), tr(key), gfx::HorizontalAlignment::LEFT, inner, static_cast<int>(row * 0.5f),
                              GROUP);
            continue;
        }
        if (i == cursor) {
            canvas.draw_rect(r, FOCUS);
        }
        std::string label = key == "back" ? tr("SETTINGS_BACK") : tr("SETTING_" + upper(key));
        font->draw_string(canvas, gfx::Vec2(left + row * 0.4f, base), label, gfx::HorizontalAlignment::LEFT, inner * LABEL_SHARE,
                          text_size, TEXT);
        if (_is_setting(i)) {
            _draw_value(canvas, key, i, gfx::Vec2(left + inner * LABEL_SHARE, r.position.y), inner * (1.0f - LABEL_SHARE), row,
                        text_size);
        }
    }
    for (gd::Int layout = 0; layout < _pictures.size(); ++layout) {
        _draw_pad(canvas, layout);
        bottom = std::max(bottom, _pictures[layout].end().y);
    }
    // The focused row's or field's help line.
    std::string help = _help(cursor);
    if (!help.empty()) {
        font->draw_multiline_string(canvas, gfx::Vec2(x, bottom + row * 0.7f), help, gfx::HorizontalAlignment::LEFT, width,
                                    static_cast<int>(row * 0.45f), 3, MUTED);
    }
    std::string keys = tr("SETTINGS_KEYS");
    font->draw_string(canvas, gfx::Vec2(x, _panel.end().y - row * 0.35f), keys, gfx::HorizontalAlignment::CENTER, width,
                      _fitted(keys, width, static_cast<int>(row * 0.42f)), MUTED);
}

std::string SettingsView::_help(gd::Int i) const {
    if (is_field(i)) {
        return tr("SETTINGS_BIND_HELP");
    }
    std::string help = !ROWS[i].help.empty() ? tr(ROWS[i].help) : "";
    if (_key(i) == "game_texts" && !Settings->usa_converted) {
        help += " " + tr("SETTINGS_TEXTS_MISSING");
    }
    return help;
}

void SettingsView::_draw_value(gfx::Canvas& canvas, const std::string& key, gd::Int i, gfx::Vec2 at, float width, float row,
                               int text_size) {
    if (_is_level(i)) {
        gfx::Rect2 bar = _bar_rect(i);
        canvas.draw_rect(bar, gfx::Color(1, 1, 1, 0.15f));
        canvas.draw_rect(gfx::Rect2(bar.position, gfx::Vec2(bar.size.x * static_cast<float>(Settings->level(key)), bar.size.y)), VALUE);
        font->draw_string(canvas, gfx::Vec2(bar.end().x + row * 0.3f, at.y + row * 0.68f), value_text(key),
                          gfx::HorizontalAlignment::LEFT, -1, text_size, VALUE);
        return;
    }
    std::string text = value_text(key);
    if (i == cursor) {
        text = "<  " + text + "  >";
    }
    font->draw_string(canvas, gfx::Vec2(at.x, at.y + row * 0.68f), text, gfx::HorizontalAlignment::CENTER, width, text_size, VALUE);
}

void SettingsView::_draw_pad(gfx::Canvas& canvas, gd::Int layout) {
    float row = _row;
    gfx::Rect2 picture = _pictures[layout];
    gfx::Rect2 first = _rects[field_of(layout, FIELD_BUTTONS[0])];
    font->draw_string(canvas, gfx::Vec2(first.position.x, first.position.y - row * 0.4f), format_d(tr("SETTINGS_PLAYER_KEYS"), layout + 1),
                      gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(row * 0.5f), GROUP);
    if (pad_texture != nullptr) {
        canvas.draw_texture_rect(pad_texture, picture, false);
    } else {
        canvas.draw_rect(picture, EDGE, false, 1.0f);
    }
    for (gd::Int k = 0; k < FIELD_BUTTONS.size(); ++k) {
        gd::Int bit = FIELD_BUTTONS[k];
        gd::Int i = field_of(layout, bit);
        gfx::Rect2 r = _rects[i];
        bool right = k >= FIELDS_PER_SIDE;
        float glyph_width = std::min(row * GLYPH_ROWS, r.size.x * 0.4f);
        gfx::Rect2 glyph(right ? r.end().x - glyph_width : r.position.x, r.position.y, glyph_width, r.size.y);
        gfx::Rect2 box(right ? r.position.x : glyph.end().x, r.position.y, r.size.x - glyph_width, r.size.y);
        gfx::Vec2 spot = button_spot(layout, bit);
        gfx::Vec2 from(right ? box.position.x : box.end().x, center(box).y);
        canvas.draw_line(from, spot, i == cursor ? GROUP : LEADER, 1.0f);
        canvas.draw_circle(spot, std::max(2.0f, row * 0.08f), i == cursor ? GROUP : LEADER);
        if (i == cursor) {
            canvas.draw_rect(r, FOCUS);
        }
        canvas.draw_rect(box, BOX);
        canvas.draw_rect(box, EDGE, false, 1.0f);
        _draw_glyph(canvas, bit, glyph);
        std::string text = i == listening ? tr("SETTINGS_PRESS_KEY") : GameSettings::keycode_string(Settings->bound_key(layout, bit));
        float inset = row * 0.15f;
        int text_size = _fitted(text, box.size.x - 2.0f * inset, static_cast<int>(row * 0.45f));
        font->draw_string(canvas, gfx::Vec2(box.position.x + inset, box.position.y + box.size.y * 0.5f + static_cast<float>(text_size) * 0.35f),
                          text, gfx::HorizontalAlignment::CENTER, box.size.x - 2.0f * inset, text_size, i == listening ? GROUP : VALUE);
    }
}

int SettingsView::_fitted(const std::string& text, float width, int most) const {
    int at = most;
    while (at > 8 && font->get_string_size(text, gfx::HorizontalAlignment::LEFT, -1, at).x > width) {
        at -= 1;
    }
    return at;
}

void SettingsView::_draw_glyph(gfx::Canvas& canvas, gd::Int bit, gfx::Rect2 r) {
    gfx::Vec2 centre = center(r);
    float s = std::min(r.size.x, r.size.y) * 0.32f;
    if (SHOULDER_NAMES.has(bit)) {
        std::string name = SHOULDER_NAMES.at(bit);
        int text_size = _fitted(name, r.size.x * 0.9f, static_cast<int>(_row * 0.42f));
        font->draw_string(canvas, gfx::Vec2(r.position.x, centre.y + static_cast<float>(text_size) * 0.35f), name,
                          gfx::HorizontalAlignment::CENTER, r.size.x, text_size, TEXT);
        return;
    }
    if ((bit & PadState::DIRECTIONS) != 0) {
        gfx::Vec2 d = bit == UP ? gfx::Vec2(0, -1) : bit == DOWN ? gfx::Vec2(0, 1) : bit == LEFT ? gfx::Vec2(-1, 0) : gfx::Vec2(1, 0);
        gfx::Vec2 side(-d.y, d.x);
        canvas.draw_colored_polygon({centre + d * s, centre - d * (s * 0.6f) + side * s, centre - d * (s * 0.6f) - side * s}, TEXT);
        return;
    }
    ButtonGlyphs::draw(canvas, bit, centre, s * GLYPH_RADIUS);
}

}  // namespace t3

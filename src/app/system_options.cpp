// Native: SYSTEM OPTION (system_options.h).
#include "app/system_options.h"

#include <cctype>

#include "core/game/game_flow.h"
#include "core/game/options_screen.h"
#include "platform/pad_state.h"
#include "platform/settings.h"
#include "presentation/screens/settings_view.h"

namespace t3 {

namespace {

const char* const TITLES[] = {"SYSTEM OPTION", "GRAPHICS", "SOUND", "GAME", "CONTROLS"};

// Labels that say what they do without the settings screen's help line (the page has none, as the
// game's own pages): key -> label, and a setting's values (off, on) where On / Off would not tell.
const std::pair<const char*, const char*> LABELS[] = {{"gameplay_fixes", "ORIGINAL BUGS"}, {"framing", "SCREEN EDGES"}};
const std::pair<const char*, std::pair<const char*, const char*>> FLAG_VALUES[] = {{"gameplay_fixes", {"KEPT", "FIXED"}}};

gd::Int wrap(gd::Int v, gd::Int step, gd::Int n) { return n <= 0 ? 0 : ((v + step) % n + n) % n; }

}  // namespace

// Capitals, and what font 0 cannot draw (beyond 0x20-0x5F) as "?" (one per UTF-8 character).
std::string SystemOptions::capitals(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t n = ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
        char up = static_cast<char>(std::toupper(ch));
        out += n == 1 && up >= 0x20 && up <= 0x5F ? up : '?';
        i += n;
    }
    return out;
}

std::vector<SystemOptions::Entry> SystemOptions::entries(gd::Int list) const {
    std::vector<Entry> out;
    auto setting = [&](const std::string& key) {
        if (Settings == nullptr || !GameSettings::SCHEMA.has(key)) return;
        bool level = GameSettings::SCHEMA.at(key).choices.is_empty();
        if (!level && Settings->choices(key).size() < 2) return;
        if (key == "fullscreen" && !Settings->fullscreen_offered()) return;
        std::string upper_key = key;
        for (char& c : upper_key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string label = capitals(SettingsView::tr("SETTING_" + upper_key));
        for (const auto& [k, l] : LABELS) {
            if (key == k) label = l;
        }
        out.push_back(Entry{label, Kind::SETTING, key, 0});
    };
    switch (list) {
        case ROOT:
            out.push_back(Entry{"GRAPHICS", Kind::MENU, "", GRAPHICS});
            out.push_back(Entry{"SOUND", Kind::MENU, "", SOUND});
            out.push_back(Entry{"GAME", Kind::MENU, "", GAME});
            out.push_back(Entry{"CONTROLS", Kind::MENU, "", CONTROLS});
            break;
        case GRAPHICS:
            for (const char* key : {"graphics", "texture_filter", "shading", "texture_pack", "interpolation", "framing",
                                    "stage_backdrops", "fullscreen"}) {
                setting(key);
            }
            break;
        case SOUND:
            for (const char* key : {"volume_music", "volume_sfx", "volume_voices", "volume_system", "vibration"}) setting(key);
            break;
        case GAME:
            for (const char* key : {"gameplay_fixes", "game_texts", "memory_card"}) setting(key);
            break;
        case CONTROLS:
            setting("touch_controls");
            setting("touch_directions");
            setting("touch_size");
            setting("touch_opacity");
            setting("touch_haptics");
            if (touch) out.push_back(Entry{"EDIT LAYOUT", Kind::ACTION, "edit_layout", 0});
            if (keyboard) {
                out.push_back(Entry{"KEYBOARD KEYS", Kind::ACTION, "keyboard_keys", 0});
                out.push_back(Entry{"DEFAULT KEYS", Kind::ACTION, "default_keys", 0});
            }
            break;
        default:
            break;
    }
    out.push_back(Entry{"EXIT", Kind::BACK, "", 0});
    return out;
}

std::string SystemOptions::value(const std::string& key, const std::string& label) const {
    if (Settings == nullptr || view == nullptr) return "";
    std::string text = capitals(view->value_text(key));
    for (const auto& [k, names] : FLAG_VALUES) {
        if (key == k) text = Settings->flag(key) ? names.second : names.first;
    }
    // Beside the label with a space between, inside the value column.
    gd::Int room = (VALUE_COLUMN - static_cast<gd::Int>(label.size() + 1) * ADVANCE) / ADVANCE;
    if (room < 1) room = 1;
    if (static_cast<gd::Int>(text.size()) > room) text = text.substr(0, static_cast<size_t>(room));
    return text;
}

bool SystemOptions::is_default(const std::string& key) const {
    return Settings != nullptr && GameSettings::_equal(Settings->value(key), GameSettings::SCHEMA.at(key).def);
}

void SystemOptions::fill(OptionsScreen& o) const {
    o.system_title = TITLES[menu];
    o.system_values = menu != ROOT;
    o.system_rows.clear();
    for (const Entry& e : entries(menu)) {
        OptionsScreen::SystemRow row{e.label, "", false};
        if (e.kind == Kind::SETTING) {
            row.value = value(e.key, e.label);
            row.is_default = is_default(e.key);
        }
        o.system_rows.push_back(row);
    }
    o.system_cursor = cursors[menu];
}

// As the game's lists (OptionsScreen::_list_input): up/down with the pad's repeat, left/right
// change, Cross / Circle choose (OK's sound), Select goes back.
bool SystemOptions::step(GameFlow& g, OptionsScreen& o) {
    if (o.subs[OptionsScreen::PAGE_DISPLAY_ADJUST] == 0) {
        menu = ROOT;
        for (gd::Int i = 0; i < cursors.size(); ++i) cursors[i] = 0;
    }
    std::vector<Entry> list = entries(menu);
    gd::Int n = static_cast<gd::Int>(list.size());
    gd::Int rep = g.repeat(0) | g.repeat(1);
    gd::Int pressed = g.pressed_any();
    gd::Int step = ((rep >> 14) & 1) - ((rep >> 12) & 1);
    gd::Int lr = ((rep >> 13) & 1) - ((rep >> 15) & 1);
    gd::Int cur = wrap(gd::clampi(cursors[menu], 0, n - 1), step, n);
    if (step != 0) g.sound(OptionsScreen::SOUND_MOVE);
    cursors[menu] = cur;
    const Entry& e = list[static_cast<size_t>(cur)];
    bool left = false;
    if (e.kind == Kind::SETTING && lr != 0) {
        Settings->cycle(e.key, lr);
        g.sound(OptionsScreen::SOUND_MOVE);
    }
    bool back = (pressed & PadState::SELECT) != 0;
    if ((pressed & OptionsScreen::CHOOSE) != 0) {
        switch (e.kind) {
            case Kind::MENU:
                g.sound(OptionsScreen::SOUND_OK);
                menu = e.menu;
                cursors[menu] = 0;
                break;
            case Kind::ACTION:
                g.sound(OptionsScreen::SOUND_OK);
                if (e.key == "edit_layout" && edit_layout) edit_layout();
                if (e.key == "keyboard_keys" && keyboard_keys) keyboard_keys();
                if (e.key == "default_keys") Settings->reset_key_bindings();
                break;
            case Kind::BACK: back = true; break;
            default: break;
        }
    }
    if (back) {
        g.sound(OptionsScreen::SOUND_OK);
        if (menu == ROOT) {
            left = true;
        } else {
            menu = ROOT;
        }
    }
    if (left) {
        if (Settings != nullptr) Settings->save();
        if (closed) closed();
        return true;
    }
    fill(o);
    return false;
}

}  // namespace t3

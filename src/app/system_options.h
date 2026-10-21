#pragma once
// Native (not in the remake): SYSTEM OPTION, the remake's settings screen rebuilt as a page of the
// game's own OPTION MODE (OptionsScreen::system_option, in the original's DISPLAY ADJUST place). Its
// entries open lists in GAME OPTION's look: GRAPHICS, SOUND, GAME and CONTROLS, each setting with its
// value on the right (in the default's colour while it is the default). Up/down choose, left/right
// change a setting, Cross / Circle open an entry, Select or EXIT go back. The texts are the settings
// screen's, in capitals (the game's fonts have no small letters); the remake's interface language
// is left out (the game's fonts cannot draw Japanese).
#include <functional>
#include <string>
#include <vector>

#include "gd/gd.h"

namespace t3 {

class GameFlow;
class GameSettings;
class OptionsScreen;
class SettingsView;

class SystemOptions {
public:
    enum class Kind { MENU, SETTING, ACTION, BACK };
    struct Entry {
        std::string label;
        Kind kind = Kind::BACK;
        std::string key;                     // SETTING: the settings key; ACTION: its name; MENU: ""
        gd::Int menu = 0;                    // MENU: the list it opens
    };
    static constexpr gd::Int ROOT = 0, GRAPHICS = 1, SOUND = 2, GAME = 3, CONTROLS = 4;
    static constexpr gd::Int VALUE_COLUMN = 324;   // GAME OPTION's value_width: the values' right edge
    static constexpr gd::Int ADVANCE = 9;          // font 0's advance

    GameSettings* Settings = nullptr;
    SettingsView* view = nullptr;            // its value texts and translations
    bool touch = false;                      // the touch controls are shown (EDIT LAYOUT)
    bool keyboard = false;                   // a keyboard is at hand (its keys)
    bool other_input = true;                 // a pad or keyboard besides touch: TOUCH CONTROLS may be OFF
    std::function<void()> edit_layout;       // the touch controls' layout edit
    std::function<void()> keyboard_keys;     // the settings screen's key fields
    std::function<void()> closed;            // the page was left (the settings are saved)

    gd::Int menu = ROOT;
    gd::PI32 cursors{0, 0, 0, 0, 0};

    // One frame of the page (OptionsScreen::system_step): true when it is left.
    bool step(GameFlow& g, OptionsScreen& o);
    std::vector<Entry> entries(gd::Int list) const;
    // A setting's value as the page shows it: capitals, only what font 0 draws, cut to fit beside
    // its label.
    std::string value(const std::string& key, const std::string& label) const;
    bool is_default(const std::string& key) const;
    static std::string capitals(const std::string& text);
    // The rows the view draws for the list shown.
    void fill(OptionsScreen& o) const;
};

}  // namespace t3

#pragma once
// remake/presentation/screens/settings_view.gd
// The remake's settings screen (GameSettings): a vector panel over the main menu, opened with
// Select there (Esc, the keyboard's Select keys, or the gear on touch screens). It has two tabs,
// switched with L1 / R1, Tab or a click on them: General (the game and sound settings, the display
// ones beside them) and
// Controls (vibration, the touch controls and, where a keyboard can be at hand, the keys of the
// two keyboard layouts: a pad picture per player with a field beside each button). The
// directions move between the rows and fields by their places on the screen; on a setting left
// and right (or Cross and Start) change it, Circle or Select (Esc) close the screen. Cross, Enter
// or a click on a field waits for the field's new key (Esc cancels, a gamepad button too). A
// click or tap on a row changes it too (on a volume's bar it sets the volume there, and a drag
// along the bar follows the finger), one outside the panel closes it (the touch controls are
// hidden while it is open). Settings with a single choice in this build are not shown. The rows
// stand in two columns on landscape windows, in one on narrow ones. The interface strings are
// translated (app/translations).
//
// Native: `extends Control` is a class with `visible` and `size`, drawn by `draw(canvas, window)`.
// The signal `closed` is a list of std::function. Godot's input events arrive through `key`
// (InputEventKey: _input), `joy_button` (InputEventJoypadButton: _input) and `pointer`
// (InputEventMouseButton / ScreenTouch / MouseMotion / ScreenDrag as PointerEvent DOWN / MOVE / UP
// / CANCEL in window pixels: _gui_input). The autoloads `Settings` and `Pads` are pointers the app
// sets. `tr` reads the remake's app/translations.csv English column, built in (no
// TranslationServer natively yet).
//
// Native addition (not in the remake): the touch controls' layout edit, a row "Edit layout" in the
// touch controls' section (shown with the touch controls on), which closes the screen and emits
// `edit_layout` (the app calls TouchControls.enter_edit).
#include <functional>
#include <string>
#include <vector>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "platform/app.h"
#include "platform/pad_state.h"

namespace t3 {

class GameSettings;
class InputRouter;

class SettingsView {
public:
    // A row of ROWS: [key, help] (a fixed record).
    struct Row {
        std::string key;
        std::string help;
        bool operator==(const Row& o) const { return key == o.key && help == o.help; }
    };

    // signal closed
    std::vector<std::function<void()>> closed;
    // Native: the layout edit's row was picked (the screen has closed).
    std::vector<std::function<void()>> edit_layout;

    static inline const gd::Arr<Row> ROWS{
        {"SETTINGS_GAME", ""}, {"gameplay_fixes", "SETTINGS_FIXES_HELP"}, {"game_texts", "SETTINGS_TEXTS_HELP"},
        {"language", "SETTINGS_LANGUAGE_HELP"}, {"memory_card", "SETTINGS_CARD_HELP"},
        {"SETTINGS_SOUND", ""}, {"volume_music", ""}, {"volume_sfx", ""}, {"volume_voices", ""}, {"volume_system", ""},
        {"SETTINGS_DISPLAY", ""}, {"fullscreen", "SETTINGS_FULLSCREEN_HELP"},
        {"graphics", "SETTINGS_GRAPHICS_HELP"}, {"texture_filter", "SETTINGS_FILTER_HELP"},
        {"shading", "SETTINGS_SHADING_HELP"},
        {"texture_pack", "SETTINGS_PACK_HELP"}, {"interpolation", "SETTINGS_INTERPOLATION_HELP"},
        {"framing", "SETTINGS_FRAMING_HELP"}, {"wide_screens", "SETTINGS_WIDE_SCREENS_HELP"}, {"stage_backdrops", "SETTINGS_BACKDROPS_HELP"},
        // Native: no "touch_macros" (own buttons are made in the layout edit instead).
        {"vibration", ""}, {"touch_controls", "SETTINGS_TOUCH_HELP"},
        {"touch_size", "SETTINGS_TOUCH_SIZE_HELP"}, {"touch_layout", "SETTINGS_TOUCH_LAYOUT_HELP"},   // native: the layout edit
        {"default_keys", "SETTINGS_DEFAULT_KEYS_HELP"},
        {"back", ""},
    };
    static inline const gd::Arr<std::string> TABS{"SETTINGS_TAB_GAME", "SETTINGS_TAB_CONTROLS"};
    static constexpr gd::Int GAME = 0;
    static constexpr gd::Int CONTROLS = 1;
    // Each tab's first row; Back stands on both.
    static inline gd::PI32 tab_start{0, ROWS.find(Row{"vibration", ""})};
    // Each tab's first row of the second column.
    static inline gd::PI32 column_break{ROWS.find(Row{"SETTINGS_DISPLAY", ""}),
                                        ROWS.find(Row{"touch_size", "SETTINGS_TOUCH_SIZE_HELP"})};
    static inline gd::Int back_row = ROWS.size() - 1;
    static inline gd::Int default_keys_row = ROWS.find(Row{"default_keys", "SETTINGS_DEFAULT_KEYS_HELP"});
    // Native: the layout edit's row.
    static inline gd::Int touch_layout_row = ROWS.find(Row{"touch_layout", "SETTINGS_TOUCH_LAYOUT_HELP"});
    // The key fields beside a pad picture, top to bottom: its left side, then its right side.
    static inline const gd::Arr<gd::Int> FIELD_BUTTONS{
        PadState::L2, PadState::L1, PadState::UP, PadState::LEFT, PadState::RIGHT, PadState::DOWN, PadState::SELECT,
        PadState::R2, PadState::R1, PadState::TRIANGLE, PadState::SQUARE, PadState::CIRCLE, PadState::CROSS, PadState::START,
    };
    static constexpr gd::Int FIELDS_PER_SIDE = 7;
    static constexpr gd::Int LAYOUTS = 2;
    // The fields follow the rows in the focus order: layout x FIELD_BUTTONS.size() + button.
    static inline gd::Int first_field = ROWS.size();
    static inline gd::Int item_count = ROWS.size() + LAYOUTS * FIELD_BUTTONS.size();
    // The pad picture (the game's KEY CONFIGURATION one, screens/pad_0.png) and each button's place on it.
    static inline const gfx::Vec2 PAD_SIZE{144, 136};
    static inline const gd::Dict<gd::Int, gfx::Vec2> BUTTON_SPOTS = [] {
        gd::Dict<gd::Int, gfx::Vec2> d;
        d.set(PadState::L2, gfx::Vec2(30, 10)); d.set(PadState::L1, gfx::Vec2(30, 24));
        d.set(PadState::R2, gfx::Vec2(114, 10)); d.set(PadState::R1, gfx::Vec2(114, 24));
        d.set(PadState::UP, gfx::Vec2(30, 69)); d.set(PadState::LEFT, gfx::Vec2(20, 81));
        d.set(PadState::RIGHT, gfx::Vec2(39, 81)); d.set(PadState::DOWN, gfx::Vec2(30, 93));
        d.set(PadState::SELECT, gfx::Vec2(63, 92)); d.set(PadState::START, gfx::Vec2(81, 92));
        d.set(PadState::TRIANGLE, gfx::Vec2(114, 62)); d.set(PadState::SQUARE, gfx::Vec2(101, 82));
        d.set(PadState::CIRCLE, gfx::Vec2(126, 82));
        d.set(PadState::CROSS, gfx::Vec2(114, 99));
        return d;
    }();
    static inline const gd::Dict<gd::Int, std::string> SHOULDER_NAMES = [] {
        gd::Dict<gd::Int, std::string> d;
        d.set(PadState::L2, "L2"); d.set(PadState::L1, "L1"); d.set(PadState::R2, "R2"); d.set(PadState::R1, "R1");
        d.set(PadState::SELECT, "SELECT"); d.set(PadState::START, "START");
        return d;
    }();
    static constexpr float GLYPH_RADIUS = 1.45f;          // a face button's ButtonGlyphs radius per half of its arrow size
    static constexpr float LABEL_SHARE = 0.55f;           // of a row's width: the label, then the value
    static constexpr float BAR_SHARE = 0.72f;             // of the value's width: a volume's bar
    static constexpr float BAR_SLACK = 0.4f;              // of the row height: taps this far beside a bar still set it
    static inline const gfx::Color DIM{0, 0, 0, 0.72f};
    static inline const gfx::Color PANEL{0.07f, 0.08f, 0.12f, 0.94f};
    static inline const gfx::Color EDGE{0.55f, 0.6f, 0.75f, 0.6f};
    static inline const gfx::Color TEXT{0.88f, 0.9f, 0.95f};
    static inline const gfx::Color MUTED{0.6f, 0.63f, 0.72f};
    static inline const gfx::Color GROUP{1.0f, 0.78f, 0.35f};
    static inline const gfx::Color FOCUS{0.3f, 0.45f, 0.85f, 0.55f};
    static inline const gfx::Color TAB_ON{0.3f, 0.45f, 0.85f, 0.3f};
    static inline const gfx::Color BOX{1, 1, 1, 0.08f};
    static inline const gfx::Color LEADER{1, 1, 1, 0.3f};
    static inline const gfx::Color VALUE{1, 1, 1};
    static constexpr float ROW_SHARE = 0.05f;             // row height as a share of the window height
    static constexpr float MIN_ROW = 22.0f;
    static constexpr float COLUMN_ROWS = 14.0f;           // a column's width in row heights
    static constexpr float TWO_COLUMNS_ASPECT = 1.2f;     // the narrowest window (width / height) with two columns
    static constexpr float EXTRA_LINES = 5.0f;            // the title, the help lines and the keys line, in rows
    static constexpr float TAB_LINES = 1.0f;              // the tabs, in rows
    static constexpr float FIELD_PITCH = 0.95f;           // a key field's line, in rows
    static constexpr float PAD_LINES = 1.1f + FIELDS_PER_SIDE * FIELD_PITCH + 0.3f;   // a pad picture with its title and fields
    static constexpr float PICTURE_SHARE = 0.42f;         // the widest pad picture, of its panel's width
    static constexpr float GLYPH_ROWS = 1.2f;             // a field's button glyph, in rows
    static constexpr float SCREEN_MARGIN = 8.0f;
    static constexpr gd::Int REPEAT_DELAY = 16;           // frames before a held direction repeats, then every REPEAT_RATE
    static constexpr gd::Int REPEAT_RATE = 4;
    static constexpr gd::Int UP = PadState::UP;
    static constexpr gd::Int DOWN = PadState::DOWN;
    static constexpr gd::Int LEFT = PadState::LEFT;
    static constexpr gd::Int RIGHT = PadState::RIGHT;
    static constexpr gd::Int CONFIRM = PadState::CROSS | PadState::START;
    static constexpr gd::Int CANCEL = PadState::CIRCLE | PadState::SELECT;
    static constexpr gd::Int PREVIOUS_TAB = PadState::L1;
    static constexpr gd::Int NEXT_TAB = PadState::R1;

    gd::Int cursor = 1;
    gd::Int tab = GAME;
    gd::Int listening = -1;                 // the key field waiting for its new key
    bool keyboard = true;                   // the key fields are shown (a keyboard can be at hand)
    gfx::TextureRef pad_texture;            // the pad picture (none: a frame stands for it)
    gfx::FontRef font;
    gd::Int _held = 0;
    gd::Int _held_frames = 0;
    gd::Arr<gfx::Rect2> _rects;             // each row's and field's place; empty for those not shown
    gd::Arr<gfx::Rect2> _tab_rects;
    gd::Arr<gfx::Rect2> _pictures;          // the pad pictures (none on the Game tab or without a keyboard)
    gd::PI32 _column_of;                    // each row's column on the tab last arranged
    gd::PI32 _line_of;                      // and its line there (-1: not there)
    gd::Int _dragging = -1;                 // the volume row a finger or the mouse drags along
    gfx::Rect2 _panel;
    float _row = MIN_ROW;
    bool _suppress = true;                  // buttons held when the screen opened are ignored until released

    // Control: shown or not and the window's size; the autoloads.
    bool visible = false;
    gfx::Vec2 size;
    GameSettings* Settings = nullptr;
    InputRouter* Pads = nullptr;
    bool _shift = false;                    // native: InputEventKey.shift_pressed (Shift held)

    // _ready
    SettingsView();
    void open();
    void close();
    // One frame of pad input (both players' held words).
    void handle(gd::Int held);
    // _input, keys: a waiting field's new key, Tab between the tabs (true: set_input_as_handled).
    bool key(gd::Int key, bool down);
    // _input, a gamepad button: it stops the wait.
    bool joy_button(bool down);
    void _handled();
    void _stop_listening();
    void switch_tab(gd::Int step);
    // _gui_input (true: the event was the screen's; it takes every event while shown).
    bool pointer(PointerEvent kind, const Pointer& p);
    // A volume row's bar.
    gfx::Rect2 _bar_rect(gd::Int i) const;
    // Sets a volume from a place along its bar.
    void _set_level(gd::Int i, float x);
    static std::string _key(gd::Int i);
    static bool _is_group(gd::Int i);
    static bool is_field(gd::Int i);
    // A key field's keyboard layout (0: left, 1: right) and button.
    static gd::Int field_layout(gd::Int i);
    static gd::Int field_button(gd::Int i);
    // The key field of a layout's button.
    static gd::Int field_of(gd::Int layout, gd::Int bit);
    // The tab a row or field stands on (-1: Back, on both).
    static gd::Int tab_of(gd::Int i);
    // A row or field changed by left and right or picked by Cross: the settings that have a value.
    bool _is_setting(gd::Int i) const;
    bool _selectable(gd::Int i) const;
    // Whether a row or field is shown on its tab: groups, Back, volumes, the settings with a choice in
    // this build, and the keys where a keyboard can be at hand.
    bool shown(gd::Int i) const;
    bool _is_level(gd::Int i) const;
    gd::Int _first_item(gd::Int t) const;
    // The nearest selectable row or field from `from` in a direction (by their places on the screen);
    // up and down wrap around.
    gd::Int neighbour(gd::Int from, gfx::Vec2 direction) const;
    // The nearest in a direction: first among those facing `from` (across the line of sight from its
    // edge), else by distance with the sideways one counted too.
    gd::Int _nearest(gd::Int from, gfx::Vec2 origin, gfx::Vec2 direction) const;
    void _change(gd::Int i, gd::Int step);
    // The shown value of a setting.
    std::string value_text(const std::string& key) const;
    // Two columns on landscape windows, one on narrow ones.
    gd::Int columns() const;
    // Rows standing after the pad pictures on the Controls tab.
    bool _is_footer(gd::Int i, gd::Int t) const;
    // Each row's column and line on a tab (_column_of, _line_of); returns the lines the deeper column takes.
    gd::Int _arrange(gd::Int t, gd::Int count);
    // The rows after the pad pictures shown on a tab.
    gd::Arr<gd::Int> _footer(gd::Int t) const;
    // The lines a tab's rows, pad pictures and footer take.
    float _content_lines(gd::Int t, gd::Int count);
    // The panel, the row height and every row's, field's and tab's rectangle for the window's size
    // (the panel keeps its size on both tabs).
    void _layout();
    // A pad picture in its panel, centred between its two columns of key fields.
    void _place_pad(gd::Int layout, gfx::Rect2 panel);
    // A button's place on a layout's pad picture.
    gfx::Vec2 button_spot(gd::Int layout, gd::Int bit) const;
    // Control._draw at the window's size.
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw(gfx::Canvas& canvas);
    std::string _help(gd::Int i) const;
    void _draw_value(gfx::Canvas& canvas, const std::string& key, gd::Int i, gfx::Vec2 at, float width, float row, int text_size);
    // A layout's pad picture with its title, its key fields and their leader lines to the buttons.
    void _draw_pad(gfx::Canvas& canvas, gd::Int layout);
    // The largest text size up to `most` at which a text fits a width.
    int _fitted(const std::string& text, float width, int most) const;
    // A pad button's sign: the shoulder and Start / Select names, the direction arrows and the face
    // buttons' symbols drawn (the web builds' font has no PlayStation symbols).
    void _draw_glyph(gfx::Canvas& canvas, gd::Int bit, gfx::Rect2 r);

    // Native: Object.tr over the built-in English strings of app/translations.csv (the key itself
    // when it has none, as Godot does).
    static std::string tr(const std::string& key);
    // Native: ROWS.find([key, help]) by the key alone.
    static gd::Int find_row(const std::string& key);
    // Native: the layout edit's row is shown with the touch controls on.
    bool _touch_layout_shown() const;
};

}  // namespace t3

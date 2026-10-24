#pragma once
// remake/presentation/hud/touch_controls.gd
// The on-screen controls of touch screens (remake-plan.md#input, "Touch"): a stick on the left
// (it centres where the finger lands; eight directions past a dead zone), the four face buttons
// on the right in the pad's layout, the shoulder buttons in one row above them, L1 L2 over the
// stick and R2 R1 over the face buttons (in reach of the thumbs on tall screens too), Select and
// Start at the bottom in the middle, and the macro buttons of the settings in the top corners (two or four limbs at once,
// such as the throws 1+3 on the left and 2+4 on the right).
// Each finger works on its own; a finger on the buttons may slide from one to another. The
// result is one pad word (`bits`) that InputRouter reads as the touch device.
//
// While something plays that Start skips (a movie, the demonstration, a replay: `set_playback`)
// only Select and Start stay, in their places, so the rest does not cover the picture.
//
// The layout scales with the window's shorter side and the size setting (`scale_share`) and keeps
// inside the display's safe area.
//
// Native additions (not in the remake), marked "Layout edit" below: an edit mode in which every
// control and the stick can be moved (drag) and resized (a pinch with two fingers, or the corner
// handle with one finger or the mouse), with Done and Reset; the placements are saved as JSON
// (`touch_layout.json` in the user directory) and laid over the remake's layout, which stays the
// default. The settings screen's "Edit layout" row opens it (`enter_edit`). Every control shows its
// size in window pixels while editing (a diameter, or width x height), and the last one touched gets
// a row with − and + that change that size by one pixel, so controls can be given equal sizes.
// ADD opens a wizard that makes a button of the player's own (any face and shoulder buttons at
// once, with a name typed in), EDIT changes or deletes the selected one; they replace the remake's
// fixed macro buttons (the settings' "Touch macros") and are saved in the same file.
// Each window size (a foldable's folded and unfolded screens) keeps a layout of its own: a size
// without one takes the saved layout of the nearest aspect within ASPECT_MATCH, else the remake's.
//
// Godot's input events arrive through `pointer` (InputEventScreenTouch / ScreenDrag as
// PointerEvent DOWN / MOVE / UP / CANCEL in window pixels); the autoload `Pads` is the `Pads`
// pointer the app sets.
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "platform/app.h"

namespace t3 {

class GameFlow;
class InputRouter;

class TouchControls {
public:
    static constexpr float UNIT_SHARE = 0.085f;      // the layout unit as a share of the window's shorter side
    static constexpr float MIN_UNIT = 28.0f;
    static constexpr float DEAD_ZONE = 0.35f;        // of the unit
    static constexpr float STICK_RADIUS = 1.3f;      // units
    // The D-pad's arms (the user's drawing, 2026-10-22: reach 750 px, arm 500 wide, straight sides to
    // 380 from the middle, the point at 130, the outline about 70 thick), as shares of the reach.
    static constexpr float DPAD_ARM = 500.0f / 750.0f;
    static constexpr float DPAD_SHOULDER = 380.0f / 750.0f;
    static constexpr float DPAD_TIP = 130.0f / 750.0f;
    static constexpr float DPAD_LINE = 70.0f / 750.0f;
    static constexpr float DPAD_ROUND = 1.6f;        // the corners' outer radius, in outline widths
    static constexpr int DPAD_ARC = 6;               // pieces of each corner's arc
    static constexpr float DPAD_PRESS_REACH = 1.2f;  // native: a finger lands on the D-pad within this many arms
    static constexpr float DPAD_DIAGONAL = 0.2617994f;   // native: 15 degrees (radians) either side of a diagonal light two arms
    static constexpr float BUTTON_RADIUS = 0.78f;
    static constexpr float BUTTON_SPREAD = 1.35f;
    static constexpr float HIT_SLACK = 1.25f;        // a round button takes touches this far out (of its radius)
    static constexpr float RECT_SLACK = 0.2f;        // units a rectangular button takes touches around it
    // Native: a finger between two neighbouring round buttons holds both (pair_at): within this share
    // of the way from either centre's end of the segment between them (0.35: its middle 30 %), and
    // this far from the segment (of the radius). Neighbours: each one of the other's two nearest
    // (the diamond's opposite buttons are not), the finger on no third button.
    static constexpr float PAIR_BAND = 0.35f;
    static constexpr float PAIR_WIDTH = 0.6f;
    static inline const gfx::Vec2 SYSTEM_SIZE{1.9f, 0.75f};
    static inline const gfx::Vec2 SHOULDER_SIZE{1.5f, 0.8f};
    static constexpr float EDGE = 0.3f;              // units between the controls and the safe area's edges
    static constexpr float GAP = 0.3f;               // units between neighbouring rectangular buttons
    static constexpr float MACRO_RADIUS = 0.6f;
    static constexpr float ALPHA = 0.55f;
    static constexpr float PRESSED_ALPHA = 0.95f;
    static inline const gfx::Color OUTLINE{0.62f, 0.62f, 0.62f};   // a button's edge at rest (white while pressed)
    static constexpr float FILL = 0.15f;             // a button's dark backing at rest, and pressed
    static constexpr float PRESSED_FILL = 0.3f;
    static const gd::Arr<gd::Int> FACE;
    static const gd::Arr<gfx::Vec2> FACE_OFFSETS;
    // The macro sets of the settings: two throws, or also 1+2 and 3+4.
    static const gd::Dict<std::string, gd::Arr<gd::Int>> MACRO_SETS;
    static const gd::Dict<gd::Int, std::string> LIMB_NAMES;
    // The shoulder buttons from left to right: L1 L2 over the stick, R2 R1 over the face buttons.
    static const gd::Arr<gd::Arr<gd::Int>> SHOULDERS;
    static constexpr float STICK_HINT = 2.6f;        // units from the safe area's bottom left corner to the stick's rest
    static const gd::Arr<gd::Int> DIRECTIONS;

    // Godot's notifications that let every finger go (`_notification`).
    static constexpr gd::Int NOTIFICATION_WM_WINDOW_FOCUS_OUT = 1005;
    static constexpr gd::Int NOTIFICATION_APPLICATION_PAUSED = 2015;
    static constexpr gd::Int NOTIFICATION_APPLICATION_FOCUS_OUT = 2017;

    // A control on the screen: its centre, radius (circles) or rectangle, and the pad bits it holds.
    class Control2 {
    public:
        gd::Int bits = 0;
        gd::Int custom_id = 0;               // a player's own button (Layout edit's ADD); 0: the game's
        gfx::Vec2 centre;
        float radius = 0.0f;
        gfx::Rect2 rect;
        std::string label;
    };
    // A finger's record (the GDScript Dictionary {"stick": origin} or {"control": Control2}).
    struct Finger {
        bool stick = false;
        gfx::Vec2 origin;
        gd::Ref<Control2> control;
        gd::Ref<Control2> pair;              // native: the second face button, a finger between two
    };

    gd::Int bits = 0;                        // the pad word of the fingers down now
    // Native: a finger came onto a button (the app's buzz); the buttons' opacity at rest (the Touch
    // opacity setting; pressed 0.4 more).
    std::function<void()> on_press;
    float opacity = ALPHA;
    // Native: the left control as a fixed cross at the stick's rest (the Touch directions setting's
    // D-pad, the setting's default; off here, the remake's stick, until the app sets it): a finger
    // in its zone points from the cross's centre, not from where it landed as the stick's does.
    bool dpad = false;
    // The player's own buttons (native: made in the layout edit; the remake's macro sets were fixed),
    // placed by default where the remake's macros were: the top corners, alternating.
    // A button presses its steps' buttons: one step held while the finger is down, or several in
    // turn (a combo), each held HOLD steps, `gap` steps from one press to the next, played through
    // once a finger lands. RIGHT and LEFT stand for forward and back (turned for a fighter on the
    // right, as the game turns the pad: FighterInput's side flag).
    static constexpr gd::Int HOLD = 3;
    static constexpr gd::Int DEFAULT_GAP = 8, MIN_GAP = 4, MAX_GAP = 30;
    static constexpr gd::Int MAX_STEPS = 8;
    struct CustomButton {
        gd::Int id = 0;                      // its placement key: "Button <id>"
        std::vector<gd::Int> steps{0};
        gd::Int gap = DEFAULT_GAP;
        std::string label;
        gd::Int bits() const;                // every step's buttons
        bool combo() const { return steps.size() > 1; }
    };
    std::vector<CustomButton> custom;
    // The fighter of the touch controls (player 1) is on the right: forward is LEFT.
    std::function<bool()> facing_left;
    // A combo being played: the button and the steps since it started.
    struct Playing {
        gd::Int id = 0;
        gd::Int frame = 0;
    };
    std::optional<Playing> _playing;
    // Once a physics step (before the router reads the touch word): the combo goes on.
    void tick();
    // A button's buttons this step: its steps' (forward and back turned when facing left).
    gd::Int _turned(gd::Int pad_bits) const;
    // The combo's buttons at `frame` (0 past its end).
    static gd::Int combo_bits(const CustomButton& b, gd::Int frame);
    // A finger landed on (or slid onto) a control: the buzz, and an own combo starts.
    void _landed(const gd::Ref<Control2>& c);
    gd::Arr<gd::Ref<Control2>> _controls;
    gfx::Rect2 _stick_zone;
    gd::Dict<gd::Int, Finger> _fingers;      // touch index -> {"stick": origin} or {"control": Control2}
    gfx::Vec2 _stick_origin{INFINITY, INFINITY};
    gfx::Vec2 _stick_at;
    float _unit = MIN_UNIT;
    float scale_share = 1.0f;                // the size setting: 1 is the default size
    // The shoulder buttons shown (pad bits): those the game's key configuration gives an action.
    gd::Int shoulders;
    bool playback = false;                   // only Select and Start shown
    // The free place in the top row right of the left macros (its position and the row's height), for the
    // settings button of the main menu.
    gfx::Rect2 top_left_free;
    gfx::FontRef font;                       // ThemeDB.fallback_font (_ready)

    // Control: shown or not, the window's size, the display's safe area (insets in pixels).
    bool visible = false;
    gfx::Vec2 size;
    SafeArea safe;
    InputRouter* Pads = nullptr;             // the autoload `Pads`

    TouchControls();

    // _notification: every finger is let go when the app or window loses the focus or is paused.
    void _notification(gd::Int what);
    // The remake's macro sets as custom buttons (its tests; the game starts with none).
    void set_macros(const std::string& set_name);
    void set_playback(bool on);
    static gd::Int shoulders_shown(const GameFlow& flow);
    void set_shoulders(gd::Int bits_shown);
    void set_scale_share(float share);
    void layout();
    void _rebind_fingers();
    void _add_rect(gd::Int pad_bits, gfx::Rect2 rect, const std::string& label);
    gfx::Rect2 safe_area() const;
    // _input: a finger down, moved, lifted or cancelled; true when the controls took it.
    bool pointer(PointerEvent kind, const Pointer& p);
    bool press(gd::Int index, gfx::Vec2 at);
    void move(gd::Int index, gfx::Vec2 at);
    void release(gd::Int index);
    gd::Ref<Control2> control_at(gfx::Vec2 at) const;
    // Native: the two neighbouring round buttons a finger at `at` sits between (PAIR_BAND), else false.
    bool pair_at(gfx::Vec2 at, gd::Ref<Control2>& a, gd::Ref<Control2>& b) const;
    gd::Int stick_bits(gfx::Vec2 offset) const;
    void _update();
    void reset();
    // The window's size and safe area (Control resized): lays the controls out again.
    void set_window(gfx::Vec2 window, const SafeArea& area);
    // Control._draw at the window's size.
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw(gfx::Canvas& canvas);
    void _label(gfx::Canvas& canvas, const std::string& text, gfx::Vec2 at, float height, float alpha);

    // Layout edit ------------------------------------------------------------------------------
    static constexpr const char* LAYOUT_FILE = "touch_layout.json";
    static constexpr const char* STICK_KEY = "Stick";
    static constexpr float MIN_SCALE = 0.5f;      // a control's size in the edit, of its default size
    static constexpr float MAX_SCALE = 2.0f;
    static constexpr float HANDLE = 0.5f;         // units: the side of a control's corner handle

    // A control's placement: its centre as a share of the window and its size against the default.
    struct Placement {
        float x = 0.5f, y = 0.5f, scale = 1.0f;
    };
    using Placements = gd::Dict<std::string, Placement>;
    static constexpr float ASPECT_MATCH = 0.08f;  // a share of the aspect: a saved screen this close stands in
    Placements placements;                   // the window's: control key -> placement; missing: the remake's place
    std::map<std::string, Placements> screens;   // screen_key -> its placements (empty: the remake's layout)
    Placements shared;                       // a version 1 file's placements, for a screen without its own
    std::string screen;                      // the screen_key `placements` belong to
    std::string selected;                    // the control the − and + change (the last one touched)
    bool editing = false;
    std::string layout_path;                 // where the placements are saved; empty: not saved
    gfx::Vec2 _stick_hint;                   // the stick's resting place
    float _stick_scale = 1.0f;
    gfx::Rect2 _done_rect, _reset_rect;
    gfx::Rect2 _minus_rect, _plus_rect;      // the selected control's size, one pixel at a time
    gfx::Rect2 _new_rect, _change_rect;      // ADD (a new button), EDIT (the selected own button)

    // The wizard of an own button: the pad buttons it holds (toggled), its name, OK / CANCEL / DELETE.
    static constexpr gd::Int MAX_CUSTOM = 8;
    static constexpr gd::Int MAX_LABEL = 12;      // characters
    // The toggles: □ △ ✕ ○, L1 L2 R1 R2, then up, down, forward, back.
    static const gd::Arr<gd::Int> WIZARD_BUTTONS;
    struct Wizard {
        gd::Int id = 0;                      // the button changed; 0: a new one
        std::vector<gd::Int> steps{0};
        gd::Int step = 0;                    // the step the toggles show
        gd::Int gap = DEFAULT_GAP;
        std::string label;                   // empty: the steps' names ("1+3", "D+3+4", "1,1,2")
        bool asking = false;                 // the name's text input is open
        bool complete() const;               // every step presses something
    };
    std::optional<Wizard> wizard;
    struct WizardRects {
        gfx::Rect2 panel, name, ok, cancel, remove;
        gfx::Rect2 prev, next, add_step, remove_step, step_text;   // the combo's steps
        gfx::Rect2 gap_minus, gap_plus, gap_text;                  // their timing (with more than one)
        std::vector<gfx::Rect2> toggles;     // WIZARD_BUTTONS' order
    };
    // Asks the platform for a text (title, the text so far); false: no text input here.
    std::function<bool(const std::string&, const std::string&)> ask_text;
    // A finger of the edit: the control it holds, moving or resizing (the corner handle).
    struct EditFinger {
        std::string key;
        bool resize = false;
        gfx::Vec2 grab;                      // finger minus the control's centre when it landed
        gfx::Vec2 at;
        float start_scale = 1.0f;
        float start_reach = 1.0f;            // resize: the finger's distance from the centre when it landed
    };
    std::map<gd::Int, EditFinger> _edit_fingers;
    // Two fingers on one control: a pinch.
    struct Pinch {
        std::string key;
        gd::Int a = 0, b = 0;
        float span = 1.0f, scale = 1.0f;
        gfx::Vec2 mid, centre;
    };
    std::optional<Pinch> _pinch;

    // The key a control's placement is saved under: its button's name, or "Macro 1+3".
    static std::string control_key(const Control2& c);
    // A window size's key: "<long side>x<short side>" in pixels.
    static std::string screen_key(gfx::Vec2 window);
    // The placements of the window's size (`screen`), when the size changed or `force`.
    void _select_screen(bool force);
    // The window's placements kept as its screen's (after an edit).
    void _store_screen();
    // A control's size in pixels as the edit shows it: the diameter, or the width (and height).
    bool size_px(const std::string& key, gfx::Vec2& px) const;
    std::string size_text(const std::string& key) const;
    // − / +: the selected control's size by `pixels` (the diameter, or the width).
    void step_size(float pixels);
    // The wizard: open for a new button (id 0) or an own one; the answer of `ask_text`.
    void open_wizard(gd::Int id);
    void text_entered(const std::string& text);
    WizardRects wizard_rects() const;
    // The name an own button shows without a label: its steps ("1+3", "D+3+4", "1,1,2").
    static std::string default_label(gd::Int pad_bits);
    static std::string default_label(const std::vector<gd::Int>& steps);
    bool _wizard_pointer(gfx::Vec2 at);
    void _draw_wizard(gfx::Canvas& canvas);
    const CustomButton* custom_button(gd::Int id) const;
    // The setting's way in: the edit opens (every finger let go, the game gets nothing from touch).
    void enter_edit();
    // Done: the placements are saved and the edit closes.
    void finish_edit();
    // Reset: the remake's layout again (saved by Done).
    void reset_layout();
    // Reads `path` (the placements of an earlier edit) and keeps it as the file Done writes.
    void load_layout(const std::string& path);
    bool save_layout() const;
    std::string layout_json() const;
    bool parse_layout(const std::string& text);
    bool _edit_pointer(PointerEvent kind, const Pointer& p);
    // A placed control's centre, half extent and scale (false: no such control).
    bool _placed(const std::string& key, gfx::Vec2& centre, gfx::Vec2& half, float& scale) const;
    void _place(const std::string& key, gfx::Vec2 centre, float scale);
    // Alignment while a control is dragged: its centre within SNAP units of another control's centre
    // line (vertical or horizontal) goes onto it, and the line shows (_guide_x / _guide_y, NAN: none).
    static constexpr float SNAP = 0.3f;
    float _guide_x = NAN, _guide_y = NAN;
    gfx::Vec2 _snap(const std::string& key, gfx::Vec2 centre);
    void _apply_placements(const gfx::Rect2& area);
    gfx::Vec2 _clamp_centre(gfx::Vec2 centre, gfx::Vec2 half, const gfx::Rect2& area) const;
    std::string _edit_target(gfx::Vec2 at, bool& resize) const;
    void _draw_rect_button(gfx::Canvas& canvas, const gfx::Rect2& r, const std::string& label, bool down);
};

}  // namespace t3

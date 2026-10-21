#pragma once
// remake/core/game/options_screen.gd
// The options (title.ovl FUN_800DE480, game state 5; modes.md#options), written from the
// verified port `tools/research/menu_sim.py` and the decompiled overlay (its sounds). Pages: 0 OPTION
// MODE, 1 GAME OPTION, 2 RECORDS, 3 MEMORY CARD, 4 KEY CONFIGURATION, 5 DISPLAY ADJUST (hidden in
// the remake: its item is skipped), 6 leaves. MEMORY CARD saves and loads the save file through
// the owner's `save_writer` and `card_loader` with the memory card's results.
#include <functional>
#include <string>
#include <vector>

#include "gd/gd.h"
#include "gd/json.h"
#include "core/game/flow_part.h"
#include "platform/pad_state.h"

namespace t3 {

class GameFlow;

class OptionsScreen : public FlowPart {
public:
    static constexpr gd::Int PAGE_LEAVE = 6;
    static constexpr gd::Int PAGE_DISPLAY_ADJUST = 5;
    static constexpr gd::Int PAGE_RECORDS = 2;
    static constexpr gd::Int PAGE_CARD = 3;
    static constexpr gd::Int PAGE_KEYS = 4;
    static constexpr gd::Int SOUND_MOVE = 0x546C;
    static constexpr gd::Int SOUND_OK = 0x4CEB;
    static constexpr gd::Int SOUND_DONE = 0x4D87;
    static constexpr gd::Int SOUND_KEY_OPEN = 0x45CA;
    static constexpr gd::Int CHOOSE = PadState::CONFIRM;
    static constexpr gd::Int COLOUR_TOGGLE_HELD = 0x103;
    static constexpr gd::Int TIME_LIMIT = 359998;
    static inline const gd::Arr<std::string> HIDDEN_ITEMS{"DISPLAY ADJUST"};
    // Memory card results (the card calls' codes).
    static constexpr gd::Int CARD_DONE = 0;
    static constexpr gd::Int CARD_NO_CARD = 2;
    static constexpr gd::Int CARD_UNFORMATTED = 3;
    static constexpr gd::Int CARD_FULL = 4;
    static constexpr gd::Int CARD_NO_FILE = 5;
    static constexpr gd::Int KEY_ROWS = 8;
    static constexpr gd::Int KEY_ITEMS = 4;
    static constexpr gd::Int KEY_OPEN_FRAMES = 8;
    static constexpr gd::Int KEY_ACTIONS = 13;
    static constexpr gd::Int VIBRATION_PATTERN = 0xD;

    // A key configuration button row (0x38 bytes in the game).
    class KeyRow {
    public:
        gd::Int state = 0;                   // 0 closed, 1 opening, 2 open, 3 closing
        gd::Int button = 0;                  // the button index (0x80098308 + 8·player + button)
        gd::Int mask = 0;                    // the button's pad bit
        gd::Int open = 0;                    // frames open (0–8)
        gd::Int layer = 0;
        gd::PI32 layout;
        gd::Int colour = 0;
    };

    // A player's key configuration (0x1E8 bytes in the game).
    class KeyRecord {
    public:
        gd::Int player = 0;
        gd::Int selected = 0;                // 0 the buttons, 1 VIBRATION, 2 DEFAULT, 3 EXIT
        gd::Int armed = 0;                   // a button was pressed on this page
        gd::Int busy = 0;                    // a row is open or a setting changed this frame
        gd::Int pad_kind = 0;                // 9 analog
        gd::Int x = 0;
        gd::Int y = 0;
        gd::Int held = 0;
        gd::Int repeat = 0;
        gd::Int used = 0;                    // the actions in use (bit per action)
        gd::Arr<gd::Ref<KeyRow>> rows;
    };

    gd::Int page = 0;                        // 0x800EC5E8
    gd::PI32 cursors{0, 0, 0, 0, 0, 0};      // per page (+0)
    gd::PI32 subs{0, 0, 0, 0, 0, 0};         // per page (+4)
    gd::Int card_state = 0;                  // 0x800EC63C: 0 the prompt, else the result + 1
    gd::Arr<gd::Ref<KeyRecord>> keys;
    gd::Int record_page = 0;                 // 0x800ECA10
    gd::PI32 record_tops{0, 0, 0, 0};
    gd::Arr<gd::PI32> record_lists{gd::PI32(), gd::PI32(), gd::PI32(), gd::PI32()};
    gd::Int record_total = 0;                // 0x800ECA18: fights of every character
    gd::PI32 modes;                          // GAME OPTION's mode pictures shown (Tekken Ball once unlocked)
    // Writes the save file: returns a memory card result (0 done, 5 error).
    // Callable: takes GameProgress.save_data(); empty means not valid.
    std::function<gd::Int(const gd::PBytes&)> save_writer;

    // Native: SYSTEM OPTION, the app's own settings in the game's style, in the place of the
    // original's DISPLAY ADJUST (hidden in the remake). Off unless the app turns it on (never in a
    // trace replay, so the game's own flow stays as recorded). `system_step` runs its page each
    // frame (subs[PAGE_DISPLAY_ADJUST] is 0 on the first) and is true when it is left; it fills the
    // rows the view draws.
    static inline const std::string SYSTEM_LABEL = "SYSTEM OPTION";
    struct SystemRow {
        std::string label;
        std::string value;                   // empty: an entry, not a setting
        bool is_default = false;
    };
    bool system_option = false;
    std::function<bool()> system_step;
    std::string system_title;
    bool system_values = false;              // a list of settings (GAME OPTION's look), else of entries
    std::vector<SystemRow> system_rows;
    gd::Int system_cursor = 0;
    // An item's label as shown (SYSTEM OPTION for DISPLAY ADJUST when it is on).
    std::string item_label(gd::Int p, gd::Int i);

    // _init(flow): both players' key records with their rows.
    explicit OptionsScreen(GameFlow* flow);

    // The page's items: the JSON array option_pages[p]["items"] (INTERFACES.md: JSON-shaped data).
    gd::Json items(gd::Int p);
    // An item: a JSON object (label, variable, kind, count, action, value).
    gd::Json _item(gd::Int p, gd::Int i);
    bool item_hidden(gd::Int p, gd::Int i);
    // The value byte of an item (a progress byte, or the page byte).
    gd::Int item_value(gd::Int p, gd::Int i);
    void _set_item_value(gd::Int p, gd::Int i, gd::Int value);
    void step();
    // Select + Start is ignored while a memory card screen is up.
    bool _exit_allowed();
    // The mode pictures of GAME OPTION; Tekken Ball's only once it is unlocked.
    void _option_modes();

    // ---- list pages (FUN_800DCE20)
    static gd::Int _cycle(gd::Int v, gd::Int step, gd::Int n);
    void _list_input(gd::Int p);

    // ---- records (FUN_800E0E2C, FUN_800E10E8)
    void records_init();
    // The records: left/right the sub-page, up/down scroll; true when left (any face button or Select).
    bool records_step();

    // ---- memory card (FUN_800DDED0 and its screens)
    void _card_page();
    // `quiet`: untyped Array of card states (ints).
    bool _card_gate(const gd::Arr<gd::Int>& quiet);
    gd::Int _save();
    gd::Int _load();
    // FUN_800DD198: Start loads; any other button (with a message showing, also Start, up, down) leaves.
    bool _card_load_screen();
    // FUN_800DD544: Start saves.
    bool _card_save_screen();
    // FUN_800DD9E4: turning AUTO SAVE on saves first; 2 when that worked, 1 when left.
    gd::Int _card_auto_save_screen();

    // ---- key configuration (FUN_800DF9EC, FUN_800DF630)
    void key_config_init();
    // Both players' key configuration; true when left (Select, or EXIT with no row busy).
    bool key_config_step();
    // FUN_800DF14C: the eight button rows (the pad picture).
    void _key_setting(const gd::Ref<KeyRecord>& rec, bool selected);
    // FUN_800DEF98: holding a row's button opens its list over 8 frames; up/down picks the action.
    void _key_row_input(const gd::Ref<KeyRecord>& rec, const gd::Ref<KeyRow>& row);
    // FUN_800DF324: VIBRATION (left/right toggles it, with a short buzz).
    void _key_vibration(const gd::Ref<KeyRecord>& rec, bool selected);
};

}  // namespace t3

#pragma once
// remake/content/flow_data.gd
// The game flow's converted data (`imported/tables/flow.json`, tools/remake_import/flow.py):
// the initial image of the progress block, the default key tables, the main menu's entries and
// the tables of the screens.
#include <string>
#include <variant>

#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class TextLocale;

class FlowData {
public:
    class MenuEntry {
    public:
        std::string text;
        gd::Int mode = 0;
        gd::Int two_players = 0;
        gd::Int index = 0;
    };

    gd::PBytes progress;                     // GameProgress's image in the executable
    gd::Arr<gd::PI32> key_tables;            // per player the mapped word of each physical bit
    gd::PBytes quick_grid;                   // quick select cells: s16 x, s16 y, u8 key, pad
    gd::PBytes quick_prefs;                  // per player 0x34 bytes of the record's layout values
    gd::PBytes quick_state_anim;             // per quick select state: three animation bytes
    gd::PBytes quick_decided;                // per state: the two costume-clash flags
    gd::Arr<gd::Ref<MenuEntry>> menu_entries;
    gd::PI32 button_actions;                 // the pad word of each key configuration action
    gd::PI32 button_tail;                    // the fixed words of the other eight pad bits
    gd::PI32 unlock_schedule;                // ArcadeUnlocks' steps as (kind, value) pairs
    gd::PI32 demo_characters;                // the demonstration fight's character list
    gd::PI64 attributes;                     // per costume key: portrait (bits 0-4), facing (5), layout (6-7), colour (8-31)
    gd::PI32 vs_backgrounds;                 // the VS screen: background id per kind
    gd::PI32 vs_caption_colours;             // caption bar colour (RGB) per kind
    gd::PI32 vs_caption_widths;
    gd::PI32 vs_player_pos;                  // per side: portrait x, y, slide x, slide y
    gd::PI32 vs_player_names;                // per side: name x, y, slide x, slide y
    gd::PI32 vs_team_pos;
    gd::PI32 vs_team_names;
    gd::PI32 vs_team_icons;                  // per side: x, y, step, x with eight members
    std::string ranking_alphabet = "";       // the name entry's 40 symbols ("<" back, "=" end)
    gd::Arr<std::string> ranking_rejected;   // names replaced by the character's own
    gd::PI32 ranking_skipped;                // stages the ranking backdrop never shows
    std::string ranking_gon = "";            // the name that unlocks Gon
    gd::PI32 ending_movies;                  // per costume key (21 characters x 4) the ending movie
    gd::Arr<std::string> staff_titles;       // the staff roll's title block
    gd::Arr<std::string> staff_rows;         // the rows: a kind digit, then the text
    gd::PI32 staff_font_wide;                // glyph widths from '!' (headings)
    gd::PI32 staff_font_narrow;              // glyph widths from '!' (names)
    // JSON-shaped Dictionaries are kept as parsed (INTERFACES.md).
    gd::Arr<gd::Json> theater_movies;        // Tekken 3 movies: movie, alternative, picture, mask, rule, name, lines
    gd::Arr<gd::Json> theater_tracks;        // Tekken 3 sound list: track, name
    gd::Json theater_names;                  // "0"-"3": the names of shared ending slots by the clears
    gd::Json theater_texts;                  // the buttons and captions
    gd::Arr<gd::Arr<gd::PI32>> ogre_scripts; // the Ogre scene's events (frame, kind, a, b, c) by the human's side
    gd::PI32 vs_practice_pos;                // practice.ovl's VS screen: portraits, names, captions
    gd::PI32 vs_practice_names;
    gd::Json vs_practice_texts;
    gd::Arr<gd::PI32> practice_rows;         // per page (FREE, VS CPU, COMBO TRAINING) the item id of each row
    // per bank type: combos {steps: key-ring words, moves: move slots} (PracticeMode::_combos)
    gd::Arr<gd::Arr<gd::Dict<std::string, gd::PI32>>> practice_combos;
    gd::Json practice_texts;                 // the menus' titles and labels
    gd::Arr<gd::Json> option_pages;          // the options' pages: layout, title, items, the Select byte
    gd::PI32 option_modes;                   // GAME OPTION's mode pictures, in order
    gd::Arr<gd::Json> key_rows;              // key configuration: per row the button mask, index, layer, layout, colour
    gd::PI32 key_default;                    // the default action per button
    gd::Arr<std::string> key_names;          // the 13 actions
    gd::Arr<std::string> record_titles;      // the records sub-pages
    gd::PI32 select_grid_keys;               // select.ovl: the character of each of the 22 cells
    gd::PBytes select_player_layout;         // per player 0x28 bytes copied to the record at +0x54
    gd::PI32 select_state_anim;              // per state the two bytes kept at +0x08/+0x0C
    gd::PI32 select_decided;                 // per state the costume-clash flags
    gd::PBytes select_layouts;               // the one-row and two-row layouts (0x30 bytes each)
    gd::PBytes select_context;               // the screen context's initial image
    std::string vs_text = "";                // "VS"
    std::string vs_stage = "";               // the arcade and time attack caption: "STAGE %d"
    std::string vs_round = "";               // VS, team battle and survival: "FIGHT %d"
    std::string vs_demo = "";                // the demonstration: "STAGE 1"

    // The text fields by the game memory block they come from, as converted (Japanese release).
    static inline const gd::Dict<std::string, std::string> TEXT_FIELDS = [] {
        gd::Dict<std::string, std::string> d;
        d.set("theater_names", "ending"); d.set("theater_texts", "ending"); d.set("vs_practice_texts", "practice");
        d.set("practice_texts", "practice"); d.set("key_names", "title"); d.set("record_titles", "title");
        return d;
    }();

    // A text field's value: a JSON object (theater_names, ...) or a PackedStringArray (key_names, ...).
    using TextValue = std::variant<gd::Json, gd::Arr<std::string>>;
    gd::Dict<std::string, TextValue> _texts;   // the text fields as converted

    static gd::Ref<FlowData> load_from(const std::string& path);

    // The text fields in the locale's language (null: as converted): the strings through their
    // blocks, the staff roll the USA one when it was converted.
    void localize(const gd::Ref<TextLocale>& locale);
    // FUN_8004BA28: the attribute word of a character's costume (an out-of-range one: the empty key).
    gd::Int attribute(gd::Int character, gd::Int costume) const;

    static gd::PBytes _bytes(const gd::Json& value);
    static gd::PI32 _u16s(const gd::Json& value);

    // GDScript `get(field)` / `set(field, v)` over TEXT_FIELDS (reflection, explicit here).
    TextValue _get_text(const std::string& field) const;
    void _set_text(const std::string& field, const TextValue& v);
};

}  // namespace t3

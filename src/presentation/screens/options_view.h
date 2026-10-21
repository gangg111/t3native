#pragma once
// remake/presentation/screens/options_view.gd: the options (title.ovl FUN_800DE480's drawing,
// menu_sim.option_list_draw, records and key configuration): the options backdrop, the page title in
// font 1, items in font 0 (the cursor's item in colour 5 on a pulsing bar, the others 10; a value
// equal to its default in colour 1, else 6, right-aligned in the value column), the records tables,
// the memory card's messages and both players' key configuration.
#include <string>

#include "gd/json.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class GameFlow;
class OptionsScreen;

class OptionsView : public PsxCanvas {
public:
    // CARD_MESSAGES' and LOAD_MESSAGES' values: untyped Array records [text, colour].
    struct CardMessage {
        std::string text;
        gd::Int colour = 0;
    };

    static constexpr gd::Int BAR = 0xFF4020;                 // FUN_800DC838's cursor bar (a GPU colour word)
    static constexpr gd::Int PROMPT_COLOUR = 6;              // the card pages' PUSH START BUTTON (see _card_message)
    static constexpr gd::Int MODE_ICONS = 0x800EB45CLL;      // title.ovl: per mode (u32 mode, u32 uv|clut)
    static constexpr gd::Int MODE_ICON_GREY = 0x7E5E;
    static constexpr gd::Int RECORD_ROW_Y = 0x92;
    static constexpr gd::Int RECORD_ROW = 0x14;
    static inline const gd::Arr<std::string> ORDINALS{"ST", "ND", "RD", "TH"};
    static inline const gd::Dict<gd::Int, CardMessage> CARD_MESSAGES = [] {
        gd::Dict<gd::Int, CardMessage> d;
        d.set(1, CardMessage{"SAVE OK!", 1});
        d.set(6, CardMessage{"NO TEKKEN3 FILE!", 5});
        d.set(2, CardMessage{"SAVE ERROR!", 2});
        return d;
    }();
    static inline const gd::Dict<gd::Int, CardMessage> LOAD_MESSAGES = [] {
        gd::Dict<gd::Int, CardMessage> d;
        d.set(1, CardMessage{"LOAD OK!", 1});
        d.set(6, CardMessage{"NO TEKKEN3 FILE!", 5});
        d.set(2, CardMessage{"LOAD ERROR!", 2});
        return d;
    }();

    gfx::TextureRef picture_texture;
    gd::Arr<gfx::TextureRef> pads;
    gd::Ref<OptionsScreen> options;
    gd::Ref<GameFlow> flow;
    gd::Int frame_count = 0;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
               const std::string& screens_dir);
    // The options backdrop and the pad pictures, from the texture pack in use.
    void reload_pictures() override;
    void show_state(const gd::Ref<GameFlow>& game);
    gd::Int _title_colour();
    void _draw() override;
    // FUN_800DC838: the cursor bar behind an option (pulsing).
    void _glow(float x, float y, float w, bool pulse = true);
    void _list(gd::Int p);
    // FUN_800DC914: the pictures of the modes the item applies to (greyed without the mode's bit).
    void _mode_boxes(const gd::Json& item);
    void _records();
    // The records' rank: the USA release pads it to two digits.
    std::string _rank_format();
    void _name(gd::Int character, float y);
    void _card_message();
    // FUN_800DF630: both players' pad pictures, button actions, VIBRATION, DEFAULT and EXIT.
    void _keys();
    void _item(const std::string& label, bool selected, float x, float y);
    // Native: SYSTEM OPTION (OptionsScreen::system_rows).
    void _system();
};

}  // namespace t3

#pragma once
// remake/presentation/screens/practice_view.gd: PRACTICE MODE's drawing over the fight (practice_sim.py:
// pause_page, menu_backdrop, practice_hud): the pause menu's pages on their dark blue panel, the prompts,
// the attack data with the combo counters, the hit markers at their screen points, and the key display
// along the bottom.
#include <string>

#include "core/game/practice_mode.h"
#include "gfx3d/nodes.h"
#include "presentation/screens/fight_canvas.h"
#include "presentation/world_space.h"

namespace t3 {

class GameFlow;

class PracticeView : public FightCanvas {
public:
    using Vector3 = t3::Vector3;

    static constexpr gd::Int S = PracticeMode::BASE;
    static constexpr gd::Int X0 = 0x48;
    static constexpr gd::Int VALUE_COLUMNS = 0x19;
    static constexpr gd::Int STR_MODE_SELECT = 0x800B0C1CLL;
    static constexpr gd::Int STR_FREE = 0x800B0C30LL;
    static constexpr gd::Int STR_VS_CPU = 0x800B0C40LL;
    static constexpr gd::Int STR_COMBO = 0x800B0C50LL;
    static constexpr gd::Int STR_PLAYER_SELECT = 0x800B0C68LL;
    static constexpr gd::Int STR_RESET = 0x800B0C80LL;
    static constexpr gd::Int STR_VS_CPU_TITLE = 0x800B0D20LL;
    static constexpr gd::Int STR_COMMAND_LIST = 0x800B0C9CLL;
    static constexpr gd::Int STR_TRAINING_DUMMY = 0x800B0CACLL;
    static constexpr gd::Int STR_COUNTER = 0x800B0CC0LL;
    static constexpr gd::Int STR_ATTACK_DATA = 0x800B0CD0LL;
    static constexpr gd::Int STR_FREEZE = 0x800B0CDCLL;
    static constexpr gd::Int STR_REPLAY = 0x800B0CECLL;
    static constexpr gd::Int STR_KEY_DISPLAY = 0x800B0CFCLL;
    static constexpr gd::Int STR_RETURN = 0x800B0D08LL;
    static constexpr gd::Int STR_CPU_DIFFICULTY = 0x800B0D30LL;
    static constexpr gd::Int STR_CPU_LEVEL = 0x800B0D40LL;
    static constexpr gd::Int STR_COMBO_PLAYER = 0x800B0D4CLL;
    static constexpr gd::Int STR_COMBO_TYPE = 0x800B0D68LL;
    static constexpr gd::Int STR_NONE = 0x800B0D74LL;
    static constexpr gd::Int OFF_ON = 0x800B9050LL;
    static constexpr gd::Int OK_CANCEL = 0x800B9058LL;
    static constexpr gd::Int DUMMY_NAMES = 0x800B9060LL;
    static constexpr gd::Int REPLAY_NAMES = 0x800B9084LL;
    static constexpr gd::Int DIFFICULTY_NAMES = 0x800B90A4LL;
    static constexpr gd::Int LEVEL_NAMES = 0x800B90B0LL;
    static constexpr gd::Int COMBO_NAMES = 0x800B90E4LL;
    static constexpr gd::Int TECH_ROLL_LINES = 0x800B9160LL;
    static constexpr gd::Int HUD_FMT_NUMBER = 0x800B0E8CLL;
    static constexpr gd::Int HUD_FMT_COMBO = 0x800B0E98LL;
    static constexpr gd::Int HUD_FMT_DAMAGE = 0x800B0EA8LL;
    static constexpr gd::Int HUD_TOTAL = 0x800B0E18LL;
    static constexpr gd::Int HUD_DMG = 0x800B0E2CLL;
    static constexpr gd::Int HUD_COUNTER = 0x800B0E44LL;
    static constexpr gd::Int HUD_CLEAN = 0x800B0E54LL;
    static constexpr gd::Int HUD_REPLAY_SELECT = 0x800B0E00LL;
    static constexpr gd::Int HUD_PLAY_SELECT = 0x800B0D7CLL;
    static constexpr gd::Int HUD_PLAY_REC = 0x800B0D90LL;
    // Guide state -> [string address, colour] (an untyped two-int Array: gd::Arr<gd::Int>).
    static inline const gd::Dict<gd::Int, gd::Arr<gd::Int>> HUD_PROMPTS = [] {
        gd::Dict<gd::Int, gd::Arr<gd::Int>> d;
        d.set(PracticeMode::GuideState::RECORD_WAIT, gd::Arr<gd::Int>{0x800B0DACLL, 1});
        d.set(PracticeMode::GuideState::RECORDING, gd::Arr<gd::Int>{0x800B0DC8LL, 2});
        d.set(PracticeMode::GuideState::GUIDE_PLAY, gd::Arr<gd::Int>{0x800B0DE0LL, 5});
        d.set(PracticeMode::GuideState::GUIDE_MODE, gd::Arr<gd::Int>{0x800B0DECLL, 6});
        return d;
    }();
    static constexpr gd::Int MARKER_UV = 0x800B9154LL;
    static constexpr gd::Int ARROWS = 0x800B0EDCLL;
    static inline const gd::Dict<gd::Int, gd::Int> MARKER_KINDS = [] {
        gd::Dict<gd::Int, gd::Int> d;
        d.set(0x217, 1); d.set(0x31F, 1); d.set(0x51F, 1); d.set(0x10F, 2); d.set(0x607, 3); d.set(0x706, 3);
        d.set(0x800, 4);
        return d;
    }();
    static inline const gd::Dict<gd::Int, gd::Int> ARROW_INDEX = [] {
        gd::Dict<gd::Int, gd::Int> d;
        d.set(0x1000, 1); d.set(0x3000, 2); d.set(0x2000, 3); d.set(0x6000, 4); d.set(0x4000, 5); d.set(0xC000, 6);
        d.set(0x8000, 7); d.set(0x9000, 8);
        return d;
    }();

    GameFlow* flow = nullptr;
    gd::Ref<PracticeMode> practice;
    gd::Ref<Camera3D> camera;            // the fight's camera (CameraRig.camera): the hit markers' projection
    gd::Int frame_count = 0;
    gd::PI32 _rows;                      // per menu row its colour (the drawing's own table at S+0x8C)

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image);
    void show_state(GameFlow* game);
    gd::Int _b(gd::Int at);
    gd::Int _sb(gd::Int at);
    void _draw() override;

    // ---- the pause menu

    // FUN_800B6DA0: the dark blue panel in its light grey frame.
    void _backdrop();
    void _colours(gd::Int cursor_field);
    void _label(gd::Int row, gd::Int y, const std::string& label);
    void _value(gd::Int row, gd::Int y, gd::Int table, gd::Int index);
    // FUN_800B57AC: the page of the sub-mode, or MODE SELECT.
    void _page();
    void _head(gd::Int title, gd::Int cursor_field);
    // FUN_800B5978: the FREE page.
    void _page_free();
    // FUN_800B5ED4: the VS CPU page.
    void _page_vs_cpu();
    // FUN_800B6368: the COMBO TRAINING page.
    void _page_combo();
    gd::Int _combo_count();
    // ATTACK DATA, FREEZE SIGNAL, REPLAY SETTINGS (and KEY DISPLAY), then RETURN TO MODE SELECT.
    void _tail(gd::Int row, gd::Int y, bool key_display);
    // FUN_800B7704: TECH ROLL with an up or down arrow for its 'u' / 'd' marks.
    void _tech_roll(gd::Int which, gd::Int x, gd::Int y, gd::Int colour);
    // A line with an up or down arrow sprite in place of each 'u' / 'd'.
    void _arrow_line(const std::string& line, gd::Int x, gd::Int y, gd::Int colour);

    // ---- the practice HUD

    // FUN_800B6898: the prompts, the attack data, the hit markers and the key display.
    void _hud();
    void _attack_data();
    // FUN_800B6F58: the big `n COMBO` and `n DAMAGE` counters.
    void _combo_counter(gd::Int count, gd::Int damage);
    // FUN_800B75AC: the markers at the last four hit points (0 high, 1 mid, 2 low, 3 unblockable).
    void _markers();
    void _hit_marker(gd::Int x, gd::Int y, gd::Int kind);
    // FUN_800B7C84: the last 15 inputs (directions and new buttons), oldest first.
    void _key_display();
    static gd::Int _wrap(gd::Int i);
    // FUN_800B721C: the pressed face buttons' icon.
    void _key_button(gd::Int x, gd::Int y, gd::Int pad, gd::Int guide);
    // FUN_800B7354: a direction arrow (a flipped 19 x 31 quad).
    void _key_arrow(gd::Int x, gd::Int y, gd::Int pad, gd::Int colour);
};

}  // namespace t3

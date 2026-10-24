#pragma once
// remake/presentation/screens/result_view.gd: the result screens of result.ovl (draw_sim
// team_screen_draw, time_attack_screen_draw, survival_screen_draw): team battle's members, fight grid
// and joining lines, time attack's stage times over its picture, survival's beaten characters and
// rank, over the scrolling banner.
//
// The game draws the background slot last-linked first (the black tile, the backdrop, the banner
// band subtracted, the stripes, then the portraits and boxes) and the text slot in front; the
// blended parts are passes.
#include <string>

#include "core/game/mode_region.h"
#include "core/game/result_screens.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class GameFlow;

class ResultView : public PsxCanvas {
public:
    static constexpr gd::Int S = ResultScreens::BASE;
    static constexpr gd::Int MODE = ModeRegion::BASE + 0x50;
    static constexpr gd::Int LADDER = MODE + 0x90;
    static constexpr gd::Int TEAM_LABELS = 0x800B9378LL;
    static constexpr gd::Int TEAM_MESSAGES = 0x800B9428LL;
    static constexpr gd::Int FACE_UV = 0x800B9778LL;
    static constexpr gd::Int STR_VS_ROW = 0x800B9494LL;
    static constexpr gd::Int STR_STAGE_1X = 0x800B951CLL;
    static constexpr gd::Int STR_STAGE_2D = 0x800B9530LL;
    static constexpr gd::Int STR_TICKS = 0x800B9544LL;
    static constexpr gd::Int STR_TIME = 0x800B9554LL;
    static constexpr gd::Int STR_YOUR_TIME = 0x800B959CLL;
    static constexpr gd::Int STR_TOTAL_TICKS = 0x800B9570LL;
    static constexpr gd::Int STR_TOTAL_TIME = 0x800B9580LL;
    static constexpr gd::Int STR_NEW_RECORD = 0x800B95B0LL;
    static constexpr gd::Int STR_WIN = 0x800B968CLL;
    static constexpr gd::Int STR_WINS = 0x800B9698LL;
    static constexpr gd::Int STR_YOU_ARE = 0x800B96C0LL;
    static constexpr gd::Int STR_GREATEST = 0x800B96D0LL;
    static constexpr gd::Int STR_PRACTICE = 0x800B96E4LL;
    static constexpr gd::Int ORDINALS = 0x800F29B8LL;
    static constexpr gd::Int SURVIVOR_COLOURS = 0x800B96FCLL;
    static constexpr gd::Int BANNER_TILES = 0x800B9838LL;
    static constexpr gd::Int BANNER = 0x800FAC0CLL;
    static constexpr gd::Int STR_TEAM_TITLE = 0x800B94A8LL;
    static constexpr gd::Int STR_TA_TITLE = 0x800B95C4LL;
    static constexpr gd::Int STR_SURV_TITLE = 0x800B9710LL;
    static constexpr gd::Int STR_TOTAL = 0x800B9700LL;
    static constexpr gd::Int TA_ROW_POS = 0x800B94F4LL;
    static constexpr gd::Int SURVIVOR_POS = 0x800B960CLL;
    static constexpr gd::Int TIME_CAP = 359'999;
    static constexpr gd::Int TEAM_BACKDROP = 0x1830F0;
    static constexpr gd::Int SURVIVAL_BACKDROP = 0xA0C000;
    static constexpr gd::Int TIME_ATTACK_CLEAR = 0x2030A0;
    static inline const gd::Arr<std::string> FORCE_KEYS{"COPPER", "SILVER", "GOLD"};
    static inline const gd::Arr<gd::Int> FORCE_KEY_COLOURS{2, 6, 5};
    static inline const gd::Arr<gd::Int> FORCE_KEY_AFTER{0x59, 0x77, 0x95};
    static inline const gd::Arr<gd::Int> FORCE_BOSS_AFTER{0, 0x13, 0x27, 0x3B};

    gd::Ref<GameFlow> flow;
    gd::Int frame_count = 0;
    gd::Int _screen = 0;                     // the game state shown (12 team, 13 time attack, 14 survival)
    gd::Ref<VramImage> _system;
    gd::Ref<VramImage> _time_attack;
    gd::Ref<VramImage> _force;
    gd::Arr<gfx::TextureRef> _portraits;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
               const std::string& screens_dir);
    // The portraits, from the texture pack in use.
    void reload_pictures() override;
    void show_state(const gd::Ref<GameFlow>& game);
    bool _force_result();
    bool _team();
    void _draw() override;
    gd::Int _rows_shift();
    // The subtracted parts: the banner's band, time attack's row shading.
    void _subtracted();
    // FUN_800F18FC's stripes, then the members and the fight grid's boxes.
    void _middle();
    void _stripes();
    // FUN_800EE100's colour bars (additive).
    void _added();
    gd::Int _label(gd::Int k);
    // The text slot: drawn last-linked first.
    void _front();

    // ---- team battle
    // FUN_800EE3C0: YOU WIN! / YOU LOSE! / DRAW GAME / PLAYER-n WINS!, sliding in.
    void _team_message();
    // FUN_800EE694: a team's eight member slots (portrait, greyed when defeated, or an empty box).
    void _team_members(gd::Int names, gd::Int lost, gd::Int x, gd::Int y, bool second);
    // FUN_800F131C: a 16 x 29 face, with flag 0x40 the lost mark over it.
    void _small_face(gd::Int x, gd::Int y, gd::Int key, gd::Int flags);
    void _small_face_picture(gd::Int x, gd::Int y, gd::Int key, gd::Int flags);
    gd::Int _fights();
    // FUN_800EE870: one box per fight (both faces, result colour), empty boxes after.
    void _team_grid();
    void _grid_box(gd::Int xo, gd::Int y, gd::Int col);
    void _team_grid_texts();
    // FUN_800EF0D8: the lines joining the members of each fight (the current one grows).
    void _team_lines();
    // A three-pixel-wide slanted band (POLY_F4 from (x1, y1) to (x2, y2)).
    void _line4(gd::Int x1, gd::Int y1, gd::Int x2, gd::Int y2, gd::Int col);

    // ---- time attack
    gd::Int _stage_time(gd::Int k, gd::Int last);
    // The stage rows (the one being added grows): `shading` draws their subtracted gradients.
    void _time_rows(bool shading);
    // FUN_800EFA9C: STAGE n, the stage time and the opponent's face.
    void _stage_row(gd::Int stage, gd::Int frames, gd::Int key, gd::Int flags, bool shading);
    // FUN_800EFD14: YOUR TIME, the total and the player's face; NEW RECORD blinks.
    void _your_time();

    // ---- survival
    // FUN_800F0858: each beaten character's portrait and win count.
    void _survivors();
    // FUN_800F04C4: the total and WIN / WINS.
    void _survival_wins(gd::Int wins, gd::Int x, gd::Int y);
    // FUN_800F068C: YOU ARE THE nth / GREATEST SURVIVOR!, or YOU NEED MORE PRACTICE!
    void _survival_rank(gd::Int rank, gd::Int y);

    // ---- Tekken Force
    // FUN_800F1C00 at (0x10, 100): the player's big picture (mirrored when it faces left) under a
    // gradient from its tint, framed.
    void _force_portrait();
    // FUN_800F1F08's texts, each appearing by the frames shown (with the sounds the step plays).
    void _force_texts();
};

}  // namespace t3

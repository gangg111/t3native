#pragma once
// remake/core/game/vs_screen.gd
// The pre-fight VS screen (FUN_80052808, game state 11; modes.md#pre-fight-vs-screen): the
// portraits, names and caption of the next fight slide in over 8 frames, stay for one more, and
// the game goes on to FightMain's loading (the state saved in 0x800AFF64/65).
//
// Sub-state 1 builds the screen objects (0x800B9380, the modes' overlay set-ups, VsModeSetups in
// draw_sim.py) and keeps the loaded pictures' ids in the progress block (0x800984E4–E7).
//
// Tekken Ball and Tekken Force read their layouts and captions from their overlays (mode_ram).
#include <string>

#include "gd/gd.h"
#include "core/game/flow_part.h"

namespace t3 {

class GameFlow;
class GameRam;

class VsScreen : public FlowPart {
public:
    static constexpr gd::Int SLIDE_FRAMES = 8;
    static constexpr gd::Int MAX_OBJECTS = 64;
    static constexpr gd::Int KEY_NONE = 0x58;
    static constexpr gd::Int NO_PICTURE = 0x26;             // FUN_80052794: a side without a big picture
    static constexpr gd::Int NAME_WIDTH = 0x15;             // pixels per name character
    static constexpr gd::Int NAME_LEFT = 0xC;
    static constexpr gd::Int NAME_RIGHT = 0x164;
    static constexpr gd::Int SCREEN_WIDTH = 0x170;
    static inline const gd::Arr<gd::Int> VS_TEXT{0xA2, 0xFA};        // "VS": colour 0, font 2 (x, y)
    static inline const gd::Arr<gd::Int> CAPTION_AT{0x1C, 0x2A};     // the caption line: colour 6, font 1 (x, y)
    static constexpr gd::Int ICON_FLAGS = 8;
    static constexpr gd::Int ICON_BEATEN = 0x68;
    static constexpr gd::Int TEAM_STRIDE = 0xD;
    static constexpr gd::Int TEAM_AT = 0x59;                // mode context: the two teams (keys, current, count, size)
    static constexpr gd::Int PROGRESS_BACKGROUND = 0x214;   // 0x800984E4: the loaded background id
    static constexpr gd::Int PROGRESS_PICTURES = 0x216;     // 0x800984E6: per side, the loaded big picture
    static inline const std::string TEXT_CODES = "%c%f%H%V";   // the text engine's colour, font and position prefix
    // volley.ovl FUN_800B55B4
    static constexpr gd::Int BALL_POS = 0x800B6908LL;         // per side 4 x s16: portrait, then name
    static constexpr gd::Int BALL_NAMES = 0x800B6918LL;
    static constexpr gd::Int BALL_VS = 0x800B0AA4LL;
    static constexpr gd::Int BALL_CAPTION = 0x800B0AB0LL;     // "TEKKEN BALL"
    static constexpr gd::Int BALL_CAPTION_KIND = 5;
    // force.ovl FUN_800B6648
    static constexpr gd::Int FORCE_POS = 0x800B6A24LL;        // the player's portrait
    static constexpr gd::Int FORCE_NAME = 0x800B6A2CLL;       // the player's name
    static constexpr gd::Int FORCE_LEVEL_NAME = 0x800B6A34LL; // the level's name lines (x right edge, y, slide)
    static constexpr gd::Int FORCE_LEVEL_NAMES = 0x800B6A3CLL;  // per level two string pointers
    static constexpr gd::Int FORCE_LEVEL_COLOURS = 0x800B6A64LL;
    static constexpr gd::Int FORCE_CAPTION = 0x800B0FA0LL;    // "TEKKEN FORCE"
    static constexpr gd::Int FORCE_STAGE = 0x800B0FB8LL;      // "STAGE %d"
    static constexpr gd::Int FORCE_FINAL = 0x800B0FCCLL;      // "FINAL STAGE"
    static constexpr gd::Int FORCE_FINAL_LEVEL = 4;
    static constexpr gd::Int FORCE_SIDE = 0x3E;             // mode context: the player's side
    static constexpr gd::Int FORCE_CAPTION_KIND = 6;
    static inline const gd::Arr<gd::Int> STAGE_AT{0x20, 0x54};
    static constexpr gd::Int LEVEL_NAME_STEP = 0x2E;

    enum Kind : gd::Int { NONE, BACKGROUND, TEAM_BARS, CAPTION_BAR, PORTRAIT, ICON, FRAME, TEXT, NAME };

    // A screen object (0x28 bytes at 0x800B9380): where it rests, where it slides in from, and what it draws.
    class VsObject {
    public:
        gd::Int kind = Kind::NONE;
        gd::Int x = 0;
        gd::Int y = 0;
        gd::Int dx = 0;                      // the slide offset at the start (type 1 frame: width)
        gd::Int dy = 0;                      // (type 6 frame: height)
        gd::Int colour = 0;                  // bar and portrait tint (RGB); text colour index
        gd::Int font = 0;
        std::string text = "";               // texts: the format without the HUD codes
        gd::Int arg = 0;                     // texts: the number printed; icons: the costume key
        gd::Int flags = 0;                   // icons: 8, or 0x68 dimmed
        gd::Int facing = 0;                  // portraits: 1 mirrored
        gd::Int picture = 0;                 // portraits: the side's big picture (attribute & 0x1F)

        // The position this frame (FUN_80052538: the slide scaled by the frames left).
        gd::Vector2i at(gd::Int slide);
    };

    gd::Int slide = 0;                       // 0x800B937C: frames left of the slide-in
    gd::Int background = 0;                  // 0x800B9378: the background id of this screen
    gd::PI32 pictures{0, 0};                 // 0x800B937A: per side, the big picture
    gd::Arr<gd::Ref<VsObject>> objects;
    bool shown = false;                      // the screen is drawn this frame (sub-states 2 and 3)

    // FlowPart's _init(flow): VsScreen has no _init of its own.
    explicit VsScreen(GameFlow* flow);

    void step();
    // Sub-state 1: the objects of the mode, then the pictures to load (kept in the progress block).
    void _setup();
    // VsModeSetups: arcade.ovl 0x800B3C70 (arcade, time attack), 0x800B3D70 (VS), 0x800B3E64 (team),
    // 0x800B3F98 (survival), 0x800B408C (demonstration).
    void _mode_setup(const gd::PI32& keys, gd::Int kind);
    // volley.ovl FUN_800B55B4: both sides as in versus, captioned TEKKEN BALL.
    void _ball_setup(const gd::PI32& keys, gd::Int kind);
    // force.ovl FUN_800B6648: the player's portrait and name, TEKKEN FORCE, STAGE n / FINAL STAGE,
    // and the level's name sliding in on the right; the other side has no big picture.
    void _force_setup(const gd::PI32& keys);
    // Four s16 of an overlay table: x, y and the slide offsets.
    static gd::PI32 _ram_pos(const gd::Ref<GameRam>& ram, gd::Int address);
    static std::string _ram_text(const gd::Ref<GameRam>& ram, gd::Int address);
    // FUN_80051C64: a new object (null when all 64 are used).
    gd::Ref<VsObject> _alloc(gd::Int kind);
    void _place(const gd::Ref<VsObject>& o, const gd::PI32& pos);
    // FUN_800523FC: a side's big picture, facing the other side.
    void _add_portrait(gd::Int side, gd::Int key, const gd::PI32& pos);
    // FUN_80052650: a side's name (colour 8, font 3), kept on screen; a negative x right-aligns.
    void _add_name(gd::Int key, const gd::PI32& pos);
    // FUN_800525DC: a text shown at rest.
    void _add_text(const std::string& text, gd::Int colour, gd::Int font, gd::Int x, gd::Int y, gd::Int arg);
    void _add_caption(const std::string& text, gd::Int arg);
    // FUN_80051FC4: the caption bar and the tiled background.
    void _add_background(gd::Int kind, gd::Int caption);
    // arcade.ovl FUN_800B3928: a team's members but the one fighting (the beaten ones dimmed, the
    // ones beyond the team's count locked), and a frame around them.
    void _team_icons(gd::Int side);
};

}  // namespace t3

#pragma once
// remake/presentation/screens/select_view.gd: the character select screen (select.ovl's drawing,
// select_sim.select_draw), on the screen's own pictures (VRAM after select.ovl's upload). The game
// links it all into the background slot, so it draws in the reverse order of the calls: the scrolling
// tiles over the title pictures, the coloured backlights (added), the layout's pictures over their
// gradient, the grid of faces, the name plates, the time limit, the NEW arrows, the big portraits and
// the rising sparks; the cursors are in the slots above.
//
// The portraits' bands (FUN_8010F38C): at full brightness drawn as they are; below it (and always
// at the top and bottom, which fade over their height) the band's shape is first subtracted
// through CLUT 0x7FD0 (all white but 0) and the picture added, both scaled, so it fades in over
// the backlight. Portrait attribute 0x15 is only ever subtracted.
#include <string>

#include "core/game/character_select.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class GameFlow;

class SelectView : public PsxCanvas {
public:
    static constexpr gd::Int CTX = CharacterSelect::BASE;
    static constexpr gd::Int SMALL_FACES = 0x800B93C8LL;      // one-row layout: per character u32 uv|clut, u16 tpage
    static constexpr gd::Int BG_ROWS = 0x80118BDCLL;
    static constexpr gd::Int BG_TILES = 0x80118C1CLL;
    static constexpr gd::Int PORTRAIT_BANDS = 0x80118B9CLL;   // per strip layout (attribute >> 6): three band heights
    static constexpr gd::Int BAND_SHADES = 0x80118BCCLL;      // four brightness weights (x/32) at the band edges
    static inline const gd::Arr<gd::Int> IMAGE_LISTS{0x800B9418LL, 0x800B9528LL};
    static constexpr gd::Int PORTRAIT_W = 0x7E;
    static constexpr gd::Int FULL = 0x100;
    static constexpr gd::Int SUBTRACTED_PORTRAIT = 0x15;    // the attribute byte of the portrait drawn subtracted
    // How `_portraits` draws: the bands at full brightness, the fading bands' shapes subtracted, the
    // fading bands added, and the subtracted portrait.
    enum Bands : gd::Int { OPAQUE, SHAPES, FADING, SUBTRACTED };

    gd::Ref<CharacterSelect> select;
    gd::Arr<gfx::TextureRef> portraits;
    gd::Int frame_count = 0;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
               const std::string& screens_dir);
    // The portraits, from the texture pack in use.
    void reload_pictures() override;
    void show_state(const gd::Ref<GameFlow>& game);
    bool _shown();
    gd::Int rec(gd::Int p);
    void _draw() override;
    // FUN_801102A0: the black top, the title pictures, and over them the diagonally scrolling tiles
    // (clipped above the grid).
    void _background();
    gfx::Color _attr_colour(gd::Int p);
    // FUN_8010FBFC: the glow behind each portrait, in the character's colour (added).
    void _backlights();
    // The layout's pictures, the grid of faces, the name plates, the time limit, the NEW arrows and
    // the portraits' fully bright bands.
    void _middle();
    void _wide_floor();
    // The portraits' fading bands and the sparks (added).
    void _glowing();
    // FUN_80110154: the gradient under the grid, then the layout's static pictures over it.
    void _pictures();
    // FUN_8010F760: the big portraits in three bands, brightening in over 32 frames, mirrored to face
    // the middle, from the layout's first texel row (v 0; the CLUT moves on every 64 rows, which the
    // pictures carry). `kind`: which of the bands' draws (Bands) this pass makes.
    void _portraits(gd::Int kind);
    // A band edge's colour: FUN_8004E254 scales 0x7F7F7F by the brightness (/0x100), a texture
    // modulation of 0x80 = 1.
    gfx::Color _band_shade(gd::Int brightness, bool opaque);
    void _band(const gfx::TextureRef& tex, float x, float y, gd::Int h, gd::Int row, bool flip, const gfx::Color& top,
               const gfx::Color& bottom);
    // FUN_8010FD64: the grid of small faces (the two-row layout shows the HUD portraits).
    void _grid_faces();
    // FUN_8010FEA0: each side's cursor frame (pulsing while choosing) in two halves, each side's own
    // half in the higher slot, and its tag above.
    void _cursors();
    // FUN_8010EDFC: each side's name plate (and the start prompt of a side that can join).
    void _name_plates();
    // FUN_8010EC9C: blinking arrows at the grid's ends while a character is new.
    void _new_arrows();
    // FUN_8010F020: the rising streaks (added).
    void _sparks();
};

}  // namespace t3

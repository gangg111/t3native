#pragma once
// remake/presentation/screens/psx_canvas.gd: a screen drawn the game's way: sprites cut from the VRAM
// image by texture page, CLUT and texel rectangle (SPRT, FUN_8004DE84's pictures, the HUD portraits
// and timer digits), with the game's tables read from its memory (GameRam) by address, as the verified
// Python ports do.
#include <string>
#include <vector>

#include "content/game_ram.h"
#include "content/vram_image.h"
#include "presentation/screens/screen_canvas.h"

namespace t3 {

class PsxCanvas : public ScreenCanvas {
public:
    static constexpr gd::Int PORTRAIT_UV = 0x8002152CLL;   // per character: u32 uv|clut, u16 tpage
    static constexpr gd::Int PORTRAIT_FRAME_COLOURS = 0x80021664LL;
    static constexpr gd::Int PORTRAIT_FRAME_LINES = 0x800215ECLL;
    static constexpr gd::Int TIMER_UV = 0x80022154LL;
    static constexpr gd::Int COIN_TEXTS = 0x800223A0LL;
    static constexpr gd::Int FREE_TEXTS = 0x800223D8LL;
    // FUN_80029860's display rectangle (0x800AE6F8): the part of the 368 x 480 screen drawn to.
    static gfx::Rect2 DISPLAY() { return gfx::Rect2(0, 20, 368, 448); }
    // Native: the wide mode keeps the foreground in the display rectangle (ScreenMask's opening).
    gfx::Rect2 foreground_area() const override { return DISPLAY(); }
    static inline const gd::Arr<gd::Int> BACKDROPS{0x80022CF8LL, 0x80022D78LL};   // FUN_800551FC's gradient quads
    static constexpr gd::Int FRAME_COUNT_SHIFT = 4;

    gd::Ref<GameRam> ram;
    gd::Ref<VramImage> vram;
    std::string pictures_dir;   // the screens' directory the pictures come from

    // The pictures the view takes from TexturePacks (a texture pack or filter changed): none here.
    virtual void reload_pictures() {}
    void setup_psx(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image);
    // The pictures `<folder>/<n>.png` of the screens' directory, one per character.
    static gd::Arr<gfx::TextureRef> character_pictures(const std::string& screens_dir, const std::string& folder);
    // SPRT: w x h texels from the packet's uv|clut word, in the texture page `tpage`.
    void sprt(float x, float y, gd::Int w, gd::Int h, gd::Int uv_clut, gd::Int tpage, const gfx::Color& tint = gfx::Color::white());
    // FUN_8004DE84: a VRAM picture drawn at (sx, sy).
    void picture(float sx, float sy, gd::Int w, gd::Int h, gd::Int vx, gd::Int vy, gd::Int clut, gd::Int mode,
                 const gfx::Color& tint = gfx::Color::white());
    // FUN_8004BA68: a portrait's frame, ten lines in the three colours of set `flags & 3`, pulsing
    // with flag 0x10 (`frames`: the frame counter), clipped to a draw area when given.
    void portrait_frame(gd::Int x, gd::Int y, gd::Int flags, gd::Int frames, gfx::Rect2 clip = gfx::Rect2());
    // FUN_8004BBC0: a character's HUD portrait (flags 8: no frame, 0x20: dimmed, 0x40: the lock).
    void portrait(float x, float y, gd::Int key, gd::Int flags, gd::Int frames = 0);
    void _portrait_face(float x, float y, gd::Int key, gd::Int flags);
    // FUN_8004D15C with a format string of the game's memory.
    void ptext(gd::Int format_address, const std::vector<Arg>& args);
    // POLY_FT4 over an axis-aligned w x h rectangle: tw x th texels from (u, v) stretched over it.
    void ft4(float x, float y, float w, float h, gd::Int u, gd::Int v, gd::Int tw, gd::Int th, gd::Int clut, gd::Int tpage,
             const gfx::Color& tint = gfx::Color::white());
    // FUN_800551FC: the menus' gradient backdrop (kind 0: four opaque quads, 1: two blended).
    void backdrop(gd::Int kind, gd::Int colour);
    // FUN_8004E254: a colour word scaled by k / 256 per channel (saturating).
    static gd::Int scale_colour(gd::Int c, gd::Int k);
    // FUN_8004F1D0: the blinking coin prompt of a side that has not joined.
    void coin(gd::Int player, gd::Int x, gd::Int y, gd::Int colour, gd::Int frames);
    // FUN_8004E410: two 16 x 46 digits (-1: the infinity sign).
    void timer(gd::Int value, float x, float y, gd::Int clut);
};

}  // namespace t3

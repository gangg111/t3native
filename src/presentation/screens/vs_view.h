#pragma once
// remake/presentation/screens/vs_view.gd: the pre-fight VS screen (FUN_80052808's drawing loop,
// draw_sim.vs_draw): the tiled `makuma` background, the caption bar, the portraits (`face_b` pictures)
// with their frame and coloured glow, the names sliding in, team battle's member icons and bars, and
// the texts.
//
// The game links every object into the same ordering-table slot, so the last object is drawn
// first (the background) and the first ones on top; the texts are in the front slot. The glow and
// the caption bar's tail blend with the background: they are passes in their blend modes.
#include <string>

#include "core/game/vs_screen.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class GameFlow;

class VsView : public PsxCanvas {
public:
    static constexpr gd::Int COL_EDGES = 0x800228C6LL;
    static constexpr gd::Int ROW_EDGES = 0x800228D8LL;
    static constexpr gd::Int FRAME_RECTS = 0x8002292CLL;
    static constexpr gd::Int BACKGROUND_BASE = 10;          // logical id of makuma00.tia
    static constexpr gd::Int PORTRAIT_W = 0x7E;
    static constexpr gd::Int PORTRAIT_H = 0xD4;
    static constexpr gd::Int PORTRAIT_TOP = 0x14;
    static constexpr gd::Int FRAME_COLOUR = 0xE8E8E8;

    gd::Ref<VsScreen> vs;
    gd::Arr<gd::Ref<VramImage>> backgrounds;
    gd::Arr<gfx::TextureRef> portraits;
    gd::Int frame_count = 0;
    gd::Ref<VramImage> _system;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
               const std::string& screens_dir);
    // The portraits, from the texture pack in use.
    void reload_pictures() override;
    void show_state(const gd::Ref<GameFlow>& game);
    gd::Arr<gd::Ref<VsScreen::VsObject>> _objects(gd::Int kind);
    void _draw() override;
    // FUN_80051C9C: the 7 x 6 grid of background cells (a black tile while sliding).
    void _background();
    // FUN_80051E74: the caption bar's gradient tail (drawn at rest).
    void _caption_tail();
    // The additive glows: team battle's bars (arcade.ovl FUN_800B3708) and each portrait's tint.
    void _glows();
    // The portraits, icons, frames and texts, the last objects first.
    void _front();
    // FUN_800520A0: a 126 x 212 portrait, mirrored to face the middle, in its frame.
    void _portrait(const gd::Ref<VsScreen::VsObject>& o, gd::Vector2i at);
};

}  // namespace t3

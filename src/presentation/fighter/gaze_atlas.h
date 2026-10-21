#pragma once
// remake/presentation/fighter/gaze_atlas.gd
// Gon's eyes' look direction on his atlas (tools/remake_import/gaze.py, 3dmk-models.md): the
// game shifts the texture strips of his eyes in VRAM; the converter writes the atlas rectangles
// any shift changes, for each shift, into one sheet, and this class pastes them over the atlas
// (the one with his head palette as loaded or swapped) into a texture of its own, for the two
// eyes' current shifts. A pack's replacement of the atlas may be larger: the patches are scaled
// to it (DuckStation's packs do not know the shifted pictures either).
//
// gfx::Texture keeps no CPU copy (Godot's Texture2D.get_image()), so the constructor takes the
// pictures of the three textures (FighterView loads them from the same paths).
#include "gd/gd.h"
#include "gd/json.h"
#include "gfx/image.h"
#include "gfx/texture.h"

namespace t3 {

class GazeAtlas {
public:
    static constexpr gd::Int NO_SHIFT = 99;        // an eye whose strip was never copied shows the atlas as it is
    static constexpr gd::Int SHEET_WIDTH = 1024;   // the converter's sheet (gaze.SHEET_WIDTH)

    gfx::TextureRef texture;                       // the atlas with the patches, updated in place
    gd::Vector2i _size;                            // the atlas's size at the converter's scale
    gd::Json _patches;                             // the JSON object as read
    gd::Arr<gd::Ref<gfx::Image>> _bases;           // the atlas with the head palette as loaded, swapped
    gd::Ref<gfx::Image> _sheet;
    double _sheet_scale = 1.0;
    double _scale = 1.0;
    gd::Ref<gfx::Image> _work;
    bool _swapped = false;
    gd::PI32 _shifts{NO_SHIFT, NO_SHIFT};

    GazeAtlas(const gd::Json& data, const gfx::Image& open, const gfx::Image& swapped, const gfx::Image& sheet);

    // The texture for the head palette (`swapped`) and each eye's shift (NO_SHIFT: as loaded).
    gfx::TextureRef show(bool swapped, const gd::PI32& shifts);
    // The picture the texture shows (tests read it: a headless renderer keeps no copy of a texture).
    gd::Ref<gfx::Image> image() const;
    // Eye `eye`'s rectangles with the pictures of `shift`, or of the atlas as it is for NO_SHIFT.
    void _paste(gd::Int eye, gd::Int shift);
    static gd::Ref<gfx::Image> _rgba(const gfx::Image& image);
};

}  // namespace t3

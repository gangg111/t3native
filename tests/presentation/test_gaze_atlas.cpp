// remake/tests/presentation/test_gaze_atlas.gd: Gon's eyes' look direction on his atlas (GazeAtlas,
// FighterView.set_gaze; the patches are tools/remake_import/gaze.py's, which
// tools/research/gon_eyes_cases.py and tests/core/test_gon_eyes.gd check against the game).
//
// gfx::Texture makes its GL texture at once and the tests have no GL context, so `_show` below runs
// GazeAtlas.show's picture part (the working copy and the pastes) without making the texture.
// test_an_eye_copies_when_its_shift_changes needs FighterView.setup, whose TexturePacks textures
// need GL too: not ported.
#include <cstdint>
#include <cstring>

#include "content/character_model.h"
#include "gd/io.h"
#include "gd/json.h"
#include "gfx/canvas.h"
#include "gfx/image.h"
#include "presentation/fighter/gaze_atlas.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

constexpr gd::Int NO = GazeAtlas::NO_SHIFT;

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};
const Rgba RED{255, 0, 0, 255};
const Rgba GREEN{0, 255, 0, 255};
const Rgba GREY{128, 128, 128, 255};
const Rgba BLACK{0, 0, 0, 255};
const Rgba WHITE{255, 255, 255, 255};

void fill_rect(gfx::Image& image, int x, int y, int w, int h, Rgba c) {
    for (int j = y; j < y + h; ++j) {
        for (int i = x; i < x + w; ++i) {
            uint8_t* p = image.pixel(i, j);
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = c.a;
        }
    }
}

gfx::Image _image(int w, int h, Rgba colour) {
    gfx::Image image;
    image.width = w;
    image.height = h;
    image.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    fill_rect(image, 0, 0, w, h, colour);
    return image;
}

// GazeAtlas.show without the texture (see the file comment).
void _show(GazeAtlas& atlas, bool swapped, const gd::PI32& shifts) {
    if (atlas._work == nullptr || swapped != atlas._swapped) {
        atlas._swapped = swapped;
        atlas._work = gd::make<gfx::Image>(*atlas._bases[swapped ? 1 : 0]);
        atlas._shifts = gd::PI32{NO, NO};
    }
    for (gd::Int eye = 0; eye < 2; ++eye) {
        if (shifts[eye] != atlas._shifts[eye]) {
            atlas._paste(eye, shifts[eye]);
            atlas._shifts[eye] = shifts[eye];
        }
    }
}

// A 16 x 16 atlas (a 1024 x 8 sheet): eye 0 has the rectangle (2, 2, 4, 4) with shift 3 at sheet
// x 0 and shift 4 at x 8, eye 1 (10, 10, 4, 4) with shift -2 at x 16; the swapped palette has
// eye 1's rectangle only.
gd::Ref<GazeAtlas> _atlas(int scale = 1) {
    gfx::Image sheet = _image(1024 * scale, 8 * scale, Rgba{});
    fill_rect(sheet, 0, 0, 4 * scale, 4 * scale, RED);
    fill_rect(sheet, 8 * scale, 0, 4 * scale, 4 * scale, GREEN);
    fill_rect(sheet, 16 * scale, 0, 4 * scale, 4 * scale, BLACK);
    gd::Json data = gd::Json::parse(R"({"size": [16, 16], "sheet": "x.png", "patches": {
        "0": {"0": {"rects": [[2, 2, 4, 4]], "shifts": {"3": [[0, 0]], "4": [[8, 0]]}},
              "1": {"rects": [[10, 10, 4, 4]], "shifts": {"-2": [[16, 0]]}}},
        "1": {"1": {"rects": [[10, 10, 4, 4]], "shifts": {"-2": [[16, 0]]}}}}})");
    return gd::make<GazeAtlas>(data, _image(16 * scale, 16 * scale, GREY), _image(16 * scale, 16 * scale, WHITE), sheet);
}

Rgba _pixel(const GazeAtlas& atlas, int x, int y) {
    const uint8_t* p = atlas.image()->pixel(x, y);
    return Rgba{p[0], p[1], p[2], p[3]};
}

}  // namespace

T3_TEST(gaze_atlas, the_patches_are_pasted_over_the_atlas) {
    gd::Ref<GazeAtlas> atlas = _atlas();
    _show(*atlas, false, gd::PI32{NO, NO});
    t.expect(_pixel(*atlas, 3, 3) == GREY, "as loaded");
    _show(*atlas, false, gd::PI32{3, NO});
    t.expect(_pixel(*atlas, 3, 3) == RED, "eye 0's shift 3");
    t.expect(_pixel(*atlas, 1, 1) == GREY, "outside the rectangle");
    t.expect(_pixel(*atlas, 11, 11) == GREY, "the other eye is as loaded");
    _show(*atlas, false, gd::PI32{4, -2});
    t.expect(_pixel(*atlas, 3, 3) == GREEN, "eye 0's shift 4");
    t.expect(_pixel(*atlas, 11, 11) == BLACK, "eye 1's shift -2");
    _show(*atlas, false, gd::PI32{NO, -2});
    t.expect(_pixel(*atlas, 3, 3) == GREY, "an eye back to as loaded");
    t.expect(_pixel(*atlas, 11, 11) == BLACK, "the other stays");
}

T3_TEST(gaze_atlas, the_swapped_palette_has_its_own_atlas_and_patches) {
    gd::Ref<GazeAtlas> atlas = _atlas();
    _show(*atlas, false, gd::PI32{3, -2});
    _show(*atlas, true, gd::PI32{3, -2});
    t.expect(_pixel(*atlas, 3, 3) == WHITE, "the swapped atlas has no patches for eye 0");
    t.expect(_pixel(*atlas, 11, 11) == BLACK, "but one for eye 1");
    _show(*atlas, false, gd::PI32{3, -2});
    t.expect(_pixel(*atlas, 3, 3) == RED, "and back");
}

// A pack that replaces the atlas as loaded (here 2x) and not the swapped one: the texture keeps one size.
T3_TEST(gaze_atlas, atlases_of_different_sizes_follow_the_first) {
    gfx::Image sheet = _image(1024, 8, Rgba{});
    fill_rect(sheet, 0, 0, 4, 4, RED);
    gd::Json data = gd::Json::parse(R"({"size": [16, 16], "sheet": "x.png", "patches": {
        "0": {"0": {"rects": [[2, 2, 4, 4]], "shifts": {"3": [[0, 0]]}}}}})");
    GazeAtlas atlas(data, _image(32, 32, GREY), _image(16, 16, WHITE), sheet);
    _show(atlas, true, gd::PI32{NO, NO});
    t.expect(atlas.image()->width == 32 && atlas.image()->height == 32, "the swapped atlas is scaled to the first");
    t.expect(_pixel(atlas, 20, 20) == WHITE, "and keeps its picture");
    _show(atlas, false, gd::PI32{3, NO});
    t.expect(atlas.image()->width == 32 && atlas.image()->height == 32, "the same size again");
    t.expect(_pixel(atlas, 5, 5) == RED, "with the patch scaled to it");
}

T3_TEST(gaze_atlas, the_patches_follow_a_larger_atlas) {
    gd::Ref<GazeAtlas> atlas = _atlas(2);
    _show(*atlas, false, gd::PI32{3, NO});
    t.expect_equal(gd::Int(atlas->image()->width), gd::Int(32), "the pack's size");
    t.expect(_pixel(*atlas, 4, 4) == RED, "the patch scaled to it");
    t.expect(_pixel(*atlas, 11, 11) == RED, "its far corner");
    t.expect(_pixel(*atlas, 12, 12) == GREY, "and no more");
}

T3_TEST(gaze_atlas, converted_gon) {
    for (gd::Int slot : {42, 43}) {
        std::string dir = t3test::imported("characters/costume_" + gd::str(slot));
        if (!t.require(gd::path_join(dir, "model.json"))) return;
        gd::Ref<CharacterModel> model = CharacterModel::load_from(dir);
        if (model->gaze.is_null() || model->gaze.size() == 0) {
            t.skipped = true;   // converted before the eyes' look direction
            return;
        }
        t.expect(gd::file_exists(gd::path_join(dir, model->gaze["sheet"].str())), "slot " + gd::str(slot) + ": the sheet");
        const gd::Json& patches = model->gaze["patches"];
        t.expect(patches.has("0") && patches["0"].has("0") && patches["0"].has("1"),
                 "slot " + gd::str(slot) + ": both eyes in the palette as loaded");
        const gd::Json& eye0 = patches["0"]["0"];
        t.expect_equal(eye0["shifts"].size(), gd::Int(18), "slot " + gd::str(slot) + ": eye 0's shifts 0 to 17");
        const gd::Json& eye1 = patches["0"]["1"];
        t.expect(eye1["shifts"].has("-17") && eye1["shifts"].has("0"), "slot " + gd::str(slot) + ": eye 1's shifts -17 to 0");
    }
    for (gd::Int slot : {0, 22, 44}) {
        std::string dir = t3test::imported(std::string("characters/costume_") + (slot < 10 ? "0" : "") + gd::str(slot));
        if (gd::file_exists(gd::path_join(dir, "model.json"))) {
            gd::Ref<CharacterModel> model = CharacterModel::load_from(dir);
            t.expect(model->gaze.is_null() || model->gaze.size() == 0, "slot " + gd::str(slot) + " has no look direction");
        }
    }
}

// remake/tests/presentation/test_arcade_face.gd: ArcadeFace against the arcade's FUN_80193F90 /
// FUN_80193EA4 / FUN_801A1C74, run in the CPU harness by tools/research/verify_arcade_eyes.py: the
// shapes it printed for the costume slots below (their roles from the arcade's tables, the same
// inputs and random numbers), as (frame, shape) where the shown shape changes.
#include <utility>
#include <vector>

#include "content/character_model.h"
#include "gd/io.h"
#include "presentation/fighter/arcade_face.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

using Changes = std::vector<std::pair<gd::Int, gd::Int>>;

constexpr gd::Int FRAMES = 600;
const std::vector<std::pair<gd::Int, gd::Int>> SHOUT_AT{{20, 0x2005}, {150, 0x2000}, {300, 0x2008}, {400, 0x2002}, {520, 0x2006}};
const std::vector<std::pair<gd::Int, gd::Int>> HELD_SPANS{{100, 110}, {200, 215}};
const std::vector<std::pair<gd::Int, gd::Int>> DOWN_SPANS{{140, 170}, {205, 220}, {450, 470}};
const Changes SLOT_0{{0, 2}, {3, 0}, {20, 3}, {100, 1}, {111, 0}, {140, 2}, {171, 0}, {200, 1}, {215, 2}, {221, 0},
                     {329, 2}, {332, 0}, {400, 3}, {420, 0}, {450, 2}, {471, 0}, {520, 3}, {570, 0}};
const Changes SLOT_12{{0, 2}, {3, 0}, {20, 5}, {40, 0}, {79, 2}, {82, 0}, {100, 1}, {111, 0}, {140, 2}, {171, 0},
                      {200, 1}, {215, 2}, {221, 0}, {300, 5}, {380, 0}, {400, 5}, {420, 0}, {450, 2}, {471, 0}, {520, 5}, {550, 0}};
const Changes SLOT_10{{0, 4}, {3, 0}, {26, 4}, {29, 0}, {68, 4}, {71, 0}, {100, 1}, {111, 0}, {140, 2}, {171, 0},
                      {200, 1}, {215, 2}, {221, 0}, {320, 4}, {323, 0}, {450, 2}, {471, 0}};

gd::Int _random = 12345;

// The harness's `rand` stand-in (verify_arcade_eyes.py rand_sequence).
gd::Int _next() {
    _random = (_random * 1103515245 + 12345) & 0x7FFFFFFF;
    return (_random >> 8) & 0x7FFF;
}

gd::Ref<CharacterModel::ArcadeEyes> _eyes(gd::Int blink, gd::Int held, gd::Int down, gd::Int shout, const gd::PI32& frames) {
    auto eyes = gd::make<CharacterModel::ArcadeEyes>();
    eyes->blink = blink;
    eyes->held = held;
    eyes->down = down;
    eyes->shout = shout;
    eyes->shout_frames = frames;
    return eyes;
}

bool _in(const std::vector<std::pair<gd::Int, gd::Int>>& spans, gd::Int frame) {
    for (const auto& span : spans) {
        if (frame >= span.first && frame < span.second) return true;
    }
    return false;
}

// The (frame, shape) pairs where the shape shown changes over the verifier's schedule.
Changes _run(const gd::Ref<CharacterModel::ArcadeEyes>& eyes) {
    _random = 12345;
    ArcadeFace face(eyes);
    face.rand = _next;
    Changes changes;
    gd::Int last = -1;
    for (gd::Int n = 0; n < FRAMES; ++n) {
        for (const auto& s : SHOUT_AT) {
            if (s.first == n) face.shout(s.second);
        }
        gd::Int shape = face.step(_in(HELD_SPANS, n), _in(DOWN_SPANS, n));
        if (shape != last) {
            changes.emplace_back(n, shape);
            last = shape;
        }
    }
    return changes;
}

}  // namespace

T3_TEST(arcade_face, matches_the_arcade_for_a_shouting_costume) {
    t.expect(_run(_eyes(2, 1, 2, 3, gd::PI32{35, 90, 20, 40, 15, 120, 50, 60, 0})) == SLOT_0, "slot 0");
}

T3_TEST(arcade_face, matches_the_arcade_for_the_fifth_shape) {
    t.expect(_run(_eyes(2, 1, 2, 5, gd::PI32{20, 20, 20, 20, 20, 20, 30, 40, 80})) == SLOT_12, "slot 12");
}

T3_TEST(arcade_face, matches_the_arcade_for_a_costume_that_does_not_shout) {
    t.expect(_run(_eyes(4, 1, 2, 0, gd::PI32{})) == SLOT_10, "slot 10");
}

T3_TEST(arcade_face, a_missing_shape_keeps_the_face) {
    ArcadeFace face(_eyes(CharacterModel::ArcadeEyes::NO_SHAPE, 1, 2, 3, gd::PI32{10}));
    face.rand = _next;
    for (gd::Int n = 0; n < 30; ++n) {
        t.expect_equal(face.step(false, false), ArcadeFace::OPEN, "the blink shape is missing: frame " + gd::str(n));
    }
    face.shout(0x2000);
    t.expect_equal(face.step(false, false), gd::Int(3), "a shout shows its face");
}

// Two faces of one costume (a mirror match) or of two fights do not blink alike: every face made
// with `next_seed` draws its open times from a sequence of its own.
T3_TEST(arcade_face, faces_do_not_blink_alike) {
    ArcadeFace first(_eyes(2, 1, 2, 3, gd::PI32{10}), ArcadeFace::next_seed());
    ArcadeFace second(_eyes(2, 1, 2, 3, gd::PI32{10}), ArcadeFace::next_seed());
    std::vector<gd::Int> draws[2];
    for (gd::Int n = 0; n < 8; ++n) {
        draws[0].push_back(first.rand());
        draws[1].push_back(second.rand());
    }
    t.expect_equal(draws[0] == draws[1], false, "two faces, two sequences");
    ArcadeFace again(_eyes(2, 1, 2, 3, gd::PI32{10}), 0);
    ArcadeFace same(_eyes(2, 1, 2, 3, gd::PI32{10}), 0);
    t.expect_equal(again.rand(), same.rand(), "the same seed gives the same draws (screenshots repeat)");
}

T3_TEST(arcade_face, converted_model_loads_its_faces) {
    std::string dir = t3test::imported("characters/costume_00");
    if (!t.require(gd::path_join(dir, "model.json"), "convert with --arcade first")) return;
    gd::Ref<CharacterModel> model = CharacterModel::load_from(dir);
    if (model->arcade_eyes == nullptr) {
        t.skipped = true;   // converted before the arcade's eye shapes
        return;
    }
    gd::Ref<CharacterModel::ArcadeEyes> eyes = model->arcade_eyes;
    for (gd::Int shape : {eyes->blink, eyes->held, eyes->down, eyes->shout}) {
        if (shape > CharacterModel::OPEN_SHAPE) {
            t.expect(model->eye_textures.has(shape), "an atlas for shape " + gd::str(shape));
        }
    }
    t.expect_equal(eyes->shout_frames.size(), gd::Int(9), "a shouting character has a frame count per voice");
}

// StageView's scene-tree-free checks from remake/tests/content/test_assets.gd
// (test_floor_reaches_under_the_panorama_walls). The other StageView tests of the remake are the
// arcade stages' (test_arcade_stage.gd: no arcade data) and the floor pattern's pictures
// (test_floor_pattern_is_composed_and_smoothed: decoded PNG pixels, part of the assets suite).
#include "content/stage_data.h"
#include "presentation/stage/stage_view.h"
#include "support/test_suite.h"

using namespace t3;

T3_TEST(stage_view, floor_reaches_under_the_panorama_walls) {
    const std::string STAGE = t3test::imported("stages/e");
    if (!t.require(STAGE + "/stage.json")) return;
    gd::Ref<StageData> stage = StageData::load_from(STAGE);
    gd::Int walls = 0;
    for (gd::Int v : stage->panorama_extent) walls = gd::maxi(walls, gd::absi(v));
    walls = static_cast<gd::Int>(static_cast<double>(walls) * StageView::PANORAMA_PARALLAX);
    t.expect(walls > 0, "panorama extent missing");
    // The floor is a square, so its corners reach the wall corners too.
    t.expect(StageView::floor_half_extent(stage) > walls, "floor " + gd::str(StageView::floor_half_extent(stage)) +
                                                              " does not reach the walls at " + gd::str(walls));
}

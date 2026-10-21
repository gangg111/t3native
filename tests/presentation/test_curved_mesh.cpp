// remake/tests/presentation/test_curved_mesh.gd: the curved surfaces' grids (CurvedMesh) and the flat
// mesh arrays (FighterView._arrays) on a synthetic model: every triangle becomes a LEVEL x LEVEL grid
// wound like the triangle, the grids are built once per model, and the smooth normal's halves reach
// CUSTOM2 / CUSTOM3 and the third joint's term CUSTOM1.w, UV2 and CUSTOM3.w.
// test_corner_texture's pixel checks need the float corner texture, which gfx3d cannot make yet
// (CurvedMesh::_texture TODO); the hand's triangle numbering it also checks is kept.
#include <cmath>
#include <set>
#include <vector>

#include "content/character_model.h"
#include "gfx3d/math3d.h"
#include "presentation/fighter/curved_mesh.h"
#include "presentation/fighter/fighter_view.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

constexpr gd::Int STRIDE = 28;

// A converter corner: positions, seam weight, the game's normal, UV, bones, the smooth normal's
// halves, the other joint, the curves flag and the third joint's term (pc, wc, bone_c).
std::vector<float> _corner(Vector3 p, gfx::Vec2 uv, gd::Int bone, bool curves) {
    float b = static_cast<float>(bone);
    return {p.x, p.y, p.z, p.x, p.y, p.z, 0.0f, 0.0f, 1.0f, 0.0f, uv.x, uv.y,
            b, b, b, 0.0f, 2.0f, 0.0f, 0.5f, 0.25f, 0.0f, 7.0f, curves ? 1.0f : 0.0f,
            3.0f, 4.0f, 5.0f, 0.25f, 11.0f};
}

gd::Ref<CharacterModel::Surface> _surface(gd::Int triangles, gd::Int bone) {
    auto surface = gd::make<CharacterModel::Surface>();
    std::vector<float>& v = surface->vertices.vec();
    for (gd::Int t = 0; t < triangles; ++t) {
        float x = static_cast<float>(t);
        for (const auto& c : {_corner(Vector3(x, 0, 0), gfx::Vec2(0, 0), bone, true),
                              _corner(Vector3(x + 1, 0, 0), gfx::Vec2(1, 0), bone, false),
                              _corner(Vector3(x, 1, 0), gfx::Vec2(0, 1), bone, true)}) {
            v.insert(v.end(), c.begin(), c.end());
        }
    }
    return surface;
}

gd::Ref<CharacterModel> _model() {
    auto model = gd::make<CharacterModel>();
    model->vertex_floats = STRIDE;
    model->surfaces.append(_surface(2, 3));
    auto hand = gd::make<CharacterModel::VariantGroup>();
    hand->variants.append(gd::Arr<gd::Ref<CharacterModel::Surface>>{_surface(1, 9)});
    model->hands.append(hand);
    return model;
}

bool is_equal_approx(float a, float b) { return std::fabs(a - b) < 0.00001f * std::fmax(1.0f, std::fabs(b)); }

}  // namespace

T3_TEST(curved_mesh, grid_per_triangle) {
    gd::Ref<CharacterModel> model = _model();
    gd::Ref<CurvedMesh> curved = CurvedMesh::of(model);
    MeshArrays arrays = curved->arrays(model->surfaces[0]);
    gd::Int per_triangle = ((CurvedMesh::LEVEL + 1) * (CurvedMesh::LEVEL + 2)) >> 1;
    const std::vector<Vector3>& positions = arrays.vertex;
    const std::vector<int>& indices = arrays.index;
    const std::vector<float>& custom0 = arrays.custom[0];
    const std::vector<gfx::Vec2>& uvs = arrays.tex_uv;
    t.expect_equal(static_cast<gd::Int>(positions.size()), 2 * per_triangle, "a grid of vertices per triangle");
    t.expect_equal(static_cast<gd::Int>(indices.size()), 2 * 3 * CurvedMesh::LEVEL * CurvedMesh::LEVEL,
                   "LEVEL squared triangles per triangle");
    t.expect_equal(custom0[static_cast<size_t>(4 * per_triangle)], 1.0f, "the second grid names the second triangle");
    for (gd::Int i = 0; i < static_cast<gd::Int>(indices.size()); i += 3) {
        const Vector3& a = positions[static_cast<size_t>(indices[static_cast<size_t>(i)])];
        const Vector3& b = positions[static_cast<size_t>(indices[static_cast<size_t>(i + 1)])];
        const Vector3& c = positions[static_cast<size_t>(indices[static_cast<size_t>(i + 2)])];
        // Barycentric (w0, w1, w2): the winding of (corner 0, corner 1, corner 2) is positive in
        // the (w1, w2) plane.
        float cross = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
        t.expect(cross > 0.0f, "grid triangle at index " + gd::str(i) + " wound like its triangle");
    }
    for (size_t i = 0; i < positions.size(); ++i) {
        const Vector3& w = positions[i];
        t.expect(is_equal_approx(w.x + w.y + w.z, 1.0f), "barycentric weights sum to 1");
        t.expect(is_equal_approx(uvs[i].x, w.y) && is_equal_approx(uvs[i].y, w.z), "the UV interpolated");
    }
}

T3_TEST(curved_mesh, corner_texture) {
    gd::Ref<CharacterModel> model = _model();
    gd::Ref<CurvedMesh> curved = CurvedMesh::of(model);
    // The hand's surface follows the body's two triangles: its triangle is the third.
    gd::Ref<CharacterModel::Surface> hand_surface = model->hands[0]->variants[0][0];
    const std::vector<float> hand_custom0 = curved->arrays(hand_surface).custom[0];
    t.expect_equal(hand_custom0[0], 2.0f, "the hand's triangles numbered after the body's");
    // TODO: gfx3d float textures: the texel checks (pa and bone_a, s_own and wb + 2 curves, ...).
}

T3_TEST(curved_mesh, built_once_per_model) {
    gd::Ref<CharacterModel> model = _model();
    t.expect(CurvedMesh::of(model) == CurvedMesh::of(model), "shared by every view of the model");
}

T3_TEST(curved_mesh, grid_index) {
    std::set<gd::Int> seen;
    for (gd::Int i = 0; i < CurvedMesh::LEVEL + 1; ++i) {
        for (gd::Int j = 0; j < CurvedMesh::LEVEL + 1 - i; ++j) {
            seen.insert(CurvedMesh::grid_index(i, j));
        }
    }
    t.expect_equal(static_cast<gd::Int>(seen.size()), ((CurvedMesh::LEVEL + 1) * (CurvedMesh::LEVEL + 2)) >> 1,
                   "every grid vertex its own index");
    t.expect_equal(CurvedMesh::grid_index(CurvedMesh::LEVEL, 0), static_cast<gd::Int>(seen.size()) - 1, "the last one last");
}

T3_TEST(curved_mesh, flat_arrays_carry_the_halves) {
    MeshArrays arrays = FighterView::_arrays(_surface(1, 4)->vertices, STRIDE);
    const std::vector<float>& custom1 = arrays.custom[1];
    const std::vector<float>& custom2 = arrays.custom[2];   // the halves as floats: index = byte offset / 2
    const std::vector<float>& custom3 = arrays.custom[3];
    const std::vector<gfx::Vec2>& uv2s = arrays.tex_uv2;
    t.expect_equal(static_cast<gd::Int>(custom2.size()) * 2, 3 * FighterView::HALF_RGBA_BYTES, "four halves per vertex");
    t.expect_equal(custom2[1], 2.0f, "s_own.y");
    t.expect_equal(custom2[3], 7.0f, "bone_other");
    t.expect_equal(custom3[0], 0.5f, "s_other.x");
    t.expect_equal(custom3[1], 0.25f, "s_other.y");
    t.expect_equal(custom1[3], 11.25f, "bone_c + wc");
    t.expect(uv2s[0].x == 3.0f && uv2s[0].y == 4.0f, "pc.xy");
    t.expect_equal(custom3[3], 5.0f, "pc.z");
}

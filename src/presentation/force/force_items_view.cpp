// remake/presentation/force/force_items_view.gd
// Tekken Force's items (see the header).
#include "presentation/force/force_items_view.h"

#include <algorithm>
#include <limits>

#include "content/stage_data.h"
#include "content/texture_packs.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/camera/view_matrix.h"
#include "core/fighter/joint_frame.h"
#include "core/force/tekken_force.h"
#include "core/math/fx.h"
#include "gd/io.h"
#include "presentation/hud/hud_frame.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

// Camera3D.project_ray_origin / project_ray_normal for the rig's perspective and frustum cameras:
// the eye, and the direction through the window point.
Vector3 project_ray_origin(const Camera3D& camera, gfx::Vec2) { return camera.get_global_position(); }

Vector3 project_ray_normal(const Camera3D& camera, gfx::Vec2 at) {
    return (camera.project_position(at, 1.0f) - camera.get_global_position()).normalized();
}

}  // namespace

void ForceItemsView::setup(const gd::Ref<StageData>& stage) {
    gfx::TextureRef picture = TexturePacks::texture(gd::path_join(stage->directory, stage->item["picture"].str()));
    gfx::TextureRef shadow = TexturePacks::texture(gd::path_join(stage->directory, stage->item["shadow"].str()));
    gd::Ref<StandardMaterial3D> picture_material = _material(picture);
    picture_material->transparency = StandardMaterial3D::TRANSPARENCY_ALPHA_SCISSOR;
    gd::Ref<StandardMaterial3D> shadow_material = _material(shadow);
    shadow_material->transparency = StandardMaterial3D::TRANSPARENCY_ALPHA;
    shadow_material->albedo_color = SHADOW_SHADE;
    for (gd::Int i = 0; i < 2; ++i) {
        for (const gd::Ref<StandardMaterial3D>& m : {picture_material, shadow_material}) {
            gd::Ref<MeshInstance3D> quad = gd::make<MeshInstance3D>();
            quad->mesh = gd::make<ImmediateMesh>();
            quad->material_override = m;
            quad->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
            quad->visible = false;
            add_child(quad);
            _meshes.append(quad);
        }
        _boxes.append(std::vector<float>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    }
}

gd::Ref<StandardMaterial3D> ForceItemsView::_material(const gfx::TextureRef& texture) {
    gd::Ref<StandardMaterial3D> m = gd::make<StandardMaterial3D>();
    m->shading_mode = StandardMaterial3D::SHADING_MODE_UNSHADED;
    m->albedo_texture = texture;
    m->texture_filter = StandardMaterial3D::TEXTURE_FILTER_NEAREST;
    m->cull_mode = StandardMaterial3D::CULL_DISABLED;
    m->disable_receive_shadows = true;
    m->disable_fog = true;
    return m;
}

// After a simulation step: each item's box.
void ForceItemsView::apply(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<TekkenForce> force = sim->fight->force;
    if (force == nullptr) {
        return;
    }
    for (gd::Int i = 0; i < 2; ++i) {
        std::vector<float>& box = _boxes[i];
        for (int k = 0; k < 5; ++k) {
            box[k] = box[5 + k];
        }
        gd::PI32 it = force->item(i);
        if (gd::Int(it[0]) != 1 || sim->view == nullptr) {
            box[7] = 0;
            continue;
        }
        std::vector<float> cur = picture_box(sim->view->screen_of(
            gd::make<JointFrame>(Fx::identity(), gd::PI32{static_cast<int32_t>(gd::Int(it[1])), static_cast<int32_t>(gd::Int(it[2])),
                                                          static_cast<int32_t>(gd::Int(it[3]))})));
        if (box[2] == 0) {
            // Newly placed: no movement to interpolate from.
            for (int k = 0; k < 5; ++k) {
                box[k] = cur[k];
            }
        }
        for (int k = 0; k < 5; ++k) {
            box[5 + k] = cur[k];
        }
    }
}

// The picture's box over an item's projected point (screen x, y, SZ).
std::vector<float> ForceItemsView::picture_box(const gd::PI32& s) {
    gd::Int d = gd::maxi(gd::Int(s[2]) >> 2, 1);
    gd::Int w = Fx::div_trunc(WIDTH, d);
    gd::Int h = Fx::div_trunc(HEIGHT, d);
    gd::Int left = gd::Int(s[0]) - Fx::div_trunc(w, 2);
    gd::Int base = gd::Int(s[1]) - h;
    return std::vector<float>{static_cast<float>(left), static_cast<float>(base - h), static_cast<float>(w),
                              static_cast<float>(h), static_cast<float>(gd::Int(s[2]))};
}

// Every rendered frame: the quads between the last two steps, in the camera's view.
void ForceItemsView::show_between(double weight, const gd::Ref<Camera3D>& camera, const gfx::Rect2& frame) {
    for (gd::Int i = 0; i < 2; ++i) {
        const std::vector<float>& box = _boxes[i];
        gd::Ref<MeshInstance3D> picture = _meshes[2 * i];
        gd::Ref<MeshInstance3D> shadow = _meshes[2 * i + 1];
        picture->visible = box[7] != 0;
        shadow->visible = picture->visible;
        if (!picture->visible) {
            continue;
        }
        std::vector<float> b;
        for (int k = 0; k < 5; ++k) {
            b.push_back(static_cast<float>(box[k] + (box[5 + k] - box[k]) * weight));
        }
        float left = b[0];
        float top = b[1];
        float w = b[2];
        float h = b[3];
        gfx::Vec2 bottom = HudFrame::point(frame, left + w / 2.0f, top + h);
        // Short of the floor by the shadow's step too, so that the floor does not hide the shadow's
        // bottom.
        double depth = std::min(b[4] / WorldSpace::UNITS_PER_METRE, _floor_depth(camera, bottom) - 2.0 * SHADOW_BEHIND);
        _quad(picture, camera, frame, gfx::Rect2(left, top, w, h), depth);
        _quad(shadow, camera, frame, gfx::Rect2(left, top + h - w, w, w), depth + SHADOW_BEHIND);
    }
}

// The view depth at which the ray through a window point meets the floor (y 0).
double ForceItemsView::_floor_depth(const gd::Ref<Camera3D>& camera, gfx::Vec2 at) {
    Vector3 origin = project_ray_origin(*camera, at);
    Vector3 ray = project_ray_normal(*camera, at);
    if (ray.y >= -1e-6f) {
        return std::numeric_limits<double>::infinity();
    }
    Vector3 hit = origin + ray * (-origin.y / ray.y);
    return (hit - camera->get_global_position()).dot(-camera->get_global_basis().z());
}

void ForceItemsView::_quad(const gd::Ref<MeshInstance3D>& quad, const gd::Ref<Camera3D>& camera, const gfx::Rect2& frame,
                           const gfx::Rect2& box, double depth) {
    std::vector<Vector3> corners;
    for (gfx::Vec2 c : {box.position, gfx::Vec2(box.end().x, box.position.y), gfx::Vec2(box.position.x, box.end().y), box.end()}) {
        corners.push_back(camera->project_position(HudFrame::point(frame, c.x, c.y), static_cast<float>(depth)));
    }
    const gfx::Vec2 uvs[4] = {gfx::Vec2(0, 0), gfx::Vec2(1, 0), gfx::Vec2(0, 1), gfx::Vec2(1, 1)};
    gd::Ref<ImmediateMesh> mesh = std::dynamic_pointer_cast<ImmediateMesh>(quad->mesh);
    mesh->clear_surfaces();
    mesh->surface_begin(Primitive::TRIANGLES);
    for (int k : {0, 1, 2, 1, 3, 2}) {
        mesh->surface_set_uv(uvs[k]);
        mesh->surface_add_vertex(corners[k]);
    }
    mesh->surface_end();
}

}  // namespace t3

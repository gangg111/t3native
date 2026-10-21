#include "gfx3d/nodes.h"

#include <algorithm>
#include <cmath>

namespace t3 {

void Node3D::add_child(const gd::Ref<Node3D>& child) {
    if (!child || child->parent_ == this) return;
    if (child->parent_) child->parent_->remove_child(child.get());
    child->parent_ = this;
    children_.push_back(child);
}

void Node3D::remove_child(const Node3D* child) {
    auto it = std::find_if(children_.begin(), children_.end(), [&](const gd::Ref<Node3D>& c) { return c.get() == child; });
    if (it == children_.end()) return;
    (*it)->parent_ = nullptr;
    children_.erase(it);
}

void Node3D::move_child(const Node3D* child, int index) {
    auto it = std::find_if(children_.begin(), children_.end(), [&](const gd::Ref<Node3D>& c) { return c.get() == child; });
    if (it == children_.end()) return;
    gd::Ref<Node3D> c = *it;
    children_.erase(it);
    index = std::clamp(index, 0, static_cast<int>(children_.size()));
    children_.insert(children_.begin() + index, c);
}

int Node3D::get_index() const {
    if (!parent_) return -1;
    const auto& siblings = parent_->children_;
    for (size_t i = 0; i < siblings.size(); ++i)
        if (siblings[i].get() == this) return static_cast<int>(i);
    return -1;
}

bool Node3D::is_inside_tree() const {
    const Node3D* n = this;
    while (n->parent_) n = n->parent_;
    return n->tree_root_;
}

bool Node3D::is_visible_in_tree() const {
    for (const Node3D* n = this; n; n = n->parent_)
        if (!n->visible) return false;
    return true;
}

// Godot's Basis::from_euler with EULER_ORDER_YXZ: Ry * Rx * Rz.
void Node3D::set_rotation(const Vector3& euler) {
    Vector3 scale = transform.basis.get_scale();
    Basis x = Basis::from_axis_angle({1, 0, 0}, euler.x);
    Basis y = Basis::from_axis_angle({0, 1, 0}, euler.y);
    Basis z = Basis::from_axis_angle({0, 0, 1}, euler.z);
    transform.basis = (y * x * z).scaled_columns(scale);
}

void Node3D::set_scale(const Vector3& s) {
    Basis b = transform.basis;
    for (int i = 0; i < 3; ++i) {
        Vector3 c = b.get_column(i).normalized();
        b.set_column(i, c * s[i]);
    }
    transform.basis = b;
}

Transform3D Node3D::get_global_transform() const {
    Transform3D t = transform;
    for (const Node3D* n = parent_; n; n = n->parent_) t = n->transform * t;
    return t;
}

Projection Camera3D::get_camera_projection() const {
    float aspect = viewport_size.x / std::max(viewport_size.y, 1.0f);
    bool flip = keep_aspect == KEEP_WIDTH;
    switch (projection) {
        case PROJECTION_PERSPECTIVE: return Projection::perspective(fov, aspect, near, far, flip);
        case PROJECTION_ORTHOGONAL: {
            // Godot's set_orthogonal(size, aspect, near, far, flip_fov).
            float w = flip ? size : size * aspect, h = flip ? size / aspect : size;
            return Projection::orthogonal(-w / 2, w / 2, -h / 2, h / 2, near, far);
        }
        case PROJECTION_FRUSTUM: {
            // Godot's set_frustum(size, aspect, offset, near, far, flip_fov).
            float s = flip ? size : size * aspect;
            return Projection::frustum(-s / 2 + frustum_offset.x, s / 2 + frustum_offset.x,
                                       -s / aspect / 2 + frustum_offset.y, s / aspect / 2 + frustum_offset.y, near, far);
        }
    }
    return {};
}

gfx::Vec2 Camera3D::unproject_position(const Vector3& world) const {
    Vector3 v = get_global_transform().affine_inverse().xform(world);
    Vector4 clip = get_camera_projection().xform({v.x, v.y, v.z, 1.0f});
    float w = clip.w != 0 ? clip.w : 1e-6f;
    float nx = clip.x / w, ny = clip.y / w;
    return {(nx * 0.5f + 0.5f) * viewport_size.x, (-ny * 0.5f + 0.5f) * viewport_size.y};
}

bool Camera3D::is_position_behind(const Vector3& world) const {
    Transform3D t = get_global_transform();
    Vector3 eye_dir = -t.basis.get_column(2);
    return eye_dir.dot(world - t.origin) < near;
}

Vector3 Camera3D::project_position(gfx::Vec2 screen, float z_depth) const {
    // The point on the plane at z_depth in front of the camera under the pixel.
    Projection p = get_camera_projection();
    float nx = screen.x / viewport_size.x * 2.0f - 1.0f;
    float ny = 1.0f - screen.y / viewport_size.y * 2.0f;
    // For x_ndc = (m00 x + m20 z) / -z: x = (x_ndc * -z - m20 z) / m00, with z = -depth.
    float z = -z_depth;
    float w = p.is_orthogonal() ? 1.0f : -z;
    float x = (nx * w - p.m[2][0] * z - p.m[3][0]) / p.m[0][0];
    float y = (ny * w - p.m[2][1] * z - p.m[3][1]) / p.m[1][1];
    return get_global_transform().xform({x, y, z});
}

}  // namespace t3

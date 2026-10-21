#pragma once
// Godot's 3D nodes as a small retained tree: Node3D (transform, visibility, children), the
// geometry instances, Camera3D, the lights and Environment, with the members the remake's views
// use. Children are shared (gd::Ref) like Godot's owned nodes; a view that extends Node3D in the
// remake extends it here and adds its parts with add_child.
#include <cstdint>
#include <vector>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx3d/math3d.h"
#include "gfx3d/resources.h"

namespace t3 {

class Node3D {
public:
    Transform3D transform;
    bool visible = true;

    virtual ~Node3D() = default;

    void add_child(const gd::Ref<Node3D>& child);
    void remove_child(const Node3D* child);
    void move_child(const Node3D* child, int index);
    int get_index() const;
    const std::vector<gd::Ref<Node3D>>& get_children() const { return children_; }
    Node3D* get_parent() const { return parent_; }
    // The root of a rendered tree counts as inside the tree, and so does everything under it.
    bool is_inside_tree() const;
    bool is_visible_in_tree() const;
    void set_as_tree_root(bool root) { tree_root_ = root; }

    Vector3 get_position() const { return transform.origin; }
    void set_position(const Vector3& p) { transform.origin = p; }
    // Godot's rotation (Euler angles, YXZ order) and scale on the basis.
    void set_rotation(const Vector3& euler);
    void set_scale(const Vector3& s);
    Transform3D get_global_transform() const;
    Basis get_global_basis() const { return get_global_transform().basis; }
    Vector3 get_global_position() const { return get_global_transform().origin; }

private:
    Node3D* parent_ = nullptr;
    std::vector<gd::Ref<Node3D>> children_;
    bool tree_root_ = false;
};

class VisualInstance3D : public Node3D {
public:
    uint32_t layers = 1;
};

class GeometryInstance3D : public VisualInstance3D {
public:
    enum ShadowCastingSetting { SHADOW_CASTING_SETTING_OFF, SHADOW_CASTING_SETTING_ON };
    ShadowCastingSetting cast_shadow = SHADOW_CASTING_SETTING_ON;
    gd::Ref<Material> material_override;
    float transparency = 0.0f;
    AABB custom_aabb;   // no volume: the mesh's own
};

class MeshInstance3D : public GeometryInstance3D {
public:
    gd::Ref<Mesh> mesh;
};

class MultiMeshInstance3D : public GeometryInstance3D {
public:
    gd::Ref<MultiMesh> multimesh;
};

class Environment {
public:
    enum BGMode { BG_CLEAR_COLOR, BG_COLOR };
    enum AmbientSource { AMBIENT_SOURCE_BG, AMBIENT_SOURCE_DISABLED, AMBIENT_SOURCE_COLOR };
    enum ToneMapper { TONE_MAPPER_LINEAR };
    BGMode background_mode = BG_CLEAR_COLOR;
    gfx::Color background_color = gfx::Color::black();
    AmbientSource ambient_light_source = AMBIENT_SOURCE_BG;
    gfx::Color ambient_light_color = gfx::Color::black();
    float ambient_light_energy = 1.0f;
    ToneMapper tonemap_mode = TONE_MAPPER_LINEAR;
    bool glow_enabled = false;
    float glow_intensity = 0.8f;
    bool ssao_enabled = false;
    float ssao_intensity = 2.0f;
    float ssao_radius = 1.0f;
    bool ssil_enabled = false;
};

class Camera3D : public Node3D {
public:
    enum ProjectionType { PROJECTION_PERSPECTIVE, PROJECTION_ORTHOGONAL, PROJECTION_FRUSTUM };
    enum KeepAspect { KEEP_WIDTH, KEEP_HEIGHT };
    ProjectionType projection = PROJECTION_PERSPECTIVE;
    KeepAspect keep_aspect = KEEP_HEIGHT;
    float fov = 75.0f;              // degrees
    float size = 1.0f;              // orthogonal and frustum
    gfx::Vec2 frustum_offset;
    float near = 0.05f;
    float far = 4000.0f;
    uint32_t cull_mask = 0xFFFFF;
    gd::Ref<Environment> environment;
    // The viewport's size in pixels (Godot reads it from the viewport): the renderer sets it before
    // drawing, the views may set it before projecting.
    gfx::Vec2 viewport_size{1, 1};

    Projection get_camera_projection() const;
    // Godot's unproject_position: the viewport pixel of a world point.
    gfx::Vec2 unproject_position(const Vector3& world) const;
    bool is_position_behind(const Vector3& world) const;
    // Godot's project_position: the world point at `z_depth` along the view through pixel `screen`.
    Vector3 project_position(gfx::Vec2 screen, float z_depth) const;
};

class Light3D : public VisualInstance3D {
public:
    gfx::Color light_color = gfx::Color::white();
    float light_energy = 1.0f;
    float light_specular = 0.5f;
    bool shadow_enabled = false;
    float shadow_bias = 0.1f;
    float shadow_normal_bias = 2.0f;
    float shadow_blur = 1.0f;
    uint32_t light_cull_mask = 0xFFFFFFFF;
};

class DirectionalLight3D : public Light3D {
public:
    float directional_shadow_max_distance = 100.0f;
};

class OmniLight3D : public Light3D {
public:
    float omni_range = 5.0f;
    float omni_attenuation = 1.0f;
    OmniLight3D() { shadow_bias = 0.2f; shadow_normal_bias = 1.0f; }
};

}  // namespace t3

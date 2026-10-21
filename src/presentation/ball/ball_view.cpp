// remake/presentation/ball/ball_view.gd
// Tekken Ball's ball and court (see the header).
#include "presentation/ball/ball_view.h"

#include <algorithm>
#include <cmath>

#include "content/game_ram.h"
#include "core/ball/tekken_ball.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_state.h"
#include "core/game/byte_block.h"
#include "core/game/mode_region.h"
#include "presentation/screens/screen_canvas.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

// Basis.from_scale.
Basis from_scale(const Vector3& s) { return Basis(Vector3(s.x, 0, 0), Vector3(0, s.y, 0), Vector3(0, 0, s.z)); }

struct Quat {
    double x = 0, y = 0, z = 0, w = 1;
};

// Godot's Basis.get_rotation_quaternion: the orthonormalised basis (Gram-Schmidt, x first; a
// negative determinant flips it) as a quaternion.
Quat rotation_quaternion(const Basis& b) {
    Vector3 x = b.get_column(0), y = b.get_column(1), z = b.get_column(2);
    x = x.normalized();
    y = (y - x * x.dot(y)).normalized();
    z = (z - x * x.dot(z) - y * y.dot(z)).normalized();
    Basis m(x, y, z);
    if (m.determinant() < 0) m = Basis(-x, -y, -z);
    double trace = m.rows[0].x + m.rows[1].y + m.rows[2].z;
    Quat q;
    if (trace > 0.0) {
        double s = std::sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (m.rows[2].y - m.rows[1].z) / s;
        q.y = (m.rows[0].z - m.rows[2].x) / s;
        q.z = (m.rows[1].x - m.rows[0].y) / s;
    } else if (m.rows[0].x > m.rows[1].y && m.rows[0].x > m.rows[2].z) {
        double s = std::sqrt(1.0 + m.rows[0].x - m.rows[1].y - m.rows[2].z) * 2.0;
        q.w = (m.rows[2].y - m.rows[1].z) / s;
        q.x = 0.25 * s;
        q.y = (m.rows[0].y + m.rows[1].x) / s;
        q.z = (m.rows[0].z + m.rows[2].x) / s;
    } else if (m.rows[1].y > m.rows[2].z) {
        double s = std::sqrt(1.0 + m.rows[1].y - m.rows[0].x - m.rows[2].z) * 2.0;
        q.w = (m.rows[0].z - m.rows[2].x) / s;
        q.x = (m.rows[0].y + m.rows[1].x) / s;
        q.y = 0.25 * s;
        q.z = (m.rows[1].z + m.rows[2].y) / s;
    } else {
        double s = std::sqrt(1.0 + m.rows[2].z - m.rows[0].x - m.rows[1].y) * 2.0;
        q.w = (m.rows[1].x - m.rows[0].y) / s;
        q.x = (m.rows[0].z + m.rows[2].x) / s;
        q.y = (m.rows[1].z + m.rows[2].y) / s;
        q.z = 0.25 * s;
    }
    return q;
}

Quat slerp(Quat a, Quat b, double t) {
    double cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (cosom < 0.0) {
        cosom = -cosom;
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    double s0 = 1.0 - t, s1 = t;
    if (1.0 - cosom > 1e-6) {
        double omega = std::acos(cosom), sinom = std::sin(omega);
        s0 = std::sin((1.0 - t) * omega) / sinom;
        s1 = std::sin(t * omega) / sinom;
    }
    Quat r{s0 * a.x + s1 * b.x, s0 * a.y + s1 * b.y, s0 * a.z + s1 * b.z, s0 * a.w + s1 * b.w};
    double l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    return {r.x / l, r.y / l, r.z / l, r.w / l};
}

Basis basis_of(const Quat& q) {
    double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Basis b;
    b.rows[0] = Vector3(static_cast<float>(1 - 2 * (yy + zz)), static_cast<float>(2 * (xy - wz)), static_cast<float>(2 * (xz + wy)));
    b.rows[1] = Vector3(static_cast<float>(2 * (xy + wz)), static_cast<float>(1 - 2 * (xx + zz)), static_cast<float>(2 * (yz - wx)));
    b.rows[2] = Vector3(static_cast<float>(2 * (xz - wy)), static_cast<float>(2 * (yz + wx)), static_cast<float>(1 - 2 * (xx + yy)));
    return b;
}

// Godot's Transform3D.interpolate_with: rotation slerped, scale and origin lerped.
Transform3D interpolate_with(const Transform3D& from, const Transform3D& to, double weight) {
    Vector3 s = from.basis.get_scale().lerp(to.basis.get_scale(), static_cast<float>(weight));
    Quat q = slerp(rotation_quaternion(from.basis), rotation_quaternion(to.basis), weight);
    return Transform3D(basis_of(q).scaled_columns(s), from.origin.lerp(to.origin, static_cast<float>(weight)));
}

}  // namespace

BallView::BallView() {
    _ball = gd::make<MeshInstance3D>();
    _court = gd::make<MeshInstance3D>();
    _flat = gd::make<StandardMaterial3D>();
}

void BallView::setup(const gd::Ref<GameRam>& ram) {
    _ram = ram;
    for (gd::Int t = 0; t < 3; ++t) {
        _meshes.append(_sphere(t));
        gd::Ref<StandardMaterial3D> m = gd::make<StandardMaterial3D>();
        m->vertex_color_use_as_albedo = true;
        // The face colours are the game's display values (sRGB), like textures.
        m->vertex_color_is_srgb = true;
        gd::Int power = ram->u8(STYLES + 24 * t + 0xC);
        m->roughness = std::clamp(1.0f - static_cast<float>(power) / 128.0f, 0.1f, 1.0f);
        m->metallic_specular = 0.8f;
        _materials.append(m);
        _overlay_materials.append(_over_scene(gd::make<StandardMaterial3D>(*m)));
    }
    _flat->shading_mode = StandardMaterial3D::SHADING_MODE_UNSHADED;
    _flat->vertex_color_use_as_albedo = false;
    _flat_overlay = _over_scene(gd::make<StandardMaterial3D>(*_flat));
    // Like the fighters, the ball casts the stage light's shadow (the game's flattened ring of
    // the equator is replaced as the fighters' flat shadows are).
    _ball->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_ON;
    add_child(_ball);
    _court->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
    add_child(_court);
}

gd::Ref<StandardMaterial3D> BallView::_over_scene(const gd::Ref<StandardMaterial3D>& m) {
    m->no_depth_test = true;
    m->render_priority = static_cast<int>(OVERLAY_PRIORITY);
    return m;
}

// A game SVECTOR (three s16) in RAM, in the game's frame.
Vector3 BallView::svector(const gd::Ref<GameRam>& ram, gd::Int at) {
    return Vector3(static_cast<float>(ram->s16(at)), static_cast<float>(ram->s16(at + 2)), static_cast<float>(ram->s16(at + 4)));
}

gd::PI32 BallView::face_corners(const gd::Ref<GameRam>& ram, gd::Int pass_, gd::Int i) {
    gd::Int table = pass_ == 0 ? TRIANGLES : QUADS;
    gd::PI32 idx;
    for (gd::Int k = 0; k < 3 + pass_; ++k) {
        idx.append(static_cast<int32_t>(ram->u8(table + 4 * i + k)));
    }
    return idx;
}

gfx::Color BallView::face_colour(const gd::Ref<GameRam>& ram, gd::Int t, gd::Int pass_, gd::Int i) {
    return ScreenCanvas::psx(ram->u32(COLOURS + 64 * t + 0x20 * pass_ + 4 * (i & 7)));
}

// The sphere of a ball type.
gd::Ref<ArrayMesh> BallView::_sphere(gd::Int t) {
    MeshArrays arrays;
    for (gd::Int pass_ = 0; pass_ < 2; ++pass_) {
        gd::Int faces = FACE_COUNTS[pass_];
        for (gd::Int i = 0; i < faces; ++i) {
            gfx::Color c = face_colour(_ram, t, pass_, i);
            gd::PI32 idx = face_corners(_ram, pass_, i);
            // PSX quads are (0, 1, 2) and (1, 3, 2); the game's winding faces outwards with y down.
            std::vector<std::array<int, 3>> tris;
            if (pass_ == 0) tris = {{0, 1, 2}};
            else tris = {{0, 1, 2}, {1, 3, 2}};
            for (const std::array<int, 3>& tri : tris) {
                for (int k : {tri[0], tri[2], tri[1]}) {
                    gd::Int v = idx[k];
                    Vector3 at = svector(_ram, VERTICES + 8 * v);
                    arrays.vertex.push_back(WorldSpace::point(at.x, at.y, at.z));
                    Vector3 n = svector(_ram, NORMALS + 8 * v);
                    arrays.normal.push_back(WorldSpace::point(n.x, n.y, n.z).normalized());
                    arrays.color.push_back(c);
                }
            }
        }
    }
    gd::Ref<ArrayMesh> mesh = gd::make<ArrayMesh>();
    mesh->add_surface_from_arrays(Primitive::TRIANGLES, std::move(arrays));
    return mesh;
}

// The court.
gd::Ref<ArrayMesh> BallView::_court_mesh(gd::Int zone) {
    std::vector<float> shades;
    for (gd::Int k = 0; k < COURT_ROWS; ++k) {
        gd::Int v = 0xC0 - (k < 4 ? 0x30 * (4 - k) : 0x30 * (k - 4));
        shades.push_back(static_cast<float>(gd::maxi(v, 0) / 255.0));
    }
    MeshArrays arrays;
    const double strips[3][2] = {{static_cast<double>(-BAND_HALF), static_cast<double>(BAND_HALF)},
                                 {-zone - LINE_WIDTH / 2, -zone + LINE_WIDTH / 2},
                                 {zone - LINE_WIDTH / 2, zone + LINE_WIDTH / 2}};
    for (const auto& strip : strips) {
        double x0 = strip[0];
        double x1 = strip[1];
        for (gd::Int j = 0; j < COURT_ROWS - 1; ++j) {
            gd::Int z0 = COURT_START + COURT_STEP * j;
            gd::Int z1 = z0 + COURT_STEP;
            gfx::Color c0(shades[j], shades[j], shades[j]);
            gfx::Color c1(shades[j + 1], shades[j + 1], shades[j + 1]);
            struct Corner { double x; double z; gfx::Color c; };
            const Corner quad[4] = {{x0, static_cast<double>(z0), c0}, {x1, static_cast<double>(z0), c0},
                                    {x0, static_cast<double>(z1), c1}, {x1, static_cast<double>(z1), c1}};
            for (int k : {0, 2, 1, 1, 2, 3}) {
                const Corner& q = quad[k];
                arrays.vertex.push_back(WorldSpace::point(q.x, -1, q.z));
                arrays.color.push_back(q.c);
            }
        }
    }
    gd::Ref<ArrayMesh> mesh = gd::make<ArrayMesh>();
    mesh->add_surface_from_arrays(Primitive::TRIANGLES, std::move(arrays));
    gd::Ref<StandardMaterial3D> m = gd::make<StandardMaterial3D>();
    m->shading_mode = StandardMaterial3D::SHADING_MODE_UNSHADED;
    m->vertex_color_use_as_albedo = true;
    m->blend_mode = StandardMaterial3D::BLEND_MODE_ADD;
    m->no_depth_test = false;
    mesh->surface_set_material(0, m);
    return mesh;
}

// After a simulation step.
void BallView::apply(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<TekkenBall> tb = sim->fight->ball;
    if (tb == nullptr) {
        visible = false;
        _snap = true;
        return;
    }
    visible = true;
    if (_court->mesh == nullptr) {
        _court->mesh = _court_mesh(tb->neutral_zone());
    }
    gd::Ref<ByteBlock> b = tb->b;
    gd::Int t = sim->fight->region->ctx8(TekkenBall::BALL_TYPE) % 3;
    if (t != _type) {
        _type = t;
        _ball->mesh = _meshes[t];
    }
    gd::Int kind = b->u8(TekkenBall::B_KIND);
    bool over = OVERLAY_KINDS.has(kind);
    if (FLAT_KINDS.has(kind)) {
        // The faces' flat grey: kind 0 fades out from black by 8 a frame, kind 1 in from 7, the
        // others white.
        gd::Int k = b->u8(TekkenBall::B_TIMER);
        gd::Int level = 0xFF;
        switch (kind) {
            case 0:
                level = (-8 * k) & 0xFF;
                break;
            case 1:
                level = (8 * k - 1) & 0xFF;
                break;
            default:
                break;
        }
        gd::Ref<StandardMaterial3D> flat = over ? _flat_overlay : _flat;
        flat->albedo_color = gfx::Color::c8(static_cast<int>(level), static_cast<int>(level), static_cast<int>(level));
        _ball->material_override = flat;
    } else {
        _ball->material_override = over ? _overlay_materials[t] : _materials[t];
    }
    gd::PI32 angles{static_cast<int32_t>(b->u16(TekkenBall::B_ANGLES)), static_cast<int32_t>(b->u16(TekkenBall::B_ANGLES + 2)),
                    static_cast<int32_t>(b->u16(TekkenBall::B_ANGLES + 4))};
    Basis rotation_basis = _psx_rotation(angles);
    double squash = static_cast<double>(b->s16(TekkenBall::B_SQUASH)) / 4096.0;
    Basis squash_basis;
    if (squash != 0.0) {
        Basis axis = _psx_rotation(gd::PI32{static_cast<int32_t>(b->u16(TekkenBall::B_SQUASH_AXIS)),
                                            static_cast<int32_t>(b->u16(TekkenBall::B_SQUASH_AXIS + 2)),
                                            static_cast<int32_t>(b->u16(TekkenBall::B_SQUASH_AXIS + 4))});
        Basis scale = from_scale(Vector3(static_cast<float>(1.0 - squash), static_cast<float>(1.0 + squash),
                                         static_cast<float>(1.0 + squash)));
        squash_basis = axis * scale * axis.transposed();
    }
    Vector3 pos = WorldSpace::point(static_cast<double>(b->s32(TekkenBall::B_POS)), static_cast<double>(b->s32(TekkenBall::B_POS + 4)),
                                    static_cast<double>(b->s32(TekkenBall::B_POS + 8)));
    Transform3D pose(squash_basis * rotation_basis, pos);
    gd::Ref<FighterState> f0 = sim->fight->fighters[0];
    gd::Ref<FighterState> f1 = sim->fight->fighters[1];
    gd::Int court_z = (f0->root_z + f1->root_z) >> 1;
    _previous = _snap ? pose : _current;
    _current = pose;
    _court_z[0] = static_cast<float>(_snap ? court_z : static_cast<gd::Int>(_court_z[1]));
    _court_z[1] = static_cast<float>(court_z);
    _snap = false;
}

// libgte RotMatrix (4096 units): R = Rx · Ry · Rz in the game's y-down frame.
Basis BallView::rot_matrix(const gd::PI32& angles) {
    return Basis::from_axis_angle(Vector3::RIGHT(), static_cast<float>(WorldSpace::radians(angles[0]))) *
           Basis::from_axis_angle(Vector3::UP(), static_cast<float>(WorldSpace::radians(angles[1]))) *
           Basis::from_axis_angle(Vector3::BACK(), static_cast<float>(WorldSpace::radians(angles[2])));
}

// rot_matrix turned into Godot's frame.
Basis BallView::_psx_rotation(const gd::PI32& angles) {
    // WorldSpace flips y and z: conjugate by that flip.
    Basis flip = from_scale(Vector3(1, -1, -1));
    return flip * rot_matrix(angles) * flip;
}

// Every rendered frame.
void BallView::show_between(double weight) {
    if (!visible) {
        return;
    }
    _ball->transform = interpolate_with(_previous, _current, weight);
    _court->set_position(WorldSpace::point(0, 0, _court_z[0] + (_court_z[1] - _court_z[0]) * weight));
}

}  // namespace t3

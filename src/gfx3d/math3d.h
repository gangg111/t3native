#pragma once
// Godot's 3D math types (single precision, as Godot's default builds): Vector3, Vector4, Basis,
// Transform3D, Projection and AABB, with the members and conventions the views use, so their code
// translates line by line. Basis stores rows (Godot's `rows[3]`); its columns are the axes
// (`get_column(i)`, Godot's `basis.x/y/z` in GDScript). Projection is column-major, as Godot's and
// OpenGL's. Right-handed, +y up, a camera looks along -z.
#include <algorithm>
#include <cmath>

namespace t3 {

struct Vector3 {
    float x = 0, y = 0, z = 0;
    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    static constexpr Vector3 ZERO() { return {0, 0, 0}; }
    static constexpr Vector3 ONE() { return {1, 1, 1}; }
    static constexpr Vector3 UP() { return {0, 1, 0}; }
    static constexpr Vector3 DOWN() { return {0, -1, 0}; }
    static constexpr Vector3 FORWARD() { return {0, 0, -1}; }
    static constexpr Vector3 BACK() { return {0, 0, 1}; }
    static constexpr Vector3 RIGHT() { return {1, 0, 0}; }

    constexpr float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vector3 operator*(const Vector3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vector3 operator/(const Vector3& o) const { return {x / o.x, y / o.y, z / o.z}; }
    constexpr Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vector3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vector3 operator-() const { return {-x, -y, -z}; }
    Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vector3& o) const { return !(*this == o); }

    constexpr float dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vector3 cross(const Vector3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    constexpr float length_squared() const { return dot(*this); }
    // Godot: a zero vector stays zero.
    Vector3 normalized() const {
        float l = length_squared();
        if (l == 0) return {};
        l = std::sqrt(l);
        return {x / l, y / l, z / l};
    }
    float distance_to(const Vector3& o) const { return (o - *this).length(); }
    constexpr Vector3 lerp(const Vector3& to, float w) const { return {x + (to.x - x) * w, y + (to.y - y) * w, z + (to.z - z) * w}; }
    Vector3 abs() const { return {std::fabs(x), std::fabs(y), std::fabs(z)}; }
};
inline constexpr Vector3 operator*(float s, const Vector3& v) { return v * s; }

struct Vector4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vector4() = default;
    constexpr Vector4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    constexpr Vector4 lerp(const Vector4& to, float t) const {
        return {x + (to.x - x) * t, y + (to.y - y) * t, z + (to.z - z) * t, w + (to.w - w) * t};
    }
    bool operator==(const Vector4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};

struct Basis {
    Vector3 rows[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    constexpr Basis() = default;
    // From the three columns (axes), as Godot's Basis(x_axis, y_axis, z_axis).
    constexpr Basis(const Vector3& cx, const Vector3& cy, const Vector3& cz)
        : rows{{cx.x, cy.x, cz.x}, {cx.y, cy.y, cz.y}, {cx.z, cy.z, cz.z}} {}

    Vector3& operator[](int r) { return rows[r]; }
    const Vector3& operator[](int r) const { return rows[r]; }
    constexpr Vector3 get_column(int i) const { return {rows[0][i], rows[1][i], rows[2][i]}; }
    void set_column(int i, const Vector3& v) { rows[0][i] = v.x; rows[1][i] = v.y; rows[2][i] = v.z; }
    // Godot's basis.x / .y / .z in GDScript.
    constexpr Vector3 x() const { return get_column(0); }
    constexpr Vector3 y() const { return get_column(1); }
    constexpr Vector3 z() const { return get_column(2); }

    constexpr Vector3 xform(const Vector3& v) const { return {rows[0].dot(v), rows[1].dot(v), rows[2].dot(v)}; }
    // The transposed product (Godot's xform_inv: the inverse for an orthonormal basis).
    constexpr Vector3 xform_inv(const Vector3& v) const {
        return {rows[0].x * v.x + rows[1].x * v.y + rows[2].x * v.z, rows[0].y * v.x + rows[1].y * v.y + rows[2].y * v.z,
                rows[0].z * v.x + rows[1].z * v.y + rows[2].z * v.z};
    }
    Vector3 operator*(const Vector3& v) const { return xform(v); }
    Basis operator*(const Basis& o) const {
        Basis r;
        for (int i = 0; i < 3; ++i)
            r.rows[i] = {rows[i].dot(o.get_column(0)), rows[i].dot(o.get_column(1)), rows[i].dot(o.get_column(2))};
        return r;
    }
    Basis transposed() const { return Basis(rows[0], rows[1], rows[2]); }
    float determinant() const { return rows[0].dot(rows[1].cross(rows[2])); }
    Basis inverse() const {
        float det = determinant();
        Vector3 c0 = rows[1].cross(rows[2]), c1 = rows[2].cross(rows[0]), c2 = rows[0].cross(rows[1]);
        float s = 1.0f / det;
        return Basis(c0 * s, c1 * s, c2 * s);
    }
    Basis scaled(const Vector3& s) const {
        Basis r = *this;
        for (int i = 0; i < 3; ++i) r.rows[i] = r.rows[i] * s[i];
        return r;
    }
    // Each column (axis) times its scale (Godot's scaled_local).
    Basis scaled_columns(const Vector3& s) const {
        Basis r = *this;
        for (int i = 0; i < 3; ++i) r.set_column(i, get_column(i) * s[i]);
        return r;
    }
    Vector3 get_scale() const { return {get_column(0).length(), get_column(1).length(), get_column(2).length()}; }
    // Godot's Basis.looking_at: -z towards `target`, +y close to `up`.
    static Basis looking_at(const Vector3& target, const Vector3& up = Vector3::UP()) {
        Vector3 vz = (-target).normalized();
        Vector3 vx = up.cross(vz).normalized();
        Vector3 vy = vz.cross(vx);
        return Basis(vx, vy, vz);
    }
    // Rotation by `angle` radians about the unit `axis` (Godot's Basis(axis, angle)).
    static Basis from_axis_angle(const Vector3& axis, float angle) {
        float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
        const Vector3& a = axis;
        Basis b;
        b.rows[0] = {t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y};
        b.rows[1] = {t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x};
        b.rows[2] = {t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c};
        return b;
    }
    bool operator==(const Basis& o) const { return rows[0] == o.rows[0] && rows[1] == o.rows[1] && rows[2] == o.rows[2]; }
};

struct Transform3D {
    Basis basis;
    Vector3 origin;
    constexpr Transform3D() = default;
    constexpr Transform3D(const Basis& b, const Vector3& o) : basis(b), origin(o) {}

    Vector3 xform(const Vector3& v) const { return basis.xform(v) + origin; }
    Vector3 operator*(const Vector3& v) const { return xform(v); }
    Transform3D operator*(const Transform3D& o) const { return {basis * o.basis, xform(o.origin)}; }
    // Godot's inverse(): for orthonormal bases (rotation and translation only).
    Transform3D inverse() const {
        Basis b = basis.transposed();
        return {b, b.xform(-origin)};
    }
    Transform3D affine_inverse() const {
        Basis b = basis.inverse();
        return {b, b.xform(-origin)};
    }
    Transform3D translated(const Vector3& offset) const { return {basis, origin + offset}; }
    // Godot's Transform3D.looking_at.
    Transform3D looking_at(const Vector3& target, const Vector3& up = Vector3::UP()) const {
        return {Basis::looking_at(target - origin, up), origin};
    }
    bool operator==(const Transform3D& o) const { return basis == o.basis && origin == o.origin; }
    bool operator!=(const Transform3D& o) const { return !(*this == o); }

    // Column-major 4x4 for GL uniforms.
    void to_gl(float out[16]) const {
        for (int c = 0; c < 3; ++c) {
            out[4 * c] = basis.rows[0][c];
            out[4 * c + 1] = basis.rows[1][c];
            out[4 * c + 2] = basis.rows[2][c];
            out[4 * c + 3] = 0;
        }
        out[12] = origin.x;
        out[13] = origin.y;
        out[14] = origin.z;
        out[15] = 1;
    }
};

// Godot's vertical fov from a horizontal one (Camera3D's KEEP_WIDTH): 2 atan(tan(fovx / 2) / aspect)
// for aspect w / h.
inline float fovy_from_fovx(float fovx_degrees, float aspect) {
    return static_cast<float>(std::atan(std::tan(fovx_degrees * 3.14159265358979 / 360.0) / aspect) * 360.0 / 3.14159265358979);
}

// A 4x4 projection, column-major (m[column][row]), as Godot's Projection.
struct Projection {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    // Godot's set_perspective(fovy_degrees, aspect, near, far, flip_fov): with flip_fov the angle is
    // the horizontal one (Camera3D.KEEP_WIDTH).
    static Projection perspective(float fov_degrees, float aspect, float z_near, float z_far, bool flip_fov) {
        // Godot: fovy = get_fovy(fovx, 1.0 / aspect) = 2 atan(tan(fovx / 2) / aspect).
        if (flip_fov) fov_degrees = fovy_from_fovx(fov_degrees, aspect);
        Projection p;
        float rad = fov_degrees / 2.0f * 3.14159265358979f / 180.0f;
        float dz = z_far - z_near, s = std::sin(rad);
        float cot = std::cos(rad) / s;
        p.m[0][0] = cot / aspect;
        p.m[1][1] = cot;
        p.m[2][2] = -(z_far + z_near) / dz;
        p.m[2][3] = -1;
        p.m[3][2] = -2 * z_near * z_far / dz;
        p.m[3][3] = 0;
        p.m[0][1] = p.m[0][2] = p.m[0][3] = p.m[1][0] = p.m[1][2] = p.m[1][3] = p.m[2][0] = p.m[2][1] = p.m[3][0] =
            p.m[3][1] = 0;
        return p;
    }
    // glFrustum's matrix (Godot's Projection::set_frustum with explicit planes).
    static Projection frustum(float left, float right, float bottom, float top, float z_near, float z_far) {
        Projection p;
        p.m[0][0] = 2 * z_near / (right - left);
        p.m[1][1] = 2 * z_near / (top - bottom);
        p.m[2][0] = (right + left) / (right - left);
        p.m[2][1] = (top + bottom) / (top - bottom);
        p.m[2][2] = -(z_far + z_near) / (z_far - z_near);
        p.m[2][3] = -1;
        p.m[3][2] = -2 * z_far * z_near / (z_far - z_near);
        p.m[3][3] = 0;
        return p;
    }
    bool is_orthogonal() const { return m[3][3] == 1.0f; }
    static Projection orthogonal(float left, float right, float bottom, float top, float z_near, float z_far) {
        Projection p;
        p.m[0][0] = 2 / (right - left);
        p.m[1][1] = 2 / (top - bottom);
        p.m[2][2] = -2 / (z_far - z_near);
        p.m[3][0] = -(right + left) / (right - left);
        p.m[3][1] = -(top + bottom) / (top - bottom);
        p.m[3][2] = -(z_far + z_near) / (z_far - z_near);
        return p;
    }
    // The clip-space position of a view-space point (x, y, z, w).
    Vector4 xform(const Vector4& v) const {
        Vector4 r;
        for (int row = 0; row < 4; ++row)
            r[row] = m[0][row] * v.x + m[1][row] * v.y + m[2][row] * v.z + m[3][row] * v.w;
        return r;
    }
    const float* gl() const { return &m[0][0]; }
};

struct AABB {
    Vector3 position;
    Vector3 size;
    constexpr AABB() = default;
    constexpr AABB(const Vector3& p, const Vector3& s) : position(p), size(s) {}
    Vector3 end() const { return position + size; }
    Vector3 get_center() const { return position + size * 0.5f; }
    bool has_volume() const { return size.x > 0 && size.y > 0 && size.z > 0; }
    AABB expand(const Vector3& p) const {
        Vector3 b = position, e = end();
        b = {std::min(b.x, p.x), std::min(b.y, p.y), std::min(b.z, p.z)};
        e = {std::max(e.x, p.x), std::max(e.y, p.y), std::max(e.z, p.z)};
        return {b, e - b};
    }
    AABB merge(const AABB& o) const { return expand(o.position).expand(o.end()); }
    AABB grow(float by) const { return {position - Vector3(by, by, by), size + Vector3(2 * by, 2 * by, 2 * by)}; }
    Vector3 get_endpoint(int i) const {
        return {(i & 1) ? position.x + size.x : position.x, (i & 2) ? position.y + size.y : position.y,
                (i & 4) ? position.z + size.z : position.z};
    }
    AABB transformed(const Transform3D& t) const {
        AABB r(t.xform(get_endpoint(0)), {});
        for (int i = 1; i < 8; ++i) r = r.expand(t.xform(get_endpoint(i)));
        return r;
    }
};

}  // namespace t3

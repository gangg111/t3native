// remake/presentation/camera/camera_rig.gd
// Shows the simulation's camera (CameraView) with a Godot camera, for any window shape.
#include "presentation/camera/camera_rig.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/camera/camera_view.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double TAU = 2.0 * PI;
constexpr double INF = std::numeric_limits<double>::infinity();

// Godot's fposmod.
double fposmod(double x, double y) {
    double v = std::fmod(x, y);
    if ((v < 0.0 && y > 0.0) || (v > 0.0 && y < 0.0)) v += y;
    return v;
}

// Godot's lerp_angle.
double lerp_angle(double from, double to, double weight) {
    double difference = std::fmod(to - from, TAU);
    double distance = std::fmod(2.0 * difference, TAU) - difference;
    return from + distance * weight;
}

double lerpf(double a, double b, double w) { return a + (b - a) * w; }

}  // namespace

CameraRig::CameraRig() {
    camera = gd::make<Camera3D>();
    _ready();
}

void CameraRig::_ready() {
    camera->near = static_cast<float>(NEAR);
    camera->far = static_cast<float>(FAR);
    add_child(camera);
}

// A new simulation step's camera; `snap` drops interpolation (cuts).
void CameraRig::set_view(const gd::Ref<CameraView>& view, bool snap) {
    _previous = (snap || _current == nullptr) ? view : _current;
    _current = view;
}

// Places the camera between the last two steps (`weight` 0 -> previous, 1 -> current).
void CameraRig::place(double weight, gfx::Vec2 window) {
    if (_current == nullptr) {
        return;
    }
    gd::Ref<CameraView> a = _previous;
    gd::Ref<CameraView> b = _current;
    double pitch = lerp_angle(WorldSpace::radians(a->pitch), WorldSpace::radians(b->pitch), weight);
    double yaw = lerp_angle(WorldSpace::radians(a->yaw), WorldSpace::radians(b->yaw), weight);
    Vector3 eye = WorldSpace::point(lerpf(static_cast<double>(a->x), static_cast<double>(b->x), weight),
                                    lerpf(static_cast<double>(a->y), static_cast<double>(b->y), weight),
                                    lerpf(static_cast<double>(a->z), static_cast<double>(b->z), weight));
    camera->transform = Transform3D(view_basis(pitch, yaw), eye);
    // Native: Godot's camera reads the viewport's size; the views project through it (FightView's
    // backdrop, the items) before the renderer sets it.
    camera->viewport_size = window;
    _project(lerpf(static_cast<double>(a->h), static_cast<double>(b->h), weight), pitch, yaw, window);
}

// Camera-to-world basis in Godot space: S·M^T·S with M = Rx(-pitch)·Ry(-yaw), S = diag(1, -1, -1).
Basis CameraRig::view_basis(double pitch, double yaw) {
    float cy = static_cast<float>(std::cos(-yaw));
    float sy = static_cast<float>(std::sin(-yaw));
    float cx = static_cast<float>(std::cos(-pitch));
    float sx = static_cast<float>(std::sin(-pitch));
    Basis y(Vector3(cy, 0, sy), Vector3(0, 1, 0), Vector3(-sy, 0, cy));   // columns
    Basis x(Vector3(1, 0, 0), Vector3(0, cx, -sx), Vector3(0, sx, cx));
    Basis m = x * y;
    Basis s(Vector3(1, 0, 0), Vector3(0, -1, 0), Vector3(0, 0, -1));
    return s * m.transposed() * s;
}

void CameraRig::_project(double h, double pitch, double yaw, gfx::Vec2 window) {
    double tan_x = HALF_WIDTH / h;
    double tan_y = HALF_HEIGHT / h;
    frame_tan = gfx::Vec2(static_cast<float>(tan_x), static_cast<float>(tan_y));
    double aspect = window.x / std::max(static_cast<double>(window.y), 1.0);
    double base = tan_x / tan_y;
    if (aspect >= base) {
        // Wider than 4:3: extend sideways, at most to 21:9.
        camera->projection = Camera3D::PROJECTION_PERSPECTIVE;
        camera->keep_aspect = Camera3D::KEEP_HEIGHT;
        camera->fov = static_cast<float>(2.0 * std::atan(tan_y) * 180.0 / PI);
        double shown = std::min(aspect, MAX_ASPECT) / aspect;
        visible_rect = gfx::Rect2(static_cast<float>((1.0 - shown) / 2.0), 0, static_cast<float>(shown), 1);
        double frame_width = base / aspect;
        frame_rect = gfx::Rect2(static_cast<float>((1.0 - frame_width) / 2.0), 0, static_cast<float>(frame_width), 1);
    } else {
        // Taller than 4:3: upwards as far as the backdrop covers, downwards over the floor
        // for the rest of the window (an off-centre frustum).
        double window_tan = tan_x / aspect;
        double up = std::clamp(_covered_tan(pitch, yaw, tan_x, tan_y), tan_y, window_tan);
        double down = 2.0 * window_tan - up;
        camera->projection = Camera3D::PROJECTION_FRUSTUM;
        camera->keep_aspect = Camera3D::KEEP_WIDTH;
        camera->size = static_cast<float>(2.0 * tan_x * NEAR);
        camera->frustum_offset = gfx::Vec2(0, static_cast<float>((up - down) / 2.0 * NEAR));
        visible_rect = gfx::Rect2(0, 0, 1, 1);
        double span = up + down;
        frame_rect = gfx::Rect2(0, static_cast<float>((up - tan_y) / span), 1, static_cast<float>(2.0 * tan_y / span));
    }
}

// The coverage direction the camera faces.
gd::Int CameraRig::coverage_index(double yaw, gd::Int count) {
    return static_cast<gd::Int>(std::round(coverage_angle(yaw) / TAU * static_cast<double>(count))) % count;
}

// θ of the direction the camera faces, 0 to 2π.
double CameraRig::coverage_angle(double yaw) {
    Vector3 forward = -view_basis(0.0, yaw).z();
    return fposmod(std::atan2(static_cast<double>(forward.z), static_cast<double>(forward.x)), TAU);
}

// The covered elevation (degrees) at direction position f (θ·count/2π), linear between the
// stage's directions.
double CameraRig::_coverage_at(double f) {
    gd::Int count = coverage.size();
    gd::Int k = static_cast<gd::Int>(std::floor(f));
    return lerpf(static_cast<double>(gd::Int(coverage[gd::posmod(k, count)])),
                 static_cast<double>(gd::Int(coverage[gd::posmod(k + 1, count)])), f - static_cast<double>(k));
}

// How far above the view centre (in view-plane units) the backdrop still covers.
double CameraRig::_covered_tan(double pitch, double yaw, double side_tan, double frame_tan) {
    if (coverage.is_empty()) {
        return INF;
    }
    // The minimum of the piecewise-linear coverage over the span: its ends and the directions
    // inside it.
    double per_radian = static_cast<double>(coverage.size()) / TAU;
    double centre = coverage_angle(yaw) * per_radian;
    double half = std::atan(side_tan) * per_radian;
    double lowest = std::min(_coverage_at(centre - half), _coverage_at(centre + half));
    gd::Int k_from = static_cast<gd::Int>(std::ceil(centre - half));
    gd::Int k_to = static_cast<gd::Int>(std::floor(centre + half)) + 1;
    for (gd::Int k = k_from; k < k_to; ++k) {
        lowest = std::min(lowest, static_cast<double>(gd::Int(coverage[gd::posmod(k, coverage.size())])));
    }
    double limit = lowest * PI / 180.0;
    if (limit >= PI / 2.0 - 0.001) {
        return INF;
    }
    double room = limit + pitch;            // angle between the view axis and the covered top
    return std::max(std::tan(std::clamp(room, 0.0, PI / 2.0 - 0.01)), frame_tan);
}

}  // namespace t3

// remake/presentation/stage/tiled_panorama.gd
// The screen-space backdrops of the tile-map stages (Tekken Ball, Tekken Force), see the header.
#include "presentation/stage/tiled_panorama.h"

#include <cmath>

#include "content/fight_tables.h"
#include "content/json_file.h"
#include "content/stage_data.h"
#include "content/texture_packs.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_view.h"
#include "core/camera/view_matrix.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/round_state.h"
#include "core/force/tekken_force.h"
#include "core/game/mode_region.h"
#include "core/game/mode_rules.h"
#include "core/math/fx.h"
#include "core/math/gte.h"
#include "gd/io.h"
#include "presentation/hud/hud_frame.h"
#include "presentation/screens/screen_canvas.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

// Vector2 * Vector2 (per component).
gfx::Vec2 _mul(gfx::Vec2 a, gfx::Vec2 b) { return gfx::Vec2(a.x * b.x, a.y * b.y); }

// Rect2.grow(by).
gfx::Rect2 _grow(const gfx::Rect2& r, float by) {
    return gfx::Rect2(r.position.x - by, r.position.y - by, r.size.x + 2 * by, r.size.y + 2 * by);
}

// Vector2.normalized() in single precision (a zero vector stays zero).
gfx::Vec2 _normalized(gfx::Vec2 v) {
    float l = v.x * v.x + v.y * v.y;
    if (l == 0) return v;
    l = std::sqrt(l);
    return gfx::Vec2(v.x / l, v.y / l);
}

// Godot's floori / ceili / floorf / signf / lerpf on floats.
gd::Int floori(double v) { return static_cast<gd::Int>(std::floor(v)); }
gd::Int ceili(double v) { return static_cast<gd::Int>(std::ceil(v)); }
double signf(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }
double lerpf(double from, double to, double weight) { return from + (to - from) * weight; }

}  // namespace

void TiledPanorama::setup(const gd::Ref<StageData>& stage, const gd::Ref<FightTables>& tables) {
    _tables = tables;
    const gd::Json& map = stage->tile_map;
    picture = TexturePacks::texture(gd::path_join(stage->directory, map["picture"].str()));
    tile = JsonFile::number(map["tile"]);
    map_width = JsonFile::number(map["width"]);
    map_height = JsonFile::number(map["height"]);
    ball = stage->number == ModeRules::TEKKEN_BALL_STAGE;
    level = stage->number - ModeRules::FORCE_FIRST_STAGE;
    // set_anchors_and_offsets_preset(PRESET_FULL_RECT) and MOUSE_FILTER_IGNORE: the app draws it
    // over the whole window (draw's `window`) and it takes no input.
}

// After a simulation step: the scroll and the map's top edge of this frame.
void TiledPanorama::step(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<CameraView> view = sim->camera->view;
    gd::Ref<ViewMatrix> m = ViewMatrix::build(view, _tables);
    gd::PI32 eye = Gte::apply_lv(m->rot, gd::PI32{view->x, view->y, view->z});
    gd::Int scroll = 0;
    gd::Int top = 0;
    gd::Ref<FightState> fight = sim->fight;
    if (ball) {
        gd::Int kind = fight->region->ctx32(TekkenBall::LAST_POINT);
        shown = kind != 1;
        if (kind != _last_kind) _yaw_ref = view->yaw;
        _last_kind = kind;
        gd::Int yaw = view->yaw & 0xFFF;
        gd::Int c = FightMath::cos12(yaw, _tables);
        gd::Int s = FightMath::sin12(yaw, _tables);
        gd::Int e0 = eye[0];
        gd::Int e2 = eye[2];
        scroll = (Fx::div_trunc(Fx::w32(e0 * c + e2 * s), BALL_SCROLL_DIV) >> 12) + (Fx::w32(_yaw_ref - view->yaw) >> 1);
        gd::PI32 horizon{Fx::w32(view->x - s * BALL_REACH) >> 12, BALL_HORIZON_Y, Fx::w32(view->z + c * BALL_REACH) >> 12};
        top = _screen_y(m, horizon) - map_height * tile;
        gfx::Vec2 forward = _normalized(gfx::Vec2(static_cast<float>(-s), static_cast<float>(-c)));
        // Vector2(...) / UNITS_PER_METRE: each component divided in single precision.
        gfx::Vec2 at(static_cast<float>(gd::Int(horizon[0])) / static_cast<float>(WorldSpace::UNITS_PER_METRE),
                     static_cast<float>(-gd::Int(horizon[2])) / static_cast<float>(WorldSpace::UNITS_PER_METRE));
        floor_edge = Vector3(forward.x, forward.y, forward.x * at.x + forward.y * at.y);
        _shift = 0;
        if (fight->round_state == RoundState::RESULT && kind != 0) {
            _shift = 14;
        } else if (fight->round_state >= RoundState::REPLAY) {
            _shift = 8;
        }
    } else {
        scroll = Fx::div_trunc(eye[0], FORCE_SCROLL_DIV);
        top = _screen_y(m, gd::PI32{view->x, 0, FORCE_HORIZON_Z}) - map_height * tile;
        floor_edge = Vector3(0, -1, static_cast<float>(FORCE_HORIZON_Z / WorldSpace::UNITS_PER_METRE));
        _tile_only = fight->force != nullptr && fight->force->gu32(TekkenForce::TILE_ONLY) != 0;
        shown = true;
    }
    gd::Int period = map_width * tile;
    scroll = gd::posmod(scroll, period);
    if (!_started) {
        _scroll[1] = static_cast<float>(scroll);
        _top[1] = static_cast<float>(top);
        _started = true;
    }
    _scroll[0] = _scroll[1];
    _top[0] = _top[1];
    // A jump of more than half the map is a wrap: continue from the nearer image.
    double d = static_cast<double>(scroll) - _scroll[0];
    if (std::fabs(d) > period / 2.0) _scroll[0] = static_cast<float>(_scroll[0] + period * signf(d));
    _scroll[1] = static_cast<float>(scroll);
    _top[1] = static_cast<float>(top);
}

gd::Int TiledPanorama::_screen_y(const gd::Ref<ViewMatrix>& m, const gd::PI32& point) {
    gd::PI32 v = Gte::apply_lv(m->rot, point);
    gd::PI32 tr{Fx::w32(v[0] + m->t[0]), Fx::w32(v[1] + m->t[1]), Fx::w32(v[2] + m->t[2])};
    return Fx::s16(Gte::rtps_origin(tr, m->h, ViewMatrix::SCREEN_OFFSET_X, ViewMatrix::SCREEN_OFFSET_Y)[1]);
}

// Every rendered frame: between the last two steps.
void TiledPanorama::show_between(double weight, const gfx::Rect2& frame) {
    _weight = weight;
    frame_rect = frame;
    // queue_redraw(): drawn every frame.
}

gfx::Vec2 TiledPanorama::_point(double x, double y) const {
    gfx::Rect2 box(_mul(frame_rect.position, size), _mul(frame_rect.size, size));
    return HudFrame::point(box, static_cast<float>(x), static_cast<float>(y));
}

gfx::Rect2 TiledPanorama::_rect(double x, double y, double w, double h) const {
    gfx::Vec2 a = _point(x, y);
    return gfx::Rect2(a, _point(x + w, y + h) - a);
}

// The window's left and right edges in frame-buffer units.
gfx::Vec2 TiledPanorama::_extent() const {
    gfx::Rect2 box(_mul(frame_rect.position, size), _mul(frame_rect.size, size));
    if (box.size.x <= 0) return gfx::Vec2(HudFrame::LEFT, HudFrame::LEFT + ScreenCanvas::SCREEN().x);
    double unit = static_cast<double>(ScreenCanvas::SCREEN().x) / box.size.x;
    return gfx::Vec2(static_cast<float>(HudFrame::LEFT - box.position.x * unit),
                     static_cast<float>(HudFrame::LEFT + (size.x - box.position.x) * unit));
}

void TiledPanorama::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    size = window;
    _canvas = &canvas;
    _draw();
    _canvas = nullptr;
}

void TiledPanorama::_draw() {
    if (picture == nullptr || !shown) return;
    double scroll = lerpf(_scroll[0], _scroll[1], _weight);
    double top = lerpf(_top[0], _top[1], _weight);
    gfx::Vec2 span = _extent();
    if (ball) {
        _draw_ball(scroll, top, span);
    } else {
        _draw_force(scroll, top, span);
    }
}

void TiledPanorama::_draw_ball(double scroll, double top, gfx::Vec2 span) {
    // The sky from the display's top down to the map's top edge (the whole screen when the
    // map is below it).
    double sky_bottom = std::fmin(top, static_cast<double>(SCREEN_HEIGHT)) + SCREEN_TOP;
    _canvas->draw_rect(_rect(span.x, 0, span.y - span.x, sky_bottom), BALL_SKY);
    // Screen x shows map pixel scroll + 32 * (first column) + x.
    double origin = scroll + static_cast<double>(tile * (BALL_FIRST + _shift));
    _draw_rows(origin, top, span);
}

void TiledPanorama::_draw_force(double scroll, double top, gfx::Vec2 span) {
    bool sloped = level == FORCE_SLOPE_LEVEL;
    if (sloped || _tile_only) {
        gfx::Rect2 ground = FORCE_GROUND;
        if (_tile_only) {
            ground = gfx::Rect2(0, FORCE_GROUND.position.y, ScreenCanvas::SCREEN().x,
                                static_cast<float>(top + static_cast<double>(map_height * tile) - 0x10));
            _canvas->draw_rect(_rect(span.x, ground.position.y, span.y - span.x, ground.size.y), gfx::Color::black());
            return;
        }
        _canvas->draw_rect(_rect(ground.position.x, ground.position.y, ground.size.x, ground.size.y), gfx::Color::black());
    }
    if (!sloped) {
        _draw_rows(scroll, top, span);
        return;
    }
    // The fourth level climbs: every third column rises by 192 * tan(slope), from a phase that
    // keeps the steps in place on the map.
    double angle = WorldSpace::radians(FORCE_SLOPE);
    double rise = 192.0 * std::tan(angle);
    double phase = 0xB8 - (std::floor(scroll / 192.0) * 192.0 - scroll);
    double y0 = top + phase * std::tan(angle);
    double t = static_cast<double>(tile);
    gd::Int first = floori((span.x - (std::floor(scroll / t) * t - scroll)) / t) - 1;
    gd::Int last = ceili((span.y - (std::floor(scroll / t) * t - scroll)) / t) + 1;
    gd::Int column0 = floori(scroll / t);
    double x0 = static_cast<double>(column0 * tile) - scroll;
    for (gd::Int row = 0; row < map_height; ++row) {
        for (gd::Int c = first; c < last; ++c) {
            gd::Int map_col = column0 + c;
            gd::Int steps = floori(static_cast<double>(map_col) / 3.0) - floori(static_cast<double>(column0) / 3.0);
            double y = y0 + static_cast<double>(row * tile) - static_cast<double>(steps) * rise;
            double x = x0 + static_cast<double>(c * tile);
            _draw_cell(gd::posmod(map_col, map_width), row, x, y);
            if (row == map_height - 1 && gd::posmod(map_col, map_width) == 0)
                _canvas->draw_rect(_rect(x, y + 0x40, FORCE_GATE.x, FORCE_GATE.y), gfx::Color::black());
        }
    }
}

// Straight rows: screen x shows map pixel origin + x.
void TiledPanorama::_draw_rows(double origin, double top, gfx::Vec2 span) {
    double t = static_cast<double>(tile);
    gd::Int column0 = floori(origin / t);
    double x0 = static_cast<double>(column0 * tile) - origin;
    gd::Int first = floori((span.x - x0) / t);
    gd::Int last = ceili((span.y - x0) / t);
    for (gd::Int row = 0; row < map_height; ++row) {
        double y = top + static_cast<double>(row * tile);
        if (y < -t || y > ScreenCanvas::SCREEN().y) continue;
        for (gd::Int c = first; c < last; ++c)
            _draw_cell(gd::posmod(column0 + c, map_width), row, x0 + static_cast<double>(c * tile), y);
    }
}

void TiledPanorama::_draw_cell(gd::Int column, gd::Int row, double x, double y) {
    gfx::Rect2 source(static_cast<float>(column * tile), static_cast<float>(row * tile), static_cast<float>(tile),
                      static_cast<float>(tile));
    // Half a texel of overlap hides the seams of the scaled tiles.
    gfx::Rect2 r = _grow(_rect(x, y, static_cast<double>(tile), static_cast<double>(tile)), 0.5f);
    _canvas->draw_texture_rect_region(picture, r, source);
}

}  // namespace t3

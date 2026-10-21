// remake/presentation/hud/touch_controls.gd
#include "presentation/hud/touch_controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/game/options_screen.h"
#include "gd/io.h"
#include "gd/json.h"
#include "gd/json_writer.h"
#include "platform/input_router.h"
#include "platform/log.h"
#include "platform/pad_state.h"
#include "presentation/hud/button_glyphs.h"

namespace t3 {

namespace {

constexpr float TAU = 6.28318530717958648f;

// Godot's Vector2 / Rect2 helpers the GDScript uses.
bool is_zero(gfx::Vec2 v) { return v.x == 0.0f && v.y == 0.0f; }
bool is_inf(gfx::Vec2 v) { return std::isinf(v.x) || std::isinf(v.y); }
float length(gfx::Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
float distance(gfx::Vec2 a, gfx::Vec2 b) { return length(a - b); }
gfx::Vec2 centre_of(const gfx::Rect2& r) { return r.position + r.size * 0.5f; }
bool has_point(const gfx::Rect2& r, gfx::Vec2 p) {
    return p.x >= r.position.x && p.y >= r.position.y && p.x < r.position.x + r.size.x && p.y < r.position.y + r.size.y;
}
gfx::Rect2 grow(const gfx::Rect2& r, float by) {
    return gfx::Rect2(r.position - gfx::Vec2(by, by), r.size + gfx::Vec2(2.0f * by, 2.0f * by));
}
gfx::Rect2 intersection(const gfx::Rect2& a, const gfx::Rect2& b) {
    float x0 = std::max(a.position.x, b.position.x), y0 = std::max(a.position.y, b.position.y);
    float x1 = std::min(a.end().x, b.end().x), y1 = std::min(a.end().y, b.end().y);
    if (x1 <= x0 || y1 <= y0) return gfx::Rect2();
    return gfx::Rect2(x0, y0, x1 - x0, y1 - y0);
}
gfx::Vec2 limit_length(gfx::Vec2 v, float n) {
    float l = length(v);
    return l > n && l > 0.0f ? v * (n / l) : v;
}
gfx::Color with_alpha(gfx::Color c, float a) {
    c.a = a;
    return c;
}
float clamp_scale(float s) { return std::clamp(s, TouchControls::MIN_SCALE, TouchControls::MAX_SCALE); }

}  // namespace

const gd::Arr<gd::Int> TouchControls::FACE{PadState::TRIANGLE, PadState::SQUARE, PadState::CIRCLE, PadState::CROSS};
const gd::Arr<gfx::Vec2> TouchControls::FACE_OFFSETS{gfx::Vec2(0, -1), gfx::Vec2(-1, 0), gfx::Vec2(1, 0), gfx::Vec2(0, 1)};
const gd::Dict<std::string, gd::Arr<gd::Int>> TouchControls::MACRO_SETS = [] {
    gd::Dict<std::string, gd::Arr<gd::Int>> d;
    d.set("none", gd::Arr<gd::Int>{});
    d.set("throws", gd::Arr<gd::Int>{PadState::SQUARE | PadState::CROSS, PadState::TRIANGLE | PadState::CIRCLE});
    d.set("all", gd::Arr<gd::Int>{PadState::SQUARE | PadState::CROSS, PadState::TRIANGLE | PadState::CIRCLE,
                                  PadState::SQUARE | PadState::TRIANGLE, PadState::CROSS | PadState::CIRCLE});
    return d;
}();
const gd::Dict<gd::Int, std::string> TouchControls::LIMB_NAMES = [] {
    gd::Dict<gd::Int, std::string> d;
    d.set(PadState::SQUARE, "1");
    d.set(PadState::TRIANGLE, "2");
    d.set(PadState::CROSS, "3");
    d.set(PadState::CIRCLE, "4");
    return d;
}();
const gd::Arr<gd::Arr<gd::Int>> TouchControls::SHOULDERS{gd::Arr<gd::Int>{PadState::L1, PadState::L2},
                                                          gd::Arr<gd::Int>{PadState::R2, PadState::R1}};
const gd::Arr<gd::Int> TouchControls::DIRECTIONS{PadState::RIGHT, PadState::RIGHT | PadState::DOWN, PadState::DOWN,
                                                 PadState::DOWN | PadState::LEFT, PadState::LEFT, PadState::LEFT | PadState::UP,
                                                 PadState::UP, PadState::UP | PadState::RIGHT};

TouchControls::TouchControls()
    : shoulders(PadState::L1 | PadState::L2 | PadState::R1 | PadState::R2), font(gfx::Font::fallback()) {}

// Every finger is let go when the app or window loses the focus or is paused: the lifts that
// happen meanwhile are never delivered, and a stale stick finger would keep the stick for good.
void TouchControls::_notification(gd::Int what) {
    switch (what) {
        case NOTIFICATION_APPLICATION_FOCUS_OUT:
        case NOTIFICATION_APPLICATION_PAUSED:
        case NOTIFICATION_WM_WINDOW_FOCUS_OUT: reset(); break;
        default: break;
    }
}

void TouchControls::set_macros(const std::string& set_name) {
    custom.clear();
    gd::Arr<gd::Int> set = MACRO_SETS.get(set_name, gd::Arr<gd::Int>{});
    for (gd::Int i = 0; i < set.size(); ++i) custom.push_back(CustomButton{i + 1, {set[i]}, DEFAULT_GAP, default_label(set[i])});
    layout();
}

// Only Select and Start while something plays; every control otherwise. The fingers down are let
// go on a change (a stick or button held would otherwise stay pressed).
void TouchControls::set_playback(bool on) {
    if (on != playback) {
        playback = on;
        reset();
        layout();
    }
}

// The shoulder buttons to show (pad bits): those player 1's key configuration gives an action (by
// default the game leaves all four unused), all four on the KEY CONFIGURATION page itself, where
// an unused one has to be pressed to be given an action.
gd::Int TouchControls::shoulders_shown(const GameFlow& flow) {
    if (flow.state == GameFlow::State::OPTIONS && flow.options->page == OptionsScreen::PAGE_KEYS) {
        return PadState::L1 | PadState::L2 | PadState::R1 | PadState::R2;
    }
    gd::Int bits = 0;
    for (gd::Int button : {0, 1, 2, 3}) {   // L2 R2 L1 R1: pad bits 1 << button
        if (flow.progress->button_assigned(0, button)) bits |= gd::Int(1) << button;
    }
    return bits;
}

// Shows only these shoulder buttons (pad bits); the others keep their places empty.
void TouchControls::set_shoulders(gd::Int bits_shown) {
    if (bits_shown != shoulders) {
        shoulders = bits_shown;
        layout();
    }
}

// The controls' size as a share of the default one.
void TouchControls::set_scale_share(float share) {
    scale_share = share;
    layout();
}

// Places the controls for the window's size and safe area.
void TouchControls::layout() {
    _select_screen(false);
    gfx::Rect2 area = safe_area();
    _unit = std::max(MIN_UNIT, std::min(area.size.x, area.size.y) * UNIT_SHARE * scale_share);
    float u = _unit;
    _controls.clear();
    gfx::Vec2 centre = area.end() - gfx::Vec2(3.1f, 3.1f) * u;
    gd::Int n_face = FACE.size();
    for (gd::Int i = 0; i < n_face; ++i) {
        auto c = gd::make<Control2>();
        c->bits = FACE[i];
        c->centre = centre + FACE_OFFSETS[i] * BUTTON_SPREAD * u;
        c->radius = BUTTON_RADIUS * u;
        _controls.append(c);
    }
    float r = MACRO_RADIUS * u;
    gd::Int left = 0;   // the own buttons in the top left corner
    gd::Int n_custom = static_cast<gd::Int>(custom.size());
    for (gd::Int i = 0; i < n_custom; ++i) {
        // In the top corners, alternating: the first one on the left, the next on the right.
        auto c = gd::make<Control2>();
        c->bits = custom[i].bits();
        c->custom_id = custom[i].id;
        c->radius = r;
        float offset = EDGE * u + r + static_cast<float>(i / 2) * (2.0f * r + GAP * u);
        c->centre = gfx::Vec2(i % 2 == 0 ? area.position.x + offset : area.end().x - offset, area.position.y + EDGE * u + r);
        if (i % 2 == 0) left += 1;
        c->label = custom[i].label.empty() ? default_label(custom[i].steps) : custom[i].label;
        _controls.append(c);
    }
    gfx::Vec2 system = SYSTEM_SIZE * u;
    float bottom = area.end().y - (EDGE * u + system.y);
    // At the bottom in the middle, clear of the HUD's bars and timer; moved left where the Cross
    // button's touch area would reach Start's (4:3 screens at the larger sizes).
    float pair = 2.0f * system.x + GAP * u;
    float clear = centre.x - (BUTTON_RADIUS * HIT_SLACK + RECT_SLACK + GAP / 2.0f) * u;   // left of Cross's touch area
    float pair_left = std::min(centre_of(area).x - pair / 2.0f, clear - pair);
    for (gd::Int i = 0; i < 2; ++i) {
        float x = pair_left + static_cast<float>(i) * (system.x + GAP * u);
        _add_rect(i == 0 ? PadState::SELECT : PadState::START, gfx::Rect2(gfx::Vec2(x, bottom), system),
                  i == 0 ? "SELECT" : "START");
    }
    gfx::Vec2 shoulder = SHOULDER_SIZE * u;
    // Layout edit: every shoulder button can be placed while editing.
    gd::Int shown = editing ? (PadState::L1 | PadState::L2 | PadState::R1 | PadState::R2) : shoulders;
    // Over Triangle, their touch areas clear of its own (a rectangle's touch is taken first).
    float y = centre.y - (BUTTON_SPREAD + BUTTON_RADIUS * HIT_SLACK + RECT_SLACK + GAP / 2.0f) * u - shoulder.y;
    for (gd::Int side = 0; side < 2; ++side) {
        float middle = side == 0 ? area.position.x + STICK_HINT * u : centre.x;
        for (gd::Int i = 0; i < 2; ++i) {
            float x = i == 0 ? middle - GAP * u / 2.0f - shoulder.x : middle + GAP * u / 2.0f;
            gd::Int bit = SHOULDERS[side][i];
            if ((shown & bit) == 0) continue;
            _add_rect(bit, gfx::Rect2(gfx::Vec2(x, y), shoulder), PadState::NAMES.at(bit));
        }
    }
    top_left_free = gfx::Rect2(area.position + gfx::Vec2(EDGE * u + static_cast<float>(left) * (2.0f * r + GAP * u), EDGE * u),
                               gfx::Vec2(0, 2.0f * r));
    float top = (EDGE + GAP) * u + 2.0f * r;
    _stick_zone = gfx::Rect2(area.position + gfx::Vec2(0, top), gfx::Vec2(area.size.x * 0.5f, area.size.y - top));
    // Layout edit: the stick's rest (the hint _draw puts there) and the edit's buttons in the top
    // row's middle, then the placements over the remake's layout.
    _stick_hint = gfx::Vec2(_stick_zone.position.x + STICK_HINT * u, _stick_zone.end().y - STICK_HINT * u);
    _stick_scale = 1.0f;
    float row_y = area.position.y + EDGE * u + r - system.y / 2.0f;
    _reset_rect = gfx::Rect2(gfx::Vec2(centre_of(area).x - pair / 2.0f, row_y), system);
    _done_rect = gfx::Rect2(gfx::Vec2(centre_of(area).x + GAP * u / 2.0f, row_y), system);
    // Below them, − and + at the pair's ends with the selected control's size between.
    float step_y = row_y + system.y + GAP * u;
    _minus_rect = gfx::Rect2(gfx::Vec2(_reset_rect.position.x, step_y), gfx::Vec2(system.y, system.y));
    _plus_rect = gfx::Rect2(gfx::Vec2(_done_rect.end().x - system.y, step_y), gfx::Vec2(system.y, system.y));
    // ADD left of Reset; EDIT right of Done (for a selected own button).
    _new_rect = gfx::Rect2(gfx::Vec2(_reset_rect.position.x - GAP * u - system.x, row_y), system);
    _change_rect = gfx::Rect2(gfx::Vec2(_done_rect.end().x + GAP * u, row_y), system);
    _apply_placements(area);
    if (playback && !editing) {   // Layout edit: every control placeable over a movie too
        gd::Arr<gd::Ref<Control2>> kept;
        for (const auto& c : _controls) {
            if ((c->bits & (PadState::SELECT | PadState::START)) != 0) kept.append(c);
        }
        _controls = kept;
        _stick_zone = gfx::Rect2();
    }
    _rebind_fingers();
}

// The fingers on controls after a layout: each holds the new control of its buttons, and lets go
// when there is none (a shoulder button no longer shown).
void TouchControls::_rebind_fingers() {
    bool changed = false;
    for (gd::Int index : _fingers.keys()) {
        Finger& f = _fingers.at(index);
        if (f.stick) continue;
        gd::Ref<Control2> old = f.control;
        gd::Ref<Control2> now = nullptr;
        for (const auto& c : _controls) {
            if (c->bits == old->bits && is_zero(c->rect.size) == is_zero(old->rect.size)) {
                now = c;
                break;
            }
        }
        if (now == nullptr) {
            _fingers.erase(index);
        } else {
            f.control = now;
        }
        changed = true;
    }
    if (changed) _update();
}

void TouchControls::_add_rect(gd::Int pad_bits, gfx::Rect2 rect, const std::string& label) {
    auto c = gd::make<Control2>();
    c->bits = pad_bits;
    c->rect = rect;
    c->label = label;
    _controls.append(c);
}

// The display's safe area in this control's coordinates (the whole control where unknown). The
// platform reports the insets in window pixels (Godot: DisplayServer.get_display_safe_area on mobile).
gfx::Rect2 TouchControls::safe_area() const {
    gfx::Rect2 full(gfx::Vec2(0, 0), size);
    gfx::Rect2 inner(static_cast<float>(safe.left), static_cast<float>(safe.top),
                     size.x - static_cast<float>(safe.left + safe.right), size.y - static_cast<float>(safe.top + safe.bottom));
    if (!inner.has_area()) return full;
    return intersection(inner, full);
}

// _input: InputEventScreenTouch (DOWN, UP; CANCEL as a lift) and InputEventScreenDrag (MOVE).
// True when the controls took the event (`set_input_as_handled`).
bool TouchControls::pointer(PointerEvent kind, const Pointer& p) {
    if (!visible) return false;
    if (editing) return _edit_pointer(kind, p);
    gfx::Vec2 at(p.x, p.y);
    gd::Int index = p.id;
    switch (kind) {
        case PointerEvent::DOWN:
            return press(index, at);
        case PointerEvent::MOVE:
            if (_fingers.has(index)) {
                move(index, at);
                return true;
            }
            return false;
        case PointerEvent::UP:
        case PointerEvent::CANCEL:
            if (_fingers.has(index)) {
                release(index);
                return true;
            }
            return false;
    }
    return false;
}

// A finger down at `at`: a control, or the stick zone. True when the controls took it. The stick
// has one finger, the first one down: another in its zone (a resting palm) is taken but does nothing.
bool TouchControls::press(gd::Int index, gfx::Vec2 at) {
    if (_fingers.has(index)) release(index);   // a lost lift: the finger starts anew
    gd::Ref<Control2> c = control_at(at);
    if (c != nullptr) {
        _fingers.set(index, Finger{false, gfx::Vec2(), c});
        _landed(c);
    } else if (has_point(_stick_zone, at)) {
        if (!is_inf(_stick_origin)) return true;
        _fingers.set(index, Finger{true, at, nullptr});
        _stick_origin = dpad ? _stick_hint : at;
        _stick_at = at;
    } else {
        return false;
    }
    _update();
    return true;
}

void TouchControls::move(gd::Int index, gfx::Vec2 at) {
    Finger& f = _fingers.at(index);
    if (f.stick) {
        _stick_at = at;
    } else if (is_zero(f.control->rect.size)) {
        // A finger on the face buttons slides between them.
        gd::Ref<Control2> c = control_at(at);
        if (c != nullptr && is_zero(c->rect.size)) {
            bool other = c != f.control;
            f.control = c;
            if (other) _landed(c);
        }
    }
    _update();
}

void TouchControls::release(gd::Int index) {
    Finger f = _fingers.at(index);
    _fingers.erase(index);
    if (f.stick) _stick_origin = gfx::Vec2(INFINITY, INFINITY);
    _update();
}

gd::Ref<TouchControls::Control2> TouchControls::control_at(gfx::Vec2 at) const {
    gd::Ref<Control2> best = nullptr;
    float best_d = INFINITY;
    for (const auto& c : _controls) {
        if (!is_zero(c->rect.size)) {
            if (has_point(grow(c->rect, RECT_SLACK * _unit), at)) return c;
            continue;
        }
        float d = distance(at, c->centre);
        if (d < c->radius * HIT_SLACK && d < best_d) {
            best = c;
            best_d = d;
        }
    }
    return best;
}

// The stick's direction bits for an offset from its origin.
gd::Int TouchControls::stick_bits(gfx::Vec2 offset) const {
    if (length(offset) < DEAD_ZONE * _unit * _stick_scale) return 0;
    gd::Int sector = gd::posmod(static_cast<gd::Int>(std::llround(std::atan2(offset.y, offset.x) / (TAU / 8.0f))), 8);
    return DIRECTIONS[sector];
}

void TouchControls::_update() {
    gd::Int b = 0;
    for (const auto& [index, f] : _fingers) {
        if (f.stick) {
            b |= stick_bits(_stick_at - _stick_origin);
        } else if (f.control->custom_id != 0) {
            // An own button: its one step while held (turned for the side); a combo plays below.
            const CustomButton* own = custom_button(f.control->custom_id);
            if (own != nullptr && !own->combo()) b |= _turned(own->steps[0]);
        } else {
            b |= f.control->bits;
        }
    }
    if (_playing) {
        const CustomButton* own = custom_button(_playing->id);
        if (own != nullptr) b |= _turned(combo_bits(*own, _playing->frame));
    }
    bits = b;
    if (Pads == nullptr) return;
    // The touch controls are player 1's device from their first press (InputRouter).
    if (b != 0 && gd::Int(Pads->devices[0]) != InputRouter::TOUCH) Pads->assign_touch();
    Pads->touch_latch |= b & ~Pads->touch_bits;
    Pads->touch_bits = b;
}

// Every finger lifted (the controls hidden, the window left).
void TouchControls::reset() {
    _fingers.clear();
    _stick_origin = gfx::Vec2(INFINITY, INFINITY);
    bits = 0;
    _playing.reset();
    _edit_fingers.clear();   // Layout edit
    _pinch.reset();
    if (Pads == nullptr) return;
    Pads->touch_bits = 0;
    Pads->touch_latch = 0;
}

void TouchControls::set_window(gfx::Vec2 window, const SafeArea& area) {
    size = window;
    safe = area;
    layout();
}

void TouchControls::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    if (window.x != size.x || window.y != size.y) set_window(window, safe);
    _draw(canvas);
}

void TouchControls::_draw(gfx::Canvas& canvas) {
    float u = _unit;
    // Layout edit: the screen dimmed under the controls being placed.
    if (editing) canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), gfx::Color(0, 0, 0, 0.35f));
    float stick = STICK_RADIUS * u * _stick_scale;
    // Native: the Touch opacity setting scales the remake's alphas (ALPHA is its default).
    float rest = editing ? ALPHA : opacity;
    float pressed_alpha = std::min(1.0f, rest + (PRESSED_ALPHA - ALPHA));
    float share = rest / ALPHA;
    if (dpad && _stick_zone.has_area()) {
        // The D-pad: a cross at the stick's rest, the arm (or the two arms) pointed at lit.
        float reach = stick;
        float arm = 0.62f * reach;
        gd::Int held = is_inf(_stick_origin) ? 0 : stick_bits(_stick_at - _stick_origin);
        gfx::Vec2 c = _stick_hint;
        gfx::Rect2 vertical(c - gfx::Vec2(arm / 2.0f, reach), gfx::Vec2(arm, 2.0f * reach));
        gfx::Rect2 horizontal(c - gfx::Vec2(reach, arm / 2.0f), gfx::Vec2(2.0f * reach, arm));
        canvas.draw_rect(vertical, gfx::Color(0, 0, 0, FILL * rest));
        canvas.draw_rect(horizontal, gfx::Color(0, 0, 0, FILL * rest));
        float len = reach - arm / 2.0f;
        const std::pair<gd::Int, gfx::Rect2> arms[] = {
            {PadState::UP, gfx::Rect2(c + gfx::Vec2(-arm / 2.0f, -reach), gfx::Vec2(arm, len))},
            {PadState::DOWN, gfx::Rect2(c + gfx::Vec2(-arm / 2.0f, arm / 2.0f), gfx::Vec2(arm, len))},
            {PadState::LEFT, gfx::Rect2(c + gfx::Vec2(-reach, -arm / 2.0f), gfx::Vec2(len, arm))},
            {PadState::RIGHT, gfx::Rect2(c + gfx::Vec2(arm / 2.0f, -arm / 2.0f), gfx::Vec2(len, arm))}};
        for (const auto& [bit, r] : arms) {
            bool lit = (held & bit) != 0;
            if (lit) canvas.draw_rect(r, gfx::Color(1, 1, 1, 0.35f * pressed_alpha));
            canvas.draw_rect(r, with_alpha(lit ? gfx::Color::white() : OUTLINE, lit ? pressed_alpha : rest), false, 2.0f);
            // A small arrow in the arm, pointing out.
            gfx::Vec2 mid = centre_of(r);
            gfx::Vec2 out = bit == PadState::UP ? gfx::Vec2(0, -1) : bit == PadState::DOWN ? gfx::Vec2(0, 1)
                          : bit == PadState::LEFT ? gfx::Vec2(-1, 0) : gfx::Vec2(1, 0);
            gfx::Vec2 side(-out.y, out.x);
            float s = arm * 0.22f;
            canvas.draw_colored_polygon({mid + out * s, mid - out * s * 0.6f + side * s, mid - out * s * 0.6f - side * s},
                                        gfx::Color(1, 1, 1, lit ? pressed_alpha : rest));
        }
    } else if (!is_inf(_stick_origin)) {
        canvas.draw_circle(_stick_origin, stick, gfx::Color(1, 1, 1, 0.12f * share));
        canvas.draw_arc(_stick_origin, stick, 0, TAU, 40, with_alpha(OUTLINE, rest), 2.0f);
        gfx::Vec2 knob = _stick_origin + limit_length(_stick_at - _stick_origin, stick);
        canvas.draw_circle(knob, 0.55f * u * _stick_scale, gfx::Color(1, 1, 1, pressed_alpha * 0.6f));
    } else if (_stick_zone.has_area()) {
        canvas.draw_arc(_stick_hint, stick, 0, TAU, 40, with_alpha(OUTLINE, editing ? ALPHA : 0.25f * share), 2.0f);
    }
    for (const auto& c : _controls) {
        bool down = (bits & c->bits) == c->bits;
        if (c->custom_id != 0) {
            // An own button: while a finger holds it or its combo plays.
            down = _playing && _playing->id == c->custom_id;
            for (const auto& [index, f] : _fingers) down = down || (!f.stick && f.control == c);
        }
        float alpha = down ? pressed_alpha : rest;
        gfx::Color edge = with_alpha(down ? gfx::Color::white() : OUTLINE, alpha);
        gfx::Color backing(0, 0, 0, (down ? PRESSED_FILL : FILL) * alpha);
        if (!is_zero(c->rect.size)) {
            canvas.draw_rect(c->rect, backing);
            canvas.draw_rect(c->rect, edge, false, 2.0f);
            _label(canvas, c->label, centre_of(c->rect), c->rect.size.y * 0.42f, alpha);
            continue;
        }
        canvas.draw_circle(c->centre, c->radius, backing);
        canvas.draw_arc(c->centre, c->radius, 0, TAU, 40, edge, 2.0f);
        if (c->label.empty()) {
            ButtonGlyphs::draw(canvas, c->bits, c->centre, c->radius * 0.8f, alpha);
        } else {
            // A long own name made smaller to fit across the circle.
            float h = c->radius * 0.8f;
            float w = font->get_string_size(c->label, gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(h)).x;
            if (w > 1.7f * c->radius) h = std::max(8.0f, h * 1.7f * c->radius / w);
            _label(canvas, c->label, c->centre, h, alpha);
        }
    }
    if (editing) {
        // Each control's corner handle (white while a finger holds the control), Reset and Done.
        std::vector<std::string> keys;
        for (const auto& c : _controls) keys.push_back(control_key(*c));
        if (_stick_zone.has_area()) keys.push_back(STICK_KEY);
        float hs = HANDLE * u;
        for (const std::string& key : keys) {
            gfx::Vec2 centre, half;
            float scale = 1.0f;
            if (!_placed(key, centre, half, scale)) continue;
            bool held = false;
            for (const auto& [id, f] : _edit_fingers) held = held || f.key == key;
            gfx::Vec2 corner = centre + half;
            gfx::Rect2 handle(corner - gfx::Vec2(hs / 2.0f, hs / 2.0f), gfx::Vec2(hs, hs));
            canvas.draw_rect(handle, gfx::Color(0, 0, 0, 0.5f));
            canvas.draw_rect(handle, with_alpha(held ? gfx::Color::white() : OUTLINE, PRESSED_ALPHA), false, 2.0f);
            if (held || key == selected) {
                canvas.draw_rect(grow(gfx::Rect2(centre - half, half * 2.0f), 3.0f), gfx::Color(1, 1, 1, held ? 0.6f : 0.35f), false, 1.0f);
            }
            // Its size in pixels under it (over it at the window's bottom edge).
            float th = 0.36f * u;
            float below = centre.y + half.y + 0.25f * u + th / 2.0f;
            float y = below + th / 2.0f <= size.y ? below : centre.y - half.y - 0.25f * u - th / 2.0f;
            _label(canvas, size_text(key), gfx::Vec2(centre.x, y), th, key == selected ? PRESSED_ALPHA : ALPHA);
        }
        // The alignment lines of a drag (the centre line it snapped to).
        gfx::Color guide(0.4f, 0.85f, 1.0f, 0.8f);
        if (!std::isnan(_guide_x)) canvas.draw_rect(gfx::Rect2(gfx::Vec2(_guide_x - 1.0f, 0), gfx::Vec2(2.0f, size.y)), guide);
        if (!std::isnan(_guide_y)) canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, _guide_y - 1.0f), gfx::Vec2(size.x, 2.0f)), guide);
        _draw_rect_button(canvas, _reset_rect, "RESET", false);
        _draw_rect_button(canvas, _done_rect, "DONE", false);
        if (static_cast<gd::Int>(custom.size()) < MAX_CUSTOM) _draw_rect_button(canvas, _new_rect, "ADD", false);
        if (selected.rfind("Button ", 0) == 0) _draw_rect_button(canvas, _change_rect, "EDIT", false);
        gfx::Vec2 px;
        if (!selected.empty() && size_px(selected, px)) {
            _draw_rect_button(canvas, _minus_rect, "-", false);
            _draw_rect_button(canvas, _plus_rect, "+", false);
            float mid = (_minus_rect.end().x + _plus_rect.position.x) / 2.0f;
            float room = _plus_rect.position.x - _minus_rect.end().x - GAP * u;
            std::string name = selected;
            for (const auto& c : _controls) {
                if (c->custom_id != 0 && control_key(*c) == selected) name = c->label;
            }
            std::string text = name + "  " + size_text(selected);
            float th = _minus_rect.size.y * 0.42f;
            float w = font->get_string_size(text, gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(th)).x;
            if (w > room && w > 0.0f) th *= room / w;
            _label(canvas, text, gfx::Vec2(mid, centre_of(_minus_rect).y), th, PRESSED_ALPHA);
        }
        if (wizard) _draw_wizard(canvas);
    }
}

void TouchControls::_label(gfx::Canvas& canvas, const std::string& text, gfx::Vec2 at, float height, float alpha) {
    int s = static_cast<int>(height);
    float w = font->get_string_size(text, gfx::HorizontalAlignment::LEFT, -1, s).x;
    font->draw_string(canvas, at + gfx::Vec2(-w / 2.0f, s * 0.36f), text, gfx::HorizontalAlignment::LEFT, -1, s,
                      gfx::Color(1, 1, 1, alpha));
}

// Layout edit --------------------------------------------------------------------------------------

std::string TouchControls::control_key(const Control2& c) {
    if (c.custom_id != 0) return "Button " + std::to_string(c.custom_id);
    if (PadState::NAMES.has(c.bits)) return PadState::NAMES.at(c.bits);
    return "Macro " + c.label;
}

// The game's notation: the directions first (U, D, F, B), then the limbs, then the shoulders.
std::string TouchControls::default_label(gd::Int pad_bits) {
    std::string names;
    auto add = [&](const std::string& name) { names += (names.empty() ? "" : "+") + name; };
    for (const auto& [bit, name] : std::vector<std::pair<gd::Int, std::string>>{
             {PadState::UP, "U"}, {PadState::DOWN, "D"}, {PadState::RIGHT, "F"}, {PadState::LEFT, "B"}}) {
        if ((pad_bits & bit) != 0) add(name);
    }
    for (const auto& [bit, name] : LIMB_NAMES) {
        if ((pad_bits & bit) != 0) add(name);
    }
    for (gd::Int bit : {PadState::L1, PadState::L2, PadState::R1, PadState::R2}) {
        if ((pad_bits & bit) != 0) add(PadState::NAMES.at(bit));
    }
    return names;
}

std::string TouchControls::default_label(const std::vector<gd::Int>& steps) {
    std::string names;
    for (size_t i = 0; i < steps.size(); ++i) names += (i == 0 ? "" : ",") + default_label(steps[i]);
    return names;
}

gd::Int TouchControls::CustomButton::bits() const {
    gd::Int b = 0;
    for (gd::Int s : steps) b |= s;
    return b;
}

bool TouchControls::Wizard::complete() const {
    for (gd::Int s : steps) {
        if (s == 0) return false;
    }
    return !steps.empty();
}

gd::Int TouchControls::_turned(gd::Int pad_bits) const {
    if (!facing_left || !facing_left()) return pad_bits;
    gd::Int lr = pad_bits & (PadState::LEFT | PadState::RIGHT);
    gd::Int swapped = ((lr & PadState::LEFT) != 0 ? PadState::RIGHT : 0) | ((lr & PadState::RIGHT) != 0 ? PadState::LEFT : 0);
    return (pad_bits & ~(PadState::LEFT | PadState::RIGHT)) | swapped;
}

// Step i is pressed from i * gap for HOLD steps (at most the gap less one, so presses stay apart).
gd::Int TouchControls::combo_bits(const CustomButton& b, gd::Int frame) {
    gd::Int gap = std::clamp(b.gap, MIN_GAP, MAX_GAP);
    gd::Int hold = std::min(HOLD, gap - 1);
    gd::Int i = frame / gap;
    if (i < 0 || i >= static_cast<gd::Int>(b.steps.size())) return 0;
    return frame % gap < hold ? b.steps[static_cast<size_t>(i)] : 0;
}

void TouchControls::_landed(const gd::Ref<Control2>& c) {
    if (on_press) on_press();   // native: a short buzz (the Touch vibration setting)
    const CustomButton* b = c->custom_id != 0 ? custom_button(c->custom_id) : nullptr;
    if (b != nullptr && b->combo()) _playing = Playing{b->id, 0};
}

void TouchControls::tick() {
    if (!_playing) return;
    _playing->frame += 1;
    const CustomButton* b = custom_button(_playing->id);
    if (b == nullptr || _playing->frame >= static_cast<gd::Int>(b->steps.size()) * std::clamp(b->gap, MIN_GAP, MAX_GAP)) {
        _playing.reset();
    }
    _update();
}

const TouchControls::CustomButton* TouchControls::custom_button(gd::Int id) const {
    for (const CustomButton& b : custom) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

// Wizard ---------------------------------------------------------------------------------------------

const gd::Arr<gd::Int> TouchControls::WIZARD_BUTTONS{PadState::SQUARE, PadState::TRIANGLE, PadState::CROSS, PadState::CIRCLE,
                                                     PadState::L1, PadState::L2, PadState::R1, PadState::R2,
                                                     PadState::UP, PadState::DOWN, PadState::RIGHT, PadState::LEFT};

void TouchControls::open_wizard(gd::Int id) {
    Wizard w;
    if (const CustomButton* b = custom_button(id)) {
        w.id = id;
        w.steps = b->steps;
        w.gap = b->gap;
        w.label = b->label;
    }
    _edit_fingers.clear();
    _pinch.reset();
    wizard = w;
}

void TouchControls::text_entered(const std::string& text) {
    if (!wizard || !wizard->asking) return;
    wizard->asking = false;
    // Without the spaces around it and line breaks, at most MAX_LABEL characters (UTF-8 kept whole).
    size_t a = text.find_first_not_of(" \r\n"), b = text.find_last_not_of(" \r\n");
    std::string in = a == std::string::npos ? "" : text.substr(a, b - a + 1);
    std::string out;
    gd::Int chars = 0;
    for (size_t i = 0; i < in.size() && chars < MAX_LABEL;) {
        unsigned char ch = static_cast<unsigned char>(in[i]);
        size_t n = ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4;
        if (ch != '\n' && ch != '\r') out += in.substr(i, n), chars += 1;
        i += n;
    }
    size_t end = out.find_last_not_of(' ');
    wizard->label = end == std::string::npos ? "" : out.substr(0, end + 1);
}

// The panel in the middle: the title, three rows of four toggles (the face buttons, the shoulders,
// the directions), the steps (< STEP 1/2 > and - +), their timing, the name, then CANCEL (DELETE) OK.
TouchControls::WizardRects TouchControls::wizard_rects() const {
    WizardRects r;
    float u = _unit;
    gfx::Rect2 area = safe_area();
    constexpr float W = 10.0f, H = 11.4f;   // units at full size
    gfx::Vec2 panel_size(std::min(W * u, area.size.x - 2.0f * EDGE * u), std::min(H * u, area.size.y - 2.0f * EDGE * u));
    r.panel = gfx::Rect2(centre_of(area) - panel_size * 0.5f, panel_size);
    float k = std::min(panel_size.x / (W * u), panel_size.y / (H * u));   // smaller on a small window
    float cell = 1.4f * u * k, gap = 0.4f * u * k, line = 0.85f * u * k;
    float row_w = 4.0f * cell + 3.0f * gap;
    float x0 = centre_of(r.panel).x - row_w / 2.0f;
    float y = r.panel.position.y + 1.2f * u * k;
    for (gd::Int row = 0; row < 3; ++row) {
        float h = row == 0 ? cell : 0.7f * cell;
        for (gd::Int i = 0; i < 4; ++i) {
            r.toggles.push_back(gfx::Rect2(gfx::Vec2(x0 + static_cast<float>(i) * (cell + gap), y), gfx::Vec2(cell, h)));
        }
        y += h + gap;
    }
    // < STEP 1/2 >   - +
    gfx::Vec2 square(line, line);
    r.prev = gfx::Rect2(gfx::Vec2(x0, y), square);
    r.remove_step = gfx::Rect2(gfx::Vec2(x0 + row_w - 2.0f * line - gap, y), square);
    r.add_step = gfx::Rect2(gfx::Vec2(x0 + row_w - line, y), square);
    r.next = gfx::Rect2(gfx::Vec2(r.remove_step.position.x - gap - line, y), square);
    r.step_text = gfx::Rect2(gfx::Vec2(r.prev.end().x, y), gfx::Vec2(r.next.position.x - r.prev.end().x, line));
    y += line + gap;
    // -  8 FRAMES  +
    r.gap_minus = gfx::Rect2(gfx::Vec2(x0, y), square);
    r.gap_plus = gfx::Rect2(gfx::Vec2(x0 + row_w - line, y), square);
    r.gap_text = gfx::Rect2(gfx::Vec2(r.gap_minus.end().x, y), gfx::Vec2(r.gap_plus.position.x - r.gap_minus.end().x, line));
    y += line + gap;
    r.name = gfx::Rect2(gfx::Vec2(x0, y), gfx::Vec2(row_w, line));
    y += line + gap;
    gfx::Vec2 button = SYSTEM_SIZE * u * k;
    float bottom = std::min(y, r.panel.end().y - 0.3f * u * k - button.y);
    r.cancel = gfx::Rect2(gfx::Vec2(x0, bottom), button);
    r.ok = gfx::Rect2(gfx::Vec2(x0 + row_w - button.x, bottom), button);
    r.remove = gfx::Rect2(gfx::Vec2(centre_of(r.panel).x - button.x / 2.0f, bottom), button);
    return r;
}

// A tap in the wizard: a toggle, the name (the platform's text input), CANCEL, DELETE, OK.
bool TouchControls::_wizard_pointer(gfx::Vec2 at) {
    WizardRects r = wizard_rects();
    if (wizard->asking) return true;
    Wizard& w = *wizard;
    gd::Int& step_bits = w.steps[static_cast<size_t>(w.step)];
    for (size_t i = 0; i < r.toggles.size(); ++i) {
        if (has_point(r.toggles[i], at)) {
            gd::Int bit = WIZARD_BUTTONS[static_cast<gd::Int>(i)];
            step_bits ^= bit;
            // Up and down, forward and back: one of each pair.
            gd::Int pair = bit == PadState::UP ? PadState::DOWN : bit == PadState::DOWN ? PadState::UP
                         : bit == PadState::RIGHT ? PadState::LEFT : bit == PadState::LEFT ? PadState::RIGHT : 0;
            if ((step_bits & bit) != 0) step_bits &= ~pair;
            return true;
        }
    }
    gd::Int n = static_cast<gd::Int>(w.steps.size());
    if (has_point(r.prev, at)) {
        w.step = gd::posmod(w.step - 1, n);
        return true;
    }
    if (has_point(r.next, at)) {
        w.step = gd::posmod(w.step + 1, n);
        return true;
    }
    if (has_point(r.add_step, at) && n < MAX_STEPS) {
        w.steps.insert(w.steps.begin() + w.step + 1, 0);
        w.step += 1;
        return true;
    }
    if (has_point(r.remove_step, at) && n > 1) {
        w.steps.erase(w.steps.begin() + w.step);
        w.step = std::min(w.step, n - 2);
        return true;
    }
    if (n > 1 && (has_point(r.gap_minus, at) || has_point(r.gap_plus, at))) {
        w.gap = std::clamp(w.gap + (has_point(r.gap_plus, at) ? 1 : -1), MIN_GAP, MAX_GAP);
        return true;
    }
    if (has_point(r.name, at)) {
        wizard->asking = ask_text && ask_text("Button name", wizard->label);
        return true;
    }
    if (has_point(r.cancel, at)) {
        wizard.reset();
        return true;
    }
    if (wizard->id != 0 && has_point(r.remove, at)) {
        gd::Int id = wizard->id;
        custom.erase(std::remove_if(custom.begin(), custom.end(), [id](const CustomButton& b) { return b.id == id; }), custom.end());
        std::string key = "Button " + std::to_string(id);
        for (auto& [k, p] : screens) p.erase(key);
        shared.erase(key);
        placements.erase(key);
        if (selected == key) selected.clear();
        wizard.reset();
        layout();
        return true;
    }
    if (has_point(r.ok, at) && wizard->complete()) {
        gd::Int id = wizard->id;
        if (id == 0) {
            for (const CustomButton& b : custom) id = std::max(id, b.id);
            id += 1;
            custom.push_back(CustomButton{id, wizard->steps, wizard->gap, wizard->label});
        } else {
            for (CustomButton& b : custom) {
                if (b.id == id) b.steps = wizard->steps, b.gap = wizard->gap, b.label = wizard->label;
            }
        }
        selected = "Button " + std::to_string(id);
        wizard.reset();
        layout();
        return true;
    }
    return true;
}

void TouchControls::_draw_wizard(gfx::Canvas& canvas) {
    WizardRects r = wizard_rects();
    float u = _unit;
    canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), gfx::Color(0, 0, 0, 0.5f));
    canvas.draw_rect(r.panel, gfx::Color(0.08f, 0.08f, 0.1f, 0.96f));
    canvas.draw_rect(r.panel, with_alpha(OUTLINE, PRESSED_ALPHA), false, 2.0f);
    float k = r.cancel.size.y / (SYSTEM_SIZE.y * u);
    _label(canvas, wizard->id == 0 ? "NEW BUTTON" : "EDIT BUTTON", gfx::Vec2(centre_of(r.panel).x, r.panel.position.y + 0.65f * u * k),
           0.45f * u * k, PRESSED_ALPHA);
    const Wizard& w = *wizard;
    gd::Int step_bits = w.steps[static_cast<size_t>(w.step)];
    static const char* const DIRECTION_NAMES[] = {"UP", "DOWN", "FORWARD", "BACK"};
    for (size_t i = 0; i < r.toggles.size(); ++i) {
        gd::Int bit = WIZARD_BUTTONS[static_cast<gd::Int>(i)];
        bool on = (step_bits & bit) != 0;
        const gfx::Rect2& t = r.toggles[i];
        float alpha = on ? PRESSED_ALPHA : ALPHA;
        canvas.draw_rect(t, on ? gfx::Color(1, 1, 1, 0.25f) : gfx::Color(0, 0, 0, FILL));
        canvas.draw_rect(t, with_alpha(on ? gfx::Color::white() : OUTLINE, alpha), false, on ? 3.0f : 2.0f);
        if (i < 4) {
            ButtonGlyphs::draw(canvas, bit, centre_of(t), t.size.y * 0.32f, alpha);
        } else {
            std::string name = i < 8 ? PadState::NAMES.at(bit) : DIRECTION_NAMES[i - 8];
            float h = t.size.y * 0.42f;
            float tw = font->get_string_size(name, gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(h)).x;
            if (tw > t.size.x * 0.9f) h *= t.size.x * 0.9f / tw;
            _label(canvas, name, centre_of(t), h, alpha);
        }
    }
    // The steps: < STEP 1/2 > and - + (a step more or less); with more than one, their timing.
    gd::Int n_steps = static_cast<gd::Int>(w.steps.size());
    float th = r.step_text.size.y * 0.5f;
    _draw_rect_button(canvas, r.prev, "<", false);
    _draw_rect_button(canvas, r.next, ">", false);
    if (n_steps < MAX_STEPS) _draw_rect_button(canvas, r.add_step, "+", false);
    if (n_steps > 1) _draw_rect_button(canvas, r.remove_step, "-", false);
    _label(canvas, "STEP " + std::to_string(w.step + 1) + "/" + std::to_string(n_steps), centre_of(r.step_text), th, PRESSED_ALPHA);
    if (n_steps > 1) {
        _draw_rect_button(canvas, r.gap_minus, "-", false);
        _draw_rect_button(canvas, r.gap_plus, "+", false);
        _label(canvas, "EVERY " + std::to_string(w.gap) + " FRAMES", centre_of(r.gap_text), th, PRESSED_ALPHA);
    }
    // The name: its text, or the steps' names dimmed while none is typed.
    const gfx::Rect2& n = r.name;
    canvas.draw_rect(n, gfx::Color(0, 0, 0, 0.6f));
    canvas.draw_rect(n, with_alpha(w.asking ? gfx::Color::white() : OUTLINE, PRESSED_ALPHA), false, 2.0f);
    std::string shown = w.label.empty() ? default_label(w.steps) : w.label;
    if (shown.empty() || shown.find_first_not_of(",") == std::string::npos) shown = "NAME";
    float nh = n.size.y * 0.55f;
    float nw = font->get_string_size(shown, gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(nh)).x;
    if (nw > n.size.x * 0.95f) nh *= n.size.x * 0.95f / nw;
    _label(canvas, shown, centre_of(n), nh, w.label.empty() ? ALPHA * 0.8f : PRESSED_ALPHA);
    _draw_rect_button(canvas, r.cancel, "CANCEL", false);
    if (w.id != 0) _draw_rect_button(canvas, r.remove, "DELETE", false);
    if (w.complete()) _draw_rect_button(canvas, r.ok, "OK", false);
}

void TouchControls::enter_edit() {
    if (editing) return;
    reset();
    editing = true;
    selected.clear();
    wizard.reset();
    layout();
}

std::string TouchControls::screen_key(gfx::Vec2 window) {
    int a = static_cast<int>(std::lround(window.x)), b = static_cast<int>(std::lround(window.y));
    return std::to_string(std::max(a, b)) + "x" + std::to_string(std::min(a, b));
}

// The window's own saved layout; else the saved screen of the nearest aspect (within ASPECT_MATCH:
// the same display a few pixels off, say with other insets); else a version 1 file's; else none.
void TouchControls::_select_screen(bool force) {
    std::string key = screen_key(size);
    if (!force && key == screen) return;
    screen = key;
    auto own = screens.find(key);
    if (own != screens.end()) {
        placements = own->second.duplicate();   // gd::Dict assigns by reference
        return;
    }
    float aspect = size.y > 0 && size.x > 0 ? std::max(size.x, size.y) / std::min(size.x, size.y) : 0.0f;
    const Placements* best = nullptr;
    float best_off = ASPECT_MATCH;
    for (const auto& [k, p] : screens) {
        int l = 0, s = 0;
        if (std::sscanf(k.c_str(), "%dx%d", &l, &s) != 2 || s <= 0 || aspect <= 0.0f) continue;
        float off = std::fabs(static_cast<float>(l) / static_cast<float>(s) - aspect) / aspect;
        if (off <= best_off) {
            best_off = off;
            best = &p;
        }
    }
    placements = (best != nullptr ? *best : shared).duplicate();
}

void TouchControls::_store_screen() { screens[screen] = placements.duplicate(); }

bool TouchControls::size_px(const std::string& key, gfx::Vec2& px) const {
    gfx::Vec2 centre, half;
    float scale = 1.0f;
    if (!_placed(key, centre, half, scale)) return false;
    px = half * 2.0f;
    return true;
}

// "96 px" (a round control's diameter), "120x48 px".
std::string TouchControls::size_text(const std::string& key) const {
    gfx::Vec2 px;
    if (!size_px(key, px)) return "";
    bool round = key == STICK_KEY;
    for (const auto& c : _controls) {
        if (control_key(*c) == key) round = is_zero(c->rect.size);
    }
    std::string w = std::to_string(std::lround(px.x));
    return round ? w + " px" : w + "x" + std::to_string(std::lround(px.y)) + " px";
}

// The size the selected control shows (rounded) plus `pixels`, as a scale of its default size.
void TouchControls::step_size(float pixels) {
    gfx::Vec2 centre, half;
    float scale = 1.0f;
    if (selected.empty() || !_placed(selected, centre, half, scale) || half.x <= 0.0f) return;
    float shown = 2.0f * half.x;
    float base = shown / scale;
    _place(selected, centre, (static_cast<float>(std::lround(shown)) + pixels) / base);
}

void TouchControls::finish_edit() {
    if (!editing) return;
    reset();
    editing = false;
    _store_screen();
    save_layout();
    layout();
}

void TouchControls::reset_layout() {
    placements.clear();
    _store_screen();
    _edit_fingers.clear();
    _pinch.reset();
    layout();
}

void TouchControls::load_layout(const std::string& path) {
    layout_path = path;
    if (!gd::file_exists(path)) return;
    gd::PBytes bytes = gd::read_file(path);
    std::string text;
    gd::Int n = bytes.size();
    for (gd::Int i = 0; i < n; ++i) text.push_back(static_cast<char>(gd::Int(bytes[i])));
    if (!parse_layout(text)) Log::warning("touch: cannot read the layout " + path);
    layout();
}

namespace {

// A "controls" object's records (bad ones skipped).
TouchControls::Placements read_controls(const gd::Json& controls) {
    TouchControls::Placements out;
    for (const auto& [key, rec] : controls.members()) {
        if (!rec.is_object()) continue;
        const gd::Json& x = rec.get("x");
        const gd::Json& y = rec.get("y");
        const gd::Json& s = rec.get("scale");
        if (!x.is_number() || !y.is_number() || !s.is_number()) continue;
        if (!std::isfinite(x.num()) || !std::isfinite(y.num()) || !std::isfinite(s.num())) continue;
        out.set(key, TouchControls::Placement{std::clamp(static_cast<float>(x.num()), 0.0f, 1.0f),
                                              std::clamp(static_cast<float>(y.num()), 0.0f, 1.0f),
                                              clamp_scale(static_cast<float>(s.num()))});
    }
    return out;
}

gd::JsonValue write_controls(const TouchControls::Placements& placements) {
    gd::JsonValue controls = gd::JsonValue::object();
    for (const auto& [key, p] : placements) {
        gd::JsonValue rec = gd::JsonValue::object();
        rec.set("x", gd::JsonValue(static_cast<double>(p.x), 6));
        rec.set("y", gd::JsonValue(static_cast<double>(p.y), 6));
        rec.set("scale", gd::JsonValue(static_cast<double>(p.scale), 6));
        controls.set(key, rec);
    }
    return controls;
}

}  // namespace

// Version 1 has one "controls" object (kept as `shared`); version 2 one per screen under "screens".
bool TouchControls::parse_layout(const std::string& text) {
    bool ok = false;
    gd::Json root = gd::Json::parse(text, &ok);
    if (!ok || !root.is_object()) return false;
    const gd::Json& controls = root.get("controls");
    const gd::Json& by_screen = root.get("screens");
    if (!controls.is_object() && !by_screen.is_object()) return false;
    shared = controls.is_object() ? read_controls(controls) : Placements();
    // The own buttons: [{"id": 1, "steps": [160], "gap": 8, "label": "Rzut"}, ...]; build 9's
    // single "bits" reads as one step. Bad records are skipped.
    const gd::Json& buttons = root.get("buttons");
    if (buttons.is_array()) {
        custom.clear();
        gd::Int allowed = 0;
        for (gd::Int bit : WIZARD_BUTTONS) allowed |= bit;
        for (gd::Int i = 0; i < buttons.size(); ++i) {
            const gd::Json& b = buttons[i];
            if (!b.is_object() || !b.get("id").is_number()) continue;
            gd::Int id = static_cast<gd::Int>(b.get("id").num());
            std::vector<gd::Int> steps;
            const gd::Json& s = b.get("steps");
            if (s.is_array()) {
                for (gd::Int j = 0; j < s.size() && j < MAX_STEPS; ++j) {
                    if (s[j].is_number()) steps.push_back(static_cast<gd::Int>(s[j].num()) & allowed);
                }
            } else if (b.get("bits").is_number()) {
                steps.push_back(static_cast<gd::Int>(b.get("bits").num()) & allowed);
            }
            bool empty = steps.empty() || std::find(steps.begin(), steps.end(), gd::Int(0)) != steps.end();
            if (id <= 0 || empty || custom_button(id) != nullptr) continue;
            gd::Int gap = b.get("gap").is_number() ? std::clamp(static_cast<gd::Int>(b.get("gap").num()), MIN_GAP, MAX_GAP) : DEFAULT_GAP;
            custom.push_back(CustomButton{id, steps, gap, b.get("label").is_string() ? b.get("label").str() : ""});
            if (static_cast<gd::Int>(custom.size()) == MAX_CUSTOM) break;
        }
    }
    screens.clear();
    if (by_screen.is_object()) {
        for (const auto& [key, rec] : by_screen.members()) {
            if (rec.is_object() && rec.get("controls").is_object()) screens[key] = read_controls(rec.get("controls"));
        }
    }
    _select_screen(true);
    return true;
}

// {"version": 2, "screens": {"2316x904": {"controls": {"Cross": {"x": 0.81, "y": 0.72, "scale": 1.2},
// ...}}, ...}} and a version 1 file's "controls" while some screen still uses them (gd/json_writer.h;
// six significant digits, the controls in their order).
std::string TouchControls::layout_json() const {
    gd::JsonValue by_screen = gd::JsonValue::object();
    for (const auto& [key, p] : screens) {
        gd::JsonValue rec = gd::JsonValue::object();
        rec.set("controls", write_controls(p));
        by_screen.set(key, rec);
    }
    gd::JsonValue root = gd::JsonValue::object();
    root.set("version", 2);
    gd::JsonValue buttons = gd::JsonValue::array();
    for (const CustomButton& b : custom) {
        gd::JsonValue rec = gd::JsonValue::object();
        rec.set("id", b.id);
        gd::JsonValue steps = gd::JsonValue::array();
        for (gd::Int s : b.steps) steps.append(s);
        rec.set("steps", steps);
        rec.set("gap", b.gap);
        rec.set("label", b.label);
        buttons.append(rec);
    }
    root.set("buttons", buttons);
    if (!shared.is_empty()) root.set("controls", write_controls(shared));
    root.set("screens", by_screen);
    return gd::json_stringify(root, "  ", false) + "\n";
}

bool TouchControls::save_layout() const {
    if (layout_path.empty()) return false;
    std::error_code ec;
    std::filesystem::path parent = std::filesystem::path(layout_path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::string text = layout_json();
    std::FILE* f = std::fopen(layout_path.c_str(), "wb");
    bool ok = f != nullptr && std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f != nullptr) ok = std::fclose(f) == 0 && ok;
    if (!ok) Log::error("touch: cannot write the layout " + layout_path);
    return ok;
}

gfx::Vec2 TouchControls::_clamp_centre(gfx::Vec2 centre, gfx::Vec2 half, const gfx::Rect2& area) const {
    auto axis = [](float v, float h, float lo, float hi) { return lo + h > hi - h ? (lo + hi) / 2.0f : std::clamp(v, lo + h, hi - h); };
    return gfx::Vec2(axis(centre.x, half.x, area.position.x, area.end().x), axis(centre.y, half.y, area.position.y, area.end().y));
}

// The placements over the remake's layout: each control at its centre (a share of the window) and
// size, kept inside the safe area; the stick's rest with a zone of the default zone's size around it.
void TouchControls::_apply_placements(const gfx::Rect2& area) {
    if (placements.is_empty()) return;
    for (const auto& c : _controls) {
        std::string key = control_key(*c);
        if (!placements.has(key)) continue;
        Placement p = placements.at(key);
        float s = clamp_scale(p.scale);
        gfx::Vec2 target(p.x * size.x, p.y * size.y);
        if (!is_zero(c->rect.size)) {
            gfx::Vec2 extent = c->rect.size * s;
            gfx::Vec2 centre = _clamp_centre(target, extent * 0.5f, area);
            c->rect = gfx::Rect2(centre - extent * 0.5f, extent);
        } else {
            c->radius *= s;
            c->centre = _clamp_centre(target, gfx::Vec2(c->radius, c->radius), area);
        }
    }
    if (placements.has(STICK_KEY)) {
        Placement p = placements.at(STICK_KEY);
        _stick_scale = clamp_scale(p.scale);
        float reach = STICK_RADIUS * _unit * _stick_scale;
        _stick_hint = _clamp_centre(gfx::Vec2(p.x * size.x, p.y * size.y), gfx::Vec2(reach, reach), area);
        gfx::Vec2 half = _stick_zone.size * 0.5f;
        _stick_zone = intersection(gfx::Rect2(_stick_hint - half, half * 2.0f), _stick_zone.has_area()
            ? gfx::Rect2(gfx::Vec2(area.position.x, _stick_zone.position.y), gfx::Vec2(area.size.x, _stick_zone.size.y)) : area);
    }
}

bool TouchControls::_placed(const std::string& key, gfx::Vec2& centre, gfx::Vec2& half, float& scale) const {
    if (key == STICK_KEY) {
        centre = _stick_hint;
        float reach = STICK_RADIUS * _unit * _stick_scale;
        half = gfx::Vec2(reach, reach);
        scale = _stick_scale;
        return true;
    }
    for (const auto& c : _controls) {
        if (control_key(*c) != key) continue;
        if (!is_zero(c->rect.size)) {
            centre = centre_of(c->rect);
            half = c->rect.size * 0.5f;
        } else {
            centre = c->centre;
            half = gfx::Vec2(c->radius, c->radius);
        }
        scale = placements.has(key) ? clamp_scale(placements.at(key).scale) : 1.0f;
        return true;
    }
    return false;
}

void TouchControls::_place(const std::string& key, gfx::Vec2 centre, float scale) {
    gfx::Vec2 now, half;
    float old = 1.0f;
    if (!_placed(key, now, half, old) || size.x <= 0 || size.y <= 0) return;
    scale = clamp_scale(scale);
    gfx::Vec2 placed = _clamp_centre(centre, half * (scale / old), safe_area());
    placements.set(key, Placement{placed.x / size.x, placed.y / size.y, scale});
    _store_screen();
    layout();
}

gfx::Vec2 TouchControls::_snap(const std::string& key, gfx::Vec2 centre) {
    _guide_x = NAN;
    _guide_y = NAN;
    float best_x = SNAP * _unit, best_y = SNAP * _unit;
    std::vector<std::string> keys;
    for (const auto& c : _controls) keys.push_back(control_key(*c));
    if (_stick_zone.has_area()) keys.push_back(STICK_KEY);
    for (const std::string& other : keys) {
        if (other == key) continue;
        gfx::Vec2 c, half;
        float scale = 1.0f;
        if (!_placed(other, c, half, scale)) continue;
        float dx = std::fabs(c.x - centre.x), dy = std::fabs(c.y - centre.y);
        if (dx < best_x) best_x = dx, _guide_x = c.x;
        if (dy < best_y) best_y = dy, _guide_y = c.y;
    }
    if (!std::isnan(_guide_x)) centre.x = _guide_x;
    if (!std::isnan(_guide_y)) centre.y = _guide_y;
    return centre;
}

// What a finger lands on in the edit: a corner handle (resize) before a control's body (move).
std::string TouchControls::_edit_target(gfx::Vec2 at, bool& resize) const {
    std::vector<std::string> keys;
    for (const auto& c : _controls) keys.push_back(control_key(*c));
    if (_stick_zone.has_area()) keys.push_back(STICK_KEY);
    float hs = HANDLE * _unit;
    for (const std::string& key : keys) {
        gfx::Vec2 centre, half;
        float scale = 1.0f;
        if (!_placed(key, centre, half, scale)) continue;
        gfx::Rect2 handle(centre + half - gfx::Vec2(hs / 2.0f, hs / 2.0f), gfx::Vec2(hs, hs));
        if (has_point(grow(handle, hs * 0.3f), at)) {
            resize = true;
            return key;
        }
    }
    resize = false;
    gd::Ref<Control2> c = control_at(at);
    if (c != nullptr) return control_key(*c);
    if (_stick_zone.has_area() && distance(at, _stick_hint) < STICK_RADIUS * _unit * _stick_scale) return STICK_KEY;
    return "";
}

// The edit takes every touch: Done, Reset, a control's handle (resize), its body (move; a second
// finger on it pinches).
bool TouchControls::_edit_pointer(PointerEvent kind, const Pointer& p) {
    gfx::Vec2 at(p.x, p.y);
    gd::Int id = p.id;
    if (wizard) return kind == PointerEvent::DOWN ? _wizard_pointer(at) : true;
    if (kind == PointerEvent::DOWN) {
        if (static_cast<gd::Int>(custom.size()) < MAX_CUSTOM && has_point(_new_rect, at)) {
            open_wizard(0);
            return true;
        }
        if (selected.rfind("Button ", 0) == 0 && has_point(_change_rect, at)) {
            open_wizard(std::atoll(selected.c_str() + 7));
            return true;
        }
        if (has_point(grow(_done_rect, RECT_SLACK * _unit), at)) {
            finish_edit();
            return true;
        }
        if (has_point(grow(_reset_rect, RECT_SLACK * _unit), at)) {
            reset_layout();
            return true;
        }
        if (!selected.empty() && (has_point(_minus_rect, at) || has_point(_plus_rect, at))) {
            step_size(has_point(_plus_rect, at) ? 1.0f : -1.0f);
            return true;
        }
        _edit_fingers.erase(id);
        bool resize = false;
        std::string key = _edit_target(at, resize);
        if (key.empty()) return true;
        selected = key;
        gfx::Vec2 centre, half;
        float scale = 1.0f;
        _placed(key, centre, half, scale);
        EditFinger f{key, resize, at - centre, at, scale, std::max(1.0f, distance(at, centre))};
        if (!resize && !_pinch) {
            for (const auto& [other, g] : _edit_fingers) {
                if (g.key == key && !g.resize) {
                    _pinch = Pinch{key, other, id, std::max(1.0f, distance(at, g.at)), scale, (at + g.at) * 0.5f, centre};
                    break;
                }
            }
        }
        _edit_fingers[id] = f;
        return true;
    }
    auto found = _edit_fingers.find(id);
    if (found == _edit_fingers.end()) return true;
    if (kind == PointerEvent::MOVE) {
        EditFinger& f = found->second;
        f.at = at;
        gfx::Vec2 centre, half;
        float scale = 1.0f;
        if (!_placed(f.key, centre, half, scale)) return true;
        if (_pinch && (_pinch->a == id || _pinch->b == id)) {
            gfx::Vec2 a = _edit_fingers[_pinch->a].at, b = _edit_fingers[_pinch->b].at;
            gfx::Vec2 mid = (a + b) * 0.5f;
            _place(_pinch->key, _pinch->centre + (mid - _pinch->mid), _pinch->scale * distance(a, b) / _pinch->span);
        } else if (f.resize) {
            _place(f.key, centre, f.start_scale * distance(at, centre) / f.start_reach);
        } else {
            _place(f.key, _snap(f.key, at - f.grab), scale);
        }
        return true;
    }
    _guide_x = NAN;   // the drag is over: no alignment lines
    _guide_y = NAN;
    // UP, CANCEL: a pinch's other finger goes on moving the control from where it is now.
    _edit_fingers.erase(found);
    if (_pinch && (_pinch->a == id || _pinch->b == id)) {
        gd::Int other = _pinch->a == id ? _pinch->b : _pinch->a;
        std::string key = _pinch->key;
        _pinch.reset();
        auto rest = _edit_fingers.find(other);
        gfx::Vec2 centre, half;
        float scale = 1.0f;
        if (rest != _edit_fingers.end() && _placed(key, centre, half, scale)) {
            rest->second.grab = rest->second.at - centre;
            rest->second.start_scale = scale;
        }
    }
    return true;
}

// A rectangular button of the edit, drawn as Select and Start are.
void TouchControls::_draw_rect_button(gfx::Canvas& canvas, const gfx::Rect2& r, const std::string& label, bool down) {
    float alpha = down ? PRESSED_ALPHA : ALPHA;
    canvas.draw_rect(r, gfx::Color(0, 0, 0, (down ? PRESSED_FILL : FILL) * alpha));
    canvas.draw_rect(r, with_alpha(down ? gfx::Color::white() : OUTLINE, alpha), false, 2.0f);
    _label(canvas, label, centre_of(r), r.size.y * 0.42f, alpha);
}

}  // namespace t3

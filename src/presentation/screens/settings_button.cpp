// remake/presentation/screens/settings_button.gd
// The way into the settings on the main menu (settings_button.h).
#include "presentation/screens/settings_button.h"

#include <algorithm>
#include <cmath>

#include "presentation/screens/settings_view.h"

namespace t3 {

namespace {

constexpr float TAU = 6.28318530717958648f;

std::string tr(const std::string& key) { return SettingsView::tr(key); }

bool same(const gfx::Rect2& a, const gfx::Rect2& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.size.x == b.size.x && a.size.y == b.size.y;
}

}  // namespace

SettingsButton::SettingsButton() {
    font = gfx::Font::fallback();
    visible = false;
}

void SettingsButton::place(gfx::Rect2 anchor) {
    if (!same(anchor, _anchor)) {
        _anchor = anchor;
    }
}

float SettingsButton::_gear_size() const {
    if (_anchor.size.y > 0.0f) {
        return _anchor.size.y / (1.0f + MARGIN);
    }
    return std::max(MIN_SIZE, size.y * SIZE_SHARE);
}

gfx::Rect2 SettingsButton::area() const {
    float s = _gear_size();
    float label = font->get_string_size(tr("SETTINGS_OPEN_HINT"), gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(s * 0.45f)).x;
    float width = s * (1.0f + 3.0f * MARGIN) + label;
    gfx::Vec2 at = _anchor.size.y > 0.0f ? _anchor.position : gfx::Vec2(s * MARGIN, s * MARGIN);
    return gfx::Rect2(at, gfx::Vec2(width, s * (1.0f + MARGIN)));
}

bool SettingsButton::_has_point(gfx::Vec2 point) const {
    if (!visible) return false;
    // area().grow(_gear_size() * HIT_SLACK).has_point(point)
    float by = _gear_size() * HIT_SLACK;
    gfx::Rect2 r = area();
    r.position = r.position - gfx::Vec2(by, by);
    r.size = r.size + gfx::Vec2(2.0f * by, 2.0f * by);
    return point.x >= r.position.x && point.y >= r.position.y && point.x < r.end().x && point.y < r.end().y;
}

bool SettingsButton::pointer(PointerEvent kind, const Pointer& p) {
    // The control gets the events on its points only (_has_point).
    if (!_has_point(gfx::Vec2(p.x, p.y))) return false;
    // A tap arrives as the touch and as a mouse click emulated from it: only the touch counts.
    // Native: the platforms send each finger once.
    if (kind == PointerEvent::DOWN) {
        for (const auto& f : pressed) f();
        return true;
    }
    return false;
}

void SettingsButton::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    size = window;
    _draw(canvas);
}

void SettingsButton::_draw(gfx::Canvas& canvas) {
    float s = _gear_size();
    gfx::Rect2 r = area();
    canvas.draw_rect(r, BAND);
    gfx::Vec2 c(r.position.x + s * (0.5f + MARGIN), r.position.y + r.size.y * 0.5f);
    // The gear: a ring with teeth.
    for (int i = 0; i < TEETH; ++i) {
        float a = TAU * static_cast<float>(i) / static_cast<float>(TEETH);
        gfx::Vec2 dir(std::cos(a), std::sin(a));
        gfx::Vec2 side = gfx::Vec2(-dir.y, dir.x) * (s * 0.08f);
        gfx::Vec2 inner = c + dir * (s * 0.3f);
        gfx::Vec2 outer = c + dir * (s * 0.46f);
        canvas.draw_colored_polygon({inner - side, outer - side, outer + side, inner + side}, COLOUR);
    }
    canvas.draw_arc(c, s * 0.26f, 0.0f, TAU, 24, COLOUR, s * 0.14f);
    font->draw_string(canvas, gfx::Vec2(c.x + s * (0.5f + MARGIN), c.y + s * 0.16f), tr("SETTINGS_OPEN_HINT"),
                      gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(s * 0.45f), COLOUR);
}

}  // namespace t3

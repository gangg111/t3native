#include "presentation/screens/screen_canvas.h"

#include <cmath>
#include <cstdio>

#include "core/math/fx.h"
#include "presentation/screens/screen_box.h"

namespace t3 {

void ScreenCanvas::setup_canvas(const gd::Ref<HudData>& hud_data) { hud = hud_data; }

void ScreenCanvas::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    if (!visible) return;
    target = &canvas;
    size = window;
    canvas.set_filter(gfx::Filter::NEAREST);   // texture_filter = TEXTURE_FILTER_NEAREST
    canvas.set_blend(gfx::Blend::MIX);
    canvas.set_clip(gfx::Rect2());
    canvas.set_texel_mask(false);
    _draw();
    // Native: the wide mode's foreground stays in the 4:3 box (black() and the background passes
    // draw past it).
    bool widened_now = widened();
    for (const Pass& p : _passes) {
        begin();
        canvas.set_blend(p.blend);
        canvas.set_texel_mask(p.texel_mask);
        gfx::Rect2 clip = p.clip.has_area() ? r(p.clip.position.x, p.clip.position.y, p.clip.size.x, p.clip.size.y) : gfx::Rect2();
        if (widened_now && !p.background && !clip.has_area()) {
            gfx::Rect2 a = foreground_area();
            clip = r(a.position.x, a.position.y, a.size.x, a.size.y);
        }
        canvas.set_clip(clip);
        p.painter();
    }
    canvas.set_blend(gfx::Blend::MIX);
    canvas.set_texel_mask(false);
    canvas.set_clip(gfx::Rect2());
}

gfx::Rect2 ScreenCanvas::layout_box() const { return ScreenBox::rect(size); }

void ScreenCanvas::add_pass(gfx::Blend blend, std::function<void()> painter, gfx::Rect2 clip) {
    _passes.push_back({blend, std::move(painter), clip, false});
}

void ScreenCanvas::add_texel_mask_pass(std::function<void()> painter) {
    _passes.push_back({gfx::Blend::SUB, std::move(painter), gfx::Rect2(), true});
}

void ScreenCanvas::add_background_pass(gfx::Blend blend, std::function<void()> painter) {
    _passes.push_back({blend, std::move(painter), gfx::Rect2(), false, true});
}

float ScreenCanvas::wide_left() const {
    if (!widened() || box.size.x <= 0) return 0.0f;
    return -box.position.x * static_cast<float>(SCREEN_X) / box.size.x;
}

float ScreenCanvas::wide_right() const {
    if (!widened() || box.size.x <= 0) return static_cast<float>(SCREEN_X);
    return (size.x - box.position.x) * static_cast<float>(SCREEN_X) / box.size.x;
}

float ScreenCanvas::wide_top() const {
    if (!widened() || box.size.y <= 0) return 0.0f;
    return -box.position.y * static_cast<float>(SCREEN_Y) / box.size.y;
}

float ScreenCanvas::wide_bottom() const {
    if (!widened() || box.size.y <= 0) return static_cast<float>(SCREEN_Y);
    return (size.y - box.position.y) * static_cast<float>(SCREEN_Y) / box.size.y;
}

void ScreenCanvas::image_wide(const gfx::TextureRef& tex) {
    if (!tex) return;
    float sx = static_cast<float>(SCREEN_X), sy = static_cast<float>(SCREEN_Y);
    target->draw_texture_rect(tex, r(0, 0, sx, sy), false, gfx::Color::white());
    if (!widened()) return;
    float tw = static_cast<float>(tex->width()), th = static_cast<float>(tex->height());
    float l = wide_left(), rr = wide_right(), t = wide_top(), b = wide_bottom();
    // The edge columns over the full height, then the edge rows over the full width (the corners
    // take the corner texels).
    if (l < 0) target->draw_texture_rect_region(tex, r(l, 0, -l, sy), gfx::Rect2(0, 0, 1, th), gfx::Color::white());
    if (rr > sx) target->draw_texture_rect_region(tex, r(sx, 0, rr - sx, sy), gfx::Rect2(tw - 1, 0, 1, th), gfx::Color::white());
    if (t < 0) {
        target->draw_texture_rect_region(tex, r(0, t, sx, -t), gfx::Rect2(0, 0, tw, 1), gfx::Color::white());
        target->draw_texture_rect_region(tex, r(l, t, -l, -t), gfx::Rect2(0, 0, 1, 1), gfx::Color::white());
        target->draw_texture_rect_region(tex, r(sx, t, rr - sx, -t), gfx::Rect2(tw - 1, 0, 1, 1), gfx::Color::white());
    }
    if (b > sy) {
        target->draw_texture_rect_region(tex, r(0, sy, sx, b - sy), gfx::Rect2(0, th - 1, tw, 1), gfx::Color::white());
        target->draw_texture_rect_region(tex, r(l, sy, -l, b - sy), gfx::Rect2(0, th - 1, 1, 1), gfx::Color::white());
        target->draw_texture_rect_region(tex, r(sx, sy, rr - sx, b - sy), gfx::Rect2(tw - 1, th - 1, 1, 1), gfx::Color::white());
    }
}

gfx::Vec2 ScreenCanvas::pt(float x, float y) const { return ScreenBox::point(box, SCREEN(), x, y); }

gfx::Rect2 ScreenCanvas::r(float x, float y, float w, float h) const {
    gfx::Vec2 a = pt(x, y);
    return gfx::Rect2(a, pt(x + w, y + h) - a);
}

void ScreenCanvas::black() { target->draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), gfx::Color::black()); }

void ScreenCanvas::fill(float x, float y, float w, float h, const gfx::Color& c) { target->draw_rect(r(x, y, w, h), c); }

void ScreenCanvas::line(float x0, float y0, float x1, float y1, const gfx::Color& c, gfx::Rect2 clip) {
    gfx::Vec2 a(x0, y0);
    gfx::Vec2 b(x1, y1);
    if (clip.has_area()) {
        // Liang-Barsky against the draw area (right and bottom edges exclusive, as the GPU's).
        gfx::Vec2 d = b - a;
        float t0 = 0.0f;
        float t1 = 1.0f;
        const float edges[4][2] = {{-d.x, a.x - clip.position.x}, {d.x, clip.end().x - 1 - a.x},
                                   {-d.y, a.y - clip.position.y}, {d.y, clip.end().y - 1 - a.y}};
        for (const auto& e : edges) {
            float pe = e[0];
            float qe = e[1];
            if (std::fabs(pe) < 1e-5f) {   // is_zero_approx
                if (qe < 0) return;
                continue;
            }
            float t = qe / pe;
            if (pe < 0) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
        }
        if (t0 > t1) return;
        b = a + d * t1;
        a = a + d * t0;
    }
    // A pixel-wide rectangle along the line (the lines here are horizontal or vertical).
    gfx::Vec2 lo(std::min(a.x, b.x), std::min(a.y, b.y));
    gfx::Vec2 hi(std::max(a.x, b.x), std::max(a.y, b.y));
    target->draw_rect(r(lo.x, lo.y, hi.x - lo.x + 1, hi.y - lo.y + 1), c);
}

void ScreenCanvas::gradient(float x, float y, float w, float h, const gfx::Color& tl, const gfx::Color& tr, const gfx::Color& bl,
                            const gfx::Color& br) {
    if (w == 0 || h == 0) return;
    target->draw_polygon({pt(x, y), pt(x + w, y), pt(x + w, y + h), pt(x, y + h)}, {tl, tr, br, bl});
}

void ScreenCanvas::textured(const gfx::TextureRef& tex, float x, float y, float w, float h, const gfx::Color& c0,
                            const gfx::Color& c1, const gfx::Color& c2, const gfx::Color& c3) {
    if (!tex || w == 0 || h == 0) return;
    target->draw_polygon({pt(x, y), pt(x + w, y), pt(x + w, y + h), pt(x, y + h)},
                         {modulation(c0), modulation(c1), modulation(c3), modulation(c2)}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, tex);
}

void ScreenCanvas::quad(gfx::Vec2 p0, gfx::Vec2 p1, gfx::Vec2 p2, gfx::Vec2 p3, const gfx::Color& c0, const gfx::Color& c1,
                        const gfx::Color& c2, const gfx::Color& c3) {
    target->draw_primitive({pt(p0.x, p0.y), pt(p1.x, p1.y), pt(p2.x, p2.y)}, {c0, c1, c2});
    target->draw_primitive({pt(p1.x, p1.y), pt(p2.x, p2.y), pt(p3.x, p3.y)}, {c1, c2, c3});
}

void ScreenCanvas::image(const gfx::TextureRef& tex, float x, float y, float w, float h, const gfx::Color& modulate_colour,
                         bool flip) {
    if (!tex) return;
    gfx::Rect2 rect = r(x, y, w < 0 ? static_cast<float>(tex->width()) : w, h < 0 ? static_cast<float>(tex->height()) : h);
    if (flip) rect.size.x = -rect.size.x;   // a negative size flips the texture in place
    target->draw_texture_rect(tex, rect, false, modulate_colour);
}

gd::Int ScreenCanvas::text_width(const std::string& text, gd::Int font_index) const {
    gd::Ref<HudData::FontInfo> info = hud->font(font_index);
    return !info ? 0 : static_cast<gd::Int>(text.size()) * info->advance;
}

void ScreenCanvas::text(const std::string& s, gd::Int font_index, gd::Int colour, float x, float y, float alpha) {
    HudText::draw(*target, *hud, s, font_index, colour, x, y, HudText::layout(pt(0, 0), pt(1, 1)), gfx::Color(1, 1, 1, alpha));
}

void ScreenCanvas::text_runs(const std::vector<Run>& runs, gd::Int font_index, gd::Int colour, float x, float y) {
    gd::Ref<HudData::FontInfo> info = hud->font(font_index);
    if (!info) return;
    gd::Int c = colour;
    for (size_t i = 0; i < runs.size(); ++i) {
        if (i % 2 == 1) {
            c = std::get<gd::Int>(runs[i]);
            continue;
        }
        const std::string& s = std::get<std::string>(runs[i]);
        text(s, font_index, c, x, y);
        x += static_cast<float>(static_cast<gd::Int>(s.size()) * info->advance);
    }
}

void ScreenCanvas::print_fmt(const std::string& fmt, const std::vector<Arg>& args, gd::Int font_index, gd::Int colour) {
    float x = 0.0f;
    float y = 0.0f;
    float mx = 0.0f;   // the line start (%H / %h)
    std::string run;   // the text not drawn yet, at (x, y)
    gd::Int font = font_index;
    size_t n = fmt.size();
    size_t a = 0;
    size_t i = 0;
    auto at = [&](size_t k) { return static_cast<gd::Int>(static_cast<unsigned char>(fmt[k])); };
    while (i < n) {
        gd::Int ch = at(i);
        if (ch != PERCENT && ch != HudText::NEWLINE) {
            // The plain characters up to the next code or line break.
            size_t j = i + 1;
            while (j < n && at(j) != PERCENT && at(j) != HudText::NEWLINE) j += 1;
            run += fmt.substr(i, j - i);
            i = j;
            continue;
        }
        i += 1;
        if (ch == HudText::NEWLINE) {
            x = _flush_run(run, font, colour, x, y);
            run.clear();
            gd::Ref<HudData::FontInfo> info = hud->font(font);
            y += info ? static_cast<float>(info->line) : 0;
            x = mx;
            continue;
        }
        bool zero = false;
        gd::Int width = 0;
        while (i < n && at(i) >= DIGIT_0 && at(i) <= DIGIT_8) {
            if (at(i) == DIGIT_0) zero = true;
            else width = at(i) - DIGIT_0;
            i += 1;
        }
        char code = i < n ? fmt[i] : '\0';
        i += 1;
        if (code == '%') {
            run += "%";
            continue;
        }
        Arg value = a < args.size() ? args[a] : Arg(gd::Int(0));
        a += 1;
        gd::Int number = std::holds_alternative<gd::Int>(value) ? std::get<gd::Int>(value) : 0;
        switch (code) {
            case 'c': case 'f': case 'H': case 'V': case 'h': case 'v': {
                x = _flush_run(run, font, colour, x, y);
                run.clear();
                gd::Ref<HudData::FontInfo> info = hud->font(font);
                switch (code) {
                    case 'c': colour = number; break;
                    case 'f': font = number; break;
                    case 'H':
                        x = static_cast<float>(Fx::s16(number & 0xFFFF));
                        mx = x;
                        break;
                    case 'V': y = static_cast<float>(Fx::s16(number & 0xFFFF)); break;
                    case 'h':
                        x = static_cast<float>(number * (info ? info->advance : 0));
                        mx = x;
                        break;
                    case 'v': y = static_cast<float>(number * (info ? info->line : 0)); break;
                    default: break;
                }
                break;
            }
            case 's':
                run += std::holds_alternative<std::string>(value) ? std::get<std::string>(value) : gd::str(number);
                break;
            case 'C': run += static_cast<char>(number & 0xFF); break;
            case 'd': case 'D': case 'x': run += _number_text(number, code, width, zero); break;
            default: break;
        }
    }
    _flush_run(run, font, colour, x, y);
}

std::string ScreenCanvas::_number_text(gd::Int value, char code, gd::Int width, bool zero) {
    bool neg = code == 'd' && value < 0;
    gd::Int v = code == 'd' ? gd::absi(value) : value & 0xFFFFFFFFLL;
    std::string digits_text;
    if (code == 'x') {
        char b[24];
        std::snprintf(b, sizeof b, "%llX", static_cast<unsigned long long>(v));
        digits_text = b;
    } else {
        digits_text = v >= BCD_LIMIT ? "99999999" : gd::str(v);
    }
    gd::Int digits = static_cast<gd::Int>(digits_text.size());
    if (width == 0) {
        width = digits;
    } else {
        if (neg) width -= 1;
        if (width < digits || zero) digits = width;
    }
    // digits_text.right(digits).lpad(digits, "0")
    std::string shown = digits <= 0 ? "" : (digits >= static_cast<gd::Int>(digits_text.size()) ? digits_text : digits_text.substr(digits_text.size() - static_cast<size_t>(digits)));
    if (static_cast<gd::Int>(shown.size()) < digits) shown = std::string(static_cast<size_t>(digits) - shown.size(), '0') + shown;
    return std::string(static_cast<size_t>(gd::maxi(width - digits, 0)), ' ') + (neg ? "-" : "") + shown;
}

float ScreenCanvas::_flush_run(const std::string& run, gd::Int font, gd::Int colour, float x, float y) {
    if (run.empty()) return x;
    text(run, font, colour, x, y);
    gd::Ref<HudData::FontInfo> info = hud->font(font);
    return x + static_cast<float>(static_cast<gd::Int>(run.size()) * (info ? info->advance : 0));
}

void ScreenCanvas::centred(const std::string& s, gd::Int font_index, gd::Int colour, float centre_x, float y, float alpha) {
    text(s, font_index, colour, centre_x - static_cast<float>(text_width(s, font_index)) / 2.0f, y, alpha);
}

}  // namespace t3

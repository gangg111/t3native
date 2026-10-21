// remake/presentation/screens/attract_screen.gd
#include "presentation/screens/attract_screen.h"

#include <algorithm>
#include <cmath>

#include "content/text_locale.h"
#include "content/texture_packs.h"
#include "gd/io.h"
#include "presentation/screens/screen_box.h"

namespace t3 {

namespace {
// String.strip_edges(left, right): without the whitespace and control characters at those ends.
std::string strip_edges(const std::string& s, bool left = true, bool right = true) {
    size_t a = 0, b = s.size();
    if (left)
        while (a < b && static_cast<unsigned char>(s[a]) <= 32) ++a;
    if (right)
        while (b > a && static_cast<unsigned char>(s[b - 1]) <= 32) --b;
    return s.substr(a, b - a);
}
// String.replace(what, forwhat).
std::string replace(std::string s, const std::string& what, const std::string& forwhat) {
    for (size_t i = s.find(what); i != std::string::npos; i = s.find(what, i + forwhat.size())) s.replace(i, what.size(), forwhat);
    return s;
}
// roundi.
gd::Int roundi(double v) { return static_cast<gd::Int>(std::llround(v)); }
// The original screen's size as ScreenBox takes it (Vector2i to Vector2).
gfx::Vec2 vec(gd::Vector2i v) { return gfx::Vec2(static_cast<float>(gd::Int(v.x)), static_cast<float>(gd::Int(v.y))); }
}  // namespace

AttractScreen::AttractScreen() : font(gfx::Font::fallback()) {}

void AttractScreen::setup(const gd::Ref<ScreenData>& screens, bool enbu) {
    data = screens;
    enbu_variant = enbu;
    reload_pictures();
    // font.base_font = ThemeDB.fallback_font; font.variation_embolden = TEXT_BOLDNESS
    font = gfx::Font::fallback()->with_embolden(TEXT_BOLDNESS);
    // set_anchors_and_offsets_preset(PRESET_FULL_RECT): `draw` takes the window's size;
    // mouse_filter = MOUSE_FILTER_IGNORE: the view takes no input.
}

// The title picture of title.ovl, or the copy enbu.ovl shows after the demonstration.
void AttractScreen::setup_variant(bool enbu) {
    if (enbu != enbu_variant) {
        enbu_variant = enbu;
        reload_pictures();
    }
}

// The pictures of the variant in the texts' language (the USA title picture in English), from
// the texture pack in use.
void AttractScreen::reload_pictures() {
    if (data == nullptr) {
        return;
    }
    std::string suffix = enbu_variant ? "_enbu" : "";
    std::string title = gd::path_join(data->directory, "title" + suffix + ".png");
    title_texture = TexturePacks::texture(locale != nullptr ? locale->file_or("title" + suffix + ".png", title) : title);
    logo_texture = TexturePacks::texture(gd::path_join(data->directory, "namco" + suffix + ".png"));
    _drawn = gd::PI32();
    queue_redraw();
}

void AttractScreen::set_locale(const gd::Ref<TextLocale>& texts) {
    locale = texts;
    if (data != nullptr) {
        reload_pictures();
    }
}

void AttractScreen::show_frame(gd::Int presents, gd::Int level, gd::Int kind, gd::Int frames) {
    presents_mode = presents;
    presents_level = level;
    picture = kind;
    frame_count = frames;
    // Only what the drawing reads: the fade level while the line fades in, the prompt and its blink
    // on the title with the prompt.
    bool with_prompt = picture == PICTURE_TITLE_PROMPT;
    gd::PI32 shown{presents_mode, presents_mode == 0 ? presents_level : 0, picture,
                   with_prompt ? (frame_count & PROMPT_BLINK) : 0, with_prompt ? prompt : 0};
    if (shown != _drawn) {
        _drawn = shown;
        queue_redraw();
    }
}

void AttractScreen::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    size = window;
    // A plain Control: the project's default texture filter (Godot's LINEAR; ScreenCanvas views
    // set NEAREST themselves), so the title picture and logo are drawn smoothed as in the remake.
    gfx::Filter filter = canvas.get_filter();
    canvas.set_filter(gfx::Filter::LINEAR);
    _draw(canvas);
    canvas.set_filter(filter);
}

void AttractScreen::_draw(gfx::Canvas& canvas) {
    if (data == nullptr || size.y < 1.0f) {
        return;
    }
    gfx::Rect2 box = ScreenBox::rect(size);
    if (presents_mode >= 0 && presents_mode <= 2) {
        canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), gfx::Color::black());
        if (presents_mode != 2) {
            float alpha = presents_mode == 0 ? std::clamp(static_cast<float>(presents_level / 256.0), 0.0f, 1.0f) : 1.0f;
            _line(canvas, box, data->presents, alpha);
        }
    }
    switch (picture) {
        case PICTURE_BLACK:
            canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), gfx::Color::black());
            break;
        case PICTURE_TITLE_PROMPT:
        case PICTURE_TITLE_LOGO:
        case PICTURE_TITLE_COPYRIGHT:
        case PICTURE_TITLE:
            canvas.draw_rect(gfx::Rect2(gfx::Vec2(0, 0), size), gfx::Color::black());
            if (title_texture) canvas.draw_texture_rect(title_texture, box, false);
            if (picture == PICTURE_TITLE_PROMPT && (frame_count & PROMPT_BLINK) != 0) {
                gd::Ref<ScreenData::Text> p = data->prompts[gd::clampi(prompt, 0, data->prompts.size() - 1)];
                _text(canvas, box, p, static_cast<float>(gd::Int(p->at.x)), 1.0f);
            }
            if (picture == PICTURE_TITLE_PROMPT || picture == PICTURE_TITLE_LOGO) {
                gfx::Vec2 tl = ScreenBox::point(box, vec(data->screen), static_cast<float>(gd::Int(data->logo_at.x)),
                                                static_cast<float>(gd::Int(data->logo_at.y)));
                if (logo_texture) {
                    gfx::Vec2 logo(static_cast<float>(logo_texture->width()), static_cast<float>(logo_texture->height()));
                    gfx::Vec2 sz(logo.x / static_cast<float>(gd::Int(data->screen.x)) * box.size.x,
                                 logo.y / static_cast<float>(gd::Int(data->screen.y)) * box.size.y);
                    canvas.draw_texture_rect(logo_texture, gfx::Rect2(tl, sz), false);
                }
            }
            if (picture != PICTURE_TITLE) {
                for (const gd::Ref<ScreenData::Text>& t : data->copyright) {
                    _text(canvas, box, t, data->run_centre(t), 1.0f);
                }
            }
            break;
        default:
            break;
    }
}

// Consecutive texts of one original line (different colours), centred as the whole run.
void AttractScreen::_line(gfx::Canvas& canvas, const gfx::Rect2& box, const gd::Arr<gd::Ref<ScreenData::Text>>& parts,
                          float alpha) {
    gd::Int size_px = _size_px(box, parts[0]);
    std::string whole;
    std::string shown;   // what _draw_text draws of each part, in the texts' language
    for (const gd::Ref<ScreenData::Text>& t : parts) {
        whole += t->text;
        shown += strip_edges(_shown(t), true, false);
    }
    gd::Ref<ScreenData::Text> run = gd::make<ScreenData::Text>();
    run->text = whole;
    run->font = parts[0]->font;
    run->at = parts[0]->at;
    float width = font->get_string_size(strip_edges(shown), gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(size_px)).x;
    float x = ScreenBox::point(box, vec(data->screen), data->run_centre(run), 0).x - width / 2.0f;
    for (const gd::Ref<ScreenData::Text>& t : parts) {
        x += _draw_text(canvas, box, t, x, alpha);
    }
}

gd::Int AttractScreen::_size_px(const gfx::Rect2& box, const gd::Ref<ScreenData::Text>& t) const {
    return roundi(gd::Int(data->font_height[t->font]) * ScreenBox::line_scale(box, vec(data->screen)) * 1.05);
}

// A text centred on `centre_x` (original x), its top at the original y.
void AttractScreen::_text(gfx::Canvas& canvas, const gfx::Rect2& box, const gd::Ref<ScreenData::Text>& t, float centre_x,
                          float alpha) {
    gd::Int size_px = _size_px(box, t);
    float width = font->get_string_size(strip_edges(_shown(t)), gfx::HorizontalAlignment::LEFT, -1, static_cast<int>(size_px)).x;
    float left = ScreenBox::point(box, vec(data->screen), centre_x, 0).x - width / 2.0f;
    _draw_text(canvas, box, t, left, alpha);
}

// A text in the texts' language (the converted texts show the font's @ as ©; the locale knows
// the game's strings).
std::string AttractScreen::_shown(const gd::Ref<ScreenData::Text>& t) const {
    if (locale == nullptr) {
        return t->text;
    }
    return replace(locale->text("title", replace(t->text, "©", "@")), "@", "©");
}

// Draws a text from window x `left` at its original y; returns its width (with trailing spaces).
float AttractScreen::_draw_text(gfx::Canvas& canvas, const gfx::Rect2& box, const gd::Ref<ScreenData::Text>& t, float left,
                                float alpha) {
    int size_px = static_cast<int>(_size_px(box, t));
    std::string text = strip_edges(_shown(t), true, false);
    gfx::Vec2 top = ScreenBox::point(box, vec(data->screen), 0, static_cast<float>(gd::Int(t->at.y)));
    gfx::Vec2 baseline(left, top.y + font->get_ascent(size_px) * 0.92f);
    gd::Arr<gfx::Color> colours = data->text_colour(t->font, t->colour);
    gfx::Color fill = colours[0];
    gfx::Color outline = colours[1];
    fill.a = alpha;
    outline.a = alpha;
    int outline_px = static_cast<int>(std::max<gd::Int>(1, roundi(size_px * OUTLINE_SHARE)));
    font->draw_string_outline(canvas, baseline, text, gfx::HorizontalAlignment::LEFT, -1, size_px, outline_px, outline);
    font->draw_string(canvas, baseline, text, gfx::HorizontalAlignment::LEFT, -1, size_px, fill);
    return font->get_string_size(text, gfx::HorizontalAlignment::LEFT, -1, size_px).x;
}

}  // namespace t3

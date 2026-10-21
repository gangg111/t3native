#include "gfx/font.h"

#include <algorithm>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

namespace t3::embedded {
extern const unsigned char open_sans_semibold[];
extern const unsigned long open_sans_semibold_size;
}  // namespace t3::embedded

namespace t3::gfx {

namespace {
// Glyphs are rendered once at BASE pixels per em, as distance fields PAD pixels wider on each side
// (the shape's edge at alpha ONEDGE, PIXEL_DIST alpha steps per pixel), and scaled when drawn.
constexpr int BASE = 48;
constexpr int PAD = 6;
constexpr int ONEDGE = 128;
constexpr float PIXEL_DIST = static_cast<float>(ONEDGE) / PAD;
constexpr int ATLAS_WIDTH = 1024;
}  // namespace

struct Font::Face {
    stbtt_fontinfo info{};
    int ascent = 0, descent = 0, line_gap = 0;   // font units
    struct Glyph {
        float x0 = 0, y0 = 0;   // the field's top left from the pen, BASE pixels
        int w = 0, h = 0;       // the field's size in the atlas
        int u = 0, v = 0;       // its place in the atlas
        int advance = 0;        // font units
        int index = 0;          // the face's glyph index (kerning)
    };
    std::map<char32_t, Glyph> glyphs;
    Image atlas;
    TextureRef texture;
    bool dirty = false;
    int shelf_x = 0, shelf_y = 0, shelf_h = 0;

    Face() {
        stbtt_InitFont(&info, embedded::open_sans_semibold, stbtt_GetFontOffsetForIndex(embedded::open_sans_semibold, 0));
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
        atlas.width = ATLAS_WIDTH;
        atlas.height = 256;
        atlas.rgba.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 255);
        for (size_t i = 3; i < atlas.rgba.size(); i += 4) atlas.rgba[i] = 0;
    }

    float scale(int font_size) const { return stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(font_size)); }

    const Glyph& glyph(char32_t cp) {
        auto it = glyphs.find(cp);
        if (it != glyphs.end()) return it->second;
        Glyph g;
        g.index = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
        int lsb = 0;
        stbtt_GetGlyphHMetrics(&info, g.index, &g.advance, &lsb);
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* sdf = stbtt_GetGlyphSDF(&info, scale(BASE), g.index, PAD, ONEDGE, PIXEL_DIST, &w, &h, &xoff, &yoff);
        if (sdf) {
            if (shelf_x + w + 1 > atlas.width) {
                shelf_x = 0;
                shelf_y += shelf_h + 1;
                shelf_h = 0;
            }
            while (shelf_y + h + 1 > atlas.height) {
                // A taller atlas (the glyphs keep their places).
                atlas.height *= 2;
                size_t old = atlas.rgba.size();
                atlas.rgba.resize(static_cast<size_t>(atlas.width) * atlas.height * 4, 255);
                for (size_t i = old + 3; i < atlas.rgba.size(); i += 4) atlas.rgba[i] = 0;
                texture.reset();
            }
            g.u = shelf_x;
            g.v = shelf_y;
            g.w = w;
            g.h = h;
            g.x0 = static_cast<float>(xoff);
            g.y0 = static_cast<float>(yoff);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    atlas.rgba[(static_cast<size_t>(g.v + y) * atlas.width + g.u + x) * 4 + 3] = sdf[y * w + x];
            stbtt_FreeSDF(sdf, nullptr);
            shelf_x += w + 1;
            shelf_h = std::max(shelf_h, h);
            dirty = true;
        }
        return glyphs.emplace(cp, g).first->second;
    }

    TextureRef atlas_texture() {
        if (!texture || texture->stale()) {   // first, or after a GL context loss
            texture = std::make_shared<Texture>(atlas);
            dirty = false;
        } else if (dirty) {
            texture->update(atlas);
            dirty = false;
        }
        return texture;
    }
};

std::shared_ptr<Font> Font::fallback() {
    static std::shared_ptr<Font> font = [] {
        auto f = std::make_shared<Font>();
        f->face_ = std::make_shared<Face>();
        return f;
    }();
    return font;
}

std::shared_ptr<Font> Font::with_embolden(float strength) const {
    auto f = std::make_shared<Font>(*this);
    f->embolden_ = strength;
    return f;
}

// FreeType's scaled metrics (Godot's ascent and descent): rounded outwards to whole pixels.
float Font::get_ascent(int font_size) const { return std::ceil(face_->ascent * face_->scale(font_size)); }
float Font::get_descent(int font_size) const { return std::ceil(-face_->descent * face_->scale(font_size)); }

std::vector<char32_t> Font::codepoints(const std::string& text) {
    std::vector<char32_t> out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > text.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        char32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

// A glyph's advance as Godot places it (SUBPIXEL_POSITIONING_AUTO): to half pixels up to 20 pixels
// per em, to whole pixels above.
static float placed_advance(float advance, int font_size) {
    return font_size <= 20 ? std::round(advance * 2.0f) / 2.0f : std::round(advance);
}

float Font::_width(const std::vector<char32_t>& cps, int font_size) const {
    float s = face_->scale(font_size);
    float x = 0;
    int prev = -1;
    for (char32_t cp : cps) {
        const Face::Glyph& g = face_->glyph(cp);
        if (prev >= 0) x += stbtt_GetGlyphKernAdvance(&face_->info, prev, g.index) * s;
        x += placed_advance(g.advance * s, font_size);
        prev = g.index;
    }
    return x;
}

Vec2 Font::get_string_size(const std::string& text, HorizontalAlignment alignment, float width, int font_size) const {
    float w = _width(codepoints(text), font_size);
    if (alignment == HorizontalAlignment::FILL && width > 0) w = width;
    return {w, get_height(font_size)};
}

void Font::_draw(Canvas& canvas, Vec2 pos, const std::string& text, HorizontalAlignment alignment, float width, int font_size,
                 float grow, const Color& modulate) const {
    std::vector<char32_t> cps = codepoints(text);
    float s = face_->scale(font_size);
    float x = pos.x;
    if (width > 0 && alignment != HorizontalAlignment::LEFT && alignment != HorizontalAlignment::FILL) {
        float w = _width(cps, font_size);
        x += alignment == HorizontalAlignment::CENTER ? (width - w) / 2 : width - w;
    }
    float k = static_cast<float>(font_size) / BASE;
    // Every glyph first, so the atlas is complete before it is uploaded.
    for (char32_t cp : cps) face_->glyph(cp);
    TextureRef tex = face_->atlas_texture();
    Filter filter = canvas.get_filter();
    float screen = canvas.transform_scale();
    canvas.set_filter(Filter::LINEAR);
    // A unit of alpha is 255 / PIXEL_DIST base pixels, drawn k times larger, times the transform.
    canvas.set_sdf({true, 255.0f / PIXEL_DIST * k * screen, grow * screen});
    int prev = -1;
    for (char32_t cp : cps) {
        const Face::Glyph& g = face_->glyph(cp);
        if (prev >= 0) x += stbtt_GetGlyphKernAdvance(&face_->info, prev, g.index) * s;
        if (g.w > 0)
            canvas.draw_texture_rect_region(tex, Rect2(x + g.x0 * k, pos.y + g.y0 * k, g.w * k, g.h * k),
                                            Rect2(static_cast<float>(g.u), static_cast<float>(g.v), static_cast<float>(g.w),
                                                  static_cast<float>(g.h)),
                                            modulate);
        x += placed_advance(g.advance * s, font_size);
        prev = g.index;
    }
    canvas.set_sdf({});
    canvas.set_filter(filter);
}

void Font::draw_string(Canvas& canvas, Vec2 pos, const std::string& text, HorizontalAlignment alignment, float width,
                       int font_size, const Color& modulate) const {
    _draw(canvas, pos, text, alignment, width, font_size, embolden_ * font_size / 32.0f, modulate);
}

void Font::draw_string_outline(Canvas& canvas, Vec2 pos, const std::string& text, HorizontalAlignment alignment, float width,
                               int font_size, int size, const Color& modulate) const {
    // The outline grows the shape by size / 4 (FreeType's stroker radius in Godot's text server): measured
    // on the attract screen at 1280x720 (size_px 25, outline size 4), Godot's halo outside the fill is
    // 1.0 px (median, 1.5 mean) where size / 2 drew 2.0 px (2.9 mean); the fill at half its peak
    // covered the same area (2120 against 2123 pixels), so the embolden's size / 32 stays.
    _draw(canvas, pos, text, alignment, width, font_size, embolden_ * font_size / 32.0f + size / 4.0f, modulate);
}

std::vector<std::string> Font::wrap(const std::string& text, float width, int font_size) const {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string para = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (width <= 0) {
            lines.push_back(para);
        } else {
            std::string line;
            size_t i = 0;
            while (i <= para.size()) {
                size_t sp = para.find(' ', i);
                std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                std::string tried = line.empty() ? word : line + " " + word;
                if (!line.empty() && get_string_size(tried, HorizontalAlignment::LEFT, -1, font_size).x > width) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = tried;
                }
                if (sp == std::string::npos) break;
                i = sp + 1;
            }
            lines.push_back(line);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

void Font::draw_multiline_string(Canvas& canvas, Vec2 pos, const std::string& text, HorizontalAlignment alignment, float width,
                                 int font_size, int max_lines, const Color& modulate) const {
    std::vector<std::string> lines = wrap(text, width, font_size);
    float y = pos.y;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (max_lines >= 0 && static_cast<int>(i) >= max_lines) break;
        draw_string(canvas, {pos.x, y}, lines[i], alignment, width, font_size, modulate);
        y += get_height(font_size);
    }
}

}  // namespace t3::gfx

#include "presentation/screens/psx_canvas.h"

#include <utility>

#include "content/texture_packs.h"
#include "core/game/game_progress.h"
#include "core/math/fx.h"
#include "gd/io.h"

namespace t3 {

void PsxCanvas::setup_psx(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image) {
    setup_canvas(hud_data);
    ram = game_ram;
    vram = image;
}

gd::Arr<gfx::TextureRef> PsxCanvas::character_pictures(const std::string& screens_dir, const std::string& folder) {
    gd::Arr<gfx::TextureRef> out;
    for (gd::Int i = 0; i < GameProgress::CHARACTERS; ++i)
        out.append(TexturePacks::texture(gd::path_join(screens_dir, folder + "/" + gd::str(i) + ".png")));
    return out;
}

void PsxCanvas::sprt(float x, float y, gd::Int w, gd::Int h, gd::Int uv_clut, gd::Int tpage, const gfx::Color& tint) {
    gfx::TextureRef tex = vram->sprite(uv_clut & 0xFF, (uv_clut >> 8) & 0xFF, w, h, (uv_clut >> 16) & 0xFFFF, tpage);
    image(tex, x, y, static_cast<float>(w), static_cast<float>(h), tint);
}

void PsxCanvas::picture(float sx, float sy, gd::Int w, gd::Int h, gd::Int vx, gd::Int vy, gd::Int clut, gd::Int mode,
                        const gfx::Color& tint) {
    image(vram->picture(vx, vy, w, h, clut, (mode >> 7) & 3), sx, sy, static_cast<float>(w), static_cast<float>(h), tint);
}

void PsxCanvas::portrait_frame(gd::Int x, gd::Int y, gd::Int flags, gd::Int frames, gfx::Rect2 clip) {
    gd::Int k = (flags & 0x10) != 0 ? triangle(gd::shl(frames, FRAME_COUNT_SHIFT)) : 0x80;
    gfx::Color colours[3];
    for (gd::Int i = 0; i < 3; ++i) colours[i] = psx(scale_colour(ram->u32(PORTRAIT_FRAME_COLOURS + (flags & 3) * 0x28 + 4 * i), k * 2));
    gd::Int base = (x & 0xFFFF) | ((y << 16) & 0xFFFFFFFFLL);
    for (gd::Int i = 0; i < 10; ++i) {
        gd::Int e = PORTRAIT_FRAME_LINES + 12 * i;
        gd::Int a = (base + ram->u32(e + 4)) & 0xFFFFFFFFLL;
        gd::Int b = (base + ram->u32(e + 8)) & 0xFFFFFFFFLL;
        line(static_cast<float>(Fx::s16(a & 0xFFFF)), static_cast<float>(Fx::s16(a >> 16)), static_cast<float>(Fx::s16(b & 0xFFFF)),
             static_cast<float>(Fx::s16(b >> 16)), colours[ram->s32(e)], clip);
    }
}

void PsxCanvas::portrait(float x, float y, gd::Int key, gd::Int flags, gd::Int frames) {
    if ((flags & 8) == 0) {
        _portrait_face(x + 2, y + 3, key, flags);
        portrait_frame(static_cast<gd::Int>(x), static_cast<gd::Int>(y), flags, frames);
    } else {
        _portrait_face(x, y, key, flags);
    }
}

void PsxCanvas::_portrait_face(float x, float y, gd::Int key, gd::Int flags) {
    if ((flags & 0x80) != 0) return;
    gd::Int i = key == 0x59 ? 0x17 : key >> 2;
    gd::Int uvc = ram->u32(PORTRAIT_UV + 8 * i);
    gd::Int tp = ram->u16(PORTRAIT_UV + 8 * i + 4);
    if ((flags & 0x20) != 0) uvc = (uvc & 0xFFFF) | 0x7D500000LL;
    if (i == 0x14) {
        sprt(x, y, 0x20, 0x18, uvc, tp);
        sprt(x, y + 0x18, 0x20, 0x18, uvc + 0x20, tp);
        sprt(x, y + 0x30, 0x20, 0x0A, uvc + 0x40, tp);
    } else if (i < 0x16) {
        sprt(x, y, 0x20, 0x3A, uvc, tp);
    } else {
        sprt(x, y, 0x20, 0x0A, uvc, tp);
        sprt(x, y + 10, 0x20, 0x18, (uvc & 0xFFFF00FFLL) + 0x20, tp);
        sprt(x, y + 0x22, 0x20, 0x18, (uvc & 0xFFFF00FFLL) + 0x40, tp);
    }
    if ((flags & 0x40) != 0) {
        sprt(x, y - 5, 0x20, 0x20, 0x7F1DE000LL, 0x1F);
        sprt(x, y + 0x1B, 0x20, 0x20, 0x7F1DE020LL, 0x1F);
    }
}

void PsxCanvas::ptext(gd::Int format_address, const std::vector<Arg>& args) { print_fmt(ram->string(format_address), args); }

void PsxCanvas::ft4(float x, float y, float w, float h, gd::Int u, gd::Int v, gd::Int tw, gd::Int th, gd::Int clut, gd::Int tpage,
                    const gfx::Color& tint) {
    if (tw <= 0 || th <= 0) return;
    image(vram->sprite(u, v, tw, th, clut, tpage), x, y, w, h, tint);
}

void PsxCanvas::backdrop(gd::Int kind, gd::Int colour) {
    gd::Int table = BACKDROPS[kind];
    gd::Int quads = kind == 0 ? 4 : 2;
    for (gd::Int q = 0; q < quads; ++q) {
        gd::Int e = table + 32 * q;
        gfx::Color c[4];
        for (gd::Int i = 0; i < 4; ++i) {
            gd::Int w = ram->u32(e + 16 + 4 * i);
            c[i] = psx(w == 0xFFFFFFFFLL ? colour : w);
        }
        gfx::Vec2 v[4];
        for (gd::Int i = 0; i < 4; ++i) {
            gd::Int xy = ram->u32(e + 4 * i);
            v[i] = gfx::Vec2(static_cast<float>(Fx::s16(xy & 0xFFFF)), static_cast<float>(Fx::s16(xy >> 16)));
        }
        quad(v[0], v[1], v[2], v[3], c[0], c[1], c[2], c[3]);
        // Native: the wide mode: a quad that meets the screen's left or right edge goes on to the
        // window's edge in that edge's colours (its two corners there: the title band's ends,
        // the floor's full-width gradients).
        if (widened()) {
            for (float edge : {0.0f, static_cast<float>(SCREEN_X)}) {
                int at[2], n = 0;
                for (int i = 0; i < 4 && n < 2; ++i)
                    if (v[i].x == edge) at[n++] = i;
                if (n < 2) continue;
                if (v[at[0]].y > v[at[1]].y) std::swap(at[0], at[1]);
                float far = edge == 0.0f ? wide_left() : wide_right();
                gfx::Vec2 top(edge, v[at[0]].y), bottom(edge, v[at[1]].y);
                gfx::Vec2 top_far(far, top.y), bottom_far(far, bottom.y);
                quad(top_far, top, bottom_far, bottom, c[at[0]], c[at[0]], c[at[1]], c[at[1]]);
            }
            // The same at the screen's top and bottom (a window taller than 4:3), with the corners
            // out to the window's corners where the edge meets a side too.
            for (float edge : {0.0f, static_cast<float>(SCREEN_Y)}) {
                int at[2], n = 0;
                for (int i = 0; i < 4 && n < 2; ++i)
                    if (v[i].y == edge) at[n++] = i;
                if (n < 2) continue;
                if (v[at[0]].x > v[at[1]].x) std::swap(at[0], at[1]);
                float far = edge == 0.0f ? wide_top() : wide_bottom();
                if (far == edge) continue;
                gfx::Color cl = c[at[0]], cr = c[at[1]];
                float x0 = v[at[0]].x, x1 = v[at[1]].x;
                if (x0 == 0.0f) x0 = wide_left();
                if (x1 == static_cast<float>(SCREEN_X)) x1 = wide_right();
                quad(gfx::Vec2(x0, edge), gfx::Vec2(x1, edge), gfx::Vec2(x0, far), gfx::Vec2(x1, far), cl, cr, cl, cr);
            }
        }
    }
}

gd::Int PsxCanvas::scale_colour(gd::Int c, gd::Int k) {
    gd::Int out = 0;
    for (gd::Int sh : {0, 8, 16}) out |= gd::mini(((gd::shr(c, sh) & 0xFF) * k) >> 8, 0xFF) << sh;
    return out;
}

void PsxCanvas::coin(gd::Int player, gd::Int x, gd::Int y, gd::Int colour, gd::Int frames) {
    if ((frames & 0x30) == 0) return;
    bool plugged = true;
    gd::Int table = COIN_TEXTS;
    gd::Int k = 0;
    if ((frames & 0x40) == 0) {
        k = plugged ? (player == 0 ? 1 : 2) : 0;
    } else {
        k = plugged ? 0 : 1;
        table = FREE_TEXTS;
    }
    text(ram->string(ram->u32(table + 8 * k)), 0, colour, static_cast<float>(x - ram->u32(table + 8 * k + 4)), static_cast<float>(y));
}

void PsxCanvas::timer(gd::Int value, float x, float y, gd::Int clut) {
    gd::Int digits = value == -1 ? 0xAB : (gd::mod(gd::div(value, 10), 10) << 4 | gd::mod(value, 10));
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Int d = digits & 0xF;
        digits >>= 4;
        sprt(x, y, 0x10, 0x2E, ram->u32(TIMER_UV + 4 * d) | (clut << 16), 0xE);
        x -= 0xF;
    }
}

}  // namespace t3

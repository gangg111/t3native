// remake/presentation/hud/mode_hud_view.gd
#include "presentation/hud/mode_hud_view.h"

#include <algorithm>
#include <vector>

#include "content/fight_tables.h"
#include "content/game_ram.h"
#include "content/hud_data.h"
#include "content/vram_image.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_view.h"
#include "core/camera/view_matrix.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/round_state.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/force/tekken_force.h"
#include "core/game/byte_block.h"
#include "core/game/game_mode.h"
#include "core/math/fx.h"
#include "gd/io.h"

namespace t3 {

namespace {

// Godot's `Color * float`: every channel, alpha included, times k.
gfx::Color _times(const gfx::Color& c, float k) { return gfx::Color(c.r * k, c.g * k, c.b * k, c.a * k); }

// A print_fmt argument the GDScript passes as a float: ScreenCanvas.print_fmt reads a number only from
// an `int` argument (`if value is int`), so a float one counts as 0 there. The C++ Arg has no float,
// so the value it reads is passed as such.
ScreenCanvas::Arg _float_arg(float) { return gd::Int(0); }

// ScreenCanvas.gpu_draws (screen_canvas.gd, not in screen_canvas.h): the GPU drops a polygon whose
// vertices span more than MAX_POLYGON.
bool gpu_draws(const gd::Arr<gd::Vector2i>& vertices) {
    gd::Vector2i lo = vertices[0];
    gd::Vector2i hi = vertices[0];
    for (const gd::Vector2i& v : vertices) {
        lo = gd::Vector2i(gd::mini(lo.x, v.x), gd::mini(lo.y, v.y));   // lo.min(v)
        hi = gd::Vector2i(gd::maxi(hi.x, v.x), gd::maxi(hi.y, v.y));   // hi.max(v)
    }
    return gd::Int(hi.x) - gd::Int(lo.x) <= ScreenCanvas::MAX_POLYGON_X && gd::Int(hi.y) - gd::Int(lo.y) <= ScreenCanvas::MAX_POLYGON_Y;
}

}  // namespace

void ModeHudView::setup_modes(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& exe, const gd::Ref<VramImage>& system,
                              const gd::Ref<GameRam>& force, const std::string& screens_dir) {
    setup_psx(hud_data, exe, system);
    force_ram = force;
    keys_vram = system->duplicate_image();
    keys_vram->upload_file(gd::path_join(screens_dir, "force_keys.tims"));
    add_pass(gfx::Blend::ADD, [this] { _streaks(); });
    add_pass(gfx::Blend::ADD, [this] { _progress_glow(); });
}

// After a simulation step.
void ModeHudView::show_state(const gd::Ref<FightSimulation>& sim, bool fighting) {
    _sim = sim;
    gd::Int mode = sim->fight->mode;
    _shown = fighting && (mode == GameMode::BALL || mode == GameMode::FORCE);
    visible = _shown;
    if (_shown) redraw();
}

void ModeHudView::_draw() {
    if (!_shown || _sim == nullptr) return;
    begin();
    gd::Ref<FightState> fight = _sim->fight;
    if (gd::Int(fight->mode) == GameMode::FORCE && fight->force != nullptr) {
        _force(fight);
    } else if (gd::Int(fight->mode) == GameMode::BALL && fight->ball != nullptr) {
        _ball(fight);
    }
}

// ---- Tekken Force

void ModeHudView::_force(const gd::Ref<FightState>& fight) {
    gd::Ref<TekkenForce> force = fight->force;
    if (fight->replay_playing == 0 && fight->hud_shown != 0) {
        for (gd::Int i = 0; i < 3; ++i) {
            if (_visible(fight, i)) {
                _name(fight, i);
                _bar(force, i);
            }
        }
        _progress(force);
        _keys(force);
    }
    _life_up(force);
    for (gd::Int i = 0; i < 2; ++i) {
        if (force->slot_popup_shown(i)) _seconds(force, i);
    }
    switch (force->g32(TekkenForce::STATE)) {
        case 4:
            print_fmt(ram_text(STR_NOW_LOADING), {gd::Int(0), gd::Int(6), gd::Int(0xF2), gd::Int(0x1A6)});
            break;
        case 6:
            _bonus_left(force);
            break;
        case 7:
            _tally(force);
            break;
        default:
            break;
    }
}

gd::Ref<FighterState> ModeHudView::_record(const gd::Ref<FightState>& fight, gd::Int i) {
    return i == 0 ? fight->force->player() : fight->force->slot_fighter(i - 1);
}

// A shown enemy: not hidden (+0xC2) and past the round's intro.
bool ModeHudView::_visible(const gd::Ref<FightState>& fight, gd::Int i) {
    if (i == 0) return true;
    return _record(fight, i)->invulnerable == 0 && fight->round_state != RoundState::INTRO;
}

std::string ModeHudView::ram_text(gd::Int address) {
    return force_ram->string(address);
}

void ModeHudView::_name(const gd::Ref<FightState>& fight, gd::Int i) {
    gd::Ref<FighterState> f = _record(fight, i);
    gfx::TextureRef tex = hud->name_texture(f->costume_slot);
    if (tex == nullptr) return;
    gd::Int w = hud->name_width(f->costume_slot);
    gd::Int x = force_ram->s16(NAME_POS + 8 * i);
    gd::Int y = force_ram->s16(NAME_POS + 8 * i + 2);
    if (i > 0) x -= w;
    gfx::TextureRef plate = hud->plate_texture(f->costume_slot);
    if (plate != nullptr) image(plate, x - 4, y + 9, w + 8, 10);
    image(tex, x, y, w, 16);
}

void ModeHudView::_bar(const gd::Ref<TekkenForce>& force, gd::Int i) {
    gd::PI32 b = force->bar(i);
    gd::Int fill = b[1];
    gd::Int recent = b[2];
    bool right = b[3] == 1;
    gd::Int bx = b[4];
    gd::Int y = b[5];
    gd::Int length = b[6];
    gd::Int full = length - 4;
    gd::Int x = i == 0 ? bx : bx - (length - PLAYER_BAR);
    image(hud->texture("bar_cap_left.png"), x, y, CAP.x, CAP.y);
    image(hud->texture("bar_cap_right.png"), x + length - 6, y, CAP.x, CAP.y);
    float pos = static_cast<float>(bx + 0x9A);
    // `parts`: untyped [texture, length] rows, a struct here.
    struct Part {
        std::string file;
        gd::Int n;
    };
    std::vector<Part> parts{{"bar_health.png", fill}, {"bar_damage.png", recent - fill}, {"bar_empty.png", full - recent}};
    for (const Part& part : parts) {
        gd::Int n = part.n;
        if (n <= 0) continue;
        float a = pos;
        if (right) {
            pos += static_cast<float>(n);
        } else {
            pos -= static_cast<float>(n);
            a = pos;
        }
        image(hud->texture(part.file), a, y, n, BAR_HEIGHT);
    }
}

// The level progress bar: four 60-pixel segments from x 64 (lit up to the current level, the
// current one pulsing and split at the camera's marker), the marker and its pointer.
void ModeHudView::_progress(const gd::Ref<TekkenForce>& force) {
    gd::Int level = force->level();
    gd::Int start = level * SEGMENT;
    gd::Int x0 = start + BAR_LEFT;
    gd::Int mark = start + 0x7C;
    if (force->gu32(TekkenForce::BAR_DONE) == 0) {
        gd::Int v = Fx::w32((_sim->camera->view->x - force->g32(TekkenForce::START_X)) * SEGMENT);
        mark = gd::mini(x0 + ((v < 0 ? v + 0x7FFF : v) >> 15), start + 0x7C);
    }
    if (level == 4) mark = x0 + 0xC;
    gd::Int phase = force->gu32(TekkenForce::BAR_PHASE) & 0xFFF;
    gd::Int pulse = gd::mini(((gd::Int(_sim->fight->tables->sin_table[phase]) + 0x1000) >> 6) + 0x80, 0xFF);
    gd::Int height = BAR_BOTTOM - BAR_TOP;
    _glow = gfx::Rect2();
    // Each segment is one texel through its CLUT (a colour of BAR_COLOURS at 5 bits, semi-
    // transparent): half and half over the scene when lit (vertex colour 0x80), the scene halved
    // when not (colour 0); the current level's part up to the marker is additive and pulses.
    for (gd::Int i = 0; i < 4; ++i) {
        gd::Int x = BAR_LEFT + SEGMENT * i;
        if (i == level) {
            _glow = gfx::Rect2(x0, BAR_TOP, mark - x0, height);
            _glow_colour = _modulated(_bar_colour(i), pulse);
            fill(mark, BAR_TOP, x0 + SEGMENT - mark, height, HALF_DARK);
        } else if (i < level) {
            gfx::Color c = _bar_colour(i);
            fill(x, BAR_TOP, SEGMENT, height, gfx::Color(c.r, c.g, c.b, 0.5f));   // Color(_bar_colour(i), 0.5)
        } else {
            fill(x, BAR_TOP, SEGMENT, height, HALF_DARK);
        }
    }
    if (level == 4) {
        // The Doctor B. level: the short fifth piece after the four segments pulses instead.
        _glow = gfx::Rect2(BAR_LEFT + 4 * SEGMENT, BAR_TOP, 0xC, height);
        _glow_colour = _modulated(_bar_colour(3), pulse);
    }
    for (gd::Int k = 0; k < 5; ++k) line(0x3F + SEGMENT * k, BAR_TOP, 0x3F + SEGMENT * k, BAR_BOTTOM, gfx::Color::white());
    line(mark, BAR_TOP, mark, BAR_BOTTOM, gfx::Color::white());
    gd::Int s = gd::Int(_sim->fight->tables->sin_table[phase]) * 12;
    gd::Int half = (Fx::div_trunc(s, 4096)) >> 1;
    std::vector<gfx::Vec2> points{pt(mark - half, POINTER_Y), pt(mark + half, POINTER_Y), pt(mark, BAR_TOP)};
    target->draw_polygon(points, {gfx::Color::c8(0, 0x80, 0xFF), gfx::Color::c8(0, 0x80, 0xFF), gfx::Color::white()});
}

// A segment's CLUT colour: BAR_COLOURS (4 bytes apart) at 5 bits per channel (FUN_800B396C).
gfx::Color ModeHudView::_bar_colour(gd::Int i) {
    std::vector<float> c;   // PackedFloat32Array
    for (gd::Int k = 0; k < 3; ++k) c.push_back(static_cast<float>((force_ram->u8(BAR_COLOURS + 4 * i + k) >> 3) / 31.0));
    return gfx::Color(c[0], c[1], c[2]);
}

// A texel modulated by a vertex colour (0x80 neutral, saturating).
gfx::Color ModeHudView::_modulated(const gfx::Color& c, gd::Int level) {
    double m = static_cast<double>(level) / 128.0;
    return gfx::Color(static_cast<float>(std::min(c.r * m, 1.0)), static_cast<float>(std::min(c.g * m, 1.0)),
                      static_cast<float>(std::min(c.b * m, 1.0)));
}

// The additive pass: the current level's pulsing part of the progress bar.
void ModeHudView::_progress_glow() {
    if (!_shown || _sim == nullptr || gd::Int(_sim->fight->mode) != GameMode::FORCE || !_glow.has_area()) return;
    if (_sim->fight->replay_playing != 0 || _sim->fight->hud_shown == 0) return;
    fill(_glow.position.x, _glow.position.y, _glow.size.x, _glow.size.y, _glow_colour);
}

// One key per key earned: copper, silver, gold (20 x 36 at x 0, 20, 40).
void ModeHudView::_keys(const gd::Ref<TekkenForce>& force) {
    for (gd::Int i = 0; i < 3; ++i) {
        if (force->g32(TekkenForce::KEYS) > i) {
            gfx::TextureRef tex = keys_vram->picture(KEY_VRAM.x, gd::Int(KEY_VRAM.y) + gd::Int(KEY_SIZE.y) * i, KEY_SIZE.x, KEY_SIZE.y,
                                                     (gd::Int(KEY_CLUT.y) << 6) | ((gd::Int(KEY_CLUT.x) + 16 * i) >> 4), 0);
            image(tex, gd::Int(KEY_SIZE.x) * i, KEY_Y, KEY_SIZE.x, KEY_SIZE.y);
        }
    }
}

gd::PI32 ModeHudView::_screen(const gd::PI32& point, const gd::Ref<ViewMatrix>& view) {
    if (view == nullptr) return gd::PI32{0, 0, 0};
    return view->screen_of(gd::make<JointFrame>(Fx::identity(), point));
}

// A picked-up item's LIFE UP!, rising half a pixel a frame, flashing (the placed items are
// ForceItemsView's, in the scene).
void ModeHudView::_life_up(const gd::Ref<TekkenForce>& force) {
    gd::Int clock = force->g32(TekkenForce::CLOCK);
    for (gd::Int i = 0; i < 2; ++i) {
        if (force->gu32(TekkenForce::ITEMS + 0xBC * i) == 2) {
            gd::Ref<TekkenForce::ForcePopup> p = force->item_popups[i];
            gd::PI32 s = _screen(p->point, p->view);
            gd::Int elapsed = clock - (p->until - 0x3C);
            gd::Int colour = (clock & 2) != 0 ? 2 : 5;
            print_fmt(ram_text(STR_LIFE_UP), {gd::Int(0), colour, gd::Int(s[0]) - 0x24, gd::Int(s[1]) - ((elapsed * 8) >> 4)});
        }
    }
}

// "+n SEC" over a defeated enemy, rising half a pixel a frame.
void ModeHudView::_seconds(const gd::Ref<TekkenForce>& force, gd::Int i) {
    gd::Ref<TekkenForce::ForcePopup> p = force->slot_popups[i];
    gd::PI32 s = _screen(p->point, p->view);
    gd::Int clock = force->g32(TekkenForce::CLOCK);
    gd::Int y = gd::Int(s[1]) - (((clock - (p->until - 0x3C)) * 8) >> 4);
    gd::Int kind = force->g16(TekkenForce::slot(i) + 0xE);
    gd::Int seconds = force_ram->u16(TekkenForce::KILL_SECONDS + 4 * kind + 2);
    float x = static_cast<float>(gd::Int(s[0]) - 0x1B);
    print_fmt("%f%c%H%V+", {gd::Int(0), gd::Int(5), _float_arg(x), y + 4});
    x += static_cast<float>(text_width("+", 0));
    print_fmt("%f%c%H%V%d", {gd::Int(1), gd::Int(5), _float_arg(x), y, seconds});
    x += static_cast<float>(text_width(gd::str(seconds), 1));
    print_fmt("%f%c%H%VSEC", {gd::Int(0), gd::Int(6), _float_arg(x), y + 6});
}

// The bonus time left over the player (FUN_800B1778 state 6).
void ModeHudView::_bonus_left(const gd::Ref<TekkenForce>& force) {
    gd::Int b = force->g32(TekkenForce::BONUS_TIME);
    gd::Ref<FighterState> p = force->player();
    if (b <= 0 || p->health <= 0) return;
    gd::PI32 s = _screen(gd::PI32{p->root_x, p->root_y - 500, p->root_z}, _sim->view);
    gd::Int seconds = Fx::div_trunc(b, 60);
    seconds -= Fx::div_trunc(seconds, 60) * 60;
    gd::Int hundredths = Fx::div_trunc(Fx::w32(b * 100), 60);
    hundredths -= Fx::div_trunc(hundredths, 100) * 100;
    print_fmt(ram_text(STR_BONUS_LEFT), {gd::Int(0), gd::Int(5), gd::Int(s[0]) - 0x16, gd::Int(s[1]), seconds, hundredths});
}

gd::PI32 ModeHudView::_mss(gd::Int t) {
    gd::Int m = Fx::div_trunc(t, 3600);
    gd::Int hundredths = Fx::div_trunc(t * 100, 60);
    return gd::PI32{m - Fx::div_trunc(m, 60) * 60, Fx::div_trunc(t, 60) - 60 * m, hundredths - Fx::div_trunc(hundredths, 100) * 100};
}

// The stage clear tally (state 7): the lines appear 30 frames apart.
void ModeHudView::_tally(const gd::Ref<TekkenForce>& force) {
    gd::PI32 lines = force->tally_lines();
    gd::Int t = lines[0];
    if (t >= TALLY_AT[0]) print_fmt(ram_text(STR_STAGE_BONUS), {gd::Int(1), gd::Int(4), gd::Int(0x2F), gd::Int(0xB4), gd::Int(lines[1])});
    if (t >= TALLY_AT[1]) print_fmt(ram_text(STR_CLEAR_BONUS), {gd::Int(1), gd::Int(1), gd::Int(0x2F), gd::Int(0xE8), gd::Int(lines[2])});
    gd::Int cs = 0;
    if (t >= TALLY_AT[2]) {
        gd::PI32 clear = _mss(lines[3]);
        cs = clear[2];
        print_fmt(ram_text(STR_CLEAR_TIME),
                  {gd::Int(1), gd::Int(5), gd::Int(0x2F), gd::Int(0x11C), gd::Int(clear[0]), gd::Int(clear[1]), gd::Int(clear[2])});
    }
    if (t < TALLY_AT[3]) return;
    switch (gd::Int(lines[5])) {
        case 0: {
            gd::PI32 bonus = _mss(lines[4]);
            gd::Int c = bonus[2];
            if (cs + c < 100) c = 100 - cs;
            print_fmt(ram_text(STR_BONUS_TIME), {gd::Int(1), gd::Int(6), gd::Int(0x2F), gd::Int(0x150), gd::Int(bonus[0]), gd::Int(bonus[1]), c});
            break;
        }
        case 1:
            print_fmt(ram_text(STR_KEY), {gd::Int(1), gd::Int((t & 2) != 0 ? 6 : 2), gd::Int(0x56), gd::Int(0x150)});
            break;
        case 2:
            print_fmt(ram_text(STR_FINAL), {gd::Int(1), gd::Int((t & 4) != 0 ? 6 : 2), gd::Int(0x22), gd::Int(0x150)});
            break;
        default:
            break;
    }
}

// ---- Tekken Ball

void ModeHudView::_ball(const gd::Ref<FightState>& fight) {
    gd::Ref<TekkenBall> tb = fight->ball;
    if (fight->replay_playing == 0 && fight->hud_shown != 0) {
        for (gd::Int g = 0; g < 2; ++g) _gauge(fight, tb, g);
    }
    for (const gd::Ref<TekkenBall::BallPopup>& p : tb->popups) {
        gd::PI32 s = _screen(p->point, p->view);
        gd::Vector2i shift = POPUP_SHIFT.get(p->font, gd::Vector2i(0, 0));   // Vector2i.ZERO
        // String.length(): the game's texts are ASCII, one byte per character.
        gd::Int x = gd::Int(s[0]) - gd::Int(shift.x) * static_cast<gd::Int>(p->text.length());
        gd::Int y = gd::Int(s[1]) - gd::Int(shift.y) - p->rise;
        print_fmt("%c%f%H%V" + p->text, {p->colour, p->font, x, y});
    }
}

// The ball's render kind 2 (a point): four additive Gouraud triangles in the 2D ordering table,
// white at the ball's centre, 0x303030 at the screen's bottom (the first two) or top edge, 8 x
// the kind's strength wide there, from the centre's x plus each streak's jitter.
void ModeHudView::_streaks() {
    if (!_shown || _sim == nullptr || gd::Int(_sim->fight->mode) != GameMode::BALL || _sim->fight->ball == nullptr) return;
    gd::Ref<ByteBlock> b = _sim->fight->ball->b;
    if (b->u8(TekkenBall::B_KIND) != 2) return;
    gd::PI32 c = _screen(gd::PI32{b->s32(TekkenBall::B_POS), b->s32(TekkenBall::B_POS + 4), b->s32(TekkenBall::B_POS + 8)}, _sim->view);
    gd::Int k = b->u8(TekkenBall::B_TIMER);
    gfx::Color edge = gfx::Color::c8(0x30, 0x30, 0x30);
    for (gd::Int i = 0; i < 4; ++i) {
        gd::Int x = gd::Int(c[0]) + gd::Int(_sim->fight->ball->streaks[i]);
        gd::Int y = (i & 2) != 0 ? 0 : STREAK_BOTTOM;
        // The packet's coordinates as the GPU takes them (the jitter can reach past 11 bits).
        gd::Arr<gd::Vector2i> v;
        gd::Arr<gd::Vector2i> corners{gd::Vector2i(c[0], c[1]), gd::Vector2i(x, y), gd::Vector2i(x + 8 * k, y)};
        for (const gd::Vector2i& p : corners) v.append(gd::Vector2i(gpu_coordinate(p.x), gpu_coordinate(p.y)));
        if (!gpu_draws(v)) continue;
        target->draw_primitive({pt(v[0].x, v[0].y), pt(v[1].x, v[1].y), pt(v[2].x, v[2].y)}, {gfx::Color::white(), edge, edge}, {});
    }
}

// A 76-pixel power gauge: the fill in the player's colour, the trail, the rest; the charging
// player's pulses.
void ModeHudView::_gauge(const gd::Ref<FightState>& fight, const gd::Ref<TekkenBall>& tb, gd::Int g) {
    gd::PI32 s = tb->gauge(g);
    gd::Int fill_px = s[0];
    gd::Int trail = s[1];
    bool right = s[2] == 1;
    gd::Int x = s[3];
    gd::Int y = s[4];
    gfx::Color tint = _times(gfx::Color(0.5f, 0.5f, 0.5f), 2.0f);
    if (tb->gauge_highlight == gd::shl(1, g) && fight->pause_request == 0) {
        double k = static_cast<double>(ScreenCanvas::triangle(fight->vblank << 4)) / 255.0;
        tint = _times(gfx::Color(static_cast<float>(k), static_cast<float>(k), static_cast<float>(k)), 2.0f);
    }
    image(hud->texture("bar_cap_left.png"), x, y, CAP.x, CAP.y, tint);
    image(hud->texture("bar_cap_right.png"), x + GAUGE_CAP_RIGHT, y, CAP.x, CAP.y, tint);
    float pos = static_cast<float>(right ? x + 2 : x + 0x4E);
    // `parts`: untyped [clut, length] rows, a struct here.
    struct Part {
        gd::Int clut;
        gd::Int n;
    };
    std::vector<Part> parts{{GAUGE_FILL_CLUTS[g], fill_px}, {0x7EDC, trail - fill_px}, {0x7EDF, GAUGE_WIDTH - trail}};
    for (const Part& part : parts) {
        gd::Int n = part.n;
        if (n <= 0) continue;
        float a = pos;
        if (right) {
            pos += static_cast<float>(n);
        } else {
            pos -= static_cast<float>(n);
            a = pos;
        }
        gfx::TextureRef tex = vram->sprite(0xE8, 0xA8, 2, 24, part.clut, 0x0D);
        image(tex, a, y, n, BAR_HEIGHT, tint);
    }
}

}  // namespace t3

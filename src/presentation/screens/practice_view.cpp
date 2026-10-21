// remake/presentation/screens/practice_view.gd
#include "presentation/screens/practice_view.h"

#include "content/fight_tables.h"
#include "content/game_ram.h"
#include "content/text_locale.h"
#include "content/vram_image.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_state.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_mode.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "gfx3d/math3d.h"
#include "presentation/hud/hud_frame.h"

namespace t3 {

namespace {

// String.replace(what, forwhat).
std::string _replaced(std::string s, const std::string& what, const std::string& forwhat) {
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + forwhat.size()))
        s.replace(at, what.size(), forwhat);
    return s;
}

}  // namespace

void PracticeView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image) {
    setup_psx(hud_data, game_ram, image);
}

void PracticeView::show_state(GameFlow* game) {
    flow = game;
    practice = game->practice;
    frame_count = game->fight->vblank;
    redraw();
}

gd::Int PracticeView::_b(gd::Int at) {
    return practice->s->u8(at);
}

gd::Int PracticeView::_sb(gd::Int at) {
    return practice->s->s8(at);
}

void PracticeView::_draw() {
    begin();
    if (flow == nullptr || gd::Int(flow->region->mode) != GameMode::PRACTICE) return;
    if (_b(PracticeMode::PAUSED) != 0) {
        // While COMMAND LIST is on the move lists (MoveListView) take the screen.
        if (_b(PracticeMode::COMMAND_LIST) == 0) {
            _backdrop();
            _page();
        }
    } else {
        _hud();
    }
}

// ---- the pause menu ---------------------------------------------------------------------------

// FUN_800B6DA0: the dark blue panel in its light grey frame.
void PracticeView::_backdrop() {
    if (_b(PracticeMode::MODE_SELECT) != 0) {
        fill(7, 0x26, 0xAE, 0xC4, psx(0xE0E0E0));
        fill(8, 0x28, 0xAC, 0xC0, psx(0x301000));
    } else if (_b(PracticeMode::SUB) < 3) {
        fill(0x3F, 0x26, 0xF2, 0x140, psx(0xE0E0E0));
        fill(0x40, 0x28, 0xF0, 0x13C, psx(0x301000));
    }
}

void PracticeView::_colours(gd::Int cursor_field) {
    _rows = gd::PI32();
    _rows.resize(0x11);
    _rows.fill(0xB);
    gd::Int c = _sb(cursor_field);
    if (c >= 0 && c < _rows.size()) _rows[c] = 0xA;
}

void PracticeView::_label(gd::Int row, gd::Int y, const std::string& label) {
    text(label, 0, _rows[row], X0, y);
}

void PracticeView::_value(gd::Int row, gd::Int y, gd::Int table, gd::Int index) {
    std::string v = ram->string(ram->u32(table + 4 * index));
    text(v, 0, _rows[row], X0 + (VALUE_COLUMNS - static_cast<gd::Int>(v.length())) * 9, y);
}

// FUN_800B57AC: the page of the sub-mode, or MODE SELECT.
void PracticeView::_page() {
    if (_b(PracticeMode::MODE_SELECT) != 0) {
        ptext(STR_MODE_SELECT, {10, 1, 0x10, 0x38});
        _colours(PracticeMode::SUB);
        gd::Arr<gd::Int> items{STR_FREE, STR_VS_CPU, STR_COMBO, STR_PLAYER_SELECT, STR_RESET};
        gd::Int n = items.size();
        for (gd::Int k = 0; k < n; ++k) ptext(items[k], {gd::Int(_rows[k]), 0, 0x10, 0x6E + 0x16 * k});
        return;
    }
    switch (_b(PracticeMode::SUB)) {
        case 0: _page_free(); break;
        case 1: _page_vs_cpu(); break;
        case 2: _page_combo(); break;
        default: break;
    }
}

void PracticeView::_head(gd::Int title, gd::Int cursor_field) {
    ptext(title, {10, 1, X0, 0x38});
    _colours(cursor_field);
    _label(0, 0x6E, ram->string(ram->u32(OK_CANCEL + 4 * _b(PracticeMode::CHANGED))));
    _label(1, 0x84, ram->string(STR_COMMAND_LIST));
}

// FUN_800B5978: the FREE page.
void PracticeView::_page_free() {
    _head(STR_FREE, PracticeMode::CURSOR_FREE);
    gd::Int y = 0xA2;
    _label(2, y, ram->string(STR_TRAINING_DUMMY));
    y += 0x16;
    gd::Int dummy = _sb(PracticeMode::DUMMY);
    if (dummy == 6 || dummy == 7) {
        _tech_roll(dummy - 6, X0 + 0x7E, y, _rows[2]);
    } else {
        _value(2, y, DUMMY_NAMES, dummy);
    }
    y += 0x16;
    _label(3, y, ram->string(STR_COUNTER));
    _value(3, y, OFF_ON, _b(PracticeMode::COUNTER_ATTACKS));
    _tail(4, y + 0x16, true);
}

// FUN_800B5ED4: the VS CPU page.
void PracticeView::_page_vs_cpu() {
    _head(STR_VS_CPU_TITLE, PracticeMode::CURSOR_VS);
    gd::Int y = 0xA2;
    _label(2, y, ram->string(STR_CPU_DIFFICULTY));
    _value(2, y, DIFFICULTY_NAMES, _sb(PracticeMode::CPU_DIFFICULTY));
    y += 0x16;
    _label(3, y, ram->string(STR_CPU_LEVEL));
    _value(3, y, LEVEL_NAMES, _sb(PracticeMode::CPU_LEVEL));
    _tail(4, y + 0x16, true);
}

// FUN_800B6368: the COMBO TRAINING page.
void PracticeView::_page_combo() {
    _head(STR_COMBO, PracticeMode::CURSOR_COMBO);
    gd::Int y = 0xA2;
    gd::Int pl = _sb(PracticeMode::COMBO_PLAYER);
    ptext(STR_COMBO_PLAYER, {gd::Int(_rows[2]), 0, X0, y, pl + 1});
    gd::Int key = flow->fight->fighters[gd::clampi(pl, 0, 1)]->costume_key;
    text(flow->fight->tables->character(key > 0x5C ? 0x58 : key)["name"].str(), 0, _rows[2], X0 | 0x87, y);
    y += 0x16;
    _label(3, y, ram->string(STR_COMBO_TYPE));
    if (_combo_count() != 0) {
        _value(3, y, COMBO_NAMES, _sb(PracticeMode::COMBO_TYPE));
    } else {
        text(ram->string(STR_NONE), 0, _rows[3], X0 + 0xBD, y);
    }
    _tail(4, y + 0x16, false);
}

gd::Int PracticeView::_combo_count() {
    return practice->combo_count();
}

// ATTACK DATA, FREEZE SIGNAL, REPLAY SETTINGS (and KEY DISPLAY), then RETURN TO MODE SELECT.
void PracticeView::_tail(gd::Int row, gd::Int y, bool key_display) {
    // The untyped rows [label string, value table, S field, signed]: a small struct.
    struct TailRow {
        gd::Int label;
        gd::Int table;
        gd::Int field;
        bool is_signed;
    };
    gd::Arr<TailRow> rows{TailRow{STR_ATTACK_DATA, OFF_ON, PracticeMode::ATTACK_DATA, false},
        TailRow{STR_FREEZE, OFF_ON, PracticeMode::FREEZE_SIGNAL, true},
        TailRow{STR_REPLAY, REPLAY_NAMES, PracticeMode::REPLAY_SETTING, true}};
    if (key_display) rows.append(TailRow{STR_KEY_DISPLAY, OFF_ON, PracticeMode::KEY_DISPLAY, false});
    gd::Int n = rows.size();
    for (gd::Int i = 0; i < n; ++i) {
        const TailRow& e = rows[i];
        _label(row, y, ram->string(e.label));
        gd::Int field = e.field;
        _value(row, y, e.table, e.is_signed ? _sb(field) : _b(field));
        if (i < rows.size() - 1) row += 1;
        y += 0x16;
    }
    text(ram->string(STR_RETURN), 0, _rows[row + 1], X0, 0x148);
}

// FUN_800B7704: TECH ROLL with an up or down arrow for its 'u' / 'd' marks.
void PracticeView::_tech_roll(gd::Int which, gd::Int x, gd::Int y, gd::Int colour) {
    _arrow_line(ram->string(ram->u32(TECH_ROLL_LINES + 4 * which)), x, y, colour);
}

// A line with an up or down arrow sprite in place of each 'u' / 'd'.
void PracticeView::_arrow_line(const std::string& line, gd::Int x, gd::Int y, gd::Int colour) {
    gd::Int clut = (((((colour >> 4) + 0x18) & 0x1F) + 0x1E0) << 6) | (colour & 0xF) | 0x10;
    gd::Int cx = x;
    gd::Int n = static_cast<gd::Int>(line.length());
    for (gd::Int i = 0; i < n; ++i) {
        gd::Int c = static_cast<unsigned char>(line[static_cast<size_t>(i)]);
        if (c == 0x75 || c == 0x64) sprt(cx, y, 0xA, 0x10, (clut << 16) | (c == 0x75 ? 0xE078 : 0xE082), 6);
        cx += 9;
    }
    text(_replaced(_replaced(line, "u", " "), "d", " "), 0, colour, x, y);
}

// ---- the practice HUD -------------------------------------------------------------------------

// FUN_800B6898: the prompts, the attack data, the hit markers and the key display.
void PracticeView::_hud() {
    if (flow->fight->practice_intro != 0) return;
    gd::Int sub = _sb(PracticeMode::SUB);
    gd::Arr<gd::Int> prompt;   // [string address, colour]
    if (_b(PracticeMode::GUIDE) != 0 && !(sub == 2 && _combo_count() == 0)) {
        gd::Int k = _sb(PracticeMode::GUIDE_STATE);
        if (k == PracticeMode::GuideState::READY) {
            prompt = sub == 2 ? gd::Arr<gd::Int>{HUD_PLAY_SELECT, 1} : gd::Arr<gd::Int>{HUD_PLAY_REC, 7};
        } else if (HUD_PROMPTS.has(k)) {
            prompt = HUD_PROMPTS.at(k);
        }
    } else if (_b(PracticeMode::PAUSED) == 0 && _sb(PracticeMode::REPLAY_SETTING) == 4 && sub == 0) {
        prompt = gd::Arr<gd::Int>{HUD_REPLAY_SELECT, 1};
    }
    if (_b(PracticeMode::KEY_DISPLAY) != 0) _key_display();
    if (_b(PracticeMode::ATTACK_DATA) != 0 && _b(PracticeMode::PAUSED) == 0) {
        _markers();
        _attack_data();
    }
    if (!prompt.is_empty()) {
        if (prompt[0] == HUD_PLAY_REC && ram->locale != nullptr && ram->locale->english) {
            // The USA release (FUN_800B6248) writes the line with a down arrow through its
            // QUICK ROLL routine, then its first letter again in colour 5.
            _arrow_line(ram->locale->practice_prompt, 9, 0x17B, 7);
            print_fmt("%c%h%vP", {gd::Int(5), gd::Int(1), gd::Int(0x15)});
        } else {
            ptext(prompt[0], {prompt[1], 1, 0x15});
        }
    }
}

void PracticeView::_attack_data() {
    gd::Int me = practice->pd(_b(PracticeMode::PLAYER), 0);
    gd::Int d = practice->pd(_b(PracticeMode::OTHER), 0);
    gd::Ref<ByteBlock> s = practice->s;
    // The last hit's damage (+0xE) against the best of the move (+0xC).
    gd::Int dmg = s->s16(d + PracticeMode::D_HIT_SHARE);
    gd::Int top = s->s16(d + PracticeMode::D_HIT_DAMAGE);
    ptext(HUD_TOTAL, {1, 0x17, 3, s->u32(me + PracticeMode::D_TOTAL)});
    ptext(HUD_TOTAL, {1, 1, 3, s->u32(d + PracticeMode::D_TOTAL)});
    gd::Int hits = s->s8(d + PracticeMode::D_BEST);
    if (hits >= 2 && s->s16(d + PracticeMode::D_MARKER) != 0 && s->s32(d + PracticeMode::D_SHOWN_DAMAGE) > 0) {
        _combo_counter(hits, s->s32(d + PracticeMode::D_SHOWN_DAMAGE));
    } else if (s->s16(d + PracticeMode::D_HITS) >= 2) {
        _combo_counter(0, s->s32(d + PracticeMode::D_SHOWN_DAMAGE));
    }
    gd::Int col = top < dmg ? 7 : (top == dmg ? 6 : 3);
    gd::Int pct = top != 0 && dmg != 0 ? Fx::div_trunc(dmg * 100, top) : 0;
    ptext(HUD_DMG, {col, 1, 4, dmg, pct});
    if (s->u8(d + PracticeMode::D_COUNTER) != 0) ptext(HUD_COUNTER, {2, 1, 5});
    if (s->u8(d + PracticeMode::D_CLEAN) != 0) ptext(HUD_CLEAN, {5, 1, 6});
}

// FUN_800B6F58: the big `n COMBO` and `n DAMAGE` counters.
void PracticeView::_combo_counter(gd::Int count, gd::Int damage) {
    if (count != 0) {
        ptext(HUD_FMT_NUMBER, {0xC, 0x88, 1, 6, count});
        ptext(HUD_FMT_COMBO, {count >= 10 ? 0x2A : 0x20, 0x88, 1, 5});
    }
    ptext(HUD_FMT_NUMBER, {0x16, 0xA6, 1, 6, damage});
    gd::Int a = gd::absi(damage);
    gd::Int x = a >= 100 ? 0x40 : (a < 10 ? 0x2A : 0x35);
    if (damage < 0) x += 0xD;
    ptext(HUD_FMT_DAMAGE, {x, 0xB0, 0, 7});
}

// FUN_800B75AC: the markers at the last four hit points (0 high, 1 mid, 2 low, 3 unblockable).
void PracticeView::_markers() {
    if (camera == nullptr || !camera->is_inside_tree()) return;
    gd::Int idx = _b(PracticeMode::MARKER_INDEX);
    for (gd::Int n = 0; n < 4; ++n) {
        if (idx < 0 || idx > 3) idx = 3;
        gd::Int m = PracticeMode::MARKERS + 8 * idx;
        if (practice->s->s16(m + 2) != 0) {
            gd::Int kind = MARKER_KINDS.get(practice->s->s16(m), 0);
            if (kind != 4) {
                gd::PI32 p = practice->marker_points[idx];
                Vector3 world = WorldSpace::point(static_cast<double>(gd::Int(p[0])), static_cast<double>(gd::Int(p[1])),
                                                  static_cast<double>(gd::Int(p[2])));
                if (!camera->is_position_behind(world)) {
                    gfx::Vec2 at = camera->unproject_position(world);
                    float x = (at.x - box.position.x) / box.size.x * SCREEN().x + HudFrame::LEFT;
                    float y = (at.y - box.position.y) / box.size.y * SCREEN().y;
                    _hit_marker(static_cast<gd::Int>(x), static_cast<gd::Int>(y), kind);
                }
            }
        }
        idx -= 1;
    }
}

void PracticeView::_hit_marker(gd::Int x, gd::Int y, gd::Int kind) {
    if (kind == 3) {
        sprt(x - 0x18, y - 0x18, 0x30, 0x30, 0x7FF7C0D0, 0x1F);
        sprt(x - 6, y - 0x18, 0xC, 0x30, 0x7FF6C090, 0x1F);
    } else {
        sprt(x - 0x18, y - 0x18, 0x30, 0x30, 0x7FB7C0D0, 0x1F);
        sprt(x - 0x18, y - 8, 0x30, 0x10, ram->u32(MARKER_UV + 4 * kind), 0x1F);
    }
}

// FUN_800B7C84: the last 15 inputs (directions and new buttons), oldest first.
void PracticeView::_key_display() {
    gd::Ref<ByteBlock> s = practice->s;
    gd::Int head = s->s32(PracticeMode::HEADS + 4 * _sb(PracticeMode::COMBO_PLAYER));
    gd::Int guide_end = s->s32(PracticeMode::REPLAY);
    gd::Int mode = _b(PracticeMode::PROMPT);
    gd::Int dir_col = 0;
    gd::Int btn_col = 1;
    gd::Int dir_guide = 0;
    gd::Int btn_guide = 1;
    if (mode == 2 || (mode == 3 && (_b(PracticeMode::PROMPT_TIMER) & 0x10) != 0)) {
        dir_col = 3;
        btn_col = 0;
        dir_guide = 3;
        btn_guide = 0;
    } else if (mode == 4 || mode == 5) {
        dir_guide = 3;
        btn_guide = 0;
    }
    gd::Int i = _wrap(head + 1);
    gd::Arr<gd::Vector2i> items;
    gd::Int since = 0;
    gd::Int prev_dir = 0;
    gd::Int prev_btn = 0;
    for (gd::Int n = 0; n < PracticeMode::RING_WORDS; ++n) {
        gd::Int e = PracticeMode::RING + 4 * i;
        if (i == 0) since = 0;
        gd::Int d = s->u8(e + 1);
        if (d != 0 && d != prev_dir) {
            items.append(gd::Vector2i((d << 12) & 0xFFFF, guide_end < i ? dir_guide : dir_col));
            since += 1;
        }
        gd::Int bt = s->u8(e);
        if ((bt & (bt ^ prev_btn)) != 0) {
            items.append(gd::Vector2i((bt << 4) & 0xFFFF, guide_end < i ? btn_guide : btn_col));
            since += 1;
        }
        prev_dir = d;
        prev_btn = bt;
        i = _wrap(i + 1);
    }
    gd::Int shown = gd::mini(items.size(), 15);
    if (_b(PracticeMode::GUIDE_PROMPT) != 0) shown = gd::mini(gd::mini(since, 15), items.size());
    gd::Int x = 0xC;
    gd::Int from = items.size() - shown;
    gd::Int to = items.size();
    for (gd::Int k = from; k < to; ++k) {
        gd::Vector2i item = items[k];
        if ((gd::Int(item.x) & 0xF000) != 0) {
            _key_arrow(x, 400, item.x, item.y);
            x += 0x17;
        } else if ((gd::Int(item.x) & 0xF0) != 0) {
            _key_button(x, 400, item.x, item.y);
            x += 0x17;
        }
    }
}

gd::Int PracticeView::_wrap(gd::Int i) {
    return i < 0 ? 0x31 : (i < 0x32 ? i : 0);
}

// FUN_800B721C: the pressed face buttons' icon.
void PracticeView::_key_button(gd::Int x, gd::Int y, gd::Int pad, gd::Int guide) {
    gd::Int idx = ((pad & 0x80) != 0 ? 1 : 0) | ((pad & 0x10) != 0 ? 2 : 0) | ((pad & 0x40) != 0 ? 4 : 0)
        | ((pad & 0x20) != 0 ? 8 : 0);
    gd::Int back = (((((guide != 0 ? 1 : 0) * 0x10 + 0x300) >> 4) & 0x3F)) | 0x7F80;
    sprt(x, y, 0x14, 0x20, (back << 16) | 0xE014, 6);
    sprt(x, y, 0x14, 0x20, ((((idx & 0x18) << 3) | 0x7F00 | (idx & 7) | 0x30) << 16) | 0xE000, 6);
}

// FUN_800B7354: a direction arrow (a flipped 19 x 31 quad).
void PracticeView::_key_arrow(gd::Int x, gd::Int y, gd::Int pad, gd::Int colour) {
    gd::Int idx = ARROW_INDEX.get(pad & 0xF000, 0);
    gd::Int base = ram->u16(ARROWS + 4 * idx);
    gd::Int flags = ram->u16(ARROWS + 4 * idx + 2);
    gd::Dict<gd::Int, gd::Int> cluts;
    cluts.set(1, 0x7FB3);
    cluts.set(2, 0x7FB4);
    cluts.set(3, 0x7FB5);
    gd::Int clut = cluts.get(colour, 0x7FB2);
    gfx::TextureRef tex = vram->sprite(base & 0xFF, base >> 8, 0x13, 0x1F, clut, 6);
    gfx::Rect2 dest = r(x, y, 0x13, 0x1F);
    if ((flags & 1) != 0) dest.size.x = -dest.size.x;
    if ((flags & 2) != 0) dest.size.y = -dest.size.y;
    target->draw_texture_rect(tex, dest, false);
}

}  // namespace t3

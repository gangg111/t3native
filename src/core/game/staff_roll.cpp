// remake/core/game/staff_roll.gd
// The staff roll after an arcade ending (ending.ovl FUN_80112798; modes.md#arcade-ending-and-
// staff-roll), written from the verified port `tools/research/screens_sim.py` (staff_roll) and the
// decompiled overlay (its sound and music). Each frame lists the lines to draw (`lines`) with
// their brightness and glow, and the Namco logo's brightness in the last phase.
#include "core/game/staff_roll.h"

#include <string>

#include "content/flow_data.h"
#include "core/fighter/character.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/math/fx.h"

namespace t3 {

namespace {

// String.unicode_at / substr on the roll's ASCII texts (bytes).
gd::Int _unicode_at(const std::string& s, gd::Int i) { return static_cast<unsigned char>(s[static_cast<size_t>(i)]); }
std::string _substr(const std::string& s, gd::Int from) {
    return static_cast<size_t>(from) < s.size() ? s.substr(static_cast<size_t>(from)) : std::string("");
}

}  // namespace

StaffRoll::StaffRoll(const gd::Ref<FlowData>& flow_data) {
    data = flow_data;
}

// A new roll (the overlay just loaded): the skip and end flags start clear.
void StaffRoll::reset() {
    skip = 0;
    end = 0;
}

// FUN_80112798(1): the set-up; returns the skip flag.
gd::Int StaffRoll::start() {
    phase = Phase::WAIT;
    offset = SCREEN_HEIGHT;
    first = 0;
    indent = 0x20;
    half = 0;
    clock = 0;
    hold = 0;
    fade = 0;
    base_bright = 0x78;
    rgb = BASE_RGB;
    clock += 1;
    return skip;
}

// FUN_80112798(0): one frame; true when the roll is over or skipped.
bool StaffRoll::step(GameFlow* g) {
    lines.clear();
    logo_bright = -1;
    _ball_new = g->progress->ball_new;
    _anna_start = (gd::Int(g->progress->start_costumes) >> Character::ANNA) & 1;
    if ((g->pressed_any() & SKIP_BUTTONS) != 0) {
        skip = 1;
    }
    gd::Int c = clock;
    switch (phase) {
        case Phase::WAIT:
            if (c > START_DELAY_FRAMES) {
                g->sound(SOUND_START);
                phase = Phase::MUSIC_DELAY;
                clock = 0;
            }
            break;
        case Phase::MUSIC_DELAY:
            if (c == MUSIC_DELAY_FRAMES) {
                g->music(MUSIC_ROLL);
                phase = Phase::MUSIC_LOAD;
            }
            break;
        case Phase::MUSIC_LOAD:
            if (g->music_ready()) {
                phase = Phase::TITLE_DELAY;
                first = 0;
                clock = 0;
            }
            break;
        case Phase::TITLE_DELAY:
            if (c == TITLE_DELAY_FRAMES) {
                phase = Phase::TITLES;
                clock = 0;
            }
            break;
        case Phase::TITLES:
            _titles(c);
            if (c == TITLES_FRAMES) {
                phase = Phase::SCROLL;
            }
            break;
        case Phase::SCROLL:
            if (!_scroll()) {
                return false;
            }
            break;
        case Phase::END_WAIT:
            if (c == END_WAIT_FRAMES) {
                clock = 0;
                phase = Phase::LOGO;
            }
            break;
        case Phase::LOGO:
            if (c > LOGO_FRAMES) {
                return true;
            }
            if (c < 0xB) {
                bright = (bright + 0xC) & 0xFF;
            } else if (c > 0x78) {
                bright = (bright - 2) & 0xFF;
            }
            logo_bright = bright;
            break;
    }
    half += 1;
    if (half == 2) {
        half = 0;
    }
    clock += 1;
    return skip != 0;
}

// FUN_801144B0: a string's width in pixels.
gd::Int StaffRoll::text_width(const std::string& text, bool wide) {
    gd::PI32 table = wide ? data->staff_font_wide : data->staff_font_narrow;
    gd::Int space = wide ? 8 : 10;
    gd::Int w = 0;
    gd::Int length = static_cast<gd::Int>(text.size());   // length(): ASCII texts
    for (gd::Int i = 0; i < length; ++i) {
        gd::Int ch = _unicode_at(text, i);
        if (ch < 0x31) {
            w += ch == 0x20 ? space : gd::Int(table[ch - 0x21]);
        } else {
            w += table[ch - 0x22];
        }
    }
    return w;
}

void StaffRoll::_place(const std::string& text, bool wide, gd::Int y) {
    gd::Int w = text_width(text, wide);
    _x0 = CENTRE - w / 2 + indent;
    _y = y;
    _width = w;
}

void StaffRoll::_line(const std::string& text, bool wide, bool glow) {
    gd::Ref<Line> l = gd::make<Line>();
    l->text = text;
    l->x = _x0;
    l->y = _y;
    l->wide = wide;
    l->bright = bright;
    l->glow = glow;
    l->rgb = rgb;
    l->width = _width;
    lines.append(l);
}

void StaffRoll::_glow_in(gd::Int k) {
    rgb = gd::Vector3i(Fx::s8(-3 - Fx::s8(gd::mini(k * 7, 0xFD))), Fx::s8(-0x25 - Fx::s8(gd::mini(k * 6, 0xDB))),
        Fx::s8(-0x4A - Fx::s8(gd::mini(k * 5, 0xB6))));
}

// Phase 4: the title block, glowing in and fading out.
void StaffRoll::_titles(gd::Int c) {
    bright = 0x5A;
    gd::Int count = data->staff_titles.size();   // the loop range is read once
    for (gd::Int i = 0; i < count; ++i) {
        std::string text = data->staff_titles[i];
        bool wide = i == 0;
        _place(text, wide, (wide ? 0 : ROW_HEIGHT) + TITLE_Y);
        bool glow = false;
        if (c < 0x25) {
            if (c < 0x1E) {
                gd::Int v = (((0x1B8 - _y) * 2) & 0xFF) | 1;
                if (v > 0x5A) {
                    v = 0x5A - ((v - 0x5A) >> 1);
                    bright = v & 0xFF;
                    if ((v & 0xFF) < 0x50) {
                        bright = 0x50;
                    }
                }
            }
            _glow_in(c);
            glow = true;
        } else if (c > 0xB4) {
            bright = ((0xD2 - c) * 3 + 1) & 0xFF;
            ramp = base_bright + (0xD2 - c) * -4;
        }
        if (c < 0xD2) {
            _line(text, wide, glow);
        }
    }
}

// A row's colour entering at the bottom (y 360–440).
void StaffRoll::_ramp_near_bottom(gd::Int y) {
    gd::Int k = Fx::w32((0x1B8 - y) & 0xFFFFFFFFLL);
    ramp = k;
    gd::Int v = ((k * 2) & 0xFF) | 1;
    if (v > 0x5A) {
        v = (0x5A - ((v - 0x5A) >> 1)) & 0xFFFFFFFFLL;
        bright = v & 0xFF;
        if ((v & 0xFF) < 0x50) {
            bright = 0x50;
        }
    }
    _glow_in(k);
}

// A row's colour leaving at the top (y 20–100).
void StaffRoll::_ramp_near_top(gd::Int y) {
    gd::Int k = 100 - y;
    ramp = k;
    bright = (y - 0x13) & 0xFF;
    rgb = gd::Vector3i(Fx::s8(gd::mini(Fx::div_trunc(k * 7, 2), 0xFD)), Fx::s8(gd::mini(k * 3, 0xDB)), Fx::s8(gd::mini(Fx::div_trunc(k * 5, 2), 0xB6)));
}

// The row's brightness by its place on the screen; true when it glows.
bool StaffRoll::_row_colour(gd::Int y_row, gd::Int y, gd::Int bottom, gd::Int top_lo, gd::Int top_len) {
    if (((y_row - bottom) & 0xFFFFFFFFLL) < 0x50) {
        _ramp_near_bottom(y);
        return true;
    }
    if (((y_row - top_lo) & 0xFFFFFFFFLL) < top_len) {
        _ramp_near_top(y);
        return true;
    }
    if (((y_row - top_lo) & 0xFFFFFFFFLL) < 0x1A4) {
        bright = 0x50;
        return false;
    }
    bright = 1;
    rgb = gd::Vector3i(Fx::s8(FULL_GLOW.x), Fx::s8(FULL_GLOW.y), Fx::s8(FULL_GLOW.z));
    return true;
}

// The end of the list: the hold (620 frames), the fade out and the glow out (80 each), counted
// once per frame (bug #27 not reproduced: the game counts once per heading row on screen).
// False when the roll is over (the phase changed).
bool StaffRoll::_end_tick() {
    if (end == 2 && fade == END_FADE) {
        end = 3;
        fade = 0;
    }
    if (end == 1) {
        hold += 1;
        if (hold == END_HOLD) {
            end = 2;
            hold = 0;
        }
    }
    if (end > 1) {
        fade += 1;
        if (end == 3 && fade == END_FADE) {
            end = 4;
            offset = 0;
            phase = Phase::END_WAIT;
            bright = 0;
            return false;
        }
    }
    return true;
}

// Phase.SCROLL: the rows scroll up; false returns from the frame early (the phase changed).
bool StaffRoll::_scroll() {
    gd::Arr<std::string> rows = data->staff_rows;
    if (offset < -ROW_HEIGHT) {
        first += 1;
        offset += ROW_HEIGHT;
        if (first >= rows.size()) {
            offset = 0;
            phase = Phase::END_WAIT;
            bright = 0;
            return false;
        }
    }
    bright = 0x5A;
    if (end != 0 && !_end_tick()) {
        return false;
    }
    gd::Int row = first;
    gd::Int y_row = offset;
    while (y_row < SCREEN_HEIGHT) {
        bool at_end = row >= rows.size();
        if (at_end && end == 0) {
            end = 1;
            break;
        }
        std::string entry = at_end ? std::string("") : rows[row];
        if (!at_end) {
            switch (_unicode_at(entry, 0)) {
                case 0x30: kind = 0; break;
                case 0x31: kind = 1; break;
                case 0x32: kind = 2; break;
                case 0x33: kind = _ball_new != 0 ? 3 : -1; break;
                case 0x34: kind = _ball_new != 0 ? -1 : 0; break;
                case 0x35: kind = _anna_start != 0 ? 1 : -1; break;
            }
        }
        // Rows are centred on their text without the leading kind digit (bug #32 not reproduced).
        std::string text = _substr(entry, 1);
        switch (kind) {
            case 1:
                _place(text, false, y_row);
                _line(text, false, _row_colour(y_row, y_row, 0x168, 0x14, 0x50));
                break;
            case 0: {
                _place(text, true, y_row + 4);
                bool glow = false;
                if (end == 0) {
                    glow = _row_colour(y_row, y_row + 4, 0x164, 0x10, 0x50);
                } else if (end > 1) {
                    gd::Int k = fade;
                    if (end == 2) {
                        bright = (END_FADE - k) & 0xFF;
                        ramp = k;
                        rgb = gd::Vector3i(Fx::s8(gd::mini(k * 7, 0xFD)), Fx::s8(gd::mini(k * 6, 0xDB)), Fx::s8(gd::mini(k * 5, 0xB6)));
                        glow = true;
                    } else {
                        bright = 1;
                        ramp = k;
                        _glow_in(k);
                        glow = true;
                    }
                } else {
                    bright = 0x50;
                }
                _line(text, true, glow);
                break;
            }
            case 3:
                _place(text, true, y_row + 0x14);
                _line(text, true, _row_colour(y_row, y_row + 0x14, 0x154, 0, 0x50));
                break;
        }
        row += 1;
        y_row += ROW_HEIGHT;
    }
    if (end == 0) {
        offset -= half == 0 ? 1 : 2;
    }
    return true;
}

}  // namespace t3

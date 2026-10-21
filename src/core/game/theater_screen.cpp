// remake/core/game/theater_screen.gd
// THEATER MODE (ending.ovl, game state 19 with 0x800AE158 set; modes.md#theater), written from
// the verified port `tools/research/theater_sim.py` (entry_status, movie_input, sound_input) and the
// decompiled state handler FUN_8010E8C0. The movie page (a grid of the Tekken 3 movies) and the
// sound player (the Tekken 3 tracks, arranged or arcade). The Tekken 1 and 2 disc mode is out of
// scope: the DISC button is never offered. Without movies the Theater opens on the sound page;
// without music the sound page is not offered.
#include "core/game/theater_screen.h"

#include <string>

#include "content/flow_data.h"
#include "content/json_file.h"
#include "core/fight/fight_simulation.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/sim_events.h"

namespace t3 {

TheaterScreen::TheaterScreen(GameFlow* flow) : FlowPart(flow) {}

// FUN_8010FC7C: the movie a list entry plays, −1 while locked (−2 marks an empty cell).
gd::Int TheaterScreen::entry_status(gd::Int i) {
    gd::Json e = g->data->theater_movies[i];
    gd::Int mask = JsonFile::number(e["mask"]);
    if (mask == 0) {
        return JsonFile::number(e["movie"]);
    }
    gd::Int cleared = g->progress->cleared;
    gd::Int cleared2 = g->progress->cleared2;
    switch (JsonFile::number(e["rule"])) {
        case 0:
            return (cleared & mask) != 0 ? JsonFile::number(e["movie"]) : -1;
        case 1:
        case 2: {
            // Kuma / Panda, Gun Jack's two endings.
            gd::Int bit = JsonFile::number(e["rule"]) == 1 ? 0x800 : 0x10000;
            if ((cleared2 & bit) != 0) {
                return JsonFile::number(e["alternative"]);
            }
            return (cleared & bit) != 0 ? JsonFile::number(e["movie"]) : -1;
        }
        case 3:
            // Tiger: Eddy's second-costume clear.
            return (cleared2 & 0x100) != 0 ? JsonFile::number(e["movie"]) : -1;
    }
    return -1;
}

// FUN_801109D4: 1 when no Tekken 3 movie is locked.
gd::Int TheaterScreen::all_movies() {
    gd::Int count = g->data->theater_movies.size();   // the loop range is read once
    for (gd::Int i = 0; i < count; ++i) {
        if (entry_status(i) == -1) {
            return 0;
        }
    }
    return 1;
}

// FUN_8010F634: an entry's name; names "0"–"3" stand for shared ending slots, named by the clears.
std::string TheaterScreen::movie_name(gd::Int i) {
    gd::Json e = g->data->theater_movies[i];
    std::string name = e["name"].str();
    if (!g->data->theater_names.has(name)) {
        return name;
    }
    gd::PI32 pair = SHARED_SLOTS.at(name);
    gd::Int c = g->progress->cleared;
    gd::Int second = gd::Arr<std::string>{"1", "2"}.has(name) ? gd::Int(g->progress->cleared2) : c;
    gd::Int which = ((c & pair[0]) != 0 ? 1 : 0) | ((second & pair[1]) != 0 ? 2 : 0);
    if (which == 0) {
        return "";
    }
    gd::Json names = g->data->theater_names[name];
    return names[which - 1].str();
}

// FUN_80111A08.
void TheaterScreen::reset() {
    cursor = 0;
    top = 0;
    focus = Focus::LIST;
}

// Sub-state 0 of Theater: its first page.
void TheaterScreen::open() {
    reset();
    if (!g->movies_available && music_available) {
        kind = PAGE_ARRANGE;
    }
    g->sub = 1;
}

// Sub-states 1 (the pages) and 10 (leaving).
void TheaterScreen::step() {
    switch (g->sub) {
        case 1:
            switch (_input()) {
                case Choice::PLAY:
                    if (kind == PAGE_MOVIES) {
                        g->sub = 2;
                    } else {
                        g->sim->events->add(SimEvents::Kind::MUSIC_TRACK, -1, track >> 1, track & 1);
                    }
                    break;
                case Choice::EXIT:
                    g->sub = 10;
                    break;
            }
            break;
        case 10:
            g->music_stop();
            g->goto_transition(GameFlow::State::MENU);
            break;
        default:
            g->sub = 1;
            break;
    }
}

// The movie has ended: back to the grid.
void TheaterScreen::after_movie() {
    g->sub = 1;
}

gd::Int TheaterScreen::_input() {
    return kind == PAGE_MOVIES ? _movie_input() : _sound_input();
}

gd::Vector2i TheaterScreen::_pads() {
    gd::Int pressed = g->pressed_any();
    gd::Int rep0 = g->repeat(0);
    gd::Int rep1 = g->repeat(1);
    return gd::Vector2i(pressed, rep0 | rep1);
}

// FUN_80111A68: the movie grid.
gd::Int TheaterScreen::_movie_input() {
    gd::Vector2i pads = _pads();
    gd::Int pressed = pads.x;
    gd::Int rep = pads.y;
    gd::Int count = g->data->theater_movies.size();
    gd::Int old = cursor;
    gd::Int focus0 = focus;
    gd::Int col = old % COLUMNS;
    gd::Int row = old / COLUMNS;
    bool sound_page = music_available && all_movies() != 0;
    if (focus0 == Focus::LIST) {
        if ((rep & PadState::LEFT) != 0) {
            if (col > 0 && entry_status(row * COLUMNS + col - 1) >= -1) {
                col -= 1;
            }
        } else if ((rep & PadState::RIGHT) != 0) {
            if (col < COLUMNS - 1 && entry_status(row * COLUMNS + col + 1) >= -1) {
                col += 1;
            }
        }
        if ((rep & PadState::UP) != 0) {
            if (row > 0) {
                row -= 1;
            }
            if (row < top) {
                top -= 1;
            }
        } else if ((rep & PadState::DOWN) != 0) {
            row += 1;
            if (row < count / COLUMNS) {
                if (entry_status(col + row * COLUMNS) < -1) {
                    // An empty cell: slide towards the middle.
                    gd::Int dir = col < 3 ? 1 : -1;
                    while (entry_status(col + row * COLUMNS) < -1) {
                        col += dir;
                    }
                }
            } else {
                focus = sound_page ? Focus::SWITCH : Focus::EXIT;
                row -= 1;
            }
            if (row >= top + ROWS_SHOWN) {
                top += 1;
            }
        }
    } else {
        if (sound_page) {
            if ((rep & PadState::LEFT) != 0 && focus0 == Focus::EXIT) {
                focus = Focus::SWITCH;
            } else if ((rep & PadState::RIGHT) != 0 && focus0 == Focus::SWITCH) {
                focus = Focus::EXIT;
            }
        }
        if ((rep & PadState::UP) != 0) {
            focus = Focus::LIST;
        }
    }
    gd::Int cur = col + row * COLUMNS;
    if (cur < 0) {
        cur = count - 1;
    } else if (cur >= count) {
        cur = 0;
    }
    cursor = cur;
    if (cur != old || focus != focus0) {
        g->sound(SOUND_MOVE);
    }
    if ((pressed & CHOOSE) == 0) {
        if ((pressed & PadState::SELECT) != 0) {
            g->sound(SOUND_OK);
            return Choice::EXIT;
        }
        return Choice::NONE;
    }
    switch (focus) {
        case Focus::LIST: {
            gd::Int s = entry_status(cursor);
            if (s < 0) {
                return Choice::NONE;
            }
            g->music_stop();
            movie = s;
            return Choice::PLAY;
        }
        case Focus::SWITCH:
            g->sound(SOUND_OK);
            reset();
            kind = PAGE_ARRANGE;
            break;
        case Focus::EXIT:
            g->sound(SOUND_OK);
            return Choice::EXIT;
    }
    return Choice::NONE;
}

// FUN_80111FEC: the sound list.
gd::Int TheaterScreen::_sound_input() {
    gd::Vector2i pads = _pads();
    gd::Int pressed = pads.x;
    gd::Int rep = pads.y;
    gd::Int old = cursor;
    gd::Int count = g->data->theater_tracks.size();
    gd::Int focus0 = focus;
    gd::Int cur = old;
    // Without movies the THEATER button (SWITCH) is not offered: the buttons start at BGM SELECT.
    gd::Int top_button = g->movies_available ? Focus::SWITCH : Focus::BGM;
    if (focus0 == Focus::LIST) {
        if ((rep & PadState::RIGHT) != 0) {
            focus = top_button;
        } else if ((rep & PadState::UP) != 0) {
            if (old > 0) {
                cur = old - 1;
                if (cur < top) {
                    top -= 1;
                }
            }
        } else if ((rep & PadState::DOWN) != 0) {
            if (old < count - 1) {
                cur = old + 1;
                if (cur >= top + LINES_SHOWN) {
                    top += 1;
                }
            }
        }
    } else if ((rep & PadState::LEFT) != 0) {
        focus = Focus::LIST;
    } else if ((rep & PadState::UP) != 0) {
        // Up: EXIT, (DISC,) BGM SELECT, THEATER.
        switch (focus0) {
            case Focus::EXIT: focus = Focus::BGM; break;
            case Focus::BGM: focus = top_button; break;
        }
    } else if ((rep & PadState::DOWN) != 0) {
        switch (focus0) {
            case Focus::SWITCH: focus = Focus::BGM; break;
            case Focus::BGM: focus = Focus::EXIT; break;
        }
    }
    cursor = cur;
    if (cur != old || focus != focus0) {
        g->sound(SOUND_MOVE);
    }
    if ((pressed & CHOOSE) == 0) {
        if ((pressed & PadState::SELECT) != 0) {
            g->sound(SOUND_OK);
            return Choice::EXIT;
        }
        return Choice::NONE;
    }
    switch (focus) {
        case Focus::LIST: {
            gd::Json entry = g->data->theater_tracks[cursor];
            track = JsonFile::number(entry["track"]);
            if (kind == PAGE_ARCADE) {
                track |= 1;
            }
            g->music_stop();
            return Choice::PLAY;
        }
        case Focus::SWITCH:
            g->music_stop();
            g->sound(SOUND_OK);
            reset();
            kind = PAGE_MOVIES;
            break;
        case Focus::BGM:
            g->music_stop();
            g->sound(SOUND_OK);
            kind ^= 1;
            break;
        case Focus::EXIT:
            g->sound(SOUND_OK);
            return Choice::EXIT;
    }
    return Choice::NONE;
}

}  // namespace t3

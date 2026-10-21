// remake/core/game/vs_screen.gd
// The pre-fight VS screen (FUN_80052808, game state 11; modes.md#pre-fight-vs-screen): the
// portraits, names and caption of the next fight slide in over 8 frames, stay for one more, and
// the game goes on to FightMain's loading (the state saved in 0x800AFF64/65).
//
// Sub-state 1 builds the screen objects (0x800B9380, the modes' overlay set-ups, VsModeSetups in
// draw_sim.py) and keeps the loaded pictures' ids in the progress block (0x800984E4–E7).
//
// Tekken Ball and Tekken Force read their layouts and captions from their overlays (mode_ram).
#include "core/game/vs_screen.h"

#include <string>

#include "content/fight_content.h"
#include "content/fight_tables.h"
#include "content/flow_data.h"
#include "content/game_ram.h"
#include "core/fight/fight_state.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"

namespace t3 {

namespace {

// GDScript String.replace (every occurrence; written here, the runtime has none).
std::string _replace(std::string s, const std::string& what, const std::string& with) {
    if (what.empty()) return s;
    size_t at = 0;
    while ((at = s.find(what, at)) != std::string::npos) {
        s.replace(at, what.size(), with);
        at += with.size();
    }
    return s;
}

}  // namespace

// The position this frame (FUN_80052538: the slide scaled by the frames left).
gd::Vector2i VsScreen::VsObject::at(gd::Int slide) {
    if (slide == 0) {
        return gd::Vector2i(x, y);
    }
    return gd::Vector2i(x + ((slide * dx) >> 3), y + ((slide * dy) >> 3));
}

VsScreen::VsScreen(GameFlow* flow) : FlowPart(flow) {}

void VsScreen::step() {
    shown = false;
    switch (g->sub) {
        case 0:
            g->display(0);
            g->sub = 1;
            break;
        case 1:
            _setup();
            slide = SLIDE_FRAMES;
            g->sub = 2;
            break;
        case 2:
            shown = true;
            slide -= 1;
            if (slide < 1) {
                g->sub = 3;
            }
            break;
        case 3:
            shown = true;
            g->sub = 4;
            break;
        case 4:
            g->state = GameFlow::target_state(g->region->vs_next_state);
            g->sub = g->region->vs_next_sub;
            break;
    }
}

// Sub-state 1: the objects of the mode, then the pictures to load (kept in the progress block).
void VsScreen::_setup() {
    objects.clear();
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int k0 = (gd::Int(gl->player_char[0]) << 2) | gd::Int(gl->player_costume[0]);
    gd::Int k1 = (gd::Int(gl->player_char[1]) << 2) | gd::Int(gl->player_costume[1]);
    _mode_setup(gd::PI32{k0, k1}, g->region->human_count != 1 ? 1 : 0);
    // A picture not loaded yet is read from the disc (LoadOverlaySync), which stops the music.
    gd::Ref<GameProgress> progress = g->progress;
    if (progress->s16(PROGRESS_BACKGROUND) != Fx::s16(background)) {
        progress->put16(PROGRESS_BACKGROUND, background);
        g->disc_read();
    }
    for (gd::Int side = 0; side < 2; ++side) {
        if (progress->u8(PROGRESS_PICTURES + side) != gd::Int(pictures[side])) {
            progress->put8(PROGRESS_PICTURES + side, pictures[side]);
            g->disc_read();
        }
    }
}

// VsModeSetups: arcade.ovl 0x800B3C70 (arcade, time attack), 0x800B3D70 (VS), 0x800B3E64 (team),
// 0x800B3F98 (survival), 0x800B408C (demonstration).
void VsScreen::_mode_setup(const gd::PI32& keys, gd::Int kind) {
    gd::Ref<FlowData> data = g->data;
    gd::Ref<ModeRegion> region = g->region;
    gd::Int mode = region->mode;
    if (mode == GameMode::BALL) {
        _ball_setup(keys, kind);
        return;
    }
    if (mode == GameMode::FORCE) {
        _force_setup(keys);
        return;
    }
    if (mode == GameMode::PRACTICE) {
        // practice.ovl FUN_800B713C.
        for (gd::Int side = 0; side < 2; ++side) {
            _add_portrait(side, keys[side], data->vs_practice_pos.slice(4 * side, 4 * side + 4));
            _add_name(keys[side], data->vs_practice_names.slice(4 * side, 4 * side + 4));
        }
        _add_text(data->vs_text, 0, 2, VS_TEXT[0], VS_TEXT[1], 0);
        _add_caption(data->vs_practice_texts["caption"].str(), 0);
        _add_background(0, 4);
        return;
    }
    gd::PI32 pos = mode == GameMode::TEAM ? data->vs_team_pos : data->vs_player_pos;
    gd::PI32 names = mode == GameMode::TEAM ? data->vs_team_names : data->vs_player_names;
    for (gd::Int side = 0; side < 2; ++side) {
        if (mode == GameMode::TEAM) {
            _team_icons(side);
        }
        _add_portrait(side, keys[side], pos.slice(4 * side, 4 * side + 4));
        _add_name(keys[side], names.slice(4 * side, 4 * side + 4));
    }
    _add_text(data->vs_text, 0, 2, VS_TEXT[0], VS_TEXT[1], 0);
    switch (mode) {
        case GameMode::DEMO:
            _add_caption(data->vs_demo, 0);
            _add_background(1, 1);
            break;
        case GameMode::VS:
            _add_caption(data->vs_round, region->ctx32(0x28) + 1);
            _add_background(1, 1);
            break;
        case GameMode::TEAM:
            _add_caption(data->vs_round, region->ctx8(0x58) + 1);
            _alloc(Kind::TEAM_BARS);
            _add_background(kind, 2);
            break;
        case GameMode::SURVIVAL:
            _add_caption(data->vs_round, region->ctx32(0x44) + 1);
            _add_background(0, 3);
            break;
        default:
            _add_caption(data->vs_stage, region->ctx32(0x24) + 1);
            _add_background(kind, kind);
            break;
    }
}

// volley.ovl FUN_800B55B4: both sides as in versus, captioned TEKKEN BALL.
void VsScreen::_ball_setup(const gd::PI32& keys, gd::Int kind) {
    gd::Ref<GameRam> ram = g->content->mode_ram("volley");
    for (gd::Int side = 0; side < 2; ++side) {
        _add_portrait(side, keys[side], _ram_pos(ram, BALL_POS + 8 * side));
        _add_name(keys[side], _ram_pos(ram, BALL_NAMES + 8 * side));
    }
    _add_text(_ram_text(ram, BALL_VS), 0, 2, VS_TEXT[0], VS_TEXT[1], 0);
    _add_caption(_ram_text(ram, BALL_CAPTION), 0);
    _add_background(kind, BALL_CAPTION_KIND);
}

// force.ovl FUN_800B6648: the player's portrait and name, TEKKEN FORCE, STAGE n / FINAL STAGE,
// and the level's name sliding in on the right; the other side has no big picture.
void VsScreen::_force_setup(const gd::PI32& keys) {
    gd::Ref<GameRam> ram = g->content->mode_ram("force");
    gd::Ref<ModeRegion> region = g->region;
    gd::Int level = region->ctx32(0x24);
    gd::Int side = region->ctx8(FORCE_SIDE) != 0 ? 1 : 0;
    _add_portrait(side, keys[side], _ram_pos(ram, FORCE_POS));
    pictures[side ^ 1] = NO_PICTURE;
    _add_name(keys[side], _ram_pos(ram, FORCE_NAME));
    _add_caption(_ram_text(ram, FORCE_CAPTION), 0);
    if (level < FORCE_FINAL_LEVEL) {
        _add_text(_ram_text(ram, FORCE_STAGE), 6, 1, STAGE_AT[0], STAGE_AT[1], level + 1);
    } else {
        _add_text(_ram_text(ram, FORCE_FINAL), 6, 1, STAGE_AT[0], STAGE_AT[1], 0);
    }
    gd::PI32 pos = _ram_pos(ram, FORCE_LEVEL_NAME);
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<VsObject> o = _alloc(Kind::NAME);
        if (o == nullptr) {
            break;
        }
        _place(o, pos);
        o->font = 3;
        o->colour = ram->u8(FORCE_LEVEL_COLOURS + level);
        o->text = ram->string(ram->u32(FORCE_LEVEL_NAMES + 8 * level + 4 * i));
        o->x = Fx::s16(pos[0] - static_cast<gd::Int>(o->text.size()) * NAME_WIDTH);   // length(): ASCII names
        pos[1] += LEVEL_NAME_STEP;
    }
    _add_background(0, FORCE_CAPTION_KIND);
}

// Four s16 of an overlay table: x, y and the slide offsets.
gd::PI32 VsScreen::_ram_pos(const gd::Ref<GameRam>& ram, gd::Int address) {
    return gd::PI32{ram->s16(address), ram->s16(address + 2), ram->s16(address + 4), ram->s16(address + 6)};
}

std::string VsScreen::_ram_text(const gd::Ref<GameRam>& ram, gd::Int address) {
    return _replace(ram->string(address), TEXT_CODES, "");
}

// FUN_80051C64: a new object (null when all 64 are used).
gd::Ref<VsScreen::VsObject> VsScreen::_alloc(gd::Int kind) {
    if (objects.size() >= MAX_OBJECTS) {
        return nullptr;
    }
    gd::Ref<VsObject> o = gd::make<VsObject>();
    o->kind = kind;
    objects.append(o);
    return o;
}

void VsScreen::_place(const gd::Ref<VsObject>& o, const gd::PI32& pos) {
    o->x = pos[0];
    o->y = pos[1];
    o->dx = pos[2];
    o->dy = pos[3];
}

// FUN_800523FC: a side's big picture, facing the other side.
void VsScreen::_add_portrait(gd::Int side, gd::Int key, const gd::PI32& pos) {
    gd::Ref<VsObject> o = _alloc(Kind::PORTRAIT);
    if (o == nullptr) {
        return;
    }
    gd::Int word = g->data->attribute(key >> 2, key & 3);
    o->colour = word >> 8;
    o->facing = (((word & 0xFF) >> 5) ^ (side != 0 ? 1 : 0)) & 1;
    o->picture = word & 0x1F;
    pictures[side] = o->picture;
    _place(o, pos);
}

// FUN_80052650: a side's name (colour 8, font 3), kept on screen; a negative x right-aligns.
void VsScreen::_add_name(gd::Int key, const gd::PI32& pos) {
    gd::Ref<VsObject> o = _alloc(Kind::NAME);
    if (o == nullptr) {
        return;
    }
    _place(o, pos);
    o->text = g->fight->tables->character(key > 0x5C ? KEY_NONE : key)["name"].str();
    o->colour = 8;
    o->font = 3;
    gd::Int w = static_cast<gd::Int>(o->text.size()) * NAME_WIDTH;   // length(): ASCII names
    gd::Int right = o->x + w;
    gd::Int left = o->x;
    if (o->x < 0) {
        right = -o->x;
        left = -o->x - w;
    }
    left = gd::maxi(left, NAME_LEFT);
    gd::Int v = left;
    if (right > NAME_RIGHT) {
        v += NAME_RIGHT - right;
    }
    o->x = Fx::s16(v);
    if (o->dx != 0) {
        o->dx = Fx::s16(o->dx < 1 ? -(o->x + w) : SCREEN_WIDTH - o->x);
    }
}

// FUN_800525DC: a text shown at rest.
void VsScreen::_add_text(const std::string& text, gd::Int colour, gd::Int font, gd::Int x, gd::Int y, gd::Int arg) {
    gd::Ref<VsObject> o = _alloc(Kind::TEXT);
    if (o == nullptr) {
        return;
    }
    o->text = text;
    o->colour = colour;
    o->font = font;
    o->x = x;
    o->y = y;
    o->arg = arg;
}

void VsScreen::_add_caption(const std::string& text, gd::Int arg) {
    _add_text(text, 6, 1, CAPTION_AT[0], CAPTION_AT[1], arg);
}

// FUN_80051FC4: the caption bar and the tiled background.
void VsScreen::_add_background(gd::Int kind, gd::Int caption) {
    gd::Ref<VsObject> o = _alloc(Kind::CAPTION_BAR);
    if (o == nullptr) {
        return;
    }
    o->y = 0x24;
    o->colour = g->data->vs_caption_colours[caption];
    o->dx = g->data->vs_caption_widths[caption];
    if (_alloc(Kind::BACKGROUND) != nullptr) {
        background = g->data->vs_backgrounds[kind];
    }
}

// arcade.ovl FUN_800B3928: a team's members but the one fighting (the beaten ones dimmed, the
// ones beyond the team's count locked), and a frame around them.
void VsScreen::_team_icons(gd::Int side) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Int team = TEAM_AT + TEAM_STRIDE * side;
    gd::Int n = gd::mini(region->ctx8(team + 0xC), 8);
    gd::PI32 icons = g->data->vs_team_icons;
    gd::Int x = icons[4 * side];
    gd::Int y = icons[4 * side + 1];
    gd::Int step_x = icons[4 * side + 2];
    if (n == 8) {
        x = icons[4 * side + 3];
    }
    gd::Int w = (step_x << 3) & 0xFFFF;
    gd::Int fx = (x - 1) & 0xFFFF;
    gd::Int fy = (y - 2) & 0xFFFF;
    gd::Int fw = ((n - 1) * 0x21 + 1) & 0xFFFF;
    if (side != 0) {
        fx = (fx + (n - 2) * step_x) & 0xFFFF;
    }
    gd::Int current = region->ctx8(team + 9);
    gd::Int last = region->ctx8(team + 10);
    if (!(current < last)) {
        last = current + 1;
    }
    // range(current, n) + range(ctx8(team + 9)).
    gd::Arr<gd::Int> order;
    for (gd::Int i = current; i < n; ++i) order.append(i);
    gd::Int head = region->ctx8(team + 9);
    for (gd::Int i = 0; i < head; ++i) order.append(i);
    for (gd::Int i : order) {
        gd::Int key = gd::mini(region->ctx8(team + i), KEY_NONE);
        gd::Int flags = ICON_FLAGS;
        if (i >= last) {
            key = KEY_NONE;
        } else if (i < current) {
            flags = ICON_BEATEN;
        } else if (i == current) {
            continue;
        }
        gd::Ref<VsObject> o = _alloc(Kind::ICON);
        if (o != nullptr) {
            o->arg = key;
            o->flags = flags;
            o->x = Fx::s16(x);
            o->y = Fx::s16(y);
            o->dx = Fx::s16(w);
        }
        x = (x + step_x) & 0xFFFF;
    }
    if (n >= 2) {
        gd::Ref<VsObject> o = _alloc(Kind::FRAME);
        if (o != nullptr) {
            o->x = Fx::s16(fx);
            o->y = Fx::s16(fy);
            o->dx = fw;
            o->dy = 0x3E;
        }
    }
}

}  // namespace t3

// remake/presentation/hud/fight_hud.gd
#include "presentation/hud/fight_hud.h"

#include <vector>

#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_state.h"
#include "core/game/game_mode.h"
#include "core/sim_events.h"
#include "presentation/hud/hud_frame.h"
#include "presentation/hud/hud_text.h"

namespace t3 {

void FightHud::setup(const gd::Ref<HudData>& hud_data, const gd::PI32& costume_slots) {
    data = hud_data;
    _slots = costume_slots.duplicate();
    // texture_filter = TEXTURE_FILTER_NEAREST is set on the canvas in `draw`;
    // mouse_filter = MOUSE_FILTER_IGNORE, set_anchors_and_offsets_preset(PRESET_FULL_RECT): the view
    // takes no input and covers the window (`draw` gets the window's size).
}

// A record's costume swapped in during the fight (its name plate).
void FightHud::set_slot(gd::Int record, gd::Int costume_slot) {
    if (record < _slots.size()) _slots[record] = costume_slot;
}

// After a simulation step.
void FightHud::show_state(const gd::Ref<FightSimulation>& sim) {
    _fight = sim->fight;
    _replay = sim->events->has(SimEvents::Kind::REPLAY_TEXT);
    // queue_redraw(): drawn every frame.
}

// The original 4:3 frame in the window (CameraRig.frame_rect, normalised).
void FightHud::place(gfx::Rect2 frame_rect) {
    gfx::Vec2 window = size;
    // Rect2(frame_rect.position * window, frame_rect.size * window): Vector2 * Vector2 per component.
    gfx::Rect2 frame(gfx::Vec2(frame_rect.position.x * window.x, frame_rect.position.y * window.y),
                     gfx::Vec2(frame_rect.size.x * window.x, frame_rect.size.y * window.y));
    // frame != box (gfx::Rect2 has no operator!=).
    if (frame.position.x != box.position.x || frame.position.y != box.position.y || frame.size.x != box.size.x ||
        frame.size.y != box.size.y) {
        box = frame;
        // queue_redraw(): drawn every frame.
    }
}

gfx::Vec2 FightHud::point(float x, float y) const {
    gfx::Vec2 p = HudFrame::point(box, x + CENTRE_SHIFT, y);
    // Native: on a window taller than the 4:3 frame (a foldable unfolded) the HUD keeps its place
    // from the window's top, not the frame's: all of it is at the top (timer, bars, marks, names).
    p.y -= std::max(0.0f, box.position.y);
    return p;
}

gfx::Vec2 FightHud::unit_scale() const {
    return HudFrame::unit_scale(box);
}

// A rectangle in HUD units.
gfx::Rect2 FightHud::rect(float x, float y, float w, float h) const {
    // Vector2(w, h) * unit_scale(): per component.
    gfx::Vec2 scale = unit_scale();
    return gfx::Rect2(point(x, y), gfx::Vec2(w * scale.x, h * scale.y));
}

void FightHud::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    size = window;
    target = &canvas;
    canvas.set_filter(gfx::Filter::NEAREST);
    _draw();
    target = nullptr;
}

void FightHud::_draw() {
    if (_fight == nullptr || data == nullptr || box.size.x <= 0) return;
    if (_fight->hud_shown != 0 && _fight->replay_playing == 0) {
        // Tekken Force draws its own bars and names (ModeHudView) and no round marks.
        if (gd::Int(_fight->mode) != GameMode::FORCE) {
            for (gd::Int p = 0; p < 2; ++p) {
                _draw_bar(p);
                _draw_marks(p);
                _draw_name(p);
            }
        }
        _draw_timer();
    }
    if (_fight->replay_playing == 0) {
        gd::Arr<gd::Ref<RoundHud::Message>> messages = RoundHud::messages(_fight);
        for (const auto& m : messages) draw_text_at(*target, m->text, m->font, m->palette, m->x, m->y);
    }
    if (_replay) draw_text_at(*target, "REPLAY", REPLAY_FONT, REPLAY_COLOUR, REPLAY_POS.x, REPLAY_POS.y);
    for (const auto& m : flow_texts) draw_text_at(*target, m->text, m->font, m->palette, m->x, m->y);
}

// FUN_8004E55C: the fills (health innermost, recent damage, empty) under the two end caps.
void FightHud::_draw_bar(gd::Int p) {
    gd::PI32 bar = _fight->hud_bars[p];
    float x = BAR_X[p];
    float drawn = static_cast<float>(gd::Int(bar[2]));
    float recent = static_cast<float>(gd::maxi(bar[3], bar[2]));
    // `parts`: untyped [texture, length] rows, a struct here.
    struct Part {
        std::string file;
        float length;
    };
    std::vector<Part> parts{{"bar_health.png", drawn}, {"bar_damage.png", recent - drawn}, {"bar_empty.png", BAR_LENGTH - recent}};
    float pen = p == 1 ? x + 2.0f : x + 2.0f + BAR_LENGTH;
    for (const Part& part : parts) {
        float length = part.length;
        if (length <= 0) continue;
        float from = p == 1 ? pen : pen - length;
        target->draw_texture_rect_region(data->texture(part.file), rect(from, BAR_Y, length, BAR_HEIGHT), BAR_FILL_SOURCE);
        pen = p == 1 ? pen + length : pen - length;
    }
    target->draw_texture_rect(data->texture("bar_cap_left.png"), rect(x, BAR_Y, BAR_CAP.x, BAR_CAP.y), false);
    target->draw_texture_rect(data->texture("bar_cap_right.png"), rect(x + BAR_RIGHT_CAP, BAR_Y, BAR_CAP.x, BAR_CAP.y), false);
}

// FUN_8004E410: the seconds left, (frames + 59) / 60, or infinity; units first, tens 15 to the left.
void FightHud::_draw_timer() {
    gd::PI32 digits{INFINITE[0], INFINITE[1]};   // PackedInt32Array(INFINITE)
    if (_fight->timer_stopped == 0) {
        gd::Int seconds = gd::clampi((_fight->timer + 59) / 60, 0, 99);
        digits = gd::PI32{seconds % 10, seconds / 10};
    }
    for (gd::Int i = 0; i < 2; ++i) {
        gfx::TextureRef tex = data->texture("digit_" + gd::str(gd::Int(digits[i])) + ".png");
        target->draw_texture_rect(tex, rect(TIMER_UNITS_X - static_cast<float>(i) * TIMER_STEP, TIMER_Y, DIGIT.x, DIGIT.y), false);
    }
}

// FUN_8004E87C: one mark per round to win; the newest won mark blinks.
void FightHud::_draw_marks(gd::Int p) {
    gd::Int wins = _fight->fighters[p]->round_wins;
    float x = MARK_X[p];
    gd::Int rounds = _fight->rounds_to_win;
    for (gd::Int i = 0; i < rounds; ++i) {
        bool empty = i >= wins;
        if (i == wins - 1 && _fight->hud_wins_seen[p] != wins) empty = (_fight->vblank & 0x10) != 0;
        gfx::TextureRef tex = data->texture(empty ? "mark_empty.png" : "mark_won.png");
        target->draw_texture_rect(tex, rect(x, MARK_Y, MARK.x, MARK.y), false);
        x += MARK_STEP[p];
    }
}

// FUN_8004EB9C: the name sprite on its backing quad.
void FightHud::_draw_name(gd::Int p) {
    gd::Int slot = _slots[p];
    gfx::TextureRef tex = data->name_texture(slot);
    if (tex == nullptr) return;
    float width = static_cast<float>(data->name_width(slot));
    gfx::Vec2 at = NAME_POS[p];
    float x = p == 0 ? at.x : at.x - width;
    gfx::Rect2 plate = rect(x - PLATE_MARGIN, at.y + PLATE_TOP, width + 2.0f * PLATE_MARGIN, PLATE_BOTTOM - PLATE_TOP);
    target->draw_texture_rect_region(data->plate_texture(slot), plate, PLATE_SOURCE);
    target->draw_texture_rect(tex, rect(x, at.y, width, NAME_HEIGHT), false);
}

// FUN_8004D15C: glyphs of a font and colour from (x, y), one advance per character.
void FightHud::draw_text_at(gfx::Canvas& item, const std::string& text, gd::Int font_index, gd::Int colour, float x, float y) {
    HudText::draw(item, *data, text, font_index, colour, x, y, HudText::layout(point(0, 0), point(1, 1)));
}

}  // namespace t3

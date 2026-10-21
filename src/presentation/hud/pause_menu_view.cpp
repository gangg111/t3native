// remake/presentation/hud/pause_menu_view.gd
#include "presentation/hud/pause_menu_view.h"

#include "content/text_locale.h"
#include "content/texture_packs.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/sim_events.h"
#include "gd/io.h"
#include "presentation/hud/fight_hud.h"

namespace t3 {

void PauseMenuView::setup(FightHud* fight_hud) {
    hud = fight_hud;
    // mouse_filter = MOUSE_FILTER_IGNORE, set_anchors_and_offsets_preset(PRESET_FULL_RECT): the view
    // takes no input and covers the window (`draw` gets the window's size);
    // texture_filter = TEXTURE_FILTER_NEAREST is set on the canvas in `draw`.
}

// Drops the rules picture so the next HOW TO page loads it in the texts' language.
void PauseMenuView::reset_pictures() {
    _how_to_picture = nullptr;
}

void PauseMenuView::show_state(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<SimEvents::Event> menu = sim->events->first(SimEvents::Kind::PAUSE_MENU);
    player = menu != nullptr ? menu->a : 0;
    cursor = sim->fight->pause_cursor;
    items = PauseMenu::item_count(sim->fight);
    blink = (sim->fight->vblank & 0x20) != 0;
    how_to = sim->fight->paused_player != 0 && sim->fight->pause_page == PauseMenu::PAGE_HOW_TO;
    if (how_to && _how_to_picture == nullptr) {
        std::string path = gd::path_join(assets_root, "stages/v/how_to.png");   // Assets.path(...)
        _how_to_picture = TexturePacks::texture(locale != nullptr ? locale->file_or("how_to.png", path) : path);
    }
    visible = player != 0 || how_to;
    // queue_redraw(): drawn every frame.
}

void PauseMenuView::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    size = window;
    target = &canvas;
    canvas.set_filter(gfx::Filter::NEAREST);
    _draw();
    target = nullptr;
}

void PauseMenuView::_draw() {
    if (hud == nullptr) return;
    if (how_to) {
        _draw_how_to();
        return;
    }
    if (player == 0) return;
    float h = items < PauseMenu::ITEMS.size() ? BOX().size.y : static_cast<float>(BALL_BOX_HEIGHT);
    target->draw_rect(hud->rect(BORDER().position.x, BORDER().position.y, BORDER().size.x, h + 4), BORDER_COLOUR());
    target->draw_rect(hud->rect(BOX().position.x, BOX().position.y, BOX().size.x, h), BOX_COLOUR());
    for (gd::Int i = 0; i < items; ++i) {
        std::string text = PauseMenu::ITEMS[i];
        if (locale != nullptr) text = locale->text("exe", text);
        gd::Int x = ITEM_CENTRE - ((static_cast<gd::Int>(text.length()) * ITEM_ADVANCE) >> 1);
        hud->draw_text_at(*target, text, 0, i == cursor ? SELECTED : OTHER, x, ITEM_Y + i * ITEM_STEP);
    }
    if (blink) {
        hud->draw_text_at(*target, gd::str(player) + "P PAUSE", 1, LABEL_COLOUR, player == 1 ? LABEL_LEFT : LABEL_RIGHT,
                          LABEL_Y);
    }
}

// volley.ovl FUN_800B4088: the rules picture (216 x 204) on a white card with a black shadow.
void PauseMenuView::_draw_how_to() {
    if (_how_to_picture != nullptr && _how_to_picture->width() == RULES_WIDTH) {
        target->draw_rect(hud->rect(RULES_SHADOW().position.x, RULES_SHADOW().position.y, RULES_SHADOW().size.x,
                                    RULES_SHADOW().size.y),
                          gfx::Color::black());
        target->draw_rect(hud->rect(RULES_CARD().position.x, RULES_CARD().position.y, RULES_CARD().size.x, RULES_CARD().size.y),
                          gfx::Color::white());
        target->draw_texture_rect(_how_to_picture, hud->rect(RULES_AT().x, RULES_AT().y, RULES_WIDTH, HOW_TO_SIZE().y), false);
        return;
    }
    target->draw_rect(hud->rect(HOW_TO_SHADOW().position.x, HOW_TO_SHADOW().position.y, HOW_TO_SHADOW().size.x,
                                HOW_TO_SHADOW().size.y),
                      gfx::Color::black());
    target->draw_rect(hud->rect(HOW_TO_CARD().position.x, HOW_TO_CARD().position.y, HOW_TO_CARD().size.x, HOW_TO_CARD().size.y),
                      gfx::Color::c8(0xF0, 0xF0, 0xF0));
    if (_how_to_picture != nullptr)
        target->draw_texture_rect(_how_to_picture, hud->rect(HOW_TO_AT().x, HOW_TO_AT().y, HOW_TO_SIZE().x, HOW_TO_SIZE().y),
                                  false);
}

}  // namespace t3

#include "presentation/screens/main_menu_view.h"

#include "content/flow_data.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

void MainMenuView::setup(const gd::Ref<HudData>& hud_data, const gfx::TextureRef& title_picture) {
    setup_canvas(hud_data);
    title = title_picture;
    add_pass(gfx::Blend::MIX, [this] { _list(); }, BAND());
    add_pass(gfx::Blend::SUB, [this] { _fades(); }, BAND());
}

void MainMenuView::show_state(const gd::Ref<MainMenu>& main_menu, gd::Int frames) {
    menu = main_menu;
    frame_count = frames;
    redraw();
}

void MainMenuView::_draw() {
    begin();
    black();
    image(title, 0, 0, static_cast<float>(SCREEN_X), static_cast<float>(SCREEN_Y));
    if (menu && menu->scroll == 0) _bars(menu->glow);
    if (!footer.empty()) centred(footer, 1, footer_colour, static_cast<float>(CENTRE), static_cast<float>(FOOTER_Y));
}

// The seven entries around the cursor.
void MainMenuView::_list() {
    if (!page.empty()) {
        gd::Int y = LIST_Y;
        for (gd::Int i = 0; i < 7; ++i) {
            gd::Int k = page_cursor - 3 + i;
            if (k >= 0 && k < static_cast<gd::Int>(page.size()) && !page[static_cast<size_t>(k)].empty()) {
                gd::Int colour = static_cast<size_t>(k) < page_colours.size() ? page_colours[static_cast<size_t>(k)] : 6;
                centred(page[static_cast<size_t>(k)], 1, colour, static_cast<float>(CENTRE), static_cast<float>(y));
            }
            y += LINE;
        }
        return;
    }
    if (!menu || menu->entries.is_empty()) return;
    gd::Int count = menu->entries.size();
    gd::Int cur = menu->cursor();
    gd::Int index = cur < 3 ? (cur - 3 + count) : cur - 3;
    gd::Int y = menu->scroll + LIST_Y;
    for (gd::Int i = 0; i < 7; ++i) {
        index = gd::mod(index, count);
        const auto& e = menu->entries[index];
        gd::Int colour = e->entry->index;
        if (menu->chosen == 0 && e->new_ && (frame_count & 3) == 0) colour = NEW_COLOUR;
        centred(e->entry->text, 1, colour, static_cast<float>(CENTRE), static_cast<float>(y));
        index += 1;
        y += LINE;
    }
}

// The band's edges darkened (subtracted gradients).
void MainMenuView::_fades() {
    if (!menu) return;
    gradient(0x44, 0x13C, 0xE8, 0x1C, psx(0xE0E0E0), psx(0xE0E0E0), gfx::Color::black(), gfx::Color::black());
    gradient(0x44, 0x194, 0xE8, 0x1C, gfx::Color::black(), gfx::Color::black(), psx(0xF8F8F8), psx(0xF8F8F8));
}

// The cursor's bars (opaque Gouraud quads, brightening in): a band and two lines, brightest in the middle.
void MainMenuView::_bars(gd::Int glow) {
    gd::Int g = gd::mini(glow, 0x100);
    gfx::Color c = psx(PsxCanvas::scale_colour(0xC0C0C0, g));
    gfx::Color f = psx(PsxCanvas::scale_colour(0xF8F8F8, g));
    gfx::Color none = gfx::Color::black();
    gradient(8, 0x168, static_cast<float>(CENTRE - 8), 0x1C, none, c, none, c);
    gradient(static_cast<float>(CENTRE), 0x168, static_cast<float>(0x168 - CENTRE), 0x1C, c, none, c, none);
    for (gd::Int y : {0x182, 0x168}) {
        gradient(0, static_cast<float>(y), static_cast<float>(CENTRE), 2, none, f, none, f);
        gradient(static_cast<float>(CENTRE), static_cast<float>(y), static_cast<float>(SCREEN_X - CENTRE), 2, f, none, f, none);
    }
}

}  // namespace t3

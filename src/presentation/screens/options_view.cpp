// remake/presentation/screens/options_view.gd
#include "presentation/screens/options_view.h"

#include "content/fight_tables.h"
#include "content/flow_data.h"
#include "content/json_file.h"
#include "content/text_locale.h"
#include "content/texture_packs.h"
#include "core/fight/fight_state.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/game/options_screen.h"
#include "core/game/ranking_screen.h"
#include "gd/io.h"

namespace t3::embedded {
extern const unsigned char pad_dualsense[];
extern const unsigned long pad_dualsense_size;
}  // namespace t3::embedded

namespace t3 {

namespace {

// GDScript's `format % [args]` for the formats this view uses: %d (with a width and the 0 flag),
// %s and %%.
std::string _format(const std::string& fmt, const std::vector<ScreenCanvas::Arg>& args) {
    std::string out;
    size_t next = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
        char ch = fmt[i];
        if (ch != '%' || i + 1 >= fmt.size()) {
            out += ch;
            continue;
        }
        ++i;
        if (fmt[i] == '%') {
            out += '%';
            continue;
        }
        bool zero = false;
        if (fmt[i] == '0') {
            zero = true;
            ++i;
        }
        gd::Int width = 0;
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
            width = width * 10 + (fmt[i] - '0');
            ++i;
        }
        if (i >= fmt.size() || next >= args.size()) break;
        std::string piece;
        if (fmt[i] == 'd') {
            gd::Int v = std::holds_alternative<gd::Int>(args[next]) ? std::get<gd::Int>(args[next]) : 0;
            piece = gd::str(v < 0 ? -v : v);
            gd::Int pad = width - static_cast<gd::Int>(piece.size()) - (v < 0 ? 1 : 0);
            if (zero) {
                if (pad > 0) piece = std::string(static_cast<size_t>(pad), '0') + piece;
                if (v < 0) piece = "-" + piece;
            } else {
                if (v < 0) piece = "-" + piece;
                if (pad > 0) piece = std::string(static_cast<size_t>(pad), ' ') + piece;
            }
        } else {
            piece = std::holds_alternative<std::string>(args[next]) ? std::get<std::string>(args[next])
                                                                     : gd::str(std::get<gd::Int>(args[next]));
            gd::Int pad = width - static_cast<gd::Int>(piece.size());
            if (pad > 0) piece = std::string(static_cast<size_t>(pad), ' ') + piece;
        }
        ++next;
        out += piece;
    }
    return out;
}

// lerpf(from, to, weight).
float _lerpf(float from, float to, float weight) { return from + (to - from) * weight; }

}  // namespace

void OptionsView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
                        const std::string& screens_dir) {
    setup_psx(hud_data, game_ram, image);
    pictures_dir = screens_dir;
    reload_pictures();
    wide_background = true;   // native: the wide mode stretches the picture's plain edges out
}

// The options backdrop and the pad pictures, from the texture pack in use.
void OptionsView::reload_pictures() {
    picture_texture = TexturePacks::texture(gd::path_join(pictures_dir, "options.png"));
    pads.clear();
    for (gd::Int i = 0; i < 2; ++i) pads.append(TexturePacks::texture(gd::path_join(pictures_dir, "pad_" + gd::str(i) + ".png")));
}

void OptionsView::show_state(const gd::Ref<GameFlow>& game) {
    flow = game;
    options = game->options;
    frame_count = game->fight->vblank;
    queue_redraw();
}

gd::Int OptionsView::_title_colour() { return gd::Int(flow->progress->options_colour) != 0 ? 1 : 6; }

void OptionsView::_draw() {
    begin();
    black();
    image_wide(picture_texture);
    if (options == nullptr || flow->sub != 1) return;
    switch (options->page) {
        case 0:
        case 1:
            _list(options->page);
            break;
        case OptionsScreen::PAGE_RECORDS:
            _records();
            break;
        case OptionsScreen::PAGE_CARD:
            _list(OptionsScreen::PAGE_CARD);
            _card_message();
            break;
        case OptionsScreen::PAGE_KEYS:
            _keys();
            break;
        case OptionsScreen::PAGE_DISPLAY_ADJUST:
            if (options->system_option) _system();
            break;
        default:
            break;
    }
}

// Native: SYSTEM OPTION's pages as the game draws its own: a list of entries as OPTION MODE (page
// 0's places), a list of settings as GAME OPTION (page 1's, from the top as it has no mode boxes).
void OptionsView::_system() {
    bool values = options->system_values;
    const gd::Json& page = flow->data->option_pages[values ? 1 : 0];
    centred(options->system_title, 1, _title_colour(), 0xB8, JsonFile::number(page["y"]));
    gd::Int x = JsonFile::number(page["list_x"]);
    gd::Int y = JsonFile::number(flow->data->option_pages[0]["list_y"]);
    gd::Int spacing = JsonFile::number(page["spacing"]);
    gd::Int width = JsonFile::number(page["value_width"]);
    for (gd::Int i = 0; i < static_cast<gd::Int>(options->system_rows.size()); ++i) {
        const OptionsScreen::SystemRow& row = options->system_rows[static_cast<size_t>(i)];
        gd::Int colour = 10;
        if (i == options->system_cursor) {
            colour = 5;
            _glow(x, y, row.value.empty() ? text_width(row.label, 0) : width);
        }
        text(row.label, 0, colour, x, y);
        if (!row.value.empty()) text(row.value, 0, row.is_default ? 1 : 6, x + width - text_width(row.value, 0), y);
        y += spacing;
    }
}

// FUN_800DC838: the cursor bar behind an option (pulsing).
void OptionsView::_glow(float x, float y, float w, bool pulse) {
    gd::Int c = BAR;
    if (pulse) c = scale_colour(BAR, (triangle(frame_count << 4) >> 1) + 0x80);
    fill(x - 3, y - 3, w + 6, 0x16, psx(c));
}

void OptionsView::_list(gd::Int p) {
    const gd::Json& page = flow->data->option_pages[p];
    std::string title = page["title"].str();
    if (!title.empty()) {
        // A renamed title keeps the original's centre.
        std::string name = options->shown(title);
        float x = JsonFile::number(page["x"]) + (text_width(title, 1) - text_width(name, 1)) / 2.0f;
        text(name, 1, _title_colour(), x, JsonFile::number(page["y"]));
    }
    gd::Int x = JsonFile::number(page["list_x"]);
    gd::Int y = JsonFile::number(page["list_y"]);
    gd::Int spacing = JsonFile::number(page["spacing"]);
    gd::Int width = JsonFile::number(page["value_width"]);
    const gd::Json& items = page["items"];
    gd::Int item_count = items.size();
    for (gd::Int i = 0; i < item_count; ++i) {
        if (options->item_hidden(p, i)) continue;
        const gd::Json& e = items[i];
        std::string label = options->item_label(p, i);
        gd::Int colour = 10;
        gd::Int layout = JsonFile::number(e["layout"]);
        if (i == options->cursors[p]) {
            colour = 5;
            _glow(x, y, layout == 1 ? text_width(label, 0) : width);
        }
        text(label, 0, colour, x, y);
        const gd::Json& values = e["values"];
        if (layout == 2 && values.size() != 0) {
            gd::Int v = options->item_value(p, i);
            std::string shown = values[gd::clampi(v, 0, values.size() - 1)].str();
            bool default_value = JsonFile::number(e["kind"]) != 0 && JsonFile::number(e["value"]) == v;
            text(shown, 0, default_value ? 1 : 6, x + width - text_width(shown, 0), y);
        }
        y += spacing;
    }
    if (p == 1) _mode_boxes(items[gd::Int(options->cursors[1])]);
}

// FUN_800DC914: the pictures of the modes the item applies to (greyed without the mode's bit).
void OptionsView::_mode_boxes(const gd::Json& item) {
    gd::Int mask = JsonFile::number(item["modes"]);
    gd::Int n = options->modes.size();
    fill(0xF6, 0x28, 0x62, n * 0xE + 4, psx(0xC0C0C0));
    fill(0xF7, 0x2A, 0x60, n * 0xE, gfx::Color::black());
    for (gd::Int i = 0; i < n; ++i) {
        gd::Int m = options->modes[i];
        gd::Int uv = 0;
        gd::Int mode_count = flow->data->option_modes.size();
        for (gd::Int k = 0; k < mode_count; ++k) {
            if (ram->u32(MODE_ICONS + 8 * k) == m) uv = ram->u32(MODE_ICONS + 8 * k + 4);
        }
        if ((gd::shr(mask, m & 31) & 1) == 0) uv = (uv & 0xFFFF) | (MODE_ICON_GREY << 16);
        sprt(0xF7, 0x2A + 0xE * i, 0x60, 0xE, uv, 0xB);
    }
}

void OptionsView::_records() {
    text("RECORDS", 1, 6, 0x8B, 0x28);
    gd::Int sub = options->record_page;
    centred(flow->data->record_titles[sub], 0, 6, 0xB8, 0x5A);
    text("PAGE:" + gd::str(sub + 1), 0, 6, 0xE, 2);
    gd::PI32 ids = options->record_lists[sub];
    gd::Int top = options->record_tops[sub];
    gd::Ref<GameProgress> progress = flow->progress;
    gd::Int rows = gd::mini(ids.size() - top, 10);
    for (gd::Int i = 0; i < rows; ++i) {
        gd::Int row = top + i;
        gd::Int y = RECORD_ROW_Y + RECORD_ROW * i;
        gd::Int id = ids[row];
        std::string ordinal = ORDINALS[row < 4 ? row % 10 : 3];
        text(_format(_rank_format(), {gd::mini(row + 1, 99), ordinal}), 0, 6, 0x12, y);
        switch (sub) {
            case 0: {
                _name(id, y);
                gd::Int t = gd::mini(progress->time_record(id), 359'999);
                gd::Int sec = t / 60;
                text(_format("%02d'%02d\"%02d", {sec / 60, sec % 60, (t % 60) * 100 / 60}), 0, 6, 0xC6, y);
                text(progress->time_record_name(id), 0, 6, 0x132, y);
                break;
            }
            case 1: {
                gd::Vector2i s = progress->survivor(id);
                _name(gd::Int(s.x) >> 2, y);
                text(_format("%3d %s", {gd::Int(s.y), s.y == 1 ? std::string("WIN") : std::string("WINS")}), 0, 6, 0xC6, y);
                text(progress->survivor_name(id), 0, 6, 0x132, y);
                break;
            }
            case 2: {
                _name(id, y);
                gd::Int share = RankingScreen::permille(progress->usage(id), options->record_total);
                text(_format("%d.%d%%", {share / 10, share % 10}), 0, share >= 1000 ? 1 : 6, 0xE1, y);
                text(_format("%d", {progress->usage(id)}), 0, 6, 0x120, y);
                break;
            }
            case 3: {
                _name(id, y);
                gd::Int w = progress->stat(id, 1);
                gd::Int l = progress->stat(id, 2);
                gd::Int rate = RankingScreen::permille(w, w + l);
                text(_format("%d.%d%%", {rate / 10, rate % 10}), 0, 6, 0xAB, y);
                text(_format("%d", {w}), 0, 6, 0xEA, y);
                text(_format("%d", {l}), 0, 6, 0x120, y);
                break;
            }
            default:
                break;
        }
    }
}

// The records' rank: the USA release pads it to two digits.
std::string OptionsView::_rank_format() {
    return ram->locale != nullptr && ram->locale->english ? ram->locale->record_rank : std::string("%d%s");
}

void OptionsView::_name(gd::Int character, float y) {
    gd::Json record = flow->fight->tables->character(character * 4);
    centred(record["name"].str(), 0, 6, 0x7E, y);
}

void OptionsView::_card_message() {
    gd::Int sub = options->subs[OptionsScreen::PAGE_CARD];
    if (sub == 0) return;
    gd::Int state = options->card_state;
    if (state == 0) {
        // Native: the game's colour 4 of font 0 reads the CLUT at (320, 504), in player 1's fighter
        // CLUTs (whatever fighter was loaded last); the converted system textures leave it empty, so
        // the prompt drew nothing. Font 0's colour 6 (NOW LOADING's) instead.
        text("PUSH START BUTTON", 0, PROMPT_COLOUR, 0x15, 0x168);
        return;
    }
    const gd::Dict<gd::Int, CardMessage>& table = sub == 2 ? LOAD_MESSAGES : CARD_MESSAGES;
    CardMessage e = table.get(state, CardMessage{"SOMETHING MESSAGE", 3});
    gd::Int colour = e.colour;
    centred(options->shown(e.text), 0, colour, 0xB8, 0x168);
}

// FUN_800DF630: both players' pad pictures, button actions, VIBRATION, DEFAULT and EXIT.
void OptionsView::_keys() {
    text("KEY CONFIGURATION", 1, _title_colour(), 0x4A, 0x28);
    for (const gd::Ref<OptionsScreen::KeyRecord>& rec : options->keys) {
        gd::Int x = rec->x + 0x14;
        gd::Int y = rec->y;
        if (options->system_option) {
            // Native: a DualSense in the PS1 pad's place (the pads a phone takes), drawn at 4x.
            image(_dualsense(), x, y + 0x88, PAD_SIZE.x, PAD_SIZE.y);
        } else {
            image(pads[0], x, y + 0x88);
        }
        for (gd::Int i = 0; i < 8; ++i) {
            gd::Ref<OptionsScreen::KeyRow> row = rec->rows[i];
            gd::Int action = flow->progress->button_action(rec->player, row->button);
            float open = row->open / 8.0f;
            gfx::Vec2 at(rec->x + _lerpf(row->layout[0], row->layout[2], open), rec->y + _lerpf(row->layout[1], row->layout[3], open));
            bool selected = rec->selected == 0 && row->state != 0;
            fill(at.x, at.y, 0x40, 0x16, psx(row->colour));
            fill(at.x + 1, at.y + 2, 0x3E, 0x12, gfx::Color::black());
            text(flow->data->key_names[action], 0, selected ? 5 : 6, at.x + 4, at.y + 3);
        }
        _item("SETTING", rec->selected == 0, x, y + 0x154);
        gd::Int vib = flow->progress->u8(0x1C + rec->player);
        _item("VIBRATION    " + std::string(vib != 0 ? "YES" : " NO"), rec->selected == 1, x, y + 0x16E);
        _item("DEFAULT", rec->selected == 2, x, y + 0x188);
        _item("EXIT", rec->selected == 3, x, y + 0x1A2);
    }
}

const gfx::TextureRef& OptionsView::_dualsense() {
    if (dualsense == nullptr) {
        gd::PBytes png;
        png.resize(static_cast<gd::Int>(embedded::pad_dualsense_size));
        for (unsigned long i = 0; i < embedded::pad_dualsense_size; ++i) png.set(static_cast<gd::Int>(i), embedded::pad_dualsense[i]);
        dualsense = std::make_shared<gfx::Texture>(gfx::decode_png(png), false, true);
    }
    return dualsense;
}

void OptionsView::_item(const std::string& label, bool selected, float x, float y) {
    if (selected) _glow(x, y, text_width(label, 0));
    text(label, 0, selected ? 5 : 10, x, y);
}

}  // namespace t3

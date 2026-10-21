// remake/content/flow_data.gd
#include "content/flow_data.h"

#include "content/json_file.h"
#include "content/text_locale.h"
#include "platform/log.h"

namespace t3 {

namespace {
// `str(v)` of a JSON value the converter writes as a string.
std::string jstr(const gd::Json& v) { return v.is_string() ? v.str() : std::string(); }
// PackedStringArray(Array).
gd::Arr<std::string> strings(const gd::Json& value) {
    gd::Arr<std::string> out;
    for (const gd::Json& v : value.items()) out.append(jstr(v));
    return out;
}
// `Array[Dictionary].assign(Array)`.
gd::Arr<gd::Json> objects(const gd::Json& value) {
    gd::Arr<gd::Json> out;
    for (const gd::Json& v : value.items()) out.append(v);
    return out;
}
}  // namespace

gd::Ref<FlowData> FlowData::load_from(const std::string& path) {
    gd::Json d = JsonFile::read(path);
    auto f = gd::make<FlowData>();
    f->progress = _bytes(d["progress"]);
    gd::PBytes tables = _bytes(d["key_tables"]);
    for (gd::Int p = 0; p < 2; ++p) {
        gd::PI32 words;
        for (gd::Int bit = 0; bit < 16; ++bit) {
            words.append(tables.decode_u16(0x20 * p + 2 * bit));
        }
        f->key_tables.append(words);
    }
    f->quick_grid = _bytes(d["quick_grid"]);
    f->quick_prefs = _bytes(d["quick_prefs"]);
    f->quick_state_anim = _bytes(d["quick_state_anim"]);
    f->quick_decided = _bytes(d["quick_decided"]);
    for (const gd::Json& e : d["menu_entries"].items()) {
        auto m = gd::make<MenuEntry>();
        m->text = jstr(e["text"]);
        m->mode = JsonFile::number(e["mode"]);
        m->two_players = JsonFile::number(e["two_players"]);
        m->index = JsonFile::number(e["index"]);
        f->menu_entries.append(m);
    }
    f->button_actions = _u16s(d["button_actions"]);
    f->button_tail = _u16s(d["button_tail"]);
    f->unlock_schedule = JsonFile::ints(d["unlock_schedule"]);
    f->demo_characters = JsonFile::ints(d["demo_characters"]);
    // PackedInt64Array(Array): each float converted to int.
    for (const gd::Json& v : d["attributes"].items()) f->attributes.append(v.to_int());
    f->vs_backgrounds = _u16s(d["vs_backgrounds"]);
    gd::PBytes colours = _bytes(d["vs_caption_colours"]);
    for (gd::Int i = 0; i < colours.size() / 4; ++i) {
        f->vs_caption_colours.append(colours.decode_u32(4 * i));
    }
    f->vs_caption_widths = _u16s(d["vs_caption_widths"]);
    f->vs_player_pos = JsonFile::ints(d["vs_player_pos"]);
    f->vs_player_names = JsonFile::ints(d["vs_player_names"]);
    f->vs_team_pos = JsonFile::ints(d["vs_team_pos"]);
    f->vs_team_names = JsonFile::ints(d["vs_team_names"]);
    f->vs_team_icons = JsonFile::ints(d["vs_team_icons"]);
    f->ranking_alphabet = jstr(d["ranking_alphabet"]);
    f->ranking_rejected = strings(d["ranking_rejected"]);
    f->ranking_skipped = JsonFile::ints(d["ranking_skipped"]);
    f->ranking_gon = jstr(d["ranking_gon"]);
    f->ending_movies = JsonFile::ints(d["ending_movies"]);
    f->staff_titles = strings(d["staff_titles"]);
    f->staff_rows = strings(d["staff_rows"]);
    f->staff_font_wide = JsonFile::ints(d["staff_font_wide"]);
    f->staff_font_narrow = JsonFile::ints(d["staff_font_narrow"]);
    f->theater_movies = objects(d["theater_movies"]);
    f->theater_tracks = objects(d["theater_tracks"]);
    f->theater_names = d["theater_names"];
    f->theater_texts = d["theater_texts"];
    for (const gd::Json& script : d["ogre_scripts"].items()) {
        gd::Arr<gd::PI32> events;
        for (const gd::Json& e : script.items()) {
            events.append(JsonFile::ints(e));
        }
        f->ogre_scripts.append(events);
    }
    f->vs_practice_pos = JsonFile::ints(d["vs_practice_pos"]);
    f->vs_practice_names = JsonFile::ints(d["vs_practice_names"]);
    f->vs_practice_texts = d["vs_practice_texts"];
    for (const gd::Json& row : d["practice_rows"].items()) {
        f->practice_rows.append(JsonFile::ints(row));
    }
    for (const gd::Json& kind : d["practice_combos"].items()) {
        gd::Arr<gd::Dict<std::string, gd::PI32>> list;
        for (const gd::Json& c : kind.items()) {
            gd::Dict<std::string, gd::PI32> combo;
            combo.set("steps", JsonFile::ints(c["steps"]));
            combo.set("moves", JsonFile::ints(c["moves"]));
            list.append(combo);
        }
        f->practice_combos.append(list);
    }
    f->practice_texts = d["practice_texts"];
    f->option_pages = objects(d["option_pages"]);
    f->option_modes = JsonFile::ints(d["option_modes"]);
    f->key_rows = objects(d["key_rows"]);
    f->key_default = JsonFile::ints(d["key_default"]);
    f->key_names = strings(d["key_names"]);
    f->record_titles = strings(d["record_titles"]);
    f->select_grid_keys = JsonFile::ints(d["select_grid_keys"]);
    f->select_player_layout = _bytes(d["select_player_layout"]);
    f->select_state_anim = JsonFile::ints(d["select_state_anim"]);
    f->select_decided = JsonFile::ints(d["select_decided"]);
    f->select_layouts = _bytes(d["select_layouts"]);
    f->select_context = _bytes(d["select_context"]);
    const gd::Json& texts = d["vs_texts"];
    f->vs_text = jstr(texts["vs"]);
    f->vs_stage = jstr(texts["stage"]);
    f->vs_round = jstr(texts["round"]);
    f->vs_demo = jstr(texts["demo"]);
    for (const auto& [field, block] : TEXT_FIELDS) {
        f->_texts.set(field, f->_get_text(field));
    }
    f->_texts.set("staff_titles", f->staff_titles);
    f->_texts.set("staff_rows", f->staff_rows);
    return f;
}

void FlowData::localize(const gd::Ref<TextLocale>& locale) {
    for (const auto& [field, block] : TEXT_FIELDS) {
        TextValue v = _texts.at(field);
        if (locale != nullptr) {
            const std::string& b = block;
            v = std::visit([&](const auto& value) -> TextValue { return locale->texts(b, value); }, v);
        }
        _set_text(field, v);
    }
    bool english = locale != nullptr && locale->english && !locale->staff_rows.is_empty();
    staff_titles = english ? locale->staff_titles : std::get<gd::Arr<std::string>>(_texts.at("staff_titles"));
    staff_rows = english ? locale->staff_rows : std::get<gd::Arr<std::string>>(_texts.at("staff_rows"));
}

gd::Int FlowData::attribute(gd::Int character, gd::Int costume) const {
    if (character > 0x15 || costume > 3) {
        character = 0x16;
        costume = 0;
    }
    return attributes[character * 4 + costume];
}

gd::PBytes FlowData::_bytes(const gd::Json& value) {
    gd::PBytes out;
    for (gd::Int v : JsonFile::ints(value)) {
        out.append(v);
    }
    return out;
}

gd::PI32 FlowData::_u16s(const gd::Json& value) {
    gd::PBytes b = _bytes(value);
    gd::PI32 out;
    for (gd::Int i = 0; i < b.size() / 2; ++i) {
        out.append(b.decode_u16(2 * i));
    }
    return out;
}

FlowData::TextValue FlowData::_get_text(const std::string& field) const {
    if (field == "theater_names") return theater_names;
    if (field == "theater_texts") return theater_texts;
    if (field == "vs_practice_texts") return vs_practice_texts;
    if (field == "practice_texts") return practice_texts;
    if (field == "key_names") return key_names;
    if (field == "record_titles") return record_titles;
    if (field == "staff_titles") return staff_titles;
    if (field == "staff_rows") return staff_rows;
    Log::error("FlowData: no text field " + field);
    return gd::Json();
}

void FlowData::_set_text(const std::string& field, const TextValue& v) {
    if (field == "theater_names") theater_names = std::get<gd::Json>(v);
    else if (field == "theater_texts") theater_texts = std::get<gd::Json>(v);
    else if (field == "vs_practice_texts") vs_practice_texts = std::get<gd::Json>(v);
    else if (field == "practice_texts") practice_texts = std::get<gd::Json>(v);
    else if (field == "key_names") key_names = std::get<gd::Arr<std::string>>(v);
    else if (field == "record_titles") record_titles = std::get<gd::Arr<std::string>>(v);
    else if (field == "staff_titles") staff_titles = std::get<gd::Arr<std::string>>(v);
    else if (field == "staff_rows") staff_rows = std::get<gd::Arr<std::string>>(v);
    else Log::error("FlowData: no text field " + field);
}

}  // namespace t3

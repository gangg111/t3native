// remake/core/game/game_progress.gd
#include "core/game/game_progress.h"

namespace t3 {

namespace {
// GDScript `char(c)`: the Unicode code point c as a one-character string (UTF-8 here).
std::string char_of(gd::Int c) {
    std::string s;
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
    return s;
}
}  // namespace

gd::Ref<GameProgress> GameProgress::defaults(const gd::PBytes& image) {
    auto p = gd::make<GameProgress>(SIZE);
    p->bytes = image.duplicate();
    p->bytes.resize(SIZE);
    return p;
}

GameProgress::GameProgress(gd::Int size) : ByteBlock(size) {}

gd::Int GameProgress::controller(gd::Int player) const {
    return u8(0x1C + player);
}

gd::Int GameProgress::last_character(gd::Int player) const {
    return u8(0x1E + player);
}

void GameProgress::set_last_character(gd::Int player, gd::Int key) const {
    put8(0x1E + player, key);
}

gd::Int GameProgress::button_action(gd::Int player, gd::Int button) const {
    return u8(0x38 + 8 * player + button);
}

bool GameProgress::button_assigned(gd::Int player, gd::Int button) const {
    return button_action(player, button) != KEY_NO_USE;
}

void GameProgress::set_button_action(gd::Int player, gd::Int button, gd::Int action) const {
    put8(0x38 + 8 * player + button, action);
}

// ---- records

gd::Int GameProgress::time_record(gd::Int character) const {
    return u32(TIME_RECORDS + 8 * character);
}

void GameProgress::set_time_record(gd::Int character, gd::Int frames) const {
    put32(TIME_RECORDS + 8 * character, frames);
}

std::string GameProgress::time_record_name(gd::Int character) const {
    return _name(TIME_RECORDS + 8 * character + 4);
}

gd::Vector2i GameProgress::survivor(gd::Int rank) const {
    gd::Int character = u16(SURVIVORS + 8 * rank);
    gd::Int wins = u16(SURVIVORS + 8 * rank + 2);
    return gd::Vector2i(character, wins);
}

std::string GameProgress::survivor_name(gd::Int rank) const {
    return _name(SURVIVORS + 8 * rank + 4);
}

gd::Int GameProgress::stat(gd::Int character, gd::Int which) const {
    return u16(STATS + 8 * character + 2 * which);
}

void GameProgress::set_stat(gd::Int character, gd::Int which, gd::Int v) const {
    put16(STATS + 8 * character + 2 * which, v);
}

gd::Int GameProgress::usage(gd::Int character) const {
    return stat(character, 0) + stat(character, 2) + stat(character, 3);
}

void GameProgress::put_name(gd::Int at, const std::string& text) const {
    // to_ascii_buffer: the names are ASCII, one byte per character.
    gd::PBytes raw = gd::PBytes::from(text.begin(), text.end());
    for (gd::Int k = 0; k < raw.size(); ++k) put8(at + k, raw[k]);
    put8(at + raw.size(), 0);
}

std::string GameProgress::_name(gd::Int at) const {
    std::string s = "";
    for (gd::Int k = 0; k < 4; ++k) {
        gd::Int c = u8(at + k);
        if (c == 0) break;
        s += char_of(c);
    }
    return s;
}

gd::PBytes GameProgress::save_data() const {
    gd::PBytes out = bytes.slice(0, SAVE_BLOCK);
    out.append_array(bytes.slice(RECORDS, RECORDS + RECORDS_SIZE));
    return out;
}

gd::PBytes GameProgress::with_unlock_state(const gd::PBytes& data, const gd::PBytes& kept) {
    gd::PBytes out = data.duplicate();
    for (const gd::Vector2i& part : UNLOCK_STATE) {
        for (gd::Int i = part.x; i < gd::Int(part.x) + part.y; ++i) out[i] = kept[i];
    }
    return out;
}

void GameProgress::load_save_data(const gd::PBytes& data) const {
    for (gd::Int i = 0; i < SAVE_BLOCK; ++i) bytes[i] = data[i];
    for (gd::Int i = 0; i < RECORDS_SIZE; ++i) bytes[RECORDS + i] = data[SAVE_BLOCK + i];
}

}  // namespace t3

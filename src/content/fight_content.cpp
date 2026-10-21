// remake/content/fight_content.gd
#include "content/fight_content.h"

#include <cstdio>

#include "content/character_model.h"
#include "content/fight_tables.h"
#include "content/game_ram.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "content/stage_data.h"
#include "content/text_locale.h"
#include "gd/io.h"

namespace t3 {

namespace {
// `"%02d" % v`.
std::string pad2(gd::Int v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02lld", static_cast<long long>(v));
    return buf;
}
// `STAGE_LETTERS[i]`: a one-character string (negative from the end, like String indexing).
std::string letter_at(const std::string& s, gd::Int i) {
    return std::string(1, s[gd::index(i, s.size())]);
}
}  // namespace

gd::Ref<FightContent> FightContent::load_from(const std::string& folder) {
    auto c = gd::make<FightContent>();
    c->root = folder;
    c->tables = FightTables::load_from(gd::path_join(folder, "tables"));
    c->common = MotionBank::load_named(gd::path_join(folder, MOTION), COMMON_BANK);
    c->common_rows = c->common->link(c->common, c->tables->builtin_attack_ids);
    return c;
}

gd::Ref<MotionBank> FightContent::bank(gd::Int bank_type) {
    if (!_banks.has(bank_type)) {
        gd::Ref<MotionBank> b = MotionBank::load_named(gd::path_join(root, MOTION), "divmot" + pad2(bank_type));
        _banks.set(bank_type, b);
        _rows.set(bank_type, b->link(common, tables->builtin_attack_ids));
    }
    return _banks.at(bank_type);
}

gd::Arr<gd::Ref<MoveRow>> FightContent::rows(gd::Int bank_type) {
    bank(bank_type);
    return _rows.at(bank_type);
}

gd::Ref<CharacterModel> FightContent::model(gd::Int costume_slot) {
    if (!_models.has(costume_slot)) {
        _models.set(costume_slot, CharacterModel::load_from(gd::path_join(root, "characters/costume_" + pad2(costume_slot))));
    }
    return _models.at(costume_slot);
}

gd::Ref<StageData> FightContent::stage(const std::string& letter) {
    if (!_stages.has(letter)) {
        _stages.set(letter, StageData::load_from(gd::path_join(root, "stages/" + letter)));
    }
    return _stages.at(letter);
}

gd::Ref<StageData> FightContent::stage_number(gd::Int number) {
    gd::Int last = static_cast<gd::Int>(STAGE_LETTERS.size()) - 1;
    return stage(letter_at(STAGE_LETTERS, gd::clampi(number, 0, last)));
}

gd::Ref<GameRam> FightContent::mode_ram(const std::string& overlay) {
    if (!_mode_rams.has(overlay)) {
        if (_screens_ram == nullptr) {
            _screens_ram = GameRam::load_from(gd::path_join(root, "screens"));
        }
        gd::Ref<GameRam> ram = _screens_ram->fresh();
        ram->locale = locale;
        ram->load_overlay(overlay);
        _mode_rams.set(overlay, ram);
    }
    return _mode_rams.at(overlay);
}

gd::PBytes FightContent::level_script(gd::Int stage) const {
    std::string path = stage < static_cast<gd::Int>(STAGE_LETTERS.size())
                           ? gd::path_join(root, "stages/" + letter_at(STAGE_LETTERS, stage) + "/level.bin")
                           : "";
    return path != "" && gd::file_exists(path) ? gd::read_file(path) : gd::PBytes();
}

gd::PBytes FightContent::move_text(gd::Int costume_slot) {
    std::string path = _move_text_path(costume_slot);
    if (!_move_texts.has(path)) {
        _move_texts.set(path, gd::file_exists(path) ? gd::read_file(path) : gd::PBytes());
    }
    return _move_texts.at(path);
}

bool FightContent::move_text_english(gd::Int costume_slot) {
    std::string path = _move_text_path(costume_slot);
    return path == _usa_path(costume_slot);
}

std::string FightContent::_usa_path(gd::Int costume_slot) const {
    return gd::path_join(gd::path_join(root, TextLocale::USA_DIR), "move_lists/costume_" + pad2(costume_slot) + ".bin");
}

std::string FightContent::_move_text_path(gd::Int costume_slot) {
    bool english = locale != nullptr && locale->english;
    if (locale != _move_paths_locale || english != _move_paths_english) {
        _move_paths.clear();
        _move_paths_locale = locale;
        _move_paths_english = english;
    }
    if (!_move_paths.has(costume_slot)) {
        _move_paths.set(costume_slot, _find_move_text(costume_slot));
    }
    return _move_paths.at(costume_slot);
}

std::string FightContent::_find_move_text(gd::Int costume_slot) const {
    std::string japanese = gd::path_join(root, "characters/costume_" + pad2(costume_slot) + "/move_list.bin");
    std::string english = _usa_path(costume_slot);
    bool usa_on = locale != nullptr && locale->english && !locale->usa_dir.empty();
    if ((usa_on || !gd::file_exists(japanese)) && gd::file_exists(english)) {
        return english;
    }
    return japanese;
}

}  // namespace t3

// remake/app/progress_card.gd
#include "app/progress_card.h"

#include "core/game/game_progress.h"
#include "core/game/progress_rules.h"
#include "platform/save_store.h"
#include "platform/log.h"

namespace t3 {

ProgressCard::ProgressCard() : store(gd::make<SaveStore>()) {}

std::string ProgressCard::path_for(gd::Int number) {
    return SaveStore::PROGRESS_PATHS[gd::clampi(number, 1, CARDS) - 1];
}

void ProgressCard::select(gd::Int number) {
    card = gd::clampi(number, 1, CARDS);
    store->progress_path = path_for(card);
    _saved = gd::PBytes();
}

void ProgressCard::fresh_progress(const gd::Ref<GameProgress>& progress) {
    progress->load_save_data(defaults);
    if (auto_save_default) {
        progress->auto_save = 1;
    }
    if (unlock_all) {
        ProgressRules::open_all(progress);
    }
}

gd::PBytes ProgressCard::read() {
    _saved = reads ? store->load_progress() : gd::PBytes();
    if (reads) Log::info("card: read " + store->progress_path + ": " + gd::str(_saved.size()) + " bytes (" + SaveStore::user_dir + ")");
    return unlock_all && !_saved.is_empty() ? _opened(_saved) : _saved;
}

gd::Int ProgressCard::write(gd::PBytes bytes) {
    if (!writes) {
        return SaveStore::CARD_DONE;
    }
    if (unlock_all) {
        bytes = GameProgress::with_unlock_state(bytes, !_saved.is_empty() ? _saved : defaults);
    }
    gd::Int result = store->save_progress(bytes);
    Log::info("card: wrote " + store->progress_path + (result == SaveStore::CARD_DONE ? "" : " FAILED (" + gd::str(result) + ")"));
    if (result == SaveStore::CARD_DONE) {
        _saved = bytes;
    }
    return result;
}

bool ProgressCard::saves_options(const gd::Ref<GameProgress>& progress) {
    return progress->auto_save != 0 || (!_saved.is_empty() && _saved[GameProgress::AUTO_SAVE] != 0);
}

void ProgressCard::load_into(const gd::Ref<GameProgress>& progress) {
    fresh_progress(progress);
    gd::PBytes saved = read();
    if (!saved.is_empty()) {
        progress->load_save_data(saved);
    }
}

gd::PBytes ProgressCard::_opened(const gd::PBytes& saved) {
    gd::Ref<GameProgress> progress = gd::make<GameProgress>();
    progress->load_save_data(saved);
    ProgressRules::open_all(progress);
    return progress->save_data();
}

}  // namespace t3

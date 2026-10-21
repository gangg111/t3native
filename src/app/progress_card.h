#pragma once
// remake/app/progress_card.gd
// The memory card the game uses (remake-plan.md#save-data-and-settings): one of two save files,
// as the PlayStation's two card slots (the setting `memory_card`), and the remake's saving rules
// on top of the game's own: AUTO SAVE starts on for a player with no save, and leaving OPTIONS
// saves the options when auto save is on (or was on in the file, so that turning it off lasts).
// The game itself saves only after statistics or unlock updates (modes.md#memory-card-file).
//
// A trace replay's card is fresh and never written; `--fresh` starts from a fresh card too (with
// the game's defaults, auto save off) and neither reads nor writes the save file. `--unlock` opens every
// character, costume and mode in memory only: what the game reads has them open, what it writes
// keeps the unlock state the card had (its save, or the defaults).
#include <string>

#include "gd/gd.h"

namespace t3 {

class GameProgress;
class SaveStore;

class ProgressCard {
public:
    static constexpr gd::Int CARDS = 2;

    ProgressCard();

    gd::Ref<SaveStore> store;
    gd::Int card = 1;
    bool reads = true;                    // false: every card is fresh (`--fresh`, a replay)
    bool writes = true;                   // false: saves succeed without writing (`--fresh`, a replay)
    bool auto_save_default = true;        // AUTO SAVE on for a fresh card
    bool unlock_all = false;              // `--unlock`: everything open for this session, never saved
    gd::PBytes defaults;                  // the save data at power-on: a fresh card starts from it
    gd::PBytes _saved;                    // what the card holds now (empty: no file)

    static std::string path_for(gd::Int number);
    // Uses card `number` (1 or 2) from now on.
    void select(gd::Int number);
    // The progress a fresh card starts from: the game's defaults, with AUTO SAVE on outside the tests.
    void fresh_progress(const gd::Ref<GameProgress>& progress);
    // The card's save for the game's start-up read (GameFlow.card_loader); empty when there is none.
    gd::PBytes read();
    // Writes the progress (OptionsScreen.save_writer, auto save); returns a memory card result.
    gd::Int write(gd::PBytes bytes);
    // Whether leaving OPTIONS saves: auto save is on, or the card still has it on.
    bool saves_options(const gd::Ref<GameProgress>& progress);
    // Changes to the current card's contents in place: a fresh progress, then its save if it has one.
    void load_into(const gd::Ref<GameProgress>& progress);
    // The card's save as the game reads it with `--unlock`: everything open.
    gd::PBytes _opened(const gd::PBytes& saved);
};

}  // namespace t3

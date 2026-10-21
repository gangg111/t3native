// remake/tests/app/test_progress_card.gd: the memory cards: two save files, AUTO SAVE on for a fresh
// card, the options saved on leaving OPTIONS while auto save is (or was) on, and a replay's card
// never written.
#include <filesystem>
#include <string>

#include "app/progress_card.h"
#include "core/game/game_progress.h"
#include "core/game/progress_rules.h"
#include "gd/io.h"
#include "platform/save_store.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

// Native: `user://` is a scratch folder of the tests.
void use_test_user_dir() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "t3tests_user";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    SaveStore::user_dir = dir.string();
}

// One file pair per test: the runners may run the tests at the same time.
std::string PATH(const std::string& test, gd::Int n) { return "user://test_card_" + test + "_" + gd::str(n) + ".json"; }

gd::Ref<ProgressCard> _card(const std::string& test) {
    use_test_user_dir();
    gd::Ref<ProgressCard> c = gd::make<ProgressCard>();
    gd::Ref<GameProgress> p = gd::make<GameProgress>(GameProgress::SIZE);
    p->difficulty = 2;
    c->defaults = p->save_data();
    c->select(1);
    c->store->progress_path = PATH(test, 1);
    return c;
}

void _remove(const std::string& test) {
    for (gd::Int n : {1, 2}) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(SaveStore::globalize_path(PATH(test, n))), ec);
    }
}

gd::Ref<GameProgress> progress() { return gd::make<GameProgress>(GameProgress::SIZE); }

// A card whose defaults are the game's: the ten first characters and Law's Start costume.
gd::Ref<ProgressCard> _locked_card(const std::string& test) {
    gd::Ref<ProgressCard> c = _card(test);
    gd::Ref<GameProgress> p = progress();
    p->unlocked = 0x3FF;
    p->start_costumes = 0x2;
    c->defaults = p->save_data();
    return c;
}

}  // namespace

T3_TEST(progress_card, cards_have_their_own_files) {
    t.expect_equal(ProgressCard::path_for(1), std::string("user://progress.json"));
    t.expect_equal(ProgressCard::path_for(2), std::string("user://progress2.json"));
    t.expect_equal(ProgressCard::path_for(7), std::string("user://progress2.json"));
}

T3_TEST(progress_card, fresh_card_starts_with_auto_save) {
    gd::Ref<ProgressCard> c = _card("fresh");
    _remove("fresh");
    gd::Ref<GameProgress> p = progress();
    p->difficulty = 4;
    c->load_into(p);
    t.expect_equal(gd::Int(p->difficulty), gd::Int(2), "defaults");
    t.expect_equal(gd::Int(p->auto_save), gd::Int(1), "auto save on");
    c->auto_save_default = false;
    c->load_into(p);
    t.expect_equal(gd::Int(p->auto_save), gd::Int(0), "the game's default with --fresh");
}

T3_TEST(progress_card, saved_card_loads_its_progress) {
    gd::Ref<ProgressCard> c = _card("load");
    gd::Ref<GameProgress> p = progress();
    c->fresh_progress(p);
    p->auto_save = 0;
    p->difficulty = 3;
    t.expect_equal(c->write(p->save_data()), SaveStore::CARD_DONE);
    gd::Ref<GameProgress> q = progress();
    c->load_into(q);
    t.expect_equal(gd::Int(q->difficulty), gd::Int(3));
    t.expect_equal(gd::Int(q->auto_save), gd::Int(0), "turned off stays off");
    c->reads = false;
    c->load_into(q);
    t.expect_equal(gd::Int(q->difficulty), gd::Int(2), "--fresh ignores the save");
    _remove("load");
}

T3_TEST(progress_card, options_saved_while_auto_save_is_or_was_on) {
    gd::Ref<ProgressCard> c = _card("options");
    _remove("options");
    gd::Ref<GameProgress> p = progress();
    c->load_into(p);
    t.expect(c->saves_options(p), "on");
    c->write(p->save_data());
    p->auto_save = 0;
    t.expect(c->saves_options(p), "just turned off: the card still has it on");
    c->write(p->save_data());
    t.expect(!c->saves_options(p), "off in the card too");
    _remove("options");
}

T3_TEST(progress_card, replay_card_is_never_written) {
    gd::Ref<ProgressCard> c = _card("replay");
    _remove("replay");
    c->writes = false;
    gd::Ref<GameProgress> p = progress();
    t.expect_equal(c->write(p->save_data()), SaveStore::CARD_DONE);
    t.expect(!gd::file_exists(SaveStore::globalize_path(PATH("replay", 1))));
}

T3_TEST(progress_card, unlock_opens_everything_in_memory) {
    gd::Ref<ProgressCard> c = _locked_card("unlock_open");
    _remove("unlock_open");
    c->unlock_all = true;
    gd::Ref<GameProgress> p = progress();
    c->load_into(p);
    t.expect_equal(gd::Int(p->unlocked), ProgressRules::ALL_CHARACTERS, "characters");
    t.expect_equal(gd::Int(p->start_costumes), 0x2 | ProgressRules::START_COSTUME_MASK, "Start costumes");
    t.expect(p->ball_new != 0 && p->theater_new != 0, "Tekken Ball and Theater");
    t.expect_equal(gd::Int(p->unlock_class), gd::Int(2), "every character");
    t.expect_equal(gd::Int(p->cleared), ProgressRules::ALL_CHARACTERS, "the clears that open Theater's movies");
    t.expect_equal(gd::Int(p->cleared2), ProgressRules::SECOND_CLEARS, "the second clears");
    c->unlock_all = false;
    c->load_into(p);
    t.expect_equal(gd::Int(p->unlocked), gd::Int(0x3FF), "without --unlock the defaults stay");
    t.expect_equal(gd::Int(p->ball_new), gd::Int(0));
    t.expect_equal(gd::Int(p->cleared), gd::Int(0));
}

T3_TEST(progress_card, unlock_is_read_over_a_saved_card) {
    gd::Ref<ProgressCard> c = _locked_card("unlock_read");
    _remove("unlock_read");
    gd::Ref<GameProgress> p = progress();
    c->fresh_progress(p);
    p->unlocked = 0x7FF;
    c->write(p->save_data());
    c->unlock_all = true;
    c->load_into(p);
    t.expect_equal(gd::Int(p->unlocked), ProgressRules::ALL_CHARACTERS);
    t.expect_equal(c->read().size(), GameProgress::SAVE_BLOCK + GameProgress::RECORDS_SIZE);
    _remove("unlock_read");
}

T3_TEST(progress_card, unlock_is_never_written) {
    gd::Ref<ProgressCard> c = _locked_card("unlock_save");
    _remove("unlock_save");
    c->unlock_all = true;
    gd::Ref<GameProgress> p = progress();
    c->load_into(p);
    p->difficulty = 3;
    p->set_stat(0, 0, 7);
    p->fights_since_unlock = 40;
    p->play_steps = 3;
    p->cleared2 = gd::Int(p->cleared2) | 0x2000;
    t.expect_equal(c->write(p->save_data()), SaveStore::CARD_DONE);
    gd::Ref<GameProgress> q = progress();
    gd::Ref<ProgressCard> plain = _locked_card("unlock_save");
    plain->load_into(q);
    t.expect_equal(gd::Int(q->difficulty), gd::Int(3), "the rest of the progress is saved");
    t.expect_equal(q->stat(0, 0), gd::Int(7), "the statistics too");
    t.expect_equal(gd::Int(q->unlocked), gd::Int(0x3FF), "characters");
    t.expect_equal(gd::Int(q->start_costumes), gd::Int(0x2), "Start costumes");
    t.expect_equal(gd::Int(q->ball_new), gd::Int(0), "Tekken Ball");
    t.expect_equal(gd::Int(q->theater_new), gd::Int(0), "Theater");
    t.expect_equal(gd::Int(q->unlock_class), gd::Int(0));
    t.expect_equal(gd::Int(q->cleared), gd::Int(0), "the arcade clears");
    t.expect_equal(gd::Int(q->cleared2), gd::Int(0), "the second clears");
    t.expect_equal(gd::Int(q->fights_since_unlock), gd::Int(0), "the play count of the unlock schedule");
    t.expect_equal(gd::Int(q->play_steps), gd::Int(0));
    _remove("unlock_save");
}

T3_TEST(progress_card, unlock_keeps_what_the_card_had_unlocked) {
    gd::Ref<ProgressCard> c = _locked_card("unlock_kept");
    _remove("unlock_kept");
    gd::Ref<GameProgress> p = progress();
    c->fresh_progress(p);
    p->unlocked = 0x7FF;
    p->ball_new = 2;
    p->cleared = 0x3;
    c->write(p->save_data());
    c->unlock_all = true;
    c->load_into(p);
    c->write(p->save_data());
    c->unlock_all = false;
    c->load_into(p);
    t.expect_equal(gd::Int(p->unlocked), gd::Int(0x7FF), "a real unlock survives an --unlock session");
    t.expect_equal(gd::Int(p->ball_new), gd::Int(2));
    t.expect_equal(gd::Int(p->cleared), gd::Int(0x3), "the real clears too");
    _remove("unlock_kept");
}

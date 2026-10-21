// remake/tests/platform/test_save_store.gd: the save file: the progress round-trips byte for byte;
// a missing or damaged file loads nothing.
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/game/game_progress.h"
#include "gd/io.h"
#include "platform/save_store.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

// Native: `user://` is a scratch folder of the tests (Godot's user:// of the test runs).
void use_test_user_dir() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "t3tests_user";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    SaveStore::user_dir = dir.string();
}

// One file per test: the runners may run the tests at the same time.
std::string PATH(const std::string& name) { return "user://test_progress_" + name + ".json"; }

void remove_absolute(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(SaveStore::globalize_path(path)), ec);
}

gd::Ref<SaveStore> _store(const std::string& path) {
    gd::Ref<SaveStore> s = gd::make<SaveStore>();
    s->progress_path = path;
    return s;
}

void _put(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(SaveStore::globalize_path(path).c_str(), "wb");
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

std::string get_file_as_string(const std::string& path) {
    gd::PBytes bytes = gd::read_file(SaveStore::globalize_path(path));
    std::string out;
    for (gd::Int b : bytes) out.push_back(static_cast<char>(b));
    return out;
}

gd::PBytes _document(gd::Int bytes_filled) {
    gd::PBytes bytes;
    bytes.resize(GameProgress::SAVE_BLOCK + GameProgress::RECORDS_SIZE);
    bytes.fill(bytes_filled);
    return bytes;
}

}  // namespace

T3_TEST(save_store, progress_round_trips) {
    use_test_user_dir();
    std::string path = PATH("round_trip");
    gd::Ref<SaveStore> s = _store(path);
    gd::PBytes bytes;
    for (gd::Int i = 0; i < GameProgress::SAVE_BLOCK + GameProgress::RECORDS_SIZE; ++i) {
        bytes.append((i * 7) & 0xFF);
    }
    t.expect_equal(s->save_progress(bytes), SaveStore::CARD_DONE);
    t.expect(s->load_progress() == bytes, "the bytes read back");
    remove_absolute(path);
}

T3_TEST(save_store, missing_or_damaged_file_loads_nothing) {
    use_test_user_dir();
    std::string path = PATH("damaged");
    gd::Ref<SaveStore> s = _store(path);
    remove_absolute(path);
    t.expect(s->load_progress().is_empty(), "missing file");
    _put(path, "{\"version\": 1, \"save\": \"AAAA\"}");
    t.expect(s->load_progress().is_empty(), "short file");
    remove_absolute(path);
}

T3_TEST(save_store, write_goes_through_a_temporary_file) {
    use_test_user_dir();
    std::string path = PATH("atomic");
    gd::Ref<SaveStore> s = _store(path);
    gd::PBytes first;
    first.resize(GameProgress::SAVE_BLOCK + GameProgress::RECORDS_SIZE);
    first.fill(1);
    t.expect_equal(s->save_progress(first), SaveStore::CARD_DONE);
    t.expect(!gd::file_exists(SaveStore::globalize_path(path + SaveStore::TEMPORARY_SUFFIX)), "no temporary file is left");
    // A write that cannot start (a directory holds the temporary name) keeps the previous save.
    std::error_code ec;
    std::filesystem::create_directory(SaveStore::globalize_path(path + SaveStore::TEMPORARY_SUFFIX), ec);
    gd::PBytes second = first.duplicate();
    second.fill(2);
    t.expect_equal(s->save_progress(second), SaveStore::CARD_ERROR, "the failed write is reported");
    t.expect(s->load_progress() == first, "the previous save is intact");
    remove_absolute(path + SaveStore::TEMPORARY_SUFFIX);
    remove_absolute(path);
}

// Windows removes the old file before its rename: a write interrupted between the two leaves only
// the complete temporary file, and a partial temporary file never replaces a good save.
T3_TEST(save_store, an_interrupted_write_is_recovered) {
    use_test_user_dir();
    std::string path = PATH("recovery");
    std::string temporary = path + SaveStore::TEMPORARY_SUFFIX;
    gd::Ref<SaveStore> s = _store(path);
    remove_absolute(path);
    gd::PBytes newest = _document(2);
    t.expect_equal(s->save_progress(newest), SaveStore::CARD_DONE);
    std::string text = get_file_as_string(path);
    remove_absolute(path);
    _put(temporary, text);
    t.expect(s->load_progress() == newest, "only the complete temporary file is left");
    remove_absolute(temporary);
    t.expect_equal(s->save_progress(_document(1)), SaveStore::CARD_DONE);
    _put(temporary, text.substr(0, text.size() / 2));
    t.expect(s->load_progress() == _document(1), "a partial temporary file leaves the save alone");
    remove_absolute(temporary);
    remove_absolute(path);
}

T3_TEST(save_store, settings_recover_the_same_way) {
    use_test_user_dir();
    std::string path = "user://test_settings_recovery.json";
    gd::Ref<SaveStore> store = gd::make<SaveStore>();
    store->settings_path = path;
    remove_absolute(path);
    _put(path + SaveStore::TEMPORARY_SUFFIX, "{\"volume_music\": 3}");
    gd::Json loaded = store->load_settings();
    t.expect(loaded.members().size() == 1 && loaded["volume_music"].num() == 3.0, "the complete temporary file");
    remove_absolute(path + SaveStore::TEMPORARY_SUFFIX);
    _put(path, "{\"volume_music\": 5}");
    _put(path + SaveStore::TEMPORARY_SUFFIX, "{\"volume_mus");
    loaded = store->load_settings();
    t.expect(loaded.members().size() == 1 && loaded["volume_music"].num() == 5.0, "a partial one is ignored");
    remove_absolute(path + SaveStore::TEMPORARY_SUFFIX);
    remove_absolute(path);
}

// Native: the file is Godot's JSON.stringify(..., "\t") of the same document, so the remake and the
// native build read each other's saves.
T3_TEST(save_store, progress_file_is_written_as_godot_writes_it) {
    use_test_user_dir();
    std::string path = PATH("format");
    gd::Ref<SaveStore> s = _store(path);
    gd::PBytes bytes;
    for (gd::Int i = 0; i < 4; ++i) bytes.append(i);
    s->save_progress(bytes);
    t.expect_equal(get_file_as_string(path), std::string("{\n\t\"save\": \"AAECAw==\",\n\t\"version\": 1\n}"));
    t.expect(SaveStore::base64_to_raw("AAECAw==") == bytes, "base64 read back");
    remove_absolute(path);
}

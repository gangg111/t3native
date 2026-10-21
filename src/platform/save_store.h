#pragma once
// remake/platform/save_store.gd
// The game's save files in place of the memory cards (remake-plan.md, M3): the save block and the
// records (GameProgress.save_data) in `user://progress.json` (card 1) or `progress2.json` (card 2,
// ProgressCard), and the remake's own settings
// (window, audio, input) in `user://settings.json`. The progress file keeps the bytes as the
// game's card file holds them, so a load restores exactly what was saved.
//
// Native: `user://` is `SaveStore::user_dir` (App::start's user directory: Android's
// internalDataPath, the development host's `--user`); `globalize_path` resolves it.
#include <string>

#include "gd/gd.h"
#include "gd/json.h"
#include "gd/json_writer.h"

namespace t3 {

class SaveStore {
public:
    static inline const gd::Arr<std::string> PROGRESS_PATHS{"user://progress.json", "user://progress2.json"};
    static inline const std::string SETTINGS_PATH = "user://settings.json";
    static constexpr gd::Int VERSION = 1;
    static inline const std::string TEMPORARY_SUFFIX = ".tmp";
    // The memory card results the options screen shows (menu_sim CardStub).
    static constexpr gd::Int CARD_DONE = 0;
    static constexpr gd::Int CARD_ERROR = 5;

    // Native: the directory `user://` stands for (ProjectSettings.globalize_path).
    static inline std::string user_dir;
    static std::string globalize_path(const std::string& path);

    std::string progress_path = PROGRESS_PATHS[0];
    std::string settings_path = SETTINGS_PATH;

    // The saved progress, or an empty array when there is none (or it is unreadable). A complete
    // temporary file of an interrupted write (see `_write`) is the newest save and is read first.
    gd::PBytes load_progress();
    // Writes the progress; returns a memory card result (0 done, 5 error).
    gd::Int save_progress(const gd::PBytes& bytes);
    // A Dictionary: a JSON object, an empty one when there is none.
    gd::Json load_settings();
    bool save_settings(const gd::JsonValue& settings);

    // The files a document may be in, the newest first: the temporary file of a write that was
    // interrupted before its rename (a partial one fails to parse and the loaders go on), then the file.
    static gd::Arr<std::string> _candidates(const std::string& path);
    // The JSON document in a file, or null when it does not parse (a write that was cut off).
    static gd::Json _parse(const std::string& path);
    gd::PBytes _read_progress(const std::string& path);
    // Writes `text` to a temporary file next to `path`, checks it by reading it back and renames it
    // over `path`, so that an interrupted write (a kill, a power loss, Android suspending the app)
    // leaves the previous file. Windows removes the old file before it renames: an interruption
    // between the two leaves the complete temporary file, which the loaders read.
    static bool _write(const std::string& path, const std::string& text);

    // Marshalls.raw_to_base64 / base64_to_raw (native).
    static std::string raw_to_base64(const gd::PBytes& bytes);
    static gd::PBytes base64_to_raw(const std::string& text);
};

}  // namespace t3

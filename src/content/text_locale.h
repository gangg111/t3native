#pragma once
// remake/content/text_locale.gd
// The game's texts in English (remake-plan.md, "Game texts"): the remake plays as Japan Rev.1
// and, when English is on, shows the USA release's English wherever it has one. The generic
// interface wording is the project's own (`content/texts_en.json`); names, titles, the staff
// roll, the Theater's lines, the move lists and the pictures with text come from the USA disc
// (`imported/usa/`, tools/remake_import/usa.py) and are used when it was converted.
//
// Strings are replaced by their body (the text after its leading `%x` format codes) within the
// game memory block they come from, so a string keeps its own codes.
#include <string>

#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class TextLocale {
public:
    // "res://content/texts_en.json" in the remake: relative to the folder that holds `imported/`
    // (load_from); formerly relative to the working directory, where
    // the app ships the project's own wording.
    static inline const std::string STATIC_PATH = "content/texts_en.json";
    static inline const std::string USA_DIR = "usa";
    static constexpr gd::Int THEATER_LINES = 8;

    bool english = false;
    gd::Dict<std::string, gd::Dict<std::string, std::string>> blocks;   // block name -> {Japanese body: English body}
    gd::Arr<std::string> theater_help;   // the USA help lines by the Japanese strip index ("" none)
    std::string practice_prompt = "";    // the recording prompt (drawn with a down arrow for `d`)
    std::string record_rank = "%d%s";    // the records' rank format
    gd::Arr<std::string> staff_titles;   // the USA staff roll, empty without the conversion
    gd::Arr<std::string> staff_rows;
    std::string usa_dir = "";            // the converted USA folder, "" when missing

    // The project's wording and, when `imported_root` has it, the converted USA localization.
    static gd::Ref<TextLocale> load_from(const std::string& imported_root);

    void _merge(const gd::Json& d);
    // Whether the converted USA localization is present (move lists, pictures, names).
    bool has_usa() const { return !usa_dir.empty(); }
    // A string of `block` as shown: its English when English is on and one is known.
    std::string text(const std::string& block, const std::string& s) const;

    // `value` (a string, or arrays and dictionaries of them) with each string localized.
    // The Variant parameter becomes one overload per shape: String, then Array / PackedStringArray
    // and Dictionary recursively over their elements.
    std::string texts(const std::string& block, const std::string& value) const { return text(block, value); }
    // A JSON value as read (strings, arrays and objects of them; other values unchanged).
    gd::Json texts(const std::string& block, const gd::Json& value) const;
    template <typename T>
    gd::Arr<T> texts(const std::string& block, const gd::Arr<T>& value) const {
        gd::Arr<T> out;
        for (const T& v : value) out.append(texts(block, v));
        return out;
    }
    template <typename K, typename V>
    gd::Dict<K, V> texts(const std::string& block, const gd::Dict<K, V>& value) const {
        gd::Dict<K, V> out;
        for (const auto& kv : value) out.set(kv.first, texts(block, kv.second));
        return out;
    }

    // The Theater's help line for strip `k` in English, or "" (the Japanese picture strip).
    std::string theater_line(gd::Int k) const;
    // A converted USA file (`usa/<name>`) when English is on and it exists, else "".
    std::string usa_file(const std::string& name) const;
    // The converted USA file `name` when English is on and it exists, else `fallback` (the Japanese
    // release's).
    std::string file_or(const std::string& name, const std::string& fallback) const;
    static bool _is_letter(gd::Int c);
};

}  // namespace t3

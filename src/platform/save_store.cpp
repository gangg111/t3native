// remake/platform/save_store.gd
#include "platform/save_store.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "content/json_file.h"
#include "core/game/game_progress.h"
#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

namespace {

const std::string USER_PREFIX = "user://";
const char* const BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// FileAccess.get_file_as_string.
std::string file_as_string(const std::string& path) {
    gd::PBytes bytes = gd::read_file(path);
    std::string text;
    gd::Int n = bytes.size();
    text.reserve(static_cast<size_t>(n));
    for (gd::Int i = 0; i < n; ++i) text.push_back(static_cast<char>(gd::Int(bytes[i])));
    return text;
}

// DirAccess.remove_absolute.
void remove_absolute(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path), ec);
}

}  // namespace

std::string SaveStore::globalize_path(const std::string& path) {
    if (path.compare(0, USER_PREFIX.size(), USER_PREFIX) != 0) return path;
    return gd::path_join(user_dir, path.substr(USER_PREFIX.size()));
}

gd::PBytes SaveStore::load_progress() {
    for (const std::string& path : _candidates(progress_path)) {
        gd::PBytes bytes = _read_progress(path);
        if (!bytes.is_empty()) return bytes;
    }
    return gd::PBytes();
}

gd::Int SaveStore::save_progress(const gd::PBytes& bytes) {
    gd::JsonValue doc = gd::JsonValue::object();
    doc.set("version", VERSION);
    doc.set("save", raw_to_base64(bytes));
    std::string text = gd::json_stringify(doc, "\t");
    return _write(progress_path, text) ? CARD_DONE : CARD_ERROR;
}

gd::Json SaveStore::load_settings() {
    for (const std::string& path : _candidates(settings_path)) {
        gd::Json d = _parse(path);
        if (d.is_object()) return d;
    }
    return gd::Json::parse("{}");
}

bool SaveStore::save_settings(const gd::JsonValue& settings) {
    return _write(settings_path, gd::json_stringify(settings, "\t"));
}

gd::Arr<std::string> SaveStore::_candidates(const std::string& path) {
    gd::Arr<std::string> out;
    for (const std::string& candidate : {path + TEMPORARY_SUFFIX, path}) {
        if (gd::file_exists(globalize_path(candidate))) out.append(candidate);
    }
    return out;
}

gd::Json SaveStore::_parse(const std::string& path) {
    bool ok = false;
    gd::Json json = gd::Json::parse(file_as_string(globalize_path(path)), &ok);
    if (!ok) return gd::Json();
    return json;
}

gd::PBytes SaveStore::_read_progress(const std::string& path) {
    gd::Json d = _parse(path);
    if (!d.is_object()) {
        Log::warning("SaveStore: " + path + " is not a progress file");
        return gd::PBytes();
    }
    const gd::Json& data = d;
    if (JsonFile::number(data.get("version")) != VERSION || !data.has("save")) {
        Log::warning("SaveStore: " + path + " has an unknown version");
        return gd::PBytes();
    }
    // str(data["save"]): the save is a string in every file the remake writes.
    gd::PBytes bytes = base64_to_raw(data["save"].str());
    if (bytes.size() != GameProgress::SAVE_BLOCK + GameProgress::RECORDS_SIZE) {
        Log::warning("SaveStore: " + path + " has " + gd::str(bytes.size()) + " bytes");
        return gd::PBytes();
    }
    return bytes;
}

bool SaveStore::_write(const std::string& path, const std::string& text) {
    std::string temporary = path + TEMPORARY_SUFFIX;
    std::string file = globalize_path(temporary);
    std::FILE* f = std::fopen(file.c_str(), "wb");
    if (f == nullptr) {
        Log::warning("SaveStore: cannot write " + temporary);
        return false;
    }
    bool stored = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    stored = std::fclose(f) == 0 && stored;
    // fwrite only reports the buffered write, fclose the flush.
    if (!stored || file_as_string(file) != text) {
        Log::warning("SaveStore: cannot write all of " + temporary);
        remove_absolute(file);
        return false;
    }
    // std::filesystem::rename replaces the old file (on Windows MoveFileEx with REPLACE_EXISTING).
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(file), std::filesystem::path(globalize_path(path)), ec);
    if (ec) {
        Log::warning("SaveStore: cannot replace " + path + " (" + ec.message() + ")");
        remove_absolute(file);
        return false;
    }
    return true;
}

std::string SaveStore::raw_to_base64(const gd::PBytes& bytes) {
    std::string out;
    gd::Int n = bytes.size();
    for (gd::Int i = 0; i < n; i += 3) {
        gd::Int b0 = bytes[i];
        gd::Int b1 = i + 1 < n ? gd::Int(bytes[i + 1]) : 0;
        gd::Int b2 = i + 2 < n ? gd::Int(bytes[i + 2]) : 0;
        gd::Int triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(BASE64[(triple >> 18) & 0x3F]);
        out.push_back(BASE64[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < n ? BASE64[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < n ? BASE64[triple & 0x3F] : '=');
    }
    return out;
}

// Stops at the first character that is not base64 (padding included), as a damaged file reads short.
gd::PBytes SaveStore::base64_to_raw(const std::string& text) {
    gd::PBytes out;
    gd::Int acc = 0;
    gd::Int bits = 0;
    for (char c : text) {
        const char* at = c != '\0' ? std::strchr(BASE64, c) : nullptr;
        if (at == nullptr) break;
        acc = ((acc << 6) | (at - BASE64)) & 0xFFFFFF;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.append((acc >> bits) & 0xFF);
        }
    }
    return out;
}

}  // namespace t3

#pragma once
// Files and gzip (the converter's `*.gz` assets), FileAccess / decompress_dynamic equivalents.
#include <string>

#include "gd/gd.h"

namespace gd {

// Paths starting with ASSET_PREFIX ("asset:") are read through the platform's hooks (Android: the
// APK's assets through AAssetManager); every other path through the C library.
inline const std::string ASSET_PREFIX = "asset:";
struct AssetHooks {
    bool (*read)(const std::string& name, PBytes& out) = nullptr;   // name without the prefix
    bool (*exists)(const std::string& name) = nullptr;
};
void set_asset_hooks(const AssetHooks& hooks);

bool file_exists(const std::string& path);
// The bytes of a file; empty when it cannot be read (FileAccess.get_file_as_bytes).
PBytes read_file(const std::string& path);
// A whole gzip member (RFC 1952) inflated; fails on corrupt data or a CRC mismatch.
PBytes gunzip(const PBytes& data);
// A zlib stream (RFC 1950, Python's zlib.compress; Godot COMPRESSION_DEFLATE) inflated, Adler-32 checked.
PBytes unzlib(const PBytes& data);
std::string path_join(const std::string& a, const std::string& b);
bool ends_with(const std::string& s, const std::string& suffix);

}  // namespace gd

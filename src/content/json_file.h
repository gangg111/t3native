#pragma once
// remake/content/json_file.gd + data_file.gd: JSON documents and binary files from the asset
// folder (gzip-compressed ones end in `.gz`).
#include <string>

#include "gd/json.h"

namespace t3 {

namespace DataFile {
// The bytes of a converted file, decompressed when its name ends in `.gz` (empty when missing).
gd::PBytes read(const std::string& path);
}  // namespace DataFile

namespace JsonFile {
// The document, or an empty object when the file is missing or invalid (logged).
gd::Json read(const std::string& path);
// A JSON array of numbers as PackedInt32Array (JSON numbers arrive as floats). Unsigned 32-bit
// words keep their bits (the conversion alone saturates them at 0x7FFFFFFF).
gd::PI32 ints(const gd::Json& value);
// A JSON number as int (0 when it is not a number).
gd::Int number(const gd::Json& value);
}  // namespace JsonFile

}  // namespace t3

#include "content/json_file.h"

#include <cmath>

#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

gd::PBytes DataFile::read(const std::string& path) {
    gd::PBytes data = gd::read_file(path);
    if (data.is_empty()) {
        if (!gd::file_exists(path)) Log::error("DataFile: cannot read " + path);
        return data;
    }
    if (gd::ends_with(path, ".gz")) return gd::gunzip(data);
    return data;
}

gd::Json JsonFile::read(const std::string& path) {
    std::string text = gd::ends_with(path, ".gz") ? DataFile::read(path).get_string_from_utf8()
                                                  : gd::read_file(path).get_string_from_utf8();
    if (text.empty()) {
        Log::error("JsonFile: cannot read " + path);
        return gd::Json::parse("{}");
    }
    bool ok = false;
    gd::Json value = gd::Json::parse(text, &ok);
    if (!ok) {
        Log::error("JsonFile: invalid JSON in " + path);
        return gd::Json::parse("{}");
    }
    return value;
}

namespace {
// Variant float -> int32 element of PackedInt32Array(Array): saturating (see the note above).
int32_t to_i32(const gd::Json& v) {
    double f = v.is_number() ? v.num() : 0.0;
    if (f >= 2147483647.0) return INT32_MAX;
    if (f <= -2147483648.0) return INT32_MIN;
    return static_cast<int32_t>(static_cast<gd::Int>(f));
}
}  // namespace

gd::PI32 JsonFile::ints(const gd::Json& value) {
    const std::vector<gd::Json>& items = value.items();
    gd::PI32 out;
    gd::Int max = 0;
    bool first = true;
    for (const gd::Json& item : items) {
        out.append(to_i32(item));
        gd::Int n = number(item);
        if (first || n > max) max = n;
        first = false;
    }
    if (items.empty() || max <= 0x7FFFFFFF) return out;
    for (size_t i = 0; i < items.size(); ++i) {
        gd::Int v = number(items[i]);
        out[static_cast<gd::Int>(i)] = v > 0x7FFFFFFF ? v - 0x100000000 : v;
    }
    return out;
}

gd::Int JsonFile::number(const gd::Json& value) {
    if (value.is_number()) return static_cast<gd::Int>(std::llround(value.num()));
    return 0;
}

}  // namespace t3

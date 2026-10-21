// remake/content/movie_file.gd
#include "content/movie_file.h"

#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

gd::Ref<MovieFile> MovieFile::open(const std::string& path) {
    gd::PBytes f = gd::read_file(path);
    if (f.is_empty()) {
        return nullptr;
    }
    // f.get_buffer(4).get_string_from_ascii() != MAGIC or f.get_32() != VERSION
    std::string magic;
    for (gd::Int k = 0; k < 4 && k < f.size(); ++k) magic += static_cast<char>(f.decode_u8(k));
    if (magic != MAGIC || f.size() < 20 || f.decode_u32(4) != VERSION) {
        Log::error("MovieFile: " + path + " is not a version " + gd::str(VERSION) + " movie");
        return nullptr;
    }
    gd::Ref<MovieFile> m = gd::make<MovieFile>();
    m->file = f;
    m->width = f.decode_u16(8);
    m->height = f.decode_u16(10);
    gd::Int count = f.decode_u32(12);
    m->sectors_per_second = f.decode_u32(16);
    gd::Int at = 20;
    for (gd::Int i = 0; i < count; ++i) {
        m->offsets.append(f.decode_u32(at));
        m->sizes.append(f.decode_u32(at + 4));
        m->sectors.append(f.decode_u32(at + 8));
        at += 12;
    }
    return m;
}

gd::Int MovieFile::frame_count() const {
    return offsets.size();
}

// The time (seconds from the start) at which frame `i` appears.
double MovieFile::frame_time(gd::Int i) const {
    return static_cast<double>(gd::Int(sectors[i]) - gd::Int(sectors[0])) / static_cast<double>(sectors_per_second);
}

gfx::Image MovieFile::image(gd::Int i) const {
    // file.seek(offsets[i]); img.load_jpg_from_buffer(file.get_buffer(sizes[i]))
    gd::PBytes jpeg = file.slice(offsets[i], gd::Int(offsets[i]) + gd::Int(sizes[i]));
    (void)jpeg;
    // TODO: JPEG decoding (no decoder in the native runtime): null.
    return gfx::Image();
}

}  // namespace t3

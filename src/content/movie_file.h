#pragma once
// remake/content/movie_file.gd
// The fallback movie container (`imported/movies/<name>.bin`, tools/remake_import/media.py, when
// FFmpeg has no Theora encoder): JPEG frames with the CD sector at which each frame arrives
// (150 sectors per second); the audio is `<name>.wav`, the length is the movies.json entry's
// `duration`.
#include <string>

#include "gd/gd.h"
#include "gfx/image.h"

namespace t3 {

class MovieFile {
public:
    static inline const std::string MAGIC = "T3MV";
    static constexpr gd::Int VERSION = 1;

    gd::Int width = 0;
    gd::Int height = 0;
    gd::Int sectors_per_second = 0;
    gd::PI32 offsets;
    gd::PI32 sizes;
    gd::PI32 sectors;
    // `var file: FileAccess`: the whole file's bytes (read with gd::read_file, which also reads
    // Android assets), read from at `offsets`.
    gd::PBytes file;

    static gd::Ref<MovieFile> open(const std::string& path);
    gd::Int frame_count() const;
    // The time (seconds from the start) at which frame `i` appears.
    double frame_time(gd::Int i) const;
    // Image.load_jpg_from_buffer: TODO: no JPEG decoder in the native runtime yet; an empty image
    // (the GDScript's null) until one is added. The converted movies are MPEG-1 (`video`), which do
    // not use this fallback.
    gfx::Image image(gd::Int i) const;
};

}  // namespace t3

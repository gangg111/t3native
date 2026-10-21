#pragma once
// remake/content/movie_captions.gd
// The ending movies' captions (formats/sound-and-video.md, "Movie captions"): `movies/captions.json`,
// or the USA release's English ones in `usa/captions.json` (tools/remake_import/captions.py). Each
// caption is a picture with its place in the movie's pixels (from the picture's top-left corner);
// each caption set lists its changes by decoded movie frame, as FUN_8010F0B4 shows them; `movies`
// names the set of each movie of the ending list (the releases caption different movies).
#include <string>

#include "gd/gd.h"
#include "gfx/texture.h"

namespace t3 {

class MovieCaptions {
public:
    gd::Arr<gfx::TextureRef> pictures;
    gd::Arr<gd::Vector2i> places;
    gd::Arr<gd::PI32> sets;     // per set (1 the first): frame, caption, frame, caption, ...
    gd::PI32 movies;            // per ending movie: its caption set, 0 none

    // The captions of `path`, or null when it was not converted.
    static gd::Ref<MovieCaptions> load_from(const std::string& path);
    // The caption set of ending movie `movie` (0 none).
    gd::Int set_of(gd::Int movie) const;
    // The caption of set `set_number` (1 the first) shown on decoded frame `frame` (1 the first), -1 none.
    gd::Int caption_at(gd::Int set_number, gd::Int frame) const;
};

}  // namespace t3

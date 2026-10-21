// remake/content/movie_captions.gd
#include "content/movie_captions.h"

#include "content/json_file.h"
#include "content/texture_packs.h"
#include "gd/io.h"

namespace t3 {

namespace {
// String.get_base_dir.
std::string get_base_dir(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}
}  // namespace

// The captions of `path`, or null when it was not converted.
gd::Ref<MovieCaptions> MovieCaptions::load_from(const std::string& path) {
    if (path.empty() || !gd::file_exists(path)) {
        return nullptr;
    }
    gd::Json d = JsonFile::read(path);
    gd::Ref<MovieCaptions> c = gd::make<MovieCaptions>();
    const gd::Json& captions = d.get("captions");
    gd::Int n_captions = captions.is_array() ? captions.size() : 0;
    for (gd::Int i = 0; i < n_captions; ++i) {
        const gd::Json& entry = captions[i];
        // load(...) as Texture2D: the picture through TexturePacks (a pack may replace it).
        c->pictures.append(TexturePacks::texture(gd::path_join(get_base_dir(path), entry["picture"].str())));
        c->places.append(gd::Vector2i(JsonFile::number(entry["x"]), JsonFile::number(entry["y"])));
    }
    const gd::Json& sets = d.get("sets");
    gd::Int n_sets = sets.is_array() ? sets.size() : 0;
    for (gd::Int s = 0; s < n_sets; ++s) {
        const gd::Json& changes = sets[s];
        gd::PI32 flat;
        for (gd::Int k = 0; k < changes.size(); ++k) {
            flat.append_array(JsonFile::ints(changes[k]));
        }
        c->sets.append(flat);
    }
    c->movies = JsonFile::ints(d.get("movies"));
    return c;
}

// The caption set of ending movie `movie` (0 none).
gd::Int MovieCaptions::set_of(gd::Int movie) const {
    return movie >= 0 && movie < movies.size() ? gd::Int(movies[movie]) : 0;
}

// The caption of set `set_number` (1 the first) shown on decoded frame `frame` (1 the first), -1 none.
gd::Int MovieCaptions::caption_at(gd::Int set_number, gd::Int frame) const {
    if (set_number < 1 || set_number > sets.size()) {
        return -1;
    }
    const gd::PI32& changes = sets[set_number - 1];
    gd::Int shown = -1;
    gd::Int n = changes.size();
    for (gd::Int k = 0; k < n; k += 2) {
        if (changes[k] > frame) {
            break;
        }
        shown = changes[k + 1];
    }
    return shown < pictures.size() ? shown : -1;
}

}  // namespace t3

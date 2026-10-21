// remake/presentation/screens/movie_player.gd
#include "presentation/screens/movie_player.h"

#include <algorithm>
#include <cstring>
#include <memory>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg/pl_mpeg.h"

#include "content/json_file.h"
#include "content/movie_captions.h"
#include "content/movie_file.h"
#include "gd/io.h"
#include "gfx/texture.h"
#include "platform/log.h"
#include "presentation/screens/screen_box.h"

namespace t3 {

// --- Video (VideoStreamPlayer + VideoStreamTheora, native: pl_mpeg) ---

MoviePlayer::Video::~Video() { _close(); }

void MoviePlayer::Video::_close() {
    if (_plm) plm_destroy(_plm);
    _plm = nullptr;
    _frame = nullptr;
    _bytes = gd::PBytes();
}

bool MoviePlayer::Video::set_stream(const std::string& path) {
    stop();
    _close();
    _bytes = gd::read_file(path);
    if (_bytes.is_empty()) return false;
    _plm = plm_create_with_memory(_bytes.data(), static_cast<size_t>(_bytes.size()), 0);
    if (!_plm || plm_get_width(_plm) <= 0 || plm_get_height(_plm) <= 0) {
        Log::error("MoviePlayer: " + path + " is not an MPEG-1 movie");
        _close();
        return false;
    }
    plm_set_loop(_plm, 0);
    bool sound = plm_get_num_audio_streams(_plm) > 0;
    plm_set_audio_enabled(_plm, sound ? 1 : 0);
    _rate = sound ? plm_get_samplerate(_plm) : 0;
    return true;
}

void MoviePlayer::Video::play() {
    if (!_plm) return;
    plm_rewind(_plm);
    stream_position = 0.0;
    _audio_written = 0;
    _pending.clear();
    _audio_done = _rate <= 0;
    _frame = plm_decode_video(_plm);
    if (!_frame) return;
    _next_frame_time = static_cast<plm_frame_t*>(_frame)->time;
    _playing = true;
    // video.volume_db on the Music bus: the mixer's streamed source.
    if (mixer && _rate > 0 && !mixer->stream_start(_rate, bus, volume_db)) _audio_done = true;
    _feed_audio();
}

void MoviePlayer::Video::stop() {
    if (_playing && mixer && _rate > 0) mixer->stream_stop();
    _playing = false;
    _frame = nullptr;
}

// The sound decoded ahead of the picture by AUDIO_LEAD, as far as the mixer's ring takes it.
void MoviePlayer::Video::_feed_audio() {
    if (_audio_done || !mixer) return;
    int64_t target = static_cast<int64_t>((stream_position + AUDIO_LEAD) * _rate);
    while (_audio_written < target) {
        if (_pending.empty()) {
            plm_samples_t* s = plm_decode_audio(_plm);
            if (!s) {
                _audio_done = true;
                return;
            }
            _pending.assign(s->interleaved, s->interleaved + s->count * 2);
        }
        int64_t frames = static_cast<int64_t>(_pending.size() / 2);
        int64_t n = mixer->stream_write(_pending.data(), frames);
        _audio_written += n;
        _pending.erase(_pending.begin(), _pending.begin() + n * 2);
        if (n < frames) return;   // the ring is full: the rest waits for the next tick
    }
}

void MoviePlayer::Video::process(double delta) {
    if (!_playing) return;
    stream_position += delta;
    double frame_length = 1.0 / std::max(1.0, plm_get_framerate(_plm));
    while (_frame && static_cast<plm_frame_t*>(_frame)->time <= stream_position + 1e-9) {
        plm_frame_t* f = static_cast<plm_frame_t*>(_frame);
        int w = static_cast<int>(f->width), h = static_cast<int>(f->height);
        if (_image.width != w || _image.height != h) {
            _image.width = w;
            _image.height = h;
            // Opaque: plm_frame_to_rgba leaves the alpha bytes untouched.
            _image.rgba.assign(static_cast<size_t>(w) * h * 4, 255);
        }
        plm_frame_to_rgba(f, _image.rgba.data(), w * 4);
        _dirty = true;
        _next_frame_time = f->time + frame_length;
        _frame = plm_decode_video(_plm);
    }
    _feed_audio();
    // The stream ends after its last frame's time.
    if (!_frame && stream_position + 1e-9 >= _next_frame_time) {
        stop();
        std::vector<std::function<void()>> listeners = finished;
        for (auto& f : listeners) f();
    }
}

void MoviePlayer::Video::draw(gfx::Canvas& canvas) {
    if (!visible || _image.empty()) return;
    if (_dirty || !_texture) {
        if (!_texture || _texture->width() != _image.width || _texture->height() != _image.height) {
            _texture = std::make_shared<gfx::Texture>(_image);
        } else {
            _texture->update(_image);
        }
        _dirty = false;
    }
    // expand: the frame stretched over the control.
    canvas.draw_texture_rect(_texture, gfx::Rect2(position.x, position.y, size.x, size.y));
}

// --- MoviePlayer ---

void MoviePlayer::_ready(Mixer* mixer) {
    // set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT): `size` is the window's (draw).
    audio.bus = "Music";
    audio.mixer = mixer;
    audio.voice = FALLBACK_VOICE;
    video.mixer = mixer;
    video.bus = Mixer::bus_index("Music");
    video.expand = true;
    video.visible = false;
    video.finished.push_back([this] { stop(); });
    caption.visible = false;
}

// Plays movie `entry` of `movies/movies.json` from `folder` (`caption_movie`: its place in the
// ending list, -1 for a title movie); false when it cannot be played.
bool MoviePlayer::play(const std::string& folder, const gd::Json& entry, float volume, gd::Int caption_movie) {
    // Whatever played before stops (both paths: a Theora movie's video, a fallback's sound).
    _reset();
    float lines = entry.has("height") ? static_cast<float>(JsonFile::number(entry["height"])) : DISPLAY_LINES;
    _lines = lines;
    _width = static_cast<float>(JsonFile::number(entry.get("width")));
    _captions = captions;
    _caption_set = _captions != nullptr ? _captions->set_of(caption_movie) : 0;
    _frame_times.clear();
    gd::Int fps = JsonFile::number(entry.get("fps"));
    const gd::Json& slots = entry.get("frame_slots");
    gd::Int n_slots = slots.is_array() ? slots.size() : 0;
    for (gd::Int k = 0; k < n_slots; ++k) {
        _frame_times.push_back(static_cast<double>(JsonFile::number(slots[k])) / static_cast<double>(fps));
    }
    _show_caption(-1);
    if (entry.has("video")) {
        const gd::Json& variants = entry["video"];
        std::string stream = _first_video(folder, variants);
        if (stream.empty()) {
            return false;
        }
        movie = nullptr;
        if (!video.set_stream(stream)) {
            return false;
        }
        video.volume_db = Mixer::linear_to_db(volume);
        video.visible = true;
        _start();
        video.play();
        if (!video.is_playing()) {
            _reset();
            return false;
        }
        return true;
    }
    if (!entry.has("frames")) {
        return false;
    }
    movie = MovieFile::open(gd::path_join(folder, entry["frames"].str()));
    if (movie == nullptr || movie->frame_count() == 0 || !entry.has("duration")) {
        return false;
    }
    _lines = static_cast<float>(movie->height);
    double duration = entry["duration"].num();
    _duration = duration;
    gd::Ref<AudioStream> sound = entry.has("audio") ? AudioStream::load(gd::path_join(folder, entry["audio"].str())) : nullptr;
    audio.stream = sound;
    audio.volume_db = Mixer::linear_to_db(volume);
    texture = nullptr;
    shown = -1;
    _start();
    if (sound != nullptr) {
        audio.play();
    }
    return true;
}

// The first variant present in this build.
std::string MoviePlayer::_first_video(const std::string& folder, const gd::Json& variants) {
    gd::Int n = variants.is_array() ? variants.size() : 0;
    for (gd::Int k = 0; k < n; ++k) {
        std::string file = variants[k].str();
        // Native: the MPEG-1 transcode of the Theora variant (tools/movies_from_disc.py).
        if (gd::ends_with(file, ".ogv")) file = file.substr(0, file.size() - 4) + ".mpg";
        std::string path = gd::path_join(folder, file);
        if (gd::file_exists(path)) {
            return path;
        }
    }
    return "";
}

void MoviePlayer::_start() {
    playing = true;
    visible = true;
    _started_at = _clock;
    _place_video();
}

void MoviePlayer::stop() {
    if (!playing) {
        return;
    }
    _reset();
    std::vector<std::function<void()>> listeners = finished;
    for (auto& f : listeners) f();
}

// Back to idle without a `finished` signal (a movie that did not start).
void MoviePlayer::_reset() {
    playing = false;
    audio.stop();
    video.stop();
    video.visible = false;
    visible = false;
    _show_caption(-1);
}

void MoviePlayer::_process(double delta) {
    _clock += delta;
    // The VideoStreamPlayer child's internal process (its clock, frames and sound).
    video.process(delta);
    if (!playing) {
        return;
    }
    if (movie == nullptr) {
        _place_video();
        // _frame_times.bsearch(video.stream_position, false): the number of times <= the position.
        gd::Int frame = std::upper_bound(_frame_times.begin(), _frame_times.end(), video.stream_position) -
                        _frame_times.begin();
        _update_caption(frame);
        return;
    }
    // audio.get_playback_position() + AudioServer.get_time_since_last_mix(): TODO: the mixer has no
    // playback position; the fallback runs on the app's clock (the GDScript's silent branch).
    double t = _clock - _started_at;
    if (t >= _duration) {
        stop();
        return;
    }
    gd::Int i = shown;
    while (i + 1 < movie->frame_count() && movie->frame_time(i + 1) <= t) {
        i += 1;
    }
    if (i != shown && i >= 0) {
        gfx::Image img = movie->image(i);
        if (!img.empty()) {
            // ImageTexture.create_from_image / update: uploaded when drawn.
            _texture_image = img;
            texture = nullptr;
        }
        shown = i;
    }
    _update_caption(shown + 1);
}

// The caption of decoded frame `frame` (1 the first) and its place over the picture.
void MoviePlayer::_update_caption(gd::Int frame) {
    if (_caption_set == 0) {
        return;
    }
    gd::Int index = _captions->caption_at(_caption_set, frame);
    if (index != _caption_shown) {
        _show_caption(index);
    }
    if (index < 0 || _width <= 0.0f) {
        return;
    }
    gfx::Rect2 r = _picture_rect();
    gfx::Vec2 scale(r.size.x / _width, ScreenBox::rect(size).size.y / DISPLAY_LINES);
    const gfx::TextureRef& picture = _captions->pictures[index];
    gd::Vector2i place = _captions->places[index];
    caption.position = r.position + gfx::Vec2(static_cast<float>(gd::Int(place.x)) * scale.x,
                                              static_cast<float>(gd::Int(place.y)) * scale.y);
    gfx::Vec2 picture_size = picture ? gfx::Vec2(static_cast<float>(picture->width()), static_cast<float>(picture->height()))
                                     : gfx::Vec2(0, 0);
    caption.size = gfx::Vec2(picture_size.x * scale.x, picture_size.y * scale.y);
}

void MoviePlayer::_show_caption(gd::Int index) {
    _caption_shown = index;
    caption.texture = index >= 0 ? gfx::TextureRef(_captions->pictures[index]) : nullptr;
    caption.visible = index >= 0;
}

// The picture's rectangle: the 4:3 box, a shorter picture centred on its 240 lines.
gfx::Rect2 MoviePlayer::_picture_rect() const {
    gfx::Rect2 box = ScreenBox::rect(size);
    float h = box.size.y * std::min(_lines / DISPLAY_LINES, 1.0f);
    return gfx::Rect2(box.position.x, box.position.y + (box.size.y - h) / 2.0f, box.size.x, h);
}

void MoviePlayer::_place_video() {
    gfx::Rect2 r = _picture_rect();
    video.position = r.position;
    video.size = r.size;
}

void MoviePlayer::draw(gfx::Canvas& canvas, gfx::Vec2 window) {
    if (!visible) return;
    size = window;
    // The layout follows the window (the anchors): the picture and the caption are placed again.
    _place_video();
    if (playing && movie == nullptr) {
        gd::Int frame = std::upper_bound(_frame_times.begin(), _frame_times.end(), video.stream_position) -
                        _frame_times.begin();
        _update_caption(frame);
    }
    // "with a smooth filter"
    canvas.set_filter(gfx::Filter::LINEAR);
    _draw(canvas);
    video.draw(canvas);
    if (caption.visible && caption.texture) {
        canvas.draw_texture_rect(caption.texture, gfx::Rect2(caption.position.x, caption.position.y, caption.size.x,
                                                             caption.size.y));
    }
    canvas.set_filter(gfx::Filter::NEAREST);
}

void MoviePlayer::_draw(gfx::Canvas& canvas) {
    canvas.draw_rect(gfx::Rect2(0, 0, size.x, size.y), gfx::Color::black());
    if (movie != nullptr && !_texture_image.empty()) {
        if (texture == nullptr) {
            texture = std::make_shared<gfx::Texture>(_texture_image);
        }
        canvas.draw_texture_rect(texture, _picture_rect(), false);
    }
}

void MoviePlayer::graphics_lost() {
    video.graphics_lost();
    texture = nullptr;
}

}  // namespace t3

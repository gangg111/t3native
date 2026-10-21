#pragma once
// remake/presentation/screens/movie_player.gd
// Plays a converted movie in the window's 4:3 area (the original display is 256 or 320 pixels
// wide, 240 lines), with a smooth filter. An Ogg Theora movie (`video`: its variants, preferred
// first; a build packs one) plays through a VideoStreamPlayer; the fallback (`frames` +
// `audio`, a MovieFile and a WAV) shows the frame due at the audio position until the entry's
// `duration`. `finished` is emitted at the end, or when `stop()` is called.
// An ending movie (`caption_movie`, its place in the ending list) shows the caption set `captions`
// names for it over the picture by decoded frame: the Theora movie's frame from the entry's
// `frame_slots` (each decoded frame's first slot of the `fps` grid), the fallback's from the frame
// shown.
//
// Native: the Theora movies are played as their MPEG-1 transcodes (tools/movies_from_disc.py:
// `<variant>.mpg` beside each `<variant>.ogv` the entry names) through pl_mpeg (`Video`, the
// VideoStreamPlayer). The player is a `extends Control` view: `visible`, `size` and `draw`.
// Time comes from the app: `_process(delta)` is called once a physics tick (1 / 60 s), where
// Godot's video advanced by its render frames' delta. The sound goes to the mixer's streamed
// source (Mixer::stream_*) on the Music bus.
#include <functional>
#include <string>
#include <vector>

#include "audio/mixer.h"
#include "gd/gd.h"
#include "gd/json.h"
#include "gfx/canvas.h"
#include "gfx/image.h"

struct plm_t;

namespace t3 {

class MovieFile;
class MovieCaptions;

class MoviePlayer {
public:
    // VideoStreamPlayer with a VideoStreamTheora, native: an MPEG-1 program stream decoded by
    // pl_mpeg from its bytes (gd::read_file, so Android assets too); each decoded frame becomes
    // RGBA (plm_frame_to_rgba) in `image` and the texture is updated from it when drawn.
    class Video {
    public:
        // The sound written ahead of the picture's clock (the mixer plays what it was given).
        static constexpr double AUDIO_LEAD = 0.1;

        Mixer* mixer = nullptr;
        int bus = Mixer::MUSIC;          // video.bus = &"Music"
        float volume_db = 0.0f;
        bool expand = true;
        bool visible = false;
        gfx::Vec2 position;
        gfx::Vec2 size;
        double stream_position = 0.0;    // seconds since play()
        std::vector<std::function<void()>> finished;   // signal finished

        Video() = default;
        ~Video();
        Video(const Video&) = delete;
        Video& operator=(const Video&) = delete;

        // video.stream = load(path): false when the file is missing or not an MPEG-1 stream.
        bool set_stream(const std::string& path);
        void play();
        void stop();
        bool is_playing() const { return _playing; }
        // The VideoStreamPlayer's internal process: the clock advances by `delta`, the frames due
        // are decoded, the sound is fed ahead; at the end of the stream `finished` is emitted.
        void process(double delta);
        void draw(gfx::Canvas& canvas);
        // The GL context was lost: the texture is made again from the last frame.
        void graphics_lost() { _texture = nullptr; _dirty = !_image.empty(); }

    private:
        void _close();
        void _feed_audio();
        gd::PBytes _bytes;               // the stream (pl_mpeg reads it in place)
        plm_t* _plm = nullptr;
        bool _playing = false;
        double _next_frame_time = 0.0;   // the next undecoded frame's presentation time
        gfx::Image _image;               // the frame shown
        bool _dirty = false;
        gfx::TextureRef _texture;
        int _rate = 0;                   // audio samples per second, 0: no sound
        int64_t _audio_written = 0;      // frames handed to the mixer
        std::vector<float> _pending;     // decoded stereo frames the mixer had no room for
        bool _audio_done = false;
        void* _frame = nullptr;          // the decoded frame not yet due (plm_frame_t*)
    };

    // A TextureRect (EXPAND_IGNORE_SIZE, STRETCH_SCALE).
    struct TextureRect {
        gfx::TextureRef texture;
        gfx::Vec2 position;
        gfx::Vec2 size;
        bool visible = false;
    };

    static constexpr float DISPLAY_LINES = 240.0f;

    std::vector<std::function<void()>> finished;   // signal finished

    gd::Ref<MovieFile> movie;
    AudioStreamPlayer audio;
    Video video;
    gfx::TextureRef texture;
    gfx::Image _texture_image;          // ImageTexture.update(img)'s image, uploaded when drawn
    gd::Int shown = -1;
    bool playing = false;
    gd::Ref<MovieCaptions> captions;     // the language's captions (game.gd), null: none converted
    TextureRect caption;
    gd::Ref<MovieCaptions> _captions;    // the captions this movie started with (their set numbering)
    gd::Int _caption_set = 0;
    gd::Int _caption_shown = -1;
    std::vector<double> _frame_times;    // PackedFloat64Array
    float _width = 0.0f;
    float _lines = DISPLAY_LINES;
    double _duration = 0.0;
    double _started_at = 0.0;
    // Control
    bool visible = false;
    gfx::Vec2 size;
    // Time.get_ticks_msec() / 1000.0, native: the seconds of the app's ticks (`_process` deltas).
    double _clock = 0.0;

    // _ready: the children set up; `mixer` plays the sound (the AudioStreamPlayer's and the
    // video's). The fallback's WAV plays on mixer voice FALLBACK_VOICE.
    static constexpr int FALLBACK_VOICE = Mixer::MAX_VOICES - 1;
    void _ready(Mixer* mixer);
    // Plays movie `entry` of `movies/movies.json` from `folder` (`caption_movie`: its place in the
    // ending list, -1 for a title movie); false when it cannot be played.
    bool play(const std::string& folder, const gd::Json& entry, float volume, gd::Int caption_movie = -1);
    // The first variant present in this build (its `.mpg`), "" when none is.
    static std::string _first_video(const std::string& folder, const gd::Json& variants);
    void _start();
    void stop();
    // Back to idle without a `finished` signal (a movie that did not start).
    void _reset();
    void _process(double delta);
    // The caption of decoded frame `frame` (1 the first) and its place over the picture.
    void _update_caption(gd::Int frame);
    void _show_caption(gd::Int index);
    // The picture's rectangle: the 4:3 box, a shorter picture centred on its 240 lines.
    gfx::Rect2 _picture_rect() const;
    void _place_video();
    // _draw, then the children (video, caption) as the scene tree draws them.
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw(gfx::Canvas& canvas);
    void graphics_lost();
};

}  // namespace t3

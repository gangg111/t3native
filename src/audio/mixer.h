#pragma once
// The native stand-in for Godot's audio engine (AudioServer, AudioStreamPlayer, AudioStreamWAV,
// AudioStreamOggVorbis), as far as the AudioDirector uses it.
//
// Threads: everything but `Mixer::mix` runs on the game thread. `mix` runs on the platform's
// real-time audio thread (App::audio) and never allocates, locks or reads files:
// - the game thread sends commands through a single-producer, single-consumer ring (`_commands`);
//   `mix` drains it at the start of each buffer and publishes the last sequence number it applied;
// - decoded tones (`Pcm`) are shared read-only; the game thread keeps every tone a voice was given
//   alive until the audio thread has applied the command that replaced it (`_retired`), so the
//   audio thread only ever holds raw pointers and never frees anything;
// - the music is decoded by a worker thread of its own (`_music_worker`, every 20 ms, under
//   `_music_mutex` with the game thread's music_play / music_stop) into a ring of float frames at
//   the stream's own rate that `mix` reads (`_music_ring`, monotonic 64-bit counters): a game thread
//   held up longer than the ring (loading a fight's models) does not starve the music.
//
// Godot's meaning, reproduced:
// - volume_db: the gain is db_to_linear(volume_db) = 10^(db / 20); linear_to_db is 20 log10(x)
//   (Godot's Math::linear_to_db / db_to_linear). A player's gain and its bus's gain multiply.
// - buses: Music, SFX, Voices and System (AudioDirector.BUSES) sum into the master bus; each has a
//   volume (AudioServer.set_bus_volume_db) and a mute (set_bus_mute).
// - AudioEffectStereoEnhance with pan_pullout 0 on the master bus (the director's downmix): Godot
//   computes center = (l + r) / 2, l = center + (l - center) * pan_pullout, likewise r, so with
//   pullout 0 both sides become (l + r) / 2 (`set_mono`). Its time_pullout and surround are 0.
// - a mono stream plays at full level on both sides, as AudioStreamPlaybackWAV writes (s, s).
// - AudioStreamWAV's loop (the `smpl` chunk, edit/loop_mode "detect from WAV", forward): after
//   loop_end the position goes back by (loop_end - loop_begin). The converter writes the chunk's
//   end as the last sample (inclusive, tools/remake_import/media.py wav_bytes), so loop_end is
//   that plus one.
// - AudioStreamOggVorbis.loop: the whole stream loops (loop_offset 0).
// - pitch_scale is not used by the director; the resampler steps rate_in / rate_out source frames
//   per output frame with linear interpolation (Godot interpolates cubically: may differ).
// - a volume change while playing ramps linearly over one mix buffer (Godot ramps a player's volume
//   over its mix chunk too), which avoids clicks on the music fades.
// - the final mix is clamped to -1 ... 1 (Godot's drivers clamp when they convert).
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gd/gd.h"

struct stb_vorbis;

namespace t3 {

// Decoded 16-bit PCM, shared read-only between the threads.
struct Pcm {
    std::vector<int16_t> samples;   // interleaved
    int channels = 1;
    int rate = 0;
    int64_t loop_begin = 0;         // frames
    int64_t loop_end = -1;          // frames, exclusive; -1: no loop
    int64_t frames() const { return channels > 0 ? static_cast<int64_t>(samples.size()) / channels : 0; }
    // A PCM WAV (RIFF, 16-bit, 1 or 2 channels, optional `smpl` forward loop); null when not one.
    static std::shared_ptr<const Pcm> from_wav(const gd::PBytes& bytes);
};
using PcmRef = std::shared_ptr<const Pcm>;

class Mixer {
public:
    // AudioDirector.BUSES, in order; MASTER is Godot's bus 0.
    enum Bus : int { MUSIC, SFX, VOICES, SYSTEM, BUS_COUNT };
    static constexpr int MAX_VOICES = 32;               // the director uses 24 (SPU_VOICES)
    static constexpr int COMMANDS = 1024;               // queued between two mixes at most
    static constexpr int64_t MUSIC_RING = 1 << 16;      // frames (about 1.5 s at 44.1 kHz)
    static constexpr int64_t MUSIC_DECODE_STEP = 1 << 14;   // frames decoded per update at most

    static float db_to_linear(float db);
    static float linear_to_db(float linear);
    // A bus by its Godot name ("Music", "SFX", "Voices", "System"); SFX for any other.
    static int bus_index(const std::string& name);

    Mixer();
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // Game thread. `play` restarts `voice` with `pcm` (null: only stops it) and returns the play's
    // id for `voice_playing`.
    uint64_t play(int voice, const PcmRef& pcm, int bus, float volume_db);
    void set_voice_volume_db(int voice, float volume_db);
    void stop(int voice);
    bool voice_playing(int voice, uint64_t id) const;
    void set_bus_volume_db(int bus, float volume_db);
    void set_bus_mute(int bus, bool mute);
    void set_mono(bool on);
    bool mono() const { return _mono; }

    // The music stream: an Ogg Vorbis file's bytes (kept by the caller's stream object as well).
    // False when it does not decode.
    bool music_play(const gd::PBytes& ogg, bool loop, int bus, float volume_db);
    void music_set_volume_db(float volume_db);
    void music_stop();
    bool music_playing() const;
    // Once a step: decodes music ahead and releases the tones the audio thread no longer holds.
    void update();

    // A streamed PCM source fed by the game thread (the movie player's sound, a VideoStreamPlayer's
    // audio): stereo float frames at `rate` Hz into a ring (`_stream_ring`) the audio thread reads
    // like the music's, without locks or allocations. `stream_write` takes as many frames as there
    // is room for and returns how many; `stream_queued` is the number written and not yet played.
    static constexpr int64_t STREAM_RING = 1 << 15;     // frames (about 0.7 s at 44.1 kHz)
    bool stream_start(int rate, int bus, float volume_db);
    int64_t stream_write(const float* stereo, int64_t frames);
    int64_t stream_queued() const;
    void stream_stop();

    // Audio thread: `frames` stereo float frames at `rate` Hz.
    void mix(float* out, int frames, int rate);

    // How many commands could not be queued (the audio thread was not draining them).
    int64_t dropped_commands = 0;

private:
    enum CommandKind : int { PLAY, STOP, VOICE_VOLUME, BUS_VOLUME, MONO, MUSIC_START, MUSIC_VOLUME, MUSIC_STOP,
                             STREAM_START, STREAM_STOP };
    struct Command {
        int kind = PLAY;
        int index = 0;          // voice or bus
        int bus = SFX;
        float gain = 1.0f;      // linear
        const Pcm* pcm = nullptr;
        uint64_t id = 0;        // PLAY: the play's id
        int64_t start = 0;      // MUSIC_START: the ring position its frames start at
        int rate = 0;           // MUSIC_START: the stream's rate
        uint64_t seq = 0;
    };
    bool _push(Command c);

    std::array<Command, COMMANDS> _commands{};
    std::atomic<uint64_t> _head{0};        // written by the game thread
    std::atomic<uint64_t> _tail{0};        // written by the audio thread
    std::atomic<uint64_t> _applied{0};     // the last command sequence the audio thread applied
    uint64_t _seq = 0;
    uint64_t _next_id = 0;

    // Game side.
    std::array<PcmRef, MAX_VOICES> _held{};
    std::array<uint64_t, MAX_VOICES> _voice_id{};
    std::vector<std::pair<uint64_t, PcmRef>> _retired;   // (sequence that released it, tone)
    std::array<float, BUS_COUNT> _bus_db{};
    std::array<bool, BUS_COUNT> _bus_mute{};
    bool _mono = false;
    void _send_bus(int bus);

    // Audio side (only `mix` touches these).
    struct Voice {
        const Pcm* pcm = nullptr;
        uint64_t id = 0;
        int bus = SFX;
        double pos = 0;
        float gain = 0, target = 0;
    };
    std::array<Voice, MAX_VOICES> _voices{};
    std::array<float, BUS_COUNT> _bus_gain{};
    bool _mix_mono = false;
    std::array<std::atomic<uint64_t>, MAX_VOICES> _finished{};   // the id of the play that ended

    // Music. Game side: the decoder and the write counter; audio side: the read state.
    std::vector<float> _music_ring;                 // MUSIC_RING stereo frames
    std::atomic<int64_t> _music_write{0};           // frames written (published by the game thread)
    std::atomic<int64_t> _music_read{0};            // frames consumed (published by the audio thread)
    std::atomic<int64_t> _music_fence{0};           // where the stream before the newest one ends
    gd::PBytes _music_data;
    stb_vorbis* _vorbis = nullptr;
    int _music_channels = 0;
    bool _music_loop = false;
    bool _music_on = false;                         // a stream was started and not stopped
    int64_t _music_eof = -1;                        // write position of a finished stream's end
    float _music_db = 0;
    int _music_bus = MUSIC;
    void _music_close();
    void _music_decode(int64_t frames);
    void _music_worker();
    mutable std::mutex _music_mutex;                // the decoder's state: the game thread and the worker
    std::condition_variable _music_wake;
    bool _music_quit = false;
    std::thread _music_thread;
    // audio side
    bool _m_on = false;
    int64_t _m_start = 0;
    int _m_rate = 0;
    int _m_bus = MUSIC;
    double _m_frac = 0;
    float _m_gain = 0, _m_target = 0;

    // The streamed source. Game side: the write counter; audio side: the read state.
    std::vector<float> _stream_ring;                // STREAM_RING stereo frames
    std::atomic<int64_t> _stream_write{0};          // frames written (published by the game thread)
    std::atomic<int64_t> _stream_read{0};           // frames consumed (published by the audio thread)
    std::atomic<int64_t> _stream_fence{0};          // where the stream before the newest one ends
    bool _stream_on = false;
    int64_t _stream_start = 0;                      // game side: the newest stream's first frame
    // audio side
    bool _s_on = false;
    int64_t _s_start = 0;
    int _s_rate = 0;
    int _s_bus = MUSIC;
    double _s_frac = 0;
    float _s_gain = 0;
};

// Godot's AudioStream resources the director loads.
class AudioStream {
public:
    virtual ~AudioStream() = default;
    // load(path) as AudioStream: a `.wav` becomes an AudioStreamWAV, an `.ogg` an
    // AudioStreamOggVorbis; null when the file is missing or unreadable (read with gd::read_file).
    static gd::Ref<AudioStream> load(const std::string& path);
};
class AudioStreamWAV : public AudioStream {
public:
    PcmRef pcm;
};
class AudioStreamOggVorbis : public AudioStream {
public:
    gd::PBytes data;    // the whole file (compressed); decoded in chunks while it plays
    bool loop = false;
};

// AudioStreamPlayer: one mixer voice, or the mixer's music stream for an Ogg Vorbis stream.
// `volume_db` written while playing takes effect through `set_volume_db` (Godot's property setter).
class AudioStreamPlayer {
public:
    gd::Ref<AudioStream> stream;
    std::string bus = "Master";
    float volume_db = 0.0f;
    Mixer* mixer = nullptr;
    int voice = 0;              // the mixer voice a WAV plays on

    void set_volume_db(float db);
    void play();
    void stop();
    bool is_playing() const;    // Godot's `playing`

private:
    uint64_t _id = 0;           // the voice play
    bool _music = false;        // playing the music stream
};

}  // namespace t3

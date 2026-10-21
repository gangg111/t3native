#include "audio/mixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "gd/io.h"
#include "platform/log.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"

namespace t3 {

namespace {

uint32_t u32(const gd::PBytes& b, int64_t at) {
    const uint8_t* p = b.data() + at;
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t u16(const gd::PBytes& b, int64_t at) {
    const uint8_t* p = b.data() + at;
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

// Godot's Math::db_to_linear / linear_to_db (exp(db * ln(10) / 20), ln(x) * 20 / ln(10)).
float Mixer::db_to_linear(float db) { return std::exp(db * 0.11512925464970228420089957273422f); }
float Mixer::linear_to_db(float linear) { return std::log(linear) * 8.6858896380650365530225783783321f; }

int Mixer::bus_index(const std::string& name) {
    if (name == "Music") return MUSIC;
    if (name == "Voices") return VOICES;
    if (name == "System") return SYSTEM;
    return SFX;
}

PcmRef Pcm::from_wav(const gd::PBytes& b) {
    int64_t size = b.size();
    if (size < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return nullptr;
    auto pcm = std::make_shared<Pcm>();
    int bits = 0;
    bool have_fmt = false, have_data = false;
    int64_t at = 12;
    while (at + 8 <= size) {
        int64_t len = u32(b, at + 4);
        int64_t body = at + 8;
        if (body + len > size) len = size - body;
        if (std::memcmp(b.data() + at, "fmt ", 4) == 0 && len >= 16) {
            if (u16(b, body) != 1) return nullptr;   // PCM only
            pcm->channels = u16(b, body + 2);
            pcm->rate = static_cast<int>(u32(b, body + 4));
            bits = u16(b, body + 14);
            have_fmt = true;
        } else if (std::memcmp(b.data() + at, "data", 4) == 0) {
            pcm->samples.resize(static_cast<size_t>(len / 2));
            std::memcpy(pcm->samples.data(), b.data() + body, pcm->samples.size() * 2);   // little-endian hosts
            have_data = true;
        } else if (std::memcmp(b.data() + at, "smpl", 4) == 0 && len >= 36 + 24 && u32(b, body + 28) > 0) {
            // The first loop: cue id, type (0 forward), start, end (the last sample, inclusive).
            pcm->loop_begin = u32(b, body + 36 + 8);
            pcm->loop_end = static_cast<int64_t>(u32(b, body + 36 + 12)) + 1;
        }
        at = body + len + (len & 1);
    }
    if (!have_fmt || !have_data || bits != 16 || pcm->channels < 1 || pcm->channels > 2 || pcm->rate <= 0) return nullptr;
    pcm->samples.resize(static_cast<size_t>(pcm->frames() * pcm->channels));
    if (pcm->loop_end > pcm->frames()) pcm->loop_end = pcm->frames();
    if (pcm->loop_end >= 0 && pcm->loop_end <= pcm->loop_begin) pcm->loop_end = -1;
    return pcm;
}

Mixer::Mixer()
    : _music_ring(static_cast<size_t>(MUSIC_RING * 2), 0.0f), _stream_ring(static_cast<size_t>(STREAM_RING * 2), 0.0f) {
    _bus_gain.fill(1.0f);
    for (auto& f : _finished) f.store(0);
    _music_thread = std::thread([this] { _music_worker(); });
}

Mixer::~Mixer() {
    {
        std::lock_guard<std::mutex> lock(_music_mutex);
        _music_quit = true;
    }
    _music_wake.notify_all();
    if (_music_thread.joinable()) _music_thread.join();
    _music_close();
}

// Keeps the music ring filled while a stream plays (MUSIC_DECODE_STEP frames per wake-up).
void Mixer::_music_worker() {
    std::unique_lock<std::mutex> lock(_music_mutex);
    while (!_music_quit) {
        if (_music_on) _music_decode(MUSIC_DECODE_STEP);
        _music_wake.wait_for(lock, std::chrono::milliseconds(20));
    }
}

bool Mixer::_push(Command c) {
    uint64_t head = _head.load(std::memory_order_relaxed);
    if (head - _tail.load(std::memory_order_acquire) >= COMMANDS) {
        if (dropped_commands++ == 0) Log::warning("mixer: the audio thread is not draining its commands");
        return false;
    }
    c.seq = ++_seq;
    _commands[head % COMMANDS] = c;
    _head.store(head + 1, std::memory_order_release);
    return true;
}

uint64_t Mixer::play(int voice, const PcmRef& pcm, int bus, float volume_db) {
    if (voice < 0 || voice >= MAX_VOICES) return 0;
    Command c;
    c.kind = PLAY;
    c.index = voice;
    c.bus = bus;
    c.gain = db_to_linear(volume_db);
    c.pcm = pcm.get();
    c.id = ++_next_id;
    if (!_push(c)) return 0;
    if (_held[voice]) _retired.emplace_back(_seq, _held[voice]);
    _held[voice] = pcm;
    _voice_id[voice] = c.id;
    return c.id;
}

void Mixer::set_voice_volume_db(int voice, float volume_db) {
    if (voice < 0 || voice >= MAX_VOICES || !_held[voice]) return;
    Command c;
    c.kind = VOICE_VOLUME;
    c.index = voice;
    c.gain = db_to_linear(volume_db);
    _push(c);
}

void Mixer::stop(int voice) {
    if (voice < 0 || voice >= MAX_VOICES || !_held[voice]) return;   // nothing the audio thread holds
    Command c;
    c.kind = STOP;
    c.index = voice;
    if (!_push(c)) return;
    _retired.emplace_back(_seq, _held[voice]);
    _held[voice] = nullptr;
    _voice_id[voice] = 0;
}

bool Mixer::voice_playing(int voice, uint64_t id) const {
    if (voice < 0 || voice >= MAX_VOICES || id == 0) return false;
    return _voice_id[voice] == id && _finished[voice].load(std::memory_order_acquire) != id;
}

void Mixer::_send_bus(int bus) {
    Command c;
    c.kind = BUS_VOLUME;
    c.index = bus;
    c.gain = _bus_mute[bus] ? 0.0f : db_to_linear(_bus_db[bus]);
    _push(c);
}

void Mixer::set_bus_volume_db(int bus, float volume_db) {
    if (bus < 0 || bus >= BUS_COUNT) return;
    _bus_db[bus] = volume_db;
    _send_bus(bus);
}

void Mixer::set_bus_mute(int bus, bool mute) {
    if (bus < 0 || bus >= BUS_COUNT) return;
    _bus_mute[bus] = mute;
    _send_bus(bus);
}

void Mixer::set_mono(bool on) {
    _mono = on;
    Command c;
    c.kind = MONO;
    c.index = on ? 1 : 0;
    _push(c);
}

void Mixer::_music_close() {
    if (_vorbis) stb_vorbis_close(_vorbis);
    _vorbis = nullptr;
}

bool Mixer::music_play(const gd::PBytes& ogg, bool loop, int bus, float volume_db) {
    music_stop();
    std::lock_guard<std::mutex> lock(_music_mutex);
    int error = 0;
    _music_data = ogg;
    _vorbis = stb_vorbis_open_memory(_music_data.data(), static_cast<int>(_music_data.size()), &error, nullptr);
    if (!_vorbis) {
        Log::warning("mixer: an Ogg Vorbis stream does not decode (stb_vorbis error " + std::to_string(error) + ")");
        return false;
    }
    stb_vorbis_info info = stb_vorbis_get_info(_vorbis);
    _music_channels = info.channels;
    _music_loop = loop;
    _music_eof = -1;
    _music_db = volume_db;
    _music_bus = bus;
    int64_t start = _music_write.load(std::memory_order_relaxed);
    // The stream before this one ends where this one starts, even before the audio thread sees
    // the start.
    _music_fence.store(start, std::memory_order_release);
    Command c;
    c.kind = MUSIC_START;
    c.bus = bus;
    c.gain = db_to_linear(volume_db);
    c.start = start;
    c.rate = static_cast<int>(info.sample_rate);
    if (!_push(c)) {
        _music_close();
        return false;
    }
    _music_on = true;
    _music_decode(MUSIC_DECODE_STEP);
    return true;
}

void Mixer::music_set_volume_db(float volume_db) {
    std::lock_guard<std::mutex> lock(_music_mutex);
    _music_db = volume_db;
    if (!_music_on) return;
    Command c;
    c.kind = MUSIC_VOLUME;
    c.gain = db_to_linear(volume_db);
    _push(c);
}

void Mixer::music_stop() {
    std::lock_guard<std::mutex> lock(_music_mutex);
    if (!_music_on) return;
    _music_on = false;
    _music_close();
    _music_fence.store(_music_write.load(std::memory_order_relaxed), std::memory_order_release);
    Command c;
    c.kind = MUSIC_STOP;
    _push(c);
}

bool Mixer::music_playing() const {
    std::lock_guard<std::mutex> lock(_music_mutex);
    if (!_music_on) return false;
    return _music_eof < 0 || _music_read.load(std::memory_order_acquire) + 1 < _music_eof;
}

// Decodes up to `frames` frames into the ring, as far as it has room; a looping stream starts
// again at its end, another one ends there.
void Mixer::_music_decode(int64_t frames) {
    if (!_vorbis || _music_eof >= 0) return;
    int64_t write = _music_write.load(std::memory_order_relaxed);
    int64_t room = MUSIC_RING - (write - _music_read.load(std::memory_order_acquire));
    int64_t want = std::min(frames, room);
    float chunk[2048];
    while (want > 0) {
        int ch = _music_channels;
        int n = static_cast<int>(std::min<int64_t>(want, 2048 / 2));
        int got = stb_vorbis_get_samples_float_interleaved(_vorbis, ch >= 2 ? 2 : 1, chunk, n * (ch >= 2 ? 2 : 1));
        if (got <= 0) {
            if (_music_loop && stb_vorbis_seek_start(_vorbis)) {
                if (stb_vorbis_get_samples_float_interleaved(_vorbis, 1, chunk, 1) <= 0) break;   // empty stream
                stb_vorbis_seek_start(_vorbis);
                continue;
            }
            _music_eof = write;
            break;
        }
        for (int i = 0; i < got; ++i) {
            size_t slot = static_cast<size_t>((write + i) % MUSIC_RING) * 2;
            float l = ch >= 2 ? chunk[i * 2] : chunk[i];
            float r = ch >= 2 ? chunk[i * 2 + 1] : chunk[i];
            _music_ring[slot] = l;
            _music_ring[slot + 1] = r;
        }
        write += got;
        want -= got;
        _music_write.store(write, std::memory_order_release);
    }
}

bool Mixer::stream_start(int rate, int bus, float volume_db) {
    stream_stop();
    if (rate <= 0) return false;
    int64_t start = _stream_write.load(std::memory_order_relaxed);
    // As music_play: the stream before this one ends where this one starts.
    _stream_fence.store(start, std::memory_order_release);
    Command c;
    c.kind = STREAM_START;
    c.bus = bus;
    c.gain = db_to_linear(volume_db);
    c.start = start;
    c.rate = rate;
    if (!_push(c)) return false;
    _stream_on = true;
    _stream_start = start;
    return true;
}

int64_t Mixer::stream_write(const float* stereo, int64_t frames) {
    if (!_stream_on) return 0;
    int64_t write = _stream_write.load(std::memory_order_relaxed);
    // Before the audio thread applied the start, `read` is older than `_stream_start`: less room.
    int64_t read = std::max(_stream_read.load(std::memory_order_acquire), _stream_start);
    int64_t room = STREAM_RING - (write - std::min(read, write));
    int64_t n = std::min(frames, room);
    for (int64_t i = 0; i < n; ++i) {
        size_t slot = static_cast<size_t>((write + i) % STREAM_RING) * 2;
        _stream_ring[slot] = stereo[i * 2];
        _stream_ring[slot + 1] = stereo[i * 2 + 1];
    }
    if (n > 0) _stream_write.store(write + n, std::memory_order_release);
    return n;
}

int64_t Mixer::stream_queued() const {
    if (!_stream_on) return 0;
    int64_t write = _stream_write.load(std::memory_order_relaxed);
    int64_t read = std::max(_stream_read.load(std::memory_order_acquire), _stream_start);
    return std::max<int64_t>(0, write - read);
}

void Mixer::stream_stop() {
    if (!_stream_on) return;
    _stream_on = false;
    _stream_fence.store(_stream_write.load(std::memory_order_relaxed), std::memory_order_release);
    Command c;
    c.kind = STREAM_STOP;
    _push(c);
}

void Mixer::update() {
    {
        // A step tops the ring up too, so a recording (the host's --audio-out pulls the mix on the
        // game thread) does not depend on when the worker woke.
        std::lock_guard<std::mutex> lock(_music_mutex);
        if (_music_on) _music_decode(MUSIC_DECODE_STEP);
    }
    // Voices that ended by themselves: the audio thread dropped their tone before saying so.
    uint64_t applied = _applied.load(std::memory_order_acquire);
    for (int v = 0; v < MAX_VOICES; ++v) {
        if (_held[v] && _voice_id[v] != 0 && _finished[v].load(std::memory_order_acquire) == _voice_id[v]) {
            _held[v] = nullptr;
            _voice_id[v] = 0;
        }
    }
    _retired.erase(std::remove_if(_retired.begin(), _retired.end(),
                                  [applied](const std::pair<uint64_t, PcmRef>& r) { return r.first <= applied; }),
                   _retired.end());
}

void Mixer::mix(float* out, int frames, int rate) {
    // The commands queued since the last buffer.
    uint64_t tail = _tail.load(std::memory_order_relaxed);
    uint64_t head = _head.load(std::memory_order_acquire);
    uint64_t applied = 0;
    while (tail != head) {
        const Command& c = _commands[tail % COMMANDS];
        switch (c.kind) {
            case PLAY: {
                Voice& v = _voices[c.index];
                if (v.pcm) _finished[c.index].store(v.id, std::memory_order_release);
                v.pcm = c.pcm;
                v.id = c.id;
                v.bus = c.bus;
                v.pos = 0;
                v.gain = v.target = c.gain;
                if (!v.pcm || v.pcm->frames() == 0) {
                    v.pcm = nullptr;
                    _finished[c.index].store(c.id, std::memory_order_release);
                }
                break;
            }
            case STOP: {
                Voice& v = _voices[c.index];
                if (v.pcm) _finished[c.index].store(v.id, std::memory_order_release);
                v.pcm = nullptr;
                break;
            }
            case VOICE_VOLUME: _voices[c.index].target = c.gain; break;
            case BUS_VOLUME: _bus_gain[c.index] = c.gain; break;
            case MONO: _mix_mono = c.index != 0; break;
            case MUSIC_START:
                _m_on = true;
                _m_start = c.start;
                _m_rate = c.rate;
                _m_bus = c.bus;
                _m_frac = 0;
                _m_gain = _m_target = c.gain;
                _music_read.store(c.start, std::memory_order_release);
                break;
            case MUSIC_VOLUME: _m_target = c.gain; break;
            case MUSIC_STOP: _m_on = false; break;
            case STREAM_START:
                _s_on = true;
                _s_start = c.start;
                _s_rate = c.rate;
                _s_bus = c.bus;
                _s_frac = 0;
                _s_gain = c.gain;
                _stream_read.store(c.start, std::memory_order_release);
                break;
            case STREAM_STOP: _s_on = false; break;
            default: break;
        }
        applied = c.seq;
        ++tail;
    }
    _tail.store(tail, std::memory_order_release);
    if (applied) _applied.store(applied, std::memory_order_release);

    for (int i = 0; i < frames * 2; ++i) out[i] = 0.0f;
    if (rate <= 0 || frames <= 0) return;
    float ramp = 1.0f / static_cast<float>(frames);

    for (int vi = 0; vi < MAX_VOICES; ++vi) {
        Voice& v = _voices[vi];
        if (!v.pcm) continue;
        const Pcm& p = *v.pcm;
        const int16_t* s = p.samples.data();
        int64_t n = p.frames();
        bool loop = p.loop_end >= 0;
        double step = static_cast<double>(p.rate) / rate;
        float bus = _bus_gain[v.bus];
        float g0 = v.gain, dg = (v.target - v.gain) * ramp;
        bool ended = false;
        for (int f = 0; f < frames; ++f) {
            int64_t i = static_cast<int64_t>(v.pos);
            float frac = static_cast<float>(v.pos - static_cast<double>(i));
            int64_t j = i + 1;
            if (loop && j >= p.loop_end) j = p.loop_begin;
            if (j >= n) j = i;
            float g = (g0 + dg * static_cast<float>(f)) * bus * (1.0f / 32768.0f);
            if (p.channels == 2) {
                float l = s[i * 2] + (s[j * 2] - s[i * 2]) * frac;
                float r = s[i * 2 + 1] + (s[j * 2 + 1] - s[i * 2 + 1]) * frac;
                out[f * 2] += l * g;
                out[f * 2 + 1] += r * g;
            } else {
                float m = (s[i] + (s[j] - s[i]) * frac) * g;
                out[f * 2] += m;
                out[f * 2 + 1] += m;
            }
            v.pos += step;
            if (loop && v.pos >= static_cast<double>(p.loop_end)) {
                v.pos -= static_cast<double>(p.loop_end - p.loop_begin);
            } else if (v.pos >= static_cast<double>(n)) {
                ended = true;
                break;
            }
        }
        v.gain = v.target;
        if (ended) {
            v.pcm = nullptr;
            _finished[vi].store(v.id, std::memory_order_release);
        }
    }

    if (_m_on && _m_rate > 0) {
        int64_t read = _music_read.load(std::memory_order_relaxed);
        int64_t limit = _music_write.load(std::memory_order_acquire);
        int64_t fence = _music_fence.load(std::memory_order_acquire);
        if (fence > _m_start) limit = std::min(limit, fence);
        double step = static_cast<double>(_m_rate) / rate;
        float bus = _bus_gain[_m_bus];
        float g0 = _m_gain, dg = (_m_target - _m_gain) * ramp;
        const float* ring = _music_ring.data();
        for (int f = 0; f < frames; ++f) {
            if (read + 1 >= limit) break;   // underrun, or the end of the stream
            size_t a = static_cast<size_t>(read % MUSIC_RING) * 2;
            size_t b = static_cast<size_t>((read + 1) % MUSIC_RING) * 2;
            float frac = static_cast<float>(_m_frac);
            float g = (g0 + dg * static_cast<float>(f)) * bus;
            out[f * 2] += (ring[a] + (ring[b] - ring[a]) * frac) * g;
            out[f * 2 + 1] += (ring[a + 1] + (ring[b + 1] - ring[a + 1]) * frac) * g;
            _m_frac += step;
            while (_m_frac >= 1.0) {
                _m_frac -= 1.0;
                ++read;
            }
        }
        _m_gain = _m_target;
        _music_read.store(read, std::memory_order_release);
    }

    // The streamed source, read like the music ring (linear interpolation, stops at what was written).
    if (_s_on && _s_rate > 0) {
        int64_t read = _stream_read.load(std::memory_order_relaxed);
        int64_t limit = _stream_write.load(std::memory_order_acquire);
        int64_t fence = _stream_fence.load(std::memory_order_acquire);
        if (fence > _s_start) limit = std::min(limit, fence);
        double step = static_cast<double>(_s_rate) / rate;
        float g = _s_gain * _bus_gain[_s_bus];
        const float* ring = _stream_ring.data();
        for (int f = 0; f < frames; ++f) {
            if (read + 1 >= limit) break;   // underrun, or the end of the stream
            size_t a = static_cast<size_t>(read % STREAM_RING) * 2;
            size_t b = static_cast<size_t>((read + 1) % STREAM_RING) * 2;
            float frac = static_cast<float>(_s_frac);
            out[f * 2] += (ring[a] + (ring[b] - ring[a]) * frac) * g;
            out[f * 2 + 1] += (ring[a + 1] + (ring[b + 1] - ring[a + 1]) * frac) * g;
            _s_frac += step;
            while (_s_frac >= 1.0) {
                _s_frac -= 1.0;
                ++read;
            }
        }
        _stream_read.store(read, std::memory_order_release);
    }

    for (int f = 0; f < frames; ++f) {
        float l = out[f * 2], r = out[f * 2 + 1];
        if (_mix_mono) {
            float center = (l + r) / 2.0f;   // AudioEffectStereoEnhance, pan_pullout 0
            l = r = center;
        }
        out[f * 2] = std::clamp(l, -1.0f, 1.0f);
        out[f * 2 + 1] = std::clamp(r, -1.0f, 1.0f);
    }
}

gd::Ref<AudioStream> AudioStream::load(const std::string& path) {
    gd::PBytes bytes = gd::read_file(path);
    if (bytes.size() == 0) {
        Log::error("audio: cannot read " + path);
        return nullptr;
    }
    if (gd::ends_with(path, ".ogg")) {
        auto ogg = gd::make<AudioStreamOggVorbis>();
        ogg->data = bytes;
        return ogg;
    }
    PcmRef pcm = Pcm::from_wav(bytes);
    if (!pcm) {
        Log::error("audio: not a 16-bit PCM WAV: " + path);
        return nullptr;
    }
    auto wav = gd::make<AudioStreamWAV>();
    wav->pcm = pcm;
    return wav;
}

void AudioStreamPlayer::set_volume_db(float db) {
    volume_db = db;
    if (!mixer) return;
    if (_music) mixer->music_set_volume_db(db);
    else if (_id != 0) mixer->set_voice_volume_db(voice, db);
}

void AudioStreamPlayer::play() {
    stop();
    if (!mixer || !stream) return;
    if (auto ogg = std::dynamic_pointer_cast<AudioStreamOggVorbis>(stream)) {
        _music = mixer->music_play(ogg->data, ogg->loop, Mixer::bus_index(bus), volume_db);
    } else if (auto wav = std::dynamic_pointer_cast<AudioStreamWAV>(stream)) {
        _id = mixer->play(voice, wav->pcm, Mixer::bus_index(bus), volume_db);
    }
}

void AudioStreamPlayer::stop() {
    if (!mixer) return;
    if (_music) mixer->music_stop();
    else if (_id != 0) mixer->stop(voice);
    _music = false;
    _id = 0;
}

bool AudioStreamPlayer::is_playing() const {
    if (!mixer) return false;
    if (_music) return mixer->music_playing();
    return mixer->voice_playing(voice, _id);
}

}  // namespace t3

// The native mixer (audio/mixer.h): resampling, volume in dB, WAV loops, the mono downmix and the
// music stream decoded in chunks. Expectations are computed here from the definitions (linear
// interpolation, 10^(dB / 20)), not with the mixer's own helpers.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "audio/mixer.h"
#include "gd/io.h"
#include "support/test_suite.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"

using namespace t3;

namespace {

void put16(std::vector<uint8_t>& b, int v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A 16-bit WAV as the converter writes it (media.py wav_bytes): `loop` (start, end exclusive)
// goes into `smpl` with the end as the last sample.
gd::PBytes wav(const std::vector<int16_t>& samples, int channels, int rate, int loop_start = -1, int loop_end = -1) {
    std::vector<uint8_t> b;
    tag(b, "RIFF");
    put32(b, 0);
    tag(b, "WAVE");
    tag(b, "fmt ");
    put32(b, 16);
    put16(b, 1);
    put16(b, channels);
    put32(b, static_cast<uint32_t>(rate));
    put32(b, static_cast<uint32_t>(rate * channels * 2));
    put16(b, channels * 2);
    put16(b, 16);
    tag(b, "data");
    put32(b, static_cast<uint32_t>(samples.size() * 2));
    for (int16_t s : samples) put16(b, s);
    if (loop_start >= 0) {
        tag(b, "smpl");
        put32(b, 60);
        uint32_t head[9] = {0, 0, 0, 60, 0, 0, 0, 1, 0};
        for (uint32_t v : head) put32(b, v);
        uint32_t lp[6] = {0, 0, static_cast<uint32_t>(loop_start), static_cast<uint32_t>(loop_end - 1), 0, 0};
        for (uint32_t v : lp) put32(b, v);
    }
    uint32_t riff = static_cast<uint32_t>(b.size() - 8);
    std::memcpy(b.data() + 4, &riff, 4);
    gd::PBytes out;
    out.resize(static_cast<gd::Int>(b.size()));
    std::memcpy(out.data(), b.data(), b.size());
    return out;
}

bool near(float a, double b, double tolerance = 1e-6) { return std::abs(static_cast<double>(a) - b) <= tolerance; }

}  // namespace

// A ramp at 24 kHz mixed at 48 kHz: every second output frame falls between two samples, which
// linear interpolation halves; -6.0206 dB is a gain of one half on both sides of a mono tone.
T3_TEST(mixer, half_rate_resampled_at_a_volume_in_db) {
    std::vector<int16_t> ramp;
    for (int i = 0; i < 100; ++i) ramp.push_back(static_cast<int16_t>(i * 300));
    PcmRef pcm = Pcm::from_wav(wav(ramp, 1, 24000));
    t.expect(pcm != nullptr && pcm->frames() == 100 && pcm->rate == 24000, "the WAV reads");
    if (!pcm) return;
    Mixer mixer;
    float db = -6.0206f;
    uint64_t id = mixer.play(0, pcm, Mixer::SFX, db);
    std::vector<float> out(128 * 2);
    mixer.mix(out.data(), 128, 48000);
    double gain = std::pow(10.0, db / 20.0);
    bool ok = true;
    for (int f = 0; f < 128; ++f) {
        double expected = 300.0 * (f * 0.5) / 32768.0 * gain;
        ok = ok && near(out[f * 2], expected) && near(out[f * 2 + 1], expected);
    }
    t.expect(ok, "out[f] = ramp(f / 2) * 10^(dB / 20), on both sides");
    t.expect(mixer.voice_playing(0, id), "still playing (150 of 200 output frames)");
    mixer.mix(out.data(), 128, 48000);
    mixer.update();
    t.expect(!mixer.voice_playing(0, id), "ended after its last sample");
}

// The smpl loop: start 1, end 4 (exclusive): 0 1 2 3 1 2 3 1 ...; a bus at -inf (muted) is silent.
T3_TEST(mixer, wav_loop_points_and_bus_mute) {
    PcmRef pcm = Pcm::from_wav(wav({1000, 2000, 3000, 4000, 9999}, 1, 48000, 1, 4));
    t.expect(pcm != nullptr && pcm->loop_begin == 1 && pcm->loop_end == 4, "the loop from smpl (end inclusive there)");
    if (!pcm) return;
    Mixer mixer;
    mixer.play(3, pcm, Mixer::VOICES, 0.0f);
    std::vector<float> out(10 * 2);
    mixer.mix(out.data(), 10, 48000);
    const int expected[10] = {1000, 2000, 3000, 4000, 2000, 3000, 4000, 2000, 3000, 4000};
    bool ok = true;
    for (int f = 0; f < 10; ++f) ok = ok && near(out[f * 2], expected[f] / 32768.0);
    t.expect(ok, "the loop repeats samples 1 to 3 and never reaches 9999");
    mixer.set_bus_mute(Mixer::VOICES, true);
    mixer.mix(out.data(), 10, 48000);
    t.expect(out[0] == 0.0f && out[19] == 0.0f, "a muted bus is silent");
}

// AudioEffectStereoEnhance with pan_pullout 0: both sides become (l + r) / 2.
T3_TEST(mixer, mono_folds_both_sides) {
    PcmRef pcm = Pcm::from_wav(wav({8000, 0, 8000, 0, 8000, 0}, 2, 48000));
    if (!t.expect(pcm != nullptr && pcm->channels == 2, "a stereo WAV")) return;
    Mixer mixer;
    mixer.play(0, pcm, Mixer::SFX, 0.0f);
    mixer.set_mono(true);
    std::vector<float> out(2 * 2);
    mixer.mix(out.data(), 2, 48000);
    t.expect(near(out[0], 4000 / 32768.0) && near(out[1], 4000 / 32768.0), "left only becomes half on both sides");
}

// The music stream decoded in chunks into the ring, mixed at its own rate, equals a whole decode.
T3_TEST(mixer, music_streams_in_chunks) {
    std::string path = t3test::imported("music/cdda_02.ogg");
    if (!t.require(path, "no music group")) return;
    gd::PBytes ogg = gd::read_file(path);
    int channels = 0, rate = 0;
    short* whole = nullptr;
    int frames = stb_vorbis_decode_memory(ogg.data(), static_cast<int>(ogg.size()), &channels, &rate, &whole);
    if (!t.expect(frames > 40000 && channels == 2, "the reference decode")) return;
    Mixer mixer;
    t.expect(mixer.music_play(ogg, false, Mixer::MUSIC, 0.0f), "the stream opens");
    std::vector<float> out(1000 * 2);
    bool ok = true;
    for (int block = 0; block < 40; ++block) {
        mixer.mix(out.data(), 1000, rate);
        mixer.update();
        for (int f = 0; f < 1000 && ok; ++f) {
            int at = (block * 1000 + f) * 2;
            ok = near(out[f * 2], whole[at] / 32768.0, 2.0 / 32768.0) && near(out[f * 2 + 1], whole[at + 1] / 32768.0, 2.0 / 32768.0);
        }
    }
    free(whole);
    t.expect(ok, "40 000 frames as the whole decode (16-bit rounding)");
    t.expect(mixer.music_playing(), "still playing");
    mixer.music_stop();
    mixer.mix(out.data(), 1000, rate);
    t.expect(out[0] == 0.0f && !mixer.music_playing(), "stopped");
}

// db_to_linear / linear_to_db as Godot's (and as 10^(dB / 20)).
T3_TEST(mixer, decibels) {
    t.expect(near(Mixer::db_to_linear(-20.0f), 0.1, 1e-6), "-20 dB is a tenth");
    t.expect(near(Mixer::linear_to_db(0.0001f), -80.0, 1e-4), "the director's floor 0.0001 is -80 dB");
    t.expect(near(Mixer::db_to_linear(0.0f), 1.0), "0 dB is unity");
}

// The mixer's streamed PCM source (Mixer::stream_*, the movie player's sound): frames written by the
// game thread come out of `mix` at the stream's rate, with the gain 10^(dB / 20), stop where the
// writes stop, and the ring takes no more than STREAM_RING frames ahead. Expectations are computed
// here from the definitions, not with the mixer's helpers.
#include <cmath>
#include <vector>

#include "audio/mixer.h"
#include "support/test_suite.h"

using namespace t3;

namespace {
bool near(float a, double b) { return std::abs(static_cast<double>(a) - b) <= 1e-6; }
}  // namespace

T3_TEST(mixer_stream, written_frames_play_at_their_gain_and_stop_at_the_end) {
    Mixer mixer;
    t.expect(mixer.stream_start(48000, Mixer::MUSIC, -6.0206f), "the stream starts");
    std::vector<float> in;
    for (int i = 0; i < 100; ++i) {
        in.push_back(0.001f * static_cast<float>(i));    // left: a ramp
        in.push_back(-0.002f * static_cast<float>(i));   // right: another
    }
    t.expect_equal(mixer.stream_write(in.data(), 100), int64_t(100), "all 100 frames fit");
    t.expect_equal(mixer.stream_queued(), int64_t(100), "100 frames queued");
    std::vector<float> out(128 * 2);
    mixer.mix(out.data(), 128, 48000);
    double gain = std::pow(10.0, -6.0206 / 20.0);
    bool ok = true;
    // Same rate: frame f is input frame f; the last one waits for its successor (interpolation),
    // so 99 frames come out and the rest is silence.
    for (int f = 0; f < 128; ++f) {
        double l = f < 99 ? 0.001 * f * gain : 0.0;
        double r = f < 99 ? -0.002 * f * gain : 0.0;
        ok = ok && near(out[f * 2], l) && near(out[f * 2 + 1], r);
    }
    t.expect(ok, "out[f] = in[f] * 10^(dB / 20) for the written frames, then silence");
    t.expect_equal(mixer.stream_queued(), int64_t(1), "one frame left");
    mixer.stream_stop();
    t.expect_equal(mixer.stream_queued(), int64_t(0), "a stopped stream has nothing queued");
}

T3_TEST(mixer_stream, the_ring_takes_at_most_its_size) {
    Mixer mixer;
    t.expect(mixer.stream_start(44100, Mixer::MUSIC, 0.0f), "the stream starts");
    std::vector<float> in(static_cast<size_t>(Mixer::STREAM_RING + 500) * 2, 0.25f);
    t.expect_equal(mixer.stream_write(in.data(), Mixer::STREAM_RING + 500), Mixer::STREAM_RING,
                   "STREAM_RING frames taken, the rest refused");
    t.expect_equal(mixer.stream_write(in.data(), 10), int64_t(0), "a full ring takes nothing");
    std::vector<float> out(64 * 2);
    mixer.mix(out.data(), 64, 44100);
    t.expect(near(out[0], 0.25) && near(out[127], 0.25), "the frames play at 0 dB");
    t.expect_equal(mixer.stream_write(in.data(), 100), int64_t(64), "the 64 frames played made room for 64");
}

// The music keeps playing while the game thread is held up (no Mixer::update for longer than the
// ring lasts): the worker thread refills it. The expectation is by construction: a silent-free
// stretch of the real OGG's sound after a 2.5 s stall (the ring holds about 1.5 s at 44.1 kHz).
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "audio/mixer.h"
#include "gd/io.h"
#include "support/test_suite.h"

using namespace t3;

T3_TEST(mixer_music_worker, music_survives_a_stalled_game_thread) {
    std::string path = t3test::imported("music/xa_43.ogg");
    if (!t.require(path)) return;
    gd::PBytes ogg = gd::read_file(path);
    Mixer mixer;
    t.expect(mixer.music_play(ogg, true, Mixer::bus_index("Music"), 0.0f), "music starts");
    std::vector<float> out(2 * 1024);
    mixer.mix(out.data(), 1024, 48000);   // the audio thread takes the start
    // The game thread stalls for 2.5 s (no update); the audio thread keeps pulling in real time.
    double loudest_late = 0;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2500)) {
        mixer.mix(out.data(), 1024, 48000);
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(2000)) {
            for (float v : out) loudest_late = std::max(loudest_late, static_cast<double>(std::fabs(v)));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(21));   // 1024 frames at 48 kHz
    }
    // source: by construction: past 2 s without update the ring alone would be empty (silence)
    t.expect(loudest_late > 0.01, "music still sounds after 2 s without the game thread");
}

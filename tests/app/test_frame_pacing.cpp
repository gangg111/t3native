// GameApp::ticks_due: the 60 Hz steps each display refresh runs, with a jittery clock, at the
// refresh rates of phones (60, 90, 120, 144 Hz). Every refresh should run the steps of its slot in
// an even pattern (60: 1 each; 120: 1, 0; 90: 1, 1, 0; 144: never 2), never 0 then 2.
#include <cmath>
#include <random>
#include <vector>

#include "app/game_app.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

// Steps run per refresh for `frames` refreshes at `hz`, each time stamp off by up to ±jitter s.
std::vector<gd::Int> pattern(double hz, double jitter, int frames, unsigned seed, double phase_shift = 0.0) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> off(-jitter, jitter);
    std::vector<gd::Int> out;
    gd::Int done = 0;
    for (int k = 0; k < frames; ++k) {
        double elapsed = k / hz + (k == 0 ? 0.0 : off(rng)) + phase_shift;
        gd::Int due = GameApp::ticks_due(elapsed);
        out.push_back(due - done);
        done = due;
    }
    return out;
}

}  // namespace

T3_TEST(frame_pacing, even_steps_with_a_jittery_clock) {
    const double JITTER = 0.001;   // source: assumed: 1 ms of time stamp jitter (vsync callbacks)
    for (unsigned seed : {1u, 2u, 3u}) {
        std::vector<gd::Int> p60 = pattern(60, JITTER, 600, seed);
        bool even60 = true;
        for (size_t i = 1; i < p60.size(); ++i) even60 = even60 && p60[i] == 1;
        t.expect(even60, "60 Hz: one step every refresh");
        // source: by hand: 120 Hz refreshes are half a step apart: 1, 0, 1, 0, ...
        std::vector<gd::Int> p120 = pattern(120, JITTER, 1200, seed);
        bool even120 = true;
        for (size_t i = 1; i < p120.size(); ++i) even120 = even120 && p120[i] == (i % 2 == 0 ? 1 : 0);
        t.expect(even120, "120 Hz: one step every other refresh");
        for (double hz : {90.0, 144.0}) {
            std::vector<gd::Int> p = pattern(hz, JITTER, 1440, seed);
            bool fine = true;
            for (size_t i = 1; i < p.size(); ++i) fine = fine && p[i] <= 1;
            t.expect(fine, std::to_string(static_cast<int>(hz)) + " Hz: never two steps in one refresh");
        }
    }
}

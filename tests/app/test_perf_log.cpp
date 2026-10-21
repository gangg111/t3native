// Native: the performance log's window (app/perf_log.h): one line per 5 s from the frames' CPU
// times, intervals and GPU times.
#include <string>

#include "app/perf_log.h"
#include "support/test_suite.h"

using namespace t3;

T3_TEST(perf_log, statistics) {
    // source: by hand: sorted 1 2 3 4 -> median (2 + 3) / 2; nearest rank of 95 % of 20 is the 19th
    t.expect_equal(PerfLog::median({4, 1, 3, 2}), 2.5, "even count: the mean of the middle two");
    t.expect_equal(PerfLog::median({5, 1, 3}), 3.0, "odd count: the middle one");
    std::vector<double> twenty;
    for (int i = 1; i <= 20; ++i) twenty.push_back(i);
    t.expect_equal(PerfLog::percentile(twenty, 0.95), 19.0, "95th percentile of 1..20");
}

T3_TEST(perf_log, one_line_per_window) {
    PerfLog log;
    // 120 Hz for 5 s (601 frames, 600 intervals of 8.333 ms), one late frame of 25 ms in it.
    double at = 0.0;
    for (int i = 0; i <= 600; ++i) {
        log.frame(at, i == 300 ? 4.0 : 1.0, 2.0);
        if (i == 590) t.expect_equal(log.take(at, 8), std::string(), "nothing before 5 s");
        if (i < 600) at += i == 299 ? 0.025 : 1.0 / 120.0;
    }
    log.gpu_ms = {1.0, 2.0, 3.0};
    std::string line = log.take(at, 8);
    // source: by hand: 600 intervals over 5.0167 s = 119.6 fps; median 8.33 ms; the one 25 ms
    // interval > 1.5 x 8.33 is the jank; cpu mean (600 + 4) / 601 = 1.005, 95th 1.00, longest 4;
    // render 2 every frame; gpu mean 2, longest 3
    t.expect_equal(line, std::string("t=5s state=8 fps=119.6 frame=8.33/25.0ms jank=1 update=1.00/1.00/4.00ms render=2.00/2.00ms gpu=2.00/3.00ms"),
                   "the window's line");
    t.expect_equal(log.take(at + 1.0, 8), std::string(), "a new window begins");
}

T3_TEST(perf_log, spike_line) {
    // source: by hand: 200.1 + 2.9 = 203.0
    t.expect_equal(PerfLog::spike(12.34, 200.1, 2.9, 8, "fight view"),
                   std::string("spike t=12.3s 203.0ms update=200.1 render=2.9 state=8 fight view"), "a long frame's line");
    t.expect_equal(PerfLog::spike(1.0, 30.0, 0.0, 2, ""), std::string("spike t=1.0s 30.0ms update=30.0 render=0.0 state=2"),
                   "without a note");
}

T3_TEST(perf_log, update_parts) {
    PerfLog log;
    // Two frames: steps 1 + 2 then 3 ms, mixer 4 ms in the second only.
    log.frame(0.0, 3.0, 1.0);
    log.part("steps", 1.0);
    log.part("steps", 2.0);
    // source: by hand: a frame's parts summed, in name order
    t.expect_equal(log.frame_parts(), std::string(" steps=3.0"), "the first frame's parts");
    log.frame(5.0, 7.0, 1.0);
    log.part("steps", 3.0);
    log.part("mixer", 4.0);
    t.expect_equal(log.frame_parts(), std::string(" mixer=4.0 steps=3.0"), "the second frame's");
    std::string line = log.take(5.0, 8);
    // source: by hand: mean over the 2 frames: mixer 4 / 2 = 2.00, steps 6 / 2 = 3.00; longest 4 and 3
    const std::string tail = " mixer=2.00/4.00ms steps=3.00/3.00ms";
    t.expect(line.size() > tail.size() && line.substr(line.size() - tail.size()) == tail,
             "the window's line ends with the parts: " + line);
}

T3_TEST(perf_log, gpu_passes_and_scene) {
    PerfLog log;
    log.frame(0.0, 1.0, 1.0);
    log.frame(5.0, 1.0, 1.0);
    log.gpu_ms = {9.0};
    log.gpu_parts[PerfLog::GPU_SHADOW] = {1.0, 2.0};
    log.gpu_parts[PerfLog::GPU_SCENE] = {6.0};
    log.gpu_parts[PerfLog::GPU_2D] = {0.5, 1.5};
    std::string line = log.take(5.0, 8, "stage=5 arcade");
    // source: by hand: means shadow (1 + 2) / 2 = 1.50, scene 6.00, post none = 0.00, 2d (0.5 + 1.5) / 2 = 1.00
    const std::string tail = " shadow/scene/post/2d=1.50/6.00/0.00/1.00ms stage=5 arcade";
    t.expect(line.size() > tail.size() && line.compare(line.size() - tail.size(), tail.size(), tail) == 0, "passes and scene: " + line);
    log.frame(10.0, 1.0, 1.0);
    log.frame(15.0, 1.0, 1.0);
    std::string next = log.take(15.0, 8);
    t.expect(next.find("shadow/scene") == std::string::npos, "the passes are the window's only: " + next);
}

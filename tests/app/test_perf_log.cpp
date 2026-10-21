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
        log.frame(at, i == 300 ? 4.0 : 1.0);
        if (i == 590) t.expect_equal(log.take(at, 8), std::string(), "nothing before 5 s");
        if (i < 600) at += i == 299 ? 0.025 : 1.0 / 120.0;
    }
    log.gpu_ms = {1.0, 2.0, 3.0};
    std::string line = log.take(at, 8);
    // source: by hand: 600 intervals over 5.0167 s = 119.6 fps; median 8.33 ms; the one 25 ms
    // interval > 1.5 x 8.33 is the jank; cpu mean (600 + 4) / 601 = 1.005, 95th 1.00, longest 4;
    // gpu mean 2, longest 3
    t.expect_equal(line, std::string("t=5s state=8 fps=119.6 frame=8.33/25.0ms jank=1 cpu=1.00/1.00/4.00ms gpu=2.00/3.00ms"),
                   "the window's line");
    t.expect_equal(log.take(at + 1.0, 8), std::string(), "a new window begins");
}

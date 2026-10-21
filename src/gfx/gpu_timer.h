#pragma once
// Native: the GPU time of whole frames (GL_EXT_disjoint_timer_query's GL_TIME_ELAPSED_EXT) for the
// performance log. A ring of RING queries, read only once their results are there (the GPU runs a
// few frames behind; a full ring skips a frame rather than wait); a disjoint period (the GPU's clock
// changed) drops the results in flight. Made again after a context loss. On the development host,
// desktop GL 3.3's timer queries (without the disjoint flag).
#include <vector>

namespace t3::gfx {

class GpuTimer {
public:
    static constexpr int RING = 8;

    void begin();
    void end();
    // The GPU milliseconds of the frames finished since the last call, appended to `out`.
    void collect(std::vector<double>& out);
    bool available();

private:
    bool _ready();
    unsigned ids_[RING] = {};
    bool pending_[RING] = {};
    int next_ = 0;
    bool open_ = false;
    int state_ = 0;            // 0 not looked at, 1 timers, -1 none
    unsigned generation_ = 0;  // the GL context the queries belong to
    bool desktop_ = false;
};

}  // namespace t3::gfx

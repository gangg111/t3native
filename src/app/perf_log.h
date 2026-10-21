#pragma once
// Native: the performance log (SYSTEM OPTION > GAME > PERFORMANCE LOG). Every frame adds its game
// thread's time in two parts, `update` (the game's steps and the views they build) and `render`
// (the GL calls, with whatever the driver waits on in them: a frame's buffer), and its interval
// from the previous frame; the GPU timer adds the frames' GPU times as they come back. Every
// WINDOW seconds `take` sums the window up in one line (the platform appends the phone's
// temperature and writes it out):
//
//   t=125s state=8 fps=119.8 frame=8.33/16.9ms jank=0 update=0.90/1.40/2.10ms render=6.10/7.80ms gpu=0.90/1.40ms
//
// frame: the median and longest interval; jank: intervals over 1.5 x the median (the display's own
// rate, which an LTPO screen changes); update: mean / 95th percentile / longest; render, gpu:
// mean / longest ("gpu=-" without GPU timers). A frame over SPIKE_MS gets a line of its own at
// once (`spike`) with what the app did in it, when it knows.
#include <map>
#include <string>
#include <vector>

namespace t3 {

class PerfLog {
public:
    static constexpr double WINDOW = 5.0;          // seconds a line sums up
    static constexpr double JANK = 1.5;            // an interval this many medians long is a jank
    static constexpr double SPIKE_MS = 20.0;       // a frame this long is reported on its own

    std::vector<double> gpu_ms;                    // the window's GPU times (GpuTimer::collect)
    // The GPU per pass, from the frames timed in passes (every third frame: GPU timers do not nest):
    // the sun's shadow, the 3D scene with its sky, the resolve, glow and encode, the 2D screens.
    // The line adds "shadow/scene/post/2d=a/b/c/dms" (means).
    static constexpr int GPU_PARTS = 4;
    enum GpuPart { GPU_SHADOW, GPU_SCENE, GPU_POST, GPU_2D };
    std::vector<double> gpu_parts[GPU_PARTS];
    // The shadow pass's own parts, from every third frame: its clear and each cascade's casters.
    // The line adds "shadow clear/c0/c1/c2/c3=a/b/c/d/ems" (means).
    static constexpr int SHADOW_PARTS = 5;
    std::vector<double> shadow_parts[SHADOW_PARTS];

    // One frame at `now` (seconds) that took `update_ms` + `render_ms` on the game thread.
    void frame(double now, double update_ms, double render_ms);
    // A named part of this frame's update (steps, audio, mixer, movie): the window's line adds
    // "name=mean/longest" for each, the spike line the frame's own.
    void part(const std::string& name, double ms);
    // This frame's parts as " name=ms ..." (for a spike line), then forgotten for the next frame.
    std::string frame_parts();
    // The window's line once WINDOW seconds have passed since it began, else ""; a new window then.
    // `scene` (what is drawn, "stage=5 arcade") ends it.
    std::string take(double now, int state, const std::string& scene = "");
    // A long frame's own line: "spike t=12.3s 203.0ms update=200.1 render=2.9 state=8 fight view".
    static std::string spike(double now, double update_ms, double render_ms, int state, const std::string& note);
    // Forgets the window (the log turned off, the app paused): the next frame starts a new one.
    void reset();

    // Statistics (public for the tests).
    static double median(std::vector<double> v);
    static double percentile(std::vector<double> v, double share);

private:
    double start_ = -1.0;    // the window's first frame
    double last_ = -1.0;     // the previous frame
    std::vector<double> update_ms_, render_ms_, intervals_ms_;
    struct Part {
        double sum = 0.0, longest = 0.0;
    };
    std::map<std::string, Part> parts_;          // the window's
    std::map<std::string, double> frame_parts_;  // this frame's
};

}  // namespace t3

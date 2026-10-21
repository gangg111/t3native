#include "app/perf_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace t3 {

void PerfLog::frame(double now, double update_ms, double render_ms) {
    if (start_ < 0.0) start_ = now;
    if (last_ >= 0.0) intervals_ms_.push_back((now - last_) * 1000.0);
    last_ = now;
    update_ms_.push_back(update_ms);
    render_ms_.push_back(render_ms);
}

void PerfLog::part(const std::string& name, double ms) {
    frame_parts_[name] += ms;
}

std::string PerfLog::frame_parts() {
    std::string out;
    char item[64];
    for (const auto& [name, ms] : frame_parts_) {
        std::snprintf(item, sizeof item, " %s=%.1f", name.c_str(), ms);
        out += item;
        Part& p = parts_[name];
        p.sum += ms;
        p.longest = std::max(p.longest, ms);
    }
    frame_parts_.clear();
    return out;
}

std::string PerfLog::spike(double now, double update_ms, double render_ms, int state, const std::string& note) {
    char line[256];
    std::snprintf(line, sizeof line, "spike t=%.1fs %.1fms update=%.1f render=%.1f state=%d%s%s", now, update_ms + render_ms,
                  update_ms, render_ms, state, note.empty() ? "" : " ", note.c_str());
    return line;
}

double PerfLog::median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// The nearest-rank percentile: the smallest value with `share` of the values at or below it.
double PerfLog::percentile(std::vector<double> v, double share) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t rank = static_cast<size_t>(std::ceil(share * static_cast<double>(v.size())));
    return v[std::clamp<size_t>(rank, 1, v.size()) - 1];
}

std::string PerfLog::take(double now, int state, const std::string& scene) {
    if (start_ < 0.0 || now - start_ < WINDOW || update_ms_.empty()) return "";
    double seconds = now - start_;
    double fps = static_cast<double>(intervals_ms_.size()) / std::max(seconds, 1e-9);
    double mid = median(intervals_ms_);
    double longest = intervals_ms_.empty() ? 0.0 : *std::max_element(intervals_ms_.begin(), intervals_ms_.end());
    long jank = std::count_if(intervals_ms_.begin(), intervals_ms_.end(), [&](double ms) { return ms > JANK * mid; });
    double n = static_cast<double>(update_ms_.size());
    double update_mean = std::accumulate(update_ms_.begin(), update_ms_.end(), 0.0) / n;
    double update_max = *std::max_element(update_ms_.begin(), update_ms_.end());
    double render_mean = std::accumulate(render_ms_.begin(), render_ms_.end(), 0.0) / n;
    double render_max = *std::max_element(render_ms_.begin(), render_ms_.end());
    char gpu[48] = "gpu=-";
    if (!gpu_ms.empty()) {
        double gpu_mean = std::accumulate(gpu_ms.begin(), gpu_ms.end(), 0.0) / static_cast<double>(gpu_ms.size());
        std::snprintf(gpu, sizeof gpu, "gpu=%.2f/%.2fms", gpu_mean, *std::max_element(gpu_ms.begin(), gpu_ms.end()));
    }
    char line[256];
    std::snprintf(line, sizeof line, "t=%.0fs state=%d fps=%.1f frame=%.2f/%.1fms jank=%ld update=%.2f/%.2f/%.2fms render=%.2f/%.2fms %s",
                  now, state, fps, mid, longest, jank, update_mean, percentile(update_ms_, 0.95), update_max, render_mean,
                  render_max, gpu);
    std::string out = line;
    char item[64];
    for (const auto& [name, p] : parts_) {
        std::snprintf(item, sizeof item, " %s=%.2f/%.2fms", name.c_str(), p.sum / n, p.longest);
        out += item;
    }
    parts_.clear();
    bool passes = false;
    for (const std::vector<double>& v : gpu_parts) passes = passes || !v.empty();
    if (passes) {
        double m[GPU_PARTS];
        for (int i = 0; i < GPU_PARTS; ++i) {
            const std::vector<double>& v = gpu_parts[i];
            m[i] = v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
        }
        std::snprintf(item, sizeof item, " shadow/scene/post/2d=%.2f/%.2f/%.2f/%.2fms", m[0], m[1], m[2], m[3]);
        out += item;
    }
    if (!scene.empty()) out += " " + scene;
    for (std::vector<double>& v : gpu_parts) v.clear();
    // A new window from this frame on.
    start_ = now;
    update_ms_.clear();
    render_ms_.clear();
    intervals_ms_.clear();
    gpu_ms.clear();
    return out;
}

void PerfLog::reset() {
    parts_.clear();
    frame_parts_.clear();
    start_ = -1.0;
    last_ = -1.0;
    update_ms_.clear();
    render_ms_.clear();
    intervals_ms_.clear();
    gpu_ms.clear();
    for (std::vector<double>& v : gpu_parts) v.clear();
}

}  // namespace t3

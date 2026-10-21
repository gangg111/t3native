#include "app/perf_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace t3 {

void PerfLog::frame(double now, double cpu_ms) {
    if (start_ < 0.0) start_ = now;
    if (last_ >= 0.0) intervals_ms_.push_back((now - last_) * 1000.0);
    last_ = now;
    cpu_ms_.push_back(cpu_ms);
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

std::string PerfLog::take(double now, int state) {
    if (start_ < 0.0 || now - start_ < WINDOW || cpu_ms_.empty()) return "";
    double seconds = now - start_;
    double fps = static_cast<double>(intervals_ms_.size()) / std::max(seconds, 1e-9);
    double mid = median(intervals_ms_);
    double longest = intervals_ms_.empty() ? 0.0 : *std::max_element(intervals_ms_.begin(), intervals_ms_.end());
    long jank = std::count_if(intervals_ms_.begin(), intervals_ms_.end(), [&](double ms) { return ms > JANK * mid; });
    double cpu_mean = std::accumulate(cpu_ms_.begin(), cpu_ms_.end(), 0.0) / static_cast<double>(cpu_ms_.size());
    double cpu_max = *std::max_element(cpu_ms_.begin(), cpu_ms_.end());
    char gpu[48] = "gpu=-";
    if (!gpu_ms.empty()) {
        double gpu_mean = std::accumulate(gpu_ms.begin(), gpu_ms.end(), 0.0) / static_cast<double>(gpu_ms.size());
        std::snprintf(gpu, sizeof gpu, "gpu=%.2f/%.2fms", gpu_mean, *std::max_element(gpu_ms.begin(), gpu_ms.end()));
    }
    char line[256];
    std::snprintf(line, sizeof line, "t=%.0fs state=%d fps=%.1f frame=%.2f/%.1fms jank=%ld cpu=%.2f/%.2f/%.2fms %s", now,
                  state, fps, mid, longest, jank, cpu_mean, percentile(cpu_ms_, 0.95), cpu_max, gpu);
    // A new window from this frame on.
    start_ = now;
    cpu_ms_.clear();
    intervals_ms_.clear();
    gpu_ms.clear();
    return line;
}

void PerfLog::reset() {
    start_ = -1.0;
    last_ = -1.0;
    cpu_ms_.clear();
    intervals_ms_.clear();
    gpu_ms.clear();
}

}  // namespace t3

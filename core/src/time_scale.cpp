#include "mpp/time_scale.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace mpp {

std::int64_t TimeScale::frame_at(double x) const {
  return static_cast<std::int64_t>(std::floor(x / px_per_frame + scroll));
}

std::int64_t TimeScale::boundary_at(double x) const {
  return static_cast<std::int64_t>(std::llround(x / px_per_frame + scroll));
}

std::int64_t tick_step(Rate rate, double px_per_frame, double min_px) {
  const std::int64_t fps = std::max(1, rate.nominal());
  std::vector<std::int64_t> steps = {1, 2, 5, 10};
  if (fps > 20) steps.push_back(fps / 2);
  for (std::int64_t seconds : {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600}) {
    steps.push_back(seconds * fps);
  }
  for (std::int64_t step : steps) {
    if (step * px_per_frame >= min_px) return step;
  }
  return steps.back();
}

std::vector<std::int64_t> snap_points(const Project& project, std::int64_t edit_line,
                                      const std::vector<int>& ignore) {
  std::vector<std::int64_t> points = {0, edit_line};
  for (const Clip& c : project.clips) {
    if (std::find(ignore.begin(), ignore.end(), c.id) != ignore.end()) continue;
    points.push_back(c.start);
    points.push_back(c.end());
  }
  return points;
}

namespace {

// The point nearest `frame` within `tolerance`, if any.
std::optional<std::int64_t> nearest(std::int64_t frame, const std::vector<std::int64_t>& points,
                                    double tolerance) {
  std::optional<std::int64_t> best;
  double best_distance = tolerance;
  for (std::int64_t p : points) {
    const double d = std::abs(static_cast<double>(p - frame));
    if (d <= best_distance) {
      best = p;
      best_distance = d;
    }
  }
  return best;
}

}  // namespace

std::int64_t snap(std::int64_t frame, const std::vector<std::int64_t>& points,
                  double tolerance) {
  return nearest(frame, points, tolerance).value_or(frame);
}

std::int64_t snap_span(std::int64_t start, std::int64_t length,
                       const std::vector<std::int64_t>& points, double tolerance) {
  const auto head = nearest(start, points, tolerance);
  const auto tail = nearest(start + length, points, tolerance);
  if (!head && !tail) return start;
  if (head && (!tail || std::llabs(*head - start) <= std::llabs(*tail - (start + length)))) {
    return *head;
  }
  return *tail - length;
}

}  // namespace mpp

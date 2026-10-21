#pragma once

#include <cstdint>
#include <vector>

#include "mpp/project.h"
#include "mpp/rate.h"

namespace mpp {

// Maps timeline frames to horizontal pixels in the Construction window.
struct TimeScale {
  double px_per_frame = 4.0;
  double scroll = 0.0;  // the frame at the left edge

  double x_of(double frame) const { return (frame - scroll) * px_per_frame; }
  // The frame under a pixel (floor).
  std::int64_t frame_at(double x) const;
  // The frame boundary nearest a pixel, for placing edges.
  std::int64_t boundary_at(double x) const;
};

// The Construction window's zoom levels, in pixels per frame, closest first.
inline constexpr double kZoomLevels[] = {16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 0.0625, 0.03125};
inline constexpr int kZoomLevelCount = sizeof(kZoomLevels) / sizeof(kZoomLevels[0]);

// Frames between labelled ticks on the ruler: the smallest round step
// (frames, then seconds, then minutes) that leaves at least `min_px`
// between labels.
std::int64_t tick_step(Rate rate, double px_per_frame, double min_px);

// Where edits snap: the program start, the edit line, and the start and
// end of every clip and transition except those whose ids are in `ignore`.
std::vector<std::int64_t> snap_points(const Project& project, std::int64_t edit_line,
                                      const std::vector<int>& ignore);

// The point nearest `frame` if one is within `tolerance` frames, else
// `frame` itself.
std::int64_t snap(std::int64_t frame, const std::vector<std::int64_t>& points,
                  double tolerance);

// Where a span of `length` frames starting at `start` lands when either of
// its ends snaps; the closer snap wins.
std::int64_t snap_span(std::int64_t start, std::int64_t length,
                       const std::vector<std::int64_t>& points, double tolerance);

}  // namespace mpp

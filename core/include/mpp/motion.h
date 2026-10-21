#pragma once

#include <vector>

#include "mpp/image.h"

namespace mpp {

// A point on a clip's motion path (Premiere 2.0's Motion dialog): where the
// middle of the picture is at a moment of the clip, turned and scaled.
// Between points everything changes in a straight line (easing came later).
struct MotionPoint {
  double t = 0;         // 0 at the clip's first frame, 1 at its last
  double x = 0.5;       // the picture's centre, as a fraction of the frame;
  double y = 0.5;       // outside 0..1 is off screen
  double rotation = 0;  // degrees, clockwise
  double zoom = 100;    // percent of the picture's fitted size

  bool operator==(const MotionPoint& o) const {
    return t == o.t && x == o.x && y == o.y && rotation == o.rotation && zoom == o.zoom;
  }
};

struct Motion {
  std::vector<MotionPoint> points;  // sorted by t; at least one

  bool operator==(const Motion& o) const { return points == o.points; }
  bool operator!=(const Motion& o) const { return !(*this == o); }
};

// Premiere's starting path: in from the left edge, straight across, out by
// the right.
Motion default_motion();

// Where the picture is at `t`, between the points around it.
MotionPoint motion_at(const Motion& motion, double t);

// Draws a picture into a transparent frame of the given size, centred and
// unturned when `motion` is null, otherwise where the motion has it at `t`
// (bilinear, so it moves and turns smoothly).
Image place_picture(const Image& picture, const Motion* motion, double t, int width, int height);

}  // namespace mpp

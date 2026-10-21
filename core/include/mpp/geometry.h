#pragma once

#include <algorithm>

namespace mpp {

// A position in continuous coordinates, such as the mouse in the document.
struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

struct Point {
  int x = 0;
  int y = 0;
};

// Half-open integer rectangle: covers [x, x + width) by [y, y + height).
struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  int right() const { return x + width; }
  int bottom() const { return y + height; }
  bool empty() const { return width <= 0 || height <= 0; }
  bool contains(Point p) const {
    return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
  }

  Rect intersect(const Rect& o) const {
    const int x0 = std::max(x, o.x);
    const int y0 = std::max(y, o.y);
    const int x1 = std::min(right(), o.right());
    const int y1 = std::min(bottom(), o.bottom());
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
  }

  bool operator==(const Rect& o) const {
    return x == o.x && y == o.y && width == o.width && height == o.height;
  }
  bool operator!=(const Rect& o) const { return !(*this == o); }
};

}  // namespace mpp

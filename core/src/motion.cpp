#include "mpp/motion.h"

#include <algorithm>
#include <cmath>

namespace mpp {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint8_t to_byte(double v) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

}  // namespace

Motion default_motion() {
  Motion m;
  MotionPoint start, end;
  start.t = 0;
  start.x = -0.5;
  end.t = 1;
  end.x = 1.5;
  m.points = {start, end};
  return m;
}

MotionPoint motion_at(const Motion& motion, double t) {
  const std::vector<MotionPoint>& p = motion.points;
  if (p.empty()) return {};
  if (t <= p.front().t) return p.front();
  for (std::size_t i = 1; i < p.size(); ++i) {
    if (t <= p[i].t) {
      const MotionPoint& a = p[i - 1];
      const MotionPoint& b = p[i];
      const double k = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1.0;
      MotionPoint m;
      m.t = t;
      m.x = a.x + (b.x - a.x) * k;
      m.y = a.y + (b.y - a.y) * k;
      m.rotation = a.rotation + (b.rotation - a.rotation) * k;
      m.zoom = a.zoom + (b.zoom - a.zoom) * k;
      return m;
    }
  }
  return p.back();
}

Image place_picture(const Image& picture, const Motion* motion, double t, int width, int height) {
  Image out(width, height);
  if (picture.empty()) return out;
  if (!motion || motion->points.empty()) {
    out.blit(picture, {(width - picture.width()) / 2, (height - picture.height()) / 2});
    return out;
  }
  const MotionPoint m = motion_at(*motion, t);
  const double scale = m.zoom / 100.0;
  if (scale <= 0) return out;
  // For each frame pixel, the point of the picture under it: undo the
  // move, the turn and the scale.
  const double angle = m.rotation * kPi / 180.0;
  const double c = std::cos(angle), s = std::sin(angle);
  const double cx = m.x * width, cy = m.y * height;
  const double pw = picture.width(), ph = picture.height();
  const std::uint8_t* src = picture.data();
  const int sw = picture.width(), sh = picture.height();
  // Premultiplied texel, transparent outside the picture so edges soften.
  const auto texel = [&](int x, int y, double out_px[4]) {
    if (x < 0 || y < 0 || x >= sw || y >= sh) {
      out_px[0] = out_px[1] = out_px[2] = out_px[3] = 0;
      return;
    }
    const std::uint8_t* p = src + (static_cast<std::size_t>(y) * sw + x) * 4;
    const double a = p[3] / 255.0;
    out_px[0] = p[0] * a;
    out_px[1] = p[1] * a;
    out_px[2] = p[2] * a;
    out_px[3] = p[3];
  };
  std::uint8_t* o = out.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      // Turn back by the rotation (clockwise on screen, y down).
      const double ux = (dx * c + dy * s) / scale;
      const double uy = (-dx * s + dy * c) / scale;
      const double sx = ux + pw / 2 - 0.5, sy = uy + ph / 2 - 0.5;
      if (sx < -1 || sy < -1 || sx > pw || sy > ph) continue;
      const int x0 = static_cast<int>(std::floor(sx)), y0 = static_cast<int>(std::floor(sy));
      const double fx = sx - x0, fy = sy - y0;
      double a[4], b[4], cc[4], d[4];
      texel(x0, y0, a);
      texel(x0 + 1, y0, b);
      texel(x0, y0 + 1, cc);
      texel(x0 + 1, y0 + 1, d);
      double px[4];
      for (int k = 0; k < 4; ++k) {
        px[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (cc[k] * (1 - fx) + d[k] * fx) * fy;
      }
      std::uint8_t* q = o + (static_cast<std::size_t>(y) * width + x) * 4;
      const std::uint8_t alpha = to_byte(px[3]);
      q[3] = alpha;
      if (alpha == 0) continue;
      for (int k = 0; k < 3; ++k) q[k] = to_byte(px[k] * 255.0 / px[3]);
    }
  }
  return out;
}

}  // namespace mpp

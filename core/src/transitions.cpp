#include "mpp/transitions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "mpp/compositing.h"
#include "mpp/resample.h"

namespace mpp {

namespace {

constexpr double kPi = 3.14159265358979323846;

const std::vector<std::string> kSides = {"From left", "From right", "From top", "From bottom"};
const std::vector<std::string> kEightWays = {"From left",      "From right",     "From top",
                                             "From bottom",    "From top left",  "From top right",
                                             "From bottom left", "From bottom right"};
const std::vector<std::string> kCorners = {"From top left", "From top right", "From bottom left",
                                           "From bottom right"};
const std::vector<std::string> kOrientations = {"Horizontal", "Vertical"};

std::uint8_t to_byte(double v) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// Where a moving edge is after travelling `extent` pixels, starting
// `border` pixels early so a border is out of sight at progress 0 and
// pushed past the end at 1.
double edge(double p, double extent, int border) { return p * (extent + border) - border; }

// Signed distance in pixels from a pixel centre to the edge of the area
// showing `to`: positive inside it.
using Mask = std::function<double(double x, double y)>;

// Mixes by a mask, anti-aliasing its edge and drawing the border band on
// the `from` side of it.
Image masked(const Image& from, const Image& to, const Mask& distance, const TransitionSettings& s) {
  Image out(from.width(), from.height());
  const std::uint8_t* a = from.data();
  const std::uint8_t* b = to.data();
  std::uint8_t* o = out.data();
  const double border = std::max(0, s.border);
  const std::uint8_t bc[4] = {s.border_color.r, s.border_color.g, s.border_color.b, 255};
  for (int y = 0; y < out.height(); ++y) {
    for (int x = 0; x < out.width(); ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * out.width() + x) * 4;
      const double d = distance(x + 0.5, y + 0.5);
      const double show_to = clamp01(d + 0.5);
      const double covered = border > 0 ? clamp01(d + border + 0.5) : show_to;
      if (covered <= 0.0) {
        o[i] = a[i], o[i + 1] = a[i + 1], o[i + 2] = a[i + 2], o[i + 3] = a[i + 3];
      } else if (show_to >= 1.0) {
        o[i] = b[i], o[i + 1] = b[i + 1], o[i + 2] = b[i + 2], o[i + 3] = b[i + 3];
      } else {
        const double band = covered - show_to;
        for (int c = 0; c < 4; ++c) {
          o[i + c] = to_byte(a[i + c] * (1.0 - covered) + bc[c] * band + b[i + c] * show_to);
        }
      }
    }
  }
  return out;
}

// Pixel access that clamps to the edge.
Color at(const Image& img, int x, int y) {
  return img.pixel(std::clamp(x, 0, img.width() - 1), std::clamp(y, 0, img.height() - 1));
}

// Builds a picture by asking, for each pixel, which image and where.
template <typename F>
Image remap(int w, int h, F source) {
  Image out(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) out.set_pixel(x, y, source(x, y));
  }
  return out;
}

// ---- Dissolves ------------------------------------------------------------

Image cross_dissolve(const Image& a, const Image& b, double p, const TransitionSettings&) {
  return mix(a, b, p);
}

// Both pictures at full strength in the middle, so it brightens.
Image additive_dissolve(const Image& a, const Image& b, double p, const TransitionSettings&) {
  Image out(a.width(), a.height());
  const double ka = std::min(1.0, 2.0 * (1.0 - p)), kb = std::min(1.0, 2.0 * p);
  for (std::size_t i = 0; i < out.size_bytes(); ++i) {
    out.data()[i] = to_byte(a.data()[i] * ka + b.data()[i] * kb);
  }
  return out;
}

// Pixels switch over in an ordered dither pattern.
Image dither_dissolve(const Image& a, const Image& b, double p, const TransitionSettings&) {
  static const int bayer[8][8] = {
      {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
      {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
      {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
      {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21}};
  return remap(a.width(), a.height(), [&](int x, int y) {
    return (bayer[y % 8][x % 8] + 0.5) / 64.0 < p ? b.pixel(x, y) : a.pixel(x, y);
  });
}

// The brightest parts of the incoming picture appear first.
Image non_additive_dissolve(const Image& a, const Image& b, double p, const TransitionSettings&) {
  const double threshold = (1.0 - p) * 256.0;
  return remap(a.width(), a.height(), [&](int x, int y) {
    const Color c = b.pixel(x, y);
    const double luma = (c.r * 299 + c.g * 587 + c.b * 114) / 1000.0;
    return luma >= threshold ? c : a.pixel(x, y);
  });
}

// ---- Wipes ----------------------------------------------------------------

Image wipe(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double w = a.width(), h = a.height();
  // Unit direction of travel and the corner or edge it starts from.
  double ux = 1, uy = 0, ox = 0, oy = 0;
  switch (s.direction) {
    case 1: ux = -1, ox = w; break;
    case 2: ux = 0, uy = 1; break;
    case 3: ux = 0, uy = -1, oy = h; break;
    case 4: ux = w, uy = h; break;
    case 5: ux = -w, uy = h, ox = w; break;
    case 6: ux = w, uy = -h, oy = h; break;
    case 7: ux = -w, uy = -h, ox = w, oy = h; break;
    default: break;
  }
  const double len = std::hypot(ux, uy);
  ux /= len, uy /= len;
  const double extent = std::abs(ux) * w + std::abs(uy) * h;
  const double e = edge(p, extent, s.border);
  return masked(a, b, [&](double x, double y) { return e - ((x - ox) * ux + (y - oy) * uy); }, s);
}

Image barn_doors(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double w = a.width(), h = a.height();
  const bool vertical = s.direction == 1;
  const double e = edge(p, (vertical ? h : w) / 2, s.border);
  return masked(a, b, [&](double x, double y) {
    return e - (vertical ? std::abs(y - h / 2) : std::abs(x - w / 2));
  }, s);
}

// Eight bands, alternate ones wiping from opposite sides.
Image band_wipe(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double w = a.width(), h = a.height();
  const bool vertical = s.direction == 1;
  const double extent = vertical ? h : w;
  const double band = (vertical ? w : h) / 8.0;
  const double e = edge(p, extent, s.border);
  return masked(a, b, [&](double x, double y) {
    const int index = static_cast<int>((vertical ? x : y) / band);
    const double along = vertical ? y : x;
    return e - (index % 2 ? extent - along : along);
  }, s);
}

// An 8 x 6 board: one colour of squares wipes in, then the other.
Image checkerboard(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double cw = a.width() / 8.0, ch = a.height() / 6.0;
  const double e0 = edge(std::min(1.0, 2 * p), cw, s.border);
  const double e1 = edge(std::max(0.0, 2 * p - 1), cw, s.border);
  return masked(a, b, [&](double x, double y) {
    const int cx = static_cast<int>(x / cw), cy = static_cast<int>(y / ch);
    const double local = x - cx * cw;
    return ((cx + cy) % 2 ? e1 : e0) - local;
  }, s);
}

// A hand sweeping clockwise from twelve o'clock.
Image clock_wipe(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double cx = a.width() / 2.0, cy = a.height() / 2.0;
  const double sweep = p * 2 * kPi;
  return masked(a, b, [&](double x, double y) {
    const double dx = x - cx, dy = y - cy;
    double theta = std::atan2(dx, -dy);
    if (theta < 0) theta += 2 * kPi;
    const double r = std::max(1.0, std::hypot(dx, dy));
    return std::min(sweep - theta, theta) * r;
  }, s);
}

// Ten slats opening together.
Image venetian_blinds(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const bool vertical = s.direction == 1;
  const double slat = (vertical ? a.width() : a.height()) / 10.0;
  const double e = edge(p, slat, s.border);
  return masked(a, b, [&](double x, double y) {
    return e - std::fmod(vertical ? x : y, slat);
  }, s);
}

// A 16 x 12 grid of blocks switching over in a fixed shuffled order.
Image random_blocks(const Image& a, const Image& b, double p, const TransitionSettings&) {
  constexpr int kCols = 16, kRows = 12, kCount = kCols * kRows;
  static const std::array<int, kCount> rank = [] {
    std::array<int, kCount> order{};
    for (int i = 0; i < kCount; ++i) order[i] = i;
    std::uint32_t state = 12345;
    for (int i = kCount - 1; i > 0; --i) {
      state = state * 1664525u + 1013904223u;
      std::swap(order[i], order[(state >> 8) % static_cast<std::uint32_t>(i + 1)]);
    }
    std::array<int, kCount> ranks{};
    for (int i = 0; i < kCount; ++i) ranks[order[i]] = i;
    return ranks;
  }();
  const double shown = p * kCount;
  return remap(a.width(), a.height(), [&](int x, int y) {
    const int col = x * kCols / a.width(), row = y * kRows / a.height();
    return rank[row * kCols + col] < shown ? b.pixel(x, y) : a.pixel(x, y);
  });
}

// The incoming picture grows from a corner as a rectangle.
Image inset(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double w = a.width(), h = a.height();
  const bool right = s.direction == 1 || s.direction == 3;
  const bool bottom = s.direction == 2 || s.direction == 3;
  const double ex = edge(p, w, s.border);
  const double ey = ex * h / w;
  return masked(a, b, [&](double x, double y) {
    const double sx = right ? w - x : x, sy = bottom ? h - y : y;
    return std::min(ex - sx, ey - sy);
  }, s);
}

// ---- Irises ---------------------------------------------------------------

Image iris_round(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double cx = a.width() / 2.0, cy = a.height() / 2.0;
  const double r = edge(p, std::hypot(cx, cy), s.border);
  return masked(a, b, [&](double x, double y) { return r - std::hypot(x - cx, y - cy); }, s);
}

Image iris_square(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double cx = a.width() / 2.0, cy = a.height() / 2.0;
  const double rx = edge(p, cx, s.border), ry = rx * cy / cx;
  return masked(a, b, [&](double x, double y) {
    return std::min(rx - std::abs(x - cx), ry - std::abs(y - cy));
  }, s);
}

Image iris_diamond(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double cx = a.width() / 2.0, cy = a.height() / 2.0;
  const double r = edge(p, 2 * cx, s.border);
  return masked(a, b, [&](double x, double y) {
    return (r - (std::abs(x - cx) + std::abs(y - cy) * cx / cy)) * 0.7071;
  }, s);
}

Image iris_cross(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const double cx = a.width() / 2.0, cy = a.height() / 2.0;
  const double rx = edge(p, cx, s.border), ry = rx * cy / cx;
  return masked(a, b, [&](double x, double y) {
    return std::max(rx - std::abs(x - cx), ry - std::abs(y - cy));
  }, s);
}

// ---- Slides ---------------------------------------------------------------

// The incoming picture pushes the outgoing one off the far side.
Image push(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const int w = a.width(), h = a.height();
  const bool horizontal = s.direction < 2;
  const int size = horizontal ? w : h;
  const int off = static_cast<int>(std::lround(p * size));
  const bool from_start = s.direction == 0 || s.direction == 2;
  return remap(w, h, [&](int x, int y) {
    const int along = horizontal ? x : y;
    int src;
    bool incoming;
    if (from_start) {
      incoming = along < off;
      src = incoming ? along - off + size : along - off;
    } else {
      incoming = along >= size - off;
      src = incoming ? along - (size - off) : along + off;
    }
    const Image& img = incoming ? b : a;
    return horizontal ? at(img, src, y) : at(img, x, src);
  });
}

// The incoming picture slides in over the outgoing one.
Image slide(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const int w = a.width(), h = a.height();
  const bool horizontal = s.direction < 2;
  const int size = horizontal ? w : h;
  const int off = static_cast<int>(std::lround(p * size));
  const bool from_start = s.direction == 0 || s.direction == 2;
  return remap(w, h, [&](int x, int y) {
    const int along = horizontal ? x : y;
    if (from_start ? along < off : along >= size - off) {
      const int src = from_start ? along - off + size : along - (size - off);
      return horizontal ? at(b, src, y) : at(b, x, src);
    }
    return a.pixel(x, y);
  });
}

// The outgoing picture splits down the middle and slides apart.
Image split(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const int w = a.width(), h = a.height();
  const bool horizontal = s.direction == 0;
  const int size = horizontal ? w : h;
  const int half = size / 2;
  const int off = static_cast<int>(std::lround(p * half));
  return remap(w, h, [&](int x, int y) {
    const int along = horizontal ? x : y;
    int src;
    if (along < half - off) src = along + off;
    else if (along >= half + off) src = along - off;
    else return b.pixel(x, y);
    return horizontal ? at(a, src, y) : at(a, x, src);
  });
}

// The outgoing picture splits into quarters that slide to the corners.
Image center_split(const Image& a, const Image& b, double p, const TransitionSettings&) {
  const int w = a.width(), h = a.height(), hw = w / 2, hh = h / 2;
  const int ox = static_cast<int>(std::lround(p * hw)), oy = static_cast<int>(std::lround(p * hh));
  return remap(w, h, [&](int x, int y) {
    int sx, sy;
    if (x < hw - ox) sx = x + ox;
    else if (x >= hw + ox) sx = x - ox;
    else return b.pixel(x, y);
    if (y < hh - oy) sy = y + oy;
    else if (y >= hh + oy) sy = y - oy;
    else return b.pixel(x, y);
    return at(a, sx, sy);
  });
}

// The incoming picture's quarters slide in from the corners and meet.
Image center_merge(const Image& a, const Image& b, double p, const TransitionSettings&) {
  const int w = a.width(), h = a.height(), hw = w / 2, hh = h / 2;
  const int ox = static_cast<int>(std::lround(p * hw)), oy = static_cast<int>(std::lround(p * hh));
  return remap(w, h, [&](int x, int y) {
    int sx, sy;
    if (x < ox) sx = x + hw - ox;
    else if (x >= w - ox) sx = x - (w - ox) + hw;
    else return a.pixel(x, y);
    if (y < oy) sy = y + hh - oy;
    else if (y >= h - oy) sy = y - (h - oy) + hh;
    else return a.pixel(x, y);
    return at(b, sx, sy);
  });
}

// The pictures slide apart sideways and change places front to back.
Image swap(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const int w = a.width(), h = a.height();
  const bool first_half = p < 0.5;
  const int off = static_cast<int>(std::lround((first_half ? 2 * p : 2 - 2 * p) * w / 2));
  const int dir = s.direction == 1 ? -1 : 1;
  const Image& front = first_half ? a : b;
  const Image& back = first_half ? b : a;
  const int front_off = first_half ? -off * dir : off * dir;
  const int back_off = -front_off;
  return remap(w, h, [&](int x, int y) {
    const int fx = x - front_off;
    if (fx >= 0 && fx < w) return front.pixel(fx, y);
    const int bx = x - back_off;
    if (bx >= 0 && bx < w) return back.pixel(bx, y);
    return kBlack;
  });
}

// ---- Zoom and stretch -----------------------------------------------------

// The incoming picture grows from the centre.
Image zoom(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const int w = a.width(), h = a.height();
  Image out = a;
  const int zw = static_cast<int>(std::lround(w * p)), zh = static_cast<int>(std::lround(h * p));
  if (zw <= 0 || zh <= 0) return out;
  const int x0 = (w - zw) / 2, y0 = (h - zh) / 2;
  if (s.border > 0) {
    out.fill({x0 - s.border, y0 - s.border, zw + 2 * s.border, zh + 2 * s.border},
             {s.border_color.r, s.border_color.g, s.border_color.b, 255});
  }
  out.blit(resized(b, zw, zh), {x0, y0});
  return out;
}

// The incoming picture stretches in from a side as the outgoing one
// squeezes away.
Image stretch(const Image& a, const Image& b, double p, const TransitionSettings& s) {
  const int w = a.width(), h = a.height();
  const bool horizontal = s.direction < 2;
  const int size = horizontal ? w : h;
  const int in = static_cast<int>(std::lround(p * size));
  const bool from_start = s.direction == 0 || s.direction == 2;
  Image out(w, h);
  const auto place = [&](const Image& img, int length, int at) {
    if (length <= 0) return;
    out.blit(horizontal ? resized(img, length, h) : resized(img, w, length),
             horizontal ? Point{at, 0} : Point{0, at});
  };
  if (from_start) {
    place(b, in, 0);
    place(a, size - in, in);
  } else {
    place(a, size - in, 0);
    place(b, in, size - in);
  }
  return out;
}

std::vector<TransitionDef> make_registry() {
  return {
      {"cross_dissolve", "Cross Dissolve", "Dissolve", {}, false, cross_dissolve},
      {"additive_dissolve", "Additive Dissolve", "Dissolve", {}, false, additive_dissolve},
      {"dither_dissolve", "Dither Dissolve", "Dissolve", {}, false, dither_dissolve},
      {"non_additive_dissolve", "Non-Additive Dissolve", "Dissolve", {}, false,
       non_additive_dissolve},
      {"wipe", "Wipe", "Wipe", kEightWays, true, wipe},
      {"barn_doors", "Barn Doors", "Wipe", {"Open sideways", "Open up and down"}, true, barn_doors},
      {"band_wipe", "Band Wipe", "Wipe", kOrientations, true, band_wipe},
      {"checkerboard", "Checkerboard", "Wipe", {}, true, checkerboard},
      {"clock_wipe", "Clock Wipe", "Wipe", {}, false, clock_wipe},
      {"venetian_blinds", "Venetian Blinds", "Wipe", kOrientations, true, venetian_blinds},
      {"random_blocks", "Random Blocks", "Wipe", {}, false, random_blocks},
      {"inset", "Inset", "Wipe", kCorners, true, inset},
      {"iris_round", "Iris Round", "Iris", {}, true, iris_round},
      {"iris_square", "Iris Square", "Iris", {}, true, iris_square},
      {"iris_diamond", "Iris Diamond", "Iris", {}, true, iris_diamond},
      {"iris_cross", "Iris Cross", "Iris", {}, true, iris_cross},
      {"push", "Push", "Slide", kSides, false, push},
      {"slide", "Slide", "Slide", kSides, false, slide},
      {"split", "Split", "Slide", kOrientations, false, split},
      {"center_split", "Center Split", "Slide", {}, false, center_split},
      {"center_merge", "Center Merge", "Slide", {}, false, center_merge},
      {"swap", "Swap", "Slide", {"To the left", "To the right"}, false, swap},
      {"zoom", "Zoom", "Zoom", {}, true, zoom},
      {"stretch", "Stretch", "Stretch", kSides, false, stretch},
  };
}

}  // namespace

const std::vector<TransitionDef>& transitions() {
  static const std::vector<TransitionDef> registry = make_registry();
  return registry;
}

const TransitionDef* find_transition(const std::string& id) {
  for (const TransitionDef& def : transitions()) {
    if (def.id == id) return &def;
  }
  return nullptr;
}

Image apply_transition(const TransitionDef& def, const Image& from, const Image& to,
                       double progress, const TransitionSettings& settings) {
  if (from.width() != to.width() || from.height() != to.height()) {
    throw std::invalid_argument("A transition needs two pictures of the same size");
  }
  if (settings.reverse) {
    TransitionSettings forward = settings;
    forward.reverse = false;
    return apply_transition(def, to, from, 1.0 - progress, forward);
  }
  if (progress <= 0.0) return from;
  if (progress >= 1.0) return to;
  TransitionSettings s = settings;
  const int directions = static_cast<int>(def.directions.size());
  s.direction = directions ? std::clamp(s.direction, 0, directions - 1) : 0;
  if (!def.border) s.border = 0;
  return def.apply(from, to, progress, s);
}

double transition_progress(const TransitionSettings& settings, std::int64_t index,
                           std::int64_t length) {
  if (length <= 0) return settings.start;
  const double t = (static_cast<double>(index) + 0.5) / static_cast<double>(length);
  return settings.start + (settings.end - settings.start) * t;
}

}  // namespace mpp

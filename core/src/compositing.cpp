#include "mpp/compositing.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mpp {

namespace {

std::uint8_t to_byte(double v) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

void require_same_size(const Image& a, const Image& b) {
  if (a.width() != b.width() || a.height() != b.height()) {
    throw std::invalid_argument("Pictures must be the same size to combine");
  }
}

}  // namespace

void composite_over(Image& base, const Image& top, double opacity) {
  require_same_size(base, top);
  opacity = std::clamp(opacity, 0.0, 1.0);
  if (opacity <= 0.0) return;
  std::uint8_t* b = base.data();
  const std::uint8_t* t = top.data();
  for (std::size_t i = 0; i < base.size_bytes(); i += 4) {
    const double at = t[i + 3] / 255.0 * opacity;
    if (at <= 0.0) continue;
    const double ab = b[i + 3] / 255.0;
    const double ao = at + ab * (1.0 - at);
    if (at >= 1.0) {
      b[i] = t[i];
      b[i + 1] = t[i + 1];
      b[i + 2] = t[i + 2];
      b[i + 3] = 255;
      continue;
    }
    for (int c = 0; c < 3; ++c) {
      b[i + c] = to_byte((t[i + c] * at + b[i + c] * ab * (1.0 - at)) / ao);
    }
    b[i + 3] = to_byte(ao * 255.0);
  }
}

Image mix(const Image& a, const Image& b, double t) {
  require_same_size(a, b);
  t = std::clamp(t, 0.0, 1.0);
  Image out(a.width(), a.height());
  const std::uint8_t* pa = a.data();
  const std::uint8_t* pb = b.data();
  std::uint8_t* o = out.data();
  for (std::size_t i = 0; i < out.size_bytes(); ++i) {
    o[i] = to_byte(pa[i] * (1.0 - t) + pb[i] * t);
  }
  return out;
}

Image flattened(const Image& image, Color background) {
  Image out(image.width(), image.height(), background);
  composite_over(out, image);
  return out;
}

}  // namespace mpp

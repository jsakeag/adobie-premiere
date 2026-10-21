#include "mpp/resample.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace mpp {

namespace {

// Weights for one output position along one axis.
struct Tap {
  int first = 0;               // first source index
  std::vector<float> weights;  // sum to 1
};

std::vector<Tap> taps(int src, int dst) {
  std::vector<Tap> out(dst);
  const double scale = static_cast<double>(src) / dst;
  const double support = std::max(1.0, scale);
  for (int i = 0; i < dst; ++i) {
    const double center = (i + 0.5) * scale - 0.5;
    const int lo = static_cast<int>(std::floor(center - support)) + 1;
    const int hi = static_cast<int>(std::ceil(center + support)) - 1;
    Tap& t = out[i];
    t.first = lo;
    double sum = 0;
    for (int s = lo; s <= hi; ++s) {
      const double w = std::max(0.0, 1.0 - std::abs(s - center) / support);
      t.weights.push_back(static_cast<float>(w));
      sum += w;
    }
    if (sum <= 0) {  // can't happen with support >= 1, but stay safe
      t.first = std::clamp(static_cast<int>(std::lround(center)), 0, src - 1);
      t.weights.assign(1, 1.0f);
      continue;
    }
    for (float& w : t.weights) w = static_cast<float>(w / sum);
  }
  return out;
}

std::uint8_t to_byte(float v) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

}  // namespace

Image resized(const Image& src, int width, int height) {
  if (width <= 0 || height <= 0) throw std::invalid_argument("resized needs a positive size");
  if (src.empty()) return Image(width, height);
  if (src.width() == width && src.height() == height) return src;
  const int sw = src.width(), sh = src.height();

  // Premultiplied floats, 4 per pixel.
  std::vector<float> pre(static_cast<std::size_t>(sw) * sh * 4);
  const std::uint8_t* p = src.data();
  for (std::size_t i = 0; i < pre.size(); i += 4) {
    const float a = p[i + 3] / 255.0f;
    pre[i] = p[i] * a;
    pre[i + 1] = p[i + 1] * a;
    pre[i + 2] = p[i + 2] * a;
    pre[i + 3] = static_cast<float>(p[i + 3]);
  }

  const std::vector<Tap> tx = taps(sw, width);
  std::vector<float> mid(static_cast<std::size_t>(width) * sh * 4);
  for (int y = 0; y < sh; ++y) {
    const float* row = &pre[static_cast<std::size_t>(y) * sw * 4];
    float* out = &mid[static_cast<std::size_t>(y) * width * 4];
    for (int x = 0; x < width; ++x) {
      const Tap& t = tx[x];
      float acc[4] = {0, 0, 0, 0};
      for (std::size_t k = 0; k < t.weights.size(); ++k) {
        const int s = std::clamp(t.first + static_cast<int>(k), 0, sw - 1);
        for (int c = 0; c < 4; ++c) acc[c] += row[s * 4 + c] * t.weights[k];
      }
      for (int c = 0; c < 4; ++c) out[x * 4 + c] = acc[c];
    }
  }

  const std::vector<Tap> ty = taps(sh, height);
  Image dst(width, height);
  std::uint8_t* d = dst.data();
  for (int y = 0; y < height; ++y) {
    const Tap& t = ty[y];
    for (int x = 0; x < width; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (std::size_t k = 0; k < t.weights.size(); ++k) {
        const int s = std::clamp(t.first + static_cast<int>(k), 0, sh - 1);
        const float* px = &mid[(static_cast<std::size_t>(s) * width + x) * 4];
        for (int c = 0; c < 4; ++c) acc[c] += px[c] * t.weights[k];
      }
      std::uint8_t* o = d + (static_cast<std::size_t>(y) * width + x) * 4;
      const std::uint8_t a = to_byte(acc[3]);
      o[3] = a;
      if (a == 0) {
        o[0] = o[1] = o[2] = 0;
      } else {
        for (int c = 0; c < 3; ++c) o[c] = to_byte(acc[c] * 255.0f / acc[3]);
      }
    }
  }
  return dst;
}

Point fitted_size(int src_width, int src_height, int width, int height) {
  if (src_width <= 0 || src_height <= 0) return {width, height};
  const double scale = std::min(static_cast<double>(width) / src_width,
                                static_cast<double>(height) / src_height);
  return {std::clamp(static_cast<int>(std::lround(src_width * scale)), 1, width),
          std::clamp(static_cast<int>(std::lround(src_height * scale)), 1, height)};
}

Image fit_into(const Image& src, int width, int height, Color background) {
  Image out(width, height, background);
  if (src.empty()) return out;
  const Point size = fitted_size(src.width(), src.height(), width, height);
  out.blit(resized(src, size.x, size.y), {(width - size.x) / 2, (height - size.y) / 2});
  return out;
}

}  // namespace mpp

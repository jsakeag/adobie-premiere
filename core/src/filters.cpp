#include "mpp/filters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include "mpp/resample.h"

namespace mpp {

namespace {

constexpr double kPi = 3.14159265358979323846;

using Lut = std::array<std::uint8_t, 256>;
using Values = std::vector<double>;

std::uint8_t to_byte(double v) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

double luma(const std::uint8_t* p) { return (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000.0; }

// Runs a lookup table over the colour channels, leaving alpha.
Image apply_lut(const Image& in, const Lut& lut) {
  Image out = in;
  std::uint8_t* p = out.data();
  for (std::size_t i = 0; i < out.size_bytes(); i += 4) {
    p[i] = lut[p[i]];
    p[i + 1] = lut[p[i + 1]];
    p[i + 2] = lut[p[i + 2]];
  }
  return out;
}

template <typename F>
Lut make_lut(F f) {
  Lut lut;
  for (int v = 0; v < 256; ++v) lut[v] = to_byte(f(v));
  return lut;
}

// Changes each pixel on its own.
template <typename F>
Image per_pixel(const Image& in, F f) {
  Image out = in;
  std::uint8_t* p = out.data();
  for (std::size_t i = 0; i < out.size_bytes(); i += 4) f(p + i);
  return out;
}

// ---- Premultiplied buffers, for filters that mix neighbours ---------------

struct Premul {
  float r = 0, g = 0, b = 0, a = 0;
};

struct Buffer {
  int width = 0;
  int height = 0;
  std::vector<Premul> px;

  Premul& at(int x, int y) { return px[static_cast<std::size_t>(y) * width + x]; }
  const Premul& clamped(int x, int y) const {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return px[static_cast<std::size_t>(y) * width + x];
  }
};

Buffer to_buffer(const Image& image) {
  Buffer b{image.width(), image.height(), {}};
  b.px.resize(static_cast<std::size_t>(b.width) * b.height);
  const std::uint8_t* p = image.data();
  for (Premul& out : b.px) {
    const float a = p[3] / 255.0f;
    out = {p[0] * a, p[1] * a, p[2] * a, static_cast<float>(p[3])};
    p += 4;
  }
  return b;
}

Image to_image(const Buffer& b) {
  Image image(b.width, b.height);
  std::uint8_t* p = image.data();
  for (const Premul& in : b.px) {
    const std::uint8_t a = to_byte(in.a);
    p[3] = a;
    if (a == 0) {
      p[0] = p[1] = p[2] = 0;
    } else {
      p[0] = to_byte(in.r * 255.0f / in.a);
      p[1] = to_byte(in.g * 255.0f / in.a);
      p[2] = to_byte(in.b * 255.0f / in.a);
    }
    p += 4;
  }
  return image;
}

void add_scaled(Premul& sum, const Premul& p, float w) {
  sum.r += p.r * w;
  sum.g += p.g * w;
  sum.b += p.b * w;
  sum.a += p.a * w;
}

// Convolves with a 1D kernel across then down.
Image separable(const Image& source, const std::vector<float>& kernel) {
  const int reach = static_cast<int>(kernel.size() / 2);
  Buffer in = to_buffer(source);
  Buffer across = in;
  for (int y = 0; y < in.height; ++y) {
    for (int x = 0; x < in.width; ++x) {
      Premul sum;
      for (int k = -reach; k <= reach; ++k) add_scaled(sum, in.clamped(x + k, y), kernel[k + reach]);
      across.at(x, y) = sum;
    }
  }
  for (int y = 0; y < in.height; ++y) {
    for (int x = 0; x < in.width; ++x) {
      Premul sum;
      for (int k = -reach; k <= reach; ++k) add_scaled(sum, across.clamped(x, y + k), kernel[k + reach]);
      in.at(x, y) = sum;
    }
  }
  return to_image(in);
}

Image gaussian(const Image& source, double radius) {
  if (radius <= 0.0 || source.empty()) return source;
  const int reach = static_cast<int>(std::ceil(3.0 * radius));
  std::vector<float> kernel(2 * reach + 1);
  float total = 0;
  for (int k = -reach; k <= reach; ++k) {
    total += kernel[k + reach] = static_cast<float>(std::exp(-0.5 * k * k / (radius * radius)));
  }
  for (float& w : kernel) w /= total;
  return separable(source, kernel);
}

// Samples between pixels (premultiplied bilinear), clamping at the edges.
Premul sample(const Buffer& b, double x, double y) {
  const double fx = std::floor(x - 0.5), fy = std::floor(y - 0.5);
  const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
  const float tx = static_cast<float>(x - 0.5 - fx), ty = static_cast<float>(y - 0.5 - fy);
  Premul out;
  add_scaled(out, b.clamped(x0, y0), (1 - tx) * (1 - ty));
  add_scaled(out, b.clamped(x0 + 1, y0), tx * (1 - ty));
  add_scaled(out, b.clamped(x0, y0 + 1), (1 - tx) * ty);
  add_scaled(out, b.clamped(x0 + 1, y0 + 1), tx * ty);
  return out;
}

// splitmix64's finaliser: a well-mixed hash for noise.
std::uint64_t hash(std::uint64_t z) {
  z += 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

// ---- Colour ---------------------------------------------------------------

Image brightness_contrast(const Image& in, const Values& v, std::int64_t) {
  const double c = std::clamp(v[1], -100.0, 100.0) / 100.0;
  // Contrast 100 would be an infinite stretch: cap it at a threshold.
  const double factor = c >= 0 ? 1.0 / std::max(1.0 - c, 1.0 / 512) : 1.0 + c;
  return apply_lut(in, make_lut([&](int x) {
    const double bright = std::clamp(x + v[0], 0.0, 255.0);
    return (bright - 127.5) * factor + 127.5;
  }));
}

Image levels(const Image& in, const Values& v, std::int64_t) {
  const double span = std::max(1.0, v[1] - v[0]);
  const double gamma = std::max(0.01, v[2]);
  return apply_lut(in, make_lut([&](int x) {
    const double t = std::pow(std::clamp((x - v[0]) / span, 0.0, 1.0), 1.0 / gamma);
    return v[3] + t * (v[4] - v[3]);
  }));
}

Image gamma_correction(const Image& in, const Values& v, std::int64_t) {
  const double gamma = std::max(0.01, v[0]);
  return apply_lut(in, make_lut([&](int x) { return 255.0 * std::pow(x / 255.0, 1.0 / gamma); }));
}

double hue_to_channel(double p, double q, double t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1.0 / 6) return p + (q - p) * 6 * t;
  if (t < 1.0 / 2) return q;
  if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
  return p;
}

Image hue_saturation(const Image& in, const Values& v, std::int64_t) {
  return per_pixel(in, [&](std::uint8_t* px) {
    // RGB to HSL.
    const double r = px[0] / 255.0, g = px[1] / 255.0, b = px[2] / 255.0;
    const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
    double l = (hi + lo) / 2, h = 0, s = 0;
    if (hi > lo) {
      const double d = hi - lo;
      s = l > 0.5 ? d / (2 - hi - lo) : d / (hi + lo);
      if (hi == r) h = (g - b) / d + (g < b ? 6 : 0);
      else if (hi == g) h = (b - r) / d + 2;
      else h = (r - g) / d + 4;
      h /= 6;
    }
    h = std::fmod(h + v[0] / 360.0 + 1.0, 1.0);
    s = std::clamp(s * (1 + v[1] / 100.0), 0.0, 1.0);
    const double light = v[2] / 100.0;
    l = light >= 0 ? l + (1 - l) * light : l * (1 + light);
    // HSL to RGB.
    if (s == 0) {
      px[0] = px[1] = px[2] = to_byte(l * 255);
      return;
    }
    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    const double p = 2 * l - q;
    px[0] = to_byte(hue_to_channel(p, q, h + 1.0 / 3) * 255);
    px[1] = to_byte(hue_to_channel(p, q, h) * 255);
    px[2] = to_byte(hue_to_channel(p, q, h - 1.0 / 3) * 255);
  });
}

// Shifts each channel up or down by a percentage of full scale.
Image color_balance(const Image& in, const Values& v, std::int64_t) {
  return per_pixel(in, [&](std::uint8_t* p) {
    for (int c = 0; c < 3; ++c) p[c] = to_byte(p[c] + v[c] * 2.55);
  });
}

// Colours the picture by its brightness, mixed with the original.
Image tint(const Image& in, const Values& v, std::int64_t) {
  const double amount = v[3] / 100.0;
  return per_pixel(in, [&](std::uint8_t* p) {
    const double y = luma(p) / 255.0;
    for (int c = 0; c < 3; ++c) p[c] = to_byte(p[c] * (1 - amount) + v[c] * y * amount);
  });
}

Image black_white(const Image& in, const Values&, std::int64_t) {
  return per_pixel(in, [](std::uint8_t* p) { p[0] = p[1] = p[2] = to_byte(luma(p)); });
}

Image invert(const Image& in, const Values&, std::int64_t) {
  return apply_lut(in, make_lut([](int x) { return 255 - x; }));
}

Image posterize(const Image& in, const Values& v, std::int64_t) {
  const double n = std::max(2.0, v[0]) - 1;
  return apply_lut(in, make_lut([&](int x) { return std::round(x / 255.0 * n) * 255.0 / n; }));
}

Image threshold(const Image& in, const Values& v, std::int64_t) {
  return per_pixel(in, [&](std::uint8_t* p) { p[0] = p[1] = p[2] = luma(p) >= v[0] ? 255 : 0; });
}

Image solarize(const Image& in, const Values& v, std::int64_t) {
  return apply_lut(in, make_lut([&](int x) { return x > v[0] ? 255 - x : x; }));
}

// ---- Blur and sharpen -----------------------------------------------------

Image gaussian_blur(const Image& in, const Values& v, std::int64_t) { return gaussian(in, v[0]); }

// A flat (box) blur, like an out-of-focus lens.
Image camera_blur(const Image& in, const Values& v, std::int64_t) {
  const int reach = static_cast<int>(std::lround(v[0]));
  if (reach <= 0) return in;
  return separable(in, std::vector<float>(2 * reach + 1, 1.0f / (2 * reach + 1)));
}

Image motion_blur(const Image& in, const Values& v, std::int64_t) {
  const int n = std::max(1, static_cast<int>(std::lround(v[1])));
  if (n == 1 || in.empty()) return in;
  const double angle = v[0] * kPi / 180.0;
  const Buffer b = to_buffer(in);
  Buffer out = b;
  const float w = 1.0f / n;
  for (int y = 0; y < b.height; ++y) {
    for (int x = 0; x < b.width; ++x) {
      Premul sum;
      for (int k = 0; k < n; ++k) {
        const double t = k - (n - 1) / 2.0;
        add_scaled(sum,
                   b.clamped(x + static_cast<int>(std::lround(t * std::cos(angle))),
                             y + static_cast<int>(std::lround(-t * std::sin(angle)))),
                   w);
      }
      out.at(x, y) = sum;
    }
  }
  return to_image(out);
}

// Adds back the difference from a one-pixel blur.
Image sharpen(const Image& in, const Values& v, std::int64_t) {
  const double amount = v[0] / 100.0;
  const Image blurred = gaussian(in, 1.0);
  Image out = in;
  const std::uint8_t* b = blurred.data();
  std::uint8_t* p = out.data();
  for (std::size_t i = 0; i < out.size_bytes(); i += 4) {
    if (p[i + 3] == 0) continue;
    for (int c = 0; c < 3; ++c) p[i + c] = to_byte(p[i + c] + amount * (p[i + c] - b[i + c]));
  }
  return out;
}

// ---- Stylize --------------------------------------------------------------

// Grey relief lit from `angle`: the difference in brightness across each
// pixel, around middle grey.
Image emboss(const Image& in, const Values& v, std::int64_t) {
  const double angle = v[0] * kPi / 180.0;
  const int dx = static_cast<int>(std::lround(std::cos(angle) * v[1]));
  const int dy = static_cast<int>(std::lround(-std::sin(angle) * v[1]));
  const double amount = v[2] / 100.0;
  Image out = in;
  const auto lum_at = [&](int x, int y) {
    return luma(in.data() + (static_cast<std::size_t>(std::clamp(y, 0, in.height() - 1)) * in.width() +
                             std::clamp(x, 0, in.width() - 1)) * 4);
  };
  for (int y = 0; y < in.height(); ++y) {
    for (int x = 0; x < in.width(); ++x) {
      const std::uint8_t level = to_byte(128 + (lum_at(x + dx, y + dy) - lum_at(x - dx, y - dy)) * amount);
      std::uint8_t* p = out.data() + (static_cast<std::size_t>(y) * in.width() + x) * 4;
      p[0] = p[1] = p[2] = level;
    }
  }
  return out;
}

// Dark lines on white where the picture changes, per channel (Sobel).
Image find_edges(const Image& in, const Values&, std::int64_t) {
  Image out = in;
  const auto at = [&](int x, int y, int c) {
    return in.data()[(static_cast<std::size_t>(std::clamp(y, 0, in.height() - 1)) * in.width() +
                      std::clamp(x, 0, in.width() - 1)) * 4 + c];
  };
  for (int y = 0; y < in.height(); ++y) {
    for (int x = 0; x < in.width(); ++x) {
      std::uint8_t* p = out.data() + (static_cast<std::size_t>(y) * in.width() + x) * 4;
      for (int c = 0; c < 3; ++c) {
        const int gx = at(x + 1, y - 1, c) + 2 * at(x + 1, y, c) + at(x + 1, y + 1, c) -
                       at(x - 1, y - 1, c) - 2 * at(x - 1, y, c) - at(x - 1, y + 1, c);
        const int gy = at(x - 1, y + 1, c) + 2 * at(x, y + 1, c) + at(x + 1, y + 1, c) -
                       at(x - 1, y - 1, c) - 2 * at(x, y - 1, c) - at(x + 1, y - 1, c);
        p[c] = to_byte(255 - std::sqrt(static_cast<double>(gx * gx + gy * gy)) / 4.0);
      }
    }
  }
  return out;
}

// Square blocks of each block's average colour.
Image mosaic(const Image& in, const Values& v, std::int64_t) {
  const int size = std::max(1, static_cast<int>(std::lround(v[0])));
  if (size == 1) return in;
  Buffer b = to_buffer(in);
  for (int by = 0; by < b.height; by += size) {
    for (int bx = 0; bx < b.width; bx += size) {
      const int w = std::min(size, b.width - bx), h = std::min(size, b.height - by);
      Premul sum;
      const float weight = 1.0f / (w * h);
      for (int y = by; y < by + h; ++y) {
        for (int x = bx; x < bx + w; ++x) add_scaled(sum, b.at(x, y), weight);
      }
      for (int y = by; y < by + h; ++y) {
        for (int x = bx; x < bx + w; ++x) b.at(x, y) = sum;
      }
    }
  }
  return to_image(b);
}

// Different grain on every frame: the noise depends on the frame number and
// the pixel, so a render gives the same frames every time.
Image add_noise(const Image& in, const Values& v, std::int64_t frame) {
  const int amount = std::clamp(static_cast<int>(std::lround(v[0])), 0, 255);
  if (amount == 0) return in;
  const std::uint64_t span = 2 * static_cast<std::uint64_t>(amount) + 1;
  Image out = in;
  for (int y = 0; y < out.height(); ++y) {
    for (int x = 0; x < out.width(); ++x) {
      std::uint8_t* p = out.data() + (static_cast<std::size_t>(y) * out.width() + x) * 4;
      if (p[3] == 0) continue;
      const std::uint64_t at = hash((static_cast<std::uint64_t>(frame) << 40) ^
                                    (static_cast<std::uint64_t>(x) << 20) ^ static_cast<std::uint64_t>(y));
      for (int c = 0; c < 3; ++c) {
        const int offset = static_cast<int>(hash(at + c) % span) - amount;
        p[c] = static_cast<std::uint8_t>(std::clamp(p[c] + offset, 0, 255));
      }
    }
  }
  return out;
}

// Rows slide sideways along a sine wave; sweeping the phase makes it move.
Image ripple(const Image& in, const Values& v, std::int64_t) {
  const double amplitude = v[0], wavelength = std::max(1.0, v[1]), phase = v[2] * kPi / 180.0;
  if (amplitude <= 0) return in;
  const Buffer b = to_buffer(in);
  Buffer out = b;
  for (int y = 0; y < b.height; ++y) {
    const double shift = amplitude * std::sin(2 * kPi * (y + 0.5) / wavelength + phase);
    for (int x = 0; x < b.width; ++x) out.at(x, y) = sample(b, x + 0.5 + shift, y + 0.5);
  }
  return to_image(out);
}

// The picture shrunk and repeated in an n x n grid.
Image replicate(const Image& in, const Values& v, std::int64_t) {
  const int n = std::max(1, static_cast<int>(std::lround(v[0])));
  if (n == 1) return in;
  Image out(in.width(), in.height());
  for (int row = 0; row < n; ++row) {
    for (int col = 0; col < n; ++col) {
      const int x0 = col * in.width() / n, x1 = (col + 1) * in.width() / n;
      const int y0 = row * in.height() / n, y1 = (row + 1) * in.height() / n;
      if (x1 > x0 && y1 > y0) out.blit(resized(in, x1 - x0, y1 - y0), {x0, y0});
    }
  }
  return out;
}

// Edges cut away to transparent, each as a percentage of the frame.
Image crop(const Image& in, const Values& v, std::int64_t) {
  const int w = in.width(), h = in.height();
  const int left = static_cast<int>(std::lround(w * v[0] / 100.0));
  const int top = static_cast<int>(std::lround(h * v[1] / 100.0));
  const int right = static_cast<int>(std::lround(w * v[2] / 100.0));
  const int bottom = static_cast<int>(std::lround(h * v[3] / 100.0));
  Image out(w, h);
  out.blit(in.cropped({left, top, w - left - right, h - top - bottom}), {left, top});
  return out;
}

Image flip_horizontal(const Image& in, const Values&, std::int64_t) { return in.flipped_horizontal(); }
Image flip_vertical(const Image& in, const Values&, std::int64_t) { return in.flipped_vertical(); }

// ---- Audio ----------------------------------------------------------------

// Each echo is the sound `delay` later at `decay` of the level before it.
void echo(std::vector<float>& s, int rate, const Values& start, const Values&) {
  const std::size_t delay = static_cast<std::size_t>(std::lround(start[0] / 1000.0 * rate)) * 2;
  const float decay = static_cast<float>(start[1] / 100.0);
  if (delay == 0 || decay <= 0) return;
  for (std::size_t i = delay; i < s.size(); ++i) s[i] += decay * s[i - delay];
}

// Moves the sound between the speakers, sweeping from start to end.
void pan(std::vector<float>& s, int, const Values& start, const Values& end) {
  const std::size_t frames = s.size() / 2;
  for (std::size_t i = 0; i < frames; ++i) {
    const double t = frames > 1 ? static_cast<double>(i) / (frames - 1) : 0.0;
    const double position = (start[0] + (end[0] - start[0]) * t) / 100.0;
    s[2 * i] *= static_cast<float>(position > 0 ? 1.0 - position : 1.0);
    s[2 * i + 1] *= static_cast<float>(position < 0 ? 1.0 + position : 1.0);
  }
}

void backwards(std::vector<float>& s, int, const Values&, const Values&) {
  const std::size_t frames = s.size() / 2;
  for (std::size_t i = 0; i < frames / 2; ++i) {
    std::swap(s[2 * i], s[2 * (frames - 1 - i)]);
    std::swap(s[2 * i + 1], s[2 * (frames - 1 - i) + 1]);
  }
}

// The left channel in both speakers.
void fill_left(std::vector<float>& s, int, const Values&, const Values&) {
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) s[i + 1] = s[i];
}

// The right channel in both speakers.
void fill_right(std::vector<float>& s, int, const Values&, const Values&) {
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) s[i] = s[i + 1];
}

// ---- Registries -----------------------------------------------------------

FilterParam param(std::string name, double min, double max, double value, std::string unit = "",
                  bool whole = false) {
  return {std::move(name), min, max, value, std::move(unit), whole};
}

FilterParam level(std::string name, double value) { return param(std::move(name), 0, 255, value, "", true); }

std::vector<FilterDef> make_video_filters() {
  return {
      {"brightness_contrast", "Brightness & Contrast",
       {param("Brightness", -100, 100, 0), param("Contrast", -100, 100, 0)}, brightness_contrast},
      {"levels", "Levels",
       {level("Input black", 0), level("Input white", 255), param("Gamma", 0.1, 5, 1),
        level("Output black", 0), level("Output white", 255)},
       levels},
      {"gamma_correction", "Gamma Correction", {param("Gamma", 0.1, 5, 1)}, gamma_correction},
      {"hue_saturation", "Hue & Saturation",
       {param("Hue", -180, 180, 0, "°"), param("Saturation", -100, 100, 0),
        param("Lightness", -100, 100, 0)},
       hue_saturation},
      {"color_balance", "Color Balance",
       {param("Red", -100, 100, 0, "%"), param("Green", -100, 100, 0, "%"),
        param("Blue", -100, 100, 0, "%")},
       color_balance},
      {"tint", "Tint",
       {level("Red", 255), level("Green", 170), level("Blue", 80), param("Amount", 0, 100, 50, "%")},
       tint},
      {"black_white", "Black & White", {}, black_white},
      {"invert", "Invert", {}, invert},
      {"posterize", "Posterize", {param("Levels", 2, 32, 4, "", true)}, posterize},
      {"threshold", "Threshold", {level("Level", 128)}, threshold},
      {"solarize", "Solarize", {level("Threshold", 128)}, solarize},
      {"gaussian_blur", "Gaussian Blur", {param("Radius", 0, 20, 2, "px")}, gaussian_blur},
      {"camera_blur", "Camera Blur", {param("Amount", 0, 20, 3, "px", true)}, camera_blur},
      {"motion_blur", "Motion Blur",
       {param("Angle", -180, 180, 0, "°"), param("Distance", 1, 60, 10, "px", true)}, motion_blur},
      {"sharpen", "Sharpen", {param("Amount", 0, 500, 100, "%")}, sharpen},
      {"emboss", "Emboss",
       {param("Angle", 0, 360, 135, "°"), param("Height", 1, 10, 2, "px", true),
        param("Amount", 0, 500, 100, "%")},
       emboss},
      {"find_edges", "Find Edges", {}, find_edges},
      {"mosaic", "Mosaic", {param("Block size", 1, 64, 8, "px", true)}, mosaic},
      {"add_noise", "Add Noise", {param("Amount", 0, 255, 25, "", true)}, add_noise},
      {"ripple", "Ripple",
       {param("Amplitude", 0, 30, 5, "px"), param("Wavelength", 4, 200, 40, "px"),
        param("Phase", 0, 360, 0, "°")},
       ripple},
      {"replicate", "Replicate", {param("Copies", 1, 8, 2, "", true)}, replicate},
      {"crop", "Crop",
       {param("Left", 0, 50, 0, "%"), param("Top", 0, 50, 0, "%"), param("Right", 0, 50, 0, "%"),
        param("Bottom", 0, 50, 0, "%")},
       crop},
      {"flip_horizontal", "Horizontal Flip", {}, flip_horizontal},
      {"flip_vertical", "Vertical Flip", {}, flip_vertical},
  };
}

std::vector<AudioFilterDef> make_audio_filters() {
  return {
      {"echo", "Echo", {param("Delay", 10, 1000, 250, "ms"), param("Decay", 0, 90, 50, "%")}, echo},
      {"pan", "Pan", {param("Position", -100, 100, 0)}, pan},
      {"backwards", "Backwards", {}, backwards},
      {"fill_left", "Fill Left", {}, fill_left},
      {"fill_right", "Fill Right", {}, fill_right},
  };
}

// A parameter's value, or its default if the instance is short of values.
Values complete(const Values& values, const std::vector<FilterParam>& params) {
  Values out(params.size());
  for (std::size_t i = 0; i < params.size(); ++i) {
    out[i] = i < values.size() ? values[i] : params[i].value;
  }
  return out;
}

}  // namespace

const std::vector<FilterDef>& video_filters() {
  static const std::vector<FilterDef> registry = make_video_filters();
  return registry;
}

const std::vector<AudioFilterDef>& audio_filters() {
  static const std::vector<AudioFilterDef> registry = make_audio_filters();
  return registry;
}

const FilterDef* find_video_filter(const std::string& id) {
  for (const FilterDef& f : video_filters()) {
    if (f.id == id) return &f;
  }
  return nullptr;
}

const AudioFilterDef* find_audio_filter(const std::string& id) {
  for (const AudioFilterDef& f : audio_filters()) {
    if (f.id == id) return &f;
  }
  return nullptr;
}

FilterInstance default_instance(const std::string& id) {
  FilterInstance f;
  f.id = id;
  const std::vector<FilterParam>* params = nullptr;
  if (const FilterDef* v = find_video_filter(id)) params = &v->params;
  if (const AudioFilterDef* a = find_audio_filter(id)) params = &a->params;
  if (params) {
    for (const FilterParam& p : *params) {
      f.start.push_back(p.value);
      f.end.push_back(p.value);
    }
  }
  return f;
}

std::vector<double> filter_values(const FilterInstance& f, const std::vector<FilterParam>& params,
                                  double t) {
  const Values start = complete(f.start, params), end = complete(f.end, params);
  Values out(params.size());
  for (std::size_t i = 0; i < params.size(); ++i) {
    out[i] = start[i] + (end[i] - start[i]) * t;
    if (params[i].whole) out[i] = std::round(out[i]);
  }
  return out;
}

Image apply_video_filters(const Image& picture, const std::vector<FilterInstance>& filters,
                          double t, std::int64_t frame) {
  Image out = picture;
  for (const FilterInstance& f : filters) {
    const FilterDef* def = find_video_filter(f.id);
    if (!def || out.empty()) continue;
    out = def->apply(out, filter_values(f, def->params, t), frame);
  }
  return out;
}

void apply_audio_filters(std::vector<float>& stereo, int sample_rate,
                         const std::vector<FilterInstance>& filters) {
  for (const FilterInstance& f : filters) {
    const AudioFilterDef* def = find_audio_filter(f.id);
    if (!def) continue;
    def->apply(stereo, sample_rate, filter_values(f, def->params, 0.0),
               filter_values(f, def->params, 1.0));
  }
}

}  // namespace mpp

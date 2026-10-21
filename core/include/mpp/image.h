#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mpp/geometry.h"

namespace mpp {

// 8-bit straight (non-premultiplied) RGBA.
struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
  bool operator!=(const Color& o) const { return !(*this == o); }
};

inline constexpr Color kTransparent{0, 0, 0, 0};
inline constexpr Color kWhite{255, 255, 255, 255};
inline constexpr Color kBlack{0, 0, 0, 255};

enum class Rotation { CW90, CCW90, R180 };

// Owning RGBA8 pixel buffer. Row-major, origin at the top-left.
class Image {
 public:
  static constexpr int kChannels = 4;

  Image() = default;
  Image(int width, int height, Color fill = kTransparent);

  int width() const { return width_; }
  int height() const { return height_; }
  bool empty() const { return width_ == 0 || height_ == 0; }
  Rect bounds() const { return {0, 0, width_, height_}; }
  bool contains(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
  }

  const std::uint8_t* data() const { return data_.data(); }
  std::uint8_t* data() { return data_.data(); }
  std::size_t size_bytes() const { return data_.size(); }

  // Bounds-checked; throw std::out_of_range.
  Color pixel(int x, int y) const;
  void set_pixel(int x, int y, Color c);

  void fill(Color c);
  void fill(Rect r, Color c);  // r is clipped to the image

  // Copies src with its top-left at dst, clipped. Replaces pixels, no blending.
  void blit(const Image& src, Point dst);

  // Returns the pixels under r. Parts of r outside the image get `outside`.
  Image cropped(Rect r, Color outside = kTransparent) const;

  Image flipped_horizontal() const;
  Image flipped_vertical() const;
  Image rotated(Rotation rotation) const;

  bool operator==(const Image& o) const {
    return width_ == o.width_ && height_ == o.height_ && data_ == o.data_;
  }
  bool operator!=(const Image& o) const { return !(*this == o); }

 private:
  std::size_t index(int x, int y) const {
    return (static_cast<std::size_t>(y) * width_ + x) * kChannels;
  }

  int width_ = 0;
  int height_ = 0;
  std::vector<std::uint8_t> data_;
};

}  // namespace mpp

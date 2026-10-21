#include "mpp/image.h"

#include <cstring>
#include <stdexcept>

namespace mpp {

Image::Image(int width, int height, Color fill_color) {
  if (width < 0 || height < 0) {
    throw std::invalid_argument("Image dimensions must be non-negative");
  }
  width_ = width;
  height_ = height;
  data_.resize(static_cast<std::size_t>(width) * height * kChannels);
  fill(fill_color);
}

Color Image::pixel(int x, int y) const {
  if (!contains(x, y)) throw std::out_of_range("Image::pixel out of range");
  const std::uint8_t* p = &data_[index(x, y)];
  return {p[0], p[1], p[2], p[3]};
}

void Image::set_pixel(int x, int y, Color c) {
  if (!contains(x, y)) throw std::out_of_range("Image::set_pixel out of range");
  std::uint8_t* p = &data_[index(x, y)];
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

void Image::fill(Color c) { fill(bounds(), c); }

void Image::fill(Rect r, Color c) {
  r = r.intersect(bounds());
  if (r.empty()) return;
  const std::uint8_t px[kChannels] = {c.r, c.g, c.b, c.a};
  for (int y = r.y; y < r.bottom(); ++y) {
    std::uint8_t* row = &data_[index(r.x, y)];
    for (int x = 0; x < r.width; ++x) {
      std::memcpy(row + x * kChannels, px, kChannels);
    }
  }
}

void Image::blit(const Image& src, Point dst) {
  const Rect r = Rect{dst.x, dst.y, src.width_, src.height_}.intersect(bounds());
  if (r.empty()) return;
  for (int y = r.y; y < r.bottom(); ++y) {
    std::memcpy(&data_[index(r.x, y)],
                &src.data_[src.index(r.x - dst.x, y - dst.y)],
                static_cast<std::size_t>(r.width) * kChannels);
  }
}

Image Image::cropped(Rect r, Color outside) const {
  if (r.width < 0 || r.height < 0) {
    throw std::invalid_argument("Image::cropped needs a non-negative size");
  }
  Image out(r.width, r.height, outside);
  out.blit(*this, {-r.x, -r.y});
  return out;
}

Image Image::flipped_horizontal() const {
  Image out(width_, height_);
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      std::memcpy(&out.data_[out.index(width_ - 1 - x, y)], &data_[index(x, y)],
                  kChannels);
    }
  }
  return out;
}

Image Image::flipped_vertical() const {
  Image out(width_, height_);
  const std::size_t row_bytes = static_cast<std::size_t>(width_) * kChannels;
  for (int y = 0; y < height_; ++y) {
    std::memcpy(out.data_.data() + row_bytes * (height_ - 1 - y),
                data_.data() + row_bytes * y, row_bytes);
  }
  return out;
}

Image Image::rotated(Rotation rotation) const {
  const bool quarter = rotation != Rotation::R180;
  Image out(quarter ? height_ : width_, quarter ? width_ : height_);
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      int dx = 0, dy = 0;
      switch (rotation) {
        case Rotation::CW90:
          dx = height_ - 1 - y;
          dy = x;
          break;
        case Rotation::CCW90:
          dx = y;
          dy = width_ - 1 - x;
          break;
        case Rotation::R180:
          dx = width_ - 1 - x;
          dy = height_ - 1 - y;
          break;
      }
      std::memcpy(&out.data_[out.index(dx, dy)], &data_[index(x, y)], kChannels);
    }
  }
  return out;
}

}  // namespace mpp

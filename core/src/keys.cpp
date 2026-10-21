#include "mpp/keys.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "mpp/compositing.h"

namespace mpp {

namespace {

std::uint8_t to_byte(double v) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

// How much of a pixel to keep given how far it is from dropping out:
// nothing within `similarity`, all of it past `similarity + blend`.
double keep_by_distance(double distance, double similarity, double blend) {
  if (distance <= similarity) return 0.0;
  if (blend <= 0.0 || distance >= similarity + blend) return 1.0;
  return (distance - similarity) / blend;
}

// Distance between two colours, 0 (same) to 1 (black to white).
double colour_distance(const std::uint8_t* p, Color c) {
  const double dr = p[0] - c.r, dg = p[1] - c.g, db = p[2] - c.b;
  return std::sqrt(dr * dr + dg * dg + db * db) / (255.0 * std::sqrt(3.0));
}

}  // namespace

const std::vector<std::pair<KeyType, std::string>>& key_types() {
  static const std::vector<std::pair<KeyType, std::string>> types = {
      {KeyType::None, "None"},
      {KeyType::Alpha, "Alpha Channel"},
      {KeyType::Chroma, "Chroma"},
      {KeyType::Luminance, "Luminance"},
      {KeyType::BlueScreen, "Blue Screen"},
      {KeyType::GreenScreen, "Green Screen"},
      {KeyType::Multiply, "Multiply"},
      {KeyType::Screen, "Screen"},
  };
  return types;
}

const char* key_name(KeyType type) {
  for (const auto& [t, name] : key_types()) {
    if (t == type) return name.c_str();
  }
  return "";
}

bool key_uses_color(KeyType type) { return type == KeyType::Chroma; }

bool key_uses_similarity(KeyType type) {
  return type == KeyType::Chroma || type == KeyType::Luminance || type == KeyType::BlueScreen ||
         type == KeyType::GreenScreen;
}

Image keyed(const Image& picture, const TransparencyKey& key) {
  Image out = picture;
  std::uint8_t* p = out.data();
  for (std::size_t i = 0; i < out.size_bytes(); i += 4) {
    double keep = 1.0;
    switch (key.type) {
      case KeyType::None:
      case KeyType::Multiply:
      case KeyType::Screen:
        continue;
      case KeyType::Alpha:
        keep = p[i + 3] / 255.0;
        break;
      case KeyType::Chroma:
        keep = keep_by_distance(colour_distance(p + i, key.color), key.similarity, key.blend);
        break;
      case KeyType::BlueScreen:
        keep = keep_by_distance(colour_distance(p + i, {0, 0, 255, 255}), key.similarity, key.blend);
        break;
      case KeyType::GreenScreen:
        keep = keep_by_distance(colour_distance(p + i, {0, 255, 0, 255}), key.similarity, key.blend);
        break;
      case KeyType::Luminance: {
        const double luma = (p[i] * 299 + p[i + 1] * 587 + p[i + 2] * 114) / 255000.0;
        keep = keep_by_distance(luma, key.similarity, key.blend);
        break;
      }
    }
    if (key.reverse) keep = 1.0 - keep;
    // The colour keys work on the picture as if opaque, then keep its own
    // transparency too (a letterbox stays clear).
    const double own = key.type == KeyType::Alpha ? 1.0 : p[i + 3] / 255.0;
    p[i + 3] = to_byte(keep * own * 255.0);
  }
  return out;
}

void composite_superimpose(Image& base, const Image& picture, const TransparencyKey& key,
                           double opacity) {
  if (base.width() != picture.width() || base.height() != picture.height()) {
    throw std::invalid_argument("Pictures must be the same size to combine");
  }
  opacity = std::clamp(opacity, 0.0, 1.0);
  if (key.type != KeyType::Multiply && key.type != KeyType::Screen) {
    composite_over(base, keyed(picture, key), opacity);
    return;
  }
  std::uint8_t* b = base.data();
  const std::uint8_t* t = picture.data();
  for (std::size_t i = 0; i < base.size_bytes(); i += 4) {
    const double amount = t[i + 3] / 255.0 * opacity;
    if (amount <= 0.0) continue;
    for (int c = 0; c < 3; ++c) {
      const double below = b[i + c], above = t[i + c];
      const double blended = key.type == KeyType::Multiply
                                 ? below * above / 255.0
                                 : 255.0 - (255.0 - below) * (255.0 - above) / 255.0;
      b[i + c] = to_byte(below + (blended - below) * amount);
    }
  }
}

}  // namespace mpp

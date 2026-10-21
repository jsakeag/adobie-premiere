#include <cctype>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/keys.h"
#include "mpp/project.h"

using mpp::Color;
using mpp::Image;
using mpp::KeyType;
using mpp::TransparencyKey;

namespace {

TransparencyKey key_of(KeyType type) {
  TransparencyKey k;
  k.type = type;
  return k;
}

// A Super picture with something for every key: blue screen on the left,
// a dark-to-light ramp in the middle, green screen on the right, a red
// square, and a transparent strip along the bottom like a letterbox.
Image super_picture() {
  Image img(96, 48);
  for (int y = 0; y < 48; ++y) {
    for (int x = 0; x < 96; ++x) {
      Color c;
      if (x < 32) c = {0, 0, 255, 255};
      else if (x < 64) c = {static_cast<std::uint8_t>((x - 32) * 8), static_cast<std::uint8_t>((x - 32) * 8),
                            static_cast<std::uint8_t>((x - 32) * 8), 255};
      else c = {0, 255, 0, 255};
      img.set_pixel(x, y, c);
    }
  }
  img.fill({10, 10, 12, 12}, {255, 0, 0, 255});
  img.fill({74, 10, 12, 12}, {255, 0, 0, 160});  // half see-through
  img.fill({0, 40, 96, 8}, mpp::kTransparent);
  return img;
}

Image base_picture() {
  Image img(96, 48);
  for (int y = 0; y < 48; ++y) {
    for (int x = 0; x < 96; ++x) {
      const bool odd = (x / 6 + y / 6) % 2;
      img.set_pixel(x, y, odd ? Color{200, 200, 200, 255} : Color{90, 90, 90, 255});
    }
  }
  return img;
}

}  // namespace

TEST_CASE("chroma drops the chosen colour with a soft edge", "[keys]") {
  TransparencyKey k = key_of(KeyType::Chroma);
  k.color = {0, 0, 255, 255};
  k.similarity = 0.1;
  k.blend = 0.2;
  Image img(3, 1);
  img.set_pixel(0, 0, {0, 0, 255, 255});    // the key colour
  img.set_pixel(1, 0, {0, 0, 150, 255});    // distance 105/441.7 = 0.238
  img.set_pixel(2, 0, {255, 255, 0, 255});  // far away
  const Image out = mpp::keyed(img, k);
  CHECK(out.pixel(0, 0).a == 0);
  CHECK(out.pixel(1, 0).a == 176);  // (0.238 - 0.1) / 0.2 = 0.69
  CHECK(out.pixel(2, 0).a == 255);
  k.reverse = true;
  CHECK(mpp::keyed(img, k).pixel(0, 0).a == 255);
  CHECK(mpp::keyed(img, k).pixel(2, 0).a == 0);
}

TEST_CASE("luminance drops dark areas; blue and green screens drop their colour", "[keys]") {
  const Image pic = super_picture();
  TransparencyKey luma = key_of(KeyType::Luminance);
  luma.similarity = 0.3;
  luma.blend = 0;
  const Image l = mpp::keyed(pic, luma);
  CHECK(l.pixel(33, 20).a == 0);    // grey 8: dark
  CHECK(l.pixel(60, 20).a == 255);  // grey 224: light
  CHECK(mpp::keyed(pic, key_of(KeyType::BlueScreen)).pixel(5, 30).a == 0);
  CHECK(mpp::keyed(pic, key_of(KeyType::BlueScreen)).pixel(70, 30).a == 255);
  CHECK(mpp::keyed(pic, key_of(KeyType::GreenScreen)).pixel(70, 30).a == 0);
  // A letterbox stays clear under every colour key.
  CHECK(mpp::keyed(pic, key_of(KeyType::Chroma)).pixel(50, 44).a == 0);
  CHECK(mpp::keyed(pic, key_of(KeyType::None)).pixel(50, 44).a == 0);
  CHECK(mpp::keyed(pic, key_of(KeyType::Alpha)).pixel(80, 15).a == 160);
}

TEST_CASE("multiply darkens and screen lightens", "[keys]") {
  Image base(1, 1, {200, 100, 50, 255});
  mpp::composite_superimpose(base, Image(1, 1, {128, 128, 255, 255}), key_of(KeyType::Multiply), 1.0);
  CHECK(base.pixel(0, 0) == Color{100, 50, 50, 255});
  Image light(1, 1, {200, 100, 0, 255});
  mpp::composite_superimpose(light, Image(1, 1, {255, 128, 0, 255}), key_of(KeyType::Screen), 0.5);
  // Screen gives (255, 177.8, 0); half of the way there.
  CHECK(light.pixel(0, 0) == Color{228, 139, 0, 255});
}

TEST_CASE("fades run straight between handles", "[keys][fade]") {
  CHECK(mpp::fade_level({}, 5) == 1.0);
  const std::vector<mpp::FadePoint> fade = {{10, 0.0}, {20, 1.0}, {30, 0.5}};
  CHECK(mpp::fade_level(fade, 0) == 0.0);
  CHECK(mpp::fade_level(fade, 15) == 0.5);
  CHECK(mpp::fade_level(fade, 25) == 0.75);
  CHECK(mpp::fade_level(fade, 99) == 0.5);
}

TEST_CASE("each key matches its golden", "[keys][golden]") {
  const Image pic = super_picture();
  for (const auto& [type, name] : mpp::key_types()) {
    TransparencyKey k = key_of(type);
    k.color = {255, 0, 0, 255};  // Chroma drops the red squares
    Image sheet(96 * 2 + 2, 48, {255, 0, 255, 255});
    Image full = base_picture();
    mpp::composite_superimpose(full, pic, k, 1.0);
    Image half = base_picture();
    mpp::composite_superimpose(half, pic, k, 0.5);
    sheet.blit(full, {0, 0});
    sheet.blit(half, {98, 0});
    std::string file = "key_" + name;
    for (char& c : file) c = c == ' ' ? '_' : static_cast<char>(std::tolower(c));
    INFO(name);
    CHECK(golden_mismatch(sheet, file) == "");
  }
}

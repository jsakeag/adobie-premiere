#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/filters.h"

using mpp::Color;
using mpp::FilterInstance;
using mpp::Image;

namespace {

Image run(const char* id, const Image& in, std::vector<double> values, std::int64_t frame = 0) {
  const mpp::FilterDef* def = mpp::find_video_filter(id);
  REQUIRE(def);
  return def->apply(in, values, frame);
}

// A picture with colour, detail and a transparent corner for the goldens.
Image filter_picture() {
  Image img(64, 48);
  for (int y = 0; y < 48; ++y) {
    for (int x = 0; x < 64; ++x) {
      img.set_pixel(x, y, {static_cast<std::uint8_t>(x * 4), static_cast<std::uint8_t>(y * 5),
                           static_cast<std::uint8_t>(((x / 8 + y / 8) % 2) ? 220 : 40), 255});
    }
  }
  img.fill({20, 14, 24, 20}, {250, 240, 30, 255});
  img.fill({28, 20, 8, 8}, {10, 10, 10, 255});
  img.fill({56, 0, 8, 8}, mpp::kTransparent);
  return img;
}

}  // namespace

TEST_CASE("there are 24 video filters and 5 audio filters", "[filters]") {
  CHECK(mpp::video_filters().size() == 24);
  CHECK(mpp::audio_filters().size() == 5);
  std::set<std::string> ids;
  for (const auto& f : mpp::video_filters()) {
    ids.insert(f.id);
    CHECK(mpp::find_video_filter(f.id) == &f);
    for (const auto& p : f.params) {
      INFO(f.id << " " << p.name);
      CHECK(p.min <= p.value);
      CHECK(p.value <= p.max);
    }
  }
  for (const auto& f : mpp::audio_filters()) ids.insert(f.id);
  CHECK(ids.size() == 29);
  CHECK(mpp::find_video_filter("echo") == nullptr);
  CHECK(mpp::default_instance("posterize").start == std::vector<double>{4});
}

TEST_CASE("values blend from start to end, whole numbers rounded", "[filters]") {
  const mpp::FilterDef* motion = mpp::find_video_filter("motion_blur");
  FilterInstance f{"motion_blur", {0, 1}, {90, 10}};
  CHECK(mpp::filter_values(f, motion->params, 0.0) == std::vector<double>{0, 1});
  CHECK(mpp::filter_values(f, motion->params, 0.5) == std::vector<double>{45, 6});  // 5.5 rounds up
  CHECK(mpp::filter_values(f, motion->params, 1.0) == std::vector<double>{90, 10});
  // Missing values take the defaults.
  CHECK(mpp::filter_values({"motion_blur", {}, {}}, motion->params, 0.3) == std::vector<double>{0, 10});
}

TEST_CASE("defaults that should change nothing don't", "[filters]") {
  const Image pic = filter_picture();
  for (const char* id : {"brightness_contrast", "levels", "gamma_correction", "hue_saturation",
                         "color_balance", "crop"}) {
    INFO(id);
    const FilterInstance f = mpp::default_instance(id);
    CHECK(mpp::apply_video_filters(pic, {f}, 0.0, 0) == pic);
  }
}

TEST_CASE("colour filters have exact values", "[filters]") {
  const Image px(1, 1, {200, 100, 50, 255});
  CHECK(run("invert", px, {}).pixel(0, 0) == Color{55, 155, 205, 255});
  CHECK(run("black_white", px, {}).pixel(0, 0) == Color{124, 124, 124, 255});  // 59.8+58.7+5.7
  CHECK(run("posterize", px, {2}).pixel(0, 0) == Color{255, 0, 0, 255});
  CHECK(run("threshold", px, {124}).pixel(0, 0) == mpp::kWhite);
  CHECK(run("threshold", px, {125}).pixel(0, 0) == mpp::kBlack);
  CHECK(run("solarize", px, {128}).pixel(0, 0) == Color{55, 100, 50, 255});
  CHECK(run("color_balance", px, {10, -100, 0}).pixel(0, 0) == Color{226, 0, 50, 255});
  CHECK(run("gamma_correction", Image(1, 1, {64, 64, 64, 255}), {2}).pixel(0, 0).r == 128);
}

TEST_CASE("geometry filters move pixels exactly", "[filters]") {
  Image img(4, 2);
  img.set_pixel(0, 0, {255, 0, 0, 255});
  CHECK(run("flip_horizontal", img, {}).pixel(3, 0).r == 255);
  CHECK(run("flip_vertical", img, {}).pixel(0, 1).r == 255);
  const Image cropped = run("crop", Image(10, 10, mpp::kWhite), {20, 0, 0, 50});
  CHECK(cropped.pixel(1, 1).a == 0);
  CHECK(cropped.pixel(5, 2) == mpp::kWhite);
  CHECK(cropped.pixel(5, 7).a == 0);
  // Mosaic averages each block.
  Image stripes(2, 1);
  stripes.set_pixel(0, 0, {0, 0, 0, 255});
  stripes.set_pixel(1, 0, {200, 0, 0, 255});
  CHECK(run("mosaic", stripes, {2}).pixel(0, 0).r == 100);
}

TEST_CASE("noise differs from frame to frame but repeats exactly", "[filters]") {
  const Image grey(16, 16, {128, 128, 128, 255});
  const Image a = run("add_noise", grey, {40}, 3);
  CHECK(a == run("add_noise", grey, {40}, 3));
  CHECK(a != run("add_noise", grey, {40}, 4));
  CHECK(a != grey);
}

TEST_CASE("audio filters", "[filters][audio]") {
  const auto apply = [](const char* id, std::vector<float> s, FilterInstance f) {
    f.id = id;
    mpp::apply_audio_filters(s, 1000, {f});
    return s;
  };
  const std::vector<float> ramp = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
  CHECK(apply("backwards", ramp, {}) == std::vector<float>{0.5f, 0.6f, 0.3f, 0.4f, 0.1f, 0.2f});
  CHECK(apply("fill_left", ramp, {}) == std::vector<float>{0.1f, 0.1f, 0.3f, 0.3f, 0.5f, 0.5f});
  CHECK(apply("fill_right", ramp, {}) == std::vector<float>{0.2f, 0.2f, 0.4f, 0.4f, 0.6f, 0.6f});
  // Pan sweeps from hard left to hard right.
  const std::vector<float> panned = apply("pan", std::vector<float>(6, 1.0f), {"", {-100}, {100}});
  CHECK(panned == std::vector<float>{1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f});
  // A 2 ms echo at 50% at 1 kHz: two frames later, half as loud, and again.
  std::vector<float> click(12, 0.0f);
  click[0] = click[1] = 1.0f;
  const std::vector<float> echoed = apply("echo", click, {"", {2, 50}, {2, 50}});
  CHECK(echoed[4] == 0.5f);
  CHECK(echoed[8] == 0.25f);
  CHECK(echoed[2] == 0.0f);
}

TEST_CASE("each video filter matches its golden", "[filters][golden]") {
  // Two panels: the defaults, then every value three quarters of the way
  // from its minimum to its maximum.
  const Image pic = filter_picture();
  for (const auto& def : mpp::video_filters()) {
    Image sheet(64 * 2 + 2, 48, {255, 0, 255, 255});
    std::vector<double> defaults, strong;
    for (const auto& p : def.params) {
      defaults.push_back(p.value);
      strong.push_back(p.whole ? std::round(p.min + (p.max - p.min) * 0.75) : p.min + (p.max - p.min) * 0.75);
    }
    sheet.blit(def.apply(pic, defaults, 7), {0, 0});
    sheet.blit(def.apply(pic, strong, 7), {66, 0});
    INFO(def.id);
    CHECK(golden_mismatch(sheet, "filter_" + def.id) == "");
  }
}

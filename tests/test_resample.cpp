#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/resample.h"

using mpp::Color;
using mpp::Image;

TEST_CASE("resizing keeps flat colour flat", "[resample]") {
  const Image flat(37, 23, {10, 200, 90, 255});
  for (auto [w, h] : {std::pair{5, 3}, std::pair{100, 80}, std::pair{37, 11}}) {
    const Image out = mpp::resized(flat, w, h);
    REQUIRE(out.width() == w);
    REQUIRE(out.height() == h);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) REQUIRE(out.pixel(x, y) == flat.pixel(0, 0));
    }
  }
}

TEST_CASE("shrinking by two averages pairs", "[resample]") {
  Image src(4, 1);
  src.set_pixel(0, 0, {0, 0, 0, 255});
  src.set_pixel(1, 0, {200, 0, 0, 255});
  src.set_pixel(2, 0, {100, 0, 0, 255});
  src.set_pixel(3, 0, {100, 0, 0, 255});
  const Image out = mpp::resized(src, 2, 1);
  // A tent two pixels wide on each side, centred between each pair:
  // (0*.25 + 0*.75 + 200*.75 + 100*.25) / 2 and (200*.25 + 100*1.75) / 2,
  // the edge pixel repeating past the end.
  CHECK(out.pixel(0, 0).r == 88);
  CHECK(out.pixel(1, 0).r == 113);
}

TEST_CASE("transparent pixels don't bleed colour", "[resample]") {
  Image src(2, 1);
  src.set_pixel(0, 0, {255, 0, 0, 255});
  src.set_pixel(1, 0, {0, 255, 0, 0});  // invisible green
  const Image out = mpp::resized(src, 1, 1);
  CHECK(out.pixel(0, 0).r == 255);
  CHECK(out.pixel(0, 0).g == 0);
  CHECK(out.pixel(0, 0).a == 128);
}

TEST_CASE("fit_into letterboxes and keeps the aspect ratio", "[resample]") {
  const Image wide(200, 100, {0, 0, 255, 255});
  const Image out = mpp::fit_into(wide, 160, 120);
  CHECK(mpp::fitted_size(200, 100, 160, 120).x == 160);
  CHECK(mpp::fitted_size(200, 100, 160, 120).y == 80);
  CHECK(out.pixel(80, 10) == mpp::kBlack);
  CHECK(out.pixel(80, 60) == Color{0, 0, 255, 255});
  CHECK(out.pixel(80, 109) == mpp::kBlack);
}

TEST_CASE("resampling a test pattern matches the golden", "[resample][golden]") {
  const Image pattern = make_test_pattern(64, 48);
  Image sheet(64 + 160 + 20, 120, mpp::kWhite);
  sheet.blit(mpp::resized(pattern, 20, 15), {0, 0});
  sheet.blit(mpp::resized(pattern, 160, 120), {64, 0});
  sheet.blit(mpp::fit_into(pattern, 20, 40, {255, 0, 255, 255}), {224, 0});
  CHECK(golden_mismatch(sheet, "resample_pattern") == "");
}

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "mpp/compositing.h"
#include "mpp/editor.h"
#include "mpp/renderer.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

using mpp::Color;
using mpp::Track;

namespace {

// counter.mov frame N: (N*8, 255-N*8, 64).
Color counter(int n) {
  return {static_cast<std::uint8_t>(n * 8), static_cast<std::uint8_t>(255 - n * 8), 64, 255};
}

}  // namespace

TEST_CASE("compositing mixes by alpha and opacity", "[render]") {
  mpp::Image base(2, 1, {0, 0, 0, 255});
  mpp::Image top(2, 1, {200, 100, 0, 255});
  top.set_pixel(1, 0, {200, 100, 0, 0});
  mpp::composite_over(base, top, 0.5);
  CHECK(base.pixel(0, 0) == Color{100, 50, 0, 255});
  CHECK(base.pixel(1, 0) == Color{0, 0, 0, 255});

  mpp::Image clear(1, 1, mpp::kTransparent);
  mpp::composite_over(clear, mpp::Image(1, 1, {255, 0, 0, 128}));
  CHECK(clear.pixel(0, 0) == Color{255, 0, 0, 128});

  CHECK(mpp::mix(mpp::Image(1, 1, mpp::kBlack), mpp::Image(1, 1, mpp::kWhite), 0.25).pixel(0, 0) ==
        Color{64, 64, 64, 255});
  CHECK_THROWS(mpp::composite_over(base, mpp::Image(1, 1)));
}

TEST_CASE("the program shows Video A over Video B over black", "[render]") {
  mpp::ProjectSettings settings;
  settings.width = 160;
  settings.height = 120;
  settings.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov", "still.png"}, settings));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 10, 4, 14));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoB, 0, 0, 30));
  mpp::MediaCache cache;
  const mpp::Project& p = e.project();

  // Frame 12 is 2 into the clip, which starts at source frame 4.
  CHECK(mpp::render_frame(p, cache, 12).pixel(80, 60) == counter(6));
  CHECK(mpp::render_frame(p, cache, 10).pixel(0, 0) == counter(4));
  // Before and after it, Video B's still: letterboxed 2:1 into 4:3.
  const mpp::Image still = mpp::render_frame(p, cache, 5);
  CHECK(still.pixel(20, 60) == Color{0, 0, 255, 255});
  CHECK(still.pixel(140, 60) == Color{255, 255, 0, 255});
  CHECK(still.pixel(80, 5) == mpp::kBlack);
  CHECK(mpp::render_frame(p, cache, 20).pixel(140, 60) == Color{255, 255, 0, 255});
  // Nothing anywhere: black.
  CHECK(mpp::render_frame(p, cache, 40).pixel(80, 60) == mpp::kBlack);
}

TEST_CASE("a clip with alpha shows black through on Video A", "[render]") {
  mpp::ProjectSettings settings;
  settings.width = 160;
  settings.height = 120;
  mpp::Editor e(project_with({"alpha.mov"}, settings));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 10));
  mpp::MediaCache cache;
  const mpp::Image frame = mpp::render_frame(e.project(), cache, 3);
  CHECK(frame.pixel(10, 60) == Color{255, 0, 0, 255});
  CHECK(frame.pixel(150, 60) == mpp::kBlack);
}

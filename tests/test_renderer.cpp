#include <cmath>
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

TEST_CASE("a transition mixes Video A into Video B", "[render][transitions]") {
  mpp::ProjectSettings settings;
  settings.width = 160;
  settings.height = 120;
  settings.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov", "still.png"}, settings));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 20));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoB, 10, 0, 30));
  e.execute(std::make_unique<mpp::AddTransitionCommand>("cross_dissolve", 10, 10));
  mpp::MediaCache cache;
  const mpp::Project& p = e.project();
  // Frame 14 is the fifth of ten: 45% of the way to B. B's still is blue
  // on the left, so (0, 0, 255) mixes with counter frame 14.
  const mpp::Color a = counter(14);
  const mpp::Color mixed = mpp::render_frame(p, cache, 14).pixel(20, 60);
  CHECK(mixed.r == std::lround(a.r * 0.55));
  CHECK(mixed.b == std::lround(a.b * 0.55 + 255 * 0.45));
  // Outside the transition A wins again, even though B has a clip.
  CHECK(mpp::render_frame(p, cache, 9).pixel(20, 60) == counter(9));
  CHECK(mpp::render_frame(p, cache, 25).pixel(20, 60) == Color{0, 0, 255, 255});

  // Pointing the other way, it goes from B to A.
  mpp::TransitionSettings back;
  back.b_to_a = true;
  e.execute(std::make_unique<mpp::SetTransitionSettingsCommand>(p.transitions[0].id, back));
  const mpp::Color reversed = mpp::render_frame(p, cache, 14).pixel(20, 60);
  CHECK(reversed.r == std::lround(a.r * 0.45));
}

TEST_CASE("the Super track is keyed and faded over the program", "[render][keys]") {
  mpp::ProjectSettings settings;
  settings.width = 160;
  settings.height = 120;
  settings.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov", "still.png"}, settings));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 30));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::Superimpose, 0, 0, 30));
  const int super = e.project().clip_at(Track::Superimpose, 0)->id;
  mpp::MediaCache cache;
  const mpp::Project& p = e.project();
  // No key: the still covers A, but its letterbox doesn't.
  CHECK(mpp::render_frame(p, cache, 3).pixel(20, 60) == Color{0, 0, 255, 255});
  CHECK(mpp::render_frame(p, cache, 3).pixel(20, 5) == counter(3));

  mpp::TransparencyKey blue;
  blue.type = mpp::KeyType::BlueScreen;
  e.execute(std::make_unique<mpp::SetKeyCommand>(super, blue));
  CHECK(mpp::render_frame(p, cache, 3).pixel(20, 60) == counter(3));
  CHECK(mpp::render_frame(p, cache, 3).pixel(140, 60) == Color{255, 255, 0, 255});

  // Fade from full at source frame 0 to nothing at 20: half way at 10.
  e.execute(std::make_unique<mpp::SetFadeCommand>(
      super, std::vector<mpp::FadePoint>{{0, 1.0}, {20, 0.0}}));
  const Color half = mpp::render_frame(p, cache, 10).pixel(140, 60);
  const Color a = counter(10);
  CHECK(half.r == std::lround((255 + a.r) / 2.0));
  CHECK(half.b == std::lround((0 + a.b) / 2.0));
}

TEST_CASE("filters run on each clip, blending start to end", "[render][filters]") {
  mpp::ProjectSettings settings;
  settings.width = 160;
  settings.height = 120;
  settings.rate = {15, 1, false};
  mpp::Editor e(project_with({"still.png"}, settings));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 11));
  const int id = e.project().clips[0].id;
  // Brightness from -100 on the first frame to +100 on the last.
  e.execute(std::make_unique<mpp::SetFiltersCommand>(
      id, std::vector<mpp::FilterInstance>{{"brightness_contrast", {-100, 0}, {100, 0}}}));
  mpp::MediaCache cache;
  const mpp::Project& p = e.project();
  CHECK(mpp::render_frame(p, cache, 0).pixel(140, 60) == Color{155, 155, 0, 255});
  CHECK(mpp::render_frame(p, cache, 5).pixel(140, 60) == Color{255, 255, 0, 255});
  CHECK(mpp::render_frame(p, cache, 10).pixel(20, 60) == Color{100, 100, 255, 255});
  // Filters don't touch the letterbox.
  CHECK(mpp::render_frame(p, cache, 10).pixel(80, 5) == mpp::kBlack);
}

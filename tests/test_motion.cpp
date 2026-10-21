#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/audio_mixer.h"
#include "mpp/editor.h"
#include "mpp/motion.h"
#include "mpp/renderer.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

using mpp::Color;
using mpp::Image;
using mpp::Motion;
using mpp::MotionPoint;
using mpp::Track;

namespace {

Motion still_at(double x, double y, double rotation = 0, double zoom = 100) {
  Motion m;
  m.points.push_back({0, x, y, rotation, zoom});
  return m;
}

// counter frame N: (N*8, 255-N*8, 64).
Color counter(int n) {
  return {static_cast<std::uint8_t>(n * 8), static_cast<std::uint8_t>(255 - n * 8), 64, 255};
}

}  // namespace

TEST_CASE("motion points blend in straight lines", "[motion]") {
  const Motion m = mpp::default_motion();
  REQUIRE(m.points.size() == 2);
  CHECK(mpp::motion_at(m, 0).x == -0.5);
  CHECK(mpp::motion_at(m, 0.5).x == 0.5);
  Motion three;
  three.points = {{0, 0.0, 0.0, 0, 100}, {0.5, 1.0, 0.0, 90, 50}, {1, 1.0, 1.0, 90, 100}};
  const MotionPoint q = mpp::motion_at(three, 0.25);
  CHECK(q.x == 0.5);
  CHECK(q.rotation == 45);
  CHECK(q.zoom == 75);
  CHECK(mpp::motion_at(three, 0.75).y == 0.5);
  CHECK(mpp::motion_at(three, 2).y == 1.0);
}

TEST_CASE("pictures are placed, turned and scaled exactly", "[motion]") {
  Image pic = make_test_pattern(40, 30);
  // Centred and plain, a motion point changes nothing.
  const Image plain = mpp::place_picture(pic, nullptr, 0, 80, 60);
  const Motion centre = still_at(0.5, 0.5);
  CHECK(mpp::place_picture(pic, &centre, 0, 80, 60) == plain);
  CHECK(plain.pixel(20, 15) == pic.pixel(0, 0));
  // Off the left edge: nothing shows.
  const Motion gone = still_at(-0.5, 0.5);
  CHECK(mpp::place_picture(pic, &gone, 0, 80, 60) == Image(80, 60));
  // Moved right by a quarter of the frame: 20 pixels.
  const Motion right = still_at(0.75, 0.5);
  CHECK(mpp::place_picture(pic, &right, 0, 80, 60).pixel(40, 15) == pic.pixel(0, 0));
  // A quarter turn clockwise takes the top-left corner to the top-right.
  Image square(20, 20, mpp::kWhite);
  square.fill({0, 0, 4, 4}, {255, 0, 0, 255});
  const Motion turned = still_at(0.5, 0.5, 90);
  const Image t = mpp::place_picture(square, &turned, 0, 20, 20);
  CHECK(t.pixel(18, 1) == Color{255, 0, 0, 255});
  CHECK(t.pixel(1, 1) == mpp::kWhite);
  // Half size leaves the corners clear.
  const Motion half = still_at(0.5, 0.5, 0, 50);
  const Image h = mpp::place_picture(square, &half, 0, 20, 20);
  CHECK(h.pixel(1, 1).a == 0);
  CHECK(h.pixel(10, 10) == mpp::kWhite);
}

TEST_CASE("motion matches its golden", "[motion][golden]") {
  const Image pic = make_test_pattern(48, 36);
  Image sheet(4 * 66, 48, mpp::kWhite);
  const Motion samples[] = {still_at(0.3, 0.4, 0, 100), still_at(0.5, 0.5, 30, 100),
                            still_at(0.6, 0.6, -15, 60), still_at(0.5, 0.5, 180, 130)};
  int x = 0;
  for (const Motion& m : samples) {
    sheet.blit(mpp::place_picture(pic, &m, 0, 64, 48), {x, 0});
    x += 66;
  }
  CHECK(golden_mismatch(sheet, "motion_samples") == "");
}

TEST_CASE("a clip moves along its path", "[motion][render]") {
  mpp::ProjectSettings s;
  s.width = 160;
  s.height = 120;
  s.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 21));
  const int id = e.project().clip_at(Track::VideoA, 0)->id;
  e.execute(std::make_unique<mpp::SetMotionCommand>(id, mpp::default_motion()));
  mpp::MediaCache cache;
  const mpp::Project& p = e.project();
  CHECK(mpp::render_frame(p, cache, 0) == Image(160, 120, mpp::kBlack));  // off the left
  CHECK(mpp::render_frame(p, cache, 10).pixel(80, 60) == counter(10));    // centred half way
  CHECK(mpp::render_frame(p, cache, 15).pixel(10, 60) == mpp::kBlack);    // gone right
  CHECK(mpp::render_frame(p, cache, 15).pixel(150, 60) == counter(15));

  Motion bad;
  bad.points = {{0.5, 0, 0, 0, 100}, {0.2, 0, 0, 0, 100}};
  CHECK_THROWS(e.execute(std::make_unique<mpp::SetMotionCommand>(id, bad)));
  e.execute(std::make_unique<mpp::SetMotionCommand>(id, std::nullopt));
  CHECK(mpp::render_frame(p, cache, 0).pixel(80, 60) == counter(0));
}

TEST_CASE("speed stretches or squeezes a clip and its sound", "[speed]") {
  mpp::ProjectSettings s;
  s.width = 160;
  s.height = 120;
  s.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov", "still.png"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 20));
  const int id = e.project().clip_at(Track::VideoA, 0)->id;
  const int sound = e.project().clip(id)->link;
  e.execute(std::make_unique<mpp::SetSpeedCommand>(id, 50));
  const mpp::Project& p = e.project();
  CHECK(p.clip(id)->length() == 40);
  CHECK(p.clip(sound)->speed == 50);
  CHECK(p.clip(sound)->length() == 40);
  mpp::MediaCache cache;
  CHECK(mpp::render_frame(p, cache, 7).pixel(80, 60) == counter(3));  // 3.5 shows 3
  CHECK(mpp::render_frame(p, cache, 38).pixel(80, 60) == counter(19));

  // Half speed reads the sound half as fast.
  const auto source = cache.audio(fixture("counter.mov"), 22050);
  const mpp::AudioBuffer slow = mpp::clip_audio(p, cache, *p.clip(sound));
  CHECK(slow.frames() == 58800);
  CHECK(slow.samples[200 * 2] == source->samples[100 * 2]);
  CHECK(std::abs(slow.samples[201 * 2] - (source->samples[100 * 2] + source->samples[101 * 2]) / 2) < 1e-6f);

  // Double speed, half as long.
  e.execute(std::make_unique<mpp::SetSpeedCommand>(id, 200));
  CHECK(p.clip(id)->length() == 10);
  CHECK(mpp::render_frame(p, cache, 4).pixel(80, 60) == counter(8));

  // Slowing down into the next clip is refused.
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoA, 30, 0, 10));
  CHECK_THROWS(e.execute(std::make_unique<mpp::SetSpeedCommand>(id, 50)));
  CHECK(p.clip(id)->speed == 200);
  CHECK_THROWS(e.execute(std::make_unique<mpp::SetSpeedCommand>(id, 0)));
}

TEST_CASE("the razor cuts slowed clips where a source frame starts", "[speed]") {
  mpp::Editor e(project_with({"h264.mp4"}));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 0, 0, 30));
  const int id = e.project().clips[0].id;
  e.execute(std::make_unique<mpp::SetSpeedCommand>(id, 50));  // 60 frames long
  e.execute(std::make_unique<mpp::RazorCommand>(id, 21));      // source 10.5 rounds to 11
  const auto pieces = e.project().clips_on(Track::VideoA);
  REQUIRE(pieces.size() == 2);
  CHECK(pieces[0]->out == 11);
  CHECK(pieces[0]->end() == 22);
  CHECK(pieces[1]->start == 22);
  CHECK(pieces[1]->in == 11);
  CHECK(pieces[1]->end() == 60);
}

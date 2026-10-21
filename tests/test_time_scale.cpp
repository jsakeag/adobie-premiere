#include <catch2/catch_test_macros.hpp>

#include "mpp/time_scale.h"

TEST_CASE("the time scale maps frames to pixels", "[scale]") {
  mpp::TimeScale s;
  s.px_per_frame = 4;
  s.scroll = 10;
  CHECK(s.x_of(10) == 0);
  CHECK(s.x_of(15) == 20);
  CHECK(s.frame_at(21) == 15);
  CHECK(s.frame_at(19.9) == 14);
  CHECK(s.boundary_at(19) == 15);
}

TEST_CASE("ruler ticks are round steps", "[scale]") {
  const mpp::Rate r30{30, 1, false};
  CHECK(mpp::tick_step(r30, 16, 60) == 5);
  CHECK(mpp::tick_step(r30, 4, 60) == 15);
  CHECK(mpp::tick_step(r30, 1, 60) == 60);
  CHECK(mpp::tick_step(r30, 0.03125, 60) == 30 * 120);  // a minute is only 56 px
  CHECK(mpp::tick_step({15, 1, false}, 4, 60) == 15);
}

TEST_CASE("edits snap to nearby edges", "[scale]") {
  mpp::Project p;
  mpp::Clip a;
  a.id = 1;
  a.start = 10;
  a.out = 20;  // covers 10..30
  p.clips.push_back(a);
  const auto points = mpp::snap_points(p, 50, {});
  CHECK(mpp::snap(28, points, 3) == 30);
  CHECK(mpp::snap(26, points, 3) == 26);
  CHECK(mpp::snap(48, points, 3) == 50);
  // A 10-frame span whose tail is near 30 lands ending on it.
  CHECK(mpp::snap_span(22, 10, points, 3) == 20);
  // Its head near 30 wins when closer.
  CHECK(mpp::snap_span(31, 10, points, 3) == 30);
  CHECK(mpp::snap_span(60, 5, points, 3) == 60);
  // Ignored clips don't attract.
  CHECK(mpp::snap(29, mpp::snap_points(p, 50, {1}), 3) == 29);
}

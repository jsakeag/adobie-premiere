#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/compositing.h"
#include "mpp/transitions.h"

using mpp::Color;
using mpp::Image;
using mpp::TransitionSettings;

namespace {

// Outgoing: a blue-green gradient with a white corner marker.
Image from_picture(int w, int h) {
  Image img(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      img.set_pixel(x, y, {20, static_cast<std::uint8_t>(60 + x * 150 / w),
                           static_cast<std::uint8_t>(120 + y * 120 / h), 255});
    }
  }
  img.fill({2, 2, 6, 6}, mpp::kWhite);
  return img;
}

// Incoming: orange and yellow checks with a black corner marker.
Image to_picture(int w, int h) {
  Image img(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool odd = (x / 8 + y / 8) % 2;
      img.set_pixel(x, y, odd ? Color{250, 200, 40, 255} : Color{230, 110, 30, 255});
    }
  }
  img.fill({w - 8, h - 8, 6, 6}, mpp::kBlack);
  return img;
}

}  // namespace

TEST_CASE("there are 24 transitions with distinct ids", "[transitions]") {
  const auto& all = mpp::transitions();
  CHECK(all.size() == 24);
  std::set<std::string> ids, names;
  for (const auto& t : all) {
    ids.insert(t.id);
    names.insert(t.name);
    CHECK(mpp::find_transition(t.id) == &t);
  }
  CHECK(ids.size() == 24);
  CHECK(names.size() == 24);
  CHECK(mpp::find_transition("page_curl") == nullptr);
}

TEST_CASE("every transition starts on from and ends on to", "[transitions]") {
  const Image a = from_picture(64, 48), b = to_picture(64, 48);
  for (const auto& def : mpp::transitions()) {
    const int directions = std::max<int>(1, static_cast<int>(def.directions.size()));
    for (int d = 0; d < directions; ++d) {
      for (int border : {0, 4}) {
        for (bool reverse : {false, true}) {
          TransitionSettings s;
          s.direction = d;
          s.border = border;
          s.reverse = reverse;
          INFO(def.id << " direction " << d << " border " << border << " reverse " << reverse);
          CHECK(mpp::apply_transition(def, a, b, 0.0, s) == a);
          CHECK(mpp::apply_transition(def, a, b, 1.0, s) == b);
          const Image mid = mpp::apply_transition(def, a, b, 0.5, s);
          CHECK(mid.width() == 64);
          CHECK(mid.height() == 48);
          CHECK(mid != a);
          CHECK(mid != b);
        }
      }
    }
  }
}

TEST_CASE("reverse runs a transition backwards with the roles swapped", "[transitions]") {
  const Image a = from_picture(40, 30), b = to_picture(40, 30);
  const auto* iris = mpp::find_transition("iris_round");
  TransitionSettings reversed;
  reversed.reverse = true;
  CHECK(mpp::apply_transition(*iris, a, b, 0.3, reversed) ==
        mpp::apply_transition(*iris, b, a, 0.7, {}));
  // A reversed iris closes on the outgoing picture: the centre still shows it.
  CHECK(mpp::apply_transition(*iris, a, b, 0.3, reversed).pixel(20, 15) == a.pixel(20, 15));
  CHECK_THROWS(mpp::apply_transition(*iris, a, Image(10, 10), 0.5, {}));
}

TEST_CASE("simple transitions have exact values", "[transitions]") {
  const Image black(4, 2, mpp::kBlack), white(4, 2, mpp::kWhite);
  const auto run = [&](const char* id, double p, TransitionSettings s = {}) {
    return mpp::apply_transition(*mpp::find_transition(id), black, white, p, s);
  };
  CHECK(run("cross_dissolve", 0.25).pixel(0, 0) == Color{64, 64, 64, 255});
  CHECK(run("additive_dissolve", 0.5).pixel(0, 0) == mpp::kWhite);
  // A wipe from the left halfway shows white on the left half only.
  const Image half = run("wipe", 0.5);
  CHECK(half.pixel(1, 0) == mpp::kWhite);
  CHECK(half.pixel(2, 0) == mpp::kBlack);
  TransitionSettings from_right;
  from_right.direction = 1;
  CHECK(run("wipe", 0.5, from_right).pixel(3, 1) == mpp::kWhite);
  CHECK(run("wipe", 0.5, from_right).pixel(0, 1) == mpp::kBlack);
  // Push moves the outgoing picture along: a one-pixel push of a 4-wide
  // frame at 25%.
  Image ramp(4, 1);
  for (int x = 0; x < 4; ++x) ramp.set_pixel(x, 0, {static_cast<std::uint8_t>(x * 10), 0, 0, 255});
  const Image pushed =
      mpp::apply_transition(*mpp::find_transition("push"), ramp, Image(4, 1, mpp::kWhite), 0.25, {});
  CHECK(pushed.pixel(0, 0) == mpp::kWhite);
  CHECK(pushed.pixel(1, 0).r == 0);
  CHECK(pushed.pixel(3, 0).r == 20);
}

TEST_CASE("progress runs from start to end at frame middles", "[transitions]") {
  TransitionSettings s;
  CHECK(mpp::transition_progress(s, 0, 10) == 0.05);
  CHECK(mpp::transition_progress(s, 9, 10) == 0.95);
  s.start = 0.2;
  s.end = 0.6;
  CHECK(mpp::transition_progress(s, 0, 2) == 0.3);
  CHECK(mpp::transition_progress(s, 1, 2) == 0.5);
}

TEST_CASE("each transition matches its golden", "[transitions][golden]") {
  // Four panels: 25%, 50% and 75% with the defaults, then 50% with the last
  // direction and a black border where those apply.
  const int w = 64, h = 48;
  const Image a = from_picture(w, h), b = to_picture(w, h);
  for (const auto& def : mpp::transitions()) {
    Image sheet(4 * w + 3 * 2, h, {255, 0, 255, 255});
    int x = 0;
    for (double p : {0.25, 0.5, 0.75}) {
      sheet.blit(mpp::apply_transition(def, a, b, p, {}), {x, 0});
      x += w + 2;
    }
    TransitionSettings s;
    s.direction = static_cast<int>(def.directions.size()) - 1;
    s.border = 3;
    s.border_color = mpp::kBlack;
    sheet.blit(mpp::apply_transition(def, a, b, 0.5, s), {x, 0});
    INFO(def.id);
    CHECK(golden_mismatch(sheet, "transition_" + def.id) == "");
  }
}

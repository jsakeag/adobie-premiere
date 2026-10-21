#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/bin_commands.h"
#include "mpp/editor.h"
#include "mpp/renderer.h"
#include "mpp/timeline_commands.h"
#include "mpp/title.h"
#include "project_fixtures.h"

using mpp::Color;
using mpp::TitleSpec;
using mpp::TitleText;

namespace {

TitleText text(const char* s, double x = 0.5, double y = 0.5) {
  TitleText t;
  t.text = s;
  t.x = x;
  t.y = y;
  t.shadow = false;
  return t;
}

bool any_ink(const mpp::Image& img, mpp::Rect r) {
  for (int y = std::max(0, r.y); y < std::min(img.height(), r.bottom()); ++y) {
    for (int x = std::max(0, r.x); x < std::min(img.width(), r.right()); ++x) {
      if (img.pixel(x, y).a > 0) return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("an empty title is its background", "[title]") {
  TitleSpec t;
  CHECK(mpp::render_title(t, 8, 6) == mpp::Image(8, 6, mpp::kTransparent));
  t.background = {10, 20, 30, 255};
  CHECK(mpp::render_title(t, 8, 6).pixel(3, 3) == Color{10, 20, 30, 255});
}

TEST_CASE("text lands inside its bounds and scales with the frame", "[title]") {
  TitleSpec t;
  t.texts.push_back(text("Premiere"));
  const mpp::Image img = mpp::render_title(t, 320, 240);
  const mpp::Rect box = mpp::text_bounds(t.texts[0], 320, 240);
  CHECK(box.width > 60);
  CHECK(std::abs(box.x + box.width / 2 - 160) <= 1);  // centred
  CHECK(std::abs(box.y + box.height / 2 - 120) <= 1);
  CHECK(any_ink(img, box));
  CHECK_FALSE(any_ink(img, {0, 0, 320, box.y - 1}));
  CHECK_FALSE(any_ink(img, {0, box.bottom() + 1, 320, 240}));
  CHECK_FALSE(any_ink(img, {box.right() + 2, 0, 320, 240}));

  const mpp::Rect big = mpp::text_bounds(t.texts[0], 640, 480);
  CHECK(std::abs(big.width - 2 * box.width) <= 2);
  CHECK(std::abs(big.height - 2 * box.height) <= 2);

  // Left and right alignment put that edge on x.
  TitleText left = text("Left", 0.1);
  left.align = mpp::TextAlign::Left;
  CHECK(mpp::text_bounds(left, 320, 240).x == 32);
  TitleText right = text("Right", 0.9);
  right.align = mpp::TextAlign::Right;
  CHECK(std::abs(mpp::text_bounds(right, 320, 240).right() - 288) <= 1);
  // More lines, taller block; accents are fine.
  CHECK(mpp::text_bounds(text("one\ntwo"), 320, 240).height >= 2 * box.height - 1);
  CHECK(mpp::text_bounds(text("Café"), 320, 240).width > 0);
}

TEST_CASE("a title matches its golden", "[title][golden]") {
  TitleSpec t;
  t.background = {20, 30, 90, 255};
  mpp::TitleShape bar;
  bar.y = 0.78;
  bar.width = 0.9;
  bar.height = 0.18;
  bar.color = {250, 200, 40, 200};
  t.shapes.push_back(bar);
  mpp::TitleShape ring;
  ring.kind = mpp::ShapeKind::Oval;
  ring.x = 0.82;
  ring.y = 0.22;
  ring.width = 0.22;
  ring.height = 0.3;
  ring.filled = false;
  ring.line = 0.02;
  t.shapes.push_back(ring);
  TitleText head = text("Mock Premiere", 0.5, 0.42);
  head.size = 0.16;
  head.shadow = true;
  t.texts.push_back(head);
  TitleText sub = text("version 2.0", 0.08, 0.78);
  sub.font = mpp::TitleFont::Regular;
  sub.size = 0.09;
  sub.align = mpp::TextAlign::Left;
  sub.color = {20, 20, 20, 255};
  t.texts.push_back(sub);
  CHECK(golden_mismatch(mpp::render_title(t, 320, 240), "title_sample") == "");
}

TEST_CASE("titles are bin items drawn at the project's size", "[title]") {
  mpp::ProjectSettings settings;
  settings.width = 160;
  settings.height = 120;
  settings.rate = {15, 1, false};
  mpp::Editor e(project_with({"counter.mov"}, settings));
  TitleSpec t;
  mpp::TitleShape box;
  box.width = box.height = 0.5;
  box.color = {255, 0, 0, 255};
  t.shapes.push_back(box);
  e.execute(std::make_unique<mpp::AddItemCommand>(mpp::make_title_item(t, "Red Box", settings)));
  const mpp::MediaItem& title = e.project().bin.back();
  CHECK(title.info.kind == mpp::MediaKind::Title);
  CHECK(title.out == settings.still_frames);
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, mpp::Track::VideoA, 0, 0, 30));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(title.id, mpp::Track::Superimpose, 0, 0, 30));
  mpp::TransparencyKey alpha;
  alpha.type = mpp::KeyType::Alpha;
  e.execute(std::make_unique<mpp::SetKeyCommand>(e.project().clip_at(mpp::Track::Superimpose, 0)->id, alpha));
  mpp::MediaCache cache;
  const mpp::Image frame = mpp::render_frame(e.project(), cache, 4);
  CHECK(frame.pixel(80, 60) == Color{255, 0, 0, 255});  // the box
  CHECK(frame.pixel(5, 5) == Color{32, 223, 64, 255});  // counter frame 4 around it

  t.texts.push_back(text("Hi"));
  e.execute(std::make_unique<mpp::SetTitleCommand>(title.id, t, "Greeting"));
  CHECK(e.project().item(title.id)->name == "Greeting");
  CHECK(e.project().item(title.id)->title->texts.size() == 1);
  CHECK_THROWS(e.execute(std::make_unique<mpp::SetTitleCommand>(1, t, "Not a title")));
}

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "mpp/bin_commands.h"
#include "mpp/editor.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

using mpp::Track;

namespace {

// counter.mov (1): 60 frames with sound. still.png (2): 150. h264.mp4 (3):
// 90. tone.wav (4): sound only. All at 30 fps.
mpp::Editor sequence_editor() {
  return mpp::Editor(project_with({"counter.mov", "still.png", "h264.mp4", "tone.wav"}));
}

}  // namespace

TEST_CASE("the sequence is a list of bin items", "[sequence]") {
  mpp::Editor e = sequence_editor();
  e.execute(std::make_unique<mpp::SetSequenceCommand>(std::vector<int>{3, 1, 3}));
  CHECK(e.project().sequence == std::vector<int>{3, 1, 3});
  CHECK_THROWS(e.execute(std::make_unique<mpp::SetSequenceCommand>(std::vector<int>{9})));
  CHECK(mpp::sequence_length(e.project(), e.project().sequence, false, 0) == 240);
  CHECK(mpp::sequence_length(e.project(), e.project().sequence, true, 15) == 210);
  e.execute(std::make_unique<mpp::RemoveItemCommand>(3));
  CHECK(e.project().sequence == std::vector<int>{1});
}

TEST_CASE("a sequence with cuts goes end to end on Video A", "[sequence]") {
  mpp::Editor e = sequence_editor();
  e.execute(std::make_unique<mpp::BuildSequenceCommand>(std::vector<int>{1, 3, 2}, 10, "", 0));
  const auto a = e.project().clips_on(Track::VideoA);
  REQUIRE(a.size() == 3);
  CHECK(a[0]->start == 10);
  CHECK(a[1]->start == 70);
  CHECK(a[2]->start == 160);
  CHECK(a[2]->end() == 310);
  CHECK(e.project().clips_on(Track::AudioA).size() == 1);  // counter's sound
  CHECK(e.project().transitions.empty());
  CHECK(e.undo_name() == "Place Sequence");
}

TEST_CASE("a sequence with a transition alternates A and B", "[sequence]") {
  mpp::Editor e = sequence_editor();
  e.execute(std::make_unique<mpp::BuildSequenceCommand>(std::vector<int>{1, 3, 2}, 0, "cross_dissolve", 15));
  const mpp::Project& p = e.project();
  REQUIRE(p.clips_on(Track::VideoA).size() == 2);
  REQUIRE(p.clips_on(Track::VideoB).size() == 1);
  const mpp::Clip* second = p.clips_on(Track::VideoB)[0];
  CHECK(second->start == 45);
  CHECK(p.clips_on(Track::VideoA)[1]->start == 120);
  REQUIRE(p.transitions.size() == 2);
  CHECK(p.transition_at(50)->start == 45);
  CHECK(p.transition_at(50)->length == 15);
  CHECK_FALSE(p.transition_at(50)->settings.b_to_a);  // A out, B in
  CHECK(p.transition_at(125)->settings.b_to_a);       // B out, A in
  CHECK(p.duration() == 270);
}

TEST_CASE("a sequence that won't fit changes nothing", "[sequence]") {
  mpp::Editor e = sequence_editor();
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoA, 100, 0, 30));
  CHECK_THROWS(e.execute(std::make_unique<mpp::BuildSequenceCommand>(std::vector<int>{1, 3}, 0, "", 0)));
  CHECK(e.project().clips.size() == 1);
  CHECK_THROWS(e.execute(std::make_unique<mpp::BuildSequenceCommand>(std::vector<int>{4}, 200, "", 0)));
  CHECK_THROWS(e.execute(std::make_unique<mpp::BuildSequenceCommand>(std::vector<int>{1, 3}, 200, "wipe", 60)));
  CHECK_THROWS(e.execute(std::make_unique<mpp::BuildSequenceCommand>(std::vector<int>{}, 200, "", 0)));
}

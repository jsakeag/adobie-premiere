#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "mpp/bin_commands.h"
#include "mpp/editor.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

using mpp::Clip;
using mpp::Editor;
using mpp::Track;

namespace {

// counter.mov (1): 60 frames at 30 fps with sound. still.png (2).
// tone.wav (3): 30 frames of sound only. h264.mp4 (4): 90 frames, no sound.
Editor editor_with_media() {
  return Editor(project_with({"counter.mov", "still.png", "tone.wav", "h264.mp4"}));
}

template <typename C, typename... Args>
void run(Editor& e, Args&&... args) {
  e.execute(std::make_unique<C>(std::forward<Args>(args)...));
}

const Clip& only_clip_on(const Editor& e, Track t) {
  const auto clips = e.project().clips_on(t);
  REQUIRE(clips.size() == 1);
  return *clips[0];
}

}  // namespace

TEST_CASE("placing a movie with sound links picture and sound", "[timeline]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 10, 5, 45);
  REQUIRE(e.project().clips.size() == 2);
  const Clip& picture = only_clip_on(e, Track::VideoA);
  const Clip& sound = only_clip_on(e, Track::AudioA);
  CHECK(picture.start == 10);
  CHECK(picture.length() == 40);
  CHECK(picture.end() == 50);
  CHECK(sound.start == 10);
  CHECK(sound.in == 5);
  CHECK(picture.link == sound.id);
  CHECK(sound.link == picture.id);
  CHECK(e.project().duration() == 50);

  // The next one's sound goes on the next free audio track.
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoB, 20, 0, 10);
  CHECK(e.project().clips_on(Track::AudioB).size() == 1);
  // Without sound, only the picture.
  run<mpp::PlaceClipCommand>(e, 1, Track::Superimpose, 0, 0, 60, false);
  CHECK(e.project().clips.size() == 5);
}

TEST_CASE("placing refuses what doesn't fit", "[timeline]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 0, 0, 60);
  CHECK_THROWS(run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 30, 0, 10));  // taken
  CHECK_THROWS(run<mpp::PlaceClipCommand>(e, 3, Track::VideoA, 100, 0, 10));  // no picture
  CHECK_THROWS(run<mpp::PlaceClipCommand>(e, 4, Track::AudioC, 100, 0, 10));  // no sound
  CHECK_THROWS(run<mpp::PlaceClipCommand>(e, 1, Track::VideoB, 0, 0, 61));   // past the end
  CHECK_THROWS(run<mpp::PlaceClipCommand>(e, 1, Track::VideoB, -1, 0, 10));
  CHECK_THROWS(run<mpp::PlaceClipCommand>(e, 99, Track::VideoB, 0, 0, 10));
  CHECK(e.project().clips.size() == 2);

  // A still has no end, and sound goes on audio tracks.
  run<mpp::PlaceClipCommand>(e, 2, Track::VideoB, 0, 0, 500);
  run<mpp::PlaceClipCommand>(e, 3, Track::AudioB, 0, 0, 30);
  CHECK(e.project().clips.size() == 4);
  // All audio tracks busy at 0..10 (A: counter, B: tone) once C is used too.
  run<mpp::PlaceClipCommand>(e, 3, Track::AudioC, 0, 0, 30);
  CHECK_THROWS(run<mpp::PlaceClipCommand>(e, 1, Track::Superimpose, 0, 0, 10));
}

TEST_CASE("moving and trimming carry the linked clip", "[timeline]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 0, 0, 30);
  const int picture = only_clip_on(e, Track::VideoA).id;
  const int sound = only_clip_on(e, Track::AudioA).id;

  run<mpp::MoveClipCommand>(e, picture, Track::VideoB, 40);
  CHECK(e.project().clip(picture)->track == Track::VideoB);
  CHECK(e.project().clip(picture)->start == 40);
  CHECK(e.project().clip(sound)->start == 40);
  CHECK(e.project().clip(sound)->track == Track::AudioA);
  CHECK_THROWS(run<mpp::MoveClipCommand>(e, picture, Track::AudioB, 40));
  CHECK_THROWS(run<mpp::MoveClipCommand>(e, sound, Track::VideoA, 40));
  run<mpp::MoveClipCommand>(e, sound, Track::AudioC, 50);
  CHECK(e.project().clip(picture)->start == 50);

  // Trim the head by 5 frames: start and in move together.
  run<mpp::TrimClipCommand>(e, picture, 55, 5, 30);
  CHECK(e.project().clip(sound)->start == 55);
  CHECK(e.project().clip(sound)->in == 5);
  CHECK(e.project().clip(picture)->end() == 80);
  CHECK_THROWS(run<mpp::TrimClipCommand>(e, picture, 45, -5, 30));  // before the source
  CHECK_THROWS(run<mpp::TrimClipCommand>(e, picture, 55, 5, 5));    // empty

  // A move onto another clip is refused and leaves both alone.
  run<mpp::PlaceClipCommand>(e, 4, Track::VideoA, 0, 0, 90);
  const int other = only_clip_on(e, Track::VideoA).id;
  CHECK_THROWS(run<mpp::MoveClipCommand>(e, picture, Track::VideoA, 0));
  CHECK(e.project().clip(other)->start == 0);
  CHECK(e.project().clip(picture)->track == Track::VideoB);
}

TEST_CASE("the razor splits a clip and its sound", "[timeline]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 10, 0, 40);
  const int picture = only_clip_on(e, Track::VideoA).id;
  CHECK_THROWS(run<mpp::RazorCommand>(e, picture, 10));  // at the edge
  run<mpp::RazorCommand>(e, picture, 25);
  const auto pictures = e.project().clips_on(Track::VideoA);
  const auto sounds = e.project().clips_on(Track::AudioA);
  REQUIRE(pictures.size() == 2);
  REQUIRE(sounds.size() == 2);
  CHECK(pictures[0]->end() == 25);
  CHECK(pictures[1]->start == 25);
  CHECK(pictures[1]->in == 15);
  CHECK(pictures[1]->out == 40);
  CHECK(pictures[0]->link == sounds[0]->id);
  CHECK(pictures[1]->link == sounds[1]->id);
  CHECK(sounds[1]->link == pictures[1]->id);

  // Deleting the right-hand picture takes its sound only.
  run<mpp::ClearCommand>(e, std::vector<int>{pictures[1]->id});
  CHECK(e.project().clips.size() == 2);
  e.undo();
  CHECK(e.project().clips.size() == 4);
}

TEST_CASE("removing a bin item removes its clips", "[timeline]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 0, 0, 30);
  run<mpp::PlaceClipCommand>(e, 4, Track::VideoB, 0, 0, 30);
  run<mpp::RemoveItemCommand>(e, 1);
  CHECK(e.project().clips.size() == 1);
  CHECK(e.project().clips[0].item == 4);
}

TEST_CASE("a frame rate change keeps clips in time and apart", "[timeline]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 0, 0, 31);
  run<mpp::PlaceClipCommand>(e, 4, Track::VideoA, 31, 0, 29);
  run<mpp::SetWorkAreaCommand>(e, 0, 60);
  mpp::ProjectSettings s = e.project().settings;
  s.rate = {15, 1, false};
  run<mpp::SetSettingsCommand>(e, s);
  const auto clips = e.project().clips_on(Track::VideoA);
  REQUIRE(clips.size() == 2);
  CHECK(clips[0]->start == 0);
  CHECK(clips[0]->end() == 15);
  CHECK(clips[1]->start == 15);
  CHECK(clips[1]->end() == 30);
  CHECK(e.project().work_out == 30);
}

TEST_CASE("the work area needs a frame", "[timeline]") {
  Editor e;
  run<mpp::SetWorkAreaCommand>(e, 5, 50);
  CHECK(e.project().work_in == 5);
  CHECK_THROWS(run<mpp::SetWorkAreaCommand>(e, 5, 5));
}

TEST_CASE("transitions sit on the T track without overlapping", "[timeline][transitions]") {
  Editor e = editor_with_media();
  run<mpp::AddTransitionCommand>(e, "cross_dissolve", 10, 20);
  REQUIRE(e.project().transitions.size() == 1);
  const int id = e.project().transitions[0].id;
  CHECK(e.project().transition_at(29)->id == id);
  CHECK(e.project().transition_at(30) == nullptr);
  CHECK(e.project().duration() == 30);
  CHECK_THROWS(run<mpp::AddTransitionCommand>(e, "cross_dissolve", 25, 10));
  CHECK_THROWS(run<mpp::AddTransitionCommand>(e, "page_curl", 40, 10));
  CHECK_THROWS(run<mpp::AddTransitionCommand>(e, "wipe", 40, 0));

  run<mpp::SetTransitionSpanCommand>(e, id, 50, 5);
  CHECK(e.project().transition(id)->start == 50);
  mpp::TransitionSettings s;
  s.b_to_a = true;
  s.border = 2;
  run<mpp::SetTransitionSettingsCommand>(e, id, s);
  CHECK(e.project().transition(id)->settings.b_to_a);
  s.end = 1.5;
  CHECK_THROWS(run<mpp::SetTransitionSettingsCommand>(e, id, s));

  run<mpp::ClearCommand>(e, std::vector<int>{id});
  CHECK(e.project().transitions.empty());
  e.undo();
  CHECK(e.project().transitions.size() == 1);
}

TEST_CASE("a dropped transition fits the A/B overlap", "[timeline][transitions]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 4, Track::VideoA, 0, 0, 60);
  run<mpp::PlaceClipCommand>(e, 2, Track::VideoB, 45, 0, 100);
  const auto span = mpp::suggest_transition_span(e.project(), 50, 30);
  CHECK(span.start == 45);
  CHECK(span.length == 15);
  CHECK_FALSE(span.b_to_a);  // A ends first, so it goes out
  const auto alone = mpp::suggest_transition_span(e.project(), 120, 30);
  CHECK(alone.start == 120);
  CHECK(alone.length == 30);
  // B going out to A.
  run<mpp::MoveClipCommand>(e, e.project().clips_on(Track::VideoA)[0]->id, Track::VideoA, 130);
  CHECK(mpp::suggest_transition_span(e.project(), 132, 30).b_to_a);
}

TEST_CASE("fades and keys are commands", "[timeline][fade]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 2, Track::Superimpose, 0, 0, 60);
  const int id = only_clip_on(e, Track::Superimpose).id;
  run<mpp::SetFadeCommand>(e, id, std::vector<mpp::FadePoint>{{50, 0.0}, {10, 1.0}});
  REQUIRE(e.project().clip(id)->fade.size() == 2);
  CHECK(e.project().clip(id)->fade[0].position == 10);  // sorted
  CHECK_THROWS(run<mpp::SetFadeCommand>(e, id, std::vector<mpp::FadePoint>{{10, 1.5}}));
  mpp::TransparencyKey k;
  k.type = mpp::KeyType::BlueScreen;
  run<mpp::SetKeyCommand>(e, id, k);
  CHECK(e.project().clip(id)->key.type == mpp::KeyType::BlueScreen);
  k.blend = 2;
  CHECK_THROWS(run<mpp::SetKeyCommand>(e, id, k));
  e.undo();
  CHECK(e.project().clip(id)->key.type == mpp::KeyType::None);
}

TEST_CASE("filters suit their track and stay in range", "[timeline][filters]") {
  Editor e = editor_with_media();
  run<mpp::PlaceClipCommand>(e, 1, Track::VideoA, 0, 0, 30);
  const int picture = only_clip_on(e, Track::VideoA).id;
  const int sound = only_clip_on(e, Track::AudioA).id;
  mpp::FilterInstance blur = mpp::default_instance("gaussian_blur");
  blur.end = {10};
  run<mpp::SetFiltersCommand>(e, picture, std::vector<mpp::FilterInstance>{blur, mpp::default_instance("invert")});
  CHECK(e.project().clip(picture)->filters.size() == 2);
  CHECK(e.project().clip(sound)->filters.empty());  // the link doesn't share filters
  CHECK_THROWS(run<mpp::SetFiltersCommand>(e, picture, std::vector<mpp::FilterInstance>{mpp::default_instance("echo")}));
  CHECK_THROWS(run<mpp::SetFiltersCommand>(e, sound, std::vector<mpp::FilterInstance>{blur}));
  blur.end = {99};
  CHECK_THROWS(run<mpp::SetFiltersCommand>(e, picture, std::vector<mpp::FilterInstance>{blur}));
  CHECK_THROWS(run<mpp::SetFiltersCommand>(e, picture, std::vector<mpp::FilterInstance>{{"invert", {1}, {1}}}));
  run<mpp::SetFiltersCommand>(e, sound, std::vector<mpp::FilterInstance>{mpp::default_instance("backwards")});
  CHECK(e.project().clip(sound)->filters.size() == 1);
  // A razor cut gives both pieces the filters.
  run<mpp::RazorCommand>(e, picture, 10);
  for (const mpp::Clip* c : e.project().clips_on(Track::VideoA)) CHECK(c->filters.size() == 2);
}

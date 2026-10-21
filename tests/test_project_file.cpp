#include <filesystem>
#include <fstream>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "mpp/editor.h"
#include "mpp/project_file.h"
#include "mpp/timeline_commands.h"
#include "project_fixtures.h"

namespace fs = std::filesystem;
using mpp::Track;

namespace {

mpp::Project sample() {
  mpp::ProjectSettings s;
  s.rate = {30000, 1001, true};
  s.audio_bits = 16;
  s.audio_rate = 44100;
  mpp::Editor e(project_with({"counter.mov", "still.png", "tone.wav"}, s));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(1, Track::VideoA, 3, 2, 40));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(2, Track::VideoB, 0, 0, 90));
  e.execute(std::make_unique<mpp::PlaceClipCommand>(3, Track::AudioC, 7, 0, 20));
  e.execute(std::make_unique<mpp::SetWorkAreaCommand>(1, 77));
  mpp::TransitionSettings t;
  t.start = 0.25;
  t.end = 0.75;
  t.b_to_a = true;
  t.reverse = true;
  t.border = 3;
  t.border_color = {10, 20, 30, 255};
  t.direction = 2;
  e.execute(std::make_unique<mpp::AddTransitionCommand>("wipe", 20, 12, t));
  const int clip = e.project().clip_at(Track::VideoA, 3)->id;
  e.execute(std::make_unique<mpp::SetFadeCommand>(
      clip, std::vector<mpp::FadePoint>{{2.5, 0.25}, {30, 0.75}}));
  mpp::TransparencyKey k;
  k.type = mpp::KeyType::Chroma;
  k.color = {1, 2, 3, 255};
  k.similarity = 0.4;
  k.blend = 0.05;
  k.reverse = true;
  e.execute(std::make_unique<mpp::SetKeyCommand>(clip, k));
  return e.project();
}

void check_same(const mpp::Project& a, const mpp::Project& b) {
  CHECK(a.settings == b.settings);
  CHECK(a.next_id == b.next_id);
  CHECK(a.work_in == b.work_in);
  CHECK(a.work_out == b.work_out);
  REQUIRE(a.bin.size() == b.bin.size());
  for (std::size_t i = 0; i < a.bin.size(); ++i) {
    CHECK(a.bin[i].id == b.bin[i].id);
    CHECK(fs::equivalent(a.bin[i].path, b.bin[i].path));
    CHECK(a.bin[i].name == b.bin[i].name);
    CHECK(a.bin[i].in == b.bin[i].in);
    CHECK(a.bin[i].out == b.bin[i].out);
    CHECK(a.bin[i].info.kind == b.bin[i].info.kind);
    CHECK(a.bin[i].info.rate == b.bin[i].info.rate);
    CHECK(a.bin[i].info.frames == b.bin[i].info.frames);
    CHECK(a.bin[i].info.has_audio == b.bin[i].info.has_audio);
  }
  REQUIRE(a.clips.size() == b.clips.size());
  for (std::size_t i = 0; i < a.clips.size(); ++i) {
    CHECK(a.clips[i].id == b.clips[i].id);
    CHECK(a.clips[i].track == b.clips[i].track);
    CHECK(a.clips[i].start == b.clips[i].start);
    CHECK(a.clips[i].in == b.clips[i].in);
    CHECK(a.clips[i].out == b.clips[i].out);
    CHECK(a.clips[i].link == b.clips[i].link);
    CHECK(a.clips[i].fade == b.clips[i].fade);
    CHECK(a.clips[i].key == b.clips[i].key);
  }
  REQUIRE(a.transitions.size() == b.transitions.size());
  for (std::size_t i = 0; i < a.transitions.size(); ++i) {
    CHECK(a.transitions[i].id == b.transitions[i].id);
    CHECK(a.transitions[i].kind == b.transitions[i].kind);
    CHECK(a.transitions[i].start == b.transitions[i].start);
    CHECK(a.transitions[i].length == b.transitions[i].length);
    CHECK(a.transitions[i].settings == b.transitions[i].settings);
  }
}

}  // namespace

TEST_CASE("a project survives a save and load", "[file]") {
  const mpp::Project p = sample();
  check_same(p, mpp::decode_project(mpp::encode_project(p)));

  const fs::path path = fs::path(MPP_TEST_OUTPUT_DIR) / "sample.mpp";
  mpp::save_project(p, path.string());
  check_same(p, mpp::load_project(path.string()));
  CHECK_FALSE(fs::exists(path.string() + ".saving"));
  CHECK(mpp::is_project_path("A.MPP"));
  CHECK_FALSE(mpp::is_project_path("a.mov"));
}

TEST_CASE("media moved along with the project is found again", "[file]") {
  const fs::path root = fs::path(MPP_TEST_OUTPUT_DIR) / "moved";
  fs::remove_all(root);
  fs::create_directories(root / "before" / "media");
  fs::copy_file(fixture("still.png"), root / "before" / "media" / "still.png");
  mpp::Project p = project_with({"still.png"});
  p.bin[0].path = (root / "before" / "media" / "still.png").string();
  mpp::save_project(p, (root / "before" / "show.mpp").string());

  fs::rename(root / "before", root / "after");
  const mpp::Project loaded = mpp::load_project((root / "after" / "show.mpp").string());
  CHECK(fs::exists(loaded.bin[0].path));
  CHECK(loaded.bin[0].path.find("after") != std::string::npos);
}

TEST_CASE("bad project files are refused", "[file]") {
  CHECK_THROWS(mpp::decode_project("not json"));
  CHECK_THROWS(mpp::decode_project("{\"format\": \"mock-photoshop\"}"));
  CHECK_THROWS(mpp::decode_project("{\"format\": \"mock-premiere\", \"version\": 99}"));
  CHECK_THROWS(mpp::decode_project("{\"format\": \"mock-premiere\", \"version\": 1}"));
  CHECK_THROWS(mpp::load_project("/nonexistent/x.mpp"));
}

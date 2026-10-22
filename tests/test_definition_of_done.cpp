#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "golden.h"
#include "mpp/audio_mixer.h"
#include "mpp/bin_commands.h"
#include "mpp/edl.h"
#include "mpp/editor.h"
#include "mpp/export_job.h"
#include "mpp/ffmpeg_source.h"
#include "mpp/filmstrip.h"
#include "mpp/image_io.h"
#include "mpp/image_sequence.h"
#include "mpp/media_cache.h"
#include "mpp/motion.h"
#include "mpp/project_file.h"
#include "mpp/render_job.h"
#include "mpp/renderer.h"
#include "mpp/timeline_commands.h"
#include "mpp/title.h"

namespace fs = std::filesystem;
using mpp::Track;

namespace {

std::string out_path(const std::string& name) { return (fs::path(MPP_TEST_OUTPUT_DIR) / name).string(); }

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The project with some clips taken away, to see what they added.
mpp::Project without(const mpp::Project& p, std::vector<int> ids) {
  mpp::Editor e(p);
  e.execute(std::make_unique<mpp::ClearCommand>(std::move(ids)));
  return e.project();
}

}  // namespace

// CLAUDE.md's definition of done, as one program made through commands.
TEST_CASE("the definition of done, headless", "[done]") {
  mpp::ProjectSettings s;
  s.width = 160;
  s.height = 120;
  s.rate = {15, 1, false};
  s.audio_rate = 22050;
  s.audio_bits = 16;
  mpp::Editor e{mpp::Project{}};
  e.execute(std::make_unique<mpp::SetSettingsCommand>(s));
  mpp::MediaCache cache;

  // Import a movie, a still and an image sequence.
  const auto import = [&](const std::string& path) {
    REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::AddItemCommand>(mpp::make_item(path, cache.info(path), s))));
    return e.project().bin.back().id;
  };
  const int movie = import(fixture("counter.mov"));
  const int still = import(fixture("still.png"));
  const fs::path seq_dir = out_path("done_sequence");
  fs::create_directories(seq_dir);
  for (int i = 1; i <= 10; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "shot_%03d.png", i);
    const auto v = static_cast<std::uint8_t>(i * 20);
    mpp::save_png(mpp::Image(160, 120, {0, v, 255, 255}), (seq_dir / name).string());
  }
  const auto run = mpp::find_image_sequence((seq_dir / "shot_004.png").string());
  REQUIRE(run);
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::AddItemCommand>(
      mpp::make_sequence_item(*run, cache.info(run->path(0)), s))));
  const int numbered = e.project().bin.back().id;
  CHECK(e.project().bin.back().out == 10);

  // Cut them together on A/B with a transition.
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::PlaceClipCommand>(movie, Track::VideoA, 0, 0, 30)));
  const int movie_clip = e.project().clip_at(Track::VideoA, 0)->id;
  CHECK(e.project().clip_at(Track::AudioA, 0) != nullptr);  // its sound came too
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::PlaceClipCommand>(numbered, Track::VideoB, 20, 0, 10)));
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::AddTransitionCommand>("cross_dissolve", 20, 10)));
  const int dissolve = e.project().transitions.back().id;
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::PlaceClipCommand>(still, Track::VideoA, 30, 0, 15)));
  const int still_clip = e.project().clip_at(Track::VideoA, 30)->id;

  // Superimpose a keyed clip with a fade.
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::PlaceClipCommand>(still, Track::Superimpose, 0, 0, 20)));
  const int super = e.project().clip_at(Track::Superimpose, 0)->id;
  mpp::TransparencyKey blue;
  blue.type = mpp::KeyType::BlueScreen;
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::SetKeyCommand>(super, blue)));
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::SetFadeCommand>(
      super, std::vector<mpp::FadePoint>{{0, 0.0}, {10, 1.0}})));

  // A filter, a title and a motion path.
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::SetFiltersCommand>(
      movie_clip, std::vector<mpp::FilterInstance>{mpp::default_instance("invert")})));
  mpp::TitleSpec spec;
  mpp::TitleText words;
  words.text = "THE END";
  spec.texts.push_back(words);
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::AddItemCommand>(mpp::make_title_item(spec, "The End", s))));
  const int title = e.project().bin.back().id;
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::PlaceClipCommand>(title, Track::Superimpose, 40, 0, 15)));
  const int title_clip = e.project().clip_at(Track::Superimpose, 40)->id;
  mpp::TransparencyKey alpha;
  alpha.type = mpp::KeyType::Alpha;
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::SetKeyCommand>(title_clip, alpha)));
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::SetMotionCommand>(title_clip, mpp::default_motion())));

  // Change a clip's speed, and undo it (then redo it).
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::SetSpeedCommand>(still_clip, 50)));
  CHECK(e.project().clip(still_clip)->length() == 30);
  e.undo();
  CHECK(e.project().clip(still_clip)->length() == 15);
  e.redo();
  CHECK(e.project().clip(still_clip)->length() == 30);
  REQUIRE_NOTHROW(e.execute(std::make_unique<mpp::SetWorkAreaCommand>(0, 60)));

  // Save and reopen: the reopened project saves the same file.
  const std::string saved = out_path("done.mpp");
  const std::string resaved = out_path("done_again.mpp");
  mpp::save_project(e.project(), saved);
  const mpp::Project p = mpp::load_project(saved);
  mpp::save_project(p, resaved);
  CHECK(read_text(saved) == read_text(resaved));
  REQUIRE(p.clips.size() == e.project().clips.size());
  CHECK(p.clip(still_clip)->speed == 50);

  // Each part shows in the rendered program.
  const mpp::Color inverted5{255 - 40, 40, 255 - 64, 255};  // counter frame 5, inverted
  CHECK(mpp::render_frame(p, cache, 5).pixel(40, 5) == inverted5);  // the filter, above the letterbox
  CHECK(mpp::render_frame(p, cache, 5).pixel(40, 60) == inverted5);  // blue keyed out
  CHECK(mpp::render_frame(p, cache, 5).pixel(120, 60) != inverted5);  // yellow half faded in
  CHECK(mpp::render_frame(p, cache, 0) == mpp::render_frame(without(p, {super}), cache, 0));  // fade at 0
  CHECK(mpp::render_frame(p, cache, 25) != mpp::render_frame(without(p, {dissolve}), cache, 25));
  CHECK(mpp::render_frame(p, cache, 40) == mpp::render_frame(without(p, {title_clip}), cache, 40));  // off screen
  CHECK(mpp::render_frame(p, cache, 47) != mpp::render_frame(without(p, {title_clip}), cache, 47));

  // Render the work area with sound, as Preview plays it.
  mpp::RenderJob job(p, 0, 60);
  const mpp::RenderedProgram rendered = job.take();
  REQUIRE(rendered.frames.size() == 60);
  CHECK(rendered.frames[25] == mpp::render_frame(p, cache, 25));
  REQUIRE(static_cast<std::int64_t>(rendered.audio.frames()) == mpp::frame_to_sample(p.settings, 60));
  float loudest = 0;
  for (float v : rendered.audio.samples) loudest = std::max(loudest, std::abs(v));
  CHECK(loudest > 0.1f);

  // Export a movie, an EDL and a Filmstrip.
  const std::string movie_path = out_path("done.mov");
  mpp::export_movie(p, cache, movie_path, mpp::MovieFormat::Animation, 0, 60);
  mpp::FfmpegSource made(movie_path);
  REQUIRE(made.info().frames == 60);
  for (int f = 0; f < 60; f += 7) CHECK(made.frame(f) == rendered.frames[static_cast<std::size_t>(f)]);
  CHECK(made.audio(22050).samples == rendered.audio.samples);

  const std::string edl = out_path("done.edl");
  mpp::save_edl(p, "DONE", edl);
  const std::string text = read_text(edl);
  CHECK(text.find("TITLE: DONE") != std::string::npos);
  CHECK(text.find("V     D    010") != std::string::npos);  // the dissolve
  CHECK(text.find("M2") != std::string::npos);              // the slowed still

  const std::string strip = out_path("done_strip.png");
  mpp::export_filmstrip(p, cache, strip, 0, 60);
  const auto info = mpp::read_filmstrip_info(mpp::load_image(strip));
  REQUIRE(info);
  CHECK(info->count == 60);
  CHECK(info->width == 160);
}
